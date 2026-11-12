"""Renders the coverage by model, how far the project has read each
firmware image, a row per image and a column per kind of work, from
docs/coverage.json to OUT, after checking every cell against the
repository: a Yes must have what its column names behind it, and a cell
whose status says nothing is there must have none of it, so the table
goes stale in neither direction without failing.  `make coverage`
writes it among the generated pages, which .github/workflows/pages.yml
publishes on GitHub Pages; the page is not committed.  PAGE is each of
the site's pages, which a link to one in the data must name.

    python3 -I tools/coverage.py OUT PAGE...

The data, one JSON object:

    title, heading     the page's <title> and <h1>
    description        the page's one-line description, which the
                       pages' index lists
    intro              the paragraphs under the heading
    no_image           what a row for a model with no image reads
                       across every column
    statuses           each status a cell can take, by key: text (its
                       label, or "" for the column's text), class
                       (its look: yes, partial, no or plain) and
                       evidence, whether its column's evidence must be
                       present, absent or either (any)
    legend             [status, meaning] pairs, in order
    columns            each column: key, heading, about (what it
                       records, prose), text (a Yes cell's label in
                       place of its status's, a template), evidence
                       (the checks a Yes rests on, templates) and link
                       (where a Yes cell links, a template)
    images             each row: image (its file's stem), name, model,
                       design (its FPGA bitstream's stem), design_name,
                       dialect, rewrite (its rewritten C's document,
                       for a Yes there), and cells, each column's
                       status by key, a status's key or {status,
                       note, text, evidence, link}, the last three in
                       place of the column's, Yes where it names none; or, for a model of
                       which no image is in IMAGES, no_image true, no
                       image, design, rewrite or cells, and one cell
                       across every column reading no_image
    footnotes          {id, text}, each a cell's note, numbered in order
    notes              paragraphs under the table

A template takes a row's fields in braces, {image} and the rest.  An
evidence check is one of:

    {"file": PATH}                       the file exists
    {"file": PATH, "section": TEXT}      and is Markdown with that heading
    {"file": PATH, "contains": TEXT}     and holds that text
    {"make": PATH, "variable": NAME, "word": WORD}
                                         the Makefile's NAME lists WORD
    {"json": PATH, "list": KEY, "field": NAME, "value": VALUE}
                                         an entry of its KEY has NAME VALUE
    {"toml": PATH, "key": KEY, "value": VALUE}
                                         the TOML file's list KEY holds VALUE
    {"dialect": MODEL, "is": DIALECT}    dialect_for() in DEVICE gives
                                         MODEL that dialect

A link is {"path": PATH} or {"path": PATH, "section": TEXT}, on
GitHub, or {"page": PAGE}, one of the site's.  Prose takes
tools/pages_render.py's marks and [text](PAGE), a link to one of the
site's pages.
"""

import argparse
import html
import json
import re
import sys
import tomllib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from pages_matrix import CSS as MATRIX_CSS  # noqa: E402
from pages_render import BLOB, ROOT, Prose, head, headings  # noqa: E402

DATA = ROOT / "docs" / "coverage.json"
# The firmware images, each IMAGE.bin; a no-image row's model must have
# none here.
IMAGES = ROOT / "third_party" / "firmware" / "images"
GENERATED = "tools/coverage.py from docs/coverage.json by make coverage"
# The source whose dialect_for() picks the daemon's dialect, and that
# function's one test: a model starting with a prefix gets one variant,
# every other the other.
DEVICE = "crates/smartclock/src/device.rs"
DIALECT_FOR = re.compile(r"fn dialect_for\b.*?starts_with\(\"([^\"]+)\"\)\s*\{\s*Dialect::(\w+)\s*\}"
                         r"\s*else\s*\{\s*Dialect::(\w+)\s*\}", re.DOTALL)
# The data's name for each of the Rust enum's variants.
DIALECTS = {"Hp58503": "58503", "Z3801": "Z3801"}
# A Makefile variable's assignment, its name and its words.
MAKE_VARIABLE = re.compile(r"^([A-Za-z_]+)\s*:?=(.*)$", re.MULTILINE)
# A link to one of the site's pages in prose.
PAGE_LINK = re.compile(r"\[([^\]]+)\]\(([\w-]+\.html)\)")
# The statuses a check knows by key: the one a cell takes by default.
YES = "yes"

# The table's style on the matrix pages' (tools/pages_matrix.py): their
# colors, type and scroll box, with the row's lead a <th> and each
# status's look.
CSS = MATRIX_CSS + """\
main{display:grid;gap:18px}
main > *{min-width:0}
h2{font-size:1rem;margin:0 0 6px}
.scroll{max-height:none}
tbody th{font-family:"IBM Plex Mono",ui-monospace,monospace;font-weight:600;font-size:.84rem;white-space:nowrap;text-align:left;color:var(--ink)}
td.no-image{text-align:center;font-style:italic}
.status{display:inline-block;padding:0 7px;border-radius:999px;font-size:.8rem;line-height:1.6}
.status.yes{color:var(--yes);background:var(--yes-bg)}
.status.partial{color:var(--mark);background:var(--partial-bg)}
.status.no{color:var(--muted);padding-inline:0}
a.status{text-decoration:none}
a.status:hover{text-decoration:underline}
sup a{text-decoration:none;font-size:.75rem;margin-left:2px}
dl.legend{display:flex;flex-wrap:wrap;gap:6px 18px;margin:0;font-size:.88rem}
dl.legend div{display:flex;gap:6px;align-items:baseline}
dl.legend dt,dl.legend dd{margin:0}
dl.legend dd{color:var(--muted)}
ul,ol{margin:0;padding-left:1.3em;max-width:80ch}
li{margin:3px 0}
li:target{background:var(--partial-bg)}
:root{--yes:#2f6b4f;--yes-bg:#dcefe4;--partial-bg:#f6e8c8}
@media (prefers-color-scheme: dark){:root:not([data-theme="light"]){--yes:#8fd3ad;--yes-bg:#1f3a2c;--partial-bg:#3a2f15}}
:root[data-theme="dark"]{--yes:#8fd3ad;--yes-bg:#1f3a2c;--partial-bg:#3a2f15}
@media (max-width:60rem){
  tbody th{position:sticky;left:0;background:var(--panel);z-index:1;box-shadow:inset -1px 0 var(--rule),inset 0 -1px var(--rule)}
  thead th:first-child{left:0;z-index:3}
  table{min-width:0}
  td{white-space:nowrap}
  td.no-image{text-align:left}
}
"""


def die(message):
    sys.exit(f"coverage: {message}")


def fill(template, row):
    try:
        return template.format(**row)
    except KeyError as missing:
        die(f"{row['name']}: no {missing.args[0]} for {template!r}")


class Evidence:
    """Answers whether an evidence check, filled in for a row, holds,
    reading each file it names once."""

    def __init__(self):
        self.sections = {}
        self.variables = {}
        self.lists = {}
        self.dialect_rule = None

    def path(self, path):
        target = ROOT / path
        if not target.is_file():
            return None
        return target

    def holds(self, check, row):
        if "file" in check:
            target = self.path(fill(check["file"], row))
            if target is not None and "contains" in check:
                return fill(check["contains"], row) in target.read_text(encoding="utf-8")
            if target is None or "section" not in check:
                return target is not None
            if target not in self.sections:
                self.sections[target] = headings(target)
            return fill(check["section"], row) in self.sections[target]
        if "make" in check:
            return fill(check["word"], row) in self.variable(check["make"], check["variable"])
        if "json" in check:
            return fill(check["value"], row) in self.entries(check["json"], check["list"], check["field"])
        if "toml" in check:
            return fill(check["value"], row) in self.toml_list(check["toml"], check["key"])
        if "dialect" in check:
            return self.dialect(fill(check["dialect"], row)) == fill(check["is"], row)
        die(f"unknown evidence check {check}")

    def variable(self, path, name):
        if path not in self.variables:
            target = self.path(path)
            if target is None:
                die(f"evidence names {path}, which does not exist")
            self.variables[path] = {m.group(1): m.group(2).split()
                                    for m in MAKE_VARIABLE.finditer(target.read_text(encoding="utf-8"))}
        if name not in self.variables[path]:
            die(f"{path} sets no {name}")
        return self.variables[path][name]

    def entries(self, path, key, field):
        if (path, key, field) not in self.lists:
            target = self.path(path)
            if target is None:
                die(f"evidence names {path}, which does not exist")
            data = json.loads(target.read_text(encoding="utf-8"))
            self.lists[path, key, field] = {entry[field] for entry in data[key]}
        return self.lists[path, key, field]

    def toml_list(self, path, key):
        if (path, key) not in self.lists:
            target = self.path(path)
            if target is None:
                die(f"evidence names {path}, which does not exist")
            data = tomllib.loads(target.read_text(encoding="utf-8"))
            if key not in data:
                die(f"{path} has no {key}")
            self.lists[path, key] = set(data[key])
        return self.lists[path, key]

    def dialect(self, model):
        if self.dialect_rule is None:
            match = DIALECT_FOR.search((ROOT / DEVICE).read_text(encoding="utf-8"))
            if not match:
                die(f"{DEVICE}: dialect_for() is no longer one prefix test; update {Path(__file__).name}")
            prefix, matched, other = match.groups()
            for variant in (matched, other):
                if variant not in DIALECTS:
                    die(f"{DEVICE}: dialect_for() gives Dialect::{variant}, which DIALECTS lacks")
            self.dialect_rule = (prefix, DIALECTS[matched], DIALECTS[other])
        prefix, matched, other = self.dialect_rule
        return matched if model.upper().startswith(prefix) else other


def cell_of(row, column):
    """A row's cell for a column: its status, its note or None, and its
    text, evidence and link, the column's where the cell sets none."""
    cell = row.get("cells", {}).get(column["key"], YES)
    if isinstance(cell, str):
        cell = {"status": cell}
    return {"status": YES, "note": None,
            **{k: column[k] for k in ("text", "evidence", "link") if k in column}, **cell}


def check(data, pages):
    """Refuses data whose cells the repository does not bear out, a
    status, column or note it does not define, or a footnote no cell
    uses."""
    statuses = data["statuses"]
    if YES not in statuses:
        die(f"no {YES} status")
    keys = [c["key"] for c in data["columns"]]
    notes = [n["id"] for n in data["footnotes"]]
    used = set()
    evidence = Evidence()
    for status, _ in data["legend"]:
        if status not in statuses:
            die(f"legend: unknown status {status}")
    for row in data["images"]:
        for key in row.get("cells", {}):
            if key not in keys:
                die(f"{row['name']}: unknown column {key}")
        if row.get("no_image"):
            found = sorted(p.name for p in IMAGES.glob(f"{row['model'].lower()}*.bin"))
            if found:
                die(f"{row['name']}: no_image, but {IMAGES.relative_to(ROOT)} holds {found}")
            if row.get("cells"):
                die(f"{row['name']}: no_image, but cells")
        for column in columns_of(data, row):
            spec = cell_of(row, column)
            status, note = spec["status"], spec["note"]
            page = spec["link"].get("page")
            if page and page not in pages:
                die(f"{row['name']}, {column['heading']}: links to {page}, which is not one of the site's pages")
            if status not in statuses:
                die(f"{row['name']}, {column['heading']}: unknown status {status}")
            if note:
                if note not in notes:
                    die(f"{row['name']}, {column['heading']}: unknown note {note}")
                used.add(note)
            wanted = statuses[status]["evidence"]
            if wanted == "any":
                continue
            for item in spec["evidence"]:
                holds = evidence.holds(item, row)
                if wanted == "present" and not holds:
                    die(f"{row['name']}, {column['heading']}: {status}, but {describe(item, row)} does not hold")
                if wanted == "absent" and holds:
                    die(f"{row['name']}, {column['heading']}: {status}, but {describe(item, row)} holds; "
                        f"mark it {YES}")
    for note in notes:
        if note not in used:
            die(f"note {note}: no cell uses it")


def columns_of(data, row):
    """The columns a row has cells of its own in: all, or for a model
    with no image none."""
    if row.get("no_image"):
        return []
    return data["columns"]


def describe(check, row):
    return json.dumps({k: fill(v, row) for k, v in check.items()})


class Text:
    """Renders prose: tools/pages_render.py's marks, and [text](PAGE)
    links to the site's pages, each checked against PAGES."""

    def __init__(self, pages):
        self.pages = pages
        self.prose = Prose(set(), lambda ref: ref, die)

    def __call__(self, text):
        out = []
        last = 0
        for match in PAGE_LINK.finditer(text):
            if match.group(2) not in self.pages:
                die(f"links to {match.group(2)}, which is not one of the site's pages")
            out.append(self.prose(text[last:match.start()]))
            out.append(f'<a href="{html.escape(match.group(2))}">{html.escape(match.group(1))}</a>')
            last = match.end()
        out.append(self.prose(text[last:]))
        return "".join(out)


def address(link, row):
    if "page" in link:
        return link["page"]
    path = fill(link["path"], row)
    if "section" not in link:
        return f"{BLOB}{path}"
    return f"{BLOB}{path}#{headings(ROOT / path)[fill(link['section'], row)]}"


def note_anchor(note):
    return "note-" + note


def cell(data, column, row, numbers):
    spec = cell_of(row, column)
    status, note = spec["status"], spec["note"]
    look = data["statuses"][status]
    if status == YES and "text" in spec:
        text = fill(spec["text"], row)
    else:
        text = look["text"] or fill(spec.get("text", ""), row)
    label = html.escape(text)
    if status == YES:
        shown = f'<a class="status yes" href="{html.escape(address(spec["link"], row))}">{label}</a>'
    elif look["class"] == "plain":
        shown = label
    else:
        shown = f'<span class="status {look["class"]}">{label}</span>'
    if note:
        shown += f'<sup><a href="#{note_anchor(note)}">{numbers[note]}</a></sup>'
    return f"<td>{shown}</td>"


def render(data, pages):
    check(data, pages)
    text = Text(pages)
    numbers = {n["id"]: i for i, n in enumerate(data["footnotes"], 1)}
    out = head(data["title"], data["description"], "firmware", GENERATED, CSS)
    emit = out.append
    emit("<body>")
    emit("<main>")
    emit("<header>")
    emit(f"<h1>{html.escape(data['heading'])}</h1>")
    for paragraph in data["intro"]:
        emit(f'<p class="muted">{text(paragraph)}</p>')
    emit("</header>")
    emit('<dl class="legend">')
    for status, meaning in data["legend"]:
        look = data["statuses"][status]
        emit(f'<div><dt><span class="status {look["class"]}">{html.escape(look["text"])}</span></dt>'
             f"<dd>{html.escape(meaning)}</dd></div>")
    emit("</dl>")
    emit('<div class="scroll" data-scroll="x">')
    emit("<table>")
    emit("<thead><tr><th>Image</th>"
         + "".join(f"<th>{html.escape(c['heading'])}</th>" for c in data["columns"]) + "</tr></thead>")
    emit("<tbody>")
    for row in data["images"]:
        spanned = len(data["columns"]) - len(columns_of(data, row))
        missing = (f'<td colspan="{spanned}" class="no-image"><span class="status no">'
                   f'{html.escape(data["no_image"])}</span></td>') if spanned else ""
        emit(f'<tr><th scope="row">{html.escape(row["name"])}</th>{missing}'
             + "".join(cell(data, c, row, numbers) for c in columns_of(data, row)) + "</tr>")
    emit("</tbody>")
    emit("</table>")
    emit("</div>")
    if data["footnotes"]:
        emit("<section>")
        emit("<ol>")
        for note in data["footnotes"]:
            emit(f'<li id="{note_anchor(note["id"])}">{text(note["text"])}</li>')
        emit("</ol>")
        emit("</section>")
    emit("<section>")
    emit("<h2>The columns</h2>")
    emit("<ul>")
    for column in data["columns"]:
        emit(f"<li><b>{html.escape(column['heading'])}</b>: {text(column['about'])}.</li>")
    emit("</ul>")
    emit("</section>")
    emit("<section>")
    emit("<h2>Notes</h2>")
    emit("<ul>")
    for note in data["notes"]:
        emit(f"<li>{text(note)}</li>")
    emit("</ul>")
    emit("</section>")
    emit(f'<footer class="muted">Generated by <a href="{BLOB}tools/coverage.py"><code>tools/coverage.py</code></a> '
         f'from <a href="{BLOB}docs/coverage.json"><code>docs/coverage.json</code></a>.</footer>')
    emit("</main>")
    emit("</body>")
    emit("</html>")
    return "\n".join(out) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("out", type=Path, help="the page to write")
    parser.add_argument("pages", nargs="+", help="the site's pages, each a file name")
    args = parser.parse_args()
    data = json.loads(DATA.read_text(encoding="utf-8"))
    args.out.write_text(render(data, set(args.pages)), encoding="utf-8")


if __name__ == "__main__":
    main()
