#!/usr/bin/env python3
"""Write netdump.js's JSON for an XC3000-family design as structural Verilog.

    python3 tools/fpga/netlist.py z3805a.json --module z3805a_fpga > z3805a.v

The netlist instantiates the models in tools/fpga/xc3000.v and keeps
the decoder's names: a logic block is its tile ("AB"), an I/O block its
pad ("PAD7"), a net the pin that drives it with "." as "_" ("AB_X",
"PAD7_I").  Each bonded I/O block in use is a port named for its package
pin ("P7"); RESET is the chip's active-Low RESET pin, which resets every
flip-flop (data sheet, 7-8 and 7-9).

A pin the decoder enabled but traced no net to is tied Low and marked
"unrouted"; the decoder's own warnings are repeated in the header.
"""

import argparse
import json
import sys

# The JSON's selection names, as the models' parameters spell them; M is
# mode FGM's merged F and G, which the model gives both outputs.
CLB_SELECTIONS = ("B", "C", "D", "E", "QX", "QY", "F", "G", "M", "DI")
CLB_INPUTS = ("A", "B", "C", "D", "E", "DI", "EC", "K", "RD")
IOB_INPUTS = ("O", "T", "OK", "IK")
# The nets a 3-state buffer drives are wired-AND Longlines, pulled High
# when a pull-up on the line is enabled (7-17).
TRI = "tri"
TRI_PULLED = "tri1"
GLOBAL_RESET = "GR"


class UnsupportedError(Exception):
    """The design uses a block the models do not cover."""


def identifier(pin):
    """A net's Verilog name, from the pin that drives it."""
    return pin.replace(".", "_")


def string(value):
    if value is not None and value not in CLB_SELECTIONS:
        raise UnsupportedError(f"unknown selection {value!r}")
    return f'"{value or ""}"'


def flag(value):
    return "1" if value else "0"


def comment_block(lines):
    return "".join(f"// {line}\n" if line else "//\n" for line in lines)


class Netlist:
    """The Verilog text for one design, built from its JSON."""

    def __init__(self, design):
        self.design = design
        self.nets = design["nets"]

    def net(self, block, pin, net_id):
        """The expression a block's input pin reads."""
        if net_id is None:
            return f"1'b0 /* unrouted {block}.{pin} */"
        return identifier(self.nets[net_id]["name"])

    def connections(self, block, pins, inputs, outputs):
        parts = []
        for pin in inputs:
            source = self.net(block, pin, pins[pin]) if pin in pins else "1'b0"
            parts.append(f".{pin}({source})")
        parts.append(f".{GLOBAL_RESET}({GLOBAL_RESET})")
        for pin in outputs:
            net_id = pins.get(pin)
            # An output whose net reaches no enabled pin stays unconnected.
            name = identifier(self.nets[net_id]["name"]) if net_id is not None else ""
            parts.append(f".{pin}({name})")
        return parts

    def clb(self, tile, clb):
        inputs = clb["lutInputs"]
        parameters = [
            f".F_TABLE(16'h{clb['lut']['F']})",
            f".G_TABLE(16'h{clb['lut']['G']})",
            *(f".F_INPUT{i}({string(inputs['F'][i])})" for i in (1, 2, 3)),
            *(f".G_INPUT{i}({string(inputs['G'][i])})" for i in (1, 2, 3)),
            f".FG_MUX({flag(clb['mode'] != 'FG')})",
            f".X_DATA({string(clb['data']['X'])})",
            f".Y_DATA({string(clb['data']['Y'])})",
            f".X_OUTPUT({string(clb['outputs']['X'])})",
            f".Y_OUTPUT({string(clb['outputs']['Y'])})",
            f".USE_EC({flag(clb['enableClock'])})",
            f".USE_RD({flag(clb['directReset'])})",
            f".K_INVERT({flag(clb['clockInvert'])})",
        ]
        equations = clb["equations"]
        notes = [f"{tile}: mode {clb['mode']}, F = {equations['F']}"]
        if equations["G"]:
            notes.append(f"{' ' * len(tile)}  G = {equations['G']}")
        ports = self.connections(tile, clb["pins"], CLB_INPUTS, ("X", "Y"))
        return comment_block(notes) + instance("xc3000_clb", tile, parameters, ports)

    def iob(self, pad, iob):
        output, input_ = iob["output"], iob["input"]
        parameters = [
            f".OUT_ENABLE({flag(output['enabled'])})",
            f".OUT_REGISTERED({flag(output['enabled'] and output['registered'])})",
            f".OUT_INVERT({flag(output['invert'])})",
            f".OK_INVERT({flag(output['clockInvert'])})",
            f".THREE_STATE({flag(output['threeState'])})",
            f".THREE_STATE_INVERT({flag(output['threeStateInvert'])})",
            f".IN_REGISTERED({flag(input_['registered'])})",
            f".IN_LATCH({flag(input_['storage'] == 'latch')})",
            f".IK_INVERT({flag(input_['clockInvert'])})",
            f".PULLUP({flag(iob['pullup'])})",
        ]
        pad_net = iob["pin"] if iob["bonded"] else f"{pad}_PAD"
        ports = [f".PAD({pad_net})"] + self.connections(pad, iob["pins"], IOB_INPUTS, ("I", "Q"))
        clocks = [f"OK on {output['clockLine']}" if output["clockLine"] else "",
                  f"IK on {input_['clockLine']}" if input_["clockLine"] else ""]
        note = ", ".join(part for part in [f"{pad}: pin {iob['pin']}"] + clocks if part)
        return comment_block([note]) + instance("xc3000_iob", pad, parameters, ports)

    def buffer(self, name, clock):
        pins = clock["pins"]
        ports = [f".I({self.net(name, 'I', pins.get('I'))})",
                 f".O({identifier(self.nets[pins['O']]['name']) if pins.get('O') is not None else ''})"]
        return instance("xc3000_buffer", name, [], ports)

    def tbuf(self, name, tribuf):
        pins = tribuf["pins"]
        ports = [f".I({self.net(name, 'I', pins.get('I'))})",
                 f".T({self.net(name, 'T', pins.get('T'))})",
                 f".O({identifier(self.nets[pins['O']]['name'])})"]
        return instance("xc3000_tbuf", name, [], ports)

    def port_direction(self, iob):
        drives = iob["output"]["enabled"]
        reads = iob["input"]["direct"] or iob["input"]["registered"]
        return "inout" if drives and reads else ("output" if drives else "input")

    def text(self, module):
        design = self.design
        for name in design["clocks"]:
            if name not in ("GCLK", "ACLK"):
                raise UnsupportedError(f"{name} is not modeled")
        pulled = {pullup["pins"].get("O") for pullup in design["pullups"].values()}
        tristated = {tribuf["pins"].get("O") for tribuf in design["tribufs"].values()}
        bonded = sorted((iob for iob in design["iobs"].values() if iob["bonded"] and iob["pins"]),
                        key=lambda iob: int(iob["pin"][1:]))

        header = [
            f"{module}: the {design['part']} design netdump.js decoded, as structural Verilog",
            "written by tools/fpga/netlist.py.  Derived from the vendor's bitstream;",
            f"decoded by {design['decoder']['url']} at {design['decoder']['commit']}.",
            f"Package {design['package']}; options: "
            + ", ".join(f"{key} {value}" for key, value in design["options"].items()) + ".",
            "Models: tools/fpga/xc3000.v.",
        ]
        for entry in design["unrouted"]:
            header.append(f"Unrouted: {entry['pin']}.")
        for entry in design["unloaded"]:
            header.append(f"Unloaded: {entry['pin']} reaches no enabled pin.")
        for pin in design["truncatedTraces"]:
            header.append(f"Truncated: the decoder stopped tracing {pin}'s net at its recursion limit.")
        for message in design["messages"]:
            header.append(f"Decoder: {message}")

        ports = [f"    {self.port_direction(iob)} {iob['pin']}" for iob in bonded]
        ports.append("    input RESET")
        lines = [comment_block(header), "`timescale 1ns / 1ps\n\n",
                 f"module {module} (\n", ",\n".join(ports), "\n);\n",
                 f"    wire {GLOBAL_RESET} = ~RESET;\n"]
        for iob_name, iob in design["iobs"].items():
            if not iob["bonded"] and iob["pins"]:
                lines.append(f"    wire {iob_name}_PAD;\n")
        for net in self.nets:
            kind = "wire"
            if net["id"] in tristated:
                kind = TRI_PULLED if net["id"] in pulled else TRI
            lines.append(f"    {kind} {identifier(net['name'])};\n")
        lines.append("\n")
        blocks = [self.buffer(name, clock) for name, clock in design["clocks"].items()]
        # A block configured but connected to nothing, such as a logic
        # block MakeBits's tie-down set to 0, has nothing to model.
        blocks += [self.iob(pad, iob) for pad, iob in design["iobs"].items() if iob["pins"]]
        blocks += [self.clb(tile, clb) for tile, clb in design["clbs"].items() if clb["pins"]]
        blocks += [self.tbuf(name, tribuf) for name, tribuf in design["tribufs"].items()]
        lines.append("\n".join(indent(block) for block in blocks))
        lines.append("endmodule\n")
        return "".join(lines)


def instance(module, name, parameters, ports):
    head = f"{module} #(\n    " + ",\n    ".join(parameters) + f"\n) {name} (\n" if parameters \
        else f"{module} {name} (\n"
    return head + "    " + ",\n    ".join(ports) + "\n);\n"


def indent(text):
    return "".join(f"    {line}" if line.strip() else line for line in text.splitlines(True))


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("json", help="netdump.js's output")
    parser.add_argument("--module", default="xc3000_design", help="the Verilog module's name")
    arguments = parser.parse_args()
    with open(arguments.json) as file:
        design = json.load(file)
    if "error" in design:
        sys.exit(f"netlist.py: the dump failed: {design['error']}")
    try:
        sys.stdout.write(Netlist(design).text(arguments.module))
    except UnsupportedError as error:
        sys.exit(f"netlist.py: {error}")


if __name__ == "__main__":
    main()
