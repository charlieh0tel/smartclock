"""What detail.py and its drawings ask of a design's decode: the netlist
model, and each design's element table bound to it and checked both
ways (detail.py's docstring lists the checks)."""

import functools
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from page import ROOT, die  # noqa: E402
from detail_designs import SPECS, Facts  # noqa: E402

# The logic block's outputs, its pins that are clocks, resets and
# enables, and the selections that name its flip-flops (data sheet,
# Figures 4 to 6, as netdump.js's JSON spells them).
OUTPUTS = ("X", "Y")
FLIP_FLOPS = {"QX": "X", "QY": "Y"}
CONTROL_PINS = ("K", "RD", "EC")
LETTERS = re.compile(r"\b(A|B|C|D|E|QX|QY)\b")

# The clock buffers, by the net each drives.
BUFFER_NETS = {"GCLK.O": "GCLK", "ACLK.O": "ACLK"}


class Netlist:
    """A design's decode, netdump.js's JSON, as the questions the page asks of it."""

    def __init__(self, path):
        if not path.exists():
            die(f"no {path}; make -f tools/fpga/Makefile decodes it")
        design = json.loads(path.read_text(encoding="utf-8"))
        self.nets = design["nets"]
        self.clbs = {tile: clb for tile, clb in design["clbs"].items() if clb["pins"]}
        self.iobs = {iob["pin"]: iob for iob in design["iobs"].values() if iob["bonded"] and iob["pins"]}
        self.pad_pins = design["padPins"]
        self.buffers = design["clocks"]

    def name(self, net):
        return self.nets[net]["name"] if net is not None else None

    def source(self, net):
        """The block and pin that drive a net, as ("AB", "X"), ("PAD7", "I") or ("GCLK", "O")."""
        block, _, pin = self.name(net).partition(".")
        return block, pin

    def label(self, net):
        """A net as the page names it: a clock buffer by name, a pad by its package pin."""
        name = self.name(net)
        if name in BUFFER_NETS:
            return BUFFER_NETS[name]
        block, pin = self.source(net)
        if pin == "I" and block in self.pad_pins:
            return self.pad_pins[block]
        return name

    def letters(self, clb, selection, seen):
        """The logic block pins a selection (F, G, M, QX, QY or DI) depends on."""
        if selection in seen:
            return set()
        seen.add(selection)
        if selection == "DI":
            return {"DI"}
        if selection in FLIP_FLOPS:
            output = FLIP_FLOPS[selection]
            return self.letters(clb, clb["data"][output], seen) | set(CONTROL_PINS)
        equation = clb["equations"]["F" if selection == "M" else selection] or clb["equations"]["F"]
        found = set()
        for letter in LETTERS.findall(equation):
            found |= self.letters(clb, letter, seen) if letter in FLIP_FLOPS else {letter}
        return found

    def used_outputs(self, tile):
        """A logic block's outputs that drive a net."""
        pins = self.clbs[tile]["pins"]
        return [o for o in OUTPUTS if pins.get(o) is not None]

    def depends(self, tile, output=None):
        """The nets a logic block's output, or every output it uses, reads: {pin: net}."""
        clb = self.clbs[tile]
        outputs = [output] if output else self.used_outputs(tile)
        letters = set()
        for out in outputs:
            letters |= self.letters(clb, clb["outputs"][out], set())
        return {pin: net for pin, net in clb["pins"].items() if pin in letters}


class Element:
    """One element of a design's diagram, its trace's text bound to its netlist."""

    def __init__(self, spec, netlist, facts, owners):
        self.spec = spec
        self.inputs = {}      # an input pin, its pin number: True
        self.outputs = []     # its output pins, each (pin, iob)
        self.clocks = []      # its clocks, as the page names them, in order
        self.resets = []      # its direct resets, likewise
        self.reads = {}       # another element's key: the nets read from it, in order
        self.undrawn = []     # nets read from blocks no element names
        try:
            self.lines = [line.format_map(facts) for line in spec.lines]
        except KeyError as error:
            die(f"{spec.key}: no figure {error}")
        self.bind(netlist, owners)

    def own(self, block):
        return block in self.spec.tiles()

    def add(self, items, value):
        if value not in items:
            items.append(value)

    def bind(self, netlist, owners):
        """Its pins, clocks, resets and the elements it reads, from the netlist."""
        nets = []
        for member in self.spec.members:
            tile, _, output = member.partition(".")
            if tile in netlist.buffers:
                nets.append(netlist.buffers[tile]["pins"]["I"])
                continue
            if tile in netlist.iobs:
                nets.append(netlist.iobs[tile]["pins"]["O"])
                continue
            clb = netlist.clbs[tile]
            for pin, net in netlist.depends(tile, output or None).items():
                block, _ = netlist.source(net)
                if pin == "K":
                    edge = " falling" if clb["clockInvert"] else ""
                    self.add(self.clocks, ("own ripple" if self.own(block) else netlist.label(net)) + edge)
                elif pin == "RD":
                    self.add(self.resets, "own" if self.own(block) else netlist.label(net))
                nets.append(net)
        for net in nets:
            name = netlist.name(net)
            if name in BUFFER_NETS:
                continue
            block, _ = netlist.source(net)
            if block in netlist.pad_pins:
                self.inputs[netlist.pad_pins[block]] = True
                continue
            owner = owners.get(name)
            if owner is None:
                self.add(self.undrawn, name)
            elif owner != self.spec.key:
                self.add(self.reads.setdefault(owner, []), name)


def bind(design, netlist_dir):
    """A design's spec bound to its netlist, checked both ways."""
    spec = SPECS[design.image]
    netlist = Netlist(netlist_dir / f"{design.image}.json")
    facts = Facts(design)
    owners = {}
    for element in spec.elements:
        for member in element.members:
            tile, _, output = member.partition(".")
            if tile in netlist.buffers or tile in netlist.iobs:
                owners[f"{tile}.O"] = element.key
                continue
            if tile not in netlist.clbs:
                die(f"{design.image}: {element.key} names {tile}, which the netlist does not use")
            clb = netlist.clbs[tile]
            used = netlist.used_outputs(tile)
            if output and output not in used:
                die(f"{design.image}: {element.key} names {member}, which drives no net")
            for out in [output] if output else used:
                net = netlist.name(clb["pins"][out])
                if net in owners:
                    die(f"{design.image}: {net} is both {owners[net]}'s and {element.key}'s")
                owners[net] = element.key
    for element in spec.elements:
        for doc, section in element.sources:
            if section not in headings(doc):
                die(f"{design.image}: {element.key} cites {doc}, \"{section}\", which has no such heading")
    elements = [Element(e, netlist, facts, owners) for e in spec.elements]
    by_key = {e.spec.key: e for e in elements}

    used = set(netlist.iobs)
    roles = set(spec.pins)
    if used - roles:
        die(f"{design.image}: pins in use with no role: {sorted(used - roles, key=pin_number)}")
    if roles - used:
        die(f"{design.image}: roles for pins not in use: {sorted(roles - used, key=pin_number)}")
    for pin, iob in sorted(netlist.iobs.items(), key=lambda item: pin_number(item[0])):
        if not iob["output"]["enabled"]:
            continue
        net = netlist.name(iob["pins"]["O"])
        owner = owners.get(net)
        block, _ = netlist.source(iob["pins"]["O"])
        if block in netlist.pad_pins:
            owner = owners.get(f"{pin}.O")
        if owner is None:
            die(f"{design.image}: {pin}'s output, {net}, is no element's")
        by_key[owner].outputs.append((pin, iob))
    taken = {pin for e in elements for pin in e.inputs}
    for pin, iob in netlist.iobs.items():
        reads = "I" in iob["pins"] and netlist.nets[iob["pins"]["I"]]["destinations"]
        if reads and pin not in taken:
            die(f"{design.image}: input pin {pin} reaches no element")
    named = {member.partition(".")[0] for e in spec.elements for member in e.members}
    undrawn = sorted(set(netlist.clbs) - named)
    return netlist, elements, undrawn


def pin_number(pin):
    return int(pin[1:])


@functools.cache
def headings(doc):
    """A Markdown document's headings, without their #s."""
    text = (ROOT / doc).read_text(encoding="utf-8")
    return {line.lstrip("#").strip() for line in text.splitlines() if line.startswith("#")}


def output_notes(netlist, iob):
    """An output pin's configuration, read from its I/O block."""
    output, pins = iob["output"], iob["pins"]
    notes = []
    if output["registered"]:
        notes.append(f"reg {netlist.label(pins['OK'])}" + (" falling" if output["clockInvert"] else ""))
    if output["invert"]:
        notes.append("inverted")
    if output["threeState"]:
        notes.append(f"3-state {netlist.label(pins['T'])}")
    return notes


def source_label(netlist, iob):
    return netlist.label(iob["pins"]["O"])
