"""Renders the detailed logic diagrams of the five SmartClock FPGA designs,
fpga-detail.html, into --output.  It is not committed: the top
Makefile's `fpga-detail` target writes it for GitHub Pages
(https://charlieh0tel.github.io/smartclock/fpga-detail.html), and
check-detail, part of tools/fpga/Makefile's `check`, fails if it cannot
be rendered.

    python3 -I tools/fpga/detail.py --netlists target/fpga --output PAGE
    python3 -I tools/fpga/detail.py --netlists target/fpga --self-test

One generator draws all five, in one grammar: each design's logic in
the same lanes, by function, each element a box with its input pins
to its left and its output pins to its right.  What an element is,
which blocks it is and what the trace says of it are the tables in
detail_designs.py, each element citing its trace's section and the
trace's confidence word.  Everything else is read from the decoded
netlist, <image>.json under --netlists: each pin's direction, its
output's source, register, clock, inversion and 3-state control; each
element's clocks and resets; and which elements each reads from, by
the nets its blocks' outputs depend on.  An element's block that the
netlist lacks or leaves unused, an output two elements name, a pin in
use with no role, a role for a pin not in use, an output or an input
pin no element takes, a section cited that its document lacks and a
figure no reference gives are errors, not omissions; the logic blocks
no element names are listed under each design.  --self-test provokes
each of those errors once, on the Z3805A's table, and fails unless
each is refused.  The figures come from tools/fpga/ref through
page.py, as the explainer's do.
"""

import argparse
import dataclasses
import functools
import html
import json
import re
import sys
import textwrap
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from page import (DESIGNS, MARKS, ROOT, STYLE, TRACES, die, document, link, mark, markers,  # noqa: E402
                  stats)
from detail_designs import LANES, SPECS, Facts  # noqa: E402

# The logic block's outputs, its pins that are clocks, resets and
# enables, and the selections that name its flip-flops (data sheet,
# Figures 4 to 6, as netdump.js's JSON spells them).
OUTPUTS = ("X", "Y")
FLIP_FLOPS = {"QX": "X", "QY": "Y"}
CONTROL_PINS = ("K", "RD", "EC")
LETTERS = re.compile(r"\b(A|B|C|D|E|QX|QY)\b")

# The diagram's geometry, in SVG units: its width; the pin columns' and
# the element boxes' left edges and widths; line heights; the padding
# inside a box; the gap between rows and between lanes; and how many
# characters of each font fit a box's line and a pin box's.
WIDTH = 960
LEFT_X, PIN_W = 8, 184
BOX_X, BOX_W = 222, 516
RIGHT_X = WIDTH - 8 - PIN_W
TITLE_LINE, LINE = 19, 16
PAD = 10
ROW_GAP, LANE_GAP, LANE_HEAD = 24, 22, 34
BOX_CHARS, MONO_CHARS, PIN_CHARS = 74, 72, 25

# The clock buffers, by the net each drives.
BUFFER_NETS = {"GCLK.O": "GCLK", "ACLK.O": "ACLK"}

EXTRA_STYLE = """
.lanes{display:flex;flex-wrap:wrap;gap:.5rem 1.5rem;font-size:.9375rem;padding:0;list-style:none}
.detail{border:1px solid var(--rule);background:var(--sheet);padding:8px}
.detail svg{display:block;width:100%;height:auto}
.dg-box.open{stroke:var(--open);stroke-dasharray:5 4}
.dg-lane{fill:var(--bg);stroke:var(--rule)}
.dg-pin{fill:var(--panel);stroke:var(--signal);stroke-width:1.2}
.dg-b{font:600 13px "IBM Plex Sans",system-ui,sans-serif;fill:var(--fg)}
.dg-m{font:11.5px "IBM Plex Mono",ui-monospace,monospace;fill:var(--fg)}
.dg-n{font:11.5px "IBM Plex Mono",ui-monospace,monospace;fill:var(--muted)}
.dg-p{font:600 12px "IBM Plex Mono",ui-monospace,monospace;fill:var(--signal)}
.dg-c{font:600 12px "IBM Plex Sans",system-ui,sans-serif}
.dg-c.read{fill:var(--read)}
.dg-c.sim{fill:var(--sim)}
.dg-c.inferred{fill:var(--inferred)}
.dg-c.open{fill:var(--open)}
.dg-w{fill:none;stroke:var(--signal);stroke-width:1.2;marker-end:url(#dg-arrow)}
.elements{display:grid;gap:1.25rem}
.element{border-left:3px solid var(--rule);padding-left:.75rem;max-width:var(--measure)}
.element>*{margin:0}
.element>*+*{margin-top:.25rem}
.element h4{font-size:1rem;font-weight:600}
.element dl{display:grid;grid-template-columns:auto 1fr;gap:.125rem .75rem;font-size:.875rem}
.element dt{color:var(--muted)}
.element dd{margin:0;min-width:0;overflow-wrap:anywhere}
@media (max-width:520px){.element dl{display:block}.element dd+dt{margin-top:.25rem}}
.undrawn{font-size:.875rem;color:var(--muted)}
"""


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


# --- drawing --------------------------------------------------------------

def esc(text):
    return html.escape(text, quote=False)


def wrap(text, width):
    return textwrap.wrap(text, width) or [""]


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


def pin_groups(spec, pins, notes):
    """Pins drawn together: [(group, [(pin, role, notes)])], in pin order."""
    groups = {}
    for pin in sorted(pins, key=pin_number):
        role = spec.pins[pin]
        groups.setdefault(role.group, []).append((pin, role.role, notes.get(pin, [])))
    return list(groups.items())


def pin_lines(groups):
    """A pin column's boxes as lines: [(group, [(class, text)])]."""
    boxes = []
    for group, pins in groups:
        lines = []
        for pin, role, notes in pins:
            first = True
            for text in wrap(f"{pin} {role}", PIN_CHARS):
                lines.append(("dg-p" if first else "dg-m", text))
                first = False
            for text in wrap(", ".join(notes), PIN_CHARS) if notes else []:
                lines.append(("dg-n", text))
        boxes.append((group, lines))
    return boxes


def column_height(boxes):
    return sum(LINE * (len(lines) + 1) + PAD for _, lines in boxes) + 8 * max(len(boxes) - 1, 0)


def draw_column(out, boxes, x, top, height, side):
    """A pin column's boxes centered on a row, each wired to the element's box."""
    y = top + (height - column_height(boxes)) / 2
    for group, lines in boxes:
        h = LINE * (len(lines) + 1) + PAD
        out.append(f'<rect class="dg-pin" x="{x}" y="{y:.1f}" width="{PIN_W}" height="{h}" rx="3"/>')
        out.append(f'<text class="dg-n" x="{x + 8}" y="{y + LINE:.1f}">{esc(group)}</text>')
        for i, (klass, text) in enumerate(lines):
            out.append(f'<text class="{klass}" x="{x + 8}" y="{y + LINE * (i + 2):.1f}">{esc(text)}</text>')
        mid = y + h / 2
        if side == "in":
            out.append(f'<polyline class="dg-w" points="{x + PIN_W},{mid:.1f} {BOX_X - 2},{mid:.1f}"/>')
        else:
            out.append(f'<polyline class="dg-w" points="{BOX_X + BOX_W},{mid:.1f} {x - 2},{mid:.1f}"/>')
        y += h + 8


def box_lines(element, titles):
    """An element's box as lines: [(class, text)], its title first."""
    spec = element.spec
    lines = []
    for text in wrap(", ".join(spec.members), MONO_CHARS):
        lines.append(("dg-m", text))
    for line in element.lines:
        lines += [("dg-t", text) for text in wrap(line, BOX_CHARS)]
    for label, items in (("clock", element.clocks), ("reset", element.resets)):
        if items:
            lines += [("dg-n", text) for text in wrap(f"{label}: {', '.join(items)}", MONO_CHARS)]
    for key, nets in element.reads.items():
        lines += [("dg-n", text) for text in wrap(f"from {titles[key]}: {', '.join(nets)}", MONO_CHARS)]
    return lines


def diagram(design, netlist, elements, spec):
    """One design's diagram: its lanes, top to bottom, each element a row."""
    titles = {e.spec.key: e.spec.title for e in elements}
    out = []
    y = 0
    for lane, heading in LANES:
        members = [e for e in elements if e.spec.lane == lane]
        lane_top = y
        body = []
        y += LANE_HEAD
        if not members:
            body.append(f'<text class="dg-n" x="{BOX_X}" y="{y + LINE:.1f}">None in this design.</text>')
            y += LINE + PAD
        previous = None
        for element in members:
            notes = {pin: output_notes(netlist, iob) for pin, iob in element.outputs}
            left = pin_lines(pin_groups(spec, element.inputs, {}))
            right = pin_lines(pin_groups(spec, [pin for pin, _ in element.outputs], {
                pin: [f"= {source_label(netlist, iob)}"] + notes[pin] for pin, iob in element.outputs}))
            lines = box_lines(element, titles)
            box_h = TITLE_LINE + LINE * len(lines) + 2 * PAD
            height = max(box_h, column_height(left), column_height(right))
            top = y
            box_y = top + (height - box_h) / 2
            if previous is not None and previous.spec.key in element.reads:
                body.append(f'<polyline class="dg-a" points="{BOX_X + BOX_W / 2},{previous_bottom:.1f} '
                            f'{BOX_X + BOX_W / 2},{box_y - 2:.1f}"/>')
            klass = {"inferred": "dg-box inferred", "open": "dg-box open"}.get(element.spec.confidence, "dg-box")
            body.append(f'<rect class="{klass}" x="{BOX_X}" y="{box_y:.1f}" width="{BOX_W}" height="{box_h}" rx="4"/>')
            body.append(f'<text class="dg-b" x="{BOX_X + PAD}" y="{box_y + PAD + 13:.1f}">'
                        f'{esc(element.spec.title)}</text>')
            word = element.spec.confidence
            body.append(f'<text class="dg-c {MARKS[word]}" x="{BOX_X + BOX_W - PAD}" '
                        f'y="{box_y + PAD + 13:.1f}" text-anchor="end">{word}</text>')
            for i, (k, text) in enumerate(lines):
                body.append(f'<text class="{k}" x="{BOX_X + PAD}" '
                            f'y="{box_y + PAD + TITLE_LINE + LINE * i + 12:.1f}">{esc(text)}</text>')
            draw_column(body, left, LEFT_X, top, height, "in")
            draw_column(body, right, RIGHT_X, top, height, "out")
            previous, previous_bottom = element, box_y + box_h
            y = top + height + ROW_GAP
        y += LANE_GAP / 2
        out.append(f'<rect class="dg-lane" x="0" y="{lane_top:.1f}" width="{WIDTH}" '
                   f'height="{y - lane_top - LANE_GAP / 2:.1f}" rx="6"/>')
        out.append(f'<text class="dg-h" x="12" y="{lane_top + 22:.1f}">{esc(heading.upper())}</text>')
        out += body
        y += LANE_GAP / 2
    label = f"The {design.name} FPGA design's logic, by function"
    return (f'<svg viewBox="0 0 {WIDTH} {y:.0f}" role="img" xmlns="http://www.w3.org/2000/svg" '
            f'aria-label="{esc(label)}">\n{markers()}\n' + "\n".join(out) + "\n</svg>")


# --- the text beside the drawing -------------------------------------------

def cite(sources):
    return "; ".join(f"{link(doc, Path(doc).name)}, &ldquo;{esc(section)}&rdquo;" for doc, section in sources)


def element_text(image, element, netlist, spec, titles):
    s = element.spec
    rows = [("Blocks", f"<code>{esc(', '.join(s.members))}</code>")]
    if element.inputs:
        rows.append(("Input pins", esc("; ".join(
            f"{pin}, {spec.pins[pin].role}" for pin in sorted(element.inputs, key=pin_number)))))
    if element.outputs:
        rows.append(("Output pins", esc("; ".join(
            f"{pin}, {spec.pins[pin].role} (= {', '.join([source_label(netlist, iob)] + output_notes(netlist, iob))})"
            for pin, iob in element.outputs))))
    if element.clocks:
        rows.append(("Clock", esc(", ".join(element.clocks))))
    if element.resets:
        rows.append(("Reset", esc(", ".join(element.resets))))
    for key, nets in element.reads.items():
        rows.append((f"From {esc(titles[key])}", f"<code>{esc(', '.join(nets))}</code>"))
    if element.undrawn:
        rows.append(("From blocks not drawn", f"<code>{esc(', '.join(element.undrawn))}</code>"))
    rows.append(("Source", cite(s.sources)))
    body = "".join(f"<dt>{k}</dt><dd>{v}</dd>" for k, v in rows)
    text = "".join(f"<p>{esc(line)}</p>" for line in element.lines)
    return (f'<div class="element" id="{image}-{s.key}">\n<h4>{esc(s.title)} &middot; {mark(s.confidence)}</h4>\n'
            f"{text}\n<dl>{body}</dl>\n</div>")


def section(design, netlist_dir):
    netlist, elements, undrawn = bind(design, netlist_dir)
    spec = SPECS[design.image]
    table = stats(design.image)
    titles = {e.spec.key: e.spec.title for e in elements}
    lanes = []
    for lane, heading in LANES:
        members = [e for e in elements if e.spec.lane == lane]
        inner = "\n".join(element_text(design.image, e, netlist, spec, titles) for e in members) or \
            "<p class=\"note\">None in this design.</p>"
        lanes.append(f"<h3>{esc(heading)}</h3>\n<div class=\"elements\">\n{inner}\n</div>")
    drawn = len(set(netlist.clbs) - set(undrawn))
    equations = "; ".join(
        f"{tile}: " + ", ".join(f"{fg} = {eq}" for fg, eq in netlist.clbs[tile]["equations"].items() if eq)
        for tile in undrawn)
    left_out = (f'<p class="undrawn">Logic blocks in use that the trace gives no role and no element names, '
                f'{len(undrawn)} of {len(netlist.clbs)}, with their equations from the netlist: '
                f'<code>{esc(equations)}</code>.  An element that reads one says so.</p>'
                if undrawn else '<p class="undrawn">Every logic block in use is drawn.</p>')
    return f"""<section id="{design.image}">
<h2>{esc(design.name)}</h2>
<p>Loaded by {esc(design.images)}.  {table['clb_used']} logic blocks, {table['iob_used']} I/O blocks and
{table['nets']} nets (<code>tools/fpga/ref/{design.image}.stats</code>); {drawn} of the logic blocks are
drawn.  The trace: {link(TRACES[design.image], Path(TRACES[design.image]).name)}.</p>
<figure class="detail">
{diagram(design, netlist, elements, spec)}
</figure>
{left_out}
{chr(10).join(lanes)}
</section>"""


def render(netlist_dir):
    sections = [section(design, netlist_dir) for design in DESIGNS]
    nav = "".join(f'<li><a href="#{d.image}">{esc(d.name)}</a></li>' for d in DESIGNS)
    lanes = "".join(f"<li>{esc(heading)}</li>" for _, heading in LANES)
    header = f"""<header>
<p class="eyebrow">SmartClock firmware &middot; FPGA</p>
<h1>FPGA detail</h1>
<p class="lede">The logic of each of the five SmartClock FPGA designs, one diagram a design, all
drawn by one generator in one grammar so that the differences stand out.  The short version is
<a href="fpga.html">Inside the SmartClock FPGAs</a>; the traces are the long one.</p>
<p class="note">Nothing here has been checked on a board; every pin's role is read from the
logic.  Each element carries its trace's confidence word: {mark('read')}, from the decoded
netlist's equations and connections; {mark('simulated')}, printed by a testbench under Icarus
Verilog; {mark('inferred')}, reasoning from those; {mark('open')}, not established.  Inferred
and open elements have dashed outlines.</p>
<nav><ol>{nav}</ol></nav>
</header>"""
    grammar = f"""<section id="reading">
<h2>How to read the diagrams</h2>
<p>Every design is drawn in the same lanes, top to bottom, and a lane the design lacks says so:</p>
<ol class="lanes">{lanes}</ol>
<ul>
<li>A box is an element: the logic blocks a trace names together for one job, by tile
(AB is row A, column B; <code>EI.Y</code> one output of it).  Its title is the role, its
first line the blocks, then what the trace says of it, its clocks and resets, and the
elements it reads from, with the nets.</li>
<li>To a box's left are the pins it reads, to its right the pins it drives, each with its
number and role; an output pin also gives its source net, whether its I/O block registers it
(<code>reg</code>, and on which clock), inverts it, or 3-states it (<code>3-state</code>, and
the net that controls it).  A pin that feeds several elements is drawn at each.</li>
<li>An arrow between two boxes in a lane is one reading the other; every other connection is
named in the box's <q>from</q> lines.</li>
<li>Pins, clocks, resets and connections are read from the netlist by the generator; the
titles and text are the traces', each cited below its diagram.  Figures come from the
checked references in <code>tools/fpga/ref</code>.</li>
</ul>
<p>On a narrow screen the diagrams shrink to fit; the list under each repeats every element in
full.</p>
</section>"""
    body = "\n\n".join([header, grammar] + sections) + """

<footer>Generated by <code>tools/fpga/detail.py</code> from the decoded netlists, the element
tables in <code>tools/fpga/detail_designs.py</code> and the checked references in
<code>tools/fpga/ref</code>.</footer>"""
    return document("FPGA detail",
                    "Each SmartClock FPGA design's logic in detail: pins, clocks, the count, the divider, "
                    "the register port and the sync logic, drawn from the decoded netlists.",
                    STYLE + EXTRA_STYLE, "detail.py", "fpga-detail", body)


# The self-test's cases: each a change to the reference design's table
# that bind() must refuse, and the words its refusal must contain.
SELF_TEST = (
    ("a block the decode does not use", lambda els, pins: change(els, "gclk", members=("AA",)), "does not use"),
    ("a pin in use with no role", lambda els, pins: pins.pop("P83"), "with no role"),
    ("a role for a pin not in use", lambda els, pins: pins.update(P1=pins["P2"]), "not in use"),
    ("an output no element drives", lambda els, pins: els.pop(find(els, "tie")), "is no element's"),
    ("an input pin no element takes", lambda els, pins: els.pop(find(els, "interpolator")), "reaches no element"),
    ("an output two elements name",
     lambda els, pins: change(els, "strobe", members=els[find(els, "strobe")].members + ("DL",)), "is both"),
    ("a figure no reference gives", lambda els, pins: change(els, "gclk", lines=("{none}",)), "no figure"),
    ("a section the trace lacks",
     lambda els, pins: change(els, "gclk", sources=(("docs/firmware/z3805a-trace.md", "Nowhere"),)), "no such heading"),
)


def find(elements, key):
    return next(i for i, e in enumerate(elements) if e.key == key)


def change(elements, key, **fields):
    i = find(elements, key)
    elements[i] = dataclasses.replace(elements[i], **fields)


def self_test(netlist_dir):
    """That bind() refuses each of SELF_TEST's changes to the Z3805A's table."""
    design = DESIGNS[0]
    original = SPECS[design.image]
    try:
        for name, provoke, words in SELF_TEST:
            elements, pins = list(original.elements), dict(original.pins)
            provoke(elements, pins)
            SPECS[design.image] = dataclasses.replace(original, elements=tuple(elements), pins=pins)
            try:
                bind(design, netlist_dir)
            except SystemExit as refusal:
                if words not in str(refusal):
                    die(f"self-test: {name}: refused for another reason: {refusal}")
                continue
            die(f"self-test: {name}: not refused")
    finally:
        SPECS[design.image] = original
    print(f"detail self-test: {len(SELF_TEST)} kinds of error each refused")


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--netlists", type=Path, required=True, help="where the decodes, <image>.json, are")
    parser.add_argument("--output", type=Path, help="the page to write")
    parser.add_argument("--self-test", action="store_true", help="check that each kind of error is refused")
    args = parser.parse_args()
    if args.self_test:
        self_test(args.netlists)
        return
    if not args.output:
        die("--output or --self-test is required")
    args.output.write_text(render(args.netlists), encoding="utf-8")


if __name__ == "__main__":
    main()
