#!/usr/bin/env python3
"""Check that logic blocks of one decoded design are another's, block
for block, as functions.

    python3 -I tools/fpga/blockmatch.py tools/fpga/58503b-z3805a.match target/fpga/58503b.json target/fpga/z3805a.json
    python3 -I tools/fpga/blockmatch.py --self-test target/fpga/z3805a.json

tilediff.py compares two designs' configurations tile by tile; this
compares what the blocks a match file pairs compute, wherever each
design places them and however it encodes them.  Each pair's two
blocks must give every connected output the same function of the same
nets, and every flip-flop in use the same data, on the same clock and
its inversion, clock enable and direct reset; each function is
compared as a truth table over every net and flip-flop it reads.  The
first design's names are read as the second's through the match file's
renamings: by default a paired block's outputs and flip-flops are the
other block's of the same letter.  A net neither paired nor renamed,
a pad's or a block's outside the match file, is taken to be the same
net in both by its name, so a match file pairs whatever its blocks
read, back to the pads.  Prints a line a pair, naming what differs,
and fails if any pair differs.

A match file holds, one a line ('#' starts a comment):
- `pair TILE [TILE]`: the first design's block and the second's, the
  same tile if one is given;
- `rename NAME NAME`: a net, or a flip-flop TILE.QX or TILE.QY, of the
  first design, and what it is in the second.

--self-test pairs every used block of a design with itself, which must
match, and then makes each of a list of concrete changes, written out
here apart from what the comparison reads, to every block it applies
to, and fails unless each kind of change is reported for at least one
(inverting a table no output reads, say, changes nothing).
"""
import argparse
import copy
import itertools
import json
import sys

# A table's inputs, as a block's lutInputs name them: its pins and its
# own flip-flops.
PINS = ("A", "B", "C", "D", "E")
STATES = ("QX", "QY")
# A flip-flop's controls, compared by net: its clock, clock enable and
# direct reset, each with the JSON flag that says it is in use (the
# clock always is).
CONTROLS = (("K", None), ("EC", "enableClock"), ("RD", "directReset"))


class Block:
    """One logic block's decode, read as functions of named nets."""

    def __init__(self, tile, clb, names):
        self.tile = tile
        self.clb = clb
        self.nets = {pin: names.get(net) for pin, net in clb["pins"].items()}

    def source(self, selection):
        """The name a table input reads: a net, a flip-flop, or None (Low)."""
        if selection in STATES:
            return f"{self.tile}.{selection}"
        return self.nets.get(selection)

    def table(self, which, values):
        """Table F or G's value, with values naming each source's level."""
        inputs = self.clb["lutInputs"][which]
        index = 0
        for bit, selection in enumerate(inputs):
            name = self.source(selection) if bit else self.nets.get("A")
            if name is not None and values.get(name):
                index |= 1 << bit
        return (int(self.clb["lut"][which], 16) >> index) & 1

    def generator(self, name, values):
        """F, G or (mode FGM's, and F mode's) E's choice of the two."""
        f, g = self.table("F", values), self.table("G", values)
        if self.clb["mode"] != "FG":
            return g if values.get(self.nets.get("E")) else f
        return f if name == "F" else g

    def signal(self, selection, values):
        if selection in STATES:
            return values.get(f"{self.tile}.{selection}", 0)
        if selection == "DI":
            return values.get(self.nets.get("DI"), 0)
        return self.generator(selection, values)

    def reads(self):
        """Every name the block's functions can read."""
        found = {self.nets.get(pin) for pin in PINS} | {self.nets.get("DI")}
        found |= {f"{self.tile}.{state}" for state in STATES}
        return found - {None}

    def functions(self):
        """Each connected output and each flip-flop in use, by name, as a
        function of a dict of levels, with the flip-flop's controls."""
        found = {}
        for output in ("X", "Y"):
            if self.clb["pins"].get(output) is not None:
                selection = self.clb["outputs"][output]
                found[f"{self.tile}.{output}"] = (lambda v, s=selection: self.signal(s, v), None)
        for state, letter in zip(STATES, ("X", "Y")):
            if self.clb["flipFlops"][letter]:
                selection = self.clb["data"][letter]
                controls = [self.clb["clockInvert"]]
                for pin, flag in CONTROLS:
                    used = flag is None or self.clb[flag]
                    controls.append(self.nets.get(pin) if used else None)
                found[f"{self.tile}.{state}"] = (lambda v, s=selection: self.signal(s, v), controls)
        return found


def load(path):
    with open(path) as handle:
        return json.load(handle)


def design_blocks(design):
    names = {net["id"]: net["name"] for net in design["nets"]}
    return {tile: Block(tile, clb, names) for tile, clb in design["clbs"].items() if clb["pins"]}


def parse_match(path):
    """A match file's pairs and renamings."""
    pairs, renames = [], {}
    with open(path) as handle:
        for number, line in enumerate(handle, 1):
            words = line.split("#", 1)[0].split()
            if not words:
                continue
            if words[0] == "pair" and len(words) in (2, 3):
                pairs.append((words[1], words[-1]))
            elif words[0] == "rename" and len(words) == 3:
                renames[words[1]] = words[2]
            else:
                sys.exit(f"{path}:{number}: not a pair or a rename: {line.strip()}")
    return pairs, renames


def renaming(pairs, renames):
    """The first design's names as the second's: each pair's own outputs
    and flip-flops by default, then the match file's."""
    found = {}
    for first, second in pairs:
        for part in ("X", "Y") + STATES:
            found[f"{first}.{part}"] = f"{second}.{part}"
    found.update(renames)
    return found


def compare(first, second, rename):
    """What differs between two blocks, the first's names renamed: a list,
    empty when they match."""
    ours = {rename.get(name, name): value for name, value in first.functions().items()}
    theirs = second.functions()
    differences = []
    for name in sorted(set(ours) | set(theirs)):
        if name not in ours or name not in theirs:
            differences.append(f"{name} only in the {'second' if name in theirs else 'first'}")
            continue
        (f_ours, c_ours), (f_theirs, c_theirs) = ours[name], theirs[name]
        if c_ours is not None:
            c_ours = [c_ours[0]] + [rename.get(net, net) for net in c_ours[1:]]
        if c_ours != c_theirs:
            differences.append(f"{name} controls {c_ours} against {c_theirs}")
        reads_ours = {rename.get(n, n) for n in first.reads()}
        variables = sorted(reads_ours | second.reads())
        for levels in itertools.product((0, 1), repeat=len(variables)):
            values = dict(zip(variables, levels))
            mine = {n: values[rename.get(n, n)] for n in first.reads()}
            if f_ours(mine) != f_theirs(values):
                differences.append(f"{name} differs where {values}")
                break
    return differences


def run(match, first_path, second_path):
    pairs, renames = parse_match(match)
    rename = renaming(pairs, renames)
    first, second = design_blocks(load(first_path)), design_blocks(load(second_path))
    failed = False
    for ours, theirs in pairs:
        if ours not in first or theirs not in second:
            print(f"{ours}: no used block {ours if ours not in first else theirs}")
            failed = True
            continue
        differences = compare(first[ours], second[theirs], rename)
        print(f"{ours}: {theirs}" + ("" if not differences else ": " + "; ".join(differences)))
        failed |= bool(differences)
    return failed


# The concrete changes --self-test makes, each to the first used block it
# applies to: a function table, a table input, an output's or a data
# input's selection, a clock's inversion, enable or reset, and an input's
# net.
def changes(clb):
    """(description, change) for each change that applies to clb."""
    found = []
    used = [letter for letter in ("X", "Y") if clb["flipFlops"][letter]]
    for which in ("F", "G"):
        found.append((f"invert table {which}", ("lut", which)))
    if used:
        found.append(("invert the clock", ("clockInvert",)))
        found.append(("toggle the clock enable", ("enableClock",)))
        found.append(("toggle the direct reset", ("directReset",)))
        found.append(("move the clock", ("pin", "K")))
    for pin in PINS:
        if clb["pins"].get(pin) is not None:
            found.append((f"move input {pin}", ("pin", pin)))
    for letter in ("X", "Y"):
        if clb["pins"].get(letter) is not None:
            found.append((f"swap output {letter}", ("output", letter)))
    return found


def apply(clb, change, spare_net):
    clb = copy.deepcopy(clb)
    kind = change[0]
    if kind == "lut":
        clb["lut"][change[1]] = f"{int(clb['lut'][change[1]], 16) ^ 0xffff:04x}"
    elif kind in ("clockInvert", "enableClock", "directReset"):
        clb[kind] = not clb[kind]
        if kind != "clockInvert":
            clb["pins"].setdefault("EC" if kind == "enableClock" else "RD", spare_net)
    elif kind == "pin":
        clb["pins"][change[1]] = spare_net
    elif kind == "output":
        letter = change[1]
        register = "QX" if letter == "X" else "QY"
        generator = "F" if letter == "X" else "G"
        clb["outputs"][letter] = generator if clb["outputs"][letter] == register else register
        clb["flipFlops"][letter] = True
    return clb


def self_test(path):
    design = load(path)
    names = {net["id"]: net["name"] for net in design["nets"]}
    blocks = design_blocks(design)
    for tile, block in blocks.items():
        if compare(block, block, {}):
            sys.exit(f"self-test: {tile} does not match itself")
    spare = max(names) + 1
    names[spare] = "SPARE.X"
    tried = set()
    for tile, block in sorted(blocks.items()):
        for description, change in changes(block.clb):
            if description in tried:
                continue
            changed = Block(tile, apply(block.clb, change, spare), names)
            if not compare(changed, block, {}):
                continue
            tried.add(description)
    expected = {description for clb in design["clbs"].values() if clb["pins"]
                for description, _ in changes(clb)}
    missed = sorted(expected - tried)
    if missed:
        sys.exit(f"self-test: no block reports {', '.join(missed)}")
    print(f"self-test: {len(blocks)} blocks match themselves; {len(tried)} kinds of change reported")


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--self-test", metavar="DESIGN")
    parser.add_argument("files", nargs="*", metavar="MATCH FIRST SECOND")
    args = parser.parse_args()
    if args.self_test:
        self_test(args.self_test)
        return
    if len(args.files) != 3:
        parser.error("needs a match file and two designs")
    if run(*args.files):
        sys.exit(1)


if __name__ == "__main__":
    main()
