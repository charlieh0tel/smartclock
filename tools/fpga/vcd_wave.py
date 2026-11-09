#!/usr/bin/env python3
"""Render a waveform figure from a Verilog VCD, in the FPGA docs' house style.

This reads a VCD a testbench dumped (`+vcd=FILE`, tools/fpga/*_tb.v) and
draws its signals: every edge sits at the time the simulator recorded,
scaled into the window.

    tools/fpga/vcd_wave.py DUMP.vcd FIGURE.json > figure.svg

FIGURE.json is one figure's spec:

    {
      "title": "Z3805A read port: one register read",
      "window": [122000, 129000],          # [t0, t1] in VCD time units (ps)
      "unit": "us",                         # axis label unit: ps|ns|us|ms
      "signals": [
        {"path": "z3805a_tb.clock",        "label": "GCLK",   "kind": "clock"},
        {"path": "z3805a_tb.chip_select_n", "label": "/CS P61", "kind": "bit"},
        {"path": "z3805a_tb.data",          "label": "DATA0-7", "kind": "bus", "base": "hex"}
      ],
      "marks": [{"t": 124000, "label": "CS falls"}]   # optional annotations
    }

The SVG references the page's CSS custom properties with literal fallbacks,
so it adapts to the artifact's light/dark theme when embedded and still
renders standalone.  Its own classes are prefixed `vw-` so they never
collide with the host page.
"""
import json
import re
import sys

# --- VCD parsing ----------------------------------------------------------


class Vcd:
    """A parsed VCD: signal id -> change list, plus name -> id resolution."""

    def __init__(self, path):
        # id -> list of (time, value_string); value is "0"/"1"/"x"/"z" for a
        # scalar or the raw bit-string ("1010") for a vector.
        self.changes = {}
        # id -> width in bits.
        self.width = {}
        # fully-qualified "scope.scope.name" -> id.
        self.by_name = {}
        self.timescale = "1ps"
        self._parse(path)

    def _parse(self, path):
        scope = []
        name_to_id = {}
        in_defs = True
        time = 0
        with open(path) as handle:
            tokens = iter(handle.read().split("\n"))
        for line in tokens:
            line = line.strip()
            if not line:
                continue
            if in_defs:
                if line.startswith("$timescale"):
                    self.timescale = line.split()[1] if len(line.split()) > 1 else "1ps"
                elif line.startswith("$scope"):
                    scope.append(line.split()[2])
                elif line.startswith("$upscope"):
                    if scope:
                        scope.pop()
                elif line.startswith("$var"):
                    parts = line.split()
                    width, ident, base_name = int(parts[2]), parts[3], parts[4]
                    full = ".".join(scope + [base_name])
                    name_to_id[full] = ident
                    self.width.setdefault(ident, width)
                    self.changes.setdefault(ident, [])
                elif line.startswith("$enddefinitions"):
                    in_defs = False
                continue
            # Value-change section.
            if line[0] == "#":
                time = int(line[1:])
            elif line[0] in "01xXzZ":
                self.changes.setdefault(line[1:], []).append((time, line[0].lower()))
            elif line[0] in "bB":
                bits, ident = line[1:].split()
                self.changes.setdefault(ident, []).append((time, bits))
            elif line[0] in "rR":
                pass  # real values: not drawn
        self.by_name = name_to_id

    def resolve(self, path):
        """Find a signal by a possibly-abbreviated hierarchical name."""
        if path in self.by_name:
            return self.by_name[path]
        # Allow dropping the top testbench scope, or a trailing/leading match.
        for full, ident in self.by_name.items():
            if full == path or full.endswith("." + path) or full.split(".")[-1] == path:
                return ident
        raise KeyError(path)

    def value_at(self, ident, t):
        """The signal's value just before or at time t (None if never set)."""
        last = None
        for when, value in self.changes[ident]:
            if when <= t:
                last = value
            else:
                break
        return last

    def window(self, ident, t0, t1):
        """(value_at_t0, [(t, value) for changes in (t0, t1]])."""
        start = self.value_at(ident, t0)
        inside = [(w, v) for (w, v) in self.changes[ident] if t0 < w <= t1]
        return start, inside


# --- drawing --------------------------------------------------------------

# Geometry, all in SVG user units.
LABEL_W = 150      # left label gutter
ROW_H = 40         # one signal's row height
WAVE_H = 22        # high-to-low span within a row
TOP = 54           # space above the first row (title + axis)
PAD_R = 24         # right padding
PLOT_W = 1180      # plot area width
BOT = 40           # space below last row (time axis ticks)

UNIT_PS = {"ps": 1, "ns": 1e3, "us": 1e6, "ms": 1e9}

STYLE = """
  .vw-bg{fill:var(--sheet,#fbfbf8)}
  .vw-lab{font:12.5px 'IBM Plex Sans Condensed',system-ui,sans-serif;fill:var(--fg,#1d2420)}
  .vw-lab.mut{fill:var(--muted,#5b655f)}
  .vw-val{font:11px 'IBM Plex Mono',ui-monospace,monospace;fill:var(--fg,#1d2420)}
  .vw-ttl{font:600 14px 'IBM Plex Sans Condensed',system-ui,sans-serif;fill:var(--fg,#1d2420)}
  .vw-ax{font:10px 'IBM Plex Mono',ui-monospace,monospace;fill:var(--muted,#5b655f)}
  .vw-w{fill:none;stroke:var(--fg,#1d2420);stroke-width:1.6}
  .vw-w.clk{stroke:var(--sim,#2453a8)}
  .vw-w.sig{stroke:var(--signal,#0b6f78)}
  .vw-x{fill:var(--open,#b3261e);opacity:.18}
  .vw-bus{fill:none;stroke:var(--fg,#1d2420);stroke-width:1.6}
  .vw-rail{stroke:var(--rule,#c9cec4);stroke-width:1;stroke-dasharray:1 4}
  .vw-grid{stroke:var(--rule,#c9cec4);stroke-width:1}
  .vw-mark{stroke:var(--inferred,#a35f00);stroke-width:1.2;stroke-dasharray:4 3}
  .vw-markt{font:10px 'IBM Plex Mono',ui-monospace,monospace;fill:var(--inferred,#a35f00)}
"""


def _x_of(t, t0, span):
    return LABEL_W + (t - t0) / span * PLOT_W


def _fmt_bus(bits, width, base):
    """Format a VCD bit-string as hex/bin, carrying x/z through."""
    if any(c in "xz" for c in bits):
        # If wholly unknown, say so; else show the raw bits.
        return "x" if set(bits) <= {"x"} else ("z" if set(bits) <= {"z"} else bits)
    bits = bits.rjust(width, "0")
    if base == "bin":
        return "b" + bits
    value = int(bits, 2)
    return f"{value:0{(width + 3) // 4}x}"


def _draw_bit(out, ident, vcd, t0, t1, span, y, klass):
    """A one-bit signal as a step waveform; x/z as a shaded mid band."""
    hi, lo = y, y + WAVE_H
    start, changes = vcd.window(ident, t0, t1)
    seq = [(t0, start)] + changes
    pts = []
    for i, (when, value) in enumerate(seq):
        end = seq[i + 1][0] if i + 1 < len(seq) else t1
        x0, x1 = _x_of(when, t0, span), _x_of(end, t0, span)
        if value in ("x", "z", None):
            out.append(f'<rect class="vw-x" x="{x0:.1f}" y="{hi:.1f}" '
                       f'width="{max(0.5, x1 - x0):.1f}" height="{WAVE_H}"/>')
            level = (hi + lo) / 2
            pts.append((x0, level))
            pts.append((x1, level))
            continue
        level = hi if value == "1" else lo
        if pts:
            # vertical edge from the previous level to this one
            pts.append((x0, pts[-1][1]))
        pts.append((x0, level))
        pts.append((x1, level))
    if pts:
        d = " ".join(f"{x:.1f},{yy:.1f}" for x, yy in pts)
        out.append(f'<polyline class="vw-w {klass}" points="{d}"/>')


def _draw_bus(out, ident, vcd, t0, t1, span, y, width, base):
    """A multi-bit bus as value cells with crossed transitions."""
    hi, lo, mid = y, y + WAVE_H, y + WAVE_H / 2
    start, changes = vcd.window(ident, t0, t1)
    seq = [(t0, start)] + changes
    for i, (when, value) in enumerate(seq):
        end = seq[i + 1][0] if i + 1 < len(seq) else t1
        x0, x1 = _x_of(when, t0, span), _x_of(end, t0, span)
        xe = min(x1, x0 + 6)  # transition ramp width
        # Cell outline: ramp in, two rails, ramp out.
        out.append(f'<polyline class="vw-bus" points="'
                   f'{x0:.1f},{mid:.1f} {xe:.1f},{hi:.1f} {x1:.1f},{hi:.1f}"/>')
        out.append(f'<polyline class="vw-bus" points="'
                   f'{x0:.1f},{mid:.1f} {xe:.1f},{lo:.1f} {x1:.1f},{lo:.1f}"/>')
        text = _fmt_bus(value, width, base) if value is not None else "x"
        cx = (xe + x1) / 2
        if x1 - xe > 14:
            out.append(f'<text class="vw-val" x="{cx:.1f}" y="{mid + 4:.1f}" '
                       f'text-anchor="middle">{text}</text>')


def render(vcd, spec):
    t0, t1 = spec["window"]
    span = float(t1 - t0)
    signals = spec["signals"]
    height = TOP + ROW_H * len(signals) + BOT
    unit = spec.get("unit", "ns")
    scale = UNIT_PS.get(unit, 1)

    out = [
        f'<svg viewBox="0 0 {LABEL_W + PLOT_W + PAD_R} {height}" '
        f'role="img" xmlns="http://www.w3.org/2000/svg" '
        f'aria-label="{spec.get("title", "waveform")}">',
        f'<style>{STYLE}</style>',
        f'<rect class="vw-bg" x="0" y="0" width="{LABEL_W + PLOT_W + PAD_R}" height="{height}"/>',
        f'<text class="vw-ttl" x="0" y="20">{spec.get("title", "")}</text>',
    ]

    # Time axis: ticks every ~nice step across the window.
    ticks = _nice_ticks(t0, t1, 8)
    for t in ticks:
        x = _x_of(t, t0, span)
        out.append(f'<line class="vw-rail" x1="{x:.1f}" y1="{TOP - 10:.1f}" '
                   f'x2="{x:.1f}" y2="{TOP + ROW_H * len(signals):.1f}"/>')
        out.append(f'<text class="vw-ax" x="{x:.1f}" y="{TOP - 14:.1f}" '
                   f'text-anchor="middle">{(t - t0) / scale:g}</text>')
    out.append(f'<text class="vw-ax" x="{LABEL_W + PLOT_W:.1f}" y="{TOP - 28:.1f}" '
               f'text-anchor="end">time ({unit}) from {t0 / scale:g} {unit}</text>')

    # Rows.
    for i, sig in enumerate(signals):
        y = TOP + ROW_H * i + (ROW_H - WAVE_H) / 2
        out.append(f'<text class="vw-lab" x="0" y="{y + WAVE_H / 2 + 4:.1f}">{sig["label"]}</text>')
        try:
            ident = vcd.resolve(sig["path"])
        except KeyError:
            out.append(f'<text class="vw-lab mut" x="{LABEL_W}" y="{y + WAVE_H / 2 + 4:.1f}">'
                       f'(signal {sig["path"]} not in dump)</text>')
            continue
        kind = sig.get("kind", "bit")
        if kind == "bus" or (kind == "auto" and vcd.width[ident] > 1):
            _draw_bus(out, ident, vcd, t0, t1, span, y, vcd.width[ident], sig.get("base", "hex"))
        else:
            klass = "clk" if kind == "clock" else ("sig" if kind == "sig" else "")
            _draw_bit(out, ident, vcd, t0, t1, span, y, klass)

    # Event marks.
    for mark in spec.get("marks", []):
        x = _x_of(mark["t"], t0, span)
        out.append(f'<line class="vw-mark" x1="{x:.1f}" y1="{TOP - 10:.1f}" '
                   f'x2="{x:.1f}" y2="{TOP + ROW_H * len(signals):.1f}"/>')
        out.append(f'<text class="vw-markt" x="{x + 3:.1f}" y="{TOP + ROW_H * len(signals) + 14:.1f}">'
                   f'{mark["label"]}</text>')

    out.append("</svg>")
    return "\n".join(out)


def _nice_ticks(t0, t1, count):
    """Axis ticks at a 1-2-5 * 10^n step nearest to (window / count)."""
    raw = max(1, (t1 - t0) / count)
    mag = 10 ** (len(str(int(raw))) - 1)
    step = next((m * mag for m in (1, 2, 5, 10) if m * mag >= raw), 10 * mag)
    ticks, t = [], (int(t0) // step) * step
    while t <= t1:
        if t >= t0:
            ticks.append(t)
        t += step
    return ticks


def main(argv):
    if len(argv) != 3:
        sys.exit("usage: vcd_wave.py DUMP.vcd FIGURE.json > figure.svg")
    vcd = Vcd(argv[1])
    with open(argv[2]) as handle:
        spec = json.load(handle)
    sys.stdout.write(render(vcd, spec))
    sys.stdout.write("\n")


if __name__ == "__main__":
    main(sys.argv)
