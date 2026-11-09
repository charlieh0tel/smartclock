"""Copies a hand-written page from docs/pages/ into the generated
pages' directory, with each file it names by a relative src="..."
(an image, say) copied from docs/ to the same path beside it.

    python3 -I tools/pages_copy.py PAGE OUT
"""

import argparse
import re
import shutil
from pathlib import Path

DOCS = Path(__file__).resolve().parent.parent / "docs"
# A src attribute naming a file by a path relative to the page.
RELATIVE_SOURCE = re.compile(r'\bsrc="(?![a-z]+:|/|#)([^"]+)"')


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("page", type=Path, help="the page's source")
    parser.add_argument("out", type=Path, help="the pages' directory")
    args = parser.parse_args()
    shutil.copyfile(args.page, args.out / args.page.name)
    for path in RELATIVE_SOURCE.findall(args.page.read_text(encoding="utf-8")):
        copy = args.out / path
        copy.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(DOCS / path, copy)


if __name__ == "__main__":
    main()
