"""Fails if a function is named for an image: if the hand-written C, any
.c or .h under third_party/firmware/rewritten/, or a names file's name
column (third_party/firmware/analysis/IMAGE/names.tsv) names a function
with an image's suffix, _58503a, _z3805a, _z3801a, _z3815a, _58503b or
_z3816a (docs/firmware/rewrite.md, "The rewrite").  A function is named
for what it does; that an image's differs from another's is its names
file note's to say.

    python3 -I tools/firmware/plain_names.py

A C string literal, a comment's text in double quotes, and a file name
(a word with an extension, or a path's) are not names, and an image's
identifier, 58503a-3704, has no underscore before its model.  Exits
nonzero, each name printed with its file and line, if one is found.
"""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
FIRMWARE = ROOT / 'third_party/firmware'
REWRITTEN = FIRMWARE / 'rewritten'
ANALYSIS = FIRMWARE / 'analysis'
# The models' suffixes, as the image folders name them.
MODELS = ('58503a', 'z3805a', 'z3801a', 'z3815a', '58503b', 'z3816a')
# A word ending in a model's suffix; a file name's extension or path, a
# word after it or before it, does not end it.
SUFFIXED = re.compile(r'(?<![\w./-])[A-Za-z_]\w*_(?:%s)\b(?![.\-/]\w)' % '|'.join(MODELS))
# What is quoted, not named: a C string or character literal, and a
# comment's text in double quotes.
STRING = re.compile(r'"(?:[^"\\\n]|\\.)*"|\'(?:[^\'\\\n]|\\.)*\'')
COMMENT = re.compile(r'/\*.*?\*/|//[^\n]*', re.S)
# The kinds of names file row whose name column names a function.
NAMED_KINDS = ('function', 'entry')


def blank(match: re.Match) -> str:
    """The match's text as spaces, its newlines kept, so that a line
    number holds."""
    return re.sub(r'[^\n]', ' ', match.group(0))


def code_names(text: str) -> str:
    """The C text with what is quoted blanked: string and character
    literals outside comments, and double-quoted text inside them."""
    out, at = [], 0
    for comment in COMMENT.finditer(text):
        out.append(STRING.sub(blank, text[at:comment.start()]))
        out.append(STRING.sub(blank, comment.group(0)))
        at = comment.end()
    out.append(STRING.sub(blank, text[at:]))
    return ''.join(out)


def found_in_c(path: pathlib.Path) -> list[tuple[int, str]]:
    text = code_names(path.read_text())
    return [(text.count('\n', 0, match.start()) + 1, match.group(0)) for match in SUFFIXED.finditer(text)]


def found_in_names(path: pathlib.Path) -> list[tuple[int, str]]:
    found = []
    for number, line in enumerate(path.read_text().split('\n'), 1):
        fields = line.split('\t')
        if line.startswith('#') or len(fields) < 3 or fields[1] not in NAMED_KINDS:
            continue
        found += [(number, match.group(0)) for match in SUFFIXED.finditer(fields[2])]
    return found


def main() -> int:
    found = [(path, number, name) for path in sorted(REWRITTEN.rglob('*.[ch]')) for number, name in found_in_c(path)]
    found += [(path, number, name) for path in sorted(ANALYSIS.glob('*/names.tsv'))
              for number, name in found_in_names(path)]
    for path, number, name in found:
        print(f"{path.relative_to(ROOT)}:{number}: {name}, a function named for an image")
    return 1 if found else 0


if __name__ == '__main__':
    sys.exit(main())
