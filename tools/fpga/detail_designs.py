"""The six designs' elements and pin roles for detail.py, as the traces
give them.

Each element is the blocks a trace names together for one job, with the
trace's words for it, its confidence word and the sections it comes
from.  detail.py binds each to the decoded netlist, which gives the
pins, clocks, resets and connections; nothing here says which pin an
element drives or which element it reads, so nothing here can draw a
wire the netlist lacks.  A design traced as another's differences is
built from that one's table, the elements the trace calls the other's
kept, citing both traces, and the rest replaced.

Figures are named, never typed: "{divide}" in a line is FIGURES'
"divide", read from the design's checked reference when the line is
rendered.
"""

import sys
from dataclasses import dataclass, replace
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from page import DESIGNS, GCLK_NS, divide, edge_periods, find, find_all, ns, num, probe, stats  # noqa: E402

# The lanes every design is drawn in, top to bottom: key and heading.
LANES = (
    ("clocks", "Clocks"),
    ("port", "Register port: writes"),
    ("read", "Register port: reads"),
    ("count", "Start, count and stop"),
    ("divider", "1 PPS divider and its pins"),
    ("sync", "Sync field"),
    ("telecom", "P7 divider and phase detector"),
    ("other", "Other outputs"),
)

# The documents elements cite, by a short key.
DOCS = {
    "z3805a": "docs/firmware/z3805a-trace.md",
    "58503a": "docs/firmware/58503a-z3801a-trace.md",
    "58503b": "docs/firmware/58503b-trace.md",
    "z3815a": "docs/firmware/z3815a-trace.md",
    "z3816a": "docs/firmware/z3816a-trace.md",
    "z3817a": "docs/firmware/z3817a-trace.md",
    "comparison": "docs/firmware/fpga-comparison.md",
}


@dataclass(frozen=True)
class Pin:
    """A pin's role, as its trace gives it."""

    # The box it is drawn in, with the other pins of its group that meet
    # the same element.
    group: str
    # What it carries.
    role: str


@dataclass(frozen=True)
class ElementSpec:
    """One element of a design: what its trace says, not what it connects to."""

    # Its id, the same for the same role in every design.
    key: str
    # The lane it is drawn in, a key of LANES.
    lane: str
    # Its role, the box's title.
    title: str
    # Its blocks: a logic block's tile ("AH"), one of its outputs
    # ("EI.Y"), a clock buffer ("GCLK"), or an output pin whose I/O block
    # passes another pin through ("P16").
    members: tuple
    # The trace's confidence word: read, simulated, inferred or open.
    confidence: str
    # The sections it comes from, each (document, section heading).
    sources: tuple
    # What the trace says of it, a line each, figures named.
    lines: tuple

    def tiles(self):
        return {member.partition(".")[0] for member in self.members}


@dataclass(frozen=True)
class DesignSpec:
    """A design's elements and every pin's role."""

    elements: tuple
    # Every pin in use: its number, its Pin.
    pins: dict


def element(key, lane, title, members, confidence, sources, *lines):
    """An ElementSpec; members space-separated, sources (doc key, section) pairs."""
    return ElementSpec(key, lane, title, tuple(members.split()), confidence,
                       tuple((DOCS[doc], section) for doc, section in sources), lines)


def derive(base, doc, keep, changes=(), drop=(), add=()):
    """A design traced as base's differences: base's elements in order, those
    in keep cited to doc too and their lines replaced by keep's (section,
    line and, where doc labels the claim otherwise than base's trace, its
    confidence word), those in changes replaced, those in drop dropped,
    add's appended to their lanes."""
    out = []
    for e in base:
        if e.key in drop:
            continue
        if e.key in changes:
            out.append(changes[e.key])
        elif e.key in keep:
            section, line, *word = keep[e.key]
            sources = tuple(dict.fromkeys(((DOCS[doc], section),) + e.sources))
            out.append(replace(e, sources=sources, lines=(line,), confidence=word[0] if word else e.confidence))
        else:
            sys.exit(f"detail_designs: {doc}: {e.key} neither kept, changed nor dropped")
    for new in add:
        last = max((i for i, e in enumerate(out) if e.lane == new.lane), default=len(out) - 1)
        out.insert(last + 1, new)
    return tuple(out)


# --- figures ---------------------------------------------------------------

def span(low, high):
    return num(low) if low == high else f"{num(low)} to {num(high)}"


def period(run, net):
    p = probe(run, net)
    return span(p["period_min"], p["period_max"])


def clocks(run, net, clock=GCLK_NS):
    """A net's period in clocks: "4", or "4 or 5" where it alternates."""
    p = probe(run, net)
    low, high = p["period_min"] // clock, p["period_max"] // clock
    return num(low) if low == high else f"{num(low)} or {num(high)}"


def exact(slow, fast):
    """How many of one period make another, which must be a whole number."""
    if slow % fast:
        sys.exit(f"detail_designs: {slow} ns is not a whole number of {fast} ns")
    return num(int(slow // fast))


def probed(run, net):
    """A net's shortest probed period, ns."""
    return probe(run, net)["period_min"]


def first_count(run):
    return num(int(find(run, r"less the preset (\d+) ticks")[0]))


def counts(run):
    return ", ".join(num(int(v)) for v in find_all(run, r"less the preset (\d+) ticks"))


def sync_count(run):
    return num(int(find(run, r"^1\.\d+ s: count \w+, less the preset (\d+) ticks")[0]))


def edges(run, pin):
    return " or ".join(num(v) for v in edge_periods(run, pin))


# The terms of the count's net offset beyond the tap, read from the
# netlist (z3805a-trace.md, "Start, stop and status bits"): the stop's
# four flip-flops on GCLK after the tap, one for counting both ends, and
# the two clock edges after the GPS edge the start does not count.
STOP_PIPELINE, BOTH_ENDS, UNCOUNTED_START = 4, 1, 2

# Each figure a line can name: a function of the design, reading its
# checked reference.
FIGURES = {
    "divide": lambda d: num(divide(d)[1]),
    "pps_period": lambda d: num(divide(d)[0]),
    "tap": lambda d: num(probe(f"{d.image}.three", d.tap)["after_min"]),
    "preset": lambda d: find(f"{d.image}.fast", r"the count reads ([0-9a-f]+)$")[0],
    "status": lambda d: find(f"{d.image}.fast", r"^[\d.]+ s: status ([0-9a-f]{2})$")[0],
    "loads": lambda d: {net: num(n) for net, n in stats(d.image)["clock_loads"].items()},
    "pps_high": lambda d: num(ns(find(f"{d.image}.three", rf"{d.pps} falls, ([\d.]+) ns high")[0])),
    "p83_high_probe": lambda d: num(probe(f"{d.image}.three", "P83")["high_min"]),
    "p20_lead": lambda d: num(probe(f"{d.image}.three", "P83")["after_min"]),
    "count": lambda d: first_count(f"{d.image}.three"),
    "counts": lambda d: counts(f"{d.image}.three"),
    "stop_after": lambda d: num(probe(f"{d.image}.three", "CI.X")["after_min"]),
    "gi_y": lambda d: period(f"{d.image}.fast", "GI.Y"),
    "ji_x": lambda d: period(f"{d.image}.fast", "JI.X"),
    "jk_y": lambda d: period(f"{d.image}.three", "JK.Y"),
    "kg_y": lambda d: period(f"{d.image}.three", "KG.Y"),
    "gf_y": lambda d: period(f"{d.image}.three", "GF.Y"),
    "sync1": lambda d: sync_count(f"{d.image}.sync1"),
    "sync2": lambda d: sync_count(f"{d.image}.sync2"),
    "sync3_pps": lambda d: find(f"{d.image}.sync3", rf"^([\d.]+) s: {d.pps} rises$")[0],
    "sync3_after": lambda d: num(probe(f"{d.image}.sync3", "P83/GPS")["after_min"]),
    "p19_period": lambda d: period(f"{d.image}.fast", "P19"),
    "p19_divide": lambda d: num(probe(f"{d.image}.fast", "P19")["period_min"] // GCLK_NS),
    "aclk_halved": lambda d: period(f"{d.image}.freerun", "AB.Y"),
    "prescale": lambda d: num(edges_one(f"{d.image}.freerun", "P40") // GCLK_NS),
    "chain": lambda d: num(divide(d)[1] * GCLK_NS // edges_one(f"{d.image}.freerun", "P40")),
    "p7_divide": lambda d: num(edges_one(f"{d.image}.freerun", "P38") // GCLK_NS),
    "p38": lambda d: edges(f"{d.image}.freerun", "P38"),
    "p40": lambda d: edges(f"{d.image}.freerun", "P40"),
    "pll": lambda d: edges(f"{d.image}.pll", "P38"),
    "ke_y": lambda d: period(f"{d.image}.freerun", "KE.Y"),
    "fe_y": lambda d: period(f"{d.image}.freerun", "FE.Y"),
    "ee_y": lambda d: period(f"{d.image}.freerun", "EE.Y"),
    "low_count": lambda d: first_count(f"{d.image}.low"),
    "nogps_status": lambda d: find(f"{d.image}.nogps", r"^[\d.]+ s: status ([0-9a-f]{2}), seconds")[0],
    "offset": lambda d: num(probe(f"{d.image}.three", d.tap)["after_min"] // GCLK_NS + STOP_PIPELINE
                            + BOTH_ENDS - UNCOUNTED_START),
    "gi_clocks": lambda d: clocks(f"{d.image}.fast", "GI.Y"),
    "ji_clocks": lambda d: clocks(f"{d.image}.fast", "JI.X"),
    "jk_clocks": lambda d: clocks(f"{d.image}.three", "JK.Y"),
    "decade1": lambda d: exact(probed(f"{d.image}.three", "KG.Y"), probed(f"{d.image}.three", "JK.Y")),
    "decade2": lambda d: exact(probed(f"{d.image}.three", "GF.Y"), probed(f"{d.image}.three", "KG.Y")),
    "decade3": lambda d: exact(divide(d)[0], probed(f"{d.image}.three", "GF.Y")),
    "aclk_prescale": lambda d: exact(edges_one(f"{d.image}.freerun", "P40"),
                                     probed(f"{d.image}.freerun", "AB.Y") / 2),
    "ke_clocks": lambda d: clocks(f"{d.image}.freerun", "KE.Y"),
    "p7_counter": lambda d: exact(edges_one(f"{d.image}.freerun", "P38"), probed(f"{d.image}.freerun", "KE.Y")),
    "p35_five": lambda d: exact(probed(f"{d.image}.p35", "KA.Y"), probed(f"{d.image}.p35", "P34")),
    "p35_count": lambda d: exact(probed(f"{d.image}.p35", "P21"), probed(f"{d.image}.p35", "P34")),
    "gc_clocks": lambda d: num(probed(f"{d.image}.p35", "GC.X") // GCLK_NS),
    "p19_clocks": lambda d: clocks(f"{d.image}.fast", "P19"),
    "jb_x": lambda d: period(f"{d.image}.three", "JB.X"),
    "ld_x": lambda d: period(f"{d.image}.three", "LD.X"),
    "hd_y": lambda d: period(f"{d.image}.three", "HD.Y"),
    "hf_y": lambda d: period(f"{d.image}.three", "HF.Y"),
    "jb_clocks": lambda d: clocks(f"{d.image}.three", "JB.X"),
    "ld_clocks": lambda d: clocks(f"{d.image}.three", "LD.X"),
    "hd_clocks": lambda d: clocks(f"{d.image}.three", "HD.Y"),
    "hf_clocks": lambda d: clocks(f"{d.image}.three", "HF.Y"),
    "p72_period": lambda d: period(f"{d.image}.three", "P72"),
    "p72_high": lambda d: num(probe(f"{d.image}.three", "P72")["high_min"]),
    "gate_after": lambda d: num(probe(f"{d.image}.three", "AI.Y")["after_min"]),
    "restart_after": lambda d: num(probe(f"{d.image}.sync3", "IF.Y")["after_min"]),
    "select_count": lambda d: first_count(f"{d.image}.select"),
    "alternate_low":lambda d: num(ns(find(f"{d.image}.freerun_4s", r"^probe ~P14 high ([\d.]+) ns from")[0])),
    "alternate_at": lambda d: " and ".join(
        f"{t} s" for t in find_all(f"{d.image}.freerun_4s", r"^probe ~P14 high [\d.]+ ns from ([\d.]+) s$")),
}


def edges_one(run, pin):
    found = edge_periods(run, pin)
    if len(found) != 1:
        sys.exit(f"detail_designs: {run}: {pin} periods {found}, not one")
    return found[0]


class Facts(dict):
    """A design's figures, each read from its checked reference when a line
    names it."""

    def __init__(self, design):
        super().__init__()
        self.design = design

    def __missing__(self, key):
        """A figure by name; "name@image" is another design's, for a block
        its trace and check-blocks or tilediff call that design's."""
        name, _, image = key.partition("@")
        if name not in FIGURES:
            raise KeyError(key)
        design = next((d for d in DESIGNS if d.image == image), None) if image else self.design
        if design is None:
            raise KeyError(key)
        value = FIGURES[name](design)
        self[key] = value
        return value


# --- pins ------------------------------------------------------------------

BUS = {f"P{pin}": Pin("bus", f"DATA{bit}") for bit, pin in enumerate((71, 70, 67, 65, 62, 60, 59, 58))}
PORT = {
    "P61": Pin("port control", "select, Low"),
    "P75": Pin("port control", "R/W, High to read"),
    "P69": Pin("address", "ADDR0"),
    "P68": Pin("address", "ADDR1"),
    "P66": Pin("address", "ADDR2"),
}
INTERPOLATOR = {f"P{pin}": Pin("interpolator byte", "interpolator input") for pin in (3, 4, 5, 6, 8, 9, 10, 11)}
SHARED = {
    **BUS, **PORT, **INTERPOLATOR,
    "P2": Pin("constant", "tied Low"),
    "P16": Pin("passed through", "the GPS 1 PPS, passed through"),
    "P82": Pin("GPS 1 PPS", "the GPS 1 PPS"),
    "P79": Pin("P79", "a 0, 3-stated"),
    "P81": Pin("start", "the start's pulse, Low"),
    "P76": Pin("P76, P77", "role not traced"),
    "P77": Pin("P76, P77", "role not traced"),
}
TEN_MHZ = {
    **SHARED,
    "P7": Pin("clock", "the oscillator, into GCLK"),
    "P17": Pin("P17", "an input, onto P84 inverted"),
    "P84": Pin("passed through", "P17, inverted"),
    "P72": Pin("start", "the start capture's other output"),
    "P83": Pin("1 PPS", "the internal 1 PPS"),
    "P78": Pin("1 PPS", "the internal 1 PPS"),
}


# --- the Z3805A, the reference ----------------------------------------------

Z = "z3805a"
Z3805A = (
    element("gclk", "clocks", "Global clock, GCLK", "GCLK", "read", ((Z, "Clocks"),),
            "P7 drives the one global clock buffer through general routing, not the dedicated TCLKIN pad (P13).",
            "GCLK clocks {loads[GCLK.O]} logic blocks; ACLK, the alternate buffer, is unused."),
    element("strobe", "port", "Select and write strobe", "HL FK FL", "simulated",
            ((Z, "Register port"), ("58503b", "The 1 PPS divider")),
            "P61 and P75 feed HL, whose X is the bus's 3-state control (read).",
            "A write is taken on GCLK: the select and R/W Low are sampled twice, and the first clock they are "
            "seen makes a one-clock strobe, which the address decodes."),
    element("decode", "port", "Write decode", "DL EL", "read", ((Z, "Register port"), ("58503a", "Register port")),
            "P66, P68 and P69 feed EL and DL: EL.G register 0, DL register 1, EL.F register 3, each with ADDR2 Low.",
            "Register 2 and 4 to 7 decode to nothing."),
    element("register0", "port", "Register 0: mode and preset", "FH", "read", ((Z, "Register port"), (Z, "The count")),
            "Bits 0 and 1 together, as one flip-flop: whether the mode is 3, so modes 1 and 2 are mode 0.",
            "Bit 6, the preset, FH.Y, which resets every stage of the count."),
    element("register1", "port", "Register 1: start, reference, read address", "CG FG FI GL", "simulated",
            ((Z, "Register port"), ("58503b", "Register port"), ("58503b", "The count")),
            "Bit 0, the start, and bit 1, the reference (CG); bits 3 to 7, the read address (FI, FG, GL).",
            "Bit 2 is not kept."),
    element("phases", "read", "Bus phases", "AL", "read",
            ((Z, "Register port"), (Z, "The count"), ("58503b", "Register port")),
            "A two-bit Johnson stage on GCLK (QX <- QY, QY <- ~QX, read from the netlist), a cycle of four GCLKs: "
            "AL.X and AL.Y clock the bus's output flip-flops and select each count stage's read half."),
    element("address", "read", "Read address decode", "EB EC", "read", ((Z, "The count"), ("58503b", "Register port")),
            "The read address's bits 0 and 1 decoded, EC.Y 0, EC.X 1, EB.Y 2, EB.X 3: each count stage's read "
            "term enables at one."),
    element("interpolator", "read", "Interpolator read terms", "AB AC AD AE", "simulated",
            ((Z, "Register port"), ("58503b", "Register port")),
            "The interpolator's pins, complemented, at A1 A0 of 0 and 1, and the count's bits 0 to 3 "
            "at 2: the interpolator reads back at 0x10 and 0x11, low nibble first."),
    element("select", "read", "Nibble select", "CE DE DF EF", "simulated",
            ((Z, "Register port"), ("58503b", "Register port")),
            "With the address's bit 2 clear CE selects, with it set DE and EF, ORs of count read terms; the "
            "address's bit 3 zeroes DF and EG."),
    element("mux01", "read", "Read multiplexer, bits 0 and 1", "CK", "simulated", ((Z, "Register port"),),
            "The status's stop and start bits, unless the read address's bit 4 is set, when they are the "
            "addressed nibble's."),
    element("mux23", "read", "Read multiplexer, bits 2 and 3", "EG", "simulated",
            ((Z, "Register port"), (Z, "Start, stop and status bits")),
            "EG never sees the address's bit 4, so 0x00 to 0x07 read bits 2 and 3 of 0x10 to 0x17: status bits 2 "
            "and 3 are two interpolator inputs, P5 and P6, complemented.",
            "Under the preset the status reads 0x{status} (simulated, the fast run)."),
    element("seconds", "read", "Seconds counter, bits 4 to 7", "IK IL", "simulated",
            ((Z, "Register port"), (Z, "The 1 PPS divider"), ("comparison", "Roles and tiles")),
            "A count of the internal 1 PPS, enabled by the terminal, read in bits 4 to 7 at every address."),
    element("start", "count", "Start capture", "AH", "read", ((Z, "Start, stop and status bits"), (Z, "Clocks")),
            "The GPS 1 PPS's rising edge on P82 clocks AH, whose Y, the start's pulse, rises at once: the interval "
            "the interpolator measures, Low on P81."),
    element("gate", "count", "Start gate", "BI AI", "read", ((Z, "Start, stop and status bits"),),
            "With register 1's reference bit High BI takes AH.Y on the first GCLK edge after the GPS edge, AI.QY "
            "sets on the second and the count first toggles on the third; with it Low, one edge sooner.",
            "With the reference bit Low a reading is one tick more, {low_count} for {count} (simulated, the low "
            "run), the start's pulse a clock shorter; how the interpolator reads a pulse under its 100 ns low "
            "reference is open (it probably clips, inferred).",
            "With no GPS 1 PPS neither status bit sets: after 512 polls the status reads 0x{nogps_status}, and the "
            "firmware's wait ends in \u201cGate failure\u201d (simulated, the nogps run)."),
    element("mode3", "count", "CH and BH", "CH BH", "open", ((Z, "Clocks"), (Z, "Start, stop and status bits")),
            "CH feeds AI's set term beside register 1's start bit (G = CG_X + ~CH_X·QY + ~CH_X·BI_Y), and CH.Y "
            "clocks BH; BH.X is ORed into the stop bit (CJ.X = BH.X + CI.X) and BH.Y into AH's outputs (read from "
            "the netlist); the trace gives them no role of their own."),
    element("p79", "count", "P79's constant", "BJ", "read", ((Z, "Other outputs"),),
            "BJ.X is a constant 0 (F = 0), driven on P79 while AI.X is Low and 3-state otherwise."),
    element("p76", "count", "BL, onto P76 and P77", "BL", "open", ((Z, "Other outputs"),),
            "P76 and P77 are BL.Y and BL.X, inverted; what the board takes from them is not traced."),
    element("tick", "count", "Count bit 0: the gated tick", "CF", "read", ((Z, "The count"),),
            "A toggle on GCLK gated by the preset (FH.Y), the stop (DG.Y) and the start (AI.Y); read bit 0 is the "
            "complement of CF.QY."),
    element("head", "count", "Count bit 1", "BB", "read", ((Z, "The count"),),
            "One flip-flop, G = ~QY, clocked by CF.Y."),
    element("stages", "count", "Count bits 2 to 23", "BC BD CC ED EE DD DB CB DC CD BE", "read", ((Z, "The count"),),
            "Eleven two-bit ripple Johnson stages, two bits each in that order, each on the falling edge of the "
            "stage before; the preset resets all.",
            "Each digit reads its state's (n + 1) mod 4: under the preset the count reads 0x{preset} (simulated, "
            "the fast run), and 0x{preset} plus the ticks counted."),
    element("tapbuffer", "count", "Tap buffer", "GH", "read", ((Z, "Clocks"),),
            "GH.X, a buffer of the divider's tap HG.Y, clocks the stop capture."),
    element("stop", "count", "Stop capture", "CI", "read", ((Z, "Start, stop and status bits"),),
            "Clocked by the tap, takes AH.X, which the start sets, so only a tap after a start stops the count; "
            "CI.X rises {stop_after} ns after P83 (simulated, the three run)."),
    element("stoppipe", "count", "Stop pipeline", "CJ DG", "read", ((Z, "Start, stop and status bits"),),
            "CI.X passes four flip-flops on GCLK, CJ.QX, CJ.QY, DG.QX and DG.QY, and DG.Y holds the tick: the last "
            "edge counted is the fourth after the tap's.",
            "So count + fraction = {offset} + (t_internal - t_GPS) / 100 ns, the firmware's calibration: the tap, "
            "4 more to the last counted edge, 1 for both ends, less the 2 the start does not count (read).",
            "With the GPS 1 PPS at 0.25 s each reading is {count} ticks over the preset (simulated, the three run)."),
    element("prescaler", "divider", "Prescaler", "GI HJ FJ HI IG JH IH JI II", "simulated",
            ((Z, "The 1 PPS divider"), ("58503b", "The 1 PPS divider")),
            "GI, which FJ.X, the terminal JI.X a clock later through HJ, clears once a cycle, and a counter on IG, "
            "JH, IH and JI: GI.Y every {gi_y} ns and JI.X every {ji_x} ns (simulated, the fast run)."),
    element("binary", "divider", "Binary stages, KH to KK", "KH KI KJ KK JJ IJ JK", "simulated",
            ((Z, "The 1 PPS divider"), ("58503b", "The 1 PPS divider")),
            "KH to KK, enabled by the prescaler: JK.Y every {jk_y} ns (simulated, the three run)."),
    element("decades", "divider", "Decades and the tap", "KF KG GE GF HF GG HG JG", "simulated",
            ((Z, "The 1 PPS divider"), ("58503b", "The 1 PPS divider")),
            "Three decades, KF and KG, GE and GF, then HF, GG and HG: KG.Y every {kg_y} ns, GF.Y every {gf_y} ns "
            "(simulated, the three run).",
            "HG.Y, the top decade's states 4 to 7, is the tap: it rises {tap} ns after P83 (simulated)."),
    element("terminal", "divider", "Terminal count", "HH", "simulated", ((Z, "The 1 PPS divider"),),
            "HH.X, the terminal, and P83 every {pps_period} ns: a divide by {divide} (simulated, the three run)."),
    element("driver", "divider", "1 PPS output driver", "EI", "read", ((Z, "The 1 PPS divider"), (Z, "Clocks")),
            "EI.Y = HH.X + P83·~KH.Y: the terminal sets the pins, P83's registered output read back holds them, "
            "and KH.Y's rise releases them.",
            "P83 stays High {pps_high} ns (simulated, the three run)."),
    element("field", "sync", "Sync field, register 3", "DI DJ", "read", ((Z, "The sync field"), (Z, "Register port")),
            "DI keeps register 3's bits 0 and 1; DJ, a clock behind DI, marks the field's change from 0."),
    element("pending", "sync", "Sync pending", "EK", "read", ((Z, "The sync field"),),
            "Sets on the field's change from 0 and holds until the terminal HH.X or kind 3's restart EJ.Y: a step "
            "acts once, at the next internal 1 PPS."),
    element("step", "sync", "Steps, kinds 1 and 2", "GJ GK", "simulated", ((Z, "The sync field"),),
            "In the second's last millisecond GJ makes GK hold the prescaler a clock longer (kind 2) or a clock "
            "shorter (kind 1).",
            "The reading after is {sync1} (kind 1) and {sync2} (kind 2) ticks over the preset, against {count} "
            "(simulated, the sync1 and sync2 runs)."),
    element("restart", "sync", "GPS edge and restart, kind 3", "DH EJ", "simulated",
            ((Z, "The sync field"), ("58503a", "The sync field")),
            "DH, a rising-edge detector of the GPS 1 PPS on GCLK, feeds EJ, whose EJ.X restarts the divider on "
            "the next GPS edge with kind 3 pending.",
            "The next internal 1 PPS then comes at {sync3_pps} s, a second and a clock after the first GCLK edge "
            "after the GPS edge (simulated, the sync3 run)."),
    element("tie", "other", "Constant 1", "BA", "read", ((Z, "Other outputs"),),
            "BA's F is the constant 1, through an inverting I/O block: P2 is tied Low."),
    element("passthrough", "other", "Pins passed through", "P16 P84", "read", ((Z, "Other outputs"),),
            "P82, the GPS 1 PPS, is also P16's output, and P17 is P84's, inverted; what the board takes from "
            "P84 is not traced."),
)

Z3805A_PINS = {
    **TEN_MHZ,
    "P14": Pin("1 PPS", "the internal 1 PPS"),
    "P45": Pin("1 PPS", "the internal 1 PPS"),
}


# --- the 58503A/Z3801A, the Z3805A's differences ----------------------------

A = "58503a"


def same(section, what="The Z3805A's", *word):
    """A kept element's section in the derived trace, its one line and,
    where the trace labels it otherwise, its confidence word."""
    return (section, f"{what}.", *word)


A58503 = derive(Z3805A, A, keep={
    "gclk": ("Clocks", "P7 drives GCLK as in the Z3805A; GCLK clocks {loads[GCLK.O]} logic blocks, the Z3805A's and HK."),
    "strobe": same("Register port", "The Z3805A's", "read"),
    "decode": ("Register port", "The Z3805A's: registers 0, 1 and 3, all with ADDR2 Low (EL.G, DL, EL.F); a write to "
               "register 2 or 4 to 7 reaches no logic."),
    "register0": same("Register port"),
    "register1": same("Register port", "The Z3805A's", "read"),
    "address": same("Register port"),
    "interpolator": ("Register port", "The Z3805A's, AB, AC, AD and AE reading AL.Y where the Z3805A's read AL.X "
                     "(tilediff); AB's, AC's and AE's functions are the Z3805A's with the inputs permuted.", "read"),
    "select": same("Register port", "The Z3805A's", "read"),
    "mux01": same("Register port", "The Z3805A's", "read"),
    "mux23": ("Register port", "The Z3805A's; under the preset the status reads 0x{status} (simulated, the fast run).",
              "read"),
    "seconds": ("The 1 PPS divider", "The Z3805A's, IK and IL, enabled by HH.X, the terminal.", "read"),
    "start": same("Start, stop and status bits"),
    "gate": same("Start, stop and status bits"),
    "mode3": same("Clocks", "As the Z3805A's; CH.Y clocks BH"),
    "p79": same("Other outputs", "BJ.X, a 0, 3-state on AI.X, as the Z3805A's"),
    "p76": same("Other outputs", "~BL.Y, ~BL.X, as the Z3805A's; their role is not traced"),
    "tick": same("The count"),
    "head": ("The count", "The Z3805A's, BB reading AL.Y on its B input where the Z3805A's reads AL.X on C."),
    "stages": ("The count", "The Z3805A's, all eleven stages (BC, BD, BE, CB, CC, CD, DB, DC, DD, ED, EE) reading "
               "AL.Y where the Z3805A's read AL.X (tilediff), CD and ED on E where the Z3805A's read it on D; under "
               "the preset the count reads 0x{preset} (simulated, the fast run)."),
    "tapbuffer": same("Clocks", "As the Z3805A's; the tap, GH.X, clocks CI"),
    "stop": same("Start, stop and status bits"),
    "stoppipe": ("The 1 PPS divider", "The Z3805A's; each reading is {count} ticks over the preset, the Z3805A's "
                 "(simulated, the three run)."),
    "binary": same("The 1 PPS divider", "The divider is the Z3805A's", "read"),
    "decades": ("The 1 PPS divider", "The Z3805A's; the tap, HG.Y, rises {tap} ns after P83 (simulated, the three "
                "run)."),
    "terminal": ("The 1 PPS divider", "HH, unchanged: HH.X every {pps_period} ns, a divide by {divide} (simulated, "
                 "the three run)."),
    "field": same("The sync field"),
    "pending": same("The sync field"),
    "step": ("The sync field", "The Z3805A's: the reading after is {sync1} (kind 1) and {sync2} (kind 2) ticks over "
             "the preset (simulated, the sync1 and sync2 runs)."),
    "tie": same("Other outputs", "~BA.X, BA F = 1: tied Low, as the Z3805A's"),
}, changes={
    "phases": element("phases", "read", "Bus phases", "AL", "read", ((A, "The count"), (A, "Register port")),
                      "The Z3805A's two-bit Johnson stage with its X and Y exchanged: this AL.Y is the Z3805A's AL.X, "
                      "and every load of either moved to the other."),
    "prescaler": element("prescaler", "divider", "Prescaler", "GI HK FJ HI IG JH IH JI II", "read",
                         ((A, "The 1 PPS divider"),),
                         "The Z3805A's, but HK, the added block, holds the Z3805A's HJ flip-flop: it registers JI.X "
                         "(HK.G = JI_X into its X flip-flop), and FJ reads HK.X."),
    "driver": element("driver", "divider", "1 PPS output driver", "EI HJ", "read", ((A, "The 1 PPS divider"),),
                      "EI.Y holds the pulse through P83's own input while either of two nets is Low: EI.F = HH_X + "
                      "~HJ_X·PAD14_I + PAD14_I·~KH_X; HJ is new, a registered four-input AND of divider nets whose one "
                      "load is EI.",
                      "P83 and P78 only; they stay High {pps_high} ns (simulated, the three run)."),
    "restart": element("restart", "sync", "GPS edge and restart, kind 3", "DH EJ", "simulated", ((A, "The sync field"),),
                       "DH is the Z3805A's rising-edge detector with its two flip-flops exchanged; EJ reads the same "
                       "nets as the Z3805A's.",
                       "Kind 3 restarts the divider on the next GPS 1 PPS: the next internal 1 PPS comes at "
                       "{sync3_pps} s (simulated, the sync3 run)."),
    "passthrough": element("passthrough", "other", "Pins passed through", "P14 P16 P84", "read",
                           ((A, "Other outputs"), (A, "The 1 PPS divider")),
                           "P14 passes P17 through, unregistered, where the Z3805A's carries the 1 PPS; P16 is P82, "
                           "P84 P17 inverted. P45 is not driven."),
})

A58503_PINS = {**TEN_MHZ, "P14": Pin("passed through", "P17, passed through"),
               "P17": Pin("P17", "an input, onto P84 inverted and onto P14")}


# --- the 58503B, the Z3805A's differences ------------------------------------

B = "58503b"

B58503 = derive(Z3805A, B, keep={
    "gclk": ("Clocks", "GCLK from P7 through general routing, as in the Z3805A; it clocks {loads[GCLK.O]} logic "
             "blocks, and no ACLK or divider comes ahead of it."),
    "strobe": ("Register port", "HL.X, F = ~PAD24_I + PAD39_I, is the bus's 3-state control: it drives while P75 is "
               "High and P61 Low.  FK and FL, the write strobe, are the Z3805A's (checked by check-blocks).", "read"),
    "register1": ("Register port", "CG latches DATA0 and DATA1 on DL.X, register 1's bits 0 and 1; FI.X, FI.Y, FG.X, "
                  "FG.Y and GL.Y hold bits 3 to 7, the read address, A0 to A4.", "read"),
    "address": ("Register port", "EB and EC decode A0 and A1: EC.Y is 0, EC.X 1, EB.Y 2 and EB.X 3.", "read"),
    "interpolator": ("Register port", "AB and AC, with AD and AE before them, take the interpolator's pads, "
                     "complemented, at A1 A0 0 and 1, and BC.X and BB.X, the count's bits 0 to 3, at 2.", "read"),
    "select": ("Register port", "A3 zeroes DF and EG; with A2 clear CE selects, with it set DE and EF, ORs of count "
               "readouts that each enable at one A1 A0.", "read"),
    "mux01": ("Register port", "CK: with A4 clear the stop bit CJ.X and the start bit AH.Y, with A4 set DF.X.", "read"),
    "mux23": ("Register port", "EG.X, which does not take A4, so 0x00 to 0x07 read bits 2 and 3 of 0x10 to 0x17; "
              "under the preset the status reads 0x{status} (simulated, the fast run).", "read"),
    "seconds": ("Register port", "IK and IL, a four-bit binary counter on GCLK enabled by HH.Y, the terminal, with no "
                "reset but the global one: bits 4 to 7 at every address.", "read"),
    "mode3": ("At a glance", "CH.Y clocks BH; the trace gives them no role of their own."),
    "p79": ("Other outputs", "BJ.X, three-state, as the Z3805A's."),
    "p76": ("Other outputs", "BL.Y and BL.X, inverted, as the Z3805A's; their role is open."),
    "head": ("The count", "BB.QY, a single flip-flop toggling on CF.X's rise (K = CF_X, G = ~QY)."),
    "stages": ("The count", "BC, BD, CC, ED, EE, DD, DB, CB, DC, CD and BE, each QX <- QY, QY <- ~QX, on the falling "
               "edge of the previous stage's Y; all reset on EI.X, the preset.  Under the preset the count reads "
               "0x{preset} (simulated, the fast run)."),
    "binary": ("The 1 PPS divider", "KH to KK with JJ and IJ, JK: the Z3805A's block by block (check-blocks).",
               "read"),
    "decades": ("The 1 PPS divider", "KF and KG, JG, GE and GF, HF, GG and HG: the Z3805A's block by block; HG.Y, the "
                "top decade's states 4 to 7, is the tap, through EH (the stop's tap buffer).", "read"),
    "terminal": ("The 1 PPS divider", "HH, its outputs exchanged: HH.Y the terminal, HH.X the same product on JG.Y, "
                 "to GJ.  P83 and P78 rise every {pps_period} ns (simulated, the three run), so the terminal comes "
                 "every {divide} clocks (inferred from them).", "read"),
    "field": ("The sync field", "EL.F, the register-3 write decode, loads DI with DATA0 and DATA1; DJ follows a clock "
              "behind: the Z3805A's."),
    "pending": ("The sync field", "EK sets on the field's change from 0 and clears on the terminal HH.Y or the "
                "restart EJ.Y: the Z3805A's, with HH's outputs exchanged."),
    "step": ("The sync field", "GJ.Y and GJ.X, kind 1's and kind 2's step lines, into GK: the reading after is "
             "{sync1} and {sync2} ticks over the preset (simulated, the sync1 and sync2 runs)."),
    "passthrough": ("Other outputs", "P16 passes P82, the GPS 1 PPS, through; P84 is P17, inverted."),
}, changes={
    "decode": element("decode", "port", "Write decode", "DL EL DK", "read", ((B, "Register port"), (B, "The count")),
                      "Addresses 0, 1, 2, 3 and 6: EL.Y register 0, DL.X register 1, DK.Y address 2, EL.F register 3, "
                      "and DL.Y address 6, ADDR2 High; the Z3805A decodes neither address 2 nor address 6."),
    "register0": element("register0", "port", "Register 0: preset and mode", "EI", "read", ((B, "The count"),),
                         "EI.X loads bit 6, the preset, on EL.Y; EI.Y loads from the mode bits and is High unless "
                         "the mode is 3, AH's data."),
    "phases": element("phases", "read", "Bus phases", "AL", "read", ((B, "Register port"),),
                      "A two-bit Johnson stage on GCLK, AL.X AL.Y's complement, a cycle of four GCLKs: the bus "
                      "latches on AL.Y's rise take a digit's low bit, those on AL.X's its high bit."),
    "start": element("start", "count", "Start capture", "AH", "read", ((B, "Start, stop and status bits"),
                                                                      (B, "Other outputs")),
                     "The GPS 1 PPS on P82 clocks AH directly (DI = EI_Y, RD = CF_Y); AH.Y is the start bit.  AH.X "
                     "and AH.Y are both combinational, so P72 and P81 swap the Z3805A's outputs."),
    "gate": element("gate", "count", "Armed gate", "AI", "read", ((B, "Start, stop and status bits"),),
                    "AI (K = GCLK, RD = EI_X) is the armed gate."),
    "tick": element("tick", "count", "Count bit 0: the gated tick", "CF", "read", ((B, "The count"),),
                    "CF.X, a GCLK toggle: it toggles while the armed gate AI.Y is High and the stop DG.Y Low, and the "
                    "preset EI.X clears it on the next clock."),
    "tapbuffer": element("tapbuffer", "count", "Tap buffer", "EH", "read", ((B, "Start, stop and status bits"),),
                         "The tap is HG.Y, as in the Z3805A, through EH, a buffer (F = HG_Y): EH.Y rises {tap} ns "
                         "after this design's own P83, a register later than the Z3805A's (simulated, the three "
                         "run)."),
    "stop": element("stop", "count", "Stop capture", "CI", "read", ((B, "Start, stop and status bits"),),
                    "CI (F = AH_Y, K = EH_Y, RD = EI_X) latches the start bit when the tap rises."),
    "stoppipe": element("stoppipe", "count", "Stop bit and pipeline", "CJ DG", "read",
                        ((B, "Start, stop and status bits"),),
                        "CJ.X (F = BH_X + CI_X), a combinational OR, is the stop bit; CJ.QX, CJ.QY, DG.QX and DG.QY "
                        "carry it on GCLK and DG.Y holds CF: the last tick is on the fourth edge after the tap.",
                        "Each reading is {count} ticks over the preset, the Z3805A's (simulated, the three run)."),
    "prescaler": element("prescaler", "divider", "Prescaler", "GI LJ FJ HI IG JH IH JI II", "read",
                         ((B, "The 1 PPS divider"),),
                         "The Z3805A's block by block, LJ (F = JI_X, registered on GCLK) for the Z3805A's HJ, the "
                         "prescaler's terminal a clock later into FJ (checked by check-blocks)."),
    "driver": element("driver", "divider", "1 PPS set-and-hold", "HK HJ", "read", ((B, "The 1 PPS divider"),),
                      "HK (F = HH_Y + ~HJ_X·FH_X + ~KH_X·FH_X): HH.Y sets HK.X, FH.X, HK.X registered, holds it, and "
                      "HJ.X·KH.X clears it, which sets the width; HK is the 58503A's EI and HJ the 58503A's HJ."),
    "restart": element("restart", "sync", "GPS edge and restart, kind 3", "DH EJ", "simulated", ((B, "The sync field"),),
                       "DH, the GPS 1 PPS's rising-edge detector, the 58503A's; EJ.X (DH_X·EK_X·DI_X·DI_Y), kind 3's "
                       "restart on the GPS edge, resets the divider's stages.",
                       "The next internal 1 PPS at {sync3_pps} s (simulated, the sync3 run)."),
    "tie": element("tie", "other", "Constant 1", "CA", "read", ((B, "Other outputs"),),
                   "CA.X, inverted onto P2, where the Z3805A uses BA.X: tied Low."),
}, add=(
    element("register2", "port", "Register 2: P14's phase", "GH HE.Y", "read", ((B, "Register port"),
                                                                                 (B, "The 1 PPS divider")),
            "GH.QX holds DATA0 on DK.Y, the address-2 decode; GH.Y (G = GH_X·~HE_Y), a one-clock pulse on its rise, "
            "resets FD, the alternate-second phase."),
    element("address6", "port", "Address 6: polarities", "FF", "read", ((B, "Register port"),),
            "FF.Y latches DATA0 and selects P83's polarity, FF.X DATA1 and P14's; the firmware never writes address "
            "6, so under it FF stays at its reset 0."),
    element("outputs", "divider", "P83 and P78", "FH", "read", ((B, "The 1 PPS divider"),),
            "FH.QX registers HK.X on GCLK and drives P78; P83 is FH.QX XOR FF.Y, the polarity select.  Either pin "
            "is two registers after HK.X, where the Z3805A's P83 is one.",
            "P83 and P78 rise every {pps_period} ns and P83 stays High {pps_high} ns (simulated, the three run)."),
    element("alternate", "divider", "P14: alternate seconds", "HD FD HE.X", "read", ((B, "The 1 PPS divider"),),
            "HD.QX registers HK.X; FD toggles on each rise of HD.QX, a divide by 2 of the second; HE.X is HD.QX a "
            "clock later.  P14 rises a clock after HD.QX only when FD.Y is Low and holds itself High through its "
            "own pad; with FF.X High it idles High and pulses Low."),
    element("p19", "other", "P19: a GCLK divider", "GA GB GC GD FB", "simulated", ((B, "Other outputs"),),
            "GA, GB, GC and GD, a small GCLK divider, GC off FB.X; P19 is GB.Y, a mid-divider tap, unregistered.",
            "P19 rises every {p19_period} ns, GCLK / {p19_divide} (simulated, the fast run); what the board takes "
            "from it is open."),
))

B58503_PINS = {
    **TEN_MHZ,
    "P72": Pin("start", "AH.Y, the start bit; board use open"),
    "P81": Pin("start", "~AH.X, combinational; board use open"),
    "P14": Pin("1 PPS", "alternate seconds, read back"),
    "P19": Pin("P19", "GB.Y, a GCLK divider's tap"),
    "P83": Pin("1 PPS", "the internal 1 PPS, its polarity FF.Y's"),
}


# --- the Z3815A ----------------------------------------------------------------

F = "z3815a"
S = "z3816a"
Z3815A = (
    element("aclk", "clocks", "Alternate clock, ACLK", "ACLK", "read", ((F, "Clocks"),),
            "P53 is taken straight into the alternate clock buffer; ACLK clocks {loads[ACLK.O]} logic blocks: AB, "
            "the 1 PPS divider's prescaler and FK, FB (P20) and LK, and P20's output flip-flop."),
    element("gclk", "clocks", "Global clock, GCLK: ACLK halved", "AB GCLK", "read", ((F, "Clocks"),),
            "AB, a lone toggle on ACLK (F = ~QY), drives GCLK: P53 halved, every {aclk_halved} ns at the "
            "testbench's 50 ns ACLK (simulated, the freerun run).  GCLK clocks {loads[GCLK.O]} logic blocks.",
            "P7 ({loads[PAD7.I]} blocks), P84 and P35 clock logic through general routing, in the lanes below."),
    element("strobe", "port", "Select and write strobe", "CL DL", "read", ((F, "Register port"), (S, "Register port")),
            "CL, over P61 and P75, gates the bus (CL.X 3-states all eight) and, through DL, makes the one-GCLK write "
            "strobe DL.Y."),
    element("decode", "port", "Write decode", "DJ EJ", "read", ((F, "Register port"),),
            "DJ decodes registers 3 (DJ.X) and 0 (DJ.Y), EJ registers 2 (EJ.X) and 1 (EJ.Y); every term wants ADDR2 "
            "Low, so the firmware's writes of registers 4 to 7 land nowhere."),
    element("register0", "port", "Register 0: preset and mode", "EI", "read", ((F, "Register port"), (F, "The count")),
            "Bit 6 into EI.Y, the preset; bits 0 and 1 into EI.X, their NAND, Low in mode 3."),
    element("register1", "port", "Register 1: references and read address", "DH FG EH DK", "read",
            ((F, "Register port"),),
            "The read address in bits 3 to 7 (FG.Y bit 3, FG.X bit 4, EH bits 5 and 6, DK.Y bit 7), and bits 0 and 1 "
            "in DH, the references."),
    element("register2", "port", "Register 2: phase and start source", "EF BF EG", "read", ((F, "Register port"),),
            "Bit 0 (EF) resets FD on its rise, through BF and EF.X; bits 2 and 3 (EG.X, EG.Y) select the start "
            "source in BG.  EG.X also feeds HB, out on P24 and P25."),
    element("phases", "read", "Bus phases", "AL", "read", ((F, "Register port"),),
            "A two-bit Johnson counter on GCLK whose phases clock the bus's bits 0 to 3 and select the count's read "
            "terms."),
    element("interpolator", "read", "Interpolator read terms", "AA AC AD AE", "read",
            ((F, "The count"), (S, "The count")),
            "Combinational terms over the interpolator's pads, merged with the head's terms; the byte reads back "
            "complemented at 0x10 and 0x11."),
    element("select", "read", "Readout decode", "CF DF BE ED DD DB EB", "read", ((F, "The count"), (S, "The count")),
            "The readout decode in rows B to E: CF and DF carry the addressed nibble's data, two bits each, one per "
            "AL phase, DB and EB the address selects from FG (z3816a-trace.md).  No input of CK, FJ, CF, ED or DD "
            "is a P7 divider net."),
    element("mux01", "read", "Read multiplexer, bits 0 and 1", "CK", "read", ((F, "The count"),),
            "DK.Y, register 1's bit 7, selects: clear, the stop bit CJ.X and the start bit BH.Y; set, CF.Y."),
    element("mux23", "read", "Read multiplexer, bits 2 and 3", "FJ", "read", ((F, "Start, stop and status bits"),),
            "At a status address FJ.X is 1, so bit 3 reads 1, and FJ.Y gives GH.Y, the internal 1 PPS flag: the "
            "status reads 0x{status} whatever the interpolator's byte (simulated, the fast run)."),
    element("seconds", "read", "Seconds counter", "HH II", "read", ((F, "Register port"),),
            "HH and II, enabled by JI.Y, the internal 1 PPS."),
    element("bits47", "read", "Bits 4 to 7: seconds or P84", "GI HI IJ HJ", "read", ((F, "The P84 counter"),),
            "GI, HI, IJ and HJ, each a multiplexer on FG.Y, the read address's bit 0: the seconds at even addresses, "
            "the P84 counter at odd ones, onto the bus unregistered."),
    element("source", "count", "Start-source select", "BG", "read", ((F, "Start, stop and status bits"),),
            "On register 2's bits 2 and 3: P82, the GPS 1 PPS, at 0x00, P84 at 0x04, P78 at 0x08 or 0x0c; BG.X "
            "clocks the start capture and CH."),
    element("start", "count", "Start capture", "BH", "read", ((F, "Start, stop and status bits"),),
            "BG.X clocks BH (DI = EI_X, RD = CG_X); BH.X, the interval the interpolator measures, is Low on P81, and "
            "BH.Y is the start bit."),
    element("gate", "count", "Armed gate", "AI", "read", ((F, "Start, stop and status bits"),),
            "AI (K = GCLK, RD = EI_Y) is the armed gate that lets the tick CG run."),
    element("pipeline", "count", "Start pipeline", "AF AG BI BJ BK", "open",
            ((S, "Start, stop and status bits"), (F, "Every output pin")),
            "The arm gate's pipeline, reset by EI.Y (z3816a-trace.md); in the Z3815A it is named only as what "
            "feeds AK, untraced."),
    element("p79", "count", "P79's constant", "AJ", "read", ((F, "Every output pin"),),
            "AJ.X (F = 0), 3-state on AI.Y: Low while AI.Y is Low, released otherwise."),
    element("p76", "count", "AK, onto P76 and P77", "AK", "open", ((F, "Every output pin"),),
            "AK on GCLK over BI, BJ, BK, the start pipeline, reset by EI.Y; untraced."),
    element("tick", "count", "Count bit 0: the gated tick", "CG", "read", ((F, "The count"),),
            "Toggles on GCLK while AI.X arms it and DI.Y does not stop it, cleared through its G while EI.Y; CG.X, "
            "EI.Y registered, resets BH."),
    element("head", "count", "Count bit 1", "BA", "read", ((F, "The count"),),
            "One flip-flop, G = ~QY, clocked by CG.Y."),
    element("stages", "count", "Count bits 2 to 23", "BB CD BD EC DC EA DA CA CB CC BC", "read", ((F, "The count"),),
            "Eleven two-bit ripple Johnson stages in that order, each on the falling edge of the one before, BB and "
            "CD mirrored; every one resets on EI.Y.",
            "Under the preset the count reads 0x{preset} (simulated, the fast run)."),
    element("stop", "count", "Stop capture", "CI", "read", ((F, "Start, stop and status bits"),),
            "Clocked by the tap KI.X; when it fires CI latches the start bit (DI = BH_Y, RD = EI_Y)."),
    element("stoppipe", "count", "Stop bit and pipeline", "CJ DI", "read", ((F, "Start, stop and status bits"),),
            "CJ.X (AG_X + CI_X) is the stop bit; CJ.QX, CJ.QY, DI.QX and DI.QY carry it on GCLK to DI.Y, which holds "
            "CG and freezes the count.",
            "The readings fall a second at a time at the testbench's 10 MHz GCLK: {counts} ticks over the preset "
            "(simulated, the three run)."),
    element("prescaler", "divider", "ACLK prescaler", "EK EL GK GL HK HL IK IL JK JL KJ KK KL LL FK FL", "simulated",
            ((F, "The 1 PPS divider"),),
            "A counter on ACLK, reset by FK.Y, whose terminal FL.Y GJ samples: a cycle of {prescale} GCLK clocks; "
            "GJ.Y sets P40, which rises every {p40} ns (simulated, the freerun run).",
            "IL, a five-input decode of GL.Y, GK.Y, HK.X, IK.X and IK.Y into JL, is read from the netlist; the "
            "trace's list of the prescaler does not name it."),
    element("gj", "divider", "Prescaled tick", "GJ", "read", ((F, "The 1 PPS divider"),),
            "GJ samples FL.Y on GCLK and makes GJ.Y one GCLK pulse each prescaler cycle; it also sets P40."),
    element("chain", "divider", "GCLK chain", "LF KF KG LI LJ KI KH LH", "read", ((F, "The 1 PPS divider"),),
            "GJ.Y enables LF and KF; KG, their carry, enables LI and LJ; KI, KH and LH follow.  KI.X, a stage bit, "
            "is the tap: it rises {tap} ns after P83 (simulated, the three run)."),
    element("terminal", "divider", "Terminal count", "JI", "simulated", ((F, "The 1 PPS divider"),),
            "JI.Y, combinational, every {chain} GJ.Y pulses: P83 every {pps_period} ns, a divide by {divide} "
            "(simulated, the three run).  It enables the seconds and sets GH."),
    element("hold", "divider", "1 PPS hold", "GH", "read", ((F, "The 1 PPS divider"),),
            "GH (F = JI_Y + ~GK_Y·FC_X) passes JI.Y, held through FC.X until the prescaler's GK.Y."),
    element("register", "divider", "1 PPS register, P83", "FC", "read", ((F, "The 1 PPS divider"),),
            "FC registers GH.X on GCLK and P83's output flip-flop registers FC.X again; FC.Y is a one-GCLK pulse at "
            "FC.X's rise.  P83 stays High {p83_high_probe} ns (simulated, the three run)."),
    element("p20", "divider", "P20: a pulse before P83", "FB", "simulated", ((F, "The 1 PPS divider"),),
            "FB makes FB.Y a one-ACLK pulse at each rise of FC.X, registered into P20 on ACLK: it comes {p20_lead} ns "
            "before P83 at the testbench's 50 ns ACLK (simulated, the three run)."),
    element("alternate", "divider", "P14 and P28: alternate seconds", "FD IB", "simulated",
            ((F, "The 1 PPS divider"),),
            "FD toggles at each internal 1 PPS, reset by register 2's bit 0; IB, on GCLK's falling edge, makes IB.Y "
            "Low from FC.Y's rise to the next falling edge on the seconds FD.Y is Low.",
            "Four seconds free: Low {alternate_low} ns at {alternate_at} (simulated, the freerun_4s run)."),
    element("field", "sync", "Sync field, register 3", "GG", "read", ((F, "The sync field"),),
            "GG.X data bit 0, GG.Y data bit 1, latched on DJ.X."),
    element("gps", "sync", "GPS edge", "CH DG", "read", ((F, "Start, stop and status bits"), (S, "The sync field")),
            "CH, clocked by BG.X as BH is, catches the start-source edge, and DG turns it into a one-GCLK pulse, "
            "DG.X, for the full sync."),
    element("pending", "sync", "Sync pending", "HG IH", "read", ((F, "The sync field"),),
            "HG and IH make IH.Y, the pending sync, on the field's change from 0."),
    element("sample", "sync", "P7 sample", "IF JF IG", "read", ((F, "The sync field"),),
            "IF and JF sample KE.X, a P7 net, on GCLK; IG.X, an edge of IF's sample, clears the pending sync."),
    element("step", "sync", "Steps, kinds 1 and 2", "JG JD", "simulated", ((F, "The sync field"),),
            "JG (with GG.X) and JD (with GG.Y), while a sync is pending, drive JE's A and B: kinds 1 and 2 step the "
            "P7 divider one P7 clock and leave the 1 PPS alone (simulated, the sync1 and sync2 runs)."),
    element("strobe3", "sync", "Full-sync strobe, kind 3", "JH", "simulated", ((F, "The sync field"),
                                                                             (F, "The 1 PPS divider")),
            "JH (F = IH_Y·GG_X·GG_Y·DG_X) fires on the start-source edge with the field at 3: JH.X resets the GCLK "
            "chain, the prescaler through FK and, through GE, the P7 divider; JH.Y into JI makes the edge the "
            "internal 1 PPS.",
            "P83 rises {sync3_after} ns after the GPS 1 PPS (simulated, the sync3 run)."),
    element("p7prescaler", "telecom", "P7 prescaler", "JE KE", "simulated", ((F, "The P7 divider: P7 / 6250"),),
            "JE and KE, a nominal /5 prescaler on P7 (4 or 6 states for other JG.Y, JD.X); KE.Y, combinational, is "
            "High once a cycle: every {ke_y} ns with P7 at 100 ns (simulated, the freerun run)."),
    element("p7counter", "telecom", "P7 counter", "FE EE IE HE CE DE FF GF HF", "simulated",
            ((F, "The P7 divider: P7 / 6250"),),
            "FE, EE, IE, HE, CE and DE, a synchronous binary counter, enabled by KE.Y and the carries FF.Y and HF.Y; "
            "not reset by the preset.  FE.Y every {fe_y} ns, EE.Y every {ee_y} ns (simulated, the freerun run)."),
    element("p7terminal", "telecom", "P7 terminal and rearm", "GE", "read", ((F, "The P7 divider: P7 / 6250"),),
            "GE decodes the counter's terminal into GE.QY, which clears it through LE.Y: P7 / {p7_divide} in all, "
            "nothing decimal.  GE.QX takes JH.X and resets JE and KE: only a full sync rearms it."),
    element("p38", "telecom", "Phase detector, P7 side", "LE", "read", ((F, "The phase detector: P38 and P40"),),
            "LE.F = GE_Y + QX: the P7 divider's terminal sets LE.X, out on P38; LE.Y resets the counter.",
            "P38 rises every {p38} ns with P7 at 100 ns (simulated, the freerun run)."),
    element("p40", "telecom", "Phase detector, GCLK side", "LD", "read", ((F, "The phase detector: P38 and P40"), (F, "What of P7 reaches the firmware")),
            "LD.G = GJ_Y + QY: the prescaled tick sets LD.QY, out on P40; LD.X = GE_X + QY·LE_X resets both, so the "
            "side that sets first stays High for the phase difference.",
            "At the frequencies the ratios imply both pins rise every {pll} ns (simulated, the pll run); that it "
            "locks a 19.6608 MHz oscillator to 10 MHz is inferred.",
            "Nothing driven by P7 reaches the firmware: the paths are read and the sync runs simulate them at one "
            "P7 phase (inferred, \u201cWhat of P7 reaches the firmware\u201d)."),
    element("tie", "other", "Constant 1", "AH", "read", ((F, "Every output pin"),),
            "AH, F = 1, through an inverting I/O block: P2 is tied Low."),
    element("passthrough", "other", "Pins passed through", "P16 P36 P47 P48 P56", "read", ((F, "Every output pin"),),
            "P16 is P82, the GPS 1 PPS; P36 is P35 inverted; P47 and P48 are P53 inverted and as it is; P56 is P50 "
            "inverted."),
    element("p84counter", "other", "P84 counter", "FH FI", "simulated", ((F, "The P84 counter"),),
            "FH and FI, clocked by P84 with no reset but configuration's, count P84's rises in four bits, read at "
            "the odd addresses in bits 4 to 7 and discarded by the firmware."),
    element("p24", "other", "P24 and P25", "HB", "read", ((F, "Every output pin"),),
            "HB.F = ~PAD80_I·~EG_X + PAD80_I·PAD84_I: P27 High passes P23, Low gives ~EG.X; P24 is HB.Y inverted."),
    element("lk", "other", "P51 and P57", "LK", "read", ((F, "Every output pin"),),
            "LK.G = QY·~PAD52_I + ~QY·PAD52_I: an ACLK-prescaler state (KL, LL) XOR P50; P57 is P51 inverted."),
    element("p35clock", "other", "P35 toggle", "LB", "read", ((F, "The P35 counter"),),
            "LB, a toggle on P35 with no reset but configuration's: LB.Y, P35 halved, on P34, clocks the counter."),
    element("p35five", "other", "P35 counter: the /5", "LA KA", "read", ((F, "The P35 counter"),),
            "LA, a two-bit counter cleared while KA.Y is High, and KA.QY: five states; KA.QX is the binary count's "
            "bit 0."),
    element("p35binary", "other", "P35 counter: binary", "HD IA JC IC HC LC KC JB ID KD", "read",
            ((F, "The P35 counter"),),
            "A thirteen-bit binary count on LB.Y, two-bit stages or toggles each enabled by the carry below it "
            "(HD.X, JB.Y, ID.Y, HA.Y, KD.Y)."),
    element("p26", "other", "P26", "HA", "read", ((F, "The P35 counter"),),
            "HA.X, set by the carry out of bit 8 and cleared at the next LB.Y edge with KA.X High."),
    element("p35decade", "other", "P35 counter: decade, P19 and P21", "GA FA", "read", ((F, "The P35 counter"),),
            "A decade on KD.Y; FA.X, its bit 3, is P21 and inverted P19."),
    element("p35reset", "other", "P35 counter: reset", "JA", "read", ((F, "The P35 counter"),),
            "JA.Y resets the counter once per internal 1 PPS, while FC.X is High for a KA.Y."),
    element("gclk12", "other", "GCLK / 12", "GB GC GD", "read", ((F, "The P35 counter"),),
            "Six flip-flops in a twisted ring on GCLK, cleared by FC.Y: GC.X is GCLK / 12, its phase set by the "
            "internal 1 PPS."),
    element("p29", "other", "P29 and P30", "KB", "read", ((F, "The P35 counter"),),
            "KB.QY registers GC.X; KB.Y is the XNOR of KB.QY with LA.Y + KA.Y, GCLK / 12 against LB.Y / 5."),
)

TELECOM_PINS = {
    **SHARED,
    "P7": Pin("P7", "the second clock, into the P7 divider"),
    "P53": Pin("clock", "the oscillator, into ACLK"),
    "P83": Pin("1 PPS", "the internal 1 PPS"),
    "P14": Pin("1 PPS", "alternate seconds, active Low"),
    "P28": Pin("1 PPS", "alternate seconds, active Low"),
    "P20": Pin("1 PPS", "a one-ACLK pulse before P83"),
    "P38": Pin("phase detector", "the P7 side"),
    "P40": Pin("phase detector", "the GCLK side"),
    "P47": Pin("passed through", "P53, inverted"),
    "P48": Pin("passed through", "P53"),
    "P51": Pin("P51, P57", "role not traced"),
    "P57": Pin("P51, P57", "role not traced"),
}

Z3815A_PINS = {
    **TELECOM_PINS,
    "P78": Pin("start source", "a start source, at 0x08 or 0x0c"),
    "P84": Pin("P84", "a start source at 0x04; the P84 counter's clock"),
    "P35": Pin("P35", "the P35 counter's clock"),
    "P36": Pin("passed through", "P35, inverted"),
    "P34": Pin("P35 counter", "LB.Y, P35 halved"),
    "P19": Pin("P35 counter", "the decade's bit 3, inverted"),
    "P21": Pin("P35 counter", "the decade's bit 3"),
    "P26": Pin("P35 counter", "two LB.Y periods in 512"),
    "P29": Pin("P35 counter", "GCLK / 12 XNOR LB.Y / 5"),
    "P30": Pin("P35 counter", "P29, inverted"),
    "P23": Pin("P23, P27", "passed to P24, P25 while P27 is High"),
    "P27": Pin("P23, P27", "selects P23 or ~EG.X"),
    "P24": Pin("P24, P25", "~HB.Y"),
    "P25": Pin("P24, P25", "HB.Y"),
    "P50": Pin("P50", "an input, into LK and P56"),
    "P56": Pin("passed through", "P50, inverted"),
}


# --- the Z3816A, the Z3815A's twin -----------------------------------------------

Z3816A = derive(Z3815A, S, keep={
    "aclk": ("Clocks", "Driven straight from P53, the oscillator; ACLK clocks {loads[ACLK.O]} logic blocks: AB, the "
             "prescaler, FB for P20 and LK."),
    "gclk": ("Clocks", "AB (F = ~QY, K = ACLK_O), a lone toggle, drives GCLK: ACLK / 2, every {aclk_halved} ns "
             "(simulated, the freerun run).  GCLK clocks {loads[GCLK.O]} blocks; P7 reaches {loads[PAD7.I]} through "
             "general routing."),
    "strobe": ("Register port", "CL (F = C + ~E, G = ~C·~E, the select and R/W) gates the bus, and DL makes a "
               "one-GCLK write strobe."),
    "decode": ("Register port", "DJ.Y register 0, EJ.Y register 1, EJ.X register 2 and DJ.X register 3, each with "
               "ADDR2 Low."),
    "register0": ("Register port", "EI.Y the preset, bit 6; EI.X ~(DATA0·DATA1), Low in mode 3."),
    "phases": ("Register port", "A two-bit Johnson counter on GCLK: P71 and P67 register on AL.Y, P70 and P65 on "
               "AL.X."),
    "interpolator": ("The count", "AA, AC, AD and AE, unclocked, merge the interpolator's pins with the head's terms, "
                     "BA.X for bit 1 (AE) and BB.Y for bits 2 and 3 (AD)."),
    "mux01": ("Register port", "CK.X = DK_Y ? CF_Y : CJ_X, CK.Y = DK_Y ? CF_Y : BH_Y, DK.Y the read address's "
              "bit 4."),
    "mux23": ("Start, stop and status bits", "FJ.X = ~DK_Y + DF_X, 1 at every status address; FJ.Y = DK_Y ? DF_X : "
              "GH_Y: the status reads 0x{status} (simulated, the fast run)."),
    "gate": ("Start, stop and status bits", "AI (K = GCLK, RD = EI_Y) is the start-armed flag; AI.X gates the tick "
             "CG."),
    "pipeline": ("Start, stop and status bits", "AF, AG, BI, BJ, BK: the arm gate's pipeline, all reset by EI.Y; "
                 "what each does is not traced.", "read"),
    "p79": ("Every output pin", "AJ.X (F = 0), 3-stated by AI.Y; board use not traced."),
    "p76": ("Every output pin", "The start pipeline's AK, reset by EI.Y, inverted onto P76 and P77; board use not "
            "traced.", "read"),
    "tick": ("The count", "CG.Y is cleared while EI.Y is High, toggles while AI.X is High and DI.Y Low, and holds "
             "once DI.Y goes High: the Z3805A's CF.Y."),
    "head": ("The count", "BA.QY, one flip-flop, G = ~QY, clocked by CG.Y."),
    "stages": ("The count", "BB, CD, BD, EC, DC, EA, DA, CA, CB, CC and BC, BB and CD mirrored, each on the falling "
               "edge of the one before, reset by EI.Y; under the preset the count reads 0x{preset} (simulated, the "
               "fast run)."),
    "stop": ("Start, stop and status bits", "CI (F = B = BH_Y, RD = EI_Y) is clocked by KI.X, the divider tap."),
    "stoppipe": ("Start, stop and status bits", "CJ.X is the stop bit; CJ.Y feeds DI, whose DI.Y stops CG: DI is the "
                 "stop pipeline."),
    "prescaler": ("The 1 PPS divider", "EK, EL, GK, GL, HK and IK on ACLK, reset by FK.Y, with HL, JK and KJ among "
                  "their enables and KK, KL, LL and JL the stages KJ reads; FL.Y its terminal.  P40 rises every {p40} "
                  "ns (simulated, the freerun run).  IL, a five-input decode into JL read from the netlist, is not "
                  "in the trace's list."),
    "gj": ("The 1 PPS divider", "GJ (DI = FL_Y, G = QX·~QY) makes GJ.Y a one-GCLK tick each prescaler cycle."),
    "chain": ("The 1 PPS divider", "LF and KF count GJ's ticks; KG their carry into LI and LJ; KI.Y enables KI, KH "
              "and LH.  The tap, KI.X, rises {tap} ns after P83 (simulated, the three run)."),
    "terminal": ("The 1 PPS divider", "JI_Y = JH_Y + LH_X·KG_X·LH_Y·KH_Y: P83 every {pps_period} ns, a divide by "
                 "{divide} (simulated, the three run)."),
    "hold": ("The 1 PPS divider", "GH.X = JI_Y + ~GK_Y·FC_X passes JI.Y to FC and holds FC.X until GK.Y."),
    "register": ("The 1 PPS divider", "FC (K = GCLK, DI = GH_X) registers it, and P83's output register takes FC.X "
                 "a GCLK later.  P83 stays High {p83_high_probe} ns (simulated, the three run)."),
    "p20": ("P20", "FB.Y, FC_X·~(FC_X one ACLK late), registered in P20's I/O block: P83 rises {p20_lead} ns after "
            "it (simulated, the three run)."),
    "alternate": ("P14 and P28: every other second", "FD toggles at each internal 1 PPS, reset by EF.X; IB.Y = FD_Y "
                  "+ ~FC_Y + ~IB.QY: Low {alternate_low} ns at {alternate_at} (simulated, the freerun_4s run)."),
    "field": ("The sync field", "GG on DJ.X: GG.X DATA0, GG.Y DATA1."),
    "pending": ("The sync field", "HG (HG.X = GG_X + GG_Y) and IH make IH.Y, the pending sync, cleared by IG.X or "
                "JH.Y."),
    "sample": ("The sync field", "IF and JF sample KE.X, the P7 prescaler's state, on GCLK; IG detects the sample's "
               "edge."),
    "step": ("The sync field", "JG (with GG.X) and JD (with GG.Y), gated by IH.Y, drive JE's A and B: kinds 1 and 2 "
             "step the P7 divider, not the 1 PPS (simulated, the sync1 and sync2 runs)."),
    "strobe3": ("The sync field", "JH fires with the field at 3, a sync pending and the GPS pulse: JH.X resets the "
                "GCLK chain, the prescaler through FK and, through KD, the P7 divider; P83 rises {sync3_after} ns "
                "after the GPS 1 PPS (simulated, the sync3 run)."),
    "p7prescaler": ("The P7 divider", "JE and KE (RD = KD_X), a nominal /5 prescaler; KE.Y every {ke_y} ns "
                    "(simulated, the freerun run)."),
    "p7counter": ("The P7 divider", "FE, EE, IE, HE, CE, DE, enabled by KE.Y and the carries GF, FF and HF, reset by "
                  "LE.Y; FE.Y every {fe_y} ns, EE.Y every {ee_y} ns (simulated, the freerun run)."),
    "p38": ("P38 and P40: a phase-frequency detector", "LE.QX, P38 (F = GE_Y + QX, RD = LD_X), set by the P7 "
            "divider's terminal; P38 every {p38} ns (simulated, the freerun run)."),
    "p40": ("P38 and P40: a phase-frequency detector", "LD.QY, P40 (G = GJ_Y + QY), set by GJ.Y; LD.X = KD_X + "
            "LD.QY·LE.X resets both.  At the implied frequencies both rise every {pll} ns (simulated, the pll run); "
            "that it locks a 19.6608 MHz oscillator to 10 MHz is inferred, the board's 5 MHz MTI Series 260 OCXO "
            "presumably doubled to that 10 MHz (inferred, not traced; \u201cClocks\u201d), and nothing driven by P7 "
            "reaches the firmware (inferred, \u201cWhat of P7 reaches the firmware\u201d)."),
    "tie": ("Every output pin", "AH, F = 1, no inputs, inverted: P2 is tied Low."),
}, changes={
    "register1": element("register1", "port", "Register 1: start bits and read address", "DH FG EH DK", "read",
                         ((S, "Register port"),),
                         "DH bits 0 and 1, into the start logic, AI, AF and BI; FG, EH and DK the read address, bits 3 "
                         "to 7."),
    "register2": element("register2", "port", "Register 2: phase", "EF BF", "read", ((S, "Register port"),
                                                                                   (S, "The 1 PPS divider")),
                         "EF bit 0; EF.X (EF.QY·~BF_X, BF.X EF.QY a GCLK late) is a pulse on its rising edge, which "
                         "resets FD and so sets which seconds P14 and P28 pulse."),
    "select": element("select", "read", "Readout decode", "CF DF BE ED DD DB EB", "read", ((S, "The count"),),
                      "CF, DF, BE, ED and DD, combinational, read each stage's read term, a decode over AL.Y and DB or "
                      "EB, the address selects from FG; CF.Y and DF.X carry the nibble to bits 0 to 3."),
    "seconds": element("seconds", "read", "Seconds counter, bits 4 to 7", "II IJ", "read",
                       ((S, "Register port"), (S, "The 1 PPS divider")),
                       "II and IJ, enabled by JI.Y, onto the bus's bits 4 to 7 unregistered."),
    "start": element("start", "count", "Start capture", "BH", "read", ((S, "Start, stop and status bits"),),
                     "The GPS 1 PPS on P82 clocks BH directly (DI = EI_X, RD = CG_X); BH.X, the interval the "
                     "interpolator measures, is Low on P81, and BH.Y (G = QX + AG_Y) is the start bit."),
    "gps": element("gps", "sync", "GPS edge", "CH DG", "read", ((S, "The sync field"),),
                   "CH (F = 1, K = PAD15_I, RD = DG_X) catches the GPS edge and DG (DG.X = ~QX·QY) makes it a "
                   "one-GCLK pulse."),
    "p7terminal": element("p7terminal", "telecom", "P7 terminal and rearm", "GE KD", "read", ((S, "The P7 divider"),),
                          "GE decodes the terminal into GE.Y, clearing the counter through LE.Y: P7 / {p7_divide}; "
                          "here GE has DI = 0 and no X.  KD (F = JH_X, on P7) takes the full sync onto P7: KD.X resets "
                          "JE and KE, the counter and the detector."),
    "passthrough": element("passthrough", "other", "Pins passed through", "P16 P47 P48 P52", "read",
                           ((S, "Every output pin"),),
                           "P16 is P82, the GPS 1 PPS; P47 is P53 inverted, P48 and P52 P53 as it is."),
    "lk": element("lk", "other", "P51 and P57", "LK", "read", ((S, "Every output pin"),),
                  "LK.QY, registered on ACLK from the prescaler's stages KL and LL; P57 is P51 inverted.  Board use "
                  "not traced."),
}, drop=("bits47", "source", "p84counter", "p24", "p35clock", "p35five", "p35binary", "p26", "p35decade", "p35reset",
         "gclk12", "p29"))

Z3816A_PINS = {
    **TELECOM_PINS,
    "P52": Pin("passed through", "P53"),
}

# --- the Z3817A, a sixth design of the first family ------------------------------

# Its blocks sit in other tiles than the Z3805A's, so no element is kept
# as derive() keeps one: each is the trace's own, citing the Z3805A's
# trace too where it calls the blocks the Z3805A's.
E = "z3817a"
Z3817A = (
    element("gclk", "clocks", "Global clock, GCLK", "GCLK", "read", ((E, "Clocks"),),
            "P7 drives the one global clock buffer through general routing, as in the Z3805A; ACLK is unused.",
            "GCLK clocks {loads[GCLK.O]} logic blocks; P84 clocks HG and HH, the P84 counter."),
    element("strobe", "port", "Select and write strobe", "GL GK", "read", ((E, "Register port"),),
            "GL.X, over P61 and P75, is the bus's 3-state control; GL.Y, the select and R/W both Low, is sampled "
            "into GK, whose GK.Y is a one-clock write strobe."),
    element("decode", "port", "Write decode", "FL EL", "read", ((E, "Register port"),),
            "FL.X register 0, FL.Y register 1, EL.X register 3, all with ADDR2 Low, and EL.Y register 4, ADDR2 High.",
            "Register 2 decodes to nothing."),
    element("register0", "port", "Register 0: preset", "LH", "read", ((E, "Register port"),),
            "Bit 6 alone, LH, the preset, which resets every stage of the count, the start and the stop; there is "
            "no mode field."),
    element("register1", "port", "Register 1: read address", "FJ DJ EJ", "read", ((E, "Register port"),),
            "Bits 3 to 7, the read address, in FJ (bits 3 and 4), DJ (5 and 6) and EJ (7).",
            "Bits 0 to 2 are not kept: no start or reference bit."),
    element("phases", "read", "Bus phases", "AL", "read", ((E, "Register port"), (E, "The count")),
            "A two-bit Johnson stage on GCLK: P71, P70, P67 and P65 register on AL.X or AL.Y, and AL.Y picks one of "
            "each count stage's two bits."),
    element("address", "read", "Read address decode", "DD DF", "read", ((E, "The count"),),
            "DD and DF decode FJ, register 1's bits 3 and 4, the address's bits 0 and 1, picking a group of "
            "stages."),
    element("select", "read", "Count read multiplexers", "DH CG BF BG CC CE CF CJ CK DC", "read",
            ((E, "Register port"),),
            "DH.Y and CG.X, the count's read multiplexers, select on DJ, the address's bits 2 and 3, among BF, BG, "
            "CC, CE, CF, CJ, CK and DC, terms over the stages' read outputs and DD and DF."),
    element("mux01", "read", "Read multiplexer, bits 0 and 1", "BK", "read", ((E, "Register port"),),
            "BK.Y = EJ.X ? DH.Y : CI.X onto P71 and BK.X = EJ.X ? DH.Y : BI.X onto P70: the stop and start bits, as "
            "in the Z3805A, unless the address's bit 4 is set."),
    element("mux23", "read", "Read multiplexer, bits 2 and 3", "FK", "simulated",
            ((E, "Register port"), (E, "Start, stop and status bits")),
            "FK.Y = EJ.X ? CG.X : FI.X onto P67 and FK.X = EJ.X ? CG.X : GJ.X onto P65: status bits 2 and 3 are "
            "flags of the 1 PPS chain, not the interpolator's (read).",
            "Under the preset the status reads 0x{status} (simulated, the fast run)."),
    element("bits47", "read", "Bits 4 to 7: seconds or P84", "IH II IJ IK", "read", ((E, "Register port"),),
            "IH, II, IJ and IK onto P62, P60, P59 and P58, each FJ.Y ? a P84 counter bit : a seconds bit, FJ.Y the "
            "address's bit 0: the Z3815A's arrangement."),
    element("seconds", "read", "Seconds counter", "JH JI", "read", ((E, "Register port"),),
            "JH and JI, enabled by GE.X, the internal 1 PPS."),
    element("source", "count", "Start select, register 4", "AG", "simulated", ((E, "Register port"),
                                                                                  (E, "Start, stop and status bits")),
            "AG.QY holds register 4's bit 0, and AG.X = AG.QY ? P84 : P82 is the selected 1 PPS, which clocks the "
            "start (read).",
            "With the bit set and a 1 PPS on P84, P84 starts the count: the reading is {select_count} ticks over the "
            "preset (simulated, the select run).  The firmware's two writers of the bit pass 0 (read)."),
    element("start", "count", "Start capture", "BI CH", "read", ((E, "Start, stop and status bits"),),
            "AG.X clocks BI, whose QY, the start bit, sets on its rising edge (its DI is CH.X, a constant 1); BH.X, "
            "the preset through BH, resets it."),
    element("gate", "count", "Start gate", "AI", "simulated", ((E, "Start, stop and status bits"),),
            "AI.QY takes the start on the next GCLK edge and holds it, and the count runs from then (read).",
            "AI.Y rises {gate_after} ns after the 1 PPS (simulated, the three run): with no reference bit the "
            "count opens on the first clock edge after it, as the Z3805A's does with the bit Low (inferred)."),
    element("constant", "count", "AH: no start pulse", "AH", "read", ((E, "Start, stop and status bits"),),
            "AH, the Z3805A's start pulse, is here two flip-flops whose data is 0: its outputs are a constant Low, "
            "so CI's F is BJ.X alone and no pin carries a start pulse."),
    element("tick", "count", "Count bit 0: the gated tick", "BH", "read", ((E, "The count"),),
            "A toggle on GCLK while AI.Y, the start, is High and DI.Y, the stop, Low; LH, the preset, resets it."),
    element("head", "count", "Count bit 1", "BD", "read", ((E, "The count"),),
            "One flip-flop, G = ~QY, clocked by BH.Y."),
    element("stages", "count", "Count bits 2 to 31", "BE AF AE AC BC DB CB BB AB BA AA AD AJ AK BL", "read",
            ((E, "The count"), (Z, "The count")),
            "Fifteen two-bit ripple Johnson stages, the Z3805A's kind, each clocked by the one before; the preset "
            "resets all.",
            "Under the preset every nibble reads 5, the count 0x{preset} (simulated, the fast run)."),
    element("tapbuffer", "count", "Tap buffer", "EH", "read", ((E, "Clocks"),),
            "EH.Y, a buffer of the divider's tap GH.Y, clocks the stop capture."),
    element("stop", "count", "Stop capture", "BJ", "simulated", ((E, "Start, stop and status bits"),),
            "BJ, the Z3805A's CI, clocked by the tap through EH, takes BI.X, so only a tap after a start stops the "
            "count (read).",
            "The tap rises {tap} ns after P78 and the stop bit, CI.X, {stop_after} ns after P78 (simulated, the "
            "three run)."),
    element("stoppipe", "count", "Stop pipeline", "CI DI", "simulated", ((E, "Start, stop and status bits"),),
            "CI.QX, CI.QY, DI.QX and DI.QY carry the stop on GCLK, and DI.Y holds BH: the Z3805A's CJ and DG, CI "
            "ORing in AH.Y, constant Low (read).",
            "With the P82 1 PPS at 0.25 s each reading is {count} ticks over the preset, the Z3805A's figure with "
            "its reference bit Low (simulated, the three run)."),
    element("prescaler", "divider", "Prescaler", "JA JB IC KC JC ID JD KD LD", "simulated",
            ((E, "The 1 PPS divider"),),
            "JA, a two-bit counter, and JB.X, its terminal, a prescaler of 4 or 5 clocks; IC, KC, JC and ID, a "
            "counter enabled by JB.X whose terminal ID.Y, through KD and LD, restarts JA and the counter: the "
            "Z3805A's GI, HI, IG, JH, IH, JI, II, HJ and FJ (check-blocks).",
            "JB.X every {jb_x} ns, LD.X every {ld_x} ns (simulated, the three run)."),
    element("binary", "divider", "Six-bit counter", "FA GA FC GC FB GB HD", "simulated", ((E, "The 1 PPS divider"),),
            "FA, GA, FC and GC, a six-bit counter enabled by LD.X, and HD.Y its terminal: the Z3805A's KH to KK, "
            "JJ and JK (check-blocks); GB's X ORs in HE.X, the kind 2 step.",
            "HD.Y every {hd_y} ns (simulated, the three run)."),
    element("decades", "divider", "Decades and the tap", "GF GG HF FE FF FG FH GH", "simulated",
            ((E, "The 1 PPS divider"),),
            "Three decades, GF and GG (HF.Y), FE and FF (GH.X), FG and FH: the Z3805A's KF, KG, JG, GE, GF, HF, GG "
            "and HG (check-blocks).",
            "HF.Y every {hf_y} ns; GH.Y, the top decade's late states, is the tap: it rises {tap} ns after P78 "
            "(simulated, the three run)."),
    element("terminal", "divider", "Terminal count", "GD GE", "simulated", ((E, "The 1 PPS divider"),
                                                                            (E, "The sync field")),
            "GD and GE, the Z3805A's HH split in two; GE.X, the internal 1 PPS, ORs in IF.Y, kind 3's restart "
            "(read).",
            "P78 every {pps_period} ns: a divide by {divide} (simulated, the three run)."),
    element("driver", "divider", "1 PPS output driver", "FI.Y FD", "simulated", ((E, "Other outputs"),
                                                                                  (E, "The 1 PPS divider")),
            "FI.Y = FD.Y + GE.X, registered on GCLK onto P78; FD sets on GE.X and holds until FA.Y, FC.X and GA.X "
            "are High together (read).",
            "P78 stays High {pps_high} ns (simulated, the three run)."),
    element("flags", "divider", "Chain flags and P72", "FI.X GJ", "simulated",
            ((E, "Register port"), (E, "Other outputs")),
            "FI.X and GJ.X, flags of the 1 PPS chain, are status bits 2 and 3, which the firmware does not test "
            "(read).",
            "GJ.Y, set on HD.Y and cleared on the next prescaler tick, is P72: every {p72_period} ns, High "
            "{p72_high} ns (simulated, the three run)."),
    element("field", "sync", "Sync field, register 3", "IG JE", "read", ((E, "The sync field"), (E, "Register port")),
            "IG keeps register 3's bits 0 and 1; JE, a clock behind, marks the field's change from 0: the Z3805A's "
            "DI and DJ (check-blocks)."),
    element("pending", "sync", "Sync pending", "JF IE", "simulated", ((E, "The sync field"),),
            "JF.QY sets on the field's change from 0 and clears on IE.X = IF.Y + GE.X·(~IG.Y + ~IG.X): kind 3's "
            "restart, or the internal 1 PPS for kinds 1 and 2 only, so a kind 3 stays pending across internal "
            "1 PPS edges (read).",
            "A kind 3 whose 1 PPS comes after an internal 1 PPS is not simulated (open)."),
    element("step", "sync", "Steps, kinds 1 and 2", "HE HC", "simulated", ((E, "The sync field"),),
            "HE.Y and HE.X, the step lines of kinds 1 and 2, come with the pending sync in the second's last "
            "millisecond; HC, the Z3805A's GK in another encoding, steps ID's terminal (read).",
            "The reading after is {sync1} (kind 1) and {sync2} (kind 2) ticks over the preset, against {count} "
            "(simulated, the sync1 and sync2 runs)."),
    element("restart", "sync", "1 PPS edge and restart, kind 3", "CD IF", "simulated", ((E, "The sync field"),),
            "CD, the selected 1 PPS's rising edge on GCLK, feeds IF, whose IF.QX resets the decades and the six-bit "
            "counter: the Z3805A's DH and EJ, CD sampling AG.X where the Z3805A's DH samples P82 (read).",
            "IF.Y rises {restart_after} ns after the 1 PPS, and the restart is itself the internal 1 PPS: P78 rises "
            "at {sync3_pps} s (simulated, the sync3 run)."),
    element("p84counter", "other", "P84 counter", "HG HH", "read", ((E, "Register port"),),
            "HG and HH, clocked by P84 with nothing to reset them, count P84's rises in four bits, read at the odd "
            "addresses in bits 4 to 7; what the firmware does with it is open."),
    element("passthrough", "other", "Pins passed through", "P16", "read", ((E, "Other outputs"),),
            "P16 is P82 passed through, unregistered, as in the Z3805A."),
    element("p19", "other", "P19: a counter's bit", "EE", "simulated", ((E, "Other outputs"),),
            "P19 is EE.Y, a counter's bit.",
            "P19 rises every {p19_period} ns, GCLK / {p19_divide} (simulated, the fast run)."),
)

Z3817A_PINS = {
    **BUS, **PORT,
    "P7": Pin("clock", "the oscillator, into GCLK"),
    "P16": Pin("passed through", "P82, passed through"),
    "P82": Pin("1 PPS in", "a 1 PPS, the start's with register 4's bit 0 clear"),
    "P84": Pin("1 PPS in", "a 1 PPS, the start's with the bit set; the P84 counter's clock"),
    "P78": Pin("1 PPS", "the internal 1 PPS"),
    "P72": Pin("P72", "GJ.Y, a pulse every 10,000 clocks"),
    "P19": Pin("P19", "EE.Y, a counter's bit"),
}

SPECS = {
    "z3805a": DesignSpec(Z3805A, Z3805A_PINS),
    "58503a-z3801a": DesignSpec(A58503, A58503_PINS),
    "58503b": DesignSpec(B58503, B58503_PINS),
    "z3815a": DesignSpec(Z3815A, Z3815A_PINS),
    "z3816a": DesignSpec(Z3816A, Z3816A_PINS),
    "z3817a": DesignSpec(Z3817A, Z3817A_PINS),
}
