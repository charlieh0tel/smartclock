"""Renders the detailed logic diagrams of the six SmartClock FPGA designs,
fpga-detail.html, into --output.  It is not committed: the top
Makefile's `fpga-detail` target writes it for GitHub Pages
(https://charlieh0tel.github.io/smartclock/fpga-detail.html), and
check-detail, part of tools/fpga/Makefile's `check`, fails if it cannot
be rendered.

    python3 -I tools/fpga/detail.py --netlists target/fpga --output PAGE
    python3 -I tools/fpga/detail.py --netlists target/fpga --self-test

One generator draws all six, each twice (detail_draw.py): the whole
chip, its pins on the frame's edges and its function blocks in the
signal's flow, one layout a family; and the time-interval counter gate
by gate, one box a logic block with its netlist equations.  Under them
every element is listed in full, by lane.  What an element is,
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
figure no reference gives are errors, not omissions, and so are a
chip-drawing wire that carries no net, an element in no block, and any
collision in either drawing (sheet.py: a wire through a box, two wires
along one line, text on text, on a box or on a wire not its own, text
out of its box); the logic blocks no element names are listed under
each design.  --self-test provokes each of those errors once, on the
Z3805A's table and layout and on sheets made for it, and fails unless
each is refused.  The figures come from tools/fpga/ref through
page.py, as the explainer's do.
"""

import argparse
import dataclasses
import html
import sys
import textwrap
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from page import DESIGNS, STYLE, TRACES, die, document, link, mark, stats  # noqa: E402
from detail_designs import LANES, SPECS  # noqa: E402
from detail_bind import bind, output_notes, pin_number, source_label  # noqa: E402
from detail_designs import Facts  # noqa: E402
from detail_draw import LAYOUTS, chip, gates  # noqa: E402
from sheet import Sheet  # noqa: E402
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import zoom  # noqa: E402


# Each drawing opens full screen at any width, at the drawing's own
# scale (tools/zoom.py).
ZOOM_MEDIA = "all"
ZOOM_HOME = ("the drawing's own scale", "copy.viewBox.baseVal.width / view.clientWidth")

EXTRA_STYLE = zoom.css(ZOOM_MEDIA, "sheet") + """
:root{--line:var(--rule);--ink:var(--fg);--read:var(--accent)}
.zoom{margin:0}
main{max-width:1440px}
.lanes{display:flex;flex-wrap:wrap;gap:.5rem 1.5rem;font-size:.9375rem;padding:0;list-style:none}
.sheet{overflow-x:auto;border:1px solid var(--rule);background:var(--sheet);border-radius:4px}
.sheet svg{display:block;min-width:940px;width:100%;height:auto}
.key{display:flex;flex-wrap:wrap;gap:.5rem 1.25rem;font-size:.875rem;padding:.75rem;background:var(--sheet);
  border:1px solid var(--rule);border-radius:4px;max-width:var(--measure)}
.key span{display:inline-flex;align-items:center;gap:.4rem}
.sw{display:inline-block;width:28px;border-top:2px solid var(--fg)}
.sw.sig{border-color:var(--signal);border-top-width:3px}
.mk{display:inline-grid;place-items:center;width:18px;height:18px;border-radius:50%;
  font:700 11px/1 "IBM Plex Mono",ui-monospace,monospace;border:1.5px solid currentColor}
.mk.sim{color:var(--sim)}.mk.inferred{color:var(--inferred)}.mk.open{color:var(--open)}
.ch text{font:12px "IBM Plex Sans Condensed","IBM Plex Sans",system-ui,sans-serif;fill:var(--fg)}
.ch .ch-ttl{font-weight:700;font-size:14px}
.ch .ch-ttl.sig,.ch .ch-net.sig{fill:var(--signal)}
.ch .ch-clb{font:10.5px "IBM Plex Mono",ui-monospace,monospace;fill:var(--muted)}
.ch .ch-lab{fill:var(--muted)}
.ch .ch-eq,.ch .ch-p,.ch .ch-pt,.ch .ch-mod{font:11px "IBM Plex Mono",ui-monospace,monospace}
.ch .ch-ctrt{font-weight:600}
.ch .ch-net{font:10.5px "IBM Plex Mono",ui-monospace,monospace;fill:var(--muted)}
.ch-frame{fill:none;stroke:var(--rule);stroke-width:2}
.ch-blk{fill:var(--panel);stroke:var(--fg);stroke-width:1.2}
.ch-blk.ti{stroke:var(--signal);stroke-width:2}
.ch-ctr{fill:var(--sheet);stroke:var(--muted);stroke-width:1}
.ch-pin,.ch-pill{fill:var(--sheet);stroke:var(--fg);stroke-width:1}
.ch-w{fill:none;stroke:var(--fg);stroke-width:1.3}
.ch-w.sig{stroke:var(--signal);stroke-width:2.2}
.ch-ah{fill:var(--fg)}.ch-ah.sig{fill:var(--signal)}
.ch-mk circle{fill:var(--sheet);stroke-width:1.5}
.ch-mk text{font:700 10px "IBM Plex Mono",ui-monospace,monospace}
.ch-mk.sim circle{stroke:var(--sim)}.ch-mk.sim text{fill:var(--sim)}
.ch-mk.inferred circle{stroke:var(--inferred)}.ch-mk.inferred text{fill:var(--inferred)}
.ch-mk.open circle{stroke:var(--open)}.ch-mk.open text{fill:var(--open)}
.ch-badge{fill:var(--sheet);stroke-width:1.5}
.ch-badge.sim{stroke:var(--sim)}.ch-badge.inferred{stroke:var(--inferred)}.ch-badge.open{stroke:var(--open)}
.ch text.ch-mk{font:700 10px "IBM Plex Mono",ui-monospace,monospace}
.ch text.ch-mk.sim{fill:var(--sim)}.ch text.ch-mk.inferred{fill:var(--inferred)}.ch text.ch-mk.open{fill:var(--open)}
.ch-dot{fill:var(--fg)}
.elements{display:grid;gap:1.25rem}
.element{border-left:3px solid var(--rule);padding-left:.75rem;max-width:var(--measure)}
.element>*{margin:0}
.element>*+*{margin-top:.25rem}
.element h4{font-size:1rem;font-weight:600}
.element dl{display:grid;grid-template-columns:auto 1fr;gap:.125rem .75rem;font-size:.875rem}
.element dt{color:var(--muted)}
.element dd{margin:0;min-width:0;overflow-wrap:anywhere}
@media (max-width:520px){.element dl{display:block}.element dd+dt{margin-top:.25rem}}
.undrawn{font-size:.875rem;color:var(--muted);max-width:var(--measure)}
"""


# --- drawing --------------------------------------------------------------

def esc(text):
    return html.escape(text, quote=False)


def wrap(text, width):
    return textwrap.wrap(text, width) or [""]


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


def zoomable(svg):
    """A drawing in its sheet, with the button that opens it full screen."""
    return f'<figure class="sheet" data-scroll="x">\n{svg}\n{zoom.OPENER}\n</figure>'


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
        lanes.append(f"<h4>{esc(heading)}</h4>\n<div class=\"elements\">\n{inner}\n</div>")
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
<h3>The whole chip</h3>
{zoomable(chip(design, netlist, elements, spec, Facts(design)))}
<h3>The time-interval counter, gate by gate</h3>
{zoomable(gates(design, netlist, elements))}
{left_out}
<h3>Every element</h3>
{chr(10).join(lanes)}
</section>"""


def render(netlist_dir):
    sections = [section(design, netlist_dir) for design in DESIGNS]
    nav = "".join(f'<li><a href="#{d.image}">{esc(d.name)}</a></li>' for d in DESIGNS)
    lanes = "".join(f"<li>{esc(heading)}</li>" for _, heading in LANES)
    header = f"""<header>
<p class="eyebrow">SmartClock firmware &middot; FPGA</p>
<h1>FPGA detail</h1>
<p class="lede">Each of the six SmartClock FPGA designs drawn twice from its decoded netlist: the
whole chip by function blocks, then the time-interval counter gate by gate, with every element
listed in full below.  All six come from one generator, in one layout a family, so that the
differences stand out.  The short version is <a href="fpga.html">Inside the SmartClock FPGAs</a>;
the traces are the long one.</p>
<p class="note">Nothing here has been checked on a board; every pin's role is read from the
logic.  Each element carries its trace's confidence word: {mark('read')}, from the decoded
netlist's equations and connections; {mark('simulated')}, printed by a testbench under Icarus
Verilog; {mark('inferred')}, reasoning from those; {mark('open')}, not established.</p>
<nav><ol>{nav}</ol></nav>
</header>"""
    grammar = f"""<section id="reading">
<h2>How to read the drawings</h2>
<div class="key" role="list" aria-label="Key">
<span role="listitem"><span class="sw"></span>nets, read from the netlist</span>
<span role="listitem"><span class="sw sig"></span>the time-interval path</span>
<span role="listitem"><span class="mk sim">S</span>a simulated element in the block</span>
<span role="listitem"><span class="mk inferred">I</span>an inferred one</span>
<span role="listitem"><span class="mk open">?</span>an open one</span>
</div>
<ul>
<li><strong>The whole chip.</strong>  The package pins sit on the frame's edges, the clocks'
on top, the other inputs left and the outputs right, each with its number and role; an output
names its source net and whether its I/O block registers it (<code>reg</code>, on which clock),
inverts it or 3-states it.  Inside, the blocks run in the signal's flow: the bus and the
registers it writes, the read multiplexer, the start, count and stop, the dividers and the 1 PPS,
the sync logic and, in the telecom pair, the P7 divider and the phase detector.  Each block lists
its logic blocks by tile (AB is row A, column B; <code>EI.Y</code> one output of it), its
elements, and an equation or two from the netlist; a divider is drawn as its counters, each with
its modulus from the checked simulations.  A wire is a net between neighboring blocks, named; a
pill at a block (a net flag) is a net it reads from a block or a pin further away.</li>
<li><strong>The time-interval counter, gate by gate.</strong>  One box a logic block on the
path: its clock <code>K</code> (rising or falling, the arrow) and reset <code>RD</code>, each
output's equation and what each flip-flop loads, with the pins named by the nets they read.
<code>&middot;</code> is AND, <code>+</code> OR, <code>&not;</code> NOT.  An arrow joins two
boxes in a row when the second reads the first.</li>
<li>Click a drawing, or the button under it, to open it full screen at its own scale: drag to pan,
the wheel, a pinch or the buttons to zoom, Close or Escape to go back.  Without JavaScript the
drawings stay as they are, and on a narrow screen they scroll sideways inside their frames.  The
list under each design repeats every element in full.</li>
</ul>
<p>The lanes the element lists use, and the elements the generator checks:</p>
<ol class="lanes">{lanes}</ol>
</section>"""
    body = "\n\n".join([header, grammar] + sections) + """

<footer>Generated by <code>tools/fpga/detail.py</code> from the decoded netlists, the element
tables in <code>tools/fpga/detail_designs.py</code> and the checked references in
<code>tools/fpga/ref</code>.</footer>
""" + "\n".join(zoom.dialog("An FPGA drawing, full screen")
                + ["<script>", zoom.script(ZOOM_MEDIA, ".sheet", *ZOOM_HOME).rstrip("\n"), "</script>"])
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


# The chip drawing's cases: each a change to the reference design's
# layout that chip() must refuse, and the words its refusal must contain.
LAYOUT_TEST = (
    ("a wire that carries no net",
     lambda layout: dataclasses.replace(layout, wires=layout.wires + (("seconds", "bus"),)), "carries no net"),
    ("an element in no block",
     lambda layout: dataclasses.replace(layout, blocks=tuple(
         dataclasses.replace(b, elements=tuple(k for k in b.elements if k != "tie")) for b in layout.blocks)),
     "in no block"),
)


def collisions(build):
    """The problems a sheet build() draws."""
    sheet = Sheet()
    build(sheet)
    return " ".join(sheet.problems())


# The collision check's cases: each a sheet with one fault, and the
# words its problem must contain.
GEOMETRY_TEST = (
    ("a wire through a box", lambda s: (s.box(0, 0, 100, 50, "ch-blk"), s.wire([(-10, 25), (200, 25)])),
     "runs through"),
    ("two wires along one line", lambda s: (s.wire([(0, 80), (100, 80)]), s.wire([(50, 80), (150, 80)])),
     "share the line"),
    ("text on text", lambda s: (s.text(0, 20, "AH.X", "ch-net"), s.text(10, 22, "BH.Y", "ch-net")),
     "overlaps text"),
    ("text on a box it is not in", lambda s: (s.box(0, 0, 100, 50, "ch-blk"), s.text(90, 20, "AH.X", "ch-net")),
     "overlaps the ch-blk box"),
    ("text on another wire", lambda s: (s.wire([(0, 18), (100, 18)]), s.text(20, 20, "AH.X", "ch-net")),
     "lies on wire"),
    ("text out of its box", lambda s: s.text(10, 20, "a line far too long for its box", "ch-eq",
                                             s.box(0, 0, 60, 40, "ch-blk")), "runs out of"),
)


def refused(name, words, attempt):
    """Fails unless attempt() is refused with words in its message."""
    try:
        attempt()
    except SystemExit as refusal:
        if words not in str(refusal):
            die(f"self-test: {name}: refused for another reason: {refusal}")
        return
    die(f"self-test: {name}: not refused")


def self_test(netlist_dir):
    """That bind() refuses each of SELF_TEST's changes to the Z3805A's
    table, and chip() each of LAYOUT_TEST's to its layout."""
    design = DESIGNS[0]
    original = SPECS[design.image]
    try:
        for name, provoke, words in SELF_TEST:
            elements, pins = list(original.elements), dict(original.pins)
            provoke(elements, pins)
            SPECS[design.image] = dataclasses.replace(original, elements=tuple(elements), pins=pins)
            refused(name, words, lambda: bind(design, netlist_dir))
    finally:
        SPECS[design.image] = original
    netlist, elements, _ = bind(design, netlist_dir)
    layout = LAYOUTS[design.image]
    try:
        for name, provoke, words in LAYOUT_TEST:
            LAYOUTS[design.image] = (provoke(layout[0]),) + layout[1:]
            refused(name, words, lambda: chip(design, netlist, elements, original, Facts(design)))
    finally:
        LAYOUTS[design.image] = layout
    for name, build, words in GEOMETRY_TEST:
        if words not in collisions(build):
            die(f"self-test: {name}: not found by the collision check")
    count = len(SELF_TEST) + len(LAYOUT_TEST) + len(GEOMETRY_TEST)
    print(f"detail self-test: {count} kinds of error each refused")


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
