"""Checks that each seeded image's check-layout.c asserts its layouts at
its own addresses, not at its base's, from which it was copied.

    python3 -I tools/firmware/layout_addresses.py [--image IMAGE] [--write]

A size or offset in check-layout.c is often written as the difference
of two addresses in the image, a field's and its record's.  The base's,
the Z3805A 3543B's, are its own.  A seeded image's file began as the
base's copy, so each of its address differences is derived again from
the image itself (docs/firmware/rewrite.md, "Shared C", rule 2):

- an address the base's code names, in an instruction that image_diff
  aligns with one of the image's in a function it carries (identical,
  relocated, or changed at seed_names.THRESHOLD or more), is the
  address the image's instruction names, the most of them agreeing;
  so that a field the image's code reaches elsewhere in its record
  shows as an offset of its own;
- any other address is the same offset into the global holding it, the
  base's export's, at the image's export's address for that global's
  name.

Each of the image's address differences that is the base's, word for
word, under the same type and field, must be what the derivation gives;
one of the image's own, under the same type and field, must be too
where its globals are the base's line's, and is otherwise only resolved:
each address in a global of the image's export.  --write rewrites the
copies to what the derivation gives.  A layout that the derivation
shows to differ then fails make check-types, against the header that
declares it.  Prints each failure; exits nonzero if any.  Needs
m68k-linux-gnu binutils.
"""

import argparse
import bisect
import collections
import importlib.util
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
FIRMWARE = ROOT / 'third_party/firmware'
ANALYSIS = FIRMWARE / 'analysis'


def load(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / 'tools/firmware' / (name + '.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


seed_names = load('seed_names')
image_diff = seed_names.image_diff

# The images check-layout.c is derived for, each from its base's.
BASES = {port.folder: port.base_folder for port in seed_names.PORTS.values()}
# A size or offset check, as common_check.py's CHECK finds it, but for
# any type, an array's or a field's too, with the span of its expression.
CHECK = re.compile(r'CHECK_(SIZE|FIELD)\(\s*([^,;]+?)\s*,\s*(?:(\w+(?:\.\w+|\[\d+\])*)\s*,\s*)?([^;,]*?)\);', re.S)
COMMENT = re.compile(r'/\*.*?\*/|//[^\n]*', re.S)
HEX = re.compile(r'0x[0-9a-fA-F]+')
# A global the export declares at its address; an installer:: one is in
# the installer's overlay, which no check-layout.c address is.
EXTERN = re.compile(r'(?m)^extern [^;]*?\b(\w+)(?:\[[^\]]*\])*; /\* ([0-9a-f]{8}) \*/$')


def layout_file(image):
    return FIRMWARE / 'rewritten' / image / 'types/check-layout.c'


class Check:
    """One check: its kind, type and field, its expression, the span of
    the expression in the file, and the addresses it names."""

    def __init__(self, match):
        self.kind, self.type, self.field = match.group(1), match.group(2), match.group(3)
        self.expression = ' '.join(match.group(4).split())
        self.span = match.span(4)
        self.addresses = [int(h, 16) for h in HEX.findall(self.expression) if is_address(int(h, 16))]

    @property
    def key(self):
        return self.kind, self.type, self.field

    def show(self):
        return 'CHECK_%s(%s, %s%s)' % (self.kind, self.type, self.field + ', ' if self.field else '', self.expression)


def is_address(value):
    return value >= image_diff.LOWEST_ADDRESS and (
        value < image_diff.RAM or image_diff.RAM <= value < image_diff.RAM + image_diff.RAM_SIZE)


def checks(image):
    """The image's checks, comments blanked so that spans stay."""
    text = layout_file(image).read_text()
    blank = COMMENT.sub(lambda m: re.sub(r'[^\n]', ' ', m.group()), text)
    return text, [Check(m) for m in CHECK.finditer(blank)]


class Globals:
    """The globals an image's export declares: by name, and by address
    sorted, for the one holding an address."""

    def __init__(self, image):
        text = (FIRMWARE / 'decompiled' / image / (image + '.c')).read_text()
        self.by_name = {name: int(address, 16) for name, address in EXTERN.findall(text)}
        self.order = sorted((address, name) for name, address in self.by_name.items())
        self.starts = [address for address, _ in self.order]

    def holding(self, address):
        """The global at or before an address, and the offset into it."""
        at = bisect.bisect_right(self.starts, address) - 1
        if at < 0:
            return None, None
        start, name = self.order[at]
        return name, address - start


def votes(seeding):
    """Each base address the carried functions name, to the image's
    addresses their aligned instructions name, counted: seed_names's
    votes, but for every address."""
    found = collections.defaultdict(collections.Counter)
    for region, matcher in seeding.matchers.items():
        for target, other, function, _, _ in matcher.references:
            # the installer's code names its overlay too, which check-layout.c
            # does not; above both overlays it is plain RAM, the primary's
            if region == 'installer' and (target < seeding.overlay_ends[0] or other < seeding.overlay_ends[1]):
                continue
            item = seeding.code.get((region, function))
            if item is not None and seeding.counted(item) and is_address(target) and is_address(other):
                found[target][other] += 1
    return found


class Derivation:
    """The image's address for each of the base's, as the module's
    docstring gives it."""

    def __init__(self, image, base):
        self.globals = Globals(image)
        self.base_globals = Globals(base)
        seeding = seed_names.Seeding(str(ANALYSIS / base), str(ANALYSIS / image))
        self.votes = votes(seeding)
        # where image_diff places a base label the image's export does not
        # name, by the references inside it or its bytes, unless the export
        # names another global there, as the seeding then dropped it
        taken = {address for address, _ in self.globals.order}
        self.placed = {label.address: label.other for (region, _), label in seeding.labels.items()
                       if region == 'primary' and label.other is not None and label.other not in taken}

    def address(self, address):
        """The image's address for a base address, and how, or None and
        why not."""
        counted = self.votes.get(address)
        if counted:
            ranked = counted.most_common(2)
            if len(ranked) > 1 and ranked[0][1] == ranked[1][1]:
                return None, 'its references disagree: %s' % ', '.join('0x%x' % a for a, _ in ranked)
            return ranked[0][0], 'referenced'
        name, offset = self.base_globals.holding(address)
        if name in self.globals.by_name:
            return self.globals.by_name[name] + offset, 'in ' + name
        start = address - offset if name is not None else None
        if start in self.placed:
            return self.placed[start] + offset, 'in %s, placed by image_diff' % name
        return None, 'in no global the image declares or image_diff places (%s)' % name

    def expression(self, check):
        """The base check's expression with the image's addresses, and
        how each was found, or None and why not."""
        mapped, hows = {}, []
        for address in check.addresses:
            other, how = self.address(address)
            if other is None:
                return None, '0x%x %s' % (address, how)
            mapped[address] = other
            hows.append((other, how))

        def replace(match):
            value = int(match.group(), 16)
            return '0x%x' % mapped[value] if value in mapped else match.group()

        return HEX.sub(replace, check.expression), hows


def resolved(globals_, check):
    return tuple(globals_.holding(address) for address in check.addresses)


def check_image(image, write, failures):
    base = BASES[image]
    derivation = Derivation(image, base)
    _, base_checks = checks(base)
    by_key = collections.defaultdict(list)
    for check in base_checks:
        if check.addresses:
            by_key[check.key].append(check)
    text, image_checks = checks(image)
    edits = []
    counts = collections.Counter()
    for check in image_checks:
        if not check.addresses:
            continue
        candidates = by_key.get(check.key, [])
        copy = next((c for c in candidates if c.expression == check.expression), None)
        if copy is None:
            mine = resolved(derivation.globals, check)
            copy = next((c for c in candidates if [n for n, _ in resolved(derivation.base_globals, c)]
                         == [n for n, _ in mine]), None)
            if copy is None:
                missing = [address for address, (name, _) in zip(check.addresses, mine) if name is None]
                counts['own'] += 1
                if missing:
                    failures.append('%s: %s: 0x%x is in no global the image declares'
                                    % (layout_file(image).relative_to(ROOT), check.show(), missing[0]))
                continue
            kind = 'paired'
        else:
            kind = 'copy'
        expected, hows = derivation.expression(copy)
        if expected is None:
            counts[kind + ' underived'] += 1
            if kind == 'paired':
                continue  # the image's own, which nothing here contradicts
            failures.append('%s: %s: not derived: %s' % (layout_file(image).relative_to(ROOT), check.show(), hows))
            continue
        if kind == 'paired':
            # the image's own: only what its code names is evidence against it
            wrong = [(mine, other) for mine, (other, how) in zip(check.addresses, hows)
                     if how == 'referenced' and mine != other]
            counts[kind + (' differs' if wrong else ' agrees')] += 1
            if wrong:
                failures.append('%s: %s: the image\'s code names 0x%x for 0x%x'
                                % (layout_file(image).relative_to(ROOT), check.show(), wrong[0][1], wrong[0][0]))
            continue
        if expected == check.expression:
            counts[kind + ' agrees'] += 1
            continue
        counts[kind + ' differs'] += 1
        if write:
            edits.append((check.span, expected))
            continue
        failures.append('%s: %s: copied from %s; the image\'s addresses give %s'
                        % (layout_file(image).relative_to(ROOT), check.show(), base, expected))
    for (start, end), expected in sorted(edits, reverse=True):
        text = text[:start] + expected + text[end:]
    if edits:
        layout_file(image).write_text(text)
    print('%s: %s' % (image, ', '.join('%s %d' % item for item in sorted(counts.items()))))


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--image', choices=sorted(BASES))
    parser.add_argument('--write', action='store_true')
    arguments = parser.parse_args()
    image_diff.ADDRESS_RANGES[:] = [(image_diff.LOWEST_ADDRESS, image_diff.RAM),
                                    (image_diff.RAM, image_diff.RAM + image_diff.RAM_SIZE)]
    failures = []
    for image in [arguments.image] if arguments.image else sorted(BASES):
        check_image(image, arguments.write, failures)
    for failure in failures:
        print(failure)
    if failures:
        sys.exit('%d failures' % len(failures))


if __name__ == '__main__':
    main()
