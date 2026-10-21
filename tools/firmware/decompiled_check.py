# /// script
# requires-python = ">=3.11"
# ///
"""Confirms the decompiled C's assembly: assembles the __asm__ blocks of
third_party/firmware/decompiled/z3805a-3543b.c alone and compares each
piece of each routine with the image -- every byte, and every relocated
field resolved at the image's own addresses: the routines' from the
.conventions.jsonl sidecar, the made symbols' from .symbols.ld, the
globals' from their .set.  Installer routines are compared with the
installer the boot code unpacks from its records (docs/firmware/restart.md,
"Forced installer entry with an unusable primary").

    python3 tools/firmware/decompiled_check.py \\
        third_party/firmware/decompiled/z3805a-3543b.c \\
        third_party/firmware/z3805a-3543b.bin [OBJECT]

Given OBJECT, the whole file compiled, it also requires each piece's
section there to be the same size as assembled alone: that gcc's own
code did not land in one.  Needs m68k-linux-gnu binutils.  Exits
nonzero on any difference.
"""

import collections
import json
import os
import re
import struct
import subprocess
import sys
import tempfile

TOOLS = 'm68k-linux-gnu-'
RAM = 0x100000
# the boot code's records: an S, then C, count, destination and bytes
RECORDS = 0xbbd
# each routine's piece is a section .text.NAME.ADDRESS
PIECE = re.compile(r'^\s*\[\s*\d+\] \.text\.(\w+)\.([0-9a-f]+)\s')
RELOCATIONS = re.compile(r"^Relocation section '\.rela(\S*)'")
PIECE_NAME = re.compile(r'^\.text\.(\w+\.[0-9a-f]+)$')
FIELD_BYTES = {'R_68K_32': 4, 'R_68K_PC32': 4, 'R_68K_16': 2, 'R_68K_PC16': 2,
               'R_68K_8': 1, 'R_68K_PC8': 1}


def run(*command):
    return subprocess.run(command, capture_output=True, text=True).stdout


def installer_records(image):
    """The boot code's records, each one's RAM destination and bytes."""
    records = []
    at = RECORDS
    while image[at:at + 1] == b'C':
        count, destination = struct.unpack('>II', image[at + 1:at + 9])
        records.append((destination, image[at + 9:at + 9 + count]))
        at += 9 + count
    return records


def installer_image(image):
    """The installer's RAM, from RAM up, as the boot code unpacks it."""
    ram = bytearray(0x10000)
    for destination, data in installer_records(image):
        ram[destination - RAM:destination - RAM + len(data)] = data
    return bytes(ram)


def assembly_lines(c):
    """Every __asm__ block's lines, in order."""
    lines = []
    for block in re.finditer(r'^__asm__\((.*?)\);$', c, re.S | re.M):
        for quoted in re.findall(r'"((?:[^"\\]|\\.)*)"', block.group(1)):
            text = bytes(quoted, 'utf-8').decode('unicode_escape')
            lines.append(text if text.endswith('\n') else text + '\n')
    return lines


def addresses(c_path, obj):
    """Every symbol's value at the image's addresses."""
    values = {}
    for line in run(TOOLS + 'nm', obj).splitlines():
        fields = line.split()
        if len(fields) == 3 and fields[1] in 'Aa':
            values[fields[2]] = int(fields[0], 16)
    base = c_path.removesuffix('.c')
    if os.path.exists(base + '.symbols.ld'):
        for made in re.finditer(r'^(\w+) = 0x([0-9a-f]+);', open(base + '.symbols.ld').read(), re.M):
            values[made.group(1)] = int(made.group(2), 16)
    installer = set()
    for line in open(base + '.conventions.jsonl'):
        function = json.loads(line)
        values[function['name']] = int(function['address'].split(':')[-1], 16)
        if function['address'].startswith('installer'):
            installer.add(function['name'])
    return values, installer


def piece_sizes(obj):
    """Each piece section's size in an object."""
    sizes = {}
    for line in run(TOOLS + 'readelf', '-SW', obj).splitlines():
        piece = re.match(r'^\s*\[\s*\d+\] (\.text\.\w+\.[0-9a-f]+)\s+\S+\s+\S+\s+\S+\s+([0-9a-f]+)', line)
        if piece:
            sizes[piece.group(1)] = int(piece.group(2), 16)
    return sizes


def main():
    c_path, image_path = sys.argv[1], sys.argv[2]
    c = open(c_path).read()
    image = open(image_path, 'rb').read()
    installer = installer_image(image)
    work = tempfile.mkdtemp()
    source, obj = work + '/a.s', work + '/a.o'
    lines = assembly_lines(c)
    open(source, 'w').write(''.join(lines))
    errors = [line for line in subprocess.run(
        [TOOLS + 'as', '-mcpu=cpu32', '-o', obj, source], capture_output=True,
        text=True).stderr.splitlines() if 'Error' in line]
    print('assembler errors:', len(errors))
    for error in errors[:15]:
        print('  ', lines[int(error.split(':')[1]) - 1].rstrip(), '<-', error.split('Error:')[1].strip())
    if errors:
        return 1
    values, in_installer = addresses(c_path, obj)
    pieces = []
    for line in run(TOOLS + 'readelf', '-SW', obj).splitlines():
        piece = PIECE.match(line)
        if piece:
            pieces.append((piece.group(1), int(piece.group(2), 16)))
            values[piece.group(1) + '.' + piece.group(2)] = int(piece.group(2), 16)
    relocations = collections.defaultdict(list)
    section = None
    for line in run(TOOLS + 'readelf', '-rW', obj).splitlines():
        header = RELOCATIONS.match(line)
        if header:
            piece = PIECE_NAME.match(header.group(1))
            section = piece.group(1) if piece else None
            continue
        fields = line.split()
        if section and len(fields) >= 5 and fields[2] in FIELD_BYTES:
            addend = int(fields[6], 16) * (-1 if fields[5] == '-' else 1) if len(fields) > 6 else 0
            relocations[section].append((int(fields[0], 16), fields[2],
                                         fields[4].removeprefix('.text.'), addend))
    identical = differing = 0
    for name, address in pieces:
        section = f'{name}.{address:x}'
        out = work + '/piece.bin'
        subprocess.run([TOOLS + 'objcopy', '-O', 'binary', '-j', '.text.' + section, obj, out],
                       check=True)
        mine = bytearray(open(out, 'rb').read())
        unresolved = set()
        for offset, kind, symbol, addend in relocations[section]:
            if symbol not in values:
                unresolved.add(symbol)
                continue
            value = values[symbol] + addend - (address + offset if 'PC' in kind else 0)
            width = FIELD_BYTES[kind]
            mine[offset:offset + width] = (value & ((1 << 8 * width) - 1)).to_bytes(width, 'big')
        original = (installer[address - RAM:address - RAM + len(mine)] if name in in_installer
                    else image[address:address + len(mine)])
        differ = [i for i in range(len(mine)) if mine[i] != original[i]]
        if differ or unresolved:
            differing += 1
            print(f'  {section}: {len(differ)} of {len(mine)} bytes differ'
                  + (f', first at +{differ[0]:x}' if differ else '')
                  + (f', unresolved {sorted(unresolved)[:3]}' if unresolved else ''))
        else:
            identical += 1
    routines = len({name for name, _ in pieces})
    print(f'pieces identical: {identical}, differing: {differing}; routines: {routines}')
    if len(sys.argv) > 3:
        alone, whole = piece_sizes(obj), piece_sizes(sys.argv[3])
        grown = sorted(name for name in alone if whole.get(name) != alone[name])
        print(f'pieces the same size in {sys.argv[3]}: {len(alone) - len(grown)} of {len(alone)}'
              + (f', not {grown[:5]}' if grown else ''))
        differing += len(grown)
    return 1 if differing else 0


if __name__ == '__main__':
    sys.exit(main())
