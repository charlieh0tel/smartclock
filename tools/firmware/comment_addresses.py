"""Checks the addresses an image's hand-written C cites in its comments
against the image itself, for the addresses carried over from the C it
was copied from (the 58503A 3704's from the Z3805A 3543B's).

    python3 -I tools/firmware/comment_addresses.py --image IMAGE --base BASE [--all]

An address is a ROM address of five hex digits (0x10000 to 0x7fffe), a
RAM address (0x100000 to 0x10ffff) or an installer one
(installer::0x...); shorter numbers, device registers and the EEPROM
are taken for values.  An address is explained when it lies in the
function the comment is in or comes before, or at or in a function or
label the same comment names (a label running to the next label, a
pForth word naming its diag_ routine), by the image's export sidecar
and names files.  One that is not, and that the base's comments cite
too, is the base's, and printed, unless it holds the same thing in both
images: in ROM or the installer the same bytes (an address compared
masked, as an instruction's operand may differ), in RAM the same label
at the same address.  Each printed line gives the function holding it
here and the base's, with that one's address here.  --all prints every
address not explained, the base's or not.  Exits nonzero if any is
printed.
"""

import argparse
import bisect
import importlib.util
import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
FIRMWARE = ROOT / 'third_party/firmware'
spec = importlib.util.spec_from_file_location('decompiled_check', ROOT / 'tools/firmware/decompiled_check.py')
decompiled_check = importlib.util.module_from_spec(spec)
spec.loader.exec_module(decompiled_check)

NAMES_FILES = ('names.tsv', 'scpi-tree.tsv', 'installer-scpi-tree.tsv')
# the generated C's files: scpi_tree_data.c and installer_scpi_tree_data.c
GENERATED = 'scpi_tree_data.c'
OVERLAY = 'installer'
# the address ranges checked, the last ROM byte, the bound of the
# flash's range, left out; the rest are values, devices or the EEPROM
ROM = (0x10000, 0x7ffff)
RAM = (0x100000, 0x110000)
# a label's extent at most, where the next label is further
LABEL_EXTENT = 0x1000
# the bytes compared at an address, and the values masked in them
COMPARED = 6
MASKED = (ROM, RAM)
ADDRESS = re.compile(r'\b(installer::)?0x([0-9a-fA-F]+)\b')
COMMENT = re.compile(r'/\*.*?\*/', re.S)
IDENTIFIER = re.compile(r'\b[A-Za-z_][A-Za-z0-9_]*\b')
# the prefix of a pForth diagnostic word's routine's name
PFORTH_WORD = 'diag_'
# a definition at column 0: its name before its parameters, then its body
DEFINITION = re.compile(r'^[A-Za-z_][^;(){}=]*?\b([A-Za-z_]\w*)\s*\([^;{}]*\)\s*\{', re.M)


def split(text):
    """An address's region ('' for the primary) and value, from a
    sidecar's or names file's form (00012345, 0x12345,
    installer::00101234, installer::0x101234)."""
    if '::' in text:
        overlay, value = text.split('::')
        return overlay, int(value, 16)
    return '', int(text, 16)


class Image:
    """An image's functions, labels and bytes, and its C's comments:
    each name's region, start and kind, the starts by region, and the
    bytes by region (the installer's as its records unpack it)."""

    def __init__(self, name):
        self.folder = FIRMWARE / name
        self.named, self.functions, self.labels = {}, {}, {}
        for line in open(self.folder / 'decompiled' / (name + '.conventions.jsonl')):
            row = json.loads(line)
            region, address = split(row['address'])
            self.functions[region, address] = row['name']
            self.named.setdefault(row['name'], (region, address, 'function'))
        for file_name in NAMES_FILES:
            path = self.folder / file_name
            if not path.exists():
                continue
            for line in open(path):
                fields = line.rstrip('\n').split('\t')
                if line.startswith('#') or len(fields) < 3 or fields[1] != 'label':
                    continue
                region, address = split(fields[0])
                self.labels[region, address] = fields[2]
                self.named.setdefault(fields[2], (region, address, 'label'))
        regions = {region for region, _ in self.functions} | {region for region, _ in self.labels}
        self.function_starts = {r: sorted(a for rr, a in self.functions if rr == r) for r in regions}
        self.label_starts = {r: sorted(a for rr, a in self.labels if rr == r) for r in regions}
        data = (FIRMWARE / (name + '.bin')).read_bytes()
        self.memory = {'': (0, data)}
        if decompiled_check.find_installer_records(data) is not None:
            self.memory[OVERLAY] = (decompiled_check.RAM, decompiled_check.installer_image(data))

    def function_at(self, region, address):
        """The name of the function whose extent, to the next function's
        start, holds the address; None if none."""
        starts = self.function_starts.get(region, [])
        i = bisect.bisect_right(starts, address) - 1
        return self.functions[region, starts[i]] if i >= 0 else None

    def label_at(self, region, address):
        """The label whose extent holds the address and its start; None
        if none."""
        starts = self.label_starts.get(region, [])
        i = bisect.bisect_right(starts, address) - 1
        if i < 0 or address >= starts[i] + LABEL_EXTENT:
            return None
        return self.labels[region, starts[i]], starts[i]

    def holds(self, name, region, address):
        """Whether a name's function or label holds the address."""
        if name not in self.named:
            return False
        named_region, start, kind = self.named[name]
        if named_region != region:
            return False
        starts = (self.function_starts if kind == 'function' else self.label_starts)[region]
        i = bisect.bisect_right(starts, start)
        end = starts[i] if i < len(starts) else start + LABEL_EXTENT
        if kind == 'label':
            end = min(end, start + LABEL_EXTENT)
        return start <= address < max(end, start + 1)

    def masked(self, region, address):
        """The bytes at an address, each long in them that is an address
        zeroed; None where the image has none."""
        base, data = self.memory.get(region, (0, b''))
        at = address - base
        if not 0 <= at <= len(data) - COMPARED:
            return None
        raw = bytearray(data[at:at + COMPARED])
        for i in range(0, COMPARED - 3, 2):
            value = int.from_bytes(raw[i:i + 4], 'big')
            if any(low <= value < high for low, high in MASKED):
                raw[i:i + 4] = bytes(4)
        return bytes(raw)

    def sources(self):
        """The hand-written C, less the SCPI trees generated from the
        image itself (dump-scpi --c), whose addresses are its own."""
        return sorted(path for path in (self.folder / 'rewritten').rglob('*.[ch]')
                      if not path.name.endswith(GENERATED))


def checked(region, address):
    return RAM[0] <= address < RAM[1] or not region and ROM[0] <= address < ROM[1]


def owners(text):
    """Each definition's name and the span of its comment before it and
    its body."""
    spans = []
    for match in DEFINITION.finditer(text):
        depth, i = 0, match.end() - 1
        while i < len(text):
            depth += text[i] == '{'
            depth -= text[i] == '}'
            if depth == 0:
                break
            i += 1
        start = match.start()
        before = text.rfind('*/', 0, start)
        if before >= 0 and not text[before + 2:start].strip():
            start = text.rfind('/*', 0, before)
        spans.append((start, i, match.group(1)))
    return spans


def comment_addresses(text):
    """Each comment's addresses checked: (offset, region, address, the
    names the comment gives and the function it belongs to)."""
    spans = owners(text)
    for comment in COMMENT.finditer(text):
        words = IDENTIFIER.findall(comment.group())
        names = set(words) | {PFORTH_WORD + word for word in words}
        names |= {name for start, end, name in spans if start <= comment.start() <= end}
        for match in ADDRESS.finditer(comment.group()):
            region = OVERLAY if match.group(1) else ''
            address = int(match.group(2), 16)
            if checked(region, address):
                yield comment.start() + match.start(), region, address, names


def base_cited(base):
    """Every address the base's comments cite."""
    cited = set()
    for path in base.sources():
        for _, region, address, _ in comment_addresses(path.read_text()):
            cited.add((region, address))
    return cited


def same_thing(image, base, region, address):
    """Whether an address holds the same in both images: the same bytes,
    or in RAM the same label at the same start."""
    if not region and RAM[0] <= address < RAM[1]:
        here, there = image.label_at(region, address), base.label_at(region, address)
        return here is not None and here == there
    mine = image.masked(region, address)
    return mine is not None and mine == base.masked(region, address)


def show(region, address):
    return '%s%#x' % (region + '::' if region else '', address)


def check(image, base, cited, path, every):
    """The file's comment addresses not explained: (line, text)."""
    text = path.read_text()
    for offset, region, address, names in comment_addresses(text):
        if any(image.holds(name, region, address) for name in names):
            continue
        if not every and ((region, address) not in cited or same_thing(image, base, region, address)):
            continue
        here = image.function_at(region, address)
        there = base.function_at(region, address)
        line = text.count('\n', 0, offset) + 1
        why = 'in %s here' % here if here else 'in no function here'
        if there is not None and there in image.named:
            moved = image.named[there]
            why += "; in the base's %s, here at %s" % (there, show(moved[0], moved[1]))
        yield line, '%s: %s' % (show(region, address), why)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--image', required=True)
    parser.add_argument('--base', required=True)
    parser.add_argument('--all', action='store_true')
    arguments = parser.parse_args()
    image, base = Image(arguments.image), Image(arguments.base)
    cited = base_cited(base)
    failures = 0
    for path in image.sources():
        for line, text in check(image, base, cited, path, arguments.all):
            print('%s:%d: %s' % (path.relative_to(ROOT), line, text))
            failures += 1
    if failures:
        sys.exit("%d addresses in %s's comments not explained by its names" % (failures, arguments.image))


if __name__ == '__main__':
    main()
