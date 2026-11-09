"""Writes the published pages' landing page, DIR/index.html: what the
project is, the hero picture, the FEATURED page if one is given under
the heading FEATURED_HEADING, then
each other PAGE given (a file in DIR) by its <title> and its
description meta tag under its smartclock-group meta tag's group, in
GROUPS' order and within one in the order given, and the repository's
main documents.  Fails if a page lacks any of the three tags or names a
group GROUPS lacks, or if FEATURED is not one of the PAGEs.

The hero is a drawing of the HP 58503A's front panel, showing UTC, or
HERO, an image under docs/ copied to the same path beside the index,
described by HERO_ALT.

    python3 -I tools/pages_index.py [--featured PAGE --featured-heading TEXT]
        [--hero PATH --hero-alt TEXT] DIR PAGE...
"""

import argparse
import html
import json
import shutil
import sys
from dataclasses import dataclass
from html.parser import HTMLParser
from pathlib import Path

INDEX = "index.html"
DOCS = Path(__file__).resolve().parent.parent / "docs"
REPOSITORY = "https://github.com/charlieh0tel/smartclock"
BLOB = f"{REPOSITORY}/blob/main"
TREE = f"{REPOSITORY}/tree/main"


@dataclass(frozen=True)
class Group:
    """One of the groups the landing page lists pages under."""

    # Its heading on the page.
    heading: str
    # One line saying what its pages are about.
    blurb: str
    # Its motif, an SVG drawing's inner elements on MOTIF_BOX, in strokes.
    motif: str


# The box each group's motif is drawn on, width by height.
MOTIF_BOX = (72, 48)

# The groups a page names in its smartclock-group meta tag, by key, in
# the landing page's order.  A group with no page is left out.
GROUPS = {
    "hardware": Group(
        "Hardware",
        "The boards and their power supplies part by part on their photographs, and what the bench measured.",
        # A board: mounting holes, a chip with its pins, a can oscillator.
        '<rect x="3" y="5" width="66" height="38" rx="2"/>'
        '<circle cx="8" cy="10" r="1.6"/><circle cx="64" cy="38" r="1.6"/>'
        '<rect x="14" y="14" width="20" height="20" rx="1"/>'
        '<path d="M18 14v-3M23 14v-3M28 14v-3M18 34v3M23 34v3M28 34v3"/>'
        '<rect x="44" y="12" width="16" height="16" rx="3"/>'
        '<path d="M42 36h8M54 36h8"/>',
    ),
    "firmware": Group(
        "Firmware and the loop",
        "How the firmware measures the 1 PPS and steers the oscillator to GPS.",
        # A loop: the measurement and the filter, the filter's output fed back.
        '<rect x="4" y="8" width="22" height="14" rx="1"/>'
        '<rect x="44" y="8" width="24" height="14" rx="1"/>'
        '<path d="M26 15h14M37 12l3 3-3 3"/>'
        '<path d="M56 22v16H15v-12M12 29l3-3 3 3"/>',
    ),
    "scpi": Group(
        "SCPI",
        "The command trees each firmware image's parser knows, against the manuals.",
        # A command tree: a root, its subsystems, their commands.
        '<circle cx="8" cy="24" r="3"/>'
        '<path d="M11 24h8M19 10v28M19 10h10M19 24h10M19 38h10"/>'
        '<circle cx="32" cy="10" r="3"/><circle cx="32" cy="24" r="3"/><circle cx="32" cy="38" r="3"/>'
        '<path d="M35 10h8M43 4v12M43 4h14M43 16h14M35 38h22"/>',
    ),
    "fpga": Group(
        "FPGAs",
        "The receivers' FPGA designs, decoded from the bitstreams in their images.",
        # An FPGA: logic blocks on a grid, a route between two of them.
        '<rect x="6" y="6" width="12" height="12"/><rect x="30" y="6" width="12" height="12"/>'
        '<rect x="54" y="6" width="12" height="12"/><rect x="6" y="30" width="12" height="12"/>'
        '<rect x="30" y="30" width="12" height="12"/><rect x="54" y="30" width="12" height="12"/>'
        '<path d="M18 12h12M42 36h12M36 18v12M24 12v24h6"/>',
    ),
}

# The repository's documents the landing page points to, each by its
# link, its name and what it holds.
DOCUMENTS = (
    (f"{REPOSITORY}#readme", "README", "What the project is, its programs, and how to run them."),
    (f"{TREE}/docs/firmware", "Firmware notes", "The firmware images read: the loop, the 1 PPS, GPS, pForth, the rewrite."),
    (f"{BLOB}/docs/running.md", "Running it", "Installing the packages, the daemon's settings, remote clients."),
    (f"{BLOB}/PLAN.md", "Plan", "The design, the decisions behind it, open questions and known defects."),
)

# The hero's drawing: the HP 58503A's front panel, in hundredths of an
# inch, from photographs with a rule laid along it.  Its maker's logo
# and the stickers a unit carries are left out.
PANEL_SIZE = (837, 347)
# The display window's left, top, width and height, and the vacuum
# fluorescent display's character cells in it: the first cell's left
# edge and top, the pitch between cells, and how many there are.
WINDOW = (13, 7, 632, 149)
CELL_ORIGIN = (141, 74)
CELL_PITCH = 34
CELLS = 14
# The text the display shows until the page's script sets the time.
DISPLAY_TEXT = "UTC 12:00:00"
# Each of a cell's 14 segments by name, as a line from one point to
# another within the cell, 22 wide and 43 high.
SEGMENTS = {
    "a": (2, 0, 20, 0), "b": (22, 2, 22, 19.5), "c": (22, 23.5, 22, 41),
    "d": (2, 43, 20, 43), "e": (0, 23.5, 0, 41), "f": (0, 2, 0, 19.5),
    "g1": (2, 21.5, 9.5, 21.5), "g2": (12.5, 21.5, 20, 21.5),
    "h": (2.5, 3, 9, 18), "i": (11, 2, 11, 19.5), "j": (19.5, 3, 13, 18),
    "k": (9, 25, 2.5, 40), "l": (11, 23.5, 11, 41), "m": (13, 25, 19.5, 40),
}
# The segments each character the display shows lights.
GLYPHS = {
    " ": "", "0": "a b c d e f", "1": "b c", "2": "a b g1 g2 e d",
    "3": "a b g2 c d", "4": "f g1 g2 b c", "5": "a f g1 g2 c d",
    "6": "a f e d c g1 g2", "7": "a b c", "8": "a b c d e f g1 g2",
    "9": "a b c d f g1 g2", "U": "f e d c b", "T": "a i l", "C": "a f e d",
}
# The colon after a cell, its two dots' offsets from the cell's origin.
COLON = ((26, 13), (26, 30))
# A lamp's radius, and a key's width, height and corner radius.
LAMP_RADIUS = 4
KEY_SIZE = (52, 34, 2.5)
# The STATUS and MODE groups: each one's label, its outline's left,
# top, right and bottom.
PANEL_GROUPS = (("STATUS", (48, 170, 634, 214)), ("MODE", (48, 224, 634, 333)))
# The STATUS row's lamps: each one's label, its x and y, and whether it
# is lit in the drawing.
STATUS_LAMPS = (
    ("Power", 129, 199, True), ("GPS Lock", 213, 199, True),
    ("Holdover", 298, 199, False), ("Alarm", 380, 199, False),
)
# The MODE keys: each one's label lines, its left and top, its kind
# (setting the key's color), and its lamp's x and whether it is lit.
MODE_KEYS = (
    (("Time",), 147, 244, "dark", 131, True),
    (("Long",), 230, 244, "gray", 215, False),
    (("Lat",), 313, 244, "gray", 298, False),
    (("Alt",), 395, 244, "gray", 380, False),
    (("Sat",), 147, 293, "light", 133, False),
    (("Status",), 230, 293, "light", 215, False),
    (("Serial", "Port"), 313, 293, "light", 298, False),
    (("Shift",), 395, 293, "blue", 380, False),
)


def display_cells(text):
    """TEXT as the display's cells: each character but a colon, and
    whether a colon follows it, lighting its cell's colon."""
    return [(char, text[index + 1:index + 2] == ":")
            for index, char in enumerate(text) if char != ":"]


def segment_cell(index, char, colon):
    """One display cell: its 14 segments, those CHAR lights lit, and its
    colon, lit if COLON."""
    x = CELL_ORIGIN[0] + index * CELL_PITCH
    y = CELL_ORIGIN[1]
    lit = set(GLYPHS[char].split())
    parts = [f'<path data-segment="{name}" class="{"on" if name in lit else "off"}" '
             f'd="M{x + x0:g} {y + y0:g}L{x + x1:g} {y + y1:g}"/>'
             for name, (x0, y0, x1, y1) in SEGMENTS.items()]
    parts += [f'<circle data-segment="colon" class="{"on" if colon else "off"}" '
              f'cx="{x + dx:g}" cy="{y + dy:g}" r="1.8"/>' for dx, dy in COLON]
    return f'<g data-cell="{index}">' + "".join(parts) + "</g>"


def lamp(x, y, lit):
    return f'<circle class="lamp{" lit" if lit else ""}" cx="{x}" cy="{y}" r="{LAMP_RADIUS}"/>'


def panel_group(label, box):
    left, top, right, bottom = box
    middle = (left + right) / 2
    gap = len(label) * 8 + 16
    return (f'<rect class="outline" x="{left}" y="{top}" width="{right - left}" height="{bottom - top}" rx="3"/>'
            f'<path class="band" d="M{left + 2} {top + 1}H{middle - gap / 2:g}M{middle + gap / 2:g} {top + 1}H{right - 2}"/>'
            f'<text class="legend" x="{middle:g}" y="{top + 5}" text-anchor="middle">{label}</text>')


def mode_key(lines, x, y, kind, lamp_x, lit):
    width, height, radius = KEY_SIZE
    text = "".join(f'<text class="key-label {kind}" x="{x + 7}" y="{y + 14 + 11 * row}">{line}</text>'
                   for row, line in enumerate(lines))
    return (lamp(lamp_x, y + height / 2 - 2, lit)
            + f'<rect class="key-shadow" x="{x + 1.5}" y="{y + 2}" width="{width}" height="{height}" rx="{radius}"/>'
            + f'<rect class="key {kind}" x="{x}" y="{y}" width="{width}" height="{height}" rx="{radius}"/>'
            + text)


def panel():
    width, height = PANEL_SIZE
    wx, wy, ww, wh = WINDOW
    shown = display_cells(DISPLAY_TEXT)
    shown += [(" ", False)] * (CELLS - len(shown))
    cells = "".join(segment_cell(index, char, colon) for index, (char, colon) in enumerate(shown))
    status = "".join(lamp(x, y, lit) + f'<text class="lamp-label" x="{x + 10}" y="{y + 3.5}">{label}</text>'
                     for label, x, y, lit in STATUS_LAMPS)
    keys = "".join(mode_key(*key) for key in MODE_KEYS)
    return f"""\
<svg class="panel" viewBox="0 0 {width} {height}" role="img" aria-labelledby="panel-title">
<title id="panel-title">An HP 58503A GPS time and frequency reference receiver's front panel, showing UTC, its Power, GPS Lock and Time lamps lit</title>
<rect class="bezel" width="{width}" height="{height}" rx="7"/>
<rect class="face" x="5" y="4" width="{width - 10}" height="{height - 8}" rx="4"/>
<rect class="window" x="{wx}" y="{wy}" width="{ww}" height="{wh}" rx="3"/>
<text class="maker" x="26" y="27" textLength="83" lengthAdjust="spacingAndGlyphs">HEWLETT</text>
<text class="maker" x="26" y="42" textLength="83" lengthAdjust="spacingAndGlyphs">PACKARD</text>
<text class="model" x="180" y="27" textLength="88" lengthAdjust="spacingAndGlyphs">58503A</text>
<text class="name" x="180" y="42" textLength="341" lengthAdjust="spacingAndGlyphs">GPS TIME AND FREQUENCY REFERENCE RECEIVER</text>
<g class="vfd" aria-hidden="true">{cells}</g>
{"".join(panel_group(label, box) for label, box in PANEL_GROUPS)}
{status}
{keys}
<text class="clear-alarm" x="454" y="256">Clear</text><text class="clear-alarm" x="454" y="268">Alarm</text>
</svg>"""


# The intro and description leave the FPGAs out until the FPGA
# write-up's page joins; that page's launch puts "their FPGAs decoded"
# back in the intro's list.
PAGE = """\
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>SmartClock</title>
<meta name="description" content="HP, Agilent and Symmetricom SmartClock GPS receivers taken apart, their firmware, disciplining loop and boards documented, and tools to monitor them: the pages built from the smartclock repository.">
<!-- Generated by tools/pages_index.py by make pages. -->
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Archivo:wdth,wght@62..125,500..800&family=IBM+Plex+Sans:wght@400;500;600&display=swap">
<style>
:root{
  --bg:#e1e2de; --ink:#1d2023; --muted:#53595f; --rule:#8f9397;
  --blue:#285a88;
  color-scheme:light;
}
@media (prefers-color-scheme: dark){
  :root:not([data-theme="light"]){
    --bg:#181a1c; --ink:#e3e4e0; --muted:#a2a7ac; --rule:#5c6166;
    --blue:#89b6df;
    color-scheme:dark;
  }
}
:root[data-theme="dark"]{
  --bg:#181a1c; --ink:#e3e4e0; --muted:#a2a7ac; --rule:#5c6166;
  --blue:#89b6df;
  color-scheme:dark;
}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--ink);
  font:1rem/1.55 "IBM Plex Sans",system-ui,sans-serif}
a{color:var(--blue)}
a:focus-visible{outline:2px solid var(--blue);outline-offset:3px;border-radius:2px}
.wrap{max-width:68rem;margin:0 auto;padding:0 1rem}
.display{font-family:Archivo,"Arial Narrow",system-ui,sans-serif;font-stretch:72%}

header.top{display:flex;justify-content:space-between;align-items:baseline;gap:1rem;
  padding-top:1.25rem;font-size:.95rem}
header.top .site{font-weight:600;color:var(--ink);text-decoration:none}

.hero{padding:2rem 0 .5rem;display:grid;grid-template-columns:minmax(0,1.75fr) minmax(0,1fr);
  grid-template-areas:"name name" "picture intro";column-gap:2.75rem;align-items:start}
.hero h1{grid-area:name;font-weight:800;font-size:clamp(3.25rem,11vw,8.5rem);line-height:.88;
  letter-spacing:-.01em;margin:0 0 1.5rem}
.hero figure{grid-area:picture;margin:0}
.hero figure img{display:block;width:100%;height:auto;border-radius:6px}
.intro{grid-area:intro;max-width:40rem;font-size:1.0625rem}
.intro p{margin:0 0 .9rem}
.intro p+p{color:var(--muted);font-size:.975rem}

/* The 58503A's front panel: the instrument's own colors in either
   theme, a little dimmer in the dark one. */
.panel{display:block;width:100%;height:auto;filter:drop-shadow(0 .4rem .9rem rgba(0,0,0,.18))}
.panel text{font-family:Archivo,"Helvetica Neue",Arial,sans-serif;font-stretch:100%}
.panel .bezel{fill:#e6e3db}
.panel .face{fill:#d9d6cd}
.panel .window{fill:#07090a}
.panel .maker{fill:#f4f4f2;font-weight:800;font-size:15px;font-stretch:125%}
.panel .model{fill:#f4f4f2;font-weight:800;font-size:17px;font-stretch:125%}
.panel .name{fill:#f4f4f2;font-weight:700;font-size:11px;font-stretch:115%}
.panel .vfd path{stroke-width:3;stroke-linecap:round}
.panel .vfd .on{stroke:#2fd8c8;fill:#2fd8c8}
.panel .vfd .off{stroke:#2fd8c8;fill:#2fd8c8;opacity:.045}
.panel .vfd circle{stroke:none}
.panel .outline{fill:none;stroke:#c9c6bd;stroke-width:1.2}
.panel .band{stroke:#bdbab2;stroke-width:6}
.panel .legend{fill:#4b4c4c;font-size:12px;letter-spacing:.06em}
.panel .lamp{fill:#3b3c3b}
.panel .lamp.lit{fill:#e4f53a;filter:drop-shadow(0 0 2.5px #e8ff5a)}
.panel .lamp-label{fill:#55585a;font-size:10.5px;letter-spacing:.05em}
.panel .key-shadow{fill:#8f8c86}
.panel .key{stroke:#a9a69f;stroke-width:.8}
.panel .key.dark{fill:#3a3b3c;stroke:#2a2b2c}
.panel .key.gray{fill:#c8c6c0}
.panel .key.light{fill:#e6e3dc}
.panel .key.blue{fill:#4f8fd3;stroke:#3d77b6}
.panel .key-label{fill:#2b2c2d;font-size:11px;font-weight:700}
.panel .key-label.dark,.panel .key-label.blue{fill:#f6f6f4}
.panel .clear-alarm{fill:#5d8fc8;font-size:10px;letter-spacing:.05em}
@media (prefers-color-scheme: dark){:root:not([data-theme="light"]) .panel{filter:brightness(.86)}}
:root[data-theme="dark"] .panel{filter:brightness(.86)}

/* A group of pages, outlined as a panel's controls are, its name set
   into the outline's top edge. */
.group{position:relative;border:1.5px solid var(--rule);border-radius:4px;
  margin:2.75rem 0 0;padding:1.75rem 1.5rem 1.25rem;
  display:grid;grid-template-columns:15rem 1fr;gap:1.5rem 2.5rem}
.group>h2{position:absolute;top:0;left:1.1rem;transform:translateY(-55%);margin:0;
  padding:0 .5rem;background:var(--bg);font-weight:700;font-size:1.45rem;line-height:1}
.group .about{color:var(--muted);font-size:.95rem}
.group .about svg{display:block;width:72px;height:48px;margin:0 0 .6rem;color:var(--blue)}
.group .about svg *{fill:none;stroke:currentColor;stroke-width:1.5;stroke-linejoin:round}
.group .about p{margin:0}
.group ul{list-style:none;margin:0;padding:0}
.group li+li{border-top:1px solid color-mix(in srgb,var(--rule) 45%,transparent)}
.group li a{display:block;padding:.7rem 0;text-decoration:none;color:var(--ink)}
.group li a:first-child{padding-top:0}
.group li:not(:first-child) a{padding-top:.7rem}
.group .title{display:block;font-weight:600;color:var(--blue);font-size:1.075rem}
.group li a:hover .title{text-decoration:underline;text-underline-offset:3px}
.group .line{display:block;color:var(--muted);max-width:40rem}

.featured{border-width:2.5px;border-color:var(--ink);grid-template-columns:auto 1fr;align-items:center}
.featured .about svg{width:144px;height:96px;margin:0}
.featured .title{font-size:1.6rem;line-height:1.15;margin-bottom:.35rem}
.featured li a{padding:0}

.documents{grid-template-columns:repeat(auto-fit,minmax(14rem,1fr));gap:1.25rem 2rem}
.documents a{text-decoration:none;color:var(--muted)}

footer.wrap{color:var(--muted);font-size:.875rem;padding-top:2.5rem;padding-bottom:2rem}

@media (max-width:60rem){
  .hero{grid-template-columns:1fr;grid-template-areas:"name" "picture" "intro"}
  .hero figure{margin:0 0 1.5rem}
}
@media (max-width:46rem){
  .group{grid-template-columns:1fr;padding:1.5rem 1rem 1rem}
  .group .about{display:flex;gap:1rem;align-items:center}
  .group .about svg{flex:none;margin:0}
  .featured .about svg{width:96px;height:64px}
  .featured .title{font-size:1.3rem}
}
</style>
</head>
<body>
<header class="top wrap">
<a class="site" href="./">SmartClock</a>
<a href="__REPOSITORY__">Repository on GitHub</a>
</header>
<main class="wrap">
<section class="hero">
<h1 class="display">SmartClock</h1>
<figure>
__HERO__
</figure>
<div class="intro">
<p>The HP, Agilent and Symmetricom SmartClock GPS time and frequency receivers, the 58503A/B, 59551A, Z3801A, Z3805A, Z3815A and Z3816A, taken apart: their firmware read and rewritten as C, their disciplining loop analyzed, their boards mapped from photographs.  A logging daemon, a terminal monitor and a browser view watch them.</p>
<p>These pages are the parts best read in a browser.  The rest of the documentation is Markdown in the <a href="__REPOSITORY__">repository</a>.</p>
</div>
</section>
__SECTIONS__
<section class="group documents" aria-labelledby="documents">
<h2 class="display" id="documents">In the repository</h2>
__DOCUMENTS__
</section>
</main>
<footer class="wrap">Built by <code>make pages</code> from the sources in <a href="__REPOSITORY__">smartclock</a>.</footer>
<script>
// Shows the time in UTC on the drawn display, as the receiver does,
// ticking each second unless the reader asks for reduced motion.
(() => {
  const glyphs = __GLYPHS__;
  const cells = [...document.querySelectorAll(".vfd [data-cell]")];
  if (!cells.length) return;
  const show = (text) => {
    const shown = [...text].flatMap((char, i) => char === ":" ? [] : [[char, text[i + 1] === ":"]]);
    cells.forEach((cell, i) => {
      const [char, colon] = shown[i] || [" ", false];
      const lit = new Set((glyphs[char] || "").split(" "));
      for (const segment of cell.children) {
        const name = segment.dataset.segment;
        segment.setAttribute("class", (name === "colon" ? colon : lit.has(name)) ? "on" : "off");
      }
    });
  };
  const tick = () => {
    const now = new Date();
    show("UTC " + now.toISOString().slice(11, 19));
    if (!matchMedia("(prefers-reduced-motion: reduce)").matches)
      setTimeout(tick, 1000 - now.getMilliseconds());
  };
  tick();
})();
</script>
</body>
</html>
"""


class Head(HTMLParser):
    """A page's <title> text and its description and smartclock-group
    meta tags' content."""

    def __init__(self):
        super().__init__()
        self.title = None
        self.description = None
        self.group = None
        self.in_title = False

    def handle_starttag(self, tag, attrs):
        attrs = dict(attrs)
        if tag == "title":
            self.in_title = True
            self.title = ""
        elif tag == "meta" and attrs.get("name") == "description":
            self.description = attrs.get("content")
        elif tag == "meta" and attrs.get("name") == "smartclock-group":
            self.group = attrs.get("content")

    def handle_endtag(self, tag):
        if tag == "title":
            self.in_title = False

    def handle_data(self, data):
        if self.in_title:
            self.title += data


def head(directory, name):
    page = Head()
    page.feed((directory / name).read_text(encoding="utf-8"))
    if not page.title or not page.description or not page.group:
        sys.exit(f"pages_index: {name} lacks a <title>, a description or a smartclock-group meta tag")
    if page.group not in GROUPS:
        sys.exit(f"pages_index: {name} names group {page.group!r}, not one of {', '.join(GROUPS)}")
    return page


def motif(group):
    width, height = MOTIF_BOX
    return f'<svg viewBox="0 0 {width} {height}" aria-hidden="true">{group.motif}</svg>'


def entry(name, page):
    return (f'<li><a href="{html.escape(name)}"><span class="title">{html.escape(page.title.strip())}</span>'
            f'<span class="line">{html.escape(page.description)}</span></a></li>')


def section(key, heading, about, entries, classes="group"):
    return (f'<section class="{classes}" aria-labelledby="group-{key}">\n'
            f'<h2 class="display" id="group-{key}">{html.escape(heading)}</h2>\n'
            f'<div class="about">{about}</div>\n'
            f"<ul>\n" + "\n".join(entries) + "\n</ul>\n</section>")


def featured(name, page, heading):
    return section("featured", heading, motif(GROUPS[page.group]), [entry(name, page)], "group featured")


def grouped(heads, skip):
    sections = []
    for key, group in GROUPS.items():
        entries = [entry(name, page) for name, page in heads.items() if page.group == key and name != skip]
        if entries:
            about = f"{motif(group)}<p>{html.escape(group.blurb)}</p>"
            sections.append(section(key, group.heading, about, entries))
    return sections


def documents():
    return "\n".join(f'<a href="{html.escape(link)}"><span class="title">{html.escape(name)}</span>'
                     f'<span class="line">{html.escape(what)}</span></a>'
                     for link, name, what in DOCUMENTS)


def hero(directory, path, alt):
    if not path:
        return panel()
    if not alt:
        sys.exit("pages_index: --hero needs --hero-alt")
    copy = directory / path
    copy.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(DOCS / path, copy)
    return f'<img src="{html.escape(path)}" alt="{html.escape(alt)}">'


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--featured", help="the page to feature, by file name; one of the pages")
    parser.add_argument("--featured-heading", default="Featured", help="the featured page's heading")
    parser.add_argument("--hero", help="the hero image, by its path under docs/")
    parser.add_argument("--hero-alt", help="the hero image's description")
    parser.add_argument("directory", type=Path, help="the pages' directory")
    parser.add_argument("pages", nargs="+", help="the pages, by file name")
    args = parser.parse_args()
    heads = {name: head(args.directory, name) for name in args.pages}
    if args.featured and args.featured not in heads:
        sys.exit(f"pages_index: the featured page {args.featured} is not one of the pages")
    sections = grouped(heads, args.featured)
    if args.featured:
        sections.insert(0, featured(args.featured, heads[args.featured], args.featured_heading))
    page = (PAGE.replace("__HERO__", hero(args.directory, args.hero, args.hero_alt))
            .replace("__SECTIONS__", "\n".join(sections))
            .replace("__DOCUMENTS__", documents())
            .replace("__GLYPHS__", json.dumps(GLYPHS))
            .replace("__REPOSITORY__", REPOSITORY))
    (args.directory / INDEX).write_text(page, encoding="utf-8")


if __name__ == "__main__":
    main()
