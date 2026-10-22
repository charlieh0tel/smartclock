# /// script
# requires-python = ">=3.11"
# ///
"""Confirms the SCPI tree's generated C data against the image: compiles
it alone with m68k gcc and smartclock.h, and compares each object with
the record at its address -- every byte, and every relocated field by
what it points at, resolved at the image's own addresses: the records'
from the generated names rows, the routines' from the decompiled C's
.conventions.jsonl sidecar, where Ghidra's names stand, and anything
else from the names file.  A routine the C names that Ghidra does not
name so at its address is a difference too, as are a record but a
string at an odd address and a child list's children, an array of
their own, not right after the list.

    DECOMPILED_CFLAGS='...' python3 tools/firmware/scpi_tree_check.py \\
        third_party/firmware/rewritten/z3805a-3543b/scpi_tree_data.c \\
        tools/ghidra/names/z3805a-3543b.scpi-tree.tsv \\
        tools/ghidra/names/z3805a-3543b.tsv \\
        third_party/firmware/decompiled/z3805a-3543b.conventions.jsonl \\
        third_party/firmware/z3805a-3543b.bin

make check-scpi-tree runs it with the Makefile's flags.  Needs
m68k-linux-gnu gcc and binutils.  Exits nonzero on any difference.
"""

import json
import os
import re
import shlex
import subprocess
import sys
import tempfile

from decompiled_check import RAM, TOOLS, installer_image, run

# what -include gives the C, which includes nothing of its own but stddef.h
TYPES = os.path.join(os.path.dirname(__file__), '..', 'ghidra', 'types', 'smartclock.h')
# the macro under which the C declares the routines it points at
ALONE = 'SCPI_TREE_DATA_ALONE'
INSTALLER = 'installer::'
# what a child list's children's name adds to the list's
CHILDREN_SUFFIX = '_children'
# how a names row's type starts for text, a keyword pair or a unit's name
TEXT = 'char['
# a .rela section's row: offset, info, type, value, symbol, sign, addend
RELOCATION = re.compile(r'^([0-9a-f]+)\s+[0-9a-f]+\s+(R_68K_\w+)\s+[0-9a-f]+\s+(\S+)\s*([+-])\s*([0-9a-f]+)$')


def addresses(names_paths, conventions_path):
    """Each name's address, a (space, address) pair: the names rows', then
    Ghidra's functions'.  A name given two addresses has none, and the C
    naming it is a difference."""
    values = {}
    ambiguous = set()

    def put(name, address):
        space = 'installer' if address.startswith(INSTALLER) else 'primary'
        value = (space, int(address.removeprefix(INSTALLER), 16))
        if values.setdefault(name, value) != value:
            ambiguous.add(name)

    for path in names_paths:
        for line in open(path):
            if line.startswith('#') or not line.strip():
                continue
            fields = line.rstrip('\n').split('\t')
            if fields[1] in ('function', 'label'):
                put(fields[2], fields[0])
    for line in open(conventions_path):
        function = json.loads(line)
        address = function['address']
        put(function['name'], (INSTALLER if address.startswith('installer') else '')
            + '0x' + address.split(':')[-1])
    for name in ambiguous:
        del values[name]
    return values


def main(work):
    c_path, tree_names, names_file, conventions, image_path = sys.argv[1:6]
    image = open(image_path, 'rb').read()
    memory = {'primary': image, 'installer': installer_image(image)}
    base = {'primary': 0, 'installer': RAM}
    obj = work + '/tree.o'
    flags = shlex.split(os.environ.get('DECOMPILED_CFLAGS', ''))
    compiled = subprocess.run([TOOLS + 'gcc', *flags, '-D' + ALONE, '-include', TYPES, '-c',
                               '-o', obj, c_path], capture_output=True, text=True)
    if compiled.returncode != 0:
        print(compiled.stderr)
        return 1
    values = addresses([tree_names, names_file], conventions)
    # the rows typed as text, which may lie at any address
    texts = {fields[2] for line in open(tree_names) if not line.startswith('#')
             and len(fields := line.rstrip('\n').split('\t')) > 5 and fields[5].startswith(TEXT)}
    symbols = {}
    for line in run(TOOLS + 'readelf', '-sW', obj).splitlines():
        fields = line.split()
        if len(fields) == 8 and fields[3] == 'OBJECT':
            symbols[fields[7]] = (int(fields[1], 16), int(fields[2]))
    data = work + '/rodata.bin'
    subprocess.run([TOOLS + 'objcopy', '-O', 'binary', '-j', '.rodata', obj, data], check=True)
    rodata = open(data, 'rb').read()
    relocations = []
    in_rodata = False
    for line in run(TOOLS + 'readelf', '-rW', obj).splitlines():
        if line.startswith('Relocation section'):
            in_rodata = "'.rela.rodata'" in line
            continue
        row = RELOCATION.match(line.strip())
        if in_rodata and row:
            if row.group(2) != 'R_68K_32':
                raise SystemExit(f'unexpected relocation: {line}')
            addend = int(row.group(5), 16) * (-1 if row.group(4) == '-' else 1)
            relocations.append((int(row.group(1), 16), row.group(3), addend))

    def resolve(symbol, addend):
        """What a relocated field points at, at the image's addresses."""
        if symbol == '.rodata':
            for name, (offset, size) in symbols.items():
                if offset <= addend < offset + max(size, 1):
                    symbol, addend = name, addend - offset
                    break
        if symbol not in values:
            return None
        return values[symbol][1] + addend

    identical = differing = 0
    for name, (offset, size) in symbols.items():
        # a child list's children, an array of their own, which the
        # parser reads past the list's end, as the image lays them out
        owner = symbols.get(name.removesuffix(CHILDREN_SUFFIX)) if name.endswith(CHILDREN_SUFFIX) else None
        if owner is not None and sum(owner) != offset:
            print(f'  {name}: not right after its list')
            differing += 1
    for name, (offset, size) in sorted(symbols.items(), key=lambda item: item[1]):
        if name not in values:
            print(f'  {name}: no address in the names rows')
            differing += 1
            continue
        space, address = values[name]
        if name not in texts and offset % 2:
            # the section starts even: a word or long here is misaligned
            print(f'  {name}: at an odd address, where its words are misaligned')
            differing += 1
            continue
        mine = bytearray(rodata[offset:offset + size])
        unresolved = []
        for at, symbol, addend in relocations:
            if offset <= at < offset + size:
                target = resolve(symbol, addend)
                if target is None:
                    unresolved.append(symbol)
                    continue
                mine[at - offset:at - offset + 4] = target.to_bytes(4, 'big')
        start = address - base[space]
        original = memory[space][start:start + size]
        differ = [i for i in range(size) if i >= len(original) or mine[i] != original[i]]
        if differ or unresolved:
            differing += 1
            print(f'  {name} ({space} {address:#x}, {size} bytes): {len(differ)} differ'
                  + (f', first at +{differ[0]:#x}' if differ else '')
                  + (f', unresolved {sorted(set(unresolved))[:3]}' if unresolved else ''))
        else:
            identical += 1
    print(f'objects identical: {identical}, differing: {differing}')
    return 1 if differing or not identical else 0


if __name__ == '__main__':
    with tempfile.TemporaryDirectory() as work:
        sys.exit(main(work))
