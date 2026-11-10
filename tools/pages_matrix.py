"""What the matrix pages share: a table of rows by section, one column
for each firmware image, that a text box and the page's own controls
filter as the reader types, its style and its script.

The page is the table's data as JSON and a script that builds the
table from it: tools/scpi_models.py's (scpi-models.html) and
tools/pforth_models.py's (pforth-models.html).  The table scrolls
inside its own box, its header row held in place and, in a narrow
window, its first column; the page itself never scrolls sideways, at a
phone's width either.

A generator imports it from its own directory, which `python3 -I` leaves
off the path:

    sys.path.insert(0, str(Path(__file__).resolve().parent))
"""

import html
import json

from pages_render import head

CSS = """\
:root{
  --bg:#f6f5f1; --ink:#1d2126; --muted:#5d646c; --rule:#d9d6ce;
  --panel:#ffffff; --accent:#1f6f8b; --band:#efede7; --mark:#9a5b00;
  color-scheme:light;
}
@media (prefers-color-scheme: dark){
  :root:not([data-theme="light"]){
    color-scheme:dark;
    --bg:#15181c; --ink:#e8e6e0; --muted:#a3a9b0; --rule:#343a41;
    --panel:#1c2025; --accent:#5fb3d0; --band:#22272d; --mark:#e6ac3a;
  }
}
:root[data-theme="dark"]{
  color-scheme:dark;
  --bg:#15181c; --ink:#e8e6e0; --muted:#a3a9b0; --rule:#343a41;
  --panel:#1c2025; --accent:#5fb3d0; --band:#22272d; --mark:#e6ac3a;
}
*{box-sizing:border-box}
body{background:var(--bg);color:var(--ink);font-family:"IBM Plex Sans",system-ui,sans-serif;font-size:15px;line-height:1.45;margin:0;padding-block:28px 48px;padding-inline:20px}
main{max-width:1200px;margin:0 auto;min-width:0}
h1{font-size:1.6rem;margin:0 0 6px}
p{margin:0 0 12px;max-width:80ch}
.muted{color:var(--muted)}
a{color:var(--accent)}
code{font-family:"IBM Plex Mono",ui-monospace,monospace;font-size:.92em;overflow-wrap:anywhere}
.controls{display:flex;flex-wrap:wrap;gap:10px 18px;align-items:center;margin:16px 0 12px}
.controls input[type=search]{font:inherit;padding:6px 10px;border:1px solid var(--rule);background:var(--panel);color:var(--ink);border-radius:6px;flex:1 1 16rem;min-width:0;max-width:24rem}
.controls select{font:inherit;padding:5px 8px;border:1px solid var(--rule);background:var(--panel);color:var(--ink);border-radius:6px;max-width:100%}
.controls label{display:flex;gap:6px;align-items:center;white-space:nowrap}
#count{margin-left:auto}
.scroll{max-height:75vh;overflow:auto;border:1px solid var(--rule);background:var(--panel)}
table{border-collapse:collapse;width:100%;font-size:.88rem;font-variant-numeric:tabular-nums}
th,td{padding:5px 8px;border-bottom:1px solid var(--rule);text-align:left;vertical-align:top}
thead th{position:sticky;top:0;background:var(--panel);z-index:2;font-size:.78rem;font-weight:600;color:var(--muted);box-shadow:inset 0 -1px var(--rule)}
td.lead{font-family:"IBM Plex Mono",ui-monospace,monospace;font-size:.84rem;overflow-wrap:anywhere}
td.lead .also{display:block;color:var(--muted);font-size:.78rem}
td.cell{font-family:"IBM Plex Mono",ui-monospace,monospace;text-align:center;white-space:nowrap}
tr.section th{background:var(--band);color:var(--ink);font-size:.85rem;font-weight:600;position:static}
/* A mark on a row, quiet; one linking to more, in the mark's color. */
.tag{display:inline-block;padding:0 5px;border:1px solid var(--rule);border-radius:3px;color:var(--muted);font:600 .7rem/1.5 "IBM Plex Sans",system-ui,sans-serif;letter-spacing:.03em;white-space:nowrap;vertical-align:.1em}
a.tag{border-color:var(--mark);color:var(--mark);text-decoration:none}
a.tag:hover{text-decoration:underline}
/* A narrow window: the table keeps a width its cells can be read at and
   scrolls inside its box, its first column held at the left. */
@media (max-width:60rem){
  body{padding-inline:16px}
  table{min-width:56rem}
  td.lead,thead th:first-child{position:sticky;left:0;background:var(--panel);z-index:1;box-shadow:inset -1px 0 var(--rule),inset 0 -1px var(--rule)}
  thead th:first-child{z-index:3}
  td.lead{min-width:10rem;max-width:12rem}
  #count{margin-left:0}
}
"""

# The script every matrix page runs: matrix(spec) builds the table from
# a page's data and filters it.  spec.columns is the header row's names;
# spec.sections the rows by section, {name, rows}; spec.row(row) a row's
# <td>s, the first its lead; spec.words(row) the text the search box
# matches; spec.tests, by a control's id, a function of a row's entry
# ({data, section}) and the control's value, a checkbox's only while it
# is checked and a select's only while it is not "".
SCRIPT = """\
const $ = id => document.getElementById(id);
function td(text, className) {
  const cell = document.createElement('td');
  if (className) cell.className = className;
  cell.textContent = text;
  return cell;
}
function matrix(spec) {
  const table = $('matrix');
  const head = document.createElement('tr');
  for (const name of spec.columns) {
    const th = document.createElement('th'); th.textContent = name; head.appendChild(th);
  }
  table.tHead.appendChild(head);
  const body = table.tBodies[0];
  const groups = spec.sections.map(section => {
    const tr = document.createElement('tr'); tr.className = 'section';
    const th = document.createElement('th'); th.colSpan = spec.columns.length; th.textContent = section.name;
    tr.appendChild(th); body.appendChild(tr);
    const members = section.rows.map(data => {
      const r = document.createElement('tr');
      for (const cell of spec.row(data)) r.appendChild(cell);
      body.appendChild(r);
      return { r, data, section: section.name, words: spec.words(data).toLowerCase() };
    });
    return { tr, members };
  });
  const controls = Object.entries(spec.tests).map(([id, test]) => [$(id), test]);
  function apply() {
    const q = $('search').value.trim().toLowerCase();
    const active = controls
      .map(([control, test]) => [test, control.type === 'checkbox' ? control.checked : control.value])
      .filter(([, value]) => value !== false && value !== '');
    let shown = 0, total = 0;
    for (const { tr, members } of groups) {
      let any = false;
      for (const m of members) {
        total++;
        const ok = (!q || m.words.includes(q)) && active.every(([test, value]) => test(m, value));
        m.r.hidden = !ok; if (ok) { any = true; shown++; }
      }
      tr.hidden = !any;
    }
    $('count').textContent = shown + ' of ' + total + ' rows';
  }
  $('search').addEventListener('input', apply);
  for (const [control] of controls) control.addEventListener('input', apply);
  apply();
}
"""


def checkbox(id, label):
    return f'<label><input type="checkbox" id="{id}"> {html.escape(label)}</label>'


def select(id, label, options):
    """A select of OPTIONS, (value, text) each, after "any", whose value is ""."""
    choices = "".join(f'<option value="{html.escape(str(value))}">{html.escape(text)}</option>'
                      for value, text in options)
    return (f'<label>{html.escape(label)} <select id="{id}"><option value="">any</option>'
            f"{choices}</select></label>")


def page(*, title, description, group, generated, heading, intro, placeholder, controls, data, script,
         css=""):
    """A matrix page: the head pages_render gives, the heading, INTRO (the
    paragraphs under it, HTML), the search box with PLACEHOLDER, the
    page's CONTROLS (HTML), and the table, which SCRIPT builds from DATA
    by matrix(); CSS adds to the shared style."""
    lines = head(title, description, group, generated, CSS + css)
    payload = json.dumps(data, ensure_ascii=False, separators=(",", ":")).replace("</", "<\\/")
    lines += [
        "<body>",
        "<main>",
        f"<h1>{html.escape(heading)}</h1>",
        intro,
        '<div class="controls">',
        f'<input type="search" id="search" placeholder="{html.escape(placeholder)}" '
        f'aria-label="{html.escape(placeholder)}">',
        *controls,
        '<span id="count" class="muted"></span>',
        "</div>",
        '<div class="scroll" data-scroll="x"><table id="matrix"><thead></thead><tbody></tbody></table></div>',
        "</main>",
        "<script>",
        f"const DATA = {payload};",
        SCRIPT.rstrip("\n"),
        script.rstrip("\n"),
        "</script>",
        "</body>",
        "</html>",
    ]
    return "\n".join(lines) + "\n"
