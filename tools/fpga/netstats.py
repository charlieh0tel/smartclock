#!/usr/bin/env python3
"""Summarize a decoded netlist's size, for the figures the FPGA docs quote.

    python3 tools/fpga/netstats.py target/fpga/z3805a.json

Prints one "key value" per line (stable order), so `make check` can diff a
fresh decode against a committed reference and a doc's counts stay honest
(AGENTS.md: every quoted figure is checked or names its command).  A logic
block counts as used when it has a mode and any equation; an I/O block when
the decode gave it a role.  It also gives the used blocks' modes, their
flip-flops, the bonded I/O blocks, and the logic blocks each clock buffer
and pad clocks.
"""
import json
import re
import sys

# A CLB tile is two row/column letters (AA..LL on the 12x12 XC3042); an I/O
# block is PAD<n>.  The decoder gives every tile a default equation, so a
# block counts as used only when the routed nets touch it.
CLB_TILE = re.compile(r"^[A-L][A-L]$")
# A logic block's clock pin, a clock buffer's output and a pad's input,
# as the decode names net endpoints.
CLB_CLOCK = re.compile(r"^[A-L][A-L]\.K$")
CLOCK_OUTPUTS = {"GCLK.O", "ACLK.O"}
PAD_INPUT = re.compile(r"^PAD[0-9]+\.I$")


def endpoints(design):
    """Distinct tiles (the part before the dot) the design configures: every
    net endpoint, plus the tiles whose outputs the decode left unloaded (the
    tie-down blocks are configured logic that happens to drive nothing)."""
    tiles = set()
    for net in design["nets"]:
        for end in net.get("sources", []) + net.get("destinations", []):
            tiles.add(end.split(".", 1)[0])
    for item in design["unloaded"]:
        tiles.add(item["pin"].split(".", 1)[0])
    return tiles


def stats(design):
    rows, columns = design["rows"], design["columns"]
    touched = endpoints(design)
    clb_used = sum(1 for t in touched if CLB_TILE.match(t))
    iob_used = sum(1 for t in touched if t.startswith("PAD"))
    out = {
        "part": design["part"],
        "package": design["package"],
        "clb_total": len(design["clbs"]),
        "clb_used": clb_used,
        "clb_array": f"{rows}x{columns}",
        "iob_used": iob_used,
        "nets": len(design["nets"]),
        "clocks": len(design["clocks"]),
        "tribufs": len(design["tribufs"]),
        "pullups": len(design["pullups"]),
        "unrouted": len(design["unrouted"]),
        "unloaded": len(design["unloaded"]),
        "truncated": len(design["truncatedTraces"]),
    }
    used = {name: clb for name, clb in design["clbs"].items() if clb["pins"]}
    for mode in sorted({clb["mode"] for clb in used.values()}):
        out[f"clb_mode_{mode}"] = sum(1 for clb in used.values() if clb["mode"] == mode)
    out["flipflops"] = sum(clb["flipFlops"]["X"] + clb["flipFlops"]["Y"] for clb in used.values())
    out["iob_bonded"] = sum(1 for iob in design["iobs"].values() if iob["pins"] and iob["bonded"])
    for source, loads in sorted(clock_loads(design).items()):
        out[f"clock_loads {source}"] = loads
    return out


def clock_loads(design):
    """The logic blocks each clock buffer and each pad clocks: the K pins
    on a net a clock buffer's output or an I/O block's input drives.  A
    block clocked from another block's output (a ripple stage) is no
    clock buffer's or pad's load."""
    loads = {}
    for net in design["nets"]:
        sources = net.get("sources", [])
        if len(sources) != 1:
            continue
        source = sources[0]
        if not (source in CLOCK_OUTPUTS or PAD_INPUT.match(source)):
            continue
        count = sum(1 for end in net.get("destinations", []) if CLB_CLOCK.match(end))
        if count:
            loads[source] = count
    return loads


def main(argv):
    if len(argv) != 2:
        sys.exit("usage: netstats.py NETLIST.json")
    with open(argv[1]) as handle:
        design = json.load(handle)
    for key, value in stats(design).items():
        print(f"{key} {value}")


if __name__ == "__main__":
    main(sys.argv)
