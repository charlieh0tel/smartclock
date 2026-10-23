"""Checks docs/firmware/README.md's figures from the function check's
report -- how many of the functions run, the primary's and the
installer's, run in user mode alone and in both modes, and how many of
the pForth console sessions agree -- against a saved report, the output
of make report-decompiled, or with --write rewrites them in place.

    python3 -I tools/firmware/report_figures.py REPORT [--write]

make report-decompiled saves an image's report in
target/firmware/IMAGE/report-decompiled.txt.
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
# function_check.py's line of the primary's console sessions, and the
# README's sentence of them, the sessions run and those agreeing
SESSIONS = re.compile(r'^console sessions agreeing: (\d+) of (\d+)$', re.M)
SESSIONS_SENTENCE = re.compile(
    r'of the\s+(\d+)\s+console sessions,\s+(\d+)\s+agree\b')


def check(text, sentence, want, what, write):
    """The text with its sentence's figures want, or None if they are;
    exits if the sentence is not there, and with write False reports a
    drift."""
    match = sentence.search(text)
    if match is None:
        sys.exit(f'{DOCUMENT}: no sentence of the {what}')
    have = match.groups()
    if have == want:
        return None
    if not write:
        print(f'{DOCUMENT}: the {what} are {", ".join(have)}; the report '
              f'gives {", ".join(want)}')
        return text
    pieces, last = [], match.start()
    for index, new in enumerate(want, 1):
        start, end = match.span(index)
        pieces.append(text[last:start] + new)
        last = end
    return text[:match.start()] + ''.join(pieces) + text[last:]


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('report', type=pathlib.Path)
    parser.add_argument('--write', action='store_true')
    args = parser.parse_args()
    report = args.report.read_text()
    found = {bool(label): (user, both)
             for label, user, both in FIGURES.findall(report)}
    sessions = SESSIONS.search(report)
    if set(found) != {False, True} or sessions is None:
        sys.exit(f'{args.report}: no figures for both images; '
                 'run make report-decompiled')
    text = DOCUMENT.read_text()
    drifted = False
    for sentence, want, what in (
            (SENTENCE, (found[False][0], found[True][0], found[False][1],
                        found[True][1]),
             'user mode alone and both modes figures'),
            (SESSIONS_SENTENCE, (sessions.group(2), sessions.group(1)),
             'console sessions run and agreeing')):
        changed = check(text, sentence, want, what, args.write)
        if changed is not None:
            drifted = True
            text = changed
    if drifted and args.write:
        DOCUMENT.write_text(text)
        return 0
    return 1 if drifted else 0


if __name__ == '__main__':
    sys.exit(main())
