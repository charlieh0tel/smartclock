"""detail.py's two drawings of each design: the whole chip by function
blocks, and the time-interval counter gate by gate.

Where a block sits on the sheet is layout, per family (LAYOUTS): the
10 MHz designs share one, the telecom pair another, and a block whose
elements a design lacks is left out.  Everything a block says is the
design's: its CLBs are its elements' (detail_designs.py), its lines
those elements' titles, the counters' moduli figures from the checked
references, and its equations the netlist's.  Every wire is a set of
nets the netlist has between the two blocks, named; a layout wire with
none is an error, and every other connection between blocks is a net
flag at the block that reads it.  Every element but the clocks' is in
exactly one block, or the drawing fails.
"""

import html
import re
import sys
import textwrap
from dataclasses import dataclass, field
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from page import die  # noqa: E402
from sheet import Sheet  # noqa: E402
from detail_bind import FLIP_FLOPS, pin_number, output_notes, source_label  # noqa: E402


@dataclass(frozen=True)
class Block:
    """One function block on the chip drawing."""

    # Its id, unique within its layout.
    key: str
    # Its column, 0 to COLUMNS - 1, left to right in the signal's flow.
    column: int
    # Its title.
    title: str
    # The elements it draws (detail_designs.py keys), those a design has.
    elements: tuple
    # Whether it is on the time-interval path, drawn highlighted.
    ti: bool = False
    # Lines beyond its elements' titles, figures and "{eq:NET}"
    # equations named, as an element's are.
    lines: tuple = ()
    # Draw its elements as a chain of counters, each with its modulus
    # (CHAINS), not as a list of titles.
    chain: bool = False


@dataclass(frozen=True)
class Layout:
    """A family's chip drawing."""

    blocks: tuple
    # The wires drawn, each (from block, to block); every other
    # connection is a net flag.
    wires: tuple
    # A chained element's counter label, by element key.
    chains: dict = field(default_factory=dict)


TEN_MHZ = Layout(
    blocks=(
        Block("bus", 0, "Bus interface", ("strobe", "decode"), lines=("{eq:HL.X}", "{eq:FL.Y}")),
        Block("startside", 0, "Start-side logic", ("mode3", "p79", "p76")),
        Block("fixed", 0, "Fixed and passed through", ("tie", "passthrough", "p19")),
        Block("registers", 1, "Write registers", ("register0", "register1", "register2", "address6")),
        Block("start", 1, "Start and gate", ("start", "gate"), ti=True),
        Block("sync", 1, "Sync and step", ("field", "pending", "step", "restart")),
        Block("readmux", 2, "Read multiplexer", ("phases", "address", "interpolator", "select", "mux01", "mux23")),
        Block("count", 2, "Interval count", ("tick", "head", "stages"), ti=True,
              lines=("{bits} bits: bit 0, bit 1 and {n:stages} two-bit Johnson stages",
                     "preset reads 0x{preset}")),
        Block("divider", 2, "1 PPS divider", ("prescaler", "binary", "decades", "terminal"), chain=True),
        Block("seconds", 3, "Seconds", ("seconds",)),
        Block("stop", 3, "Stop", ("tapbuffer", "stop", "stoppipe"), ti=True),
        Block("output", 3, "1 PPS output", ("driver", "outputs", "alternate")),
    ),
    wires=(("bus", "registers"), ("bus", "sync"), ("registers", "readmux"), ("start", "count"),
           ("start", "readmux"), ("stop", "count"), ("stop", "readmux"), ("count", "readmux"),
           ("divider", "stop"), ("divider", "output"), ("divider", "seconds"), ("sync", "divider")),
    chains={
        "prescaler": "GI.Y every {gi_clocks} clocks, JI.X every {ji_clocks}",
        "binary": "JK.Y every {jk_clocks} clocks",
        "decades": "÷{decade1} ÷{decade2} ÷{decade3}; the tap at {tap} ns",
        "terminal": "÷{divide}",
    },
)

TELECOM = Layout(
    blocks=(
        Block("bus", 0, "Bus interface", ("strobe", "decode"), lines=("{eq:CL.X}",)),
        Block("startside", 0, "Start pipeline", ("pipeline", "p79", "p76")),
        Block("fixed", 0, "Fixed and passed through", ("tie", "passthrough", "lk", "p24")),
        Block("p84", 0, "P84 counter", ("p84counter",)),
        Block("p35", 0, "P35 counter", ("p35clock", "p35five", "p35binary", "p26", "p35decade", "p35reset",
                                        "gclk12", "p29"),
              lines=("KA.Y every {p35_five} LB.Y; P21 every {p35_count}", "GC.X: GCLK ÷{gc_clocks}")),
        Block("registers", 1, "Write registers", ("register0", "register1", "register2")),
        Block("start", 1, "Start and gate", ("source", "start", "gate"), ti=True, lines=("{eq:BH.X}",)),
        Block("sync", 1, "Sync and step", ("field", "gps", "pending", "sample", "step", "strobe3"),
              lines=("{eq:JH.X}",)),
        Block("readmux", 2, "Read multiplexer", ("phases", "interpolator", "select", "mux01", "mux23", "bits47")),
        Block("count", 2, "Interval count", ("tick", "head", "stages"), ti=True,
              lines=("{bits} bits: bit 0, bit 1 and {n:stages} two-bit Johnson stages",
                     "preset reads 0x{preset}")),
        Block("divider", 2, "1 PPS divider", ("prescaler", "gj", "chain", "terminal"), chain=True),
        Block("p7", 2, "P7 divider", ("p7prescaler", "p7counter", "p7terminal"), chain=True),
        Block("seconds", 3, "Seconds", ("seconds",)),
        Block("stop", 3, "Stop", ("stop", "stoppipe"), ti=True, lines=("{eq:CJ.X}",)),
        Block("output", 3, "1 PPS output", ("hold", "register", "p20", "alternate"), lines=("{eq:GH.X}",)),
        Block("pfd", 3, "Phase detector", ("p38", "p40"), lines=("{eq:LE.X}", "{eq:LD.X}")),
    ),
    wires=(("bus", "registers"), ("bus", "sync"), ("registers", "readmux"), ("start", "count"),
           ("start", "readmux"), ("stop", "count"), ("stop", "readmux"), ("count", "readmux"),
           ("divider", "stop"), ("divider", "output"), ("divider", "seconds"), ("seconds", "readmux"),
           ("sync", "divider"), ("sync", "p7"), ("p7", "pfd"), ("divider", "pfd")),
    chains={
        "prescaler": "÷{aclk_prescale} ACLK, ÷{prescale} GCLK",
        "gj": "GJ.Y: one GCLK tick a cycle",
        "chain": "÷{chain}; the tap KI.X at {tap} ns",
        "terminal": "JI.Y: ÷{divide}",
        "p7prescaler": "÷{ke_clocks}",
        "p7counter": "÷{p7_counter}",
        "p7terminal": "P7 ÷{p7_divide}",
    },
)

# Each design's layout, the lines and counter labels it substitutes,
# and the layout's wires it lacks: a design whose blocks the trace calls
# the Z3805A's names the Z3805A's figures ("name@z3805a"), one whose
# nets differ its own equations, and the Z3816A's seconds drive the bus
# pins themselves.
LAYOUTS = {
    "z3805a": (TEN_MHZ, {"start": ("{eq:AH.Y}",), "stop": ("{eq:CJ.X}",), "output": ("{eq:EI.Y}",),
                         "sync": ("{eq:EJ.X}",)}, {}),
    "58503a-z3801a": (TEN_MHZ, {"start": ("{eq:AH.Y}",), "stop": ("{eq:CJ.X}",), "output": ("{eq:EI.Y}",),
                                "sync": ("{eq:EJ.X}",)},
                      {"prescaler": "the Z3805A's: GI.Y every {gi_clocks@z3805a} clocks, JI.X every "
                                    "{ji_clocks@z3805a}",
                       "binary": "the Z3805A's: JK.Y every {jk_clocks@z3805a} clocks",
                       "decades": "the Z3805A's: ÷{decade1@z3805a} ÷{decade2@z3805a} ÷{decade3@z3805a}; "
                                  "the tap at {tap} ns"}),
    "58503b": (TEN_MHZ, {"start": ("{eq:AH.Y}",), "stop": ("{eq:CJ.X}",), "output": ("{eq:HK.X}", "{eq:FH.Y}"),
                         "sync": ("{eq:EJ.X}",), "fixed": ("P19 every {p19_clocks} clocks",)},
               {"prescaler": "the Z3805A's (check-blocks): GI.Y every {gi_clocks@z3805a} clocks, JI.X every "
                             "{ji_clocks@z3805a}",
                "binary": "the Z3805A's: JK.Y every {jk_clocks@z3805a} clocks",
                "decades": "the Z3805A's: ÷{decade1@z3805a} ÷{decade2@z3805a} ÷{decade3@z3805a}; "
                           "the tap at {tap} ns"}),
    "z3815a": (TELECOM, {}, {}),
    "z3816a": (TELECOM, {}, {}, (("seconds", "readmux"),)),
}

# The chip sheet's geometry: its width; the pin columns' and the frame's
# left edges and widths; the blocks' columns, width and gaps; text
# metrics (characters a line holds, line heights) and padding.
COLUMNS = 4
CHIP_W = 1600
PIN_W = 216
LEFT_PIN_X = 8
FRAME_X, FRAME_W = 256, 1088
RIGHT_PIN_X = FRAME_X + FRAME_W + 24
COL_X0, COL_W, COL_GAP = FRAME_X + 22, 216, 60
FRAME_TOP, TOP_PINS = 96, 8
BLOCK_GAP = 44
BLOCK_CHARS, MONO_CHARS, PIN_CHARS = 34, 28, 30
PILL_CHARS = 30
LINE, TITLE, PAD = 16, 20, 10
PILL_H = 20

# The gate-level sheet: its width, the gap between boxes, and a box's
# narrowest and widest.
GATE_W = 1600
GATE_GAP = 56
BOX_MIN, BOX_MAX = 150, 380
STAGE_W = 104

# The rows of the gate-level drawing: their headings and the elements
# whose CLBs they draw, left to right in the signal's flow.
GATE_ROWS = (
    ("Start and gate", ("source", "start", "gate", "tick")),
    ("Stop", ("tapbuffer", "stop", "stoppipe")),
    ("Count, bit 1 upward", ("head", "stages")),
    ("Read path", ("address", "select", "mux01", "mux23")),
)
COMPACT = ("head", "stages")

MARK_LETTERS = {"simulated": ("S", "sim"), "inferred": ("I", "inferred"), "open": ("?", "open")}


def esc(text):
    return html.escape(str(text), quote=False)


def wrap(text, width):
    return textwrap.wrap(text, width, break_on_hyphens=False) or [""]


# --- equations ---------------------------------------------------------------

LETTER = re.compile(r"\b(A|B|C|D|E)\b")


def expression(netlist, clb, text):
    """A netlist equation with its pins named by the nets they read."""
    def name(match):
        net = clb["pins"].get(match.group(1))
        return netlist.label(net) if net is not None else match.group(1)
    return LETTER.sub(name, text).replace(" * ", "·").replace("~", "¬")


def selection(netlist, clb, sel):
    """What a selection (F, G, M or DI) computes."""
    if sel == "DI":
        return netlist.label(clb["pins"]["DI"])
    return expression(netlist, clb, clb["equations"]["F" if sel == "M" else sel] or clb["equations"]["F"])


def clb_lines(netlist, tile):
    """A logic block's clock, reset and equations, each a line."""
    clb = netlist.clbs[tile]
    pins = clb["pins"]
    lines = []
    if "K" in pins:
        lines.append(f"K = {netlist.label(pins['K'])}{'↓' if clb['clockInvert'] else '↑'}"
                     + (f", RD = {netlist.label(pins['RD'])}" if "RD" in pins else ""))
    if "EC" in pins:
        lines.append(f"EC = {netlist.label(pins['EC'])}")
    outputs = netlist.used_outputs(tile)
    shown = {}
    for out in outputs:
        shown.setdefault(clb["outputs"][out], []).append(out)
    flops = []
    for sel, outs in shown.items():
        if sel in FLIP_FLOPS:
            lines.append(f"{' = '.join(outs)} = {sel}")
            flops.append(sel)
        else:
            text = selection(netlist, clb, sel)
            lines.append(f"{' = '.join(outs)} = {text}")
            flops += [q for q in FLIP_FLOPS if re.search(rf"\b{q}\b", text)]
    for q in FLIP_FLOPS:
        if q in flops:
            lines.append(f"{q} ← {selection(netlist, clb, clb['data'][FLIP_FLOPS[q]])}")
    return lines


def net_equation(netlist, net):
    """"NET = ..." for a logic block output, from its block's equations."""
    tile, _, out = net.partition(".")
    if tile not in netlist.clbs or out not in netlist.used_outputs(tile):
        die(f"no logic block output {net}")
    clb = netlist.clbs[tile]
    sel = clb["outputs"][out]
    if sel in FLIP_FLOPS:
        return f"{net} = {sel} ← {selection(netlist, clb, clb['data'][FLIP_FLOPS[sel]])}"
    return f"{net} = {selection(netlist, clb, sel)}"


def fill(template, netlist, facts, elements):
    """A layout line with its equations, element counts and figures."""
    by_key = {e.spec.key: e for e in elements}
    text = re.sub(r"\{eq:([A-L]{2}\.[XY])\}", lambda m: net_equation(netlist, m.group(1)), template)
    stages = len(by_key["stages"].spec.members) if "stages" in by_key else 0
    text = text.replace("{n:stages}", str(stages)).replace("{bits}", str(2 + 2 * stages))
    try:
        return text.format_map(facts)
    except KeyError as error:
        die(f"layout: no figure {error}")


# --- the whole chip ------------------------------------------------------------

def marks(words):
    """The confidence badges for the words other than read, in order."""
    out = []
    for word in ("simulated", "inferred", "open"):
        if word in words:
            out.append(MARK_LETTERS[word])
    return out


def badge(x, y, letter, klass):
    return (f'<g class="ch-mk {klass}"><circle cx="{x}" cy="{y}" r="7.5"/>'
            f'<text x="{x}" y="{y + 3.5}" text-anchor="middle">{letter}</text></g>')


class Placed:
    """A block on the sheet: its content, and where it went."""

    def __init__(self, block, members, lines, chain, pills):
        self.block = block
        self.members = members      # its elements, bound
        self.lines = lines          # its text lines, each (class, text)
        self.chain = chain          # its counters, each (title, CLBs, label)
        self.pills = pills          # its net flags, each text
        self.x = COL_X0 + block.column * (COL_W + COL_GAP)
        self.y = 0
        self.h = 0

    def measure(self):
        self.h = TITLE + PAD + LINE * len(self.lines) + PAD
        self.h += sum(LINE * (1 + len(clbs) + len(label)) + PAD + 14 for _, clbs, label in self.chain)
        self.h += len(self.pills) * (PILL_H + 4)
        return self.h


def chip(design, netlist, elements, spec, facts):
    """The whole chip: pins on its edges, function blocks in the signal's flow."""
    layout, extra, chains, *dropped = LAYOUTS[design.image]
    dropped = set(dropped[0]) if dropped else set()
    labels = {**layout.chains, **chains}
    by_key = {e.spec.key: e for e in elements}
    owner = {}
    placed = []
    for block in layout.blocks:
        members = [by_key[k] for k in block.elements if k in by_key]
        if not members:
            continue
        for e in members:
            owner[e.spec.key] = block.key
        lines = []
        tiles = [m for e in members for m in e.spec.members]
        lines += [("ch-clb", t) for t in wrap(" ".join(tiles), MONO_CHARS)]
        chain = []
        if block.chain:
            for e in members:
                label = fill(labels[e.spec.key], netlist, facts, elements) if e.spec.key in labels else ""
                chain.append((e.spec.title, wrap(" ".join(e.spec.members), MONO_CHARS - 4),
                              wrap(label, MONO_CHARS - 3) if label else []))
        else:
            for e in members:
                lines += [("ch-lab", t) for t in wrap(e.spec.title, BLOCK_CHARS)]
        for template in block.lines + extra.get(block.key, ()):
            lines += [("ch-eq", t) for t in wrap(fill(template, netlist, facts, elements), MONO_CHARS)]
        placed.append(Placed(block, members, lines, chain, []))
    missing = [e.spec.key for e in elements if e.spec.lane != "clocks" and e.spec.key not in owner]
    if missing:
        die(f"{design.image}: elements in no block of the chip drawing: {missing}")
    blocks = {p.block.key: p for p in placed}
    block_of = {e.spec.key: blocks[owner[e.spec.key]] for e in elements if e.spec.key in owner}

    # The nets each block reads from each other, from its elements'.
    reads = {}
    for p in placed:
        for e in p.members:
            for key, nets in e.reads.items():
                if key in block_of and block_of[key] is not p:
                    bucket = reads.setdefault((block_of[key].block.key, p.block.key), [])
                    bucket += [n for n in nets if n not in bucket]
    wires = []
    for source, target in layout.wires:
        if source not in blocks or target not in blocks or (source, target) in dropped:
            continue
        nets = reads.get((source, target))
        if not nets:
            die(f"{design.image}: the chip drawing's wire {source} to {target} carries no net")
        wires.append((blocks[source], blocks[target], nets))
    wired = {(s.block.key, t.block.key) for s, t, _ in wires}
    for (source, target), nets in reads.items():
        if (source, target) not in wired:
            shown = ", ".join(nets[:3]) + (f" +{len(nets) - 3}" if len(nets) > 3 else "")
            blocks[target].pills.append(f"{shown} ← {blocks[source].block.title}")

    # Pins: the clocks' on the top edge, inputs left, outputs right.
    top, left, right = {}, {}, {}
    for e in elements:
        for pin in e.inputs:
            role = spec.pins[pin]
            side = top if e.spec.lane == "clocks" or role.group == "P7" else left
            side.setdefault(role.group, {}).setdefault(pin, []).append(e)
        for pin, iob in e.outputs:
            right.setdefault(spec.pins[pin].group, {}).setdefault(pin, []).append(e)
    for group, pins in left.items():
        flagged = {}
        for pin, readers in pins.items():
            for e in readers:
                p = block_of.get(e.spec.key)
                if p is not None and p.block.column != 0:
                    flagged.setdefault(p.block.key, []).append(pin)
        for key, flags in flagged.items():
            shown = sorted(set(flags), key=pin_number)
            blocks[key].pills.append(f"{' '.join(shown)} ← {group}")

    # Stack each column.
    for column in range(COLUMNS):
        y = FRAME_TOP + 40
        for p in [p for p in placed if p.block.column == column]:
            p.measure()
            p.y = y
            y += p.h + BLOCK_GAP
    frame_bottom = max(p.y + p.h for p in placed) + 30

    out = []
    out.append(f'<rect class="ch-frame" x="{FRAME_X}" y="{FRAME_TOP}" width="{FRAME_W}" '
               f'height="{frame_bottom - FRAME_TOP}" rx="6"/>')
    out.append(f'<text class="ch-clb" x="{FRAME_X + FRAME_W - 10}" y="{frame_bottom - 10}" '
               f'text-anchor="end">XC3042 · {esc(design.name)}</text>')
    pins_bottom = pin_boxes(out, top, left, right, netlist, spec, block_of, facts["loads"])
    for p in placed:
        draw_block(out, p)
    gaps = {}
    for source, target, nets in wires:
        draw_wire(out, source, target, nets, gaps)
    height = max(frame_bottom, max(p.y + p.h for p in placed)) + 20
    height = max(height, pins_bottom + 20)
    return (f'<svg class="ch" viewBox="0 0 {CHIP_W} {height:.0f}" role="img" xmlns="http://www.w3.org/2000/svg" '
            f'aria-label="The {esc(design.name)} FPGA: its function blocks, the nets between them and its pins">\n'
            f'{chip_markers()}\n' + "\n".join(out) + "\n</svg>")


def chip_markers():
    return ('<defs><marker id="ch-a" viewBox="0 0 10 10" refX="9" refY="5" markerUnits="userSpaceOnUse" '
            'markerWidth="9" markerHeight="9" orient="auto-start-reverse"><path class="ch-ah" d="M0,0 L10,5 L0,10 z"/>'
            '</marker><marker id="ch-as" viewBox="0 0 10 10" refX="9" refY="5" markerUnits="userSpaceOnUse" '
            'markerWidth="9" markerHeight="9" orient="auto-start-reverse"><path class="ch-ah sig" '
            'd="M0,0 L10,5 L0,10 z"/></marker></defs>')


def draw_block(out, p):
    klass = "ch-blk ti" if p.block.ti else "ch-blk"
    out.append(f'<rect class="{klass}" x="{p.x}" y="{p.y:.1f}" width="{COL_W}" height="{p.h:.1f}" rx="3"/>')
    title_class = "ch-ttl sig" if p.block.ti else "ch-ttl"
    out.append(f'<text class="{title_class}" x="{p.x + PAD}" y="{p.y + TITLE:.1f}">{esc(p.block.title)}</text>')
    for i, (letter, klass) in enumerate(reversed(marks({e.spec.confidence for e in p.members}))):
        out.append(badge(p.x + COL_W - 14 - 20 * i, p.y + 14, letter, klass))
    y = p.y + TITLE + PAD
    for klass, text in p.lines:
        y += LINE
        out.append(f'<text class="{klass}" x="{p.x + PAD}" y="{y - 4:.1f}">{esc(text)}</text>')
    previous = None
    for title, clbs, label in p.chain:
        y += 8
        h = LINE * (1 + len(clbs) + len(label)) + PAD
        out.append(f'<rect class="ch-ctr" x="{p.x + PAD}" y="{y:.1f}" width="{COL_W - 2 * PAD}" '
                   f'height="{h}" rx="3"/>')
        if previous is not None:
            out.append(f'<line class="ch-w" x1="{p.x + COL_W / 2}" y1="{previous:.1f}" x2="{p.x + COL_W / 2}" '
                       f'y2="{y - 1:.1f}" marker-end="url(#ch-a)"/>')
        ty = y + LINE
        out.append(f'<text class="ch-ctrt" x="{p.x + 2 * PAD}" y="{ty - 3:.1f}">{esc(title)}</text>')
        for text in clbs:
            ty += LINE
            out.append(f'<text class="ch-clb" x="{p.x + 2 * PAD}" y="{ty - 3:.1f}">{esc(text)}</text>')
        for text in label:
            ty += LINE
            out.append(f'<text class="ch-mod" x="{p.x + 2 * PAD}" y="{ty - 3:.1f}">{esc(text)}</text>')
        previous = y + h
        y += h + 6
    for text in p.pills:
        y += 4
        shown = text if len(text) <= PILL_CHARS else text[:PILL_CHARS - 1] + "…"
        out.append(f'<rect class="ch-pill" x="{p.x + PAD}" y="{y:.1f}" width="{16 + 6.6 * len(shown):.0f}" '
                   f'height="{PILL_H}" rx="{PILL_H / 2}"/>')
        out.append(f'<text class="ch-pt" x="{p.x + PAD + 8}" y="{y + 14:.1f}">{esc(shown)}</text>')
        y += PILL_H


def draw_wire(out, source, target, nets, gaps):
    """A wire between two blocks, through the gap between their columns."""
    ti = source.block.ti and target.block.ti
    klass, marker = ("ch-w sig", "ch-as") if ti else ("ch-w", "ch-a")
    label = nets[0] + (" …" if len(nets) > 1 else "")
    sc, tc = source.block.column, target.block.column
    if sc == tc:
        top, bottom = (source, target) if source.y < target.y else (target, source)
        x = source.x + COL_W * (0.3 if source is top else 0.7)
        y1, y2 = (top.y + top.h, bottom.y) if source is top else (bottom.y, top.y + top.h)
        out.append(f'<line class="{klass}" x1="{x:.0f}" y1="{y1:.1f}" x2="{x:.0f}" y2="{y2 + (-2 if y2 > y1 else 2):.1f}" '
                   f'marker-end="url(#{marker})"/>')
        out.append(f'<text class="ch-net{" sig" if ti else ""}" x="{x + 6:.0f}" y="{(y1 + y2) / 2 + 4:.1f}">'
                   f'{esc(label)}</text>')
        return
    if abs(sc - tc) != 1:
        die(f"layout: {source.block.key} and {target.block.key} are not in neighboring columns")
    rightward = tc > sc
    x1 = source.x + (COL_W if rightward else 0)
    x2 = target.x + (0 if rightward else COL_W)
    low, high = max(source.y, target.y) + 12, min(source.y + source.h, target.y + target.h) - 12
    gap = min(sc, tc)
    used = gaps.setdefault(gap, [])
    if high - low >= 16:
        y = low + (high - low) * (0.25 + 0.2 * (len(used) % 3))
        used.append(y)
        out.append(f'<line class="{klass}" x1="{x1}" y1="{y:.1f}" x2="{x2 + (-2 if rightward else 2)}" y2="{y:.1f}" '
                   f'marker-end="url(#{marker})"/>')
        out.append(f'<text class="ch-net{" sig" if ti else ""}" x="{min(x1, x2) + 4}" y="{y - 5:.1f}">'
                   f'{esc(label)}</text>')
        return
    xm = min(x1, x2) + COL_GAP / 2 + 10 * ((len(used) % 3) - 1)
    used.append(xm)
    y1 = source.y + min(source.h - 12, 24 + 14 * (len(used) % 4))
    y2 = target.y + min(target.h - 12, 24 + 14 * (len(used) % 4))
    out.append(f'<polyline class="{klass}" points="{x1},{y1:.1f} {xm:.0f},{y1:.1f} {xm:.0f},{y2:.1f} '
               f'{x2 + (-2 if rightward else 2)},{y2:.1f}" marker-end="url(#{marker})"/>')
    out.append(f'<text class="ch-net{" sig" if ti else ""}" x="{xm + 4:.0f}" y="{(y1 + y2) / 2:.1f}">'
               f'{esc(label)}</text>')


def pin_text(pins, spec, netlist, outputs):
    """A pin group's lines: number and role, and an output's source."""
    lines = []
    for pin in sorted(pins, key=pin_number):
        lines += wrap(f"{pin} {spec.pins[pin].role}", PIN_CHARS)
        if pin in outputs:
            iob = outputs[pin]
            lines += wrap("← " + ", ".join([source_label(netlist, iob)] + output_notes(netlist, iob)), PIN_CHARS)
    return lines


def clock_lines(netlist, loads, pin):
    """What a pin clocks, from the .stats clock loads: itself through
    general routing, the buffer it drives, and a buffer a block it clocks
    drives (GCLK from AB, ACLK halved)."""
    pad = netlist.name(netlist.iobs[pin]["pins"]["I"])
    lines = [f"{pin} clocks {loads[pad]} logic blocks directly"] if pad in loads else []
    for name, buffer in netlist.buffers.items():
        source = netlist.name(buffer["pins"]["I"])
        tile, _, _ = source.partition(".")
        via = ""
        if tile in netlist.clbs:
            clock = netlist.clbs[tile]["pins"].get("K")
            driver = netlist.name(clock) if clock is not None else None
            upstream = next((n for n, b in netlist.buffers.items() if f"{n}.O" == driver), None)
            if upstream is None or netlist.name(netlist.buffers[upstream]["pins"]["I"]) != pad:
                continue
            via = f", from {source}"
        elif source != pad:
            continue
        lines.append(f"{name}{via}: {loads[f'{name}.O']} logic blocks")
    return lines


def pin_boxes(out, top, left, right, netlist, spec, block_of, loads):
    """Each side's pin groups, each by the first block it meets; the
    lowest pin box's bottom."""
    bottom = 0
    iobs = netlist.iobs
    x = FRAME_X + 40
    for group, pins in top.items():
        text = [f"{pin} {spec.pins[pin].role}" for pin in sorted(pins, key=pin_number)]
        for pin in sorted(pins, key=pin_number):
            text += clock_lines(netlist, loads, pin)
        readers = sorted({block_of[e.spec.key].block.title for es in pins.values() for e in es
                          if e.spec.key in block_of})
        if readers:
            text += wrap("into " + ", ".join(readers), 60)
        w = 20 + 6.6 * max(len(t) for t in text)
        h = LINE * len(text) + 10
        out.append(f'<rect class="ch-pin" x="{x}" y="{TOP_PINS}" width="{w:.0f}" height="{h}" rx="3"/>')
        for i, t in enumerate(text):
            out.append(f'<text class="ch-p" x="{x + 10}" y="{TOP_PINS + LINE * (i + 1) + 1}">{esc(t)}</text>')
        out.append(f'<line class="ch-w" x1="{x + 30}" y1="{TOP_PINS + h}" x2="{x + 30}" y2="{FRAME_TOP - 2}" '
                   f'marker-end="url(#ch-a)"/>')
        x += w + 24
    for side, groups, px in (("in", left, LEFT_PIN_X), ("out", right, RIGHT_PIN_X)):
        entries = []
        for group, pins in groups.items():
            readers = [e for es in pins.values() for e in es]
            first = min((block_of[e.spec.key] for e in readers if e.spec.key in block_of),
                        key=lambda p: (p.block.column if side == "in" else -p.block.column, p.y))
            outputs = {pin: iob for pin, iob in iobs.items() if pin in pins} if side == "out" else {}
            lines = pin_text(pins, spec, netlist, outputs)
            entries.append((first.y + 20, group, lines, first))
        y = FRAME_TOP
        for want, group, lines, first in sorted(entries, key=lambda item: item[0]):
            h = LINE * (len(lines) + 1) + 8
            y = max(y, want - 10)
            out.append(f'<rect class="ch-pin" x="{px}" y="{y:.1f}" width="{PIN_W}" height="{h}" rx="3"/>')
            out.append(f'<text class="ch-clb" x="{px + 8}" y="{y + LINE - 2:.1f}">{esc(group)}</text>')
            for i, t in enumerate(lines):
                out.append(f'<text class="ch-p" x="{px + 8}" y="{y + LINE * (i + 2) - 2:.1f}">{esc(t)}</text>')
            mid = y + h / 2
            adjacent = first.block.column == (0 if side == "in" else COLUMNS - 1)
            inside = first.y + 6 <= mid <= first.y + first.h - 6
            if side == "in":
                end = first.x - 2 if adjacent and inside else FRAME_X + 14
                out.append(f'<line class="ch-w" x1="{px + PIN_W}" y1="{mid:.1f}" x2="{end}" y2="{mid:.1f}" '
                           f'marker-end="url(#ch-a)"/>')
            else:
                start = first.x + COL_W if adjacent and inside else FRAME_X + FRAME_W - 14
                out.append(f'<line class="ch-w" x1="{start}" y1="{mid:.1f}" x2="{px - 2}" y2="{mid:.1f}" '
                           f'marker-end="url(#ch-a)"/>')
                if not (adjacent and inside):
                    out.append(f'<text class="ch-net" x="{start - 4}" y="{mid - 5:.1f}" text-anchor="end">'
                               f'{esc(first.block.title)}</text>')
            if side == "in" and not (adjacent and inside):
                out.append(f'<text class="ch-net" x="{FRAME_X + 18}" y="{mid - 5:.1f}">→ {esc(first.block.title)}'
                           f'</text>')
            y += h + 6
        bottom = max(bottom, y)
    return bottom


# --- the time-interval counter, gate by gate ---------------------------------------

def badge_on(sheet, x, y, letter, klass, parent):
    """A confidence badge centered on x, y, in parent."""
    circle = sheet.box(x - 7.5, y - 7.5, 15, 15, f"ch-badge {klass}", parent, circle=True)
    sheet.text(x, y + 3.5, letter, f"ch-mk {klass}", circle, anchor="middle")


def gates(design, netlist, elements):
    """One box a CLB on the time-interval path, with its netlist equations."""
    by_key = {e.spec.key: e for e in elements}
    sheet = Sheet()
    y = 16
    for heading, keys in GATE_ROWS:
        tiles = [(m.partition(".")[0], by_key[k]) for k in keys if k in by_key for m in by_key[k].spec.members]
        if not tiles:
            continue
        sheet.text(16, y + 12, heading.upper(), "ch-clb")
        y += 24
        x = 16
        row_h = 0
        previous = None
        boxes = []
        bit = 1
        for tile, e in tiles:
            compact = e.spec.key in COMPACT
            if compact:
                clb = netlist.clbs[tile]
                width = 1 if e.spec.key == "head" else 2
                lines = [f"bit {bit}" if width == 1 else f"bits {bit}–{bit + 1}",
                         f"K = {netlist.label(clb['pins']['K'])}{'↓' if clb['clockInvert'] else '↑'}"]
                bit += width
                w = STAGE_W
            else:
                lines = [t for line in clb_lines(netlist, tile) for t in wrap(line, 52)]
                w = max(BOX_MIN, min(BOX_MAX, 24 + 6.7 * max([len(t) for t in lines] + [len(e.spec.title) + 6])))
            h = TITLE + PAD + LINE * (len(lines) + (0 if compact else 1)) + PAD
            if x + w > GATE_W - 16:
                x = 16
                y += row_h + 36
                row_h = 0
                previous = None
            boxes.append((tile, e, x, y, w, h, lines, compact, previous))
            previous = (tile, x, y, w, h)
            x += w + GATE_GAP
            row_h = max(row_h, h)
        for tile, e, bx, by, w, h, lines, compact, prev in boxes:
            box = sheet.box(bx, by, w, h, "ch-blk ti")
            sheet.text(bx + PAD, by + TITLE, tile, "ch-ttl", box)
            for letter, klass in marks({e.spec.confidence}):
                badge_on(sheet, bx + w - 14, by + 14, letter, klass, box)
            if not compact:
                sheet.text(bx + PAD + 30, by + TITLE, e.spec.title, "ch-lab", box)
            ty = by + TITLE + PAD
            for t in lines:
                ty += LINE
                sheet.text(bx + PAD, ty - 3, t, "ch-eq", box)
            if prev is not None:
                ptile, px, py, pw, ph = prev
                nets = [n for n in (netlist.name(netlist.clbs[ptile]["pins"][o]) for o in netlist.used_outputs(ptile))
                        if n in {netlist.name(v) for v in netlist.clbs[tile]["pins"].values()}]
                if nets:
                    ay = by + min(h, ph) / 2
                    wire = sheet.wire([(px + pw, ay), (bx - 2, ay)], "ch-w sig", "ch-as")
                    sheet.text(px + pw + 4, ay - 5, nets[0], "ch-net sig", wire=wire)
        y += row_h + 40
    return sheet.svg(GATE_W, y, f"The {design.name} time-interval counter, one box a logic block, with its "
                     f"equations from the netlist", chip_markers())
