"""Checks the documents' figures from the function check's report
against a saved report, the output of make report-decompiled, or with
--write rewrites them in place: docs/firmware/README.md's -- how many
of the functions run, the primary's and the installer's, run in user
mode alone and in both modes, and how many of the pForth console
sessions agree -- and the headline counts, how many of each image's
functions agree, are known, are skipped and fail, and how many of the
agreeing functions' cases are compared, that docs/firmware/rewrite.md
and README.md give.

    python3 -I tools/firmware/report_figures.py REPORT [--write]

make report-decompiled saves an image's report in
target/firmware/IMAGE/report-decompiled.txt.
Exits nonzero on drift, if the report lacks the figures, or if any
function failed, which no document's figures may say.
"""

import argparse
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
DOCUMENT = ROOT / 'docs/firmware/README.md'
REWRITE = ROOT / 'docs/firmware/rewrite.md'
SUMMARY = ROOT / 'README.md'

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
# function_check.py's line of a profile's counts
COUNTS = re.compile(
    r'^(installer )?functions agreeing: (\d+), skipped: (\d+), '
    r'known: (\d+), failed: (\d+);', re.M)
# rewrite.md's table rows of the counts, an image's each, in the order
# agreeing, known, skipped, failed
ROW = r'\|\s+function check, {}\s+\|\s+(\d+) agree, (\d+) known, (\d+) skipped, (\d+) failed\s+\|'
ROWS = {False: re.compile(ROW.format('primary')),
        True: re.compile(ROW.format('installer'))}
# rewrite.md's summary of them: the primary's agreeing and known and
# none failing, the installer's agreeing
HEADLINE = re.compile(
    r'(\d+)\s+of\s+the primary\'s functions agree,\s+(\d+)\s+differ for'
    r'\s+known reasons,\s+none\s+fail;\s+the installer\'s\s+(\d+)\s+agree\b')
# function_check.py's line of an agreeing function's cases, those run
# and all, and rewrite.md's row of their sums over both images, written
# with thousands' commas
CASES = re.compile(r'^ok \S+: (\d+) of (\d+) cases run$', re.M)
CASES_ROW = re.compile(
    r'\|\s+cases, in the agreeing functions\s+\|\s+([\d,]+) of ([\d,]+) compared\b')
# README.md's, in words: the known of both images, then the skipped
SUMMARY_SENTENCE = re.compile(
    r'all agree but\s+(\w+)\s+whose\s+difference is the firmware\'s own or'
    r'\s+one C cannot express, and\s+(\w+)\s+the\s+harness cannot run')
# the numbers README.md's sentence spells out
WORDS = ('none', 'one', 'two', 'three', 'four', 'five', 'six', 'seven',
         'eight', 'nine', 'ten', 'eleven', 'twelve')


def word(count):
    """A count as README.md's sentence spells it, digits past WORDS."""
    return WORDS[count] if count < len(WORDS) else str(count)


def check(text, document, sentence, want, what, write):
    """The text with its sentence's figures want, or None if they are;
    exits if the sentence is not there, and with write False reports a
    drift."""
    match = sentence.search(text)
    if match is None:
        sys.exit(f'{document}: no sentence of the {what}')
    have = match.groups()
    if have == want:
        return None
    if not write:
        print(f'{document}: the {what} are {", ".join(have)}; the report '
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
    counts = {bool(label): tuple(map(int, rest))
              for label, *rest in COUNTS.findall(report)}
    sessions = SESSIONS.search(report)
    if set(found) != {False, True} or set(counts) != {False, True} \
            or sessions is None:
        sys.exit(f'{args.report}: no figures for both images; '
                 'run make report-decompiled')
    failed = {installer: c[3] for installer, c in counts.items() if c[3]}
    if failed:
        sys.exit(f'{args.report}: functions failed: '
                 + ', '.join(f'{"installer" if i else "primary"} {n}'
                             for i, n in failed.items()))
    # agreeing, skipped, known, failed
    primary, installer = counts[False], counts[True]
    cases = [tuple(map(int, c)) for c in CASES.findall(report)]
    compared = tuple(f'{sum(c[i] for c in cases):,}' for i in (0, 1))
    checks = {
        DOCUMENT: (
            (SENTENCE, (found[False][0], found[True][0], found[False][1],
                        found[True][1]),
             'user mode alone and both modes figures'),
            (SESSIONS_SENTENCE, (sessions.group(2), sessions.group(1)),
             'console sessions run and agreeing')),
        REWRITE: tuple(
            (ROWS[image], tuple(str(c[i]) for i in (0, 2, 1, 3)),
             f'{"installer" if image else "primary"} function counts')
            for image, c in counts.items()) + (
            (HEADLINE, (str(primary[0]), str(primary[2]), str(installer[0])),
             'agreeing and known functions'),
            (CASES_ROW, compared, 'agreeing functions\' cases compared')),
        SUMMARY: (
            (SUMMARY_SENTENCE, (word(primary[2] + installer[2]),
                                word(primary[1] + installer[1])),
             'known and skipped functions'),),
    }
    drifted = False
    for document, sentences in checks.items():
        text = document.read_text()
        changed_any = False
        for sentence, want, what in sentences:
            changed = check(text, document, sentence, want, what, args.write)
            if changed is not None:
                changed_any = True
                text = changed
        if changed_any and args.write:
            document.write_text(text)
        drifted |= changed_any
    return 1 if drifted and not args.write else 0


if __name__ == '__main__':
    sys.exit(main())
