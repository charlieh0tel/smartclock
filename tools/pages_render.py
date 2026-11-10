"""What the generated pages share: the head every published page carries,
and their data's prose marks, rendered with each citation checked.

Prose takes three marks: `code`; [[path]] or [[path|section]], a
citation of a file in the repository, linked on GitHub, whose path must
exist and whose section must be a heading of that Markdown file, or
"item N" for hardware-investigations.md's numbered items, linked by the
fragment GitHub gives the heading; and {{ref}}, a link to one of the
page's own entries.

A generator imports it from its own directory, which `python3 -I` leaves
off the path:

    sys.path.insert(0, str(Path(__file__).resolve().parent))
"""

import html
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BLOB = "https://github.com/charlieh0tel/smartclock/blob/main/"
CITE = re.compile(r"\[\[([^|\]]+)(?:\|([^\]]+))?\]\]")
CODE = re.compile(r"`([^`]+)`")
REF = re.compile(r"\{\{([^}]+)\}\}")
ITEM = re.compile(r"item (\d+)$")
# A Markdown heading: its level's hashes and its text.
HEADING = re.compile(r"(#+) (.*)")
# A numbered item's heading text, and the level its headings take.
NUMBERED = re.compile(r"(\d+)\. ")
ITEM_LEVEL = 2
# What GitHub drops from a heading's rendered text to make its fragment:
# all but letters, marks, digits, connector punctuation, spaces and
# hyphens.  Its inline marks go first: code spans' backticks, links'
# brackets and targets, and emphasis.
FRAGMENT_DROPPED = re.compile(r"[^\w\- ]")
INLINE_LINK = re.compile(r"\[([^\]]*)\]\([^)]*\)")
INLINE_MARKS = re.compile(r"[`*]")
# The pages' type, from Google Fonts.
FONTS = ("https://fonts.googleapis.com/css2?family=IBM+Plex+Mono:wght@400;600"
         "&family=IBM+Plex+Sans:wght@400;500;600&display=swap")


def fragment(text):
    """The fragment GitHub gives a heading, before it numbers repeats."""
    text = INLINE_MARKS.sub("", INLINE_LINK.sub(r"\1", text))
    return FRAGMENT_DROPPED.sub("", text.lower()).replace(" ", "-")


def headings(path):
    """Each heading text of a Markdown file, and "item N" for its
    numbered items, against the heading's fragment; the first of a
    repeated text, as GitHub numbers the later ones -1, -2 and on."""
    found = {}
    used = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        match = HEADING.match(line)
        if not match:
            continue
        text = match.group(2).strip()
        base = fragment(text)
        repeat = used.get(base, 0)
        used[base] = repeat + 1
        name = f"{base}-{repeat}" if repeat else base
        found.setdefault(text, name)
        item = NUMBERED.match(text)
        if item and len(match.group(1)) == ITEM_LEVEL:
            found.setdefault(f"item {item.group(1)}", name)
    return found


def plain(text):
    """Prose without its marks, for an attribute: a citation as its
    file's name, code without its backticks."""
    return CITE.sub(lambda m: m.group(1).rsplit("/", 1)[-1], text).replace("`", "")


class Prose:
    """Renders prose marks, checking each citation and reference as it
    goes: a reference must be one of refs, and links to #anchor(ref);
    a fault calls fail with a message, which must not return."""

    def __init__(self, refs, anchor, fail):
        self.refs = refs
        self.anchor = anchor
        self.fail = fail
        self.headings = {}

    def code(self, text):
        return CODE.sub(lambda m: f"<code>{m.group(1)}</code>", html.escape(text, quote=False))

    def cite(self, path, section):
        target = ROOT / path
        if not target.is_file():
            self.fail(f"cites {path}, which does not exist")
        shown = re.sub(r"^(docs|third_party)/", "", path)
        if not section:
            return f'<a href="{BLOB}{html.escape(path)}"><code>{html.escape(shown)}</code></a>'
        if path not in self.headings:
            self.headings[path] = headings(target)
        if section not in self.headings[path]:
            self.fail(f"cites {path}, {section!r}, which is not one of its headings")
        address = f"{BLOB}{path}#{self.headings[path][section]}"
        link = f'<a href="{html.escape(address)}"><code>{html.escape(shown)}</code></a>'
        if ITEM.match(section):
            return f"{link}, {self.code(section)}"
        return f"{link}, “{self.code(section)}”"

    def ref(self, ref):
        if ref not in self.refs:
            self.fail(f"refers to {ref}, which is not one of the page's entries")
        return f'<a href="#{self.anchor(ref)}">{html.escape(ref)}</a>'

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


def head(title, description, group, generated, css):
    """A generated page's lines up to its <body>: the title, description
    and smartclock-group meta tags tools/pages_index.py lists it by, a
    comment saying what generated it, the fonts and its style."""
    return [
        "<!doctype html>",
        '<html lang="en">',
        "<head>",
        '<meta charset="utf-8">',
        '<meta name="viewport" content="width=device-width, initial-scale=1">',
        f"<title>{html.escape(title)}</title>",
        f'<meta name="description" content="{html.escape(description)}">',
        f'<meta name="smartclock-group" content="{html.escape(group)}">',
        f"<!-- Generated by {generated}. -->",
        '<link rel="preconnect" href="https://fonts.googleapis.com">',
        f'<link rel="stylesheet" href="{FONTS}">',
        "<style>",
        css.rstrip("\n"),
        "</style>",
        "</head>",
    ]
