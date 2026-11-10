"""Renders the board block diagram, the 10 MHz SmartClock main board's
blocks and signals, from docs/block-diagram.json to OUT, after checking
the data: its citations, its parts against the board atlas's, its marks
and that each signal's line starts and ends on its blocks.  `make
block-diagram` writes it among the generated pages, which
.github/workflows/pages.yml publishes on GitHub Pages; the page is not
committed.

    python3 -I tools/block_diagram.py OUT

The page is one file, whole without JavaScript: the diagram, inline SVG
drawn in the page's colors, then every block and every signal with its
mark and its sources, which the diagram's blocks and lines link to.
Its one inline script only adds to that, on a narrow window: a tap on
the diagram opens a copy of it full screen, to pan and zoom, closed by
its button or Escape; a tap on a block or line there closes it and goes
to the entry.  It states no counts.

The data, one JSON object:

    title, heading     the page's <title> and <h1>
    description        the page's one-line description, which the
                       pages' index lists
    intro              the header's paragraphs
    marks              the confidence marks, {id, text}, in legend order
    speculative        what "(?)" means, a paragraph
    size               the diagram's width and height, in its units
    caption            the diagram's caption
    groups             dashed outlines around blocks, {id, title, sub,
                       x, y, w, h, mark, source}
    blocks             each block: id, title, parts (board atlas
                       designators, each linked to its row there), sub
                       (lines under the title), functions ({text, mark},
                       lines inside a large block, the first's baseline
                       at functions_top), x, y, w, h, mark,
                       speculative (true draws it dashed, its title
                       with "(?)"), source
    edges              each signal: id, from, to (block or group ids),
                       points (the line, [x, y] each, the first on the
                       from block's outline, the last on the to block's),
                       label (lines, "\\n" between them), at ([x, y,
                       anchor], the label's first baseline), pins
                       ([text, x, y, anchor] each, drawn inside a block
                       at the line's end), both (an arrowhead at each
                       end), mark, speculative, source
    unlisted           designators the board atlas does not list yet
    notes              paragraphs under the diagram
    left_out           what is not drawn, and why

Prose takes tools/pages_render.py's marks, its {{ref}} a link to a
block's or a signal's entry.
"""

import argparse
import html
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from board_atlas import DATA as ATLAS_DATA  # noqa: E402
from board_atlas import anchor as atlas_anchor  # noqa: E402
from pages_render import BLOB, ROOT, Prose, head, plain  # noqa: E402

DATA = ROOT / "docs" / "block-diagram.json"
# The board atlas on the Pages site, beside this page.
ATLAS_PAGE = "board-atlas.html"
# How far a line's end may sit from its block's outline, in the
# diagram's units.
ON_OUTLINE = 0.5
# A block's text: the title's and a line's baseline steps, and the
# inset of the mark in its corner.
TITLE_STEP = 18
LINE_STEP = 15
INSET = 6
# A label's line step.
LABEL_STEP = 14

CSS = """\
/* The diagram scales to the page's width; the lists under it carry
   everything it shows, as text, for a narrow window. */
:root {
  --bg: #eef0ee; --panel: #ffffff; --ink: #1b2320; --muted: #5a6661; --line: #d3d9d6; --hi: #fff4c2;
  --read: #2f5fd0; --board: #1f7a6c; --simulated: #7a3fb8; --inferred: #a26a00; --open: #c8401f;
  color-scheme: light;
}
@media (prefers-color-scheme: dark) {
  :root:not([data-theme="light"]) {
    --bg: #121715; --panel: #1b2220; --ink: #e4ebe8; --muted: #9aa7a2; --line: #2e3834; --hi: #3a3418;
    --read: #7d9ff0; --board: #4cc3ae; --simulated: #c49af0; --inferred: #e6ac3a; --open: #ff7650;
    color-scheme: dark;
  }
}
:root[data-theme="dark"] {
  --bg: #121715; --panel: #1b2220; --ink: #e4ebe8; --muted: #9aa7a2; --line: #2e3834; --hi: #3a3418;
  --read: #7d9ff0; --board: #4cc3ae; --simulated: #c49af0; --inferred: #e6ac3a; --open: #ff7650;
  color-scheme: dark;
}
* { box-sizing: border-box; }
body { margin: 0; background: var(--bg); color: var(--ink); font: 15px/1.5 "IBM Plex Sans", system-ui, sans-serif; }
.wrap { max-width: 1240px; margin: 0 auto; padding-inline: 16px; padding-block: 24px 48px; display: grid; gap: 24px; }
.wrap > * { min-width: 0; }
header h1 { font-size: 1.6rem; margin: 0; text-wrap: balance; font-weight: 600; }
header p { margin: 6px 0 0; color: var(--muted); max-width: 80ch; }
h2 { font: 600 12px/1.4 "IBM Plex Mono", monospace; letter-spacing: .08em; text-transform: uppercase; margin: 0 0 8px; color: var(--muted); }
a { color: inherit; }
code { font: 13px "IBM Plex Mono", monospace; overflow-wrap: anywhere; }
.card { background: var(--panel); border: 1px solid var(--line); border-radius: 6px; padding: 14px 16px; min-width: 0; }
.card p { margin: 0 0 8px; max-width: 90ch; }
.card p:last-child { margin: 0; }
.card ul { margin: 0; padding-left: 20px; display: grid; gap: 6px; max-width: 90ch; }
.legend { display: flex; flex-wrap: wrap; gap: 6px 18px; font-size: 13px; color: var(--muted); margin: 0 0 8px; padding: 0; list-style: none; }
.legend li { display: flex; align-items: center; gap: 6px; }
.legend svg { flex: none; }
.c-read { --c: var(--read); } .c-board { --c: var(--board); } .c-simulated { --c: var(--simulated); }
.c-inferred { --c: var(--inferred); } .c-open { --c: var(--open); }
.mark { font: 600 11px/1.6 "IBM Plex Mono", monospace; color: var(--c); white-space: nowrap; }
figure { margin: 0; }
.diagram svg { display: block; width: 100%; height: auto; }
figcaption { font-size: 14px; color: var(--muted); margin-top: 8px; max-width: 90ch; }
/* The drawing: a block's outline and a line take their mark's color;
   inferred is dashed, open dotted, and a speculative block dashed. */
.dg-box { fill: var(--panel); stroke: var(--c); stroke-width: 1.6; }
.dg-box.spec, .dg-group { stroke-dasharray: 7 4; }
.dg-group { fill: none; stroke: var(--c); stroke-width: 1.4; }
.dg-line { fill: none; stroke: var(--c); stroke-width: 1.6; }
.dg-line.c-inferred { stroke-dasharray: 6 4; }
.dg-line.c-open { stroke-dasharray: 2 3; }
.dg-head { fill: var(--c); }
.dg-title { font: 600 14px "IBM Plex Sans", system-ui, sans-serif; fill: var(--ink); }
.dg-sub { font: 11.5px "IBM Plex Mono", monospace; fill: var(--muted); }
.dg-fn { font: 12.5px "IBM Plex Sans", system-ui, sans-serif; fill: var(--ink); }
.dg-pin { font: 600 11.5px "IBM Plex Mono", monospace; fill: var(--ink); }
.dg-label { font: 11.5px "IBM Plex Mono", monospace; fill: var(--ink); paint-order: stroke; stroke: var(--bg); stroke-width: 4px; stroke-linejoin: round; }
.dg-mark { font: 600 10.5px "IBM Plex Mono", monospace; fill: var(--c); }
.dg-label .dg-mark { stroke: var(--bg); }
svg a:hover .dg-box, svg a:focus-visible .dg-box { fill: var(--hi); }
svg a:hover .dg-line, svg a:focus-visible .dg-line { stroke-width: 3; }
svg a:focus-visible { outline: none; }
.entries { list-style: none; margin: 0; padding: 0; display: grid; gap: 0; }
.entries li { padding: 8px 0; border-bottom: 1px solid var(--line); scroll-margin-top: 12px; }
.entries li:last-child { border-bottom: 0; }
.entries li:target { background: var(--hi); }
.entries b { font-weight: 600; }
.entries .parts { font: 12.5px "IBM Plex Mono", monospace; color: var(--muted); }
.entries .source { display: block; font-size: 14px; color: var(--muted); }
.lists { display: grid; grid-template-columns: repeat(auto-fit, minmax(min(100%, 460px), 1fr)); gap: 24px; align-items: start; }
footer { color: var(--muted); font-size: 13px; }
/* With the script, on a narrow window: the diagram opens full screen,
   a fixed window onto a stage, the diagram's copy, that the script pans
   and zooms. */
button { font: inherit; font-size: 14px; color: var(--ink); background: var(--panel); border: 1px solid var(--line);
  border-radius: 4px; padding: 5px 12px; cursor: pointer; }
button:focus-visible { outline: 2px solid var(--read); outline-offset: 2px; }
.zoom-open { display: none; margin-top: 8px; }
@media (max-width: 700px) {
  .js .zoom-open { display: inline-block; }
  .js .diagram svg { cursor: zoom-in; }
}
.zoom { position: fixed; inset: 0; z-index: 10; display: flex; flex-direction: column; background: var(--bg); }
.zoom[hidden] { display: none; }
.zoom-bar { display: flex; flex-wrap: wrap; align-items: center; gap: 8px; padding: 8px 16px;
  background: var(--panel); border-bottom: 1px solid var(--line); }
.zoom-bar span { flex: 1 1 8em; font-size: 13px; color: var(--muted); }
.zoom-view { position: relative; flex: 1; overflow: hidden; touch-action: none; cursor: grab; }
.zoom-view.dragging { cursor: grabbing; }
.zoom-stage { position: absolute; left: 0; top: 0; transform-origin: 0 0; }
.zoom-stage svg { display: block; width: 100%; height: auto; }
body.zoomed { overflow: hidden; }
"""

# The zoom's script: it copies the diagram into the full-screen view,
# pans it with one pointer, zooms it with two, the wheel or the buttons,
# and lets a tap, a pointer that barely moved, through to a link.
SCRIPT = """\
(() => {
  "use strict";
  // The widths the zoom opens on, as the style's media query; the
  // largest and smallest zoom, over the diagram fitted to the window's
  // width; how far a pointer may move and still tap; a button's step.
  const NARROW = matchMedia("(max-width: 700px)");
  const MAX_ZOOM = 8, MIN_ZOOM = 0.5, TAP = 8, STEP = 1.5;
  document.documentElement.classList.add("js");
  const figure = document.querySelector(".diagram"), source = figure.querySelector("svg");
  const zoom = document.querySelector(".zoom"), view = zoom.querySelector(".zoom-view");
  const stage = zoom.querySelector(".zoom-stage"), opener = figure.querySelector(".zoom-open");
  const copy = source.cloneNode(true);
  stage.appendChild(copy);
  let x = 0, y = 0, z = 1, returnTo = null, moved = 0;
  const pointers = new Map();
  let last = null;

  function apply() {
    stage.style.transform = `translate(${x}px, ${y}px) scale(${z})`;
  }
  function home() {
    // The diagram at the window's width, then zoomed to twice that.
    stage.style.width = view.clientWidth + "px";
    z = 2;
    x = 0;
    y = 0;
    apply();
  }
  function zoomAt(factor, cx, cy) {
    const next = Math.min(MAX_ZOOM, Math.max(MIN_ZOOM, z * factor));
    x = cx - (cx - x) * next / z;
    y = cy - (cy - y) * next / z;
    z = next;
    apply();
  }
  function open(event) {
    if (!NARROW.matches) return;
    if (event) event.preventDefault();
    returnTo = document.activeElement;
    zoom.hidden = false;
    document.body.classList.add("zoomed");
    home();
    zoom.querySelector("[data-zoom=close]").focus();
  }
  function close() {
    zoom.hidden = true;
    document.body.classList.remove("zoomed");
    pointers.clear();
    if (returnTo && returnTo.focus) returnTo.focus();
  }
  source.addEventListener("click", open);
  opener.addEventListener("click", open);
  zoom.querySelector(".zoom-bar").addEventListener("click", (event) => {
    const button = event.target.closest("button");
    if (!button) return;
    const cx = view.clientWidth / 2, cy = view.clientHeight / 2;
    if (button.dataset.zoom === "close") close();
    else if (button.dataset.zoom === "in") zoomAt(STEP, cx, cy);
    else if (button.dataset.zoom === "out") zoomAt(1 / STEP, cx, cy);
    else if (button.dataset.zoom === "fit") home();
  });
  document.addEventListener("keydown", (event) => {
    if (event.key === "Escape" && !zoom.hidden) close();
  });
  view.addEventListener("pointerdown", (event) => {
    pointers.set(event.pointerId, { x: event.clientX, y: event.clientY });
    if (pointers.size === 1) moved = 0;
    last = null;
    view.classList.add("dragging");
  });
  view.addEventListener("pointermove", (event) => {
    const p = pointers.get(event.pointerId);
    if (!p) return;
    const dx = event.clientX - p.x, dy = event.clientY - p.y;
    p.x = event.clientX;
    p.y = event.clientY;
    moved += Math.abs(dx) + Math.abs(dy);
    if (moved > TAP && !view.hasPointerCapture(event.pointerId)) view.setPointerCapture(event.pointerId);
    if (pointers.size === 1) {
      x += dx;
      y += dy;
      apply();
    } else if (pointers.size === 2) {
      const [a, b] = [...pointers.values()];
      const rect = view.getBoundingClientRect();
      const spread = Math.hypot(a.x - b.x, a.y - b.y);
      const cx = (a.x + b.x) / 2 - rect.left, cy = (a.y + b.y) / 2 - rect.top;
      if (last) zoomAt(spread / last, cx, cy);
      last = spread;
    }
  });
  function release(event) {
    pointers.delete(event.pointerId);
    last = null;
    if (!pointers.size) view.classList.remove("dragging");
  }
  view.addEventListener("pointerup", release);
  view.addEventListener("pointercancel", release);
  view.addEventListener("wheel", (event) => {
    event.preventDefault();
    const rect = view.getBoundingClientRect();
    zoomAt(event.deltaY < 0 ? 1.1 : 1 / 1.1, event.clientX - rect.left, event.clientY - rect.top);
  }, { passive: false });
  copy.addEventListener("click", (event) => {
    // A drag is not a tap; a tap on a block or line closes the view and
    // follows its link to the entry.
    if (moved > TAP) {
      event.preventDefault();
      return;
    }
    if (event.target.closest("a")) close();
  });
  addEventListener("resize", () => {
    if (!zoom.hidden && !NARROW.matches) close();
  });
})();
"""


def die(message):
    sys.exit(f"block_diagram: {message}")


def entry_anchor(ref):
    return "x-" + ref


def outline(box):
    return box["x"], box["y"], box["x"] + box["w"], box["y"] + box["h"]


def on_outline(point, box):
    """Whether a point lies on a box's outline."""
    x, y = point
    left, top, right, bottom = outline(box)
    inside = left - ON_OUTLINE <= x <= right + ON_OUTLINE and top - ON_OUTLINE <= y <= bottom + ON_OUTLINE
    edge = min(abs(x - left), abs(x - right), abs(y - top), abs(y - bottom))
    return inside and edge <= ON_OUTLINE


def check(data):
    """Refuses data the page would render wrongly, and returns every
    entry's id: the blocks', the groups' and the signals'."""
    marks = {m["id"] for m in data["marks"]}
    atlas = {p["ref"] for p in json.loads(ATLAS_DATA.read_text(encoding="utf-8"))["parts"]}
    unlisted = set(data["unlisted"])
    if unlisted & atlas:
        die(f"unlisted, but the board atlas lists {sorted(unlisted & atlas)}")
    boxes = {}
    width, height = data["size"]
    for kind in ("groups", "blocks"):
        for box in data[kind]:
            if box["id"] in boxes:
                die(f"{box['id']} twice")
            boxes[box["id"]] = box
            left, top, right, bottom = outline(box)
            if left < 0 or top < 0 or right > width or bottom > height:
                die(f"{box['id']} outside the diagram")
            if box["mark"] not in marks:
                die(f"{box['id']}: mark {box['mark']}")
            if not box["source"]:
                die(f"{box['id']}: no source")
    used = set()
    for block in data["blocks"]:
        for ref in block["parts"]:
            if ref not in atlas and ref not in unlisted:
                die(f"{block['id']}: {ref} is neither in the board atlas nor unlisted")
            used.add(ref)
        for line in block.get("functions", []):
            if line["mark"] not in marks:
                die(f"{block['id']}: function mark {line['mark']}")
    if unlisted - used:
        die(f"unlisted but drawn nowhere: {sorted(unlisted - used)}")
    ids = set(boxes)
    for edge in data["edges"]:
        if edge["id"] in ids:
            die(f"{edge['id']} twice")
        ids.add(edge["id"])
        if edge["mark"] not in marks:
            die(f"{edge['id']}: mark {edge['mark']}")
        if not edge["source"]:
            die(f"{edge['id']}: no source")
        for end, point in (("from", edge["points"][0]), ("to", edge["points"][-1])):
            if edge[end] not in boxes:
                die(f"{edge['id']}: {end} {edge[end]}")
            if not on_outline(point, boxes[edge[end]]):
                die(f"{edge['id']}: its {end} end {point} is not on {edge[end]}'s outline")
    return ids, boxes


def text(x, y, klass, content, anchor="start"):
    return (f'<text class="{klass}" x="{x}" y="{y}" text-anchor="{anchor}">'
            f"{html.escape(content)}</text>")


def titled(box):
    return box["title"] + (" (?)" if box.get("speculative") else "")


def marker_id(mark):
    return f"dg-head-{mark}"


def markers(marks):
    out = ["<defs>"]
    for mark in marks:
        out.append(f'<marker id="{marker_id(mark)}" class="c-{mark}" viewBox="0 0 10 10" refX="9" refY="5" '
                   'markerWidth="7" markerHeight="7" orient="auto-start-reverse">'
                   '<path class="dg-head" d="M0,0 L10,5 L0,10 z"/></marker>')
    out.append("</defs>")
    return out


def draw_group(group):
    x, y = group["x"], group["y"]
    return [f'<a href="#{entry_anchor(group["id"])}" aria-label="{html.escape(plain(group["title"]))}">',
            f'<rect class="dg-group c-{group["mark"]}" x="{x}" y="{y}" width="{group["w"]}" height="{group["h"]}" rx="6"/>',
            text(x + 2 * INSET + 4, y + TITLE_STEP + 2, "dg-title", titled(group)),
            text(x + 2 * INSET + 4, y + TITLE_STEP + 2 + LINE_STEP, "dg-sub", group["sub"]),
            "</a>"]


def draw_block(block):
    x, y, w, h = block["x"], block["y"], block["w"], block["h"]
    spec = " spec" if block.get("speculative") else ""
    lines = block.get("sub", [])
    functions = block.get("functions", [])
    out = [f'<a href="#{entry_anchor(block["id"])}" aria-label="{html.escape(titled(block))}">',
           f'<rect class="dg-box c-{block["mark"]}{spec}" x="{x}" y="{y}" width="{w}" height="{h}" rx="4"/>']
    if functions:
        # A large block: its title and lines at the top, its functions
        # from functions_top down, its mark in the corner.
        top = y + TITLE_STEP + INSET
        step = TITLE_STEP + 4
        for i, line in enumerate(functions):
            out.append(f'<text class="dg-fn" x="{x + w / 2}" y="{block["functions_top"] + step * i}" '
                       f'text-anchor="middle">{html.escape(line["text"])} '
                       f'<tspan class="dg-mark c-{line["mark"]}">{line["mark"]}</tspan></text>')
        out.append(text(x + w - INSET, y + h - INSET, f"dg-mark c-{block['mark']}", block["mark"], "end"))
    else:
        # A small block: its title, its lines and its mark centered as one.
        top = y + (h - TITLE_STEP - LINE_STEP * (len(lines) + 1)) / 2 + TITLE_STEP - 4
        out.append(text(x + w / 2, top + LINE_STEP * (len(lines) + 1), f"dg-mark c-{block['mark']}",
                        block["mark"], "middle"))
    out.append(text(x + w / 2, top, "dg-title", titled(block), "middle"))
    for i, line in enumerate(lines):
        out.append(text(x + w / 2, top + LINE_STEP * (i + 1), "dg-sub", line, "middle"))
    out.append("</a>")
    return out


def draw_edge(edge):
    mark = edge["mark"]
    points = " ".join(f"{x},{y}" for x, y in edge["points"])
    heads = f' marker-end="url(#{marker_id(mark)})"'
    if edge.get("both"):
        heads += f' marker-start="url(#{marker_id(mark)})"'
    out = [f'<a href="#{entry_anchor(edge["id"])}" aria-label="{html.escape(plain(edge_name(edge) or edge["from"] + " to " + edge["to"]))}">',
           f'<polyline class="dg-line c-{mark}" points="{points}"{heads}/>']
    if edge.get("label"):
        x, y, anchor = edge["at"]
        lines = edge["label"].split("\n")
        for i, line in enumerate(lines):
            tail = ""
            if i == len(lines) - 1:
                spec = " (?)" if edge.get("speculative") else ""
                tail = f'{html.escape(spec)} <tspan class="dg-mark c-{mark}">{mark}</tspan>'
            out.append(f'<text class="dg-label" x="{x}" y="{y + LABEL_STEP * i}" text-anchor="{anchor}">'
                       f"{html.escape(line)}{tail}</text>")
    for pin, x, y, anchor in edge.get("pins", []):
        out.append(text(x, y, "dg-pin", pin, anchor))
    out.append("</a>")
    return out


def edge_name(edge):
    return edge["label"].replace("\n", " ") if edge.get("label") else ", ".join(p[0] for p in edge.get("pins", []))


def legend_line(mark):
    dash = {"inferred": ' stroke-dasharray="6 4"', "open": ' stroke-dasharray="2 3"'}.get(mark, "")
    return (f'<svg width="34" height="10" viewBox="0 0 34 10" aria-hidden="true">'
            f'<line x1="1" y1="5" x2="33" y2="5" stroke="var(--c)" stroke-width="2"{dash}/></svg>')


def diagram(data):
    width, height = data["size"]
    out = [f'<svg viewBox="0 0 {width} {height}" role="img" xmlns="http://www.w3.org/2000/svg" '
           f'aria-label="{html.escape(data["heading"])}">']
    out += markers(m["id"] for m in data["marks"])
    for group in data["groups"]:
        out += draw_group(group)
    # Lines after blocks: their heads, labels and pin numbers on top.
    for block in data["blocks"]:
        out += draw_block(block)
    for edge in data["edges"]:
        out += draw_edge(edge)
    out.append("</svg>")
    return "\n".join(out)


def mark(kind):
    return f'<span class="mark c-{kind}">{html.escape(kind)}</span>'


def atlas_link(ref):
    return f'<a href="{ATLAS_PAGE}#{atlas_anchor(ref)}">{html.escape(ref)}</a>'


def render(data):
    ids, boxes = check(data)
    r = Prose(ids, entry_anchor, die)
    unlisted = set(data["unlisted"])
    out = head(data["title"], data["description"], "hardware",
               "tools/block_diagram.py from docs/block-diagram.json by make block-diagram", CSS)
    emit = out.append
    emit("<body>")
    emit('<div class="wrap">')

    emit("<header>")
    emit(f"  <h1>{html.escape(data['heading'])}</h1>")
    for paragraph in data["intro"]:
        emit(f"  <p>{r(paragraph)}</p>")
    emit("</header>")

    emit('<section class="card">')
    emit("  <h2>The marks</h2>")
    emit('  <ul class="legend" aria-label="Marks">')
    for m in data["marks"]:
        emit(f'    <li class="c-{m["id"]}">{legend_line(m["id"])}<span>{mark(m["id"])} {r(m["text"])}</span></li>')
    emit("  </ul>")
    emit(f"  <p>{r(data['speculative'])}</p>")
    emit("</section>")

    emit('<figure class="card diagram">')
    emit(diagram(data))
    emit(f"<figcaption>{r(data['caption'])}</figcaption>")
    emit('<button class="zoom-open" type="button">Open full screen</button>')
    emit("</figure>")

    emit('<div class="lists">')
    emit('<section class="card">')
    emit("  <h2>Blocks</h2>")
    emit('  <ul class="entries">')
    for box in data["groups"] + data["blocks"]:
        parts = ", ".join(html.escape(ref) + " (?)" if ref in unlisted else atlas_link(ref)
                          for ref in box.get("parts", []))
        emit(f'    <li id="{entry_anchor(box["id"])}"><b>{html.escape(titled(box))}</b> {mark(box["mark"])}'
             + (f' <span class="parts">{parts}</span>' if parts else "")
             + f'<span class="source">{r(box["source"])}</span></li>')
    emit("  </ul>")
    emit("</section>")
    emit('<section class="card">')
    emit("  <h2>Signals</h2>")
    emit('  <ul class="entries">')
    for edge in data["edges"]:
        arrow = " ↔ " if edge.get("both") else " → "
        ends = titled(boxes[edge["from"]]) + arrow + titled(boxes[edge["to"]])
        # A line with no label or pin is named by its ends alone.
        name = edge_name(edge)
        spec = " (?)" if edge.get("speculative") else ""
        emit(f'    <li id="{entry_anchor(edge["id"])}"><b>{html.escape((name or ends) + spec)}</b> {mark(edge["mark"])}'
             + (f' <span class="parts">{html.escape(ends)}</span>' if name else "")
             + f'<span class="source">{r(edge["source"])}</span></li>')
    emit("  </ul>")
    emit("</section>")
    emit("</div>")

    for title, key in (("Notes", "notes"), ("Left out", "left_out")):
        emit('<section class="card">')
        emit(f"  <h2>{title}</h2>")
        emit("  <ul>")
        for item in data[key]:
            emit(f"    <li>{r(item)}</li>")
        emit("  </ul>")
        emit("</section>")

    emit(f'<footer>Generated by <a href="{BLOB}tools/block_diagram.py"><code>tools/block_diagram.py</code></a> from '
         f'<a href="{BLOB}docs/block-diagram.json"><code>docs/block-diagram.json</code></a>.</footer>')
    emit("</div>")
    emit(f'<div class="zoom" role="dialog" aria-modal="true" aria-label="{html.escape(data["heading"])}" hidden>')
    emit('  <div class="zoom-bar"><span>Drag to pan, pinch to zoom.</span>'
         '<button type="button" data-zoom="out" aria-label="Zoom out">−</button>'
         '<button type="button" data-zoom="in" aria-label="Zoom in">+</button>'
         '<button type="button" data-zoom="fit">Reset</button>'
         '<button type="button" data-zoom="close">Close</button></div>')
    emit('  <div class="zoom-view"><div class="zoom-stage"></div></div>')
    emit("</div>")
    emit("<script>")
    emit(SCRIPT.rstrip("\n"))
    emit("</script>")
    emit("</body>")
    emit("</html>")
    return "\n".join(out) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("out", type=Path, help="the page to write")
    args = parser.parse_args()
    data = json.loads(DATA.read_text(encoding="utf-8"))
    args.out.write_text(render(data), encoding="utf-8")


if __name__ == "__main__":
    main()
