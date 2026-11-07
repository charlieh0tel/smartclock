"""Lists every access an image's primary code makes into a record in RAM,
the evidence a record's type is derived from (docs/firmware/rewrite.md,
"The rewrite"), and fits a type to them.

    python3 -I tools/firmware/record_access.py IMAGE START SIZE
        [--argument FUNCTION:OFFSET[:ADDRESS]]... [--fit TYPE [--unreached]]

Each access is printed as a line: its offset into the record, its size
in bytes (0 for an address taken, ? where the mnemonic gives none), the
function and the instruction's address, how the address was formed
(abs, an absolute operand; reg, through an address register holding an
address in the record, plus a displacement; ind, the same with an index
register, whose offset is only the base's), r or w, and the mnemonic.
Then each push of an address in the record, a call's argument, as
"pass".  A register's value is followed linearly through each function,
from an address loaded into it (an immediate, a lea of an absolute or of
a register's value plus a displacement, a move from another register or
from an argument on the stack) and moved by addq, subq and a
postincrement or predecrement; it is forgotten when written otherwise,
across a call for D0, D1, A0 and A1, and at a branch's or a switch
case's target unless it was loaded from an immediate, an absolute or an
argument.  A switch's table, which objdump decodes as code, is skipped.
--argument FUNCTION:OFFSET names a function that takes the record's
address on the stack at OFFSET from its entry (4 the first argument, 6
the second after a word); with ADDRESS, the argument is that address in
the record instead.  FUNCTION is a name in the image's conventions or an
address.

--fit TYPE lays TYPE out as clang does from the image's
types/smartclock.h and prints each access that does not start a leaf
field of its size (an array's element; a char array's any byte), then
the record's size and the count of misfits; with --unreached, also each
leaf field no access reaches.  Needs m68k-linux-gnu binutils and clang.
"""

import argparse
import collections
import importlib.util
import json
import pathlib
import re
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
FIRMWARE = ROOT / 'third_party/firmware'


def load(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / 'tools/firmware' / (name + '.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


image_diff = load('image_diff')

# An instruction objdump prints: its address, mnemonic and operands.
INSTRUCTION = re.compile(r'^\s*([0-9a-f]+):\t[0-9a-f ]+\t(\S+)\s*(.*)$')
# A mnemonic's size suffix, and its bytes.
SIZES = {'b': 1, 'w': 2, 'l': 4, 's': 4, 'd': 8, 'x': 12}
# The mnemonics that take an address rather than access it.
ADDRESS_ONLY = ('lea', 'pea', 'jsr', 'jmp')
# The registers a call may change, by the image's convention.
CALL_CLOBBERED = ('d0', 'd1', 'a0', 'a1')
# An operand that is an absolute address, and one through an address
# register: (An), (An)+, -(An), d(An), and d(An,Xn), as objdump writes
# them: d(An) in decimal, d(An,Xn) in hexadecimal, a negative one as its
# 64 bits.
ABSOLUTE = re.compile(r'(0x[0-9a-f]+)(?::[wl])?')
REGISTER_INDIRECT = re.compile(r'%(a\d)@(?:\((-?\d+)\))?([+-])?')
INDEXED = re.compile(r'%(a\d)@\(([0-9a-f]+),[^)]*\)')
# A switch's jump through the PC, indexed by a data register's word.
SWITCH_JUMP = re.compile(r'%pc@\((0x[0-9a-f]+),%d\d:w\)')
# How far past the record a register's value may lie and still be
# followed, for a pointer stepped back into it.
FOLLOW_PAST = 0x10000


def primary_functions(image):
    """The primary's functions, by address: their names."""
    path = FIRMWARE / 'decompiled' / image / (image + '.conventions.jsonl')
    functions = {}
    for line in path.read_text().splitlines():
        entry = json.loads(line)
        if not entry['address'].startswith('inst'):
            functions[int(entry['address'], 16)] = entry['name']
    return functions


def disassemble(image):
    """The image's instructions, linearly: address, mnemonic, operands."""
    listing = subprocess.run(image_diff.OBJDUMP + [str(FIRMWARE / 'images' / (image + '.bin'))],
                             capture_output=True, text=True, check=True).stdout
    instructions = []
    for line in listing.splitlines():
        match = INSTRUCTION.match(line)
        if match:
            instructions.append((int(match.group(1), 16), match.group(2), match.group(3)))
    return instructions


def switch_tables(instructions, image):
    """Each switch's table of word offsets, which gcc puts after its jmp
    through the PC and which objdump decodes as code: (start, end, its
    cases' addresses).  The table runs to its first case."""
    rom = (FIRMWARE / 'images' / (image + '.bin')).read_bytes()
    tables = []
    for address, mnemonic, operands in instructions:
        match = SWITCH_JUMP.fullmatch(operands) if mnemonic == 'jmp' else None
        if not match:
            continue
        base, start = int(match.group(1), 16), address + 4
        end, targets = start, set()
        while end < min(targets, default=len(rom)) and end + 2 <= len(rom):
            target = base + int.from_bytes(rom[end:end + 2], 'big', signed=True)
            if target < end + 2:
                break
            targets.add(target)
            end += 2
        tables.append((start, end, targets))
    return tables


def operand_size(mnemonic):
    """The bytes an instruction's memory operand covers, 0 for an address
    taken, '?' where the mnemonic does not say."""
    if mnemonic.startswith(ADDRESS_ONLY):
        return 0
    match = re.search(r'([bwlsdx])$', mnemonic)
    return SIZES.get(match.group(1), '?') if match else '?'


def split_operands(operands):
    """An instruction's operands, split at the commas outside parentheses."""
    parts, depth, current = [], 0, ''
    for character in operands:
        depth += {'(': 1, ')': -1}.get(character, 0)
        if character == ',' and depth == 0:
            parts.append(current)
            current = ''
        else:
            current += character
    if current:
        parts.append(current)
    return [part.strip() for part in parts]


def register_count(registers):
    """The registers a movem list names."""
    count = 0
    for part in registers.split('/'):
        match = re.fullmatch(r'%([ad])(\d)-%([ad])(\d)', part)
        if match:
            first = (match.group(1) == 'a') * 8 + int(match.group(2))
            last = (match.group(3) == 'a') * 8 + int(match.group(4))
            count += last - first + 1
        else:
            count += 1
    return count


def pushed_bytes(mnemonic, operands):
    """The bytes an instruction pushes onto the stack, or pops (negative)."""
    if mnemonic == 'pea':
        return 4
    width = {'b': 2, 'w': 2, 'l': 4}.get(mnemonic[-1], 4)
    if operands and operands[-1] == '%sp@-':
        return 4 * register_count(operands[0]) if mnemonic == 'moveml' else width
    if operands and operands[0] == '%sp@+':
        return -(4 * register_count(operands[1]) if mnemonic == 'moveml' else width)
    immediate = re.fullmatch(r'#(\d+)', operands[0]) if operands else None
    if mnemonic in ('addql', 'addqw', 'addal', 'addaw', 'addw', 'addl') and len(operands) == 2 \
            and operands[1] == '%sp' and immediate:
        return -int(immediate.group(1))
    displacement = re.fullmatch(r'%sp@\((-?\d+)\)', operands[0]) if operands else None
    if mnemonic == 'lea' and len(operands) == 2 and operands[1] == '%sp' and displacement:
        return -int(displacement.group(1))
    return 0


class Scan:
    """One pass over the primary's code, following registers that hold an
    address in [start, end), and the accesses and pushes it finds."""

    def __init__(self, functions, start, size, arguments):
        self.functions = functions
        self.start, self.end = start, start + size
        self.arguments = arguments
        self.accesses = []  # (offset, size, function, address, mode, written, mnemonic)
        self.passes = []  # (function, address, offset)
        self.registers = {}  # a register's, or ('argument', offset)'s, value
        self.stable = set()  # the registers a branch's target keeps
        self.depth = 0  # the bytes pushed since the function's entry

    def inside(self, address, slack=0):
        return address is not None and self.start <= address < self.end + slack

    def run(self, instructions, image):
        branch_targets = {int(target, 16) for _, mnemonic, operands in instructions
                          if mnemonic.startswith(('b', 'db'))
                          for target in re.findall(r'\b0x([0-9a-f]+)\b', operands)}
        tables = switch_tables(instructions, image)
        for start, end, targets in tables:
            branch_targets |= targets
        function = None
        for address, mnemonic, operands in instructions:
            if any(start <= address < end for start, end, _ in tables):
                continue
            if address in self.functions:
                function = address
                self.registers = {('argument', offset): value
                                  for offset, value in self.arguments.get(address, {}).items()}
                self.stable = set()
                self.depth = 0
            elif address in branch_targets:
                self.registers = {key: value for key, value in self.registers.items()
                                  if key[0] == 'argument' or key in self.stable}
            if function is not None:
                self.step(function, address, mnemonic, split_operands(operands))

    def value(self, operand):
        """An operand's value, where it is followed."""
        match = re.fullmatch(r'#(-?\d+)', operand)
        if match:
            return int(match.group(1)) & 0xffffffff
        match = re.fullmatch(r'%([ad]\d)', operand)
        if match:
            return self.registers.get(match.group(1))
        match = re.fullmatch(r'%sp@\((\d+)\)', operand)
        if match:
            return self.registers.get(('argument', int(match.group(1)) - self.depth))
        match = re.fullmatch(r'%fp@\((\d+)\)', operand)
        if match:
            return self.registers.get(('argument', int(match.group(1)) - 4))
        return None

    def note(self, function, address, mnemonic, size, operand, written):
        match = ABSOLUTE.fullmatch(operand)
        if match:
            target = int(match.group(1), 16)
            if self.inside(target):
                self.accesses.append((target - self.start, size, function, address, 'abs', written, mnemonic))
            return
        indexed = INDEXED.fullmatch(operand)
        match = indexed or REGISTER_INDIRECT.fullmatch(operand)
        if not match:
            return
        register = match.group(1)
        base = self.registers.get(register)
        if not self.inside(base, FOLLOW_PAST):
            return
        if indexed:
            displacement = int(match.group(2), 16)
            displacement -= (1 << 64) if displacement >= 1 << 63 else 0
        else:
            displacement = int(match.group(2)) if match.group(2) else 0
        target = base + displacement
        if self.inside(target):
            self.accesses.append((target - self.start, size, function, address,
                                  'ind' if indexed else 'reg', written, mnemonic))
        if not indexed and match.group(3):
            step = size if isinstance(size, int) else 0
            self.registers[register] = base + (step if match.group(3) == '+' else -step)

    def step(self, function, address, mnemonic, operands):
        size = operand_size(mnemonic)
        for index, operand in enumerate(operands):
            self.note(function, address, mnemonic, size, operand, index == len(operands) - 1 and len(operands) > 1)
        self.note_pass(function, address, mnemonic, operands)
        self.depth += pushed_bytes(mnemonic, operands)
        if len(operands) == 2:
            self.write_register(mnemonic, operands)
        if mnemonic.startswith(('jsr', 'bsr')):
            for register in CALL_CLOBBERED:
                self.registers.pop(register, None)
                self.stable.discard(register)
        for operand in operands:
            moved = re.fullmatch(r'%(a\d)@[+-]', operand)
            if moved:
                self.stable.discard(moved.group(1))

    def note_pass(self, function, address, mnemonic, operands):
        """A push of an address in the record."""
        pushed = None
        if len(operands) == 2 and operands[1] == '%sp@-' and re.fullmatch(r'#-?\d+|%a\d', operands[0]):
            pushed = self.value(operands[0])
        elif mnemonic == 'pea' and ABSOLUTE.fullmatch(operands[0]):
            pushed = int(ABSOLUTE.fullmatch(operands[0]).group(1), 16)
        if self.inside(pushed):
            self.passes.append((function, address, pushed - self.start))

    def write_register(self, mnemonic, operands):
        """Follows a register an instruction writes."""
        source, destination = operands
        match = re.fullmatch(r'%([ad]\d)', destination)
        if not match:
            return
        register = match.group(1)
        self.stable.discard(register)
        if mnemonic == 'lea':
            absolute = ABSOLUTE.fullmatch(source)
            relative = re.fullmatch(r'%(a\d)@\((-?\d+)\)', source)
            if absolute:
                self.registers[register] = int(absolute.group(1), 16)
                self.stable.add(register)
            elif relative and self.registers.get(relative.group(1)) is not None:
                self.registers[register] = self.registers[relative.group(1)] + int(relative.group(2))
                self.stable.add(register)
            else:
                self.registers.pop(register, None)
        elif mnemonic.startswith(('movea', 'movel')):
            value = self.value(source)
            if value is None:
                self.registers.pop(register, None)
                return
            self.registers[register] = value
            if source.startswith(('#', '%sp@(', '%fp@(')) or source[1:] in self.stable:
                self.stable.add(register)
        elif mnemonic.startswith(('addq', 'adda', 'subq', 'suba')) and register.startswith('a') \
                and self.registers.get(register) is not None:
            immediate = re.fullmatch(r'#(\d+)', source)
            if immediate:
                sign = 1 if mnemonic.startswith('add') else -1
                self.registers[register] += sign * int(immediate.group(1))
            else:
                self.registers.pop(register, None)
        elif not mnemonic.startswith(('cmp', 'tst', 'btst')):
            self.registers.pop(register, None)


def record_layouts(image, type_name):
    """Every record clang lays out from the image's smartclock.h with TYPE
    declared: by name, its direct fields (offset, type, elements or None,
    name) and its size."""
    header = FIRMWARE / 'rewritten' / image / 'types/smartclock.h'
    with tempfile.TemporaryDirectory() as work:
        source = pathlib.Path(work) / 'fit.c'
        source.write_text('#include "%s"\n%s fit_record;\n' % (header, type_name))
        dump = subprocess.run(['clang', '-target', 'm68k-unknown-elf', '-fsyntax-only', '-w', '-Xclang',
                               '-fdump-record-layouts-complete', '-Xclang', '-fdump-record-layouts', str(source)],
                              capture_output=True, text=True, check=True).stdout
    records = {}
    name, fields = None, []
    for line in dump.splitlines():
        heading = re.match(r'\s+0 \| (?:struct |union )?(\w+)$', line)
        size = re.search(r'sizeof=(\d+)', line)
        row = re.match(r'\s*(\d+) \|   (\S.*)$', line)
        if heading:
            name, fields = heading.group(1), []
        elif size and name:
            records[name] = (fields, int(size.group(1)))
            name = None
        elif row and name:
            match = re.match(r'(?:struct |union )?(.*?)(?:\[(\d+)\])? (\w+)$', row.group(2))
            fields.append((int(row.group(1)), match.group(1).strip(),
                           int(match.group(2)) if match.group(2) else None, match.group(3)))
    return records


def leaves(records, type_name, base=0, prefix=''):
    """TYPE's leaf fields, each record and array of records opened:
    (start, end, type, elements or None, name)."""
    fields, size = records[type_name]
    out = []
    for index, (offset, kind, elements, name) in enumerate(fields):
        end = fields[index + 1][0] if index + 1 < len(fields) else size
        end = max(end, offset)
        if kind in records:
            element = records[kind][1]
            for number in range(elements or 1):
                label = '%s%s[%d].' % (prefix, name, number) if elements else '%s%s.' % (prefix, name)
                out += leaves(records, kind, base + offset + number * element, label)
        else:
            out.append((base + offset, base + end, kind, elements, prefix + name))
    return out


def fits(leaf_list, offset, size):
    """Whether an access of size bytes at offset is a leaf's: a field of
    its size, an array's element, a char array's any byte, a double's
    half, or a run of whole fields copied at once."""
    starts = {start for start, *_ in leaf_list}
    ends = {end for _, end, *_ in leaf_list}
    for start, end, kind, elements, _ in leaf_list:
        if not start <= offset < max(end, start + 1):
            continue
        element = (end - start) // elements if elements else end - start
        if kind == 'char' and elements:
            return True
        if elements and (offset - start) % element == 0 and (size == element or (element > 4 and size <= element)):
            return True
        if not elements and offset == start and size == end - start:
            return True
        if kind == 'double' and size == 4 and (offset - start) % 4 == 0:
            return True
    return offset in starts and (offset + size) in (starts | ends)


def fit(image, type_name, accesses, names, unreached):
    """Prints each access that does not start a leaf field of its size."""
    records = record_layouts(image, type_name)
    leaf_list = leaves(records, type_name)
    total = records[type_name][1]
    sizes, users = collections.defaultdict(set), collections.defaultdict(set)
    for offset, size, function, _, mode, _, _ in accesses:
        if mode != 'ind':
            sizes[offset].add(size)
            users[offset].add(names[function])
    misfits, reached = 0, set()
    for offset in sorted(sizes):
        leaf = next((leaf for leaf in leaf_list if leaf[0] <= offset < max(leaf[1], leaf[0] + 1)), None)
        if leaf is None:
            print('OUTSIDE %#x %s' % (offset, sorted(map(str, sizes[offset]))))
            misfits += 1
            continue
        reached.add(leaf[0])
        for size in sizes[offset]:
            if isinstance(size, int) and size and not fits(leaf_list, offset, size):
                print('MISFIT %#x size %d: %s %s at %#x..%#x (%s)'
                      % (offset, size, leaf[2], leaf[4], leaf[0], leaf[1], ' '.join(sorted(users[offset]))))
                misfits += 1
    print('size %#x, %d misfits' % (total, misfits))
    if unreached:
        for start, end, kind, _, name in leaf_list:
            if start not in reached:
                print('unreached %#x..%#x %s %s' % (start, end, kind, name))
    return misfits


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('image')
    parser.add_argument('start', type=lambda text: int(text, 0))
    parser.add_argument('size', type=lambda text: int(text, 0))
    parser.add_argument('--argument', action='append', default=[], metavar='FUNCTION:OFFSET[:ADDRESS]')
    parser.add_argument('--fit', metavar='TYPE')
    parser.add_argument('--unreached', action='store_true')
    options = parser.parse_args()

    functions = primary_functions(options.image)
    by_name = {name: address for address, name in functions.items()}
    arguments = collections.defaultdict(dict)
    for argument in options.argument:
        parts = argument.split(':')
        function = by_name[parts[0]] if parts[0] in by_name else int(parts[0], 16)
        arguments[function][int(parts[1], 0)] = int(parts[2], 16) if len(parts) > 2 else options.start
    scan = Scan(functions, options.start, options.size, arguments)
    scan.run(disassemble(options.image), options.image)
    for offset, size, function, address, mode, written, mnemonic in sorted(
            scan.accesses, key=lambda access: (access[0], str(access[1]), access[3])):
        print('%#06x\t%s\t%s\t%#x\t%s\t%s\t%s' % (offset, size, functions[function], address, mode,
                                                  'w' if written else 'r', mnemonic))
    for function, address, offset in scan.passes:
        print('pass\t%#06x\t%s\t%#x' % (offset, functions[function], address))
    if options.fit:
        return 1 if fit(options.image, options.fit, scan.accesses, functions, options.unreached) else 0
    return 0


if __name__ == '__main__':
    sys.exit(main())
