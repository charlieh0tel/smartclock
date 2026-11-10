"""A drawing as shapes: boxes, text, wires and dots, written out as SVG
and checked for collisions before it is.

detail_draw.py builds the FPGA detail page's drawings on a Sheet.  Each
text knows its container and each wire its own id, so problems() can
say what overlaps what: a wire through a box it does not belong to, two
wires along one line, text on text, on a box or on a wire not its own,
and text running out of its box.  Text is measured by an estimate of
its font's advance, generous enough for the fallback fonts a browser
without IBM Plex uses.
"""

import html
import itertools
from dataclasses import dataclass

# Each text class's font size and the advance of one character, px:
# the CSS in detail.py sets the same sizes.  Mono advances are 0.61 of
# the size, the sans ones a little over the widest fallback's average.
METRICS = {
    "ch-ttl": (14, 8.4),
    "ch-ctrt": (12, 7.2),
    "ch-lab": (12, 6.7),
    "ch-clb": (10.5, 6.45),
    "ch-net": (10.5, 6.45),
    "ch-eq": (11, 6.75),
    "ch-p": (11, 6.75),
    "ch-pt": (11, 6.75),
    "ch-mod": (11, 6.75),
    "ch-mk": (10, 6.2),
}

# How far two shapes may meet before they overlap, px: a wire may end
# on a box's edge, and text may touch the edge of its own box.
EPSILON = 0.6


def esc(text):
    return html.escape(str(text), quote=False)


def metrics(cls):
    """A text class's size and advance, by its first class name."""
    return METRICS[cls.split()[0]]


def text_width(cls, text):
    return metrics(cls)[1] * len(text)


@dataclass
class Box:
    """A rectangle: a block, a pin, a pill, a counter, a badge."""

    x: float
    y: float
    w: float
    h: float
    cls: str
    # The box it sits in, or None.
    parent: "Box | None" = None
    # Its corner radius.
    rx: float = 3
    # Whether wires and other text must keep out of it (the chip's
    # frame and a row's band are not).
    solid: bool = True
    # A badge's circle is drawn round its box.
    circle: bool = False

    def ancestors(self):
        box = self
        while box is not None:
            yield box
            box = box.parent

    def contains(self, x0, y0, x1, y1):
        return (x0 >= self.x - EPSILON and y0 >= self.y - EPSILON
                and x1 <= self.x + self.w + EPSILON and y1 <= self.y + self.h + EPSILON)


@dataclass
class Text:
    """One line of text, its baseline at y."""

    x: float
    y: float
    text: str
    cls: str
    # The box it is written in, or None for a wire's label.
    box: Box | None = None
    # The wire it labels, or None.
    wire: int | None = None
    anchor: str = "start"

    def bounds(self):
        size, _ = metrics(self.cls)
        width = text_width(self.cls, self.text)
        x0 = {"start": self.x, "middle": self.x - width / 2, "end": self.x - width}[self.anchor]
        return x0, self.y - 0.8 * size, x0 + width, self.y + 0.22 * size


@dataclass
class Segment:
    """A straight piece of a wire."""

    x1: float
    y1: float
    x2: float
    y2: float
    wire: int
    # The box a wire inside a box (between a divider's counters) is
    # drawn in, which it may cross.
    box: Box | None = None

    def bounds(self):
        return min(self.x1, self.x2), min(self.y1, self.y2), max(self.x1, self.x2), max(self.y1, self.y2)


def overlap(a, b, margin=EPSILON):
    """Whether two bounds (x0, y0, x1, y1) overlap by more than margin."""
    return (min(a[2], b[2]) - max(a[0], b[0]) > margin) and (min(a[3], b[3]) - max(a[1], b[1]) > margin)


def inside(segment, box):
    """Whether a segment runs through a box's inside, not just its edge."""
    x0, y0, x1, y1 = segment.bounds()
    return overlap((x0 - 0.01, y0 - 0.01, x1 + 0.01, y1 + 0.01), (box.x, box.y, box.x + box.w, box.y + box.h))


class Sheet:
    """A drawing's shapes, in drawing order."""

    def __init__(self):
        self.boxes = []
        self.texts = []
        self.wires = []       # each (points, cls, marker, id, box)
        self.segments = []
        self.dots = []
        self.next_wire = 0

    def box(self, x, y, w, h, cls, parent=None, rx=3, solid=True, circle=False):
        box = Box(x, y, w, h, cls, parent, rx, solid, circle)
        self.boxes.append(box)
        return box

    def text(self, x, y, text, cls, box=None, wire=None, anchor="start"):
        item = Text(x, y, text, cls, box, wire, anchor)
        self.texts.append(item)
        return item

    def wire(self, points, cls="ch-w", marker="ch-a", box=None):
        """A wire through the points, its arrow at the last; its id."""
        wire = self.next_wire
        self.next_wire += 1
        self.wires.append((points, cls, marker, wire, box))
        for (x1, y1), (x2, y2) in zip(points, points[1:]):
            self.segments.append(Segment(x1, y1, x2, y2, wire, box))
        return wire

    def dot(self, x, y):
        self.dots.append((x, y))

    def clear(self, bounds, wire=None, box=None):
        """Whether bounds are free of text, solid boxes other than box and
        its ancestors, and wires other than wire."""
        mine = set(box.ancestors()) if box else set()
        if any(b.solid and b not in mine and overlap(bounds, (b.x, b.y, b.x + b.w, b.y + b.h))
               for b in self.boxes):
            return False
        if any(overlap(bounds, t.bounds()) for t in self.texts):
            return False
        return not any(s.wire != wire and overlap(bounds, s.bounds(), -1) for s in self.segments)

    def problems(self):
        """Every collision, each a line saying what meets what."""
        found = []
        for s in self.segments:
            mine = set(s.box.ancestors()) if s.box else set()
            for b in self.boxes:
                if b.solid and b not in mine and inside(s, b):
                    found.append(f"wire {s.wire} runs through the {b.cls} box at {b.x:.0f},{b.y:.0f}")
        for a, b in itertools.combinations(self.segments, 2):
            if a.wire == b.wire:
                continue
            horizontal = abs(a.y1 - a.y2) < EPSILON and abs(b.y1 - b.y2) < EPSILON and abs(a.y1 - b.y1) < EPSILON
            vertical = abs(a.x1 - a.x2) < EPSILON and abs(b.x1 - b.x2) < EPSILON and abs(a.x1 - b.x1) < EPSILON
            if horizontal and min(max(a.x1, a.x2), max(b.x1, b.x2)) - max(min(a.x1, a.x2), min(b.x1, b.x2)) > 1:
                found.append(f"wires {a.wire} and {b.wire} share the line y = {a.y1:.0f}")
            if vertical and min(max(a.y1, a.y2), max(b.y1, b.y2)) - max(min(a.y1, a.y2), min(b.y1, b.y2)) > 1:
                found.append(f"wires {a.wire} and {b.wire} share the line x = {a.x1:.0f}")
        for a, b in itertools.combinations(self.texts, 2):
            if overlap(a.bounds(), b.bounds()):
                found.append(f"text {a.text!r} overlaps text {b.text!r}")
        for t in self.texts:
            bounds = t.bounds()
            mine = set(t.box.ancestors()) if t.box else set()
            for b in self.boxes:
                if b.solid and b not in mine and overlap(bounds, (b.x, b.y, b.x + b.w, b.y + b.h)):
                    found.append(f"text {t.text!r} overlaps the {b.cls} box at {b.x:.0f},{b.y:.0f}")
            for s in self.segments:
                if s.wire != t.wire and overlap(bounds, s.bounds(), -1):
                    found.append(f"text {t.text!r} lies on wire {s.wire}")
            if t.box is not None and not t.box.contains(*bounds):
                found.append(f"text {t.text!r} runs out of its {t.box.cls} box")
        return found

    def svg(self, width, height, label, defs):
        out = [f'<svg class="ch" viewBox="0 0 {width:.0f} {height:.0f}" role="img" '
               f'xmlns="http://www.w3.org/2000/svg" aria-label="{esc(label)}">', defs]
        for b in self.boxes:
            if b.circle:
                out.append(f'<circle class="{b.cls}" cx="{b.x + b.w / 2:.1f}" cy="{b.y + b.h / 2:.1f}" '
                           f'r="{b.w / 2:.1f}"/>')
            else:
                out.append(f'<rect class="{b.cls}" x="{b.x:.1f}" y="{b.y:.1f}" width="{b.w:.1f}" '
                           f'height="{b.h:.1f}" rx="{b.rx}"/>')
        for points, cls, marker, _, _ in self.wires:
            d = " ".join(f"{x:.1f},{y:.1f}" for x, y in points)
            end = f' marker-end="url(#{marker})"' if marker else ""
            out.append(f'<polyline class="{cls}" points="{d}"{end}/>')
        for x, y in self.dots:
            out.append(f'<circle class="ch-dot" cx="{x:.1f}" cy="{y:.1f}" r="3"/>')
        for t in self.texts:
            anchor = f' text-anchor="{t.anchor}"' if t.anchor != "start" else ""
            out.append(f'<text class="{t.cls}" x="{t.x:.1f}" y="{t.y:.1f}"{anchor}>{esc(t.text)}</text>')
        out.append("</svg>")
        return "\n".join(out)
