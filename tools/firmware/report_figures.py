"""Checks the documents' figures from an image's function check against
its saved report, the output of make report-decompiled, or with --write
rewrites them in place: how many of the functions agree, are known, are
skipped and fail, the primary's and the installer's; how many run in
user mode alone and in both modes; how many of the pForth console
sessions agree; and how many of the agreeing functions' cases are
compared.  CHECKS lists, for each image, the sentences that quote them.

    python3 -I tools/firmware/report_figures.py --image IMAGE REPORT [--write]

make report-decompiled saves an image's report in
target/firmware/IMAGE/report-decompiled.txt.  Exits nonzero on drift,
if the report lacks the figures, if a sentence is not in its document
exactly once, or if any function failed, which no document's figures
may say.
"""

import argparse
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
README = ROOT / 'docs/firmware/README.md'
REWRITE = ROOT / 'docs/firmware/rewrite.md'
SUMMARY = ROOT / 'README.md'
DOCUMENT_58503A = ROOT / 'docs/firmware/58503a-3704.md'

# function_check.py's line of a profile's counts, the installer's
# labeled with its overlay
COUNTS = re.compile(
    r'^(installer )?functions agreeing: (\d+), skipped: (\d+), '
    r'known: (\d+), failed: (\d+);', re.M)
# its line of a profile's functions run in user mode alone and in both
FIGURES = re.compile(
    r'^(installer )?functions run in user mode alone: (\d+), '
    r'both modes: (\d+)', re.M)
# its line of the primary's console sessions, those agreeing and those run
SESSIONS = re.compile(r'^console sessions agreeing: (\d+) of (\d+)$', re.M)
# its line of an agreeing function's cases, those run and all
CASES = re.compile(r'^ok \S+: (\d+) of (\d+) cases run$', re.M)
# the numbers a sentence spells out
WORDS = ('none', 'one', 'two', 'three', 'four', 'five', 'six', 'seven',
         'eight', 'nine', 'ten', 'eleven', 'twelve')


def word(count):
    """A count as a sentence spells it, digits past WORDS."""
    return WORDS[count] if count < len(WORDS) else str(count)


def sentence(text):
    """A sentence as a regular expression: its figures, written {}, are
    groups of digits, thousands' commas or a word; its spaces any run
    of white space, as the documents wrap."""
    pieces = [''.join(r'\s+' if part.isspace() else re.escape(part)
                      for part in re.split(r'(\s+)', piece) if part)
              for piece in text.split('{}')]
    return re.compile(r'([\w,]+)'.join(pieces))


def counts_row(profile):
    """rewrite.md's table row of a profile's counts."""
    return sentence(f'| function check, {profile} | {{}} agree, {{}} known, '
                    '{} skipped, {} failed |')


# Each image's sentences: the document, the sentence, and its figures
# from the report's, in the sentence's order.  A report's figures are
# keyed primary_ and installer_ agree, skipped, known, failed, user and
# both; sessions_agree and sessions_run; cases_run and cases_all, which
# sum the agreeing functions' of both profiles.
CHECKS = {
    'z3805a-3543b': (
        (README, sentence("Of the functions run, {} of the primary's and {} of "
                          "the installer's run in user mode alone, and {} and {} in both"),
         lambda f: (f['primary_user'], f['installer_user'], f['primary_both'],
                    f['installer_both'])),
        (README, sentence('of the {} console sessions, {} agree'),
         lambda f: (f['sessions_run'], f['sessions_agree'])),
        (REWRITE, counts_row('primary'),
         lambda f: (f['primary_agree'], f['primary_known'], f['primary_skipped'],
                    f['primary_failed'])),
        (REWRITE, counts_row('installer'),
         lambda f: (f['installer_agree'], f['installer_known'],
                    f['installer_skipped'], f['installer_failed'])),
        (REWRITE, sentence("{} of the primary's functions agree, {} differ for known "
                           "reasons, none fail; the installer's {} agree"),
         lambda f: (f['primary_agree'], f['primary_known'], f['installer_agree'])),
        (REWRITE, sentence('| cases, in the agreeing functions | {} of {} compared'),
         lambda f: (f['cases_run'], f['cases_all'])),
        (SUMMARY, sentence("all agree but {} whose difference is the firmware's own "
                           "or one C cannot express, and {} the harness cannot run"),
         lambda f: (word(f['primary_known'] + f['installer_known']),
                    word(f['primary_skipped'] + f['installer_skipped']))),
    ),
    '58503a-3704': (
        (DOCUMENT_58503A, sentence('| primary | {} | {} | {} | {} |'),
         lambda f: (f['primary_agree'], f['primary_known'], f['primary_skipped'],
                    f['primary_failed'])),
        (DOCUMENT_58503A, sentence('| installer | {} | {} | {} | {} |'),
         lambda f: (f['installer_agree'], f['installer_known'],
                    f['installer_skipped'], f['installer_failed'])),
        (DOCUMENT_58503A, sentence(
            "Console sessions {} of {}; of the functions run, {} of the primary's and "
            "{} of the installer's run in user mode alone, and {} and {} in both; {} "
            "of the agreeing functions' {} cases compared"),
         lambda f: (f['sessions_agree'], f['sessions_run'], f['primary_user'],
                    f['installer_user'], f['primary_both'], f['installer_both'],
                    f['cases_run'], f['cases_all'])),
        (DOCUMENT_58503A, sentence("*Known.* The {} are `IMAGE_KNOWN`'s"),
         lambda f: (word(f['primary_known'] + f['installer_known']),)),
        (SUMMARY, sentence(
            "of the primary's functions {} agree and {} differ for the firmware's own "
            "reasons, and the installer's {} agree, {} the harness cannot run; its "
            "console sessions agree, {} of {}"),
         lambda f: (f['primary_agree'], f['primary_known'], f['installer_agree'],
                    word(f['installer_skipped']), f['sessions_agree'],
                    f['sessions_run'])),
        (REWRITE, sentence(
            "the primary's {} agree, {} are known and {} skipped or failed, and the "
            "installer's {} agree, {} skipped; its console sessions agree, {} of {}"),
         lambda f: (f['primary_agree'], f['primary_known'],
                    word(f['primary_skipped'] + f['primary_failed']),
                    f['installer_agree'], f['installer_skipped'], f['sessions_agree'],
                    f['sessions_run'])),
    ),
}


def report_figures(path):
    """The report's figures, keyed as CHECKS has them; exits if it lacks
    any or a function failed."""
    report = path.read_text()
    figures = {}
    for label, *values in COUNTS.findall(report):
        profile = 'installer' if label else 'primary'
        for key, value in zip(('agree', 'skipped', 'known', 'failed'), values):
            figures[f'{profile}_{key}'] = int(value)
    for label, user, both in FIGURES.findall(report):
        profile = 'installer' if label else 'primary'
        figures[f'{profile}_user'], figures[f'{profile}_both'] = int(user), int(both)
    sessions = SESSIONS.search(report)
    if len(figures) != 12 or sessions is None:
        sys.exit(f'{path}: no figures for both profiles; run make report-decompiled')
    figures['sessions_agree'], figures['sessions_run'] = map(int, sessions.groups())
    cases = [tuple(map(int, c)) for c in CASES.findall(report)]
    figures['cases_run'], figures['cases_all'] = (
        f'{sum(c[i] for c in cases):,}' for i in (0, 1))
    failed = [f'{p} {figures[p + "_failed"]}' for p in ('primary', 'installer')
              if figures[p + '_failed']]
    if failed:
        sys.exit(f'{path}: functions failed: ' + ', '.join(failed))
    return figures


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--image', required=True, choices=sorted(CHECKS))
    parser.add_argument('report', type=pathlib.Path)
    parser.add_argument('--write', action='store_true')
    args = parser.parse_args()
    figures = report_figures(args.report)
    texts = {}
    drifted = False
    for document, pattern, want in CHECKS[args.image]:
        text = texts.setdefault(document, document.read_text())
        matches = list(pattern.finditer(text))
        if len(matches) != 1:
            sys.exit(f'{document}: "{pattern.pattern}" found {len(matches)} times')
        match = matches[0]
        want = tuple(map(str, want(figures)))
        have = match.groups()
        if have == want:
            continue
        drifted = True
        print(f'{document}: {", ".join(have)}; the {args.image} report gives '
              f'{", ".join(want)}')
        pieces, last = [], match.start()
        for index, value in enumerate(want, 1):
            start, end = match.span(index)
            pieces.append(text[last:start] + value)
            last = end
        texts[document] = text[:match.start()] + ''.join(pieces) + text[last:]
    if args.write:
        for document, text in texts.items():
            if text != document.read_text():
                document.write_text(text)
        return 0
    return 1 if drifted else 0


if __name__ == '__main__':
    sys.exit(main())
