"""Renders docs/board-atlas.html, the first-family SmartClock main
board's atlas, from docs/board-atlas.json, and checks the page against
it, or with --write rewrites the page.

    python3 -I tools/board_atlas.py [--write]

The page is plain HTML and CSS, without JavaScript, so that it reads the
same through https://htmlpreview.github.io/.  It states no counts.

The data, one JSON object:

    title, heading     the page's <title> and <h1>
    intro, tags        the header's paragraphs and tags
    confidence         the confidence marks, {id, text}, in legend order
    comparison         the Z3801A and 58503A columns' marks, likewise
    views              the photos, {id, title, short (the parts table's
                       name for it), image (relative to docs/), width,
                       height (pixels), alt, caption}
    groups             the parts table's sections, {id, title}, in order
    parts              each part: ref, group, marking, type, role,
                       firmware, note; id_confidence (the designator and
                       marking) and role_confidence, each a confidence
                       id; z3801a and 58503a, each {status, note};
                       at, {view id: {x, y}}, the ring's center in
                       percent of the photo's width and height
    nets_intro, nets   the measured nets, {net, path, note}
    design_note        paragraphs after the nets
    observations       paragraphs: across the three boards
    limits             paragraphs: what the photos cannot show
    open               the open items

Prose takes three marks: `code`; [[path]] or [[path|section]], a
citation of a file in the repository, linked on GitHub without a
fragment (htmlpreview points a fragment at the page itself), whose path
must exist and whose section must be a heading of that Markdown file,
or "item N" for hardware-investigations.md's numbered items; and
{{ref}}, a link to a part's row.
"""

import argparse
import difflib
import html
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DATA = ROOT / "docs" / "board-atlas.json"
PAGE = ROOT / "docs" / "board-atlas.html"
BLOB = "https://github.com/charlieh0tel/smartclock/blob/main/"
RAW = "https://raw.githubusercontent.com/charlieh0tel/smartclock/main/docs/"
CITE = re.compile(r"\[\[([^|\]]+)(?:\|([^\]]+))?\]\]")
CODE = re.compile(r"`([^`]+)`")
REF = re.compile(r"\{\{([^}]+)\}\}")
ITEM = re.compile(r"item (\d+)$")
COMPARED = ("z3801a", "58503a")
COMPARED_TITLES = {"z3801a": "Z3801A", "58503a": "58503A"}

CSS = """\
/* Layout: the photos, each with its ringed parts, then the measured
   nets, the parts table grouped by subsystem and the notes.  Tables sit
   in their own scroll boxes so the page never widens. */
:root {
  --bg: #eef0ee; --panel: #ffffff; --ink: #1b2320; --muted: #5a6661; --line: #d3d9d6; --hi: #fff4c2;
  --board: #1f7a6c; --read: #2f5fd0; --inferred: #a26a00; --open: #c8401f;
  --same: #1f7a6c; --fitted: #2f5fd0; --differs: #a26a00;
  color-scheme: light;
}
@media (prefers-color-scheme: dark) {
  :root:not([data-theme="light"]) {
    --bg: #121715; --panel: #1b2220; --ink: #e4ebe8; --muted: #9aa7a2; --line: #2e3834; --hi: #3a3418;
    --board: #4cc3ae; --read: #7d9ff0; --inferred: #e6ac3a; --open: #ff7650;
    --same: #4cc3ae; --fitted: #7d9ff0; --differs: #e6ac3a;
    color-scheme: dark;
  }
}
:root[data-theme="dark"] {
  --bg: #121715; --panel: #1b2220; --ink: #e4ebe8; --muted: #9aa7a2; --line: #2e3834; --hi: #3a3418;
  --board: #4cc3ae; --read: #7d9ff0; --inferred: #e6ac3a; --open: #ff7650;
  --same: #4cc3ae; --fitted: #7d9ff0; --differs: #e6ac3a;
  color-scheme: dark;
}
* { box-sizing: border-box; }
body { margin: 0; background: var(--bg); color: var(--ink); font: 15px/1.5 "IBM Plex Sans", system-ui, sans-serif; }
.wrap { max-width: 1240px; margin: 0 auto; padding-inline: 16px; padding-block: 24px 48px; display: grid; gap: 24px; }
header h1 { font-size: 1.6rem; margin: 0; text-wrap: balance; font-weight: 600; }
header p { margin: 6px 0 0; color: var(--muted); max-width: 80ch; }
h2 { font: 600 12px/1.4 "IBM Plex Mono", monospace; letter-spacing: .08em; text-transform: uppercase; margin: 0 0 8px; color: var(--muted); }
a { color: inherit; }
code { font: 13px "IBM Plex Mono", monospace; }
.tags { display: flex; flex-wrap: wrap; gap: 8px; margin-top: 10px; }
.tag { font: 600 11px/1 "IBM Plex Mono", monospace; letter-spacing: .06em; text-transform: uppercase; padding: 6px 8px; border: 1px solid var(--line); border-radius: 4px; background: var(--panel); }
.legend { display: flex; flex-wrap: wrap; gap: 6px 16px; font-size: 13px; color: var(--muted); margin: 0; padding: 0; list-style: none; }
.legend li { display: flex; align-items: center; gap: 6px; }
.views { display: grid; grid-template-columns: repeat(auto-fit, minmax(min(100%, 440px), 1fr)); gap: 20px; align-items: start; }
.views figure.wide { grid-column: 1 / -1; }
figure { margin: 0; display: grid; gap: 8px; min-width: 0; }
figcaption { font-size: 14px; color: var(--muted); }
.photo { position: relative; width: 100%; border-radius: 6px; overflow: hidden; border: 1px solid var(--line);
  background-size: 100% 100%; background-repeat: no-repeat; }
.pin { position: absolute; width: 0; height: 0; text-decoration: none; }
.ring { position: absolute; left: -8px; top: -8px; width: 16px; height: 16px; border-radius: 50%;
  border: 3px solid var(--c); box-shadow: 0 0 0 1px rgba(0, 0, 0, .7), inset 0 0 0 1px rgba(0, 0, 0, .7); }
.lbl { position: absolute; left: 10px; top: 0; transform: translateY(-50%); white-space: nowrap;
  font: 600 11px/1 "IBM Plex Mono", monospace; color: #fff; background: rgba(0, 0, 0, .62); padding: 2px 3px; border-radius: 3px; }
.pin:hover .lbl, .pin:focus-visible .lbl { background: #000; outline: 2px solid var(--c); }
@media (max-width: 600px) { .lbl { font-size: 9px; padding: 1px 2px; } .ring { left: -6px; top: -6px; width: 12px; height: 12px; border-width: 2px; } .lbl { left: 7px; } }
.dot { display: inline-block; width: 12px; height: 12px; border-radius: 50%; border: 3px solid var(--c); flex: none; }
.c-board { --c: var(--board); } .c-read { --c: var(--read); } .c-inferred { --c: var(--inferred); } .c-open { --c: var(--open); }
.c-same { --c: var(--same); } .c-fitted { --c: var(--fitted); } .c-differs { --c: var(--differs); }
.mark { font: 600 11px/1.6 "IBM Plex Mono", monospace; color: var(--c); white-space: nowrap; }
.card { background: var(--panel); border: 1px solid var(--line); border-radius: 6px; padding: 14px 16px; min-width: 0; }
.card p { margin: 0 0 8px; max-width: 90ch; }
.card p:last-child { margin: 0; }
.card ul { margin: 0; padding-left: 20px; display: grid; gap: 6px; max-width: 90ch; }
.notes { display: grid; grid-template-columns: repeat(auto-fit, minmax(min(100%, 340px), 1fr)); gap: 16px; }
.scroll { overflow-x: auto; }
table { border-collapse: collapse; font-size: 14px; width: 100%; }
th, td { text-align: left; vertical-align: top; padding: 5px 12px 5px 0; border-bottom: 1px solid var(--line); }
th { color: var(--muted); font-weight: 500; }
tbody th.group { font: 600 12px/1.4 "IBM Plex Mono", monospace; letter-spacing: .08em; text-transform: uppercase; color: var(--ink); padding-top: 18px; }
td.ref { white-space: nowrap; font: 600 13px "IBM Plex Mono", monospace; }
td.ref small { display: block; font: 400 12px "IBM Plex Sans", system-ui, sans-serif; color: var(--muted); white-space: normal; }
td.marking { font: 12.5px "IBM Plex Mono", monospace; }
/* Wide enough to read on a phone, inside its scroll box; narrow enough
   for a 1000 px window. */
.parts { font-size: 13px; min-width: 880px; }
.parts th, .parts td { padding-right: 8px; }
.parts code { overflow-wrap: anywhere; }
td small, .note { color: var(--muted); font-size: 13px; }
tr[id] { scroll-margin-top: 12px; }
tr:target { background: var(--hi); }
footer { color: var(--muted); font-size: 13px; }
"""


def die(message):
    sys.exit(f"board_atlas: {message}")


def headings(path):
    """The heading texts of a Markdown file, and its numbered items."""
    found = set()
    for line in path.read_text(encoding="utf-8").splitlines():
        match = re.match(r"#+ (.*)", line)
        if match:
            text = match.group(1).strip()
            found.add(text)
            item = re.match(r"(\d+)\. ", text)
            if item:
                found.add(f"item {item.group(1)}")
    return found


class Renderer:
    """Renders the data's prose marks, checking each citation and part
    reference as it goes."""

    def __init__(self, refs):
        self.refs = refs
        self.headings = {}

    def code(self, text):
        return CODE.sub(lambda m: f"<code>{m.group(1)}</code>", html.escape(text, quote=False))

    def cite(self, path, section):
        target = ROOT / path
        if not target.is_file():
            die(f"cites {path}, which does not exist")
        shown = re.sub(r"^(docs|third_party)/", "", path)
        link = f'<a href="{BLOB}{html.escape(path)}"><code>{html.escape(shown)}</code></a>'
        if not section:
            return link
        if path not in self.headings:
            self.headings[path] = headings(target)
        if section not in self.headings[path]:
            die(f"cites {path}, {section!r}, which is not one of its headings")
        if ITEM.match(section):
            return f"{link}, {self.code(section)}"
        return f"{link}, “{self.code(section)}”"

    def ref(self, ref):
        if ref not in self.refs:
            die(f"refers to {ref}, which is not a part")
        return f'<a href="#{anchor(ref)}">{html.escape(ref)}</a>'

    def __call__(self, text):
        out = []
        last = 0
        for match in CITE.finditer(text):
            out.append(self.plain(text[last:match.start()]))
            out.append(self.cite(match.group(1), match.group(2)))
            last = match.end()
        out.append(self.plain(text[last:]))
        return "".join(out)

    def plain(self, text):
        out = []
        last = 0
        for match in REF.finditer(text):
            out.append(self.code(text[last:match.start()]))
            out.append(self.ref(match.group(1)))
            last = match.end()
        out.append(self.code(text[last:]))
        return "".join(out)


def anchor(ref):
    return "part-" + re.sub(r"[^A-Za-z0-9]+", "-", ref).strip("-")


def mark(kind, text):
    return f'<span class="mark c-{kind}">{html.escape(text)}</span>'


def check(data):
    """Refuses data the page would render wrongly."""
    confidence = {c["id"] for c in data["confidence"]}
    comparison = {c["id"] for c in data["comparison"]}
    groups = {g["id"] for g in data["groups"]}
    views = {v["id"] for v in data["views"]}
    seen = set()
    for view in data["views"]:
        if not (ROOT / "docs" / view["image"]).is_file():
            die(f"view {view['id']}: no {view['image']}")
    for part in data["parts"]:
        ref = part["ref"]
        if ref in seen:
            die(f"{ref} twice")
        seen.add(ref)
        if part["group"] not in groups:
            die(f"{ref}: group {part['group']}")
        for key in ("id_confidence", "role_confidence"):
            if part[key] not in confidence:
                die(f"{ref}: {key} {part[key]}")
        for model in COMPARED:
            if part[model]["status"] not in comparison:
                die(f"{ref}: {model} {part[model]['status']}")
        for view, point in part["at"].items():
            if view not in views:
                die(f"{ref}: view {view}")
            if not (0 <= point["x"] <= 100 and 0 <= point["y"] <= 100):
                die(f"{ref}: {view} at {point}")
    return seen


def legend(entries, label):
    items = "".join(
        f'<li><span class="dot c-{e["id"]}"></span><span><b>{html.escape(e["id"])}</b>: {html.escape(e["text"])}</span></li>'
        for e in entries)
    return f'<ul class="legend" aria-label="{html.escape(label)}">{items}</ul>'


def render_view(view, parts, groups):
    pins = []
    for part in parts:
        point = part["at"].get(view["id"])
        if point is None:
            continue
        title = f"{part['ref']}: {part['type']}; {groups[part['group']]}"
        title = CITE.sub(lambda m: m.group(1).rsplit("/", 1)[-1], title).replace("`", "")
        pins.append(
            f'<a class="pin c-{part["id_confidence"]}" style="left:{point["x"]}%;top:{point["y"]}%" '
            f'href="#{anchor(part["ref"])}" title="{html.escape(title)}">'
            f'<span class="ring"></span><span class="lbl">{html.escape(part["ref"])}</span></a>')
    image = html.escape(view["image"])
    wide = ' class="wide"' if view["width"] > view["height"] else ""
    style = (f"aspect-ratio:{view['width']}/{view['height']};"
             f"background-image:url('{image}'),url('{RAW}{image}')")
    return (f'<figure id="view-{view["id"]}"{wide}>\n'
            f'  <h2>{html.escape(view["title"])}</h2>\n'
            f'  <div class="photo" role="img" aria-label="{html.escape(view["alt"])}" style="{style}">\n    '
            + "\n    ".join(pins)
            + f'\n  </div>\n  <figcaption>{html.escape(view["caption"])}  '
            f'<a href="{BLOB}docs/{image}">Full size</a>.</figcaption>\n</figure>')


def render(data):
    refs = check(data)
    r = Renderer(refs)
    groups = {g["id"]: g["title"] for g in data["groups"]}
    views = {v["id"]: v["short"] for v in data["views"]}
    parts = data["parts"]
    out = []
    emit = out.append

    emit("<!doctype html>")
    emit('<html lang="en">')
    emit("<head>")
    emit('<meta charset="utf-8">')
    emit('<meta name="viewport" content="width=device-width, initial-scale=1">')
    emit(f"<title>{html.escape(data['title'])}</title>")
    emit("<!-- Generated by tools/board_atlas.py from docs/board-atlas.json; do not edit.\n"
         "     make board-atlas rewrites it; make check-board-atlas fails when they disagree. -->")
    emit('<link rel="preconnect" href="https://fonts.googleapis.com">')
    emit('<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=IBM+Plex+Mono:wght@400;600&family=IBM+Plex+Sans:wght@400;500;600&display=swap">')
    emit("<style>")
    emit(CSS.rstrip("\n"))
    emit("</style>")
    emit("</head>")
    emit("<body>")
    emit('<div class="wrap">')

    emit("<header>")
    emit(f"  <h1>{html.escape(data['heading'])}</h1>")
    for paragraph in data["intro"]:
        emit(f"  <p>{r(paragraph)}</p>")
    emit('  <div class="tags">' + "".join(f'<span class="tag">{html.escape(t)}</span>' for t in data["tags"]) + "</div>")
    emit("</header>")

    emit('<section class="card">')
    emit("  <h2>The marks</h2>")
    emit("  <p>A ring's color is how its part is identified, its designator and marking; the table gives that and the role's apart.</p>")
    emit("  " + legend(data["confidence"], "Confidence"))
    emit("</section>")

    emit('<section class="views">')
    for view in data["views"]:
        emit(render_view(view, parts, groups))
    emit("</section>")

    emit('<section class="card">')
    emit("  <h2>Measured nets</h2>")
    emit(f"  <p>{r(data['nets_intro'])}</p>")
    emit('  <div class="scroll"><table>')
    emit("    <thead><tr><th>Net</th><th>Path</th><th>Note</th></tr></thead>")
    emit("    <tbody>")
    for net in data["nets"]:
        emit(f"      <tr><td>{r(net['net'])}</td><td>{r(net['path'])}</td><td>{r(net['note'])}</td></tr>")
    emit("    </tbody>")
    emit("  </table></div>")
    for paragraph in data["design_note"]:
        emit(f"  <p>{r(paragraph)}</p>")
    emit("</section>")

    emit('<section class="card">')
    emit("  <h2>Parts</h2>")
    emit("  <p>Confidence is the designator and marking's, then the role's.  The last two columns compare the Z3801A's and the 58503A's boards at the same designator:</p>")
    emit("  " + legend(data["comparison"], "Comparison"))
    emit('  <div class="scroll"><table class="parts">')
    emit("    <thead><tr><th>Ref</th><th>Marking</th><th>Type</th><th>Role</th><th>Firmware</th>"
         "<th>Confidence</th>" + "".join(f"<th>{COMPARED_TITLES[m]}</th>" for m in COMPARED) + "</tr></thead>")
    columns = 6 + len(COMPARED)
    for group in data["groups"]:
        members = [p for p in parts if p["group"] == group["id"]]
        emit("    <tbody>")
        emit(f'      <tr><th class="group" colspan="{columns}" scope="rowgroup">{html.escape(group["title"])}</th></tr>')
        for part in sorted(members, key=lambda p: natural(p["ref"])):
            on = ", ".join(views[v] for v in views if v in part["at"]) or "not ringed"
            role = r(part["role"])
            if part["note"]:
                role += f' <span class="note">{r(part["note"])}</span>'
            compared = "".join(
                f"<td>{mark(part[m]['status'], part[m]['status'])}"
                + (f"<br><small>{r(part[m]['note'])}</small>" if part[m]["note"] else "") + "</td>"
                for m in COMPARED)
            emit(f'      <tr id="{anchor(part["ref"])}"><td class="ref">{html.escape(part["ref"])}<small>{html.escape(on)}</small></td>'
                 f'<td class="marking">{r(part["marking"])}</td><td class="type">{r(part["type"])}</td>'
                 f'<td class="role">{role}</td><td class="fw">{r(part["firmware"])}</td>'
                 f'<td>{mark(part["id_confidence"], "id " + part["id_confidence"])}<br>'
                 f'{mark(part["role_confidence"], "role " + part["role_confidence"])}</td>{compared}</tr>')
        emit("    </tbody>")
    emit("  </table></div>")
    emit("</section>")

    emit('<div class="notes">')
    for title, key in (("Across the three boards", "observations"), ("What the photos cannot show", "limits")):
        emit('  <section class="card">')
        emit(f"    <h2>{title}</h2>")
        for paragraph in data[key]:
            emit(f"    <p>{r(paragraph)}</p>")
        emit("  </section>")
    emit("</div>")

    emit('<section class="card">')
    emit("  <h2>Open</h2>")
    emit("  <ul>")
    for item in data["open"]:
        emit(f"    <li>{r(item)}</li>")
    emit("  </ul>")
    emit("</section>")

    emit(f'<footer>Generated by <a href="{BLOB}tools/board_atlas.py"><code>tools/board_atlas.py</code></a> from '
         f'<a href="{BLOB}docs/board-atlas.json"><code>docs/board-atlas.json</code></a>.</footer>')
    emit("</div>")
    emit("</body>")
    emit("</html>")
    return "\n".join(out) + "\n"


def natural(ref):
    return [int(t) if t.isdigit() else t for t in re.split(r"(\d+)", ref)]


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--write", action="store_true", help="rewrite docs/board-atlas.html")
    args = parser.parse_args()
    page = render(json.loads(DATA.read_text(encoding="utf-8")))
    if args.write:
        PAGE.write_text(page, encoding="utf-8")
        return
    current = PAGE.read_text(encoding="utf-8") if PAGE.exists() else ""
    if current != page:
        sys.stdout.writelines(difflib.unified_diff(
            current.splitlines(keepends=True), page.splitlines(keepends=True),
            str(PAGE.relative_to(ROOT)), "rendered from " + str(DATA.relative_to(ROOT))))
        die("docs/board-atlas.html differs from docs/board-atlas.json; make board-atlas rewrites it")


if __name__ == "__main__":
    main()
