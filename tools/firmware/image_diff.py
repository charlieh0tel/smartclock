# /// script
# requires-python = ">=3.11"
# ///
"""Matches every function and named ROM table of a base image, whose
functions and labels are named, to another image by their bytes, and
reports what moved, what changed and what the other image adds.

    python3 -I tools/firmware/image_diff.py \\
        --functions third_party/firmware/decompiled/z3805a-3543b/z3805a-3543b.conventions.jsonl \\
        --names third_party/firmware/analysis/z3805a-3543b/names.tsv \\
        --modules third_party/firmware/analysis/z3805a-3543b/modules.tsv \\
        [--handlers BASE.txt OTHER.txt] [--installer] [--diff NAME ...] \\
        third_party/firmware/images/z3805a-3543b.bin OTHER.bin > OUT.tsv

A base function's body runs from its start to the first unconditional
transfer (rts, rte, jmp, bra) that no branch before it jumps past, and
never beyond the next named function or label.  Code of the base that
no named function covers is found as the other image's is, below, and
matched too, as a "piece".  A function is matched, in this order:

- exact: its bytes occur in the other image;
- masked: its bytes occur with every address and displacement that
  leaves the body masked: jsr, jmp, lea, pea, bsr, bra, Bcc and DBcc
  targets, PC-relative operands, and absolute operands, indexed
  operands' displacements and immediates that fall in the ROM or the
  RAM;
- fuzzy: a function of the other image has the same instructions and
  addressing modes, with any immediates and displacements;
- handler: with --handlers, two `dump-scpi --handlers` listings, the
  other image's handler for the same SCPI path;
- similar: the other image's function between the counterparts of its
  nearest matched neighbors whose instructions are most alike (difflib
  ratio at least SIMILAR);
- callees: one that calls the counterparts of the functions it calls.

A byte or shape match of a small function, and any match outside the
neighbors' counterparts, needs more evidence (SMALL, UNIQUE_SIZE,
FAR_SIZE, SIMILAR_FAR); an SCPI handler of the other image goes only
to a base handler of one of its paths.  Each match is then reported as
identical (the same bytes, wherever they are), relocated (the same
with addresses masked) or changed, with a summary of the instruction
diff, its calls and its strings named through the matches; a function
with no match is missing.

The other image's functions are found from the matches, the handlers,
the vector tables (a table starts with the reset table's stack pointer,
at a multiple of VECTOR_TABLE_ALIGNMENT) and prologues after a return,
by following calls and jumps, by pointers to the end of a known body,
and by decoding on from the end of one body while the code decodes
cleanly to an unconditional transfer.  The installer's packed records
are data, and so is a start a call, a pointer or such decoding gives
whose bytes are text to a NUL (SHORTEST_TEXT printable at least), or a
NUL and then such text, or whose first long is zero or an address, a
table of pointers; one
inside a base function's bytes matched there, inside the instructions
of the start before it decoded on past it, or inside code that decodes
cleanly from that one's end, unless the end is padding or data, is not
a function's start either.  Those no base function or piece matched
are reported as new, but for one inside a ROM table placed there.  A label with a type in the names file is located by the
addresses that matched instructions use in its place, or by its bytes,
and compared as a table, pointers masked; a RAM label is only located,
as a global.

With --installer, the same is done for the installers the boot code
unpacks from its records into RAM, the base's from the installer::
rows.  With --diff, the instruction diff of the named base functions
goes to stderr.  The TSV goes to stdout, the counts by status and
module to stderr.  Needs m68k-linux-gnu binutils.
"""

import argparse
import bisect
import collections
import dataclasses
import difflib
import json
import os
import re
import struct
import subprocess
import sys
import tempfile

OBJDUMP = ['m68k-linux-gnu-objdump', '-D', '-b', 'binary', '-m', 'm68k:cpu32']
# where the installer's records unpack, and how much RAM they may fill
RAM = 0x100000
RAM_SIZE = 0x10000
# an absolute value below this is a constant, not an address: the
# vector table lies below it and nothing is linked there
LOWEST_ADDRESS = 0x400
# an immediate is taken for an address only from here up
LOWEST_IMMEDIATE_ADDRESS = 0x10000
# a label's bytes are compared at most this far
TABLE_LIMIT = 0x800
# a body is decoded at most this far past its start
BODY_LIMIT = 0x4000
# the least difflib ratio for a similar match, and for one by callees,
# whose score, the mean of the ratio and the callees' overlap, must
# reach SIMILAR
SIMILAR = 0.5
SIMILAR_WITH_CALLEES = 0.45
# a match by callees outside the neighbors' gap needs this score
SIMILAR_FAR = 0.7
# a function of fewer instructions is matched by its shape only between
# its neighbors' counterparts, and as similar only nearly the same
SMALL = 8
SIMILAR_SMALL = 0.85
# scores this close count as a tie
TIE = 0.02
# a masked match unique in the image is taken at once from this size
UNIQUE_SIZE = 24
# a masked match outside the gap is taken from this size
FAR_SIZE = 64
# a body this much printable text or NUL is data
TEXT_FRACTION = 0.85
# the changed instructions a summary quotes
EXAMPLES = 3
# the shortest string a summary quotes
SHORTEST_STRING = 4
# a start whose bytes to a NUL are text: how far it is read, the fewest
# printable bytes (code's own runs before a zero byte are shorter) and
# the control bytes text may hold
STRING_LIMIT = 256
SHORTEST_TEXT = 6
TEXT_CONTROLS = b'\t\n\r'
# the words that pad between functions
PADDING = (b'\0\0', b'\x4e\x71')
# how the starts that may be rejected as not code were found
WEAK = ('called', 'pointer', 'gap')
# the base image's C type sizes, for a label's extent
TYPE_SIZES = {'char': 1, 'uint8_t': 1, 'int8_t': 1, 'int16_t': 2,
              'uint16_t': 2, 'int32_t': 4, 'uint32_t': 4, 'float': 4,
              'double': 8, 'int': 4, 'void *': 4, 'char *': 4,
              'uint8_t *': 4}
# an exception vector table: as many vectors, its first the initial
# stack pointer, as the reset table's, at a multiple of this
VECTORS = 256
VECTOR_TABLE_ALIGNMENT = 0x10000
# a function's first word: link a6, movem.l to -(sp); and the words a
# function before it may end on: rts, nop, rte, rtd
PROLOGUES = {b'\x4e\x56', b'\x48\xe7'}
TRANSFERS = {b'\x4e\x75', b'\x4e\x71', b'\x4e\x73', b'\x4e\x74'}
TERMINALS = {'rts', 'rte', 'rtr', 'rtd', 'jmp', 'bras', 'braw', 'bral',
             'bra', 'illegal'}
BRANCH = re.compile(r'^(?:b(?:ra|sr|hi|ls|cc|cs|ne|eq|vc|vs|pl|mi|ge|lt|gt|le)|db\w+)([swl]?)$')
CALLS = {'jsr', 'bsrs', 'bsrw', 'bsrl', 'bsr'}
LINE = re.compile(r'^\s*([0-9a-f]+):\t[0-9a-f ]+\t(\S+)\s*(.*)$')
PC_RELATIVE = re.compile(r'%pc@\(0x([0-9a-f]+)')
ABSOLUTE = re.compile(r'(?<![#\w(@])0x([0-9a-f]+)(?![\w@])')
IMMEDIATE = re.compile(r'#(-?\d+)')
# an indexed or memory-indirect operand's displacement, which objdump
# prints in hexadecimal without 0x, where a register's 16-bit one,
# %a6@(-4), is decimal: one before an index or an indirection, or one
# with no register before it, as an absolute table's @(1038b4,%d0:w:4);
# a negative one is printed sign-extended past eight digits
INDEXED = re.compile(r'@\(([0-9a-f]{1,8})(?=,|\)@)|(?<![\w%])@\(([0-9a-f]{1,8})\)')
# a number in an operand, a displacement after @( whole whether
# objdump printed it in decimal or in hexadecimal
NUMBER = re.compile(r'(?<=@\()(?!0x)[0-9a-f]+|-?(?:0x[0-9a-f]+|\d+)')


@dataclasses.dataclass(frozen=True)
class Instruction:
    """One decoded instruction: where it is, its bytes and its text."""
    address: int
    raw: bytes
    mnemonic: str
    operands: str

    @property
    def end(self):
        return self.address + len(self.raw)


@dataclasses.dataclass(frozen=True)
class Field:
    """An address an instruction names, and the bytes that encode it:
    offset and size within the instruction, PC-relative or absolute."""
    offset: int
    size: int
    target: int
    relative: bool


@dataclasses.dataclass
class Body:
    """A function's instructions from its start, and whether they end
    cleanly at an unconditional transfer."""
    start: int
    instructions: list
    clean: bool

    @property
    def end(self):
        return self.instructions[-1].end if self.instructions else self.start


@dataclasses.dataclass
class Item:
    """A named base function or label, and what it matched: status is
    identical, relocated, changed or missing (new for an other-image
    function no base function matched); method how it was found."""
    kind: str
    address: int
    name: str
    module: str
    size: int = 0
    type: str = ''
    status: str = 'missing'
    method: str = ''
    other: int | None = None
    other_size: int = 0
    similarity: float = 0.0
    summary: str = ''


class Memory:
    """An image, or the installer's RAM, and its disassembly: base is
    the address of its first byte; packed, the extent of the installer's
    records in an image; linear holds every instruction of one pass over
    it by address, windows those of passes from starts the linear one
    does not reach."""

    def __init__(self, data, base, packed=None):
        self.data = data
        self.base = base
        self.packed = packed
        self.linear = self.decode(base, base + len(data))
        self.order = sorted(self.linear)
        self.windows = {}

    def decode(self, start, stop):
        with tempfile.NamedTemporaryFile(suffix='.bin') as file:
            file.write(self.data)
            file.flush()
            text = subprocess.run(
                OBJDUMP + ['--adjust-vma=0x%x' % self.base,
                           '--start-address=0x%x' % start, '--stop-address=0x%x' % stop,
                           file.name], capture_output=True, text=True, check=True).stdout
        lines = [(int(m.group(1), 16), m.group(2), m.group(3))
                 for m in map(LINE.match, text.splitlines()) if m]
        decoded = {}
        for (address, mnemonic, operands), following in zip(lines, lines[1:] + [(stop, '', '')]):
            decoded[address] = Instruction(address, self.bytes(address, following[0]),
                                           mnemonic, operands.strip())
        return decoded

    def bytes(self, start, stop):
        return self.data[max(start - self.base, 0):max(stop - self.base, 0)]

    def contains(self, address):
        return self.base <= address < self.base + len(self.data)

    def instructions(self, start, stop):
        """The instructions from start up to stop."""
        if start in self.linear:
            found = []
            at = bisect.bisect_left(self.order, start)
            while at < len(self.order) and self.order[at] < stop:
                found.append(self.linear[self.order[at]])
                at += 1
            return found
        if (start, stop) not in self.windows:
            self.windows[start, stop] = sorted(self.decode(start, stop).values(),
                                               key=lambda i: i.address)
        return self.windows[start, stop]

    def is_packed(self, address):
        """Whether an address is in the installer's packed records, which
        are data in the image."""
        return self.packed is not None and self.packed[0] <= address < self.packed[1]

    def string(self, address):
        """The NUL-terminated printable string at an address, or None."""
        if not self.contains(address):
            return None
        tail = self.data[address - self.base:address - self.base + 80]
        text = tail.split(b'\0', 1)[0]
        if len(text) >= SHORTEST_STRING and all(32 <= c < 127 for c in text):
            return text.decode()
        return None


def signed(value, bits):
    return value - (1 << bits) if value & (1 << (bits - 1)) else value


def fields(instruction):
    """The addresses an instruction names, with the bytes encoding each."""
    raw, address, found = instruction.raw, instruction.address, []
    branch = BRANCH.match(instruction.mnemonic)
    operands = instruction.operands
    if branch and len(raw) >= 2:
        target = re.search(r'0x([0-9a-f]+)$', operands)
        if target:
            size = {'s': 1, 'w': 2, 'l': 4}.get(branch.group(1)) or (len(raw) - 2 or 1)
            offset = 1 if size == 1 else 2
            found.append(Field(offset, size, int(target.group(1), 16), True))
        return found
    for target in (int(t, 16) for t in PC_RELATIVE.findall(operands)):
        for offset in range(2, len(raw) - 1, 2):
            if address + offset + signed(int.from_bytes(raw[offset:offset + 2], 'big'), 16) == target:
                found.append(Field(offset, 2, target, True))
                break
            if address + offset + signed(raw[offset + 1], 8) == target:
                found.append(Field(offset + 1, 1, target, True))
                break
    outside_pc = PC_RELATIVE.sub('', operands)
    values = [(int(v, 16), False) for v in ABSOLUTE.findall(outside_pc)]
    values += [(int(a or b, 16), False) for a, b in INDEXED.findall(outside_pc)]
    values += [(int(v) & 0xffffffff, True) for v in IMMEDIATE.findall(operands)]
    for value, immediate in values:
        if value < LOWEST_ADDRESS or (immediate and value < LOWEST_IMMEDIATE_ADDRESS):
            continue
        offset = raw.find(value.to_bytes(4, 'big'), 2)
        if offset >= 2:
            found.append(Field(offset, 4, value, False))
        elif not immediate and (value < 0x8000 or value >= 0xffff8000):
            offset = raw.find((value & 0xffff).to_bytes(2, 'big'), 2)
            if offset >= 2:
                found.append(Field(offset, 2, value, False))
    return found


def jump_table(memory, instruction, stop):
    """The case targets of a jmp through a table of word offsets."""
    table = PC_RELATIVE.search(instruction.operands)
    if not table or ',' not in instruction.operands:
        return []
    base = int(table.group(1), 16)
    targets, limit, at = [], stop, base
    while at < limit and at + 2 <= stop:
        target = base + signed(int.from_bytes(memory.bytes(at, at + 2), 'big'), 16)
        if not (base < target < stop) or target % 2:
            break
        targets.append(target)
        limit = min(limit, target)
        at += 2
    return targets


def body(memory, start, stop):
    """The instructions from start to the first unconditional transfer
    that no earlier branch jumps past, before stop."""
    instructions, reach = [], start
    for instruction in memory.instructions(start, min(stop, start + BODY_LIMIT)):
        if instruction.mnemonic == '.short' or instruction.end > stop:
            return Body(start, instructions, False)
        instructions.append(instruction)
        jumps = BRANCH.match(instruction.mnemonic) and instruction.mnemonic not in CALLS
        for field in fields(instruction) if jumps else ():
            if field.relative and instruction.address < field.target < stop:
                reach = max(reach, field.target)
        if instruction.mnemonic == 'jmp':
            reach = max([reach] + jump_table(memory, instruction, stop))
        if instruction.mnemonic in TERMINALS and instruction.end > reach:
            return Body(start, instructions, True)
    return Body(start, instructions, False)


def external_fields(found):
    """Each instruction's fields that name an address outside the body."""
    return [[f for f in fields(i) if not (found.start <= f.target < found.end)
             and (f.relative or is_address(f.target))] for i in found.instructions]


# the ranges is_address tests, set per run: an absolute field outside
# them (an I/O register, a constant) must match as it stands
ADDRESS_RANGES = []


def is_address(value):
    return any(low <= value < high for low, high in ADDRESS_RANGES)


def masked_pattern(found):
    """A regular expression for a body's bytes with its outward
    addresses and displacements wildcards."""
    raw = bytearray(b''.join(i.raw for i in found.instructions))
    mask = bytearray(len(raw))
    at = 0
    for instruction, outward in zip(found.instructions, external_fields(found)):
        for field in outward:
            mask[at + field.offset:at + field.offset + field.size] = b'\1' * field.size
        at += len(instruction.raw)
    pattern, run = b'', b''
    for byte, masked in zip(raw, mask):
        if masked:
            pattern += re.escape(bytes(run)) + b'.'
            run = b''
        else:
            run += bytes([byte])
    return re.compile(b'(?=' + pattern + re.escape(bytes(run)) + b')', re.S), raw


def skeleton(instruction):
    """An instruction with every number replaced: the fuzzy form."""
    return instruction.mnemonic + ' ' + NUMBER.sub('N', instruction.operands)


def call_targets(found):
    return [f.target for i in found.instructions if i.mnemonic in CALLS or i.mnemonic == 'jmp'
            for f in fields(i) if not (found.start <= f.target < found.end)]


def is_text(memory, start):
    """Whether a start is in a string: its bytes up to a NUL are
    printable or a line's control, at least SHORTEST_TEXT printable."""
    text = memory.bytes(start, start + STRING_LIMIT).split(b'\0', 1)
    printable = sum(32 <= c < 127 for c in text[0])
    return len(text) > 1 and printable >= SHORTEST_TEXT and printable + sum(
        c in TEXT_CONTROLS for c in text[0]) == len(text[0])


def text_like(memory, start, end):
    data = memory.bytes(start, end)
    return bool(data) and sum(32 <= c < 127 or c == 0 for c in data) >= TEXT_FRACTION * len(data)


class Discovery:
    """A memory's functions, found from roots, its vector tables and
    prologues after a return, by following calls and jumps, by pointers
    to the end of a known body, and by decoding on from the end of a
    body while the code decodes cleanly: how each start was found, by
    start; order, the starts sorted; bodies, a cache by start; extents,
    the (start, end) of code known already, a base function's bytes
    matched; rejected, the starts a call, a pointer or a gap gave that
    cannot be code (not_code)."""

    def __init__(self, memory, roots, extents=()):
        self.memory = memory
        self.extents = sorted(extents)
        self.found_by = {a: how for a, how in roots.items() if memory.contains(a) and not memory.is_packed(a)}
        self.bodies = {}
        self.rejected = set()
        self.order = sorted(self.found_by)
        self.low = min(self.found_by, default=memory.base)
        self.high = max(self.found_by, default=memory.base) + BODY_LIMIT
        self.pointers = set()
        for at in range(0, len(memory.data) - 3, 2):
            value = int.from_bytes(memory.data[at:at + 4], 'big')
            if self.low <= value < self.high and value % 2 == 0:
                self.pointers.add(value)
        for at in range(0, len(memory.data) - 3, 2):
            word, before = memory.data[at:at + 2], memory.data[at - 2:at]
            if at and word in PROLOGUES and before in TRANSFERS and self.low <= at + memory.base < self.high:
                if not memory.is_packed(at + memory.base):
                    self.found_by.setdefault(at + memory.base, 'prologue')
        if memory.base == 0:
            for table in range(0, len(memory.data), VECTOR_TABLE_ALIGNMENT):
                if memory.data[table:table + 4] != memory.data[:4]:
                    continue
                for vector in range(table + 4, table + VECTORS * 4, 4):
                    value = int.from_bytes(memory.data[vector:vector + 4], 'big')
                    if LOWEST_ADDRESS <= value < len(memory.data) and value % 2 == 0 and not memory.is_packed(value):
                        self.found_by.setdefault(value, 'vector')
        self.order = sorted(self.found_by)
        self.follow()
        while self.prune():
            self.follow()

    def follow(self):
        """Adds the starts the calls, the pointers and the gaps give,
        until they give no more."""
        memory, low, high = self.memory, self.low, self.high
        while True:
            pending = sorted(self.found_by)
            while pending:
                for target in call_targets(self.body(pending.pop())):
                    if low <= target < high and target % 2 == 0 and target not in self.found_by \
                            and body(memory, target, target + 4).instructions:
                        if self.add(target, 'called'):
                            pending.append(target)
            added = False
            for start in sorted(self.found_by):
                found = self.body(start)
                for candidate in (found.end, found.end + 2):
                    if candidate in self.pointers and candidate not in self.found_by:
                        added = self.add(candidate, 'pointer') or added
                end = found.end
                if found.clean and end not in self.found_by and low <= end < high:
                    trial = body(memory, end, self.stop(end))
                    if trial.clean and len(trial.instructions) > 1 and not text_like(memory, end, trial.end):
                        added = self.add(end, 'gap') or added
            if not added:
                return

    def not_code(self, start):
        """Why a start a call, a pointer or a gap gave cannot be a
        function's, or None: it is data; or it is inside a known extent,
        the instructions of the function before it, decoded as though
        the start were not there, or code that decodes cleanly from that
        function's end, unless the end is padding or data."""
        why = self.data(start)
        if why:
            return why
        at = bisect.bisect_left(self.extents, (start, start))
        if at and self.extents[at - 1][0] < start < self.extents[at - 1][1]:
            return 'inside'
        at = bisect.bisect_left(self.order, start)
        if at:
            before = self.order[at - 1]
            beyond = self.order[at + 1] if self.order[at:at + 1] == [start] and at + 1 < len(self.order) \
                else self.stop(start)
            found = body(self.memory, before, beyond)
            if found.clean and found.end > start:
                return 'inside'
            end = found.end
            if found.clean and end < start and self.memory.bytes(end, end + 2) not in PADDING \
                    and not self.data(end):
                following = body(self.memory, end, beyond)
                if following.clean and following.end > start and not text_like(self.memory, end, following.end):
                    return 'inside'
        return None

    def data(self, start):
        """Why a start is data, or None: it is text, or a NUL, the end of
        the string before it, and then text (0x30aa0 in the Z3815A 4010,
        before "wait_v SIG_SOQ_SCI error in _sci_puts"), or a table of
        pointers, its first long zero or an address."""
        if is_text(self.memory, start) or (self.memory.bytes(start, start + 1) == b'\0'
                                           and is_text(self.memory, start + 1)):
            return 'text'
        first = int.from_bytes(self.memory.bytes(start, start + 4), 'big')
        if first == 0 or is_address(first):
            return 'pointers'
        return None

    def prune(self):
        """Drops each start a call, a pointer or a gap gave that cannot
        be code, lowest first: whether any was."""
        dropped = False
        for start in sorted(self.found_by):
            if self.found_by[start] in WEAK and self.not_code(start):
                self.remove(start)
                dropped = True
        return dropped

    def add(self, start, how):
        """Adds a start, unless it is packed data or cannot be code:
        whether it did."""
        if self.memory.is_packed(start):
            return False
        if how in WEAK and (start in self.rejected or self.not_code(start)):
            self.rejected.add(start)
            return False
        self.found_by[start] = how
        at = bisect.bisect_left(self.order, start)
        if at:
            self.bodies.pop(self.order[at - 1], None)
        self.order.insert(at, start)
        return True

    def remove(self, start):
        del self.found_by[start]
        self.rejected.add(start)
        at = bisect.bisect_left(self.order, start)
        del self.order[at]
        self.bodies.pop(start, None)
        if at:
            self.bodies.pop(self.order[at - 1], None)

    def stop(self, start):
        """The next start past a start."""
        at = bisect.bisect_right(self.order, start)
        return self.order[at] if at < len(self.order) else self.memory.base + len(self.memory.data)

    def body(self, start):
        if start not in self.bodies:
            self.bodies[start] = body(self.memory, start, self.stop(start))
        return self.bodies[start]


def installer_records(image):
    """The boot code's installer records: an S, then for each record C,
    a count, a destination in RAM and the bytes.  Each record's
    destination and bytes, and the extent of them all in the image;
    None if the image has none."""
    start = image.find(b'SC', LOWEST_ADDRESS)
    if start < 0:
        return None
    records, at = [], start + 1
    while image[at:at + 1] == b'C':
        count, destination = struct.unpack('>II', image[at + 1:at + 9])
        if not RAM <= destination <= destination + count <= RAM + RAM_SIZE:
            return None
        records.append((destination, image[at + 9:at + 9 + count]))
        at += 9 + count
    return records, (start, at)


def installer_image(records):
    """The installer's RAM from RAM up, as the boot code unpacks it."""
    ram = bytearray(RAM_SIZE)
    for destination, data in records:
        ram[destination - RAM:destination - RAM + len(data)] = data
    return bytes(ram)


def parse_address(text):
    """A names-file or sidecar address: region and value."""
    if text.startswith('installer::'):
        return 'installer', int(text.split('::')[1], 16)
    return 'primary', int(text, 16)


def load_base(functions_path, names_path, modules_path):
    """The base image's functions and typed labels by region, and every
    named address by region for extents."""
    modules = {}
    if modules_path:
        for line in open(modules_path):
            if line.startswith('#') or not line.strip():
                continue
            row = line.rstrip('\n').split('\t')
            modules[int(row[0], 16)] = row[2]
    items = collections.defaultdict(list)
    starts = collections.defaultdict(set)
    for line in open(functions_path):
        function = json.loads(line)
        region, address = parse_address(function['address'])
        module = 'installer' if region == 'installer' else modules.get(
            address, 'unassigned/' + function.get('family', function['convention']))
        items[region].append(Item('function', address, function['name'], module))
        starts[region].add(address)
    if names_path:
        for line in open(names_path):
            if line.startswith('#') or not line.strip():
                continue
            row = line.rstrip('\n').split('\t')
            if len(row) < 3 or row[1] not in ('label', 'function'):
                continue
            region, address = parse_address(row[0])
            starts[region].add(address)
            if row[1] == 'label' and len(row) > 5 and row[5]:
                items[region].append(Item('label', address, row[2], 'data', type=row[5]))
    return items, starts


def type_size(text):
    array = re.match(r'^(.*?)\s*\[(\d+)\]$', text)
    if array and array.group(1) in TYPE_SIZES:
        return TYPE_SIZES[array.group(1)] * int(array.group(2))
    if text.endswith('*'):
        return 4
    return TYPE_SIZES.get(text)


class Matcher:
    """One region's matching: the base memory and its named items, the
    other memory and the functions found in it (bodies by start, with
    how each start was found), the claims on them and the address
    correspondence the matched instructions give (votes, base address
    to other address counts)."""

    def __init__(self, base, other, items, starts, handlers):
        self.base, self.other = base, other
        self.items = sorted(items, key=lambda i: (i.address, i.kind))
        self.starts = sorted(starts)
        self.handler_pairs, self.handler_owners = handlers
        self.bodies = {}
        self.base_bodies = {}
        self.found_by = {}
        self.claimed = {}
        self.votes = collections.defaultdict(collections.Counter)
        # where each vote comes from: (base address, other address, the
        # voting base function's or piece's start, the instruction's
        # address, the field's index in it), for tools/firmware/seed_names.py
        self.references = set()
        self.reverse = collections.defaultdict(collections.Counter)
        self.names = {}
        self.unclaimed_cache = None
        self.anchors_cache = None
        self.hits = {}
        named = [i for i in self.items if i.kind == 'function']
        ours = Discovery(base, {f.address: 'named' for f in named})
        modules = sorted((f.address, f.module) for f in named)
        pieces = [Item('piece', a, 'piece_%x' % a, modules[max(bisect.bisect_right(modules, (a, '')) - 1, 0)][1])
                  for a, how in ours.found_by.items() if how != 'named' and ours.body(a).instructions]
        self.items = sorted(self.items + pieces, key=lambda i: (i.address, i.kind))
        self.functions = [i for i in self.items if i.kind in ('function', 'piece')]
        for function in self.functions:
            self.base_bodies[function.address] = ours.body(function.address) if function.kind == 'piece' \
                else body(base, function.address, self.next_start(function.address))
            function.size = self.base_bodies[function.address].end - function.address
        for item in self.items:
            self.names.setdefault(item.address, item)
        self.labels = sorted((i.address, i) for i in self.items if i.kind == 'label')

    def next_start(self, address):
        at = bisect.bisect_right(self.starts, address)
        return self.starts[at] if at < len(self.starts) else self.base.base + len(self.base.data)

    def run(self):
        self.match_bytes()
        roots = {f.other: 'matched' for f in self.functions if f.other is not None}
        for _, theirs in self.handler_pairs:
            roots.setdefault(theirs, 'handler')
        if self.other.base == 0:
            roots.setdefault(int.from_bytes(self.other.data[4:8], 'big'), 'reset')
        matched = [(f.other, f.other + f.size) for f in self.functions
                    if f.other is not None and f.method in ('exact', 'masked')]
        self.theirs = Discovery(self.other, roots, matched)
        self.found_by = self.theirs.found_by
        self.match_fuzzy()
        for _ in range(3):
            self.match_handlers()
            self.match_similar()
        self.match_by_callees()
        self.match_hits(within_gap=False)
        for function in self.functions:
            if function.other is not None:
                self.classify(function)
        self.match_references()
        for theirs_by_ours in list(self.votes.items()):
            ours, theirs = theirs_by_ours
            for address, count in theirs.items():
                self.reverse[address][ours] += count
        for function in self.functions:
            if function.status == 'changed':
                function.summary = self.summarize(self.base_bodies[function.address], self.counterpart(function))
                if function.method == 'references' and function.similarity < SIMILAR_WITH_CALLEES:
                    function.summary = 'replaced: its callers call this routine in its place; ' + function.summary
        self.locate_labels()
        return self.items + self.new_functions()

    def match_references(self):
        """Pairs each function still unmatched with the address that the
        matched callers' aligned calls name in its place, until no more
        pair: a function changed past its shape is still called from the
        same places."""
        while True:
            paired = False
            for function in self.functions:
                votes = self.votes.get(function.address)
                if function.other is not None or not votes:
                    continue
                target = votes.most_common(1)[0][0]
                if self.other.contains(target) and not self.other.is_packed(target) and self.free(function, target):
                    if target not in self.found_by:
                        self.theirs.add(target, 'referenced')
                        self.unclaimed_cache = None
                    if self.theirs.body(target).instructions:
                        self.claim(function, target, 'references')
                        self.classify(function)
                        paired = True
            if not paired:
                return

    def free(self, function, other):
        """Whether a function may take an address of the other image:
        unclaimed, and not another handler's counterpart."""
        owners = self.handler_owners.get(other)
        return other not in self.claimed and (owners is None or function.address in owners)

    def claim(self, function, other, method):
        function.other, function.method = other, method
        self.claimed[other] = function
        self.unclaimed_cache = None
        self.anchors_cache = None

    def anchors(self):
        if self.anchors_cache is None:
            self.anchors_cache = sorted((f.address, f.other) for f in self.functions if f.other is not None)
        return self.anchors_cache

    def expected(self, address):
        """Where the other image should have a base address, from the
        nearest matched function below it."""
        anchors = self.anchors()
        at = bisect.bisect_right(anchors, (address, 1 << 40)) - 1
        if at < 0:
            return address
        return anchors[at][1] + address - anchors[at][0]

    def gap(self, address):
        """The other image's range between the counterparts of the
        nearest matched functions either side of a base address."""
        anchors = self.anchors()
        at = bisect.bisect_left(anchors, (address, -1))
        low = anchors[at - 1][1] if at > 0 else self.other.base
        high = anchors[at][1] if at < len(anchors) else self.other.base + len(self.other.data)
        return (low, high) if low < high else (min(low, high) - BODY_LIMIT, max(low, high) + BODY_LIMIT)

    def match_bytes(self):
        """Exact, then masked, matches: unique hits first, the rest by
        the nearest hit to where the matched neighbors put them."""
        hits = {}
        for function in self.functions:
            found = self.base_bodies[function.address]
            if not found.instructions:
                continue
            pattern, raw = masked_pattern(found)
            exact = [m.start() + self.other.base for m in re.finditer(b'(?=' + re.escape(bytes(raw)) + b')', self.other.data)
                     if m.start() % 2 == 0]
            if exact:
                hits[function.address] = ('exact', exact)
                continue
            masked = [m.start() + self.other.base for m in pattern.finditer(self.other.data) if m.start() % 2 == 0]
            if masked:
                hits[function.address] = ('masked', masked)
        self.hits = hits
        for function in self.functions:
            method, found = hits.get(function.address, ('', []))
            if len(found) == 1 and self.free(function, found[0]) and (method == 'exact' or function.size >= UNIQUE_SIZE):
                self.claim(function, found[0], method)
        self.match_hits(within_gap=True)

    def match_hits(self, within_gap):
        """Claims for each function still unmatched the free byte match
        nearest where its matched neighbors put it: only between their
        counterparts if within_gap."""
        for function in self.functions:
            method, found = self.hits.get(function.address, ('', []))
            low, high = self.gap(function.address) if within_gap else (self.other.base, 1 << 40)
            free = [a for a in found if self.free(function, a) and low <= a < high]
            if function.other is None and free and (within_gap or method == 'exact' or function.size >= FAR_SIZE):
                expected = function.address if function.address in free else self.expected(function.address)
                self.claim(function, min(free, key=lambda a: abs(a - expected)), method)

    def unclaimed(self):
        if self.unclaimed_cache is None:
            self.unclaimed_cache = [a for a in sorted(self.found_by)
                                    if a not in self.claimed and self.theirs.body(a).instructions]
        return self.unclaimed_cache

    def match_fuzzy(self):
        index = collections.defaultdict(list)
        for start in self.unclaimed():
            index[tuple(map(skeleton, self.theirs.body(start).instructions))].append(start)
        for function in self.functions:
            found = self.base_bodies[function.address]
            if function.other is not None or len(found.instructions) < 2:
                continue
            low, high = self.gap(function.address)
            free = [a for a in index.get(tuple(map(skeleton, found.instructions)), []) if self.free(function, a)
                    and (low <= a < high or len(found.instructions) >= 2 * SMALL)]
            if free:
                expected = self.expected(function.address)
                self.claim(function, min(free, key=lambda a: abs(a - expected)), 'fuzzy')

    def ratio(self, function, start):
        ours = list(map(skeleton, self.base_bodies[function.address].instructions))
        theirs = list(map(skeleton, self.theirs.body(start).instructions))
        if not ours or not theirs or not 1 / 3 <= len(ours) / len(theirs) <= 3:
            return 0.0
        return difflib.SequenceMatcher(None, ours, theirs, autojunk=False).ratio()

    def best(self, function, scored):
        """The best scored candidate; among those within TIE of it, the
        one whose instructions are most alike as they stand, numbers
        included, so that twins differing in their device registers
        pair by those."""
        top = max(scored, default=(0.0, None))
        tied = [a for score, a in scored if score >= top[0] - TIE]
        if len(tied) < 2:
            return top
        ours = [i.mnemonic + ' ' + i.operands for i in self.base_bodies[function.address].instructions]

        def text(start):
            theirs = [i.mnemonic + ' ' + i.operands for i in self.theirs.body(start).instructions]
            return difflib.SequenceMatcher(None, ours, theirs, autojunk=False).ratio()
        chosen = max(tied, key=text)
        return dict((a, score) for score, a in scored)[chosen], chosen

    def match_handlers(self):
        for ours, theirs in self.handler_pairs:
            function = next((f for f in self.functions if f.address == ours), None)
            if function and function.other is None and theirs not in self.claimed \
                    and theirs in self.found_by and self.ratio(function, theirs) > 0:
                self.claim(function, theirs, 'handler')

    def match_similar(self):
        for function in self.functions:
            if function.other is not None or not self.base_bodies[function.address].instructions:
                continue
            low, high = self.gap(function.address)
            scored = [(self.ratio(function, a), a) for a in self.unclaimed()
                      if low <= a < high and self.free(function, a)]
            best = self.best(function, scored)
            small = len(self.base_bodies[function.address].instructions) < SMALL
            if best[0] >= (SIMILAR_SMALL if small else SIMILAR):
                self.claim(function, best[1], 'similar')

    def match_by_callees(self):
        """Pairs the rest by the counterparts of the functions they call."""
        counterpart = {f.address: f.other for f in self.functions if f.other is not None}
        callers = collections.defaultdict(set)
        for start in self.unclaimed():
            for target in call_targets(self.theirs.body(start)):
                callers[target].add(start)
        for function in self.functions:
            if function.other is not None:
                continue
            mapped = {counterpart[t] for t in call_targets(self.base_bodies[function.address]) if t in counterpart}
            candidates = set().union(*(callers[t] for t in mapped)) if mapped else set()
            low, high = self.gap(function.address)
            scored = []
            for start in (c for c in candidates if self.free(function, c)):
                theirs = set(call_targets(self.theirs.body(start)))
                overlap = len(mapped & theirs) / len(mapped | theirs)
                ratio = self.ratio(function, start)
                score = 0.5 * ratio + 0.5 * overlap
                if ratio >= SIMILAR_WITH_CALLEES and (low <= start < high or score >= SIMILAR_FAR):
                    scored.append((score, start))
            best = self.best(function, scored)
            if best[0] >= SIMILAR and self.free(function, best[1]):
                self.claim(function, best[1], 'callees')

    def counterpart(self, function):
        """A matched function's body in the other image: as long as the
        base's for a byte match, else as found there."""
        ours = self.base_bodies[function.address]
        if function.method in ('exact', 'masked'):
            return Body(function.other, self.other.instructions(function.other, function.other + function.size),
                        ours.clean)
        return self.theirs.body(function.other)

    def classify(self, function):
        ours = self.base_bodies[function.address]
        theirs = self.counterpart(function)
        function.other_size = theirs.end - theirs.start
        our_raw = b''.join(i.raw for i in ours.instructions)
        their_raw = b''.join(i.raw for i in theirs.instructions)
        pattern, _ = masked_pattern(ours)
        if our_raw == their_raw:
            function.status, function.similarity = 'identical', 1.0
        elif len(our_raw) == len(their_raw) and pattern.match(their_raw):
            function.status, function.similarity = 'relocated', 1.0
        else:
            function.status = 'changed'
            function.similarity = round(self.ratio(function, function.other), 3)
        self.vote(ours, theirs)

    def vote(self, ours, theirs):
        """Counts the address pairs that aligned instructions name."""
        our_skeletons = list(map(skeleton, ours.instructions))
        their_skeletons = list(map(skeleton, theirs.instructions))
        matcher = difflib.SequenceMatcher(None, our_skeletons, their_skeletons, autojunk=False)
        for tag, a1, a2, b1, b2 in matcher.get_opcodes():
            if tag != 'equal':
                continue
            for mine, yours in zip(ours.instructions[a1:a2], theirs.instructions[b1:b2]):
                for index, (f, g) in enumerate(zip(fields(mine), fields(yours))):
                    if not (ours.start <= f.target < ours.end):
                        self.votes[f.target][g.target] += 1
                        self.references.add((f.target, g.target, ours.start, mine.address, index))

    def base_name(self, address):
        """A base address as a named item, or an offset into a label."""
        if address in self.names:
            return self.names[address].name
        at = bisect.bisect_right(self.labels, (address, )) - 1
        if at >= 0:
            start, label = self.labels[at]
            if address < start + max(label.size, type_size(label.type) or 0):
                return '%s+%d' % (label.name, address - start)
        return None

    def describe(self, side, address, found):
        """An address as a summary names it: its base name, through the
        matches for the other image's."""
        if found.start <= address < found.end:
            return 'L+%d' % (address - found.start)
        if side == 'other':
            mine = self.claimed.get(address)
            if mine:
                return mine.name
            ours = self.reverse[address].most_common(1)[0][0] if address in self.reverse else None
            if ours is not None and self.base_name(ours):
                return self.base_name(ours)
            text = self.other.string(address)
            if text:
                return repr(text)
            return '0x%x' % ours if ours is not None else 'other:0x%x' % address
        name = self.base_name(address)
        if name:
            return name
        text = self.base.string(address)
        return repr(text) if text else '0x%x' % address

    def named(self, side, instruction, found):
        text = instruction.operands
        for field in fields(instruction):
            name = self.describe(side, field.target, found)
            text = re.sub(r'0x%x\b' % field.target, name, text, count=1) if '0x%x' % field.target in text \
                else re.sub(r'#%d\b' % signed(field.target, 32), '#' + name, text, count=1)
        return (instruction.mnemonic + ' ' + text).strip()

    def summarize(self, ours, theirs):
        mine = [self.named('base', i, ours) for i in ours.instructions]
        yours = [self.named('other', i, theirs) for i in theirs.instructions]
        replaced = inserted = deleted = 0
        examples = []
        for tag, a1, a2, b1, b2 in difflib.SequenceMatcher(None, mine, yours, autojunk=False).get_opcodes():
            if tag == 'equal':
                continue
            replaced += min(a2 - a1, b2 - b1) if tag == 'replace' else 0
            deleted += max(a2 - a1 - (b2 - b1), 0) if tag == 'replace' else a2 - a1
            inserted += max(b2 - b1 - (a2 - a1), 0) if tag == 'replace' else b2 - b1
            if len(examples) < EXAMPLES:
                examples.append('%s -> %s' % ('; '.join(mine[a1:a2][:2]) or '-', '; '.join(yours[b1:b2][:2]) or '-'))
        calls_mine = collections.Counter(m.split(' ', 1)[1] for m in mine if m.split(' ')[0] in CALLS)
        calls_yours = collections.Counter(y.split(' ', 1)[1] for y in yours if y.split(' ')[0] in CALLS)
        strings_mine = set(re.findall(r"'[^']*'", ' '.join(mine)))
        strings_yours = set(re.findall(r"'[^']*'", ' '.join(yours)))
        parts = ['%d -> %d instructions: %d replaced, %d inserted, %d deleted'
                 % (len(mine), len(yours), replaced, inserted, deleted)]
        if calls_yours - calls_mine:
            parts.append('calls added: ' + ', '.join(sorted(calls_yours - calls_mine)))
        if calls_mine - calls_yours:
            parts.append('calls removed: ' + ', '.join(sorted(calls_mine - calls_yours)))
        if strings_yours - strings_mine:
            parts.append('strings added: ' + ', '.join(sorted(strings_yours - strings_mine)))
        if strings_mine - strings_yours:
            parts.append('strings removed: ' + ', '.join(sorted(strings_mine - strings_yours)))
        if not examples:
            # the same once addresses are named: the difference is in an
            # address outside the ROM and RAM, a device's
            examples = ['%s -> %s' % (a.operands, b.operands)
                        for a, b in zip(ours.instructions, theirs.instructions)
                        if a.operands != b.operands
                        and any(not is_address(f.target) for f in fields(a))][:EXAMPLES]
            parts[0] = 'same instructions, device addresses differ'
        parts.append('first: ' + ' | '.join(examples))
        return '; '.join(parts)

    def diff(self, function):
        """The full instruction diff of a matched function."""
        ours = self.base_bodies[function.address]
        theirs = self.counterpart(function)
        mine = ['%x  %s' % (i.address, self.named('base', i, ours)) for i in ours.instructions]
        yours = ['%x  %s' % (i.address, self.named('other', i, theirs)) for i in theirs.instructions]
        strip = [m.split('  ', 1)[1] for m in mine], [y.split('  ', 1)[1] for y in yours]
        lines = []
        for tag, a1, a2, b1, b2 in difflib.SequenceMatcher(None, *strip, autojunk=False).get_opcodes():
            lines += [('  ' if tag == 'equal' else '- ') + m for m in mine[a1:a2]]
            if tag != 'equal':
                lines += ['+ ' + y for y in yours[b1:b2]]
        return '\n'.join(lines)

    def locate_labels(self):
        """Places each label by the votes for addresses within it, else
        by its bytes, and compares a ROM label's bytes."""
        for label in (i for i in self.items if i.kind == 'label'):
            size = type_size(label.type)
            limit = self.next_start(label.address) - label.address
            label.size = min(size or limit, TABLE_LIMIT)
            # a table is in the ROM, or in what the installer's records fill
            rom = self.base.contains(label.address) and (
                self.base.base == 0 or any(self.base.bytes(label.address, label.address + label.size)))
            implied = collections.Counter()
            for address in range(label.address, label.address + max(label.size, 1)):
                for theirs, count in self.votes.get(address, {}).items():
                    implied[theirs - (address - label.address)] += count
            if implied:
                label.other, label.method = implied.most_common(1)[0][0], 'references'
            elif rom and label.size >= 2:
                data = self.base.bytes(label.address, label.address + label.size)
                hits = [m.start() + self.other.base for m in re.finditer(b'(?=' + re.escape(data) + b')', self.other.data)
                        if m.start() % 2 == 0]
                expected = self.expected(label.address)
                near = [a for a in hits if abs(a - expected) < BODY_LIMIT]
                if len(hits) == 1 and label.size >= 8 or near:
                    label.other, label.method = min(hits, key=lambda a: abs(a - expected)), 'bytes'
            if label.other is None:
                continue
            if not (rom and self.base.contains(label.address) and self.other.contains(label.other)):
                label.kind = 'global'
                label.status = 'identical' if label.other == label.address else 'relocated'
                continue
            ours = self.base.bytes(label.address, label.address + label.size)
            theirs = self.other.bytes(label.other, label.other + label.size)
            if ours != theirs and len(ours) >= 2:
                # a table reached through another lands its votes off its
                # start; its own bytes near there place it
                hits = [m.start() + self.other.base for m in re.finditer(b'(?=' + re.escape(ours) + b')', self.other.data)
                        if abs(m.start() + self.other.base - label.other) < BODY_LIMIT]
                if hits:
                    label.other = min(hits, key=lambda a: abs(a - label.other))
                    label.method = 'bytes'
                    theirs = ours
            label.other_size = len(theirs)
            if ours == theirs:
                label.status, label.similarity = 'identical', 1.0
                continue
            differ = [k for k in range(min(len(ours), len(theirs))) if ours[k] != theirs[k]]
            # a differing byte is a pointer's when some long over it is
            # an address in both images
            outside = [k for k in differ if not any(
                is_address(int.from_bytes(ours[s:s + 4], 'big'))
                and is_address(int.from_bytes(theirs[s:s + 4], 'big'))
                for s in range(max(k - 3, 0), k + 1) if s + 4 <= len(theirs))]
            label.similarity = round(1 - len(outside) / max(len(ours), 1), 3)
            if not outside and len(ours) == len(theirs):
                label.status = 'relocated'
            else:
                label.status = 'changed'
                label.summary = '%d of %d bytes differ, %d outside pointers, first at +%d' % (
                    len(differ), len(ours), len(outside), outside[0] if outside else differ[0])

    def new_functions(self):
        """The other image's functions no base function matched, but for
        a start inside a ROM table placed there, which is data."""
        tables = [(i.other, i.other + max(i.other_size, 1)) for i in self.items
                  if i.kind == 'label' and i.other is not None]
        return [self.new_function(start, self.found_by[start]) for start in self.unclaimed()
                if not any(low <= start < high for low, high in tables)]

    def new_function(self, start, how):
        """The other image's function at start, found as how says, as a
        new item: its extent to the next start found, and a summary."""
        mapped = sorted((f.other, f) for f in self.functions if f.other is not None)
        theirs = self.theirs.body(start)
        at = bisect.bisect_right([m[0] for m in mapped], start) - 1
        near = mapped[at][1] if at >= 0 else None
        calls = sorted({self.describe('other', t, theirs) for t in call_targets(theirs)})
        strings = sorted({repr(s) for i in theirs.instructions for f in fields(i)
                          for s in [self.other.string(f.target)] if s})
        summary = ['%d instructions, found %s' % (len(theirs.instructions), how)]
        if near:
            summary.append('after %s' % near.name)
        if calls:
            summary.append('calls: ' + ', '.join(calls))
        if strings:
            summary.append('strings: ' + ', '.join(strings))
        return Item('function', -1, '', near.module if near else 'unassigned', status='new', method=how,
                    other=start, other_size=theirs.end - start, summary='; '.join(summary))


def handler_pairs(paths):
    """Base and other handler addresses for the same SCPI path, from two
    `dump-scpi --handlers` listings: the (base, other) pairs, the most
    common other for each base handler; and for every other handler, the
    base handlers of its paths, empty for one whose paths the base lacks."""
    if not paths:
        return [], {}
    listings = []
    for path in paths:
        handlers = {}
        for line in open(path):
            row = line.split()
            if len(row) == 3:
                handlers[row[0]] = row[1:]
        listings.append(handlers)
    pairs = collections.Counter()
    owners = collections.defaultdict(set)
    for path, theirs in listings[1].items():
        for mine, yours in zip(listings[0].get(path, ['-', '-']), theirs):
            if yours != '-':
                owners[int(yours, 16)].update([int(mine, 16)] if mine != '-' else [])
    for path, ours in listings[0].items():
        for mine, yours in zip(ours, listings[1].get(path, ['-', '-'])):
            if mine != '-' and yours != '-':
                pairs[int(mine, 16), int(yours, 16)] += 1
    best = {}
    for (mine, yours), count in pairs.most_common():
        best.setdefault(mine, yours)
    return sorted(best.items()), dict(owners)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('base')
    parser.add_argument('other')
    parser.add_argument('--functions', required=True)
    parser.add_argument('--names')
    parser.add_argument('--modules')
    parser.add_argument('--handlers', nargs=2)
    parser.add_argument('--installer', action='store_true')
    parser.add_argument('--diff', nargs='*', default=[])
    arguments = parser.parse_args()
    base_image = open(arguments.base, 'rb').read()
    other_image = open(arguments.other, 'rb').read()
    ranges = [(LOWEST_ADDRESS, max(len(base_image), len(other_image))), (RAM, RAM + RAM_SIZE)]
    ADDRESS_RANGES[:] = ranges
    items, starts = load_base(arguments.functions, arguments.names, arguments.modules)
    ours, theirs = installer_records(base_image), installer_records(other_image)
    regions = [('primary', Memory(base_image, 0, ours and ours[1]),
                Memory(other_image, 0, theirs and theirs[1]), handler_pairs(arguments.handlers))]
    if arguments.installer:
        if ours and theirs:
            regions.append(('installer', Memory(installer_image(ours[0]), RAM),
                            Memory(installer_image(theirs[0]), RAM), ([], {})))
        else:
            print('no installer records in %s' % (arguments.base if not ours else arguments.other), file=sys.stderr)
    out = sys.stdout
    out.write('# image_diff.py %s against %s\n' % (os.path.basename(arguments.base), os.path.basename(arguments.other)))
    out.write('\t'.join(['region', 'kind', 'status', 'method', 'base_address', 'name', 'module', 'base_size',
                         'other_address', 'other_size', 'similarity', 'summary']) + '\n')
    counts = collections.Counter()
    for region, base, other, pairs in regions:
        matcher = Matcher(base, other, items[region], starts[region], pairs)
        results = matcher.run()
        for item in sorted(results, key=lambda i: (i.kind != 'function', i.status == 'new',
                                                    i.address if i.address >= 0 else i.other)):
            out.write('\t'.join([region, item.kind, item.status, item.method,
                                 '' if item.address < 0 else '0x%x' % item.address, item.name, item.module,
                                 str(item.size) if item.address >= 0 else '',
                                 '' if item.other is None else '0x%x' % item.other,
                                 str(item.other_size) if item.other is not None else '',
                                 '%.3f' % item.similarity if item.status == 'changed' else '',
                                 item.summary]) + '\n')
            counts[region, item.kind, '/'.join(item.module.split('/')[:2]), item.status] += 1
        for name in arguments.diff:
            function = next((f for f in matcher.functions if f.name == name), None)
            if function and function.other is not None:
                print('== %s 0x%x -> 0x%x %s' % (name, function.address, function.other, function.status), file=sys.stderr)
                print(matcher.diff(function), file=sys.stderr)
    totals = collections.Counter()
    for (region, kind, module, status), count in sorted(counts.items()):
        totals[region, kind, status] += count
        print('%s\t%s\t%s\t%s\t%d' % (region, kind, module, status, count), file=sys.stderr)
    for (region, kind, status), count in sorted(totals.items()):
        print('total\t%s\t%s\t%s\t%d' % (region, kind, status, count), file=sys.stderr)


if __name__ == '__main__':
    main()
