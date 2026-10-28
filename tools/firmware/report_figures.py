"""Checks the documents' figures from an image's function check against
its saved report, the output of make report-decompiled, or with --write
rewrites them in place: how many of the functions agree, are known, are
skipped and fail, the primary's and the installer's; how many run in
user mode alone and in both modes; how many of the pForth console
sessions agree; and how many of the agreeing functions' cases are
compared.  With them, the figures of the export the report ran,
third_party/firmware/decompiled/IMAGE/IMAGE.c, and its sidecar: how
many functions there are, the primary's and the installer's; how many
of each are written by hand and how many run as the decompiler's
reading; and how many are assembly routines and thunks.  And the
figures of the image's link check, link_check.py's summary line: how
many functions it requires, and how many the hand-written C, the
assembly and another routine's name define.  CHECKS lists, for each
image, the sentences that quote them.

    python3 -I tools/firmware/report_figures.py --image IMAGE --link SUMMARY REPORT [--write]

make report-decompiled saves an image's report in
target/firmware/IMAGE/report-decompiled.txt, and make check-link its
summary in link-check.txt beside it.  Exits nonzero on drift, if the
report or the summary lacks the figures, if the summary is not the
export's, if a sentence is not in its document exactly once, or if any
function failed, which no document's figures may say.
"""

import argparse
import collections
import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
DECOMPILED = ROOT / 'third_party/firmware/decompiled'
README = ROOT / 'docs/firmware/README.md'
REWRITE = ROOT / 'docs/firmware/rewrite.md'
SUMMARY = ROOT / 'README.md'
DOCUMENT_58503A = ROOT / 'docs/firmware/58503a-3704.md'
DOCUMENT_Z3801A = ROOT / 'docs/firmware/z3801a-3543.md'

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
# link_check.py's summary line: the image, the functions required, and
# those the hand-written C, the assembly and another's name define
LINK = re.compile(
    r"^(\S+): functions required: (\d+), all defined: (\d+) hand-written C, "
    r"(\d+) assembly, (\d+) another routine's name$", re.M)
# the export's line before each function: its address, the installer's
# prefixed
FUNCTION_START = re.compile(r'^/\* ((installer::)?[0-9a-f]{8}) \*/$', re.M)
# the export's line in place of a function written by hand
HAND_WRITTEN = re.compile(r'^/\* \w+: rewritten in \S+ \*/$', re.M)
# the line opening an assembly routine's reading, kept beside it
ASSEMBLY = "#if 0 /* the decompiler's reading; the routine is the assembly after it */"
# a thunk's line, naming its target
THUNK = '/* a thunk to '
# the forms of an export's function: written by hand, an assembly
# routine, a thunk, or the decompiler's reading, compiled as it stands
FORMS = ('hand', 'assembly', 'thunk', 'reading')
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


def link_sentence(image):
    """rewrite.md's sentence of an image's link check."""
    return (REWRITE, sentence(f"The {image}'s link requires all {{}} of its functions and finds "
                              "each defined: {} by hand-written C, {} by assembly and {} as another "
                              "routine's name"),
            lambda f: (f['link_required'], f['link_hand'], f['link_assembly'], f['link_named']))


def summary_row(image):
    """README.md's table row of an image's functions written by hand,
    the primary's and the installer's, and its assembly routines."""
    return (SUMMARY, sentence(f'| {image} | {{}} and {{}} | {{}} |'),
            lambda f: (f['primary_hand'], f['installer_hand'], f['assembly']))


# Each image's sentences: the document, the sentence, and its figures
# from the report's, in the sentence's order.  A report's figures are
# keyed primary_ and installer_ agree, skipped, known, failed, user and
# both; sessions_agree and sessions_run; cases_run and cases_all, which
# sum the agreeing functions' of both profiles.  The export's are keyed
# primary_ and installer_ functions and each of FORMS but assembly and
# thunk, which are both regions'; functions, both regions', and
# decompiled, those not thunks.  The link check's are keyed link_
# required, hand, assembly and named.
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
        (REWRITE, sentence("Hand-written: {} of the primary's functions and {} of the installer's"),
         lambda f: (f['primary_hand'], f['installer_hand'])),
        (REWRITE, sentence("byte for byte: {} routines -- the pSOS kernel's"),
         lambda f: (f['assembly'],)),
        (REWRITE, sentence("Left as the decompiler's: {} thunks, each a"),
         lambda f: (f['thunk'],)),
        (REWRITE, sentence('| assembly (`check-decompiled`) | {} routines in'),
         lambda f: (f['assembly'],)),
        summary_row('Z3805A 3543B'),
        link_sentence('Z3805A 3543B'),
        (README, sentence('all {} routines, in'), lambda f: (f['assembly'],)),
        (README, sentence("this gives {} functions, {} the primary's and {} the installer's, "
                          "and every one but the {} thunks decompiles"),
         lambda f: (f['functions'], f['primary_functions'], f['installer_functions'],
                    f['thunk'])),
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
        (DOCUMENT_58503A, sentence('It decompiles {} functions, none failing'),
         lambda f: (f['decompiled'],)),
        (DOCUMENT_58503A, sentence(
            "In all, {} of the primary's C functions and {} of the installer's are written "
            "by hand, and {} of either is the decompiler's reading"),
         lambda f: (f['primary_hand'], f['installer_hand'],
                    word(f['primary_reading'] + f['installer_reading']))),
        summary_row('58503A 3704'),
        link_sentence('58503A 3704'),
        (REWRITE, sentence(
            "(its \"The hand-written C\"), {} of the primary's and {} of the installer's, {} "
            "left as the decompiler's reading"),
         lambda f: (f['primary_hand'], f['installer_hand'],
                    word(f['primary_reading'] + f['installer_reading']))),
    ),
    'z3801a-3543': (
        (DOCUMENT_Z3801A, sentence('| primary | {} | {} | {} | {} |'),
         lambda f: (f['primary_agree'], f['primary_known'], f['primary_skipped'],
                    f['primary_failed'])),
        (DOCUMENT_Z3801A, sentence('| installer | {} | {} | {} | {} |'),
         lambda f: (f['installer_agree'], f['installer_known'],
                    f['installer_skipped'], f['installer_failed'])),
        (DOCUMENT_Z3801A, sentence('Console sessions {} of {}.'),
         lambda f: (f['sessions_agree'], f['sessions_run'])),
        (REWRITE, sentence(
            "the primary's {} agree, {} are known and {} skipped, none failed, and the "
            "installer's {} agree, {} skipped; its console sessions agree, {} of {}"),
         lambda f: (f['primary_agree'], f['primary_known'], f['primary_skipped'],
                    f['installer_agree'], f['installer_skipped'], f['sessions_agree'],
                    f['sessions_run'])),
        (DOCUMENT_Z3801A, sentence(
            "Every C function of the image is written by hand: the primary's {} and the "
            "installer's {}, {} left as the decompiler's reading"),
         lambda f: (f['primary_hand'], f['installer_hand'],
                    word(f['primary_reading'] + f['installer_reading']))),
        (DOCUMENT_Z3801A, sentence('| `check-decompiled` | {} routines in'),
         lambda f: (f['assembly'],)),
        (DOCUMENT_Z3801A, sentence(
            "Of its {} thunks, the Z3805A's {} are the exporter's `.set` aliases"),
         lambda f: (f['thunk'], f['link_named'])),
        (DOCUMENT_Z3801A, sentence(
            "so the link counts {} routines of assembly against the export's {}."),
         lambda f: (f['link_assembly'], f['assembly'])),
        summary_row('Z3801A 3543'),
        link_sentence('Z3801A 3543'),
        (REWRITE, sentence(
            "has its C functions written by hand too, the primary's {} and the installer's {}, "
            "{} left as the decompiler's reading"),
         lambda f: (f['primary_hand'], f['installer_hand'],
                    word(f['primary_reading'] + f['installer_reading']))),
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


def export_functions(image):
    """The image's export's functions by address: each one's region,
    primary or installer, and its form, one of FORMS."""
    text = (DECOMPILED / image / f'{image}.c').read_text()
    starts = list(FUNCTION_START.finditer(text))
    functions = {}
    for start, end in zip(starts, starts[1:] + [None]):
        piece = text[start.end():end.start() if end else len(text)]
        if HAND_WRITTEN.search(piece):
            form = 'hand'
        elif ASSEMBLY in piece:
            form = 'assembly'
        elif THUNK in piece:
            form = 'thunk'
        else:
            form = 'reading'
        address = start.group(1)
        functions[address] = ('installer' if start.group(2) else 'primary', form)
    return functions


def export_figures(image):
    """The export's figures, keyed as CHECKS has them; exits if the
    export and its sidecar list other functions."""
    functions = export_functions(image)
    sidecar = DECOMPILED / image / f'{image}.conventions.jsonl'
    listed = {json.loads(line)['address'] for line in open(sidecar) if line.strip()}
    if listed != set(functions):
        sys.exit(f'{sidecar}: not the functions of its export')
    count = collections.Counter(functions.values())
    figures = {f'{region}_{form}': count[region, form]
               for region in ('primary', 'installer') for form in FORMS}
    for region in ('primary', 'installer'):
        figures[f'{region}_functions'] = sum(count[region, form] for form in FORMS)
    for form in ('assembly', 'thunk'):
        figures[form] = figures[f'primary_{form}'] + figures[f'installer_{form}']
    figures['functions'] = len(functions)
    figures['decompiled'] = len(functions) - figures['thunk']
    return figures


def link_figures(path, image, export):
    """The link check's figures, keyed as CHECKS has them; exits if the
    summary lacks them or is not of the image's export: another image's,
    other functions required, or other functions hand-written."""
    found = LINK.search(path.read_text()) if path.exists() else None
    if found is None:
        sys.exit(f'{path}: no link check summary; run make check-link')
    if found.group(1) != image:
        sys.exit(f'{path}: the summary of {found.group(1)}, not {image}')
    figures = dict(zip(('link_required', 'link_hand', 'link_assembly', 'link_named'),
                       map(int, found.groups()[1:])))
    if figures['link_required'] != export['functions']:
        sys.exit(f"{path}: {figures['link_required']} functions required, "
                 f"the export's {export['functions']}")
    if figures['link_hand'] != export['primary_hand'] + export['installer_hand']:
        sys.exit(f"{path}: {figures['link_hand']} functions of hand-written C, the export "
                 f"marks {export['primary_hand'] + export['installer_hand']}")
    return figures


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--image', required=True, choices=sorted(CHECKS))
    parser.add_argument('--link', required=True, type=pathlib.Path)
    parser.add_argument('report', type=pathlib.Path)
    parser.add_argument('--write', action='store_true')
    args = parser.parse_args()
    export = export_figures(args.image)
    figures = report_figures(args.report) | export | link_figures(args.link, args.image, export)
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
