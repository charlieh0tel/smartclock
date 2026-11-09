#!/usr/bin/env python3
"""Compare two decoded designs logic block by logic block.

    python3 tools/fpga/tilediff.py target/fpga/z3805a.json target/fpga/58503a-z3801a.json
    python3 tools/fpga/tilediff.py --self-test target/fpga/z3805a.json

A tile name is a position, so comparing two designs by it says
something only where they place the logic they share in the same
blocks.  Prints, one "key tiles" line each (stable order), for the
logic blocks either design uses, and then the same four as "iob
configured" and so on for the I/O blocks, by pad:
- configured: both use the block, with a different mode, equations,
  flip-flop data, output selection, clock enable, reset or clock
  inversion;
- connected: both configure it alike, but an input reads another net;
- added, removed: only the second, or only the first, uses it.
An I/O block's configuration is its input, output, pull-up and clock
buffer settings, its connections the nets on its O, T, OK and IK.
`make -f tools/fpga/Makefile check` diffs the 58503A/Z3801A's against
the Z3805A's with tools/fpga/ref/58503a-z3801a.tilediff.  --self-test
makes each of a list of concrete changes, written out here rather than
read from what the comparison reads, to one used block of a design, and
fails unless the report lists that block as configured (or, for a
changed input net, connected): a part of the decode the comparison
leaves out is caught.
"""
import copy
import json
import sys

# A logic block's input pins; its X and Y are named by the block itself.
INPUTS = ("A", "B", "C", "D", "E", "DI", "EC", "K", "RD")
# An I/O block's: its I is its own pad.
IOB_INPUTS = ("O", "T", "OK", "IK")
# The parts of an I/O block's decode that make its configuration.
IOB_CONFIGURATION = ("pin", "input", "output", "pullup", "dedicatedClock")


# The parts of a logic block's decode that make its configuration.
CONFIGURATION = ("mode", "lut", "lutInputs", "equations", "data", "outputs", "flipFlops", "enableClock",
                 "directReset", "clockInvert")


def load(path):
    with open(path) as handle:
        return json.load(handle)


def iobs(design):
    """Each used I/O block's configuration and the nets its inputs read."""
    names = {net["id"]: net["name"] for net in design["nets"]}
    found = {}
    for pad, iob in design["iobs"].items():
        if not iob["pins"]:
            continue
        configuration = json.dumps([iob[part] for part in IOB_CONFIGURATION], sort_keys=True)
        inputs = {pin: names.get(net) for pin, net in iob["pins"].items() if pin in IOB_INPUTS}
        found[pad] = (configuration, inputs)
    return found


def blocks(design):
    """Each used logic block's configuration and the nets its inputs read."""
    names = {net["id"]: net["name"] for net in design["nets"]}
    found = {}
    for tile, clb in design["clbs"].items():
        if not clb["pins"]:
            continue
        configuration = json.dumps([clb[part] for part in CONFIGURATION], sort_keys=True)
        inputs = {pin: names.get(net) for pin, net in clb["pins"].items() if pin in INPUTS}
        found[tile] = (configuration, inputs)
    return found


def flipped(value):
    """A boolean the other way; anything else replaced by a string no
    decode produces."""
    return not value if isinstance(value, bool) else "changed"


# Each self-test change to a logic block: what it changes, and how.
CLB_CHANGES = (
    ("mode", lambda clb: clb.update(mode="FG" if clb["mode"] != "FG" else "F")),
    ("F's truth table", lambda clb: clb["lut"].update(F=f"{int(clb['lut']['F'], 16) ^ 1:04x}")),
    ("F's inputs", lambda clb: clb["lutInputs"].update(F=clb["lutInputs"]["F"][::-1])),
    ("F's equation", lambda clb: clb["equations"].update(F=clb["equations"]["F"] + " + E")),
    ("X's flip-flop data", lambda clb: clb["data"].update(X="DI" if clb["data"]["X"] != "DI" else "F")),
    ("X's output selection", lambda clb: clb["outputs"].update(X="F" if clb["outputs"]["X"] != "F" else "QX")),
    ("Y's output selection", lambda clb: clb["outputs"].update(Y="G" if clb["outputs"]["Y"] != "G" else "QY")),
    ("X's flip-flop", lambda clb: clb["flipFlops"].update(X=not clb["flipFlops"]["X"])),
    ("the clock enable", lambda clb: clb.update(enableClock=not clb["enableClock"])),
    ("the direct reset", lambda clb: clb.update(directReset=not clb["directReset"])),
    ("the clock inversion", lambda clb: clb.update(clockInvert=not clb["clockInvert"])),
)

# Each self-test change to an I/O block.
IOB_CHANGES = (
    ("pin", lambda iob: iob.update(pin="P0")),
    ("input's registering", lambda iob: iob["input"].update(registered=not iob["input"]["registered"])),
    ("input's storage", lambda iob: iob["input"].update(storage=flipped(iob["input"]["storage"]))),
    ("output's enable", lambda iob: iob["output"].update(enabled=not iob["output"]["enabled"])),
    ("output's inversion", lambda iob: iob["output"].update(invert=not iob["output"]["invert"])),
    ("output's three-state", lambda iob: iob["output"].update(threeState=not iob["output"]["threeState"])),
    ("pull-up", lambda iob: iob.update(pullup=not iob["pullup"])),
    ("clock buffer", lambda iob: iob.update(dedicatedClock=flipped(iob["dedicatedClock"]))),
)


def self_test(path):
    """Fails unless each of CLB_CHANGES and IOB_CHANGES to the first
    block with a connected input lists the block as configured, and a
    changed net on that input lists it as connected."""
    design = load(path)
    for kind, decode, changes, inputs in [("clbs", blocks, CLB_CHANGES, INPUTS),
                                          ("iobs", iobs, IOB_CHANGES, IOB_INPUTS)]:
        name, pin = next((name, pin) for name, block in sorted(design[kind].items())
                         for pin in block["pins"] if pin in inputs)
        other = next(net["id"] for net in design["nets"] if net["id"] != design[kind][name]["pins"][pin])

        def reconnect(block):
            block["pins"][pin] = other

        for what, change, expected in [(what, change, "configured") for what, change in changes] + \
                [(f"the net on {pin}", reconnect, "connected")]:
            changed = copy.deepcopy(design)
            change(changed[kind][name])
            if name not in compare(decode(design), decode(changed))[expected]:
                sys.exit(f"tilediff.py: a change to {name}'s {what} is not reported {expected}")


def main(argv):
    if len(argv) == 3 and argv[1] == "--self-test":
        self_test(argv[2])
        return
    if len(argv) != 3:
        sys.exit("usage: tilediff.py FIRST.json SECOND.json | --self-test DESIGN.json")
    designs = load(argv[1]), load(argv[2])
    report("", blocks(designs[0]), blocks(designs[1]))
    report("iob ", iobs(designs[0]), iobs(designs[1]))


def pad_order(name):
    """PAD9 before PAD10; a tile name as itself."""
    return (len(name), name)


def compare(first, second):
    """The four lists, each in pad_order, by name."""
    shared = sorted(first.keys() & second.keys(), key=pad_order)
    return {
        "configured": [key for key in shared if first[key][0] != second[key][0]],
        "connected": [key for key in shared
                      if first[key][0] == second[key][0] and first[key][1] != second[key][1]],
        "added": sorted(second.keys() - first.keys(), key=pad_order),
        "removed": sorted(first.keys() - second.keys(), key=pad_order),
    }


def report(prefix, first, second):
    for kind, keys in compare(first, second).items():
        print(f"{prefix}{kind}", " ".join(keys))


if __name__ == "__main__":
    main(sys.argv)
