"""Checks docs/firmware/README.md's figures from the function check's
report -- how many of the functions run, the primary's and the
installer's, run in user mode alone and in both modes -- against a saved
report, the output of make report-decompiled, or with --write rewrites
them in place.

    python3 -I tools/firmware/report_figures.py REPORT [--write]

make report-decompiled saves its report in target/report-decompiled.txt.
Exits nonzero on drift, or if the report lacks the figures.
"""

import argparse
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
DOCUMENT = ROOT / 'docs/firmware/README.md'

# function_check.py's line of a profile's figures, the installer's
# labeled with its overlay
FIGURES = re.compile(
    r'^(installer )?functions run in user mode alone: (\d+), '
    r'both modes: (\d+)', re.M)
# the README's sentence, which may wrap anywhere between its words
SENTENCE = re.compile(
    r'Of the functions run,\s+(\d+)\s+of the primary\'s and\s+(\d+)\s+of'
    r'\s+the installer\'s run in user mode alone, and\s+(\d+)\s+and\s+'
    r'(\d+)\s+in both\b')


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('report', type=pathlib.Path)
    parser.add_argument('--write', action='store_true')
    args = parser.parse_args()
    found = {bool(label): (user, both)
             for label, user, both in FIGURES.findall(args.report.read_text())}
    if set(found) != {False, True}:
        sys.exit(f'{args.report}: no figures for both images; '
                 'run make report-decompiled')
    text = DOCUMENT.read_text()
    match = SENTENCE.search(text)
    if match is None:
        sys.exit(f'{DOCUMENT}: no sentence of the figures')
    want = (found[False][0], found[True][0], found[False][1], found[True][1])
    have = match.groups()
    if have == want:
        return 0
    if not args.write:
        print(f'{DOCUMENT}: user mode alone {have[0]} and {have[1]}, both '
              f'{have[2]} and {have[3]}; the report gives {want[0]} and '
              f'{want[1]}, {want[2]} and {want[3]}')
        return 1
    pieces, last = [], match.start()
    for index, new in enumerate(want, 1):
        start, end = match.span(index)
        pieces.append(text[last:start] + new)
        last = end
    DOCUMENT.write_text(text[:match.start()] + ''.join(pieces) + text[last:])
    return 0


if __name__ == '__main__':
    sys.exit(main())
