"""What the FPGA pages share: the checked references they take their
figures from, the designs and their traces, the page's styles and
shell, and the drawing primitives of their diagrams.

explainer.py (fpga.html) and detail.py (fpga-detail.html) import it.
A figure a page states is read from the file that is checked against a
fresh run: a design's size and clock loads from
tools/fpga/ref/<image>.stats (`make -f tools/fpga/Makefile check`), a
simulated figure from tools/fpga/ref/<image>.<run>.readings
(`check-readings`), the decoder's pin from tools/fpga/decoder.mk.  A
pattern that no longer matches its file is an error, not a blank.
"""

import html
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
HERE = ROOT / "tools" / "fpga"
REF = HERE / "ref"
BLOB = "https://github.com/charlieh0tel/smartclock/blob/main/"

# The testbenches' GCLK period, ns: a divide is a printed period over it.
GCLK_NS = 100


class Design:
    """One FPGA design: one bitstream, and the firmware images that load it."""

    def __init__(self, image, name, images, tap, pps):
        # Its image directory name, tools/fpga/<image>_tb.v's.
        self.image = image
        # Its heading.
        self.name = name
        # The firmware images that load it.
        self.images = images
        # The net whose rise is the count's stop tap (each trace's
        # "Start, stop and status bits").
        self.tap = tap
        # The pin that carries the internal 1 PPS, whose period gives
        # the divide.
        self.pps = pps


DESIGNS = (
    Design("z3805a", "Z3805A", "Z3805A 3543B", "HG.Y", "P83"),
    Design("58503a-z3801a", "58503A / Z3801A", "58503A 3633, 3704; Z3801A 3543", "HG.Y", "P83"),
    Design("58503b", "58503B", "58503B 1.01.04", "EH.Y", "P83"),
    Design("z3815a", "Z3815A", "Z3815A 4010", "KI.X", "P83"),
    Design("z3816a", "Z3816A", "Z3816A 4001", "KI.X", "P83"),
)

# Each design's trace, the document its figures and roles come from.
TRACES = {
    "z3805a": "docs/firmware/z3805a-trace.md",
    "58503a-z3801a": "docs/firmware/58503a-z3801a-trace.md",
    "58503b": "docs/firmware/58503b-trace.md",
    "z3815a": "docs/firmware/z3815a-trace.md",
    "z3816a": "docs/firmware/z3816a-trace.md",
}


def die(message):
    sys.exit(f"{Path(sys.argv[0]).stem}: {message}")


def readings(run):
    path = REF / f"{run}.readings"
    if not path.exists():
        die(f"no {path.relative_to(ROOT)}")
    return path.read_text(encoding="utf-8")


def find(run, pattern):
    """The first match of pattern in a run's readings, its groups."""
    match = re.search(pattern, readings(run), re.M)
    if not match:
        die(f"{run}.readings has no line matching {pattern!r}")
    return match.groups()


def find_all(run, pattern):
    found = re.findall(pattern, readings(run), re.M)
    if not found:
        die(f"{run}.readings has no line matching {pattern!r}")
    return found


def stats(image):
    """A design's .stats, each key's first value; clock_loads under its own key."""
    path = REF / f"{image}.stats"
    table = {"clock_loads": {}}
    for line in path.read_text(encoding="utf-8").splitlines():
        key, _, value = line.partition(" ")
        if key == "clock_loads":
            net, _, loads = value.partition(" ")
            table["clock_loads"][net] = int(loads)
        else:
            table.setdefault(key, value)
    return table


def decoder_commit():
    match = re.search(r"^DECODER_COMMIT \?= ([0-9a-f]{40})$",
                      (HERE / "decoder.mk").read_text(encoding="utf-8"), re.M)
    if not match:
        die("tools/fpga/decoder.mk names no DECODER_COMMIT")
    return match.group(1)


def ns(text):
    """A printed duration in ns, as an int where it is whole."""
    value = float(text)
    return int(value) if value.is_integer() else value


def num(value):
    return f"{value:,}"


def probe(run, net):
    """A probe line's figures: rises, period, high and after-reference ranges."""
    pattern = (rf"^probe {re.escape(net)}: (\d+) rises, \d+ falls; period ([-\d.]+) to ([-\d.]+) ns; "
               rf"high ([-\d.]+) to ([-\d.]+) ns; after reference ([-\d.]+) to ([-\d.]+) ns")
    rises, *ranges = find(run, pattern)
    keys = ("period_min", "period_max", "high_min", "high_max", "after_min", "after_max")
    return dict(rises=int(rises), **{k: ns(v) for k, v in zip(keys, ranges)})


def edge_periods(run, pin):
    """Every distinct interval a pin's +edges lines print, sorted."""
    found = find_all(run, rf"^[\d.]+ s: {re.escape(pin)} rises, ([\d.]+) ns after the last")
    return sorted({ns(v) for v in found})


def divide(design):
    """The 1 PPS divide: the internal 1 PPS pin's one period in the
    three run, and that period over GCLK_NS."""
    periods = edge_periods(f"{design.image}.three", design.pps)
    if len(periods) != 1:
        die(f"{design.image}.three: {design.pps} periods {periods}, not one")
    return periods[0], periods[0] // GCLK_NS


# --- rendering ------------------------------------------------------------

STYLE = """
:root{
  --bg:#f6f5f1; --sheet:#fbfbf8; --fg:#1d2420; --muted:#5b655f; --rule:#c9cec4;
  --panel:#ffffff; --accent:#1f6f8b; --sim:#2453a8; --signal:#0b6f78;
  --inferred:#a35f00; --open:#b3261e; --read:#3d6b2f;
  --measure:44rem;
}
@media (prefers-color-scheme: dark){
  :root:not([data-theme="light"]){
    color-scheme:dark;
    --bg:#15181c; --sheet:#1a1e22; --fg:#e8e6e0; --muted:#a3a9b0; --rule:#3a4148;
    --panel:#1c2025; --accent:#5fb3d0; --sim:#8fb0ff; --signal:#5cc8d0;
    --inferred:#e0a05a; --open:#f08a80; --read:#9cc98a;
  }
}
:root[data-theme="dark"]{
  color-scheme:dark;
  --bg:#15181c; --sheet:#1a1e22; --fg:#e8e6e0; --muted:#a3a9b0; --rule:#3a4148;
  --panel:#1c2025; --accent:#5fb3d0; --sim:#8fb0ff; --signal:#5cc8d0;
  --inferred:#e0a05a; --open:#f08a80; --read:#9cc98a;
}
*,*::before,*::after{box-sizing:border-box}
body{background:var(--bg);color:var(--fg);font-family:"IBM Plex Sans",system-ui,sans-serif;font-size:16px;line-height:1.55;margin:0;padding:2rem 16px 4rem}
main{max-width:1240px;margin:0 auto;display:grid;gap:3.25rem}
main>*{min-width:0}
header>*,section>*,figure{margin:0}
header>*+*,section>*+*{margin-top:1rem}
section>h3{margin-top:2.25rem}
h1{font-size:2rem;font-weight:600;line-height:1.15;text-wrap:balance}
h2{font-size:1.375rem;font-weight:600;line-height:1.25;text-wrap:balance}
h3{font-size:1rem;font-weight:600}
p,ul,ol,.lede,.note{max-width:var(--measure)}
ul,ol{padding-left:1.25rem}
li+li{margin-top:.375rem}
.eyebrow{font-size:.75rem;letter-spacing:.08em;text-transform:uppercase;color:var(--muted)}
header>.eyebrow+h1{margin-top:.5rem}
.lede{color:var(--muted);font-size:1.0625rem}
.note{font-size:.875rem;color:var(--muted)}
code,.mono{font-family:"IBM Plex Mono",ui-monospace,monospace;font-size:.92em;overflow-wrap:anywhere}
pre{font-family:"IBM Plex Mono",ui-monospace,monospace;font-size:.875rem;background:var(--panel);border-left:3px solid var(--accent);padding:.75rem 1rem;overflow-x:auto;max-width:100%}
a{color:var(--accent)}
nav ol{columns:2 16rem;column-gap:2rem;font-size:.9375rem}
.mark{font-weight:600}
.mark.read{color:var(--read)}
.mark.sim{color:var(--sim)}
.mark.inferred{color:var(--inferred)}
.mark.open{color:var(--open)}
.fig{border:1px solid var(--rule);background:var(--sheet);padding:12px;overflow-x:auto}
.fig svg{display:block;min-width:760px;width:100%;height:auto}
.fig figcaption{font-size:.875rem;color:var(--muted);margin-top:.75rem;max-width:var(--measure)}
.wide{overflow-x:auto}
table{border-collapse:collapse;width:100%;font-size:.9375rem;font-variant-numeric:tabular-nums}
.designs{min-width:44rem}
th,td{text-align:left;vertical-align:top;padding:.5rem .75rem;border-bottom:1px solid var(--rule)}
th:first-child,td:first-child{padding-left:0}
th:last-child,td:last-child{padding-right:0}
thead th{font-weight:600;font-size:.8125rem;letter-spacing:.04em;text-transform:uppercase;color:var(--muted);vertical-align:bottom;border-bottom-color:var(--muted)}
tbody th{font-weight:600}
tbody th,td.short{white-space:nowrap}
.dg-box{fill:var(--panel);stroke:var(--fg);stroke-width:1.4}
.dg-box.inferred{stroke:var(--inferred);stroke-dasharray:5 4}
.dg-t{font:13px "IBM Plex Sans",system-ui,sans-serif;fill:var(--fg)}
.dg-s{font:11.5px "IBM Plex Mono",ui-monospace,monospace;fill:var(--muted)}
.dg-h{font:600 13px "IBM Plex Sans",system-ui,sans-serif;fill:var(--muted);letter-spacing:.06em}
.dg-a{fill:none;stroke:var(--fg);stroke-width:1.4;marker-end:url(#dg-arrow)}
.dg-a.inferred{stroke:var(--inferred);stroke-dasharray:5 4;marker-end:url(#dg-arrow-inferred)}
.dg-head{fill:var(--fg)}
.dg-head.inferred{fill:var(--inferred)}
footer{font-size:.875rem;color:var(--muted);border-top:1px solid var(--rule);padding-top:1rem}
@media (max-width:720px){
  body{padding-top:1.5rem}
  h1{font-size:1.75rem}
}
"""

MARKS = {"read": "read", "simulated": "sim", "inferred": "inferred", "open": "open"}


def mark(word):
    return f'<span class="mark {MARKS[word]}">{word}</span>'


def link(path, text=None):
    return f'<a href="{BLOB}{path}"><code>{html.escape(text or path)}</code></a>'


def trace(image):
    return link(TRACES[image], Path(TRACES[image]).name)


def markers():
    """The arrowheads the diagrams' lines end in, plain and inferred."""
    return "\n".join((
        '<defs>',
        '<marker id="dg-arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" '
        'orient="auto-start-reverse"><path class="dg-head" d="M0,0 L10,5 L0,10 z"/></marker>',
        '<marker id="dg-arrow-inferred" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" '
        'markerHeight="7" orient="auto-start-reverse"><path class="dg-head inferred" d="M0,0 L10,5 L0,10 z"/></marker>',
        '</defs>'))


def box(x, y, w, h, title, sub="", inferred=False):
    klass = "dg-box inferred" if inferred else "dg-box"
    out = [f'<rect class="{klass}" x="{x}" y="{y}" width="{w}" height="{h}" rx="4"/>',
           f'<text class="dg-t" x="{x + w / 2}" y="{y + (h / 2 - 3 if sub else h / 2 + 5)}" '
           f'text-anchor="middle">{html.escape(title)}</text>']
    if sub:
        out.append(f'<text class="dg-s" x="{x + w / 2}" y="{y + h / 2 + 14}" '
                   f'text-anchor="middle">{html.escape(sub)}</text>')
    return out


def arrow(points, inferred=False):
    klass = "dg-a inferred" if inferred else "dg-a"
    d = " ".join(f"{x},{y}" for x, y in points)
    return f'<polyline class="{klass}" points="{d}"/>'


def document(title, description, style, generator, target, body):
    """A whole page: its head with the three tags the Pages site's landing
    page lists it by (tools/pages_index.py), and its content column."""
    return f"""<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>{html.escape(title)}</title>
<meta name="description" content="{html.escape(description)}">
<meta name="smartclock-group" content="fpga">
<style>{style}</style>
</head>
<body>
<!-- Generated by tools/fpga/{generator}; make {target} builds it. -->
<main>
{body}
</main>
</body>
</html>
"""
