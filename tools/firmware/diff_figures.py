"""Checks docs/firmware/58503a-3704.md's figures -- the counts table, the
pieces, tables and labels matched, the share of functions carried, the
changed functions of each subject, what a port of the hand-written C
would cost -- against image_diff.py's TSV,
docs/firmware/diff/58503a-3704-vs-z3805a-3543b.tsv, or with --write
rewrites them in place.

    python3 -I tools/firmware/diff_figures.py DIFF.tsv --functions SIDECAR [--write | --by-file]

SIDECAR is the base image's .conventions.jsonl, which image_diff.py
read, whose functions with a "rewritten" file are the functions written
by hand; --by-file prints how they matched for each file of
rewritten/ instead.  Exits nonzero on drift, or if a figure's sentence
is not in the document exactly once.
"""

import argparse
import collections
import json
import pathlib
import re
import statistics
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
DOCUMENT = ROOT / 'docs/firmware/58503a-3704.md'

# image_diff.py's statuses, in the counts table's order
STATUSES = ('identical', 'relocated', 'changed', 'missing', 'new')
# modules the table counts as one, by their first part
GROUPED = ('platform', 'services', 'unassigned')
# a changed function at this similarity has the same instructions over
# other immediates and displacements
SAME_SHAPE = '1.000'
# the statuses of a function written by hand, in --by-file's order
REWRITTEN_STATUSES = ('identical', 'relocated', 'same shape', 'changed', 'missing')
BYTES_PER_KB = 1024
# the counts table: its header, its rule and its rows up to a blank line
TABLE = re.compile(r'^\| Module \|.*\n\|[- |:]+\|\n(?:\|.*\n)+', re.M)
# the subjects of "What changed in what both have": each one's bold
# text and the modules.tsv paths whose changed primary functions it
# counts
SUBJECTS = (
    ('`pll`', ('applications/pll',)),
    ('`gps`', ('applications/gps',)),
    ('`scpi`', ('applications/scpi',)),
    ('`services/status`, `log`, `monitors`, `nv`, `records`',
     ('services/status', 'services/log', 'services/monitors', 'services/nv', 'services/records')),
    ('`screen`', ('applications/screen',)),
)
# a figure in a template below
NUMBER = '#'
# a template's figure: digits, a hexadecimal value, or "none" for zero
FIGURE = r'(none|0x[0-9a-f]+|\d+)'


def read_rows(path):
    """image_diff.py's TSV as dictionaries by its header's columns."""
    lines = [line.rstrip('\n').split('\t') for line in open(path)
             if not line.startswith('#')]
    header, rows = lines[0], lines[1:]
    return [dict(zip(header, row)) for row in rows]


def module_group(module):
    parts = module.split('/')
    return parts[0] + '/*' if parts[0] in GROUPED else '/'.join(parts[:2])


def counts_table(rows):
    """The counts table's text: each module's functions by status, the
    primary's in all, then the installer's."""
    by_group = collections.defaultdict(collections.Counter)
    for row in rows:
        if row['kind'] != 'function':
            continue
        if row['region'] == 'installer':
            by_group['installer'][row['status']] += 1
        else:
            by_group[module_group(row['module'])][row['status']] += 1
            by_group['all'][row['status']] += 1
    lines = ['| Module | ' + ' | '.join(s.capitalize() for s in STATUSES) + ' |',
             '| ------ |' + ' ----: |' * len(STATUSES)]
    for group in sorted(g for g in by_group if g not in ('all', 'installer')):
        lines.append('| `%s` | ' % group + ' | '.join(str(by_group[group][s]) for s in STATUSES) + ' |')
    lines.append('| primary, all | ' + ' | '.join('**%d**' % by_group['all'][s] for s in STATUSES) + ' |')
    lines.append('| `installer` | ' + ' | '.join(str(by_group['installer'][s]) for s in STATUSES) + ' |')
    return '\n'.join(lines) + '\n', by_group


def tally(rows, region, kind):
    return collections.Counter(r['status'] for r in rows if r['region'] == region and r['kind'] == kind)


def in_paths(module, paths):
    return any(module == path or module.startswith(path + '/') for path in paths)


def subject_counts(rows):
    """Each subject's template with its count of changed functions."""
    changed = [r['module'] for r in rows
               if r['region'] == 'primary' and r['kind'] == 'function' and r['status'] == 'changed']
    return [('**%s** (#)' % text, [sum(in_paths(m, paths) for m in changed)]) for text, paths in SUBJECTS]


def rewritten_statuses(sidecar, rows):
    """How the functions written by hand matched, by their file: each
    one's status, a changed one of the same shape apart."""
    status = {}
    for row in rows:
        if row['kind'] == 'function':
            same_shape = row['status'] == 'changed' and row['similarity'] == SAME_SHAPE
            status[row['name']] = 'same shape' if same_shape else row['status']
    found = collections.defaultdict(collections.Counter)
    for line in open(sidecar):
        function = json.loads(line)
        if 'rewritten' not in function:
            continue
        name = function['name']
        if name not in status:
            sys.exit(f'{sidecar}: {name} is not in the diff; run make image-diff')
        found[function['rewritten']][status[name]] += 1
    return found


def figures(rows, sidecar):
    """Each template of the document's sentences with its figures."""
    _, by_group = counts_table(rows)
    primary = by_group['all']
    platform = by_group['platform/*']
    pieces, installer_pieces = tally(rows, 'primary', 'piece'), tally(rows, 'installer', 'piece')
    tables, installer_tables = tally(rows, 'primary', 'label'), tally(rows, 'installer', 'label')
    labels = tally(rows, 'primary', 'global')
    moves = [int(r['other_address'], 16) - int(r['base_address'], 16) for r in rows
             if r['region'] == 'primary' and r['kind'] == 'global' and r['status'] == 'relocated']
    named = sum(primary[s] for s in STATUSES if s != 'new')
    carried = primary['identical'] + primary['relocated']
    platform_named = sum(platform[s] for s in STATUSES if s != 'new')
    new_bytes = sum(int(r['other_size']) for r in rows
                    if r['region'] == 'primary' and r['kind'] == 'function' and r['status'] == 'new')
    rewritten = sum(rewritten_statuses(sidecar, rows).values(), collections.Counter())
    sidecar_functions = sum(1 for line in open(sidecar) if line.strip())
    return [
        ('the `.conventions.jsonl` sidecar, # functions', [sidecar_functions]),
        ('the platform layer is # % identical or relocated',
         [round(100 * (platform['identical'] + platform['relocated']) / platform_named)]),
        ('code no named function covers: # identical, # relocated, # changed, # missing in the '
         'primary; in the installer #, #, # and #.',
         [pieces[s] for s in STATUSES[:4]] + [installer_pieces[s] for s in STATUSES[:4]]),
        ("the names file's typed labels: # identical, # relocated, # changed and # not placed",
         [tables[s] for s in STATUSES[:4]]),
        ('in the installer #, #, # and #. *RAM labels*: # at the same address, # elsewhere',
         [installer_tables[s] for s in STATUSES[:4]] + [labels['identical'], labels['relocated']]),
        ('(median #)', ['%#x' % round(statistics.median(moves))]),
        ("In all, # of the Z3805A's # named primary functions, # %, are in the 58503A as they "
         "stand, # changed and # not at all; the 58503A adds # functions of about # KB.",
         [carried, named, round(100 * carried / named), primary['changed'], primary['missing'],
          primary['new'], round(new_bytes / BYTES_PER_KB)]),
        ('Of its # functions # are identical and # relocated',
         [sum(rewritten.values()), rewritten['identical'], rewritten['relocated']]),
        ("the 58503A's addresses; # have the same instructions", [rewritten['same shape']]),
        ('not the bodies; # changed; and # have no counterpart',
         [rewritten['changed'], rewritten['missing']]),
        ("the # new functions and the # missing pieces' and # changed pieces' successors",
         [primary['new'], pieces['missing'], pieces['changed']]),
        ('The # typed labels not placed, and the # missing functions',
         [tables['missing'] + installer_tables['missing'], primary['missing']]),
    ] + subject_counts(rows)


def pattern(template):
    """A template as a regular expression: its figures groups, its
    spaces any run of white space, as the document wraps."""
    pieces = [re.escape(part) for part in template.split(NUMBER)]
    return re.compile(FIGURE.join(r'\s+'.join(p.split(r'\ ')) for p in pieces))


def spelled(value):
    return 'none' if value == 0 else str(value)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('diff', type=pathlib.Path)
    parser.add_argument('--functions', required=True, type=pathlib.Path)
    group = parser.add_mutually_exclusive_group()
    group.add_argument('--write', action='store_true')
    group.add_argument('--by-file', action='store_true',
                       help="print the functions written by hand by file and status")
    args = parser.parse_args()
    rows = read_rows(args.diff)
    if args.by_file:
        print('file', *(s.replace(' ', '-') for s in REWRITTEN_STATUSES), sep='\t')
        for file, found in sorted(rewritten_statuses(args.functions, rows).items()):
            print(file, *(found[s] for s in REWRITTEN_STATUSES), sep='\t')
        return 0
    text = DOCUMENT.read_text()
    drift = []
    table, _ = counts_table(rows)
    found = TABLE.findall(text)
    if len(found) != 1:
        sys.exit(f'{DOCUMENT}: {len(found)} counts tables')
    if found[0] != table:
        drift.append('the counts table')
        text = text.replace(found[0], table)
    for template, values in figures(rows, args.functions):
        matches = list(pattern(template).finditer(text))
        if len(matches) != 1:
            sys.exit(f'{DOCUMENT}: "{template}" found {len(matches)} times')
        match = matches[0]
        want = [spelled(v) if isinstance(v, int) else v for v in values]
        have = list(match.groups())
        if have == want:
            continue
        drift.append(f'"{template}": {", ".join(have)}; the diff gives {", ".join(want)}')
        pieces, last = [], match.start()
        for index, value in enumerate(want, 1):
            start, end = match.span(index)
            pieces.append(text[last:start] + value)
            last = end
        text = text[:match.start()] + ''.join(pieces) + text[last:]
    if not drift:
        return 0
    if args.write:
        DOCUMENT.write_text(text)
        return 0
    print(f'{DOCUMENT}:', *drift, sep='\n  ')
    return 1


if __name__ == '__main__':
    sys.exit(main())
