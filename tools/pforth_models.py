"""Renders the pForth words by model, every word each firmware image's
console knows side by side, as a page that filters them, from DATA, what
`tools/firmware/pforth_words.py --json` prints, to OUT.  `make
pforth-models` writes it among the generated pages, which
.github/workflows/pages.yml publishes on GitHub Pages; the page is not
committed.

    python3 -I tools/pforth_models.py DATA OUT

A row is a word of one table, kernel or diagnostic; a column an image,
its cell the word's code address there, blank where the image lacks
the word.  A word's flags, inline size and stack effect are shown once,
in the row, where every image that has it agrees; otherwise in each
cell.  Stack effects are compared with the addresses in their reasons
set aside, since the same routine sits at a different address in each
image.  Fails if an image's table holds a word twice.
"""

import argparse
import html
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from pages_matrix import checkbox, page, select  # noqa: E402
from pages_render import BLOB  # noqa: E402

GENERATED = "tools/pforth_models.py by make pforth-models"
# The tables, in the page's order: each one's key in the data and its
# section's heading.
TABLES = (("kernel", "Kernel words"), ("diagnostic", "Diagnostic words"))
# An address in a stack effect's reason, and what stands for it where
# the images' effects are compared and shown once.
ADDRESS = re.compile(r"0x[0-9a-f]+")
ADDRESS_SHOWN = "…"
# What a row's Flags or Stack shows where the images disagree, each
# cell then showing its own.
BY_IMAGE = "by image"

# Builds a row's cells, and the page's filters by control.
SCRIPT = """\
matrix({
  columns: ['Word', 'Flags', 'Stack', ...DATA.images],
  sections: DATA.sections,
  row: row => {
    const word = td(row.name, 'lead');
    for (const text of row.descriptions) {
      const line = document.createElement('span'); line.className = 'also'; line.textContent = text; word.appendChild(line);
    }
    return [word, td(row.flags), td(row.stack), ...row.cells.map(c => {
      const cell = td(c ? c.lines[0] : '', 'cell');
      if (c) {
        cell.title = c.title;
        for (const text of c.lines.slice(1)) {
          const line = document.createElement('span'); line.className = 'also'; line.textContent = text; cell.appendChild(line);
        }
      }
      return cell;
    })];
  },
  words: row => row.name + ' ' + row.descriptions.join(' '),
  tests: {
    differs: m => m.data.differs,
    shared: m => m.data.cells.every(c => c),
    table: (m, table) => m.section === table,
    image: (m, image) => m.data.cells[image] !== null,
  },
});
"""

# A word's descriptions under it, prose in the text face; each cell's
# lines under its address, smaller and wrapped, so that an effect shown
# by image does not widen the table.
CSS = """\
td.lead .also{font-family:"IBM Plex Sans",system-ui,sans-serif;overflow-wrap:normal}
td.cell .also{display:block;color:var(--muted);font-size:.74rem;white-space:normal;text-align:left}
"""


def fail(message):
    sys.exit(f"pforth_models: {message}")


def flags(word):
    """A word's flags as its row or cell shows them, the inline size after
    inline."""
    return ", ".join(f"inline {word['inline']} bytes" if name == "inline" and word["inline"] else name
                     for name in word["flags"])


def stack(word):
    return ADDRESS.sub(ADDRESS_SHOWN, word["stack"])


def uniform(values):
    """The one value all of VALUES are, or None where they differ."""
    return values[0] if len(set(values)) == 1 else None


def descriptions(images, held):
    """A word's descriptions: the one every image that describes it
    gives, or each with the images giving it."""
    givers = {}
    for index, word in held.items():
        if word["description"]:
            givers.setdefault(word["description"], []).append(images[index])
    if len(givers) == 1:
        return list(givers)
    return [f"{', '.join(names)}: {text}" for text, names in givers.items()]


def row(images, name, held):
    """A word's row: HELD is its entry in each image that has it, by the
    image's index."""
    words = list(held.values())
    shown = {"Flags": uniform([flags(w) for w in words]), "Stack": uniform([stack(w) for w in words])}
    cells = []
    for index in range(len(images)):
        word = held.get(index)
        if word is None:
            cells.append(None)
            continue
        lines = [word["code"]]
        if shown["Flags"] is None:
            lines.append(flags(word) or "no flags")
        if shown["Stack"] is None:
            lines.append(word["stack"])
        title = [f"{images[index]}: code {word['code']}"]
        if word["header"]:
            title.append(f"header {word['header']}")
        if flags(word):
            title.append(flags(word))
        title.append(f"stack {word['stack']}")
        cells.append({"lines": lines, "title": "; ".join(title)})
    return {
        "name": name,
        "descriptions": descriptions(images, held),
        "flags": BY_IMAGE if shown["Flags"] is None else shown["Flags"],
        "stack": BY_IMAGE if shown["Stack"] is None else shown["Stack"],
        "cells": cells,
        "differs": len(held) < len(images) or None in shown.values(),
    }


def matrix(data):
    """The page's data: the images' names, and the rows by table, each
    table's words in name order."""
    images = [image["name"] for image in data["images"]]
    held = {table: {} for table, _ in TABLES}
    for index, image in enumerate(data["images"]):
        for word in image["words"]:
            if word["table"] not in held:
                fail(f"{image['name']}: {word['name']} is in table {word['table']!r}, not one of "
                     f"{', '.join(held)}")
            entries = held[word["table"]].setdefault(word["name"], {})
            if index in entries:
                fail(f"{image['name']}'s {word['table']} table holds {word['name']} twice")
            entries[index] = word
    return {
        "images": images,
        "sections": [{"name": heading, "rows": [row(images, name, held[table][name])
                                                for name in sorted(held[table])]}
                     for table, heading in TABLES],
    }


def intro():
    readme = f'<a href="{BLOB}docs/firmware/pforth/README.md"><code>docs/firmware/pforth/README.md</code></a>'
    console = f'<a href="{BLOB}docs/firmware/console.md"><code>docs/firmware/console.md</code></a>'
    return ('<p class="muted">Every word each firmware image\'s pForth console knows, side by side: the '
            "interpreter's kernel words and the firmware's own diagnostic words, a row per word. A cell is the "
            "word's code address in that image, blank where the image lacks the word; hover it for the "
            "header's address too. Flags and Stack are shown once where every image with the word agrees, "
            f"the addresses in a stack effect's reason set aside, and in each cell where they do not; "
            "Stack is cells taken -- cells left. Only differing keeps the words some image lacks or whose "
            "flags or stack effect differ; only shared, those every image has.</p>\n"
            '<p class="muted">Generated by <code>tools/firmware/pforth_words.py --json</code> and '
            "<code>tools/pforth_models.py</code>, from the same reading as each image's "
            f"<code>IMAGE-words.md</code>; {readme} says how the tables are read, and {console} is the "
            "console itself.</p>")


def render(data):
    shown = matrix(data)
    return page(
        title="pForth Words by Model",
        description="Every word each SmartClock firmware image's pForth console knows, side by side, with "
                    "its address, flags and stack effect in each, as a table that filters.",
        group="firmware",
        generated=GENERATED,
        heading="pForth words by model",
        intro=intro(),
        placeholder="Filter words, e.g. gps",
        controls=[
            checkbox("differs", "Only differing"),
            checkbox("shared", "Only shared"),
            select("table", "Table", [(heading, heading) for _, heading in TABLES]),
            select("image", "Image", enumerate(shown["images"])),
        ],
        data=shown,
        script=SCRIPT,
        css=CSS,
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("data", type=Path, help="pforth_words.py --json's output")
    parser.add_argument("out", type=Path, help="the page to write")
    args = parser.parse_args()
    data = json.loads(args.data.read_text(encoding="utf-8"))
    args.out.write_text(render(data), encoding="utf-8")


if __name__ == "__main__":
    main()
