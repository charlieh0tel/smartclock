"""Checks the table of an image's candidate bases in its document --
how many of the image's functions each base's diff carries -- against
image_diff.py's TSVs, or with --write rewrites it in place.

    python3 -I tools/firmware/base_figures.py [--write]

Each document of DOCUMENTS has one table, between the markers

    <!-- base_figures.py -->
    <!-- end base_figures.py -->

a row for each TSV of its list and region: the image's functions,
matched or new (each function of the image once: image_diff pairs a
function of the other image with at most one base function), and how
they match.  Carried is identical or relocated, with changed adding
those changed at seed_names.THRESHOLD or more; each as a share of the
functions and of their bytes.  Exits nonzero on drift.
"""

import argparse
import collections
import importlib.util
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
DIFF = ROOT / 'docs/firmware/diff'
spec = importlib.util.spec_from_file_location('diff_figures', ROOT / 'tools/firmware/diff_figures.py')
diff_figures = importlib.util.module_from_spec(spec)
spec.loader.exec_module(diff_figures)
spec = importlib.util.spec_from_file_location('seed_names', ROOT / 'tools/firmware/seed_names.py')
seed_names = importlib.util.module_from_spec(spec)
spec.loader.exec_module(seed_names)

# each document with its table's diffs, IMAGE-vs-BASE, in its rows' order
DOCUMENTS = {
    'docs/firmware/z3815a-4010.md': ('z3815a-4010-vs-58503a-3704', 'z3815a-4010-vs-z3805a-3543b'),
    'docs/firmware/z3817a-3752.md': ('z3817a-3752-vs-z3815a-4010', 'z3817a-3752-vs-58503a-3704',
                                     'z3817a-3752-vs-z3805a-3543b'),
}
REGIONS = ('primary', 'installer')
MARKER = re.compile(r'^<!-- base_figures\.py -->\n.*?^<!-- end base_figures\.py -->$', re.M | re.S)
BYTES_PER_KB = 1024
HEADER = ('| Base | Functions | Identical | Relocated | Changed ≥ %.2f | Changed < %.2f | New | Carried '
          '| With changed |' % (seed_names.THRESHOLD, seed_names.THRESHOLD))
RULE = '| ---- | ----: | ----: | ----: | ----: | ----: | ----: | ----: | ----: |'


def kind(row):
    """A matched or new function's class in the table."""
    if row['status'] != 'changed':
        return row['status']
    return 'changed' if float(row['similarity']) >= seed_names.THRESHOLD else 'unlike'


def share(part, whole):
    return '%.1f %%' % (100 * part / whole)


def table_row(base, region, rows):
    """A region's row: the image's functions in the diff by class, with
    their bytes."""
    count, size = collections.Counter(), collections.Counter()
    seen = set()
    for row in rows:
        if row['region'] != region or row['kind'] != 'function' or row['status'] == 'missing':
            continue
        if row['other_address'] in seen:
            sys.exit('%s: %s is matched twice' % (base, row['other_address']))
        seen.add(row['other_address'])
        count[kind(row)] += 1
        size[kind(row)] += int(row['other_size'])
    functions, total = sum(count.values()), sum(size.values())
    carried = count['identical'] + count['relocated']
    carried_bytes = size['identical'] + size['relocated']
    return '| %s, %s | %d | %d | %d | %d | %d | %d (%d KB) | %s, %s of bytes | %s, %s |' % (
        base, region, functions, count['identical'], count['relocated'], count['changed'], count['unlike'],
        count['new'], round(size['new'] / BYTES_PER_KB), share(carried, functions), share(carried_bytes, total),
        share(carried + count['changed'], functions), share(carried_bytes + size['changed'], total))


def table(diffs):
    lines = ['<!-- base_figures.py -->', HEADER, RULE]
    for name in diffs:
        rows = diff_figures.read_rows(DIFF / (name + '.tsv'))
        base = ' '.join(seed_names.IMAGE_NAMES[name.split('-vs-')[1]])
        lines += [table_row(base, region, rows) for region in REGIONS]
    return '\n'.join(lines + ['<!-- end base_figures.py -->'])


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--write', action='store_true')
    args = parser.parse_args()
    drift = False
    for document, diffs in DOCUMENTS.items():
        path = ROOT / document
        text = path.read_text()
        found = MARKER.findall(text)
        if len(found) != 1:
            sys.exit('%s: %d base tables' % (document, len(found)))
        want = table(diffs)
        if found[0] == want:
            continue
        drift = True
        if args.write:
            path.write_text(text.replace(found[0], want))
        else:
            print('%s: the base table is not what the diffs give; make image-diff rewrites it' % document)
    return 1 if drift and not args.write else 0


if __name__ == '__main__':
    sys.exit(main())
