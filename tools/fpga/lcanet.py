#!/usr/bin/env python3
"""Write netdump.js's JSON for an XC3000-family design as the netlist the
emulator's LCA model executes (crates/smartclock-emu/src/board/fpga/lca.rs).

    python3 -I tools/fpga/lcanet.py z3805a.json --bitstream z3805a.bit > z3805a.netlist

The same decode netlist.py writes as Verilog, in a form the emulator reads
with no toolchain: a line a block, `KEY=VALUE` words, a net by the name of
the pin that drives it ("AB.X", "PAD7.I").  The blocks and their options
are those of tools/fpga/xc3000.v, which the Verilog instantiates, with the
selections that model resolves resolved here, so that the emulator's
evaluator and the Icarus netlist run the same design:

    part XC3042
    bitstream SHA256            the .bit the decode came from
    decoder COMMIT              the decoder's pinned commit
    tri NET pulled|floating     a 3-state Longline, pulled High or not
    clb TILE ...                a logic block
    iob PAD pin=PIN|- ...       an I/O block, bonded to PIN or not
    buffer NAME I=NET O=NET     a clock buffer, GCLK or ACLK
    tbuf NAME I=NET T=NET O=NET a 3-state buffer onto a Longline

A block's input that is not routed, or not used, is left out: it reads
Low, as netlist.py ties it.  An output that reaches no enabled pin is left
out too.
"""

import argparse
import hashlib
import json
import sys

# xc3000.v's selections: a table input B, C, D, E, QX or QY, any other
# reading Low.
TABLE_SELECTIONS = ("B", "C", "D", "E", "QX", "QY")
CLB_INPUTS = ("A", "B", "C", "D", "E", "DI", "EC", "K", "RD")
CLB_OUTPUTS = ("X", "Y")
IOB_INPUTS = ("O", "T", "OK", "IK")
IOB_OUTPUTS = ("I", "Q")
CLOCKS = ("GCLK", "ACLK")
# What "none" is written as.
NONE = "-"


class UnsupportedError(Exception):
    """The design uses a block the models do not cover."""


def flag(value):
    return "1" if value else "0"


def selection(value):
    if value is None:
        return NONE
    if value not in TABLE_SELECTIONS:
        raise UnsupportedError(f"unknown table selection {value!r}")
    return value


def x_data(name):
    """xc3000.v's X_DATA: DI, G, or else F."""
    return {"DI": "DI", "G": "G"}.get(name, "F")


def y_data(name):
    """xc3000.v's Y_DATA: DI, F, or else G."""
    return {"DI": "DI", "F": "F"}.get(name, "G")


class Writer:
    """The netlist's lines for one design, from its JSON."""

    def __init__(self, design):
        self.design = design
        self.nets = design["nets"]

    def name(self, net_id):
        return self.nets[net_id]["name"]

    def connections(self, pins, names):
        return [f"{pin}={self.name(pins[pin])}" for pin in names if pins.get(pin) is not None]

    def clb(self, tile, clb):
        inputs = clb["lutInputs"]
        if inputs["F"][0] != "A" or inputs["G"][0] != "A":
            raise UnsupportedError(f"{tile}: a table's first input is not A")
        words = [
            "clb", tile,
            f"f={clb['lut']['F']}", f"g={clb['lut']['G']}",
            "finputs=" + ",".join(selection(name) for name in inputs["F"][1:]),
            "ginputs=" + ",".join(selection(name) for name in inputs["G"][1:]),
            f"merged={flag(clb['mode'] != 'FG')}",
            f"xdata={x_data(clb['data']['X'])}", f"ydata={y_data(clb['data']['Y'])}",
            f"xout={'QX' if clb['outputs']['X'] == 'QX' else 'F'}",
            f"yout={'QY' if clb['outputs']['Y'] == 'QY' else 'G'}",
            f"ec={flag(clb['enableClock'])}", f"rd={flag(clb['directReset'])}",
            f"kinvert={flag(clb['clockInvert'])}",
        ]
        words += self.connections(clb["pins"], CLB_INPUTS + CLB_OUTPUTS)
        return " ".join(words)

    def iob(self, pad, iob):
        output, input_ = iob["output"], iob["input"]
        words = [
            "iob", pad, f"pin={iob['pin'] if iob['bonded'] else NONE}",
            f"out={flag(output['enabled'])}",
            f"outregistered={flag(output['enabled'] and output['registered'])}",
            f"outinvert={flag(output['invert'])}",
            f"okinvert={flag(output['clockInvert'])}",
            f"threestate={flag(output['threeState'])}",
            f"threestateinvert={flag(output['threeStateInvert'])}",
            f"inregistered={flag(input_['registered'])}",
            f"inlatch={flag(input_['storage'] == 'latch')}",
            f"ikinvert={flag(input_['clockInvert'])}",
            f"pullup={flag(iob['pullup'])}",
        ]
        words += self.connections(iob["pins"], IOB_INPUTS + IOB_OUTPUTS)
        return " ".join(words)

    def lines(self, bitstream):
        design = self.design
        for name in design["clocks"]:
            if name not in CLOCKS:
                raise UnsupportedError(f"{name} is not modeled")
        pulled = {pullup["pins"].get("O") for pullup in design["pullups"].values()}
        tristated = sorted({tribuf["pins"]["O"] for tribuf in design["tribufs"].values()})
        lines = [
            f"# {design['part']} design netdump.js decoded, for the emulator's LCA model,",
            "# written by tools/fpga/lcanet.py.  Derived from the vendor's bitstream.",
            f"part {design['part']}",
            f"bitstream {bitstream}",
            f"decoder {design['decoder']['commit']}",
        ]
        lines += [f"tri {self.name(net)} {'pulled' if net in pulled else 'floating'}"
                  for net in tristated]
        for name, clock in design["clocks"].items():
            lines.append(" ".join(["buffer", name] + self.connections(clock["pins"], ("I", "O"))))
        # A block configured but connected to nothing has nothing to run.
        lines += [self.iob(pad, iob) for pad, iob in design["iobs"].items() if iob["pins"]]
        lines += [self.clb(tile, clb) for tile, clb in design["clbs"].items() if clb["pins"]]
        for name, tribuf in design["tribufs"].items():
            lines.append(" ".join(["tbuf", name] + self.connections(tribuf["pins"], ("I", "T", "O"))))
        return lines


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("json", help="netdump.js's output")
    parser.add_argument("--bitstream", required=True, help="the .bit it was decoded from")
    arguments = parser.parse_args()
    with open(arguments.json) as file:
        design = json.load(file)
    if "error" in design:
        sys.exit(f"lcanet.py: the dump failed: {design['error']}")
    with open(arguments.bitstream, "rb") as file:
        bitstream = hashlib.sha256(file.read()).hexdigest()
    try:
        sys.stdout.write("\n".join(Writer(design).lines(bitstream)) + "\n")
    except UnsupportedError as error:
        sys.exit(f"lcanet.py: {error}")


if __name__ == "__main__":
    main()
