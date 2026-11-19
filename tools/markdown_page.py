# /// script
# requires-python = ">=3.11"
# dependencies = ["markdown-it-py==4.2.0"]
# ///
"""Renders a Markdown document in the repository as a published page, so
that the document stays the one source and the page is derived from it.

    uv run -q tools/markdown_page.py --group GROUP --description TEXT
        [--photos DIR] [--require-link URL]... SOURCE OUT

The document's first heading is the page's title, and what comes before
its first second-level heading the page's header; each second-level
heading opens a section.  Headings get the fragments GitHub gives them.
A relative link is resolved against the document's directory and
rewritten: a page published on GitHub Pages, from docs/pages/ or by a
page in docs/ forwarding to one, to its Pages URL; any other file or
directory in the repository to its GitHub URL on main, a section of a
Markdown file checked to be one of its headings; a section of the
document itself to the page's own heading.  A code span naming
such a file or directory is linked the same way.  HTML comments, the
markers around generated blocks, are dropped, and so is a top-level
paragraph that links the page's own URL on GitHub Pages, the document's
pointer to the page.

With --photos, a code span naming a photo in DIR (a path under docs/)
links to it, and the photos are shown as figures after the paragraph,
or the block, that first names them, each captioned by the words
naming it and its file's name, and copied beside the page at their
path under docs/, as tools/pages_copy.py copies a hand-written page's.

Fails on a relative link or image that does not resolve, a section
that is not a heading of its file, raw HTML other than a comment, a
link to the page's own URL anywhere but a paragraph of its own, a
photo in DIR the document never names, or a page without a link to
each --require-link URL.
"""

import argparse
import html
import re
import shutil
import struct
import sys
from pathlib import Path

from markdown_it import MarkdownIt
from markdown_it.token import Token

sys.path.insert(0, str(Path(__file__).resolve().parent))
from pages_render import BLOB, ROOT, fragment, head, headings  # noqa: E402

DOCS = ROOT / "docs"
HAND_PAGES = DOCS / "pages"
TREE = BLOB.replace("/blob/", "/tree/")
PAGES_URL = "https://charlieh0tel.github.io/smartclock/"
# A forwarding page's target, in its refresh meta tag.
REFRESH = re.compile(r'http-equiv="refresh" content="\d+; url=([^"]+)"')
# A link's target that names a scheme (https:, mailto:) and is left be.
SCHEME = re.compile(r"[a-z][a-z0-9+.-]*:", re.IGNORECASE)
# A code span's text that may name a file: one word, no spaces.
PATH_LIKE = re.compile(r"[\w.\-/]+")
PHOTO_SUFFIXES = (".jpg", ".jpeg")
# What a caption's words for a photo start after: the end of a clause.
CLAUSE_ENDS = ("; ", ": ", ". ")
# The words the document marks its claims with in bold, each a class.
MARKS = ("read", "seen", "inferred")

CSS = """\
:root{
  --bg:#f6f5f1; --fg:#1d2420; --muted:#5b655f; --rule:#c9cec4;
  --panel:#ffffff; --accent:#1f6f8b;
  --seen:#2453a8; --inferred:#a35f00; --read:#3d6b2f;
  --measure:46rem;
}
@media (prefers-color-scheme: dark){
  :root:not([data-theme="light"]){
    color-scheme:dark;
    --bg:#15181c; --fg:#e8e6e0; --muted:#a3a9b0; --rule:#3a4148;
    --panel:#1c2025; --accent:#5fb3d0;
    --seen:#8fb0ff; --inferred:#e0a05a; --read:#9cc98a;
  }
}
:root[data-theme="dark"]{
  color-scheme:dark;
  --bg:#15181c; --fg:#e8e6e0; --muted:#a3a9b0; --rule:#3a4148;
  --panel:#1c2025; --accent:#5fb3d0;
  --seen:#8fb0ff; --inferred:#e0a05a; --read:#9cc98a;
}
*,*::before,*::after{box-sizing:border-box}
body{background:var(--bg);color:var(--fg);font-family:"IBM Plex Sans",system-ui,sans-serif;font-size:16px;line-height:1.55;margin:0;padding:2rem 16px 4rem}
main{max-width:var(--measure);margin:0 auto;display:grid;gap:3rem}
main>*{min-width:0}
header>*,section>*,figure{margin:0}
header>*+*,section>*+*{margin-top:1rem}
section>h3{margin-top:2.25rem}
h1{font-size:2rem;font-weight:600;line-height:1.15;text-wrap:balance}
h2{font-size:1.375rem;font-weight:600;line-height:1.25;text-wrap:balance}
h3{font-size:1.0625rem;font-weight:600}
ul,ol{padding-left:1.25rem}
li+li{margin-top:.5rem}
code{font-family:"IBM Plex Mono",ui-monospace,monospace;font-size:.92em;overflow-wrap:anywhere}
pre{font-family:"IBM Plex Mono",ui-monospace,monospace;font-size:.875rem;background:var(--panel);border-left:3px solid var(--accent);padding:.75rem 1rem;overflow-x:auto;max-width:100%}
pre code{font-size:inherit;overflow-wrap:normal}
a{color:var(--accent);overflow-wrap:anywhere}
.mark-read{color:var(--read)}
.mark-seen{color:var(--seen)}
.mark-inferred{color:var(--inferred)}
.wide{overflow-x:auto}
table{border-collapse:collapse;width:100%;font-size:.9375rem;font-variant-numeric:tabular-nums}
th,td{text-align:left;vertical-align:top;padding:.5rem .75rem;border-bottom:1px solid var(--rule)}
th:first-child,td:first-child{padding-left:0}
th:last-child,td:last-child{padding-right:0}
thead th{font-weight:600;font-size:.8125rem;letter-spacing:.04em;text-transform:uppercase;color:var(--muted);vertical-align:bottom;border-bottom-color:var(--muted)}
.photos{display:grid;grid-template-columns:repeat(auto-fill,minmax(min(100%,13rem),1fr));gap:12px;margin:.75rem 0 0}
.photos figure{display:grid;gap:6px;align-content:start;min-width:0}
.photos img{display:block;width:100%;height:auto;border:1px solid var(--rule);border-radius:4px;background:var(--panel)}
.photos figcaption{font-size:.8125rem;color:var(--muted)}
figure:target img{outline:3px solid var(--accent);outline-offset:2px}
footer{font-size:.875rem;color:var(--muted);border-top:1px solid var(--rule);padding-top:1rem}
@media (max-width:720px){
  body{padding-top:1.5rem}
  h1{font-size:1.75rem}
  /* A table of four columns or more keeps them wide enough to read, in
     its own scroll box. */
  .wide table:has(th:nth-child(4)){min-width:34rem}
}
"""


def fail(message):
    sys.exit(f"markdown_page: {message}")


def jpeg_size(path):
    """A JPEG's width and height, from its first start-of-frame marker."""
    data = path.read_bytes()
    at = 2
    while at + 4 <= len(data):
        if data[at] != 0xFF:
            fail(f"{path.relative_to(ROOT)}: not a JPEG, or a marker is missing at {at}")
        marker = data[at + 1]
        (length,) = struct.unpack(">H", data[at + 2:at + 4])
        # SOF0 to SOF15, but for DHT (C4), JPG (C8) and DAC (CC).
        if 0xC0 <= marker <= 0xCF and marker not in (0xC4, 0xC8, 0xCC):
            height, width = struct.unpack(">HH", data[at + 5:at + 9])
            return width, height
        at += 2 + length
    fail(f"{path.relative_to(ROOT)}: no start-of-frame marker")


class Page:
    """One document's rendering: its links resolved against its
    directory, its photos found and placed."""

    def __init__(self, source, photos, url):
        self.source = source
        # The page's own URL on GitHub Pages.
        self.url = url
        self.base = source.parent
        self.photos = photos
        self.headings = {}
        # The files Markdown images name, to copy beside the page.
        self.images = set()
        # Each photo's path named so far, in order, against its caption.
        self.photo_captions = {}

    def where(self):
        return self.source.relative_to(ROOT)

    def resolve(self, target):
        """The repository file or directory a relative path names, from
        the document's directory or, failing that, the repository's root,
        or None."""
        for base in (self.base, ROOT):
            path = (base / target).resolve()
            if path.is_relative_to(ROOT) and path.exists():
                return path
        return None

    def address(self, path, section, link):
        """Where a link to a repository path, and a section of it, goes."""
        relative = path.relative_to(ROOT).as_posix()
        if path == self.source and section is not None:
            if section not in set(headings(path).values()):
                fail(f"{self.where()} links {link}, whose section is not one of its own headings")
            return f"#{section}"
        if path.is_dir():
            return f"{TREE}{relative}"
        if path.suffix == ".html":
            if path.parent == HAND_PAGES:
                return f"{PAGES_URL}{path.name}"
            forward = REFRESH.search(path.read_text(encoding="utf-8"))
            if forward:
                return forward.group(1)
        if section is None:
            return f"{BLOB}{relative}"
        if path.suffix == ".md":
            if path not in self.headings:
                self.headings[path] = set(headings(path).values())
            if section not in self.headings[path]:
                fail(f"{self.where()} links {link}, whose section is not one of {relative}'s headings")
        return f"{BLOB}{relative}#{section}"

    def link(self, target):
        """A Markdown link's or image's target, rewritten; fails on a
        relative one that does not resolve."""
        if SCHEME.match(target) or target.startswith("#"):
            return target
        name, _, section = target.partition("#")
        path = self.resolve(name)
        if path is None:
            fail(f"{self.where()} links {target}, which does not resolve to a file in the repository")
        return self.address(path, section or None, target)

    def photo(self, name):
        """The photo a code span names, or None."""
        if self.photos is None or not name.lower().endswith(PHOTO_SUFFIXES):
            return None
        path = self.photos / name
        return path if path.is_file() else None

    def code_link(self, name):
        """Where a code span naming a file goes, or None."""
        if not PATH_LIKE.fullmatch(name) or not ("/" in name or "." in name):
            return None
        photo = self.photo(name)
        if photo is not None:
            return f"#{photo_id(photo)}"
        path = self.resolve(name)
        return None if path is None else self.address(path, None, name)


def photo_id(path):
    return f"photo-{path.stem}"


def caption(content, mention):
    """The words naming a photo, in the Markdown of the inline content
    that names it at mention: the clause before the parenthesis the
    mention is in, without a leading "and", or None."""
    before = content[:mention]
    depth = 0
    for at in range(len(before) - 1, -1, -1):
        if before[at] == ")":
            depth += 1
        elif before[at] == "(":
            if depth == 0:
                break
            depth -= 1
    else:
        return None
    words = " ".join(before[:at].split())
    cut = max(words.rfind(end) + len(end) if end in words else 0 for end in CLAUSE_ENDS)
    words = words[cut:].removeprefix("and ").strip()
    return words[:1].upper() + words[1:] if words else None


def heading_level(token):
    return int(token.tag[1:])


def links_to(inline, url):
    return any(child.type == "link_open" and child.attrGet("href") == url
               for child in inline.children or ())


def render(md, page, tokens):
    """The document's tokens rewritten in place: comments and the pointer
    to the page dropped, links resolved, code spans linked, the bold
    marks classed, headings given fragments and the photos placed;
    returns the tokens and the photos shown, in order."""
    kept = []
    at = 0
    while at < len(tokens):
        token = tokens[at]
        if token.type == "html_block":
            if not token.content.lstrip().startswith("<!--"):
                fail(f"{page.where()} has raw HTML other than a comment: {token.content.strip()[:60]!r}")
        elif token.type == "paragraph_open" and token.level == 0 and links_to(tokens[at + 1], page.url):
            # The paragraph's open, inline and close tokens.
            at += 3
            continue
        else:
            kept.append(token)
        at += 1
    tokens = kept

    used = {}
    placements = []
    for index, token in enumerate(tokens):
        if token.type == "heading_open":
            base = fragment(tokens[index + 1].content)
            repeat = used.get(base, 0)
            used[base] = repeat + 1
            token.attrSet("id", f"{base}-{repeat}" if repeat else base)
        if token.type != "inline":
            continue
        first = rewrite_inline(page, token)
        if first:
            placements.append((index, first))

    # Each group of photos first named in one inline token goes after its
    # paragraph, or, outside one, after the top-level block holding it;
    # from the last back, so the indexes before stay put.
    shown = []
    for index, photos in reversed(placements):
        after = index + 1
        if tokens[after].type != "paragraph_close":
            while not (tokens[after].nesting == -1 and tokens[after].level == 0):
                after += 1
        tokens.insert(after + 1, raw(figures(md, photos)))
        shown[:0] = [path for path, _ in photos]
    return tokens, shown


def rewrite_inline(page, inline):
    """One inline token's children rewritten; returns the photos it names
    first, each with its caption's Markdown."""
    children = []
    in_link = 0
    first = []
    for at, child in enumerate(inline.children):
        if child.type == "html_inline":
            if not child.content.startswith("<!--"):
                fail(f"{page.where()} has raw HTML other than a comment: {child.content!r}")
            continue
        if child.type in ("link_open", "image"):
            attr = "href" if child.type == "link_open" else "src"
            target = child.attrGet(attr)
            if target == page.url:
                fail(f"{page.where()} links its own page, {target}, other than in a paragraph of its own")
            if child.type == "image":
                child.attrSet(attr, page_image(page, target))
            else:
                child.attrSet(attr, page.link(target))
        if child.type == "link_open":
            in_link += 1
        elif child.type == "link_close":
            in_link -= 1
        elif child.type == "strong_open" and at + 1 < len(inline.children):
            word = inline.children[at + 1].content.lower()
            if word in MARKS:
                child.attrSet("class", f"mark-{word}")
        elif child.type == "code_inline":
            photo = page.photo(child.content)
            if photo is not None and photo not in page.photo_captions:
                markup = f"`{child.content}`"
                page.photo_captions[photo] = caption(inline.content, inline.content.find(markup))
                first.append((photo, page.photo_captions[photo]))
            address = None if in_link else page.code_link(child.content)
            if address is not None:
                opening = Token("link_open", "a", 1)
                opening.attrSet("href", address)
                children.extend((opening, child, Token("link_close", "a", -1)))
                continue
        children.append(child)
    inline.children = children
    return first


def page_image(page, target):
    """A Markdown image's src: a URL left be, a file under docs/ copied
    beside the page later, at its path there."""
    if SCHEME.match(target):
        return target
    path = page.resolve(target)
    if path is None or not path.is_file() or not path.is_relative_to(DOCS):
        fail(f"{page.where()} shows {target}, which is not a file under docs/")
    page.images.add(path)
    return path.relative_to(DOCS).as_posix()


def figures(md, photos):
    """A group of photos as figures, each linked to its full size."""
    out = ['<div class="photos">']
    for path, words in photos:
        width, height = jpeg_size(path)
        source = html.escape(path.relative_to(DOCS).as_posix())
        name = html.escape(path.name)
        text = md.renderInline(words) if words else ""
        label = f"{text} · <code>{name}</code>" if text else f"<code>{name}</code>"
        alt = html.escape(re.sub(r"<[^>]+>", "", text) or path.name)
        out.append(f'<figure id="{photo_id(path)}"><a href="{source}">'
                   f'<img src="{source}" width="{width}" height="{height}" loading="lazy" alt="{alt}"></a>'
                   f"<figcaption>{label}</figcaption></figure>")
    out.append("</div>")
    return "".join(out)


def sections(md, tokens, env):
    """The page's body: the header, up to the first second-level heading,
    then a section per second-level heading, each table in a box of its
    own, which scrolls sideways at a phone's width (tools/check-pages.sh
    allows it there)."""
    chunks = [[]]
    for token in tokens:
        if token.type == "heading_open" and heading_level(token) == 2:
            chunks.append([])
        if token.type == "table_open":
            chunks[-1].append(raw('<div class="wide" data-scroll="x">'))
        chunks[-1].append(token)
        if token.type == "table_close":
            chunks[-1].append(raw("</div>"))
    out = ["<header>", md.renderer.render(chunks[0], md.options, env).rstrip(), "</header>"]
    for chunk in chunks[1:]:
        out += ["<section>", md.renderer.render(chunk, md.options, env).rstrip(), "</section>"]
    return out


def raw(text):
    """A token that renders as the HTML given."""
    token = Token("html_block", "", 0)
    token.content = text + "\n"
    return token


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--group", required=True, help="the page's group, a key of tools/pages_index.py's GROUPS")
    parser.add_argument("--description", required=True, help="the page's description meta tag")
    parser.add_argument("--photos", type=Path, help="a directory under docs/ of the photos the document names")
    parser.add_argument("--require-link", action="append", default=[], metavar="URL",
                        help="a URL the page must link to")
    parser.add_argument("source", type=Path, help="the Markdown document, in the repository")
    parser.add_argument("out", type=Path, help="the page to write")
    args = parser.parse_args()

    source = args.source.resolve()
    photos = None
    if args.photos is not None:
        photos = args.photos.resolve()
        if not photos.is_dir() or not photos.is_relative_to(DOCS):
            fail(f"{args.photos} is not a directory under docs/")
    md = MarkdownIt("commonmark", {"html": True}).enable("table")
    page = Page(source, photos, f"{PAGES_URL}{args.out.name}")
    env = {}
    tokens = md.parse(source.read_text(encoding="utf-8"), env)
    if not tokens or tokens[0].type != "heading_open" or heading_level(tokens[0]) != 1:
        fail(f"{page.where()} does not start with a first-level heading")
    title = re.sub(r"[`*]", "", tokens[1].content)
    tokens, shown = render(md, page, tokens)

    if photos is not None:
        missing = sorted(p.name for p in photos.iterdir()
                         if p.suffix.lower() in PHOTO_SUFFIXES and p not in page.photo_captions)
        if missing:
            fail(f"{page.where()} never names {', '.join(missing)}, in {photos.relative_to(ROOT)}")

    relative = source.relative_to(ROOT).as_posix()
    lines = head(title, args.description, args.group,
                 f"tools/markdown_page.py from {relative}", CSS)
    lines += ["<body>", "<main>"]
    lines += sections(md, tokens, env)
    lines.append(f'<footer>Generated by <a href="{BLOB}tools/markdown_page.py"><code>tools/markdown_page.py</code></a> '
                 f'from <a href="{BLOB}{relative}"><code>{relative}</code></a>, its source.</footer>')
    lines += ["</main>", "</body>", "</html>"]
    text = "\n".join(lines) + "\n"

    for url in args.require_link:
        if f'href="{html.escape(url)}"' not in text:
            fail(f"{page.where()} renders no link to {url}")

    args.out.write_text(text, encoding="utf-8")
    for path in [*shown, *sorted(page.images)]:
        copy = args.out.parent / path.relative_to(DOCS)
        copy.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(path, copy)


if __name__ == "__main__":
    main()
