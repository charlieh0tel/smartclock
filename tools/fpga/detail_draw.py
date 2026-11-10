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

Wires run orthogonally through the channels between columns, each on
a track and at heights of its own, so that none shares a line with
another or crosses a block; a wire between blocks that are not
neighbors is a net flag instead.  A wire's label goes on the first
clear spot along it.  Each drawing is then checked for collisions
(sheet.py) and fails on any.
"""

import re
import sys
import textwrap
from dataclasses import dataclass, field
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from page import die  # noqa: E402
from sheet import Sheet, metrics, text_width  # noqa: E402
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
COL_W, CHANNEL = 264, 176
PAD, LINE, TITLE = 14, 16, 26
INNER = COL_W - 2 * PAD
BLOCK_GAP, FRAME_PAD = 72, 44
TOP_Y, TOP_GAP = 16, 60
PIN_W, PIN_GAP, PIN_MARGIN = 256, 10, 84
LEFT_PIN_X = 16
FRAME_X = LEFT_PIN_X + PIN_W + PIN_MARGIN
COL_X0 = FRAME_X + FRAME_PAD
FRAME_W = 2 * FRAME_PAD + COLUMNS * COL_W + (COLUMNS - 1) * CHANNEL
RIGHT_PIN_X = FRAME_X + FRAME_W + PIN_MARGIN
CHIP_W = RIGHT_PIN_X + PIN_W + LEFT_PIN_X
# How far apart two wires meeting one side of a block, two horizontal
# runs in one channel, and two tracks in one channel are kept; how far
# a channel's first track is from its edge.
PORT_SPACING, ROW_SPACING, TRACK_SPACING, TRACK_MARGIN = 14, 12, 14, 22
COUNTER_GAP, PILL_GAP, PILL_PAD = 12, 6, 8

# The gate-level sheet: its width, the gaps between boxes and between
# rows, and a box's narrowest and widest.
GATE_W = 2000
GATE_GAP, GATE_ROW_GAP = 88, 56
BOX_MIN, BOX_MAX = 160, 520
STAGE_W = 116

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
    return [MARK_LETTERS[word] for word in ("simulated", "inferred", "open") if word in words]


def chip_markers():
    return ('<defs><marker id="ch-a" viewBox="0 0 10 10" refX="9" refY="5" markerUnits="userSpaceOnUse" '
            'markerWidth="9" markerHeight="9" orient="auto-start-reverse"><path class="ch-ah" d="M0,0 L10,5 L0,10 z"/>'
            '</marker><marker id="ch-as" viewBox="0 0 10 10" refX="9" refY="5" markerUnits="userSpaceOnUse" '
            'markerWidth="9" markerHeight="9" orient="auto-start-reverse"><path class="ch-ah sig" '
            'd="M0,0 L10,5 L0,10 z"/></marker></defs>')


def badge_on(sheet, x, y, letter, klass, parent):
    """A confidence badge centered on x, y, in parent."""
    circle = sheet.box(x - 7.5, y - 7.5, 15, 15, f"ch-badge {klass}", parent, circle=True)
    sheet.text(x, y + 3.5, letter, f"ch-mk {klass}", circle, anchor="middle")


def fit(text, cls, width):
    """text wrapped to lines that fit width px in cls's font."""
    return wrap(text, max(8, int(width // metrics(cls)[1])))


def column_x(column):
    return COL_X0 + column * (COL_W + CHANNEL)


class Placed:
    """A block on the sheet: what it says, and where it went."""

    def __init__(self, block, members):
        self.block = block
        self.members = members      # its elements, bound
        self.lines = []             # its text lines, each (class, text)
        self.chain = []             # its counters, each (title, CLB lines, label lines)
        self.pills = []             # its net flags, each a text
        self.x = column_x(block.column)
        self.y = 0
        self.h = 0
        self.box = None
        self.ports = {"left": [], "right": [], "top": [], "bottom": []}

    def pill_lines(self, text):
        return fit(text, "ch-pt", INNER - 2 * PILL_PAD)

    def measure(self):
        self.h = TITLE + PAD + LINE * len(self.lines) + PAD
        self.h += sum(LINE * (1 + len(clbs) + len(label)) + PAD + COUNTER_GAP for _, clbs, label in self.chain)
        self.h += sum(LINE * len(self.pill_lines(text)) + 8 + PILL_GAP for text in self.pills)
        return self.h

    def edge(self, side):
        return {"left": self.x, "right": self.x + COL_W}[side]

    def port_range(self, side):
        """The ys a wire may meet a side at: clear of the title."""
        return self.y + TITLE + PAD, self.y + self.h - PAD

    def free(self, side, y):
        return all(abs(y - used) >= PORT_SPACING for used in self.ports[side])


def content(block, members, netlist, facts, elements, labels, extra):
    """A block's lines and counters, before its net flags."""
    p = Placed(block, members)
    tiles = " ".join(m for e in members for m in e.spec.members)
    p.lines += [("ch-clb", t) for t in fit(tiles, "ch-clb", INNER)]
    if block.chain:
        for e in members:
            label = fill(labels[e.spec.key], netlist, facts, elements) if e.spec.key in labels else ""
            p.chain.append((e.spec.title, fit(" ".join(e.spec.members), "ch-clb", INNER - 2 * PAD),
                            fit(label, "ch-mod", INNER - 2 * PAD) if label else []))
    else:
        p.lines += [("ch-lab", t) for t in fit(" · ".join(e.spec.title for e in members), "ch-lab", INNER)]
    for template in block.lines + extra.get(block.key, ()):
        p.lines += [("ch-eq", t) for t in fit(fill(template, netlist, facts, elements), "ch-eq", INNER)]
    return p


def chip(design, netlist, elements, spec, facts):
    """The whole chip: pins on its edges, function blocks in the signal's flow."""
    layout, extra, chains, *dropped = LAYOUTS[design.image]
    dropped = set(dropped[0]) if dropped else set()
    labels = {**layout.chains, **chains}
    by_key = {e.spec.key: e for e in elements}
    owner, placed = {}, []
    for block in layout.blocks:
        members = [by_key[k] for k in block.elements if k in by_key]
        if not members:
            continue
        for e in members:
            owner[e.spec.key] = block.key
        placed.append(content(block, members, netlist, facts, elements, labels, extra))
    missing = [e.spec.key for e in elements if e.spec.lane != "clocks" and e.spec.key not in owner]
    if missing:
        die(f"{design.image}: elements in no block of the chip drawing: {missing}")
    blocks = {p.block.key: p for p in placed}
    block_of = {key: blocks[b] for key, b in owner.items()}

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
            blocks[target].pills.append(f"{', '.join(nets)} ← {blocks[source].block.title}")

    # Pins: the clocks' on the top edge, inputs left, outputs right; an
    # input read by a block not on the left edge is a net flag there.
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
                    flagged.setdefault(p.block.key, set()).add(pin)
        for key, flags in flagged.items():
            blocks[key].pills.append(f"{' '.join(sorted(flags, key=pin_number))} ← {group}")

    sheet = Sheet()
    top_bottom = top_pins(sheet, top, netlist, spec, block_of, facts["loads"])
    frame_top = top_bottom + TOP_GAP
    for column in range(COLUMNS):
        y = frame_top + FRAME_PAD
        for p in [p for p in placed if p.block.column == column]:
            p.measure()
            p.y = y
            y += p.h + BLOCK_GAP
    for p in placed:
        draw_block(sheet, p)
    channels = Channels()
    routes = [route(sheet, source, target, nets, placed, channels) for source, target, nets in wires]
    pins_bottom = side_pins(sheet, left, right, netlist, spec, block_of)
    frame_bottom = max(max(p.y + p.h for p in placed) + FRAME_PAD, pins_bottom)
    sheet.box(FRAME_X, frame_top, FRAME_W, frame_bottom - frame_top, "ch-frame", rx=6, solid=False)
    sheet.text(FRAME_X + FRAME_W - PAD, frame_bottom - PAD, f"XC3042 · {design.name}", "ch-clb", anchor="end")
    for wire, nets, ti in routes:
        label(sheet, wire, nets[0] + (" …" if len(nets) > 1 else ""), ti)
    problems = sheet.problems()
    if problems:
        die(f"{design.image}: the chip drawing collides: " + "; ".join(problems[:6]))
    return sheet.svg(CHIP_W, frame_bottom + PAD, f"The {design.name} FPGA: its function blocks, the nets between "
                     f"them and its pins", chip_markers())


def draw_block(sheet, p):
    """A block: its title and badges, lines, counters and net flags."""
    ti = p.block.ti
    p.box = box = sheet.box(p.x, p.y, COL_W, p.h, "ch-blk ti" if ti else "ch-blk")
    badges = marks({e.spec.confidence for e in p.members})
    sheet.text(p.x + PAD, p.y + TITLE, p.block.title, "ch-ttl sig" if ti else "ch-ttl", box)
    for i, (letter, klass) in enumerate(reversed(badges)):
        badge_on(sheet, p.x + COL_W - PAD - 7.5 - 20 * i, p.y + TITLE - 5, letter, klass, box)
    y = p.y + TITLE + PAD
    for cls, text in p.lines:
        y += LINE
        sheet.text(p.x + PAD, y - 4, text, cls, box)
    previous = None
    for title, clbs, label_lines in p.chain:
        y += COUNTER_GAP
        h = LINE * (1 + len(clbs) + len(label_lines)) + PAD
        counter = sheet.box(p.x + PAD, y, INNER, h, "ch-ctr", box)
        if previous is not None:
            sheet.wire([(p.x + COL_W / 2, previous), (p.x + COL_W / 2, y - 1)], box=box)
        ty = y + 2
        for cls, text in [("ch-ctrt", title)] + [("ch-clb", t) for t in clbs] + [("ch-mod", t) for t in label_lines]:
            ty += LINE
            sheet.text(p.x + 2 * PAD, ty - 4, text, cls, counter)
        previous = y + h
        y += h
    for text in p.pills:
        y += PILL_GAP
        lines = p.pill_lines(text)
        width = 2 * PILL_PAD + max(text_width("ch-pt", t) for t in lines)
        h = LINE * len(lines) + 8
        pill = sheet.box(p.x + PAD, y, width, h, "ch-pill", box, rx=min(10, h / 2))
        for i, t in enumerate(lines):
            sheet.text(p.x + PAD + PILL_PAD, y + 4 + LINE * (i + 1) - 4, t, "ch-pt", pill)
        y += h


def nearest_free(candidates, ok):
    """The first candidate y that ok() accepts, or None."""
    return next((y for y in candidates if ok(y)), None)


def around(low, high, want, step=4):
    """The ys from low to high, nearest want first."""
    ys = [low + step * i for i in range(int((high - low) // step) + 1)]
    return sorted(ys, key=lambda y: abs(y - want))


class Channels:
    """What the wires already take: each gap's horizontal ys and vertical
    tracks, and each row gap's verticals."""

    def __init__(self):
        self.rows = {}      # a gap between columns: the ys its horizontal segments run at
        self.tracks = {}    # a gap between columns: the xs its vertical segments run at


def route(sheet, source, target, nets, placed, channels):
    """An orthogonal route between two blocks; (wire, nets, ti)."""
    ti = source.block.ti and target.block.ti
    cls, marker = ("ch-w sig", "ch-as") if ti else ("ch-w", "ch-a")
    sc, tc = source.block.column, target.block.column
    if sc == tc:
        column = [p for p in placed if p.block.column == sc]
        upper, lower = sorted((source, target), key=lambda p: p.y)
        if column.index(lower) - column.index(upper) != 1:
            die(f"layout: {source.block.key} and {target.block.key} are not neighbors in their column")
        x = next(x for x in [source.x + COL_W * f for f in (0.5, 0.3, 0.7, 0.2, 0.8)]
                 if upper.free("bottom", x) and lower.free("top", x))
        upper.ports["bottom"].append(x)
        lower.ports["top"].append(x)
        if source is upper:
            points = [(x, upper.y + upper.h), (x, lower.y - 2)]
        else:
            points = [(x, lower.y), (x, upper.y + upper.h + 2)]
        return sheet.wire(points, cls, marker), nets, ti
    if abs(sc - tc) != 1:
        die(f"layout: {source.block.key} and {target.block.key} are not in neighboring columns")
    rightward = tc > sc
    out_side, in_side = ("right", "left") if rightward else ("left", "right")
    gap = min(sc, tc)
    rows = channels.rows.setdefault(gap, [])
    tracks = channels.tracks.setdefault(gap, [])
    x1, x2 = source.edge(out_side), target.edge(in_side)
    end = x2 - 2 if rightward else x2 + 2

    def row_free(y):
        return all(abs(y - used) >= ROW_SPACING for used in rows)

    low = max(source.port_range(out_side)[0], target.port_range(in_side)[0])
    high = min(source.port_range(out_side)[1], target.port_range(in_side)[1])
    if high > low:
        y = nearest_free(around(low, high, (low + high) / 2),
                         lambda y: row_free(y) and source.free(out_side, y) and target.free(in_side, y))
        if y is not None:
            rows.append(y)
            source.ports[out_side].append(y)
            target.ports[in_side].append(y)
            return sheet.wire([(x1, y), (end, y)], cls, marker), nets, ti
    gap_left = column_x(gap) + COL_W
    x = next(gap_left + TRACK_MARGIN + TRACK_SPACING * i for i in range(64)
             if gap_left + TRACK_MARGIN + TRACK_SPACING * i not in tracks)
    if x > gap_left + CHANNEL - TRACK_MARGIN:
        die(f"layout: the channel after column {gap} has no track left")
    tracks.append(x)
    s_low, s_high = source.port_range(out_side)
    t_low, t_high = target.port_range(in_side)
    y1 = nearest_free(around(s_low, s_high, (t_low + t_high) / 2),
                      lambda y: row_free(y) and source.free(out_side, y))
    y2 = nearest_free(around(t_low, t_high, y1 if y1 is not None else t_low),
                      lambda y: row_free(y) and target.free(in_side, y) and abs(y - (y1 or 0)) >= ROW_SPACING)
    if y1 is None or y2 is None:
        die(f"layout: no room for a wire from {source.block.key} to {target.block.key}")
    rows += [y1, y2]
    source.ports[out_side].append(y1)
    target.ports[in_side].append(y2)
    return sheet.wire([(x1, y1), (x, y1), (x, y2), (end, y2)], cls, marker), nets, ti


def label(sheet, wire, text, ti):
    """A wire's net name, on the first clear spot along it."""
    cls = "ch-net sig" if ti else "ch-net"
    width = text_width(cls, text)
    size = metrics(cls)[0]
    for s in [s for s in sheet.segments if s.wire == wire]:
        x0, y0, x1, y1 = s.bounds()
        if y0 == y1:
            spots = [(x, y0 - 4) for x in range(int(x0) + 6, int(x1 - width) - 2, 6)]
            spots += [(x, y0 + size + 3) for x in range(int(x0) + 6, int(x1 - width) - 2, 6)]
        else:
            spots = [(x0 + 5, y) for y in range(int(y0 + size) + 4, int(y1) - 4, 6)]
            spots += [(x0 - 5 - width, y) for y in range(int(y0 + size) + 4, int(y1) - 4, 6)]
        for x, y in spots:
            bounds = (x, y - 0.8 * size, x + width, y + 0.22 * size)
            if sheet.clear((bounds[0] - 2, bounds[1] - 2, bounds[2] + 2, bounds[3] + 2), wire):
                sheet.text(x, y, text, cls, wire=wire)
                return


def pin_lines(pins, spec, netlist, outputs):
    """A pin group's lines: number and role, and an output's source."""
    lines = []
    for pin in sorted(pins, key=pin_number):
        lines += [("ch-p", t) for t in fit(f"{pin} {spec.pins[pin].role}", "ch-p", PIN_W - 2 * PAD)]
        if pin in outputs:
            iob = outputs[pin]
            note = "← " + ", ".join([source_label(netlist, iob)] + output_notes(netlist, iob))
            lines += [("ch-eq", t) for t in fit(note, "ch-eq", PIN_W - 2 * PAD)]
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


def top_pins(sheet, top, netlist, spec, block_of, loads):
    """The clocks' pin groups above the frame; their boxes' bottom."""
    x, bottom = FRAME_X + FRAME_PAD, TOP_Y
    for group, pins in top.items():
        lines = [f"{pin} {spec.pins[pin].role}" for pin in sorted(pins, key=pin_number)]
        for pin in sorted(pins, key=pin_number):
            lines += clock_lines(netlist, loads, pin)
        readers = sorted({block_of[e.spec.key].block.title for es in pins.values() for e in es
                          if e.spec.key in block_of})
        if readers:
            lines += fit("into " + ", ".join(readers), "ch-p", 420)
        w = 2 * PAD + max(text_width("ch-p", t) for t in lines)
        h = LINE * len(lines) + PAD
        box = sheet.box(x, TOP_Y, w, h, "ch-pin")
        for i, t in enumerate(lines):
            sheet.text(x + PAD, TOP_Y + LINE * (i + 1) - 2, t, "ch-p", box)
        bottom = max(bottom, TOP_Y + h)
        x += w + 2 * PAD
    for b in sheet.boxes:
        sheet.wire([(b.x + 3 * PAD, b.y + b.h), (b.x + 3 * PAD, bottom + TOP_GAP - 2)])
    return bottom


def side_pins(sheet, left, right, netlist, spec, block_of):
    """Each side's pin groups, each by the first block it meets, wired to
    it when that block is on the frame's edge; the lowest box's bottom."""
    bottom = 0
    for side, groups, px in (("in", left, LEFT_PIN_X), ("out", right, RIGHT_PIN_X)):
        edge_column = 0 if side == "in" else COLUMNS - 1
        entries = []
        for group, pins in groups.items():
            readers = [e for es in pins.values() for e in es if e.spec.key in block_of]
            targets = sorted({block_of[e.spec.key] for e in readers}, key=lambda p: (p.block.column != edge_column,
                                                                                       p.y))
            outputs = {pin: netlist.iobs[pin] for pin in pins} if side == "out" else {}
            lines = [("ch-clb", group)] + pin_lines(pins, spec, netlist, outputs)
            edge = targets[0] if targets[0].block.column == edge_column else None
            entries.append(((edge or targets[0]).y + TITLE + PAD, lines, targets, edge))
        side_name = "left" if side == "in" else "right"
        y = 0
        for want, lines, targets, edge in sorted(entries, key=lambda item: item[0]):
            y = max(y, want - LINE)
            h = LINE * len(lines) + PAD
            mid = None
            if edge is not None:
                low, high = edge.port_range(side_name)
                mid = nearest_free(around(y + 8, y + h - 8, y + h / 2),
                                   lambda m: low <= m <= high and edge.free(side_name, m))
            # The blocks the drawing would not otherwise join it to: an
            # input's readers off the edge carry net flags of their own.
            unjoined = [t for t in targets if not (mid is not None and t is edge)
                        and (side == "out" or t.block.column == edge_column)]
            if unjoined:
                titles = ", ".join(t.block.title for t in unjoined)
                lines = lines + [("ch-net", t) for t in fit(("to " if side == "in" else "from ") + titles,
                                                            "ch-net", PIN_W - 2 * PAD)]
                h = LINE * len(lines) + PAD
            box = sheet.box(px, y, PIN_W, h, "ch-pin")
            for i, (cls, t) in enumerate(lines):
                sheet.text(px + PAD, y + LINE * (i + 1) - 2, t, cls, box)
            if mid is not None:
                edge.ports[side_name].append(mid)
                if side == "in":
                    sheet.wire([(px + PIN_W, mid), (edge.x - 2, mid)])
                else:
                    sheet.wire([(edge.x + COL_W, mid), (px - 2, mid)])
            else:
                mid = y + h / 2
                if side == "in":
                    sheet.wire([(px + PIN_W, mid), (FRAME_X - 2, mid)])
                else:
                    sheet.wire([(FRAME_X + FRAME_W, mid), (px - 2, mid)])
            y += h + PIN_GAP
        bottom = max(bottom, y)
    return bottom


# --- the time-interval counter, gate by gate ---------------------------------------

def gate_box(netlist, tile, e, bit):
    """A gate-level box's lines and width; the next count bit."""
    if e.spec.key in COMPACT:
        clb = netlist.clbs[tile]
        width = 1 if e.spec.key == "head" else 2
        lines = [f"bit {bit}" if width == 1 else f"bits {bit}–{bit + 1}",
                 f"K = {netlist.label(clb['pins']['K'])}{'↓' if clb['clockInvert'] else '↑'}"]
        return lines, STAGE_W, bit + width
    lines = [t for line in clb_lines(netlist, tile) for t in fit(line, "ch-eq", BOX_MAX - 2 * PAD)]
    badge = 24 if marks({e.spec.confidence}) else 0
    title = text_width("ch-ttl", tile) + 12 + text_width("ch-lab", e.spec.title) + badge
    w = 2 * PAD + max([title] + [text_width("ch-eq", t) for t in lines])
    return lines, max(BOX_MIN, w), bit


def gates(design, netlist, elements):
    """One box a CLB on the time-interval path, with its netlist equations."""
    by_key = {e.spec.key: e for e in elements}
    sheet = Sheet()
    arrows = []
    y = PAD
    for heading, keys in GATE_ROWS:
        tiles = [(m.partition(".")[0], by_key[k]) for k in keys if k in by_key for m in by_key[k].spec.members]
        if not tiles:
            continue
        sheet.text(PAD, y + 12, heading.upper(), "ch-clb")
        y += 28
        x, row_h, previous, bit = PAD, 0, None, 1
        for tile, e in tiles:
            lines, w, bit = gate_box(netlist, tile, e, bit)
            compact = e.spec.key in COMPACT
            h = TITLE + PAD + LINE * len(lines) + PAD
            if x + w > GATE_W - PAD:
                x, y, row_h, previous = PAD, y + row_h + GATE_ROW_GAP, 0, None
            box = sheet.box(x, y, w, h, "ch-blk ti")
            sheet.text(x + PAD, y + TITLE, tile, "ch-ttl", box)
            for letter, klass in marks({e.spec.confidence}):
                badge_on(sheet, x + w - PAD - 7.5, y + TITLE - 5, letter, klass, box)
            if not compact:
                sheet.text(x + PAD + text_width("ch-ttl", tile) + 12, y + TITLE, e.spec.title, "ch-lab", box)
            ty = y + TITLE + PAD
            for t in lines:
                ty += LINE
                sheet.text(x + PAD, ty - 4, t, "ch-eq", box)
            if previous is not None:
                ptile, px, py, pw, ph = previous
                inputs = {netlist.name(v) for v in netlist.clbs[tile]["pins"].values()}
                nets = [n for n in (netlist.name(netlist.clbs[ptile]["pins"][o]) for o in netlist.used_outputs(ptile))
                        if n in inputs]
                if nets:
                    ay = y + TITLE + PAD + (min(h, ph) - TITLE - PAD) / 2
                    arrows.append((sheet.wire([(px + pw, ay), (x - 2, ay)], "ch-w sig", "ch-as"), nets))
            previous = (tile, x, y, w, h)
            x += w + GATE_GAP
            row_h = max(row_h, h)
        y += row_h + GATE_ROW_GAP
    for wire, nets in arrows:
        label(sheet, wire, nets[0], True)
    problems = sheet.problems()
    if problems:
        die(f"{design.image}: the gate-level drawing collides: " + "; ".join(problems[:6]))
    return sheet.svg(GATE_W, y, f"The {design.name} time-interval counter, one box a logic block, with its "
                     f"equations from the netlist", chip_markers())
