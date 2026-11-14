# /// script
# requires-python = ">=3.11"
# dependencies = ["unicorn==2.1.4"]
# ///
"""Checks docs/firmware/gps-fields.tsv, the fields of every message the
GPS engine of crates/smartclock-gps speaks, against what the images do:
which bytes of each message an image's own decoders read, measured by
running them in Unicorn, what becomes of each field read, and which
commands its request scripts send.

    uv run -q tools/firmware/gps_fields.py [--check]

Without --check it rewrites the table's images, compared and use
columns and the list of gaps between the markers in
docs/firmware/gps.md; with it, it fails if either is not what the
images give.  Either way it fails if a message's rows do not tile its
data, if an image reads part of a field or outside the data, if a
row's engine column is not in the vocabulary below, or if
docs/firmware/gps-field-uses.tsv does not account for each function
found reading a field, and prints the table's size, each image's read
set, compared set and counts of fields by use, and the gaps.

The images are the four with an Oncore message table and rewritten C:
the Z3805A 3543B, the Z3801A 3543, the 58503A 3704 and the Z3817A
3752.  The Z3815A speaks NMEA with Furuno's sentences, which the engine
does not, and has no row.

A message's rows cover its data alone, by offset from the frame's
first @: the four bytes of @@ and ID before it and the checksum, CR and
LF after it are every message's and have no row.  A field repeated,
one a channel or a satellite, is one row with a count and the stride
from each to the next.  A byte the manual lays out by bit, of which
the images use some bits and not others, has a row for each bit or run
of bits, all at the byte's offset, with the bits column naming them,
7 the most significant: 5, or 6-4.  Between them such rows give each
bit of the byte once; any other row's bits are "-".

Reads are measured, not read off the C.  Each decoder of an image's
message table (docs/firmware/oncore/IMAGE.txt) is called as
gps_response_decode calls it, with a cursor at the data of a message
of the table's length and its record, in the image's ROM with RAM as
its own code leaves it: the fix mode set to position fix, which the
status decoders need, and the channel count set by the image's own
gps_channel_count_update, to eight by its own @@Ea decoders first.  A
six-channel message is decoded with six channels, an eight-channel one
with eight, any other with each.  A read of the message's bytes while
a decoder runs is a read, whatever the decoder then does with the
byte.  Bytes are filled with each of FILLS in turn and the reads
united.  Trap #7, by which a status decoder asks its own status
register, runs the image's handler; any other exception or an access
outside ROM and RAM fails the run.

The use column says what becomes of a field an image reads, the
strongest of what its readers do with it, one class if the images
agree and IMAGE=CLASS for each if not:

    control    it reaches the loop, a holdover or lock decision, the
               time or 1PPS handling, an alarm or a status register
    reported   it reaches a SCPI reply, the status screen, the
               diagnostic log or the front panel
    console    only a console word's report shows it
    stored     nothing is found to read where it is stored

Where a field is stored is measured: its bytes are flipped by each of
FLIPS, of a row of bits those bits alone, and the decoders run, and
the bytes of RAM that change are its stores.  That takes in what a decoder derives from the field, so a
store may hold other fields' bits too.

Who reads a store is a scan of the primary's instructions
(record_access.py), following each decoder's record and every absolute
address from the first store to the last: a function with an
instruction that reads a store, and not only to write it back, is seen
to read it.  Where an address leaves the scan's sight (WALKS), the
function is taken to reach every field stored from there to the end
of that record.  A ROM table that holds a store's address makes each
function naming the table reach it.  The decoders' run is the check
on the scan: a read of a store while they run by a function the scan
has neither seeing nor reaching it, nor called by one that does, is a
reader too.

What a reader does with the field is by hand: gps-field-uses.tsv
gives each function, for the images, messages and fields a row names
or all of them, a class or none, a reader that does nothing with the
field, with the reading of the rewritten C that says so.  The most
specific row wins, a field over a function over a message over an
image.  The tool fails on a reader no row covers, a row that wins
nothing, and a function cited that an image lacks.  It does not check
the reading itself.

What this can miss.  A store reached only through a pointer kept in
RAM, or computed, has no reader, and its field comes out stored.  The
scan sees which functions read a store, not which of its bits: the
rows of a byte split by bit have the same readers, and which reader
tests which bit is the hand table's to say, as it is for a store that
takes bits from several fields.  A function reaching a field need not read it: a row says
where it does not.  A use two calls from the reader is the reading's
to follow, and only the functions cited are checked to exist.  The
run covers the decoders alone, no other task.

The compared column is the images whose gps_response_match compares
the field's bytes with the command sent, measured: the routine runs
on each response with a command of the same bytes and the offsets of
the response it reads are noted.  The frame's trailer, compared too,
has no row.

A command field's images are those whose scripts or lists send the
command to set it, mode 0 or 2 (docs/firmware/gps.md, "Requests"); a
query sends no field.

The engine column says where crates/smartclock-gps/src/engine.rs gets
a response field, as terms joined by ";", each with an optional note
after ":":

    record.PATH   a field of the Record the engine is given
    setting.ID    a setting the command ID keeps
    clock         the engine's clock, set from the record's time
    state         other state a command left
    identity      the engine's identity
    echo          the command's own bytes
    computed      worked out from the above
    fixed         a constant the manual gives the field
    constant      a constant the manual does not give it
    zero          zero, the engine having nothing to say
    unknown       the manual's value for not known

and what it does with a command field: honored, stored (kept and
answered, with no effect), echoed (answered, not kept) or ignored.  A
gap is a response field with a constant, zero or unknown term that an
image uses, as control or reported, or compares, or a command field
an image sets that is not honored.  Such a response field that an
image only shows on the console or only stores is listed apart.
"""

import argparse
import bisect
import json
import pathlib
import re
import struct
import sys
from dataclasses import dataclass

from unicorn import (
    UC_ARCH_M68K,
    UC_HOOK_INTR,
    UC_HOOK_MEM_INVALID,
    UC_HOOK_MEM_READ,
    UC_MODE_BIG_ENDIAN,
    Uc,
    UcError,
)
from unicorn.m68k_const import (
    UC_CPU_M68K_M68020,
    UC_M68K_REG_A7,
    UC_M68K_REG_D0,
    UC_M68K_REG_PC,
    UC_M68K_REG_SR,
)

import record_access
from loop_trace import CALL_LIMIT, RAM, RAM_SIZE, RETURN, ROM_SIZE, STACK, SUPERVISOR, globals_of, names

ROOT = pathlib.Path(__file__).resolve().parents[2]
FIRMWARE = ROOT / 'third_party/firmware'
TABLE = ROOT / 'docs/firmware/gps-fields.tsv'
USES = ROOT / 'docs/firmware/gps-field-uses.tsv'
DOCUMENT = ROOT / 'docs/firmware/gps.md'
RECORD = ROOT / 'crates/smartclock-gps/src/record.rs'
ONCORE = ROOT / 'docs/firmware/oncore'

# Each image's primary vector table, as harness.py's profiles have it.
IMAGES = {
    'z3805a-3543b': 0x10000,
    'z3801a-3543': 0x10000,
    '58503a-3704': 0x10000,
    'z3817a-3752': 0x20000,
}

# A frame's bytes before and after its data: oncore::HEADER and
# oncore::TRAILER of crates/smartclock-gps/src/oncore.rs.
HEADER = 4
TRAILER = 3

# The per-channel messages, as MessageId::channels has them.
SIX_CHANNEL = frozenset({'Ba', 'Bc', 'Bk', 'Bn', 'Ca'})
EIGHT_CHANNEL = frozenset({'Ea', 'Ek', 'En', 'Fa'})
STATUS_EIGHT = 'Ea'
# The message whose record is gps.fix_mode, and position fix there.
FIX_MODE = 'Cg'
POSITION_FIX = 1
# The one message the images' tables frame with no data where the
# manuals give it a byte (oncore.rs, SZ_TABLE_LENGTH).
FRAMED_SHORT = frozenset({'Sz'})

FILLS = (0x00, 0x01, 0x55)
# The bits a field's bytes are flipped by to find where it is stored.
FLIPS = (0xFF, 0x80, 0x02, 0x01)
MESSAGE = RAM + RAM_SIZE - 0x10000
MESSAGE_SPACE = 0x1000
# Where the command is put that gps_response_match compares a response
# at MESSAGE with, and the routine's arguments as its callers push
# them: the message's index, the command and its length, the mode, the
# response's index, the response and its length.
SENT = MESSAGE + MESSAGE_SPACE // 2
MATCH = 'gps_response_match'
MATCH_ARGUMENTS = '>HIBxBxHIH'
# The offset from a decoder's entry of its record, its second argument.
RECORD_ARGUMENT = 8
# The most a walk may start before the first store it is taken to
# reach: an element of a list indexed from one.
WALK_BEFORE = 16
# How many calls deep a pushed address is followed.
FOLLOW_DEPTH = 6
# Trap #7's vector, and a format 0 exception frame: status register,
# return address, format and vector offset.
TRAP_7 = 39
TRAP_SIZE = 2
VECTOR_SIZE = 4
FRAME = '>HIH'
# What QEMU, under Unicorn, raises for an rte (EXCP_RTE), calling the
# interrupt hook in place of returning.
RETURN_FROM_EXCEPTION = 0x100

COLUMNS = (
    'message',
    'direction',
    'field',
    'offset',
    'length',
    'count',
    'stride',
    'bits',
    'manual',
    'page',
    'images',
    'compared',
    'use',
    'engine',
    'notes',
)
NONE = '-'
BYTE_BITS = 8
DIRECTIONS = ('command', 'response')
MANUALS = ('VP', 'UT', 'M12+')
SOURCES = (
    'record',
    'setting',
    'clock',
    'state',
    'identity',
    'echo',
    'computed',
    'fixed',
    'constant',
    'zero',
    'unknown',
)
GAP_SOURCES = ('constant', 'zero', 'unknown')
HANDLINGS = ('honored', 'stored', 'echoed', 'ignored')
HONORED = 'honored'
# What a function does with a field it reads, weakest first, and the
# use of a field no function reads.
CLASSES = ('none', 'console', 'reported', 'control')
STORED = 'stored'
USED = ('reported', 'control')
USE_COLUMNS = ('function', 'images', 'message', 'field', 'use', 'evidence')
ANY = '*'
# A function the hand table's evidence cites.
CITED = re.compile(r'(\w+)\(\)')
# A bits cell: one bit, or the highest and lowest of a run.
BITS = re.compile(r'[0-7](-[0-7])?')
# How the trace has a function reading a field, a later one taken
# where it has two: the decoders, run, read it there; the scan sees a
# walk that reaches it; the scan sees the read.
KINDS = ('ran', 'reach', 'seen')
# The request modes that set a value (gps.md, "Requests").
SET_MODES = (0, 2)

BEGIN = '<!-- gps_fields.py gaps -->'
END = '<!-- end gps_fields.py -->'


@dataclass(frozen=True)
class Entry:
    """One entry of an image's message table: its ID, the length it
    frames the response at (0 for none) and its decoders, each with the
    record it fills."""

    index: int
    id: str
    length: int
    decoders: tuple[tuple[int, int], ...]


@dataclass(frozen=True)
class Requests:
    """An image's message table and the IDs its scripts and lists send,
    each with the modes it is sent in."""

    entries: tuple[Entry, ...]
    sent: dict[str, frozenset[int]]


@dataclass
class Row:
    """One row of the table: a field, or count fields stride apart."""

    line: int
    message: str
    direction: str
    field: str
    offset: int
    length: int
    count: int
    stride: int
    bits: str
    manual: str
    page: str
    images: str
    compared: str
    use: str
    engine: str
    notes: str

    def instances(self) -> list[range]:
        """The bytes of each field of the row."""
        return [
            range(self.offset + index * self.stride, self.offset + index * self.stride + self.length)
            for index in range(self.count)
        ]

    def mask(self) -> int:
        """The bits of its bytes that are the row's: all of them, or
        those its bits column names, the highest first."""
        if self.bits == NONE:
            return (1 << BYTE_BITS) - 1
        high, _, low = self.bits.partition('-')
        return sum(1 << bit for bit in range(int(low or high), int(high) + 1))

    def terms(self) -> list[tuple[str, str]]:
        """The engine column's terms, each with its note."""
        return [tuple(term.partition(':')[::2]) for term in self.engine.split(';')]

    def cells(self) -> list[str]:
        stride = NONE if self.count == 1 else str(self.stride)
        return [stride if name == 'stride' else str(getattr(self, name)) for name in COLUMNS]


def requests(image: str) -> Requests:
    """An image's message table and what it sends, from dump-oncore's
    text."""
    entries = []
    sent: dict[str, set[int]] = {}
    for line in (ONCORE / f'{image}.txt').read_text().splitlines():
        entry = re.match(r'message (0x[0-9a-f]+) (\w\w) kind \d+ length (\d+)(.*)', line)
        if entry:
            decoders = re.findall(r'decoder (0x[0-9a-f]+) (0x[0-9a-f]+)', entry[4])
            entries.append(
                Entry(
                    int(entry[1], 16),
                    entry[2],
                    int(entry[3]),
                    tuple((int(decode, 16), int(record, 16)) for decode, record in decoders),
                )
            )
        elif line.startswith(('script ', 'list ')):
            for sender, mode in re.findall(r' (\w\w)/(\d)', line):
                sent.setdefault(sender, set()).add(int(mode))
    return Requests(tuple(entries), {sender: frozenset(modes) for sender, modes in sent.items()})


class Machine:
    """One image's ROM in Unicorn, noting the reads of a message."""

    def __init__(self, image: str, rom: bytes) -> None:
        self.image = image
        self.vectors = IMAGES[image]
        self.fault: str | None = None
        self.raised: int | None = None
        self.reads: set[int] | None = None
        self.watched: set[tuple[int, int]] | None = None
        self.uc = Uc(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN)
        self.uc.ctl_set_cpu_model(UC_CPU_M68K_M68020)
        self.uc.mem_map(0, ROM_SIZE)
        self.uc.mem_write(0, rom[:ROM_SIZE])
        self.uc.mem_map(RAM, RAM_SIZE)
        self.uc.hook_add(UC_HOOK_MEM_INVALID, self.invalid)
        self.uc.hook_add(UC_HOOK_INTR, self.exception)
        self.uc.hook_add(UC_HOOK_MEM_READ, self.read, begin=MESSAGE, end=MESSAGE + MESSAGE_SPACE - 1)
        self.uc.hook_add(UC_HOOK_MEM_READ, self.watch, begin=RAM, end=MESSAGE - 1)

    def invalid(self, uc: Uc, access: int, address: int, size: int, value: int, data: object) -> bool:
        self.fault = f'access of {size} bytes at {address:#x} from {uc.reg_read(UC_M68K_REG_PC):#x}'
        return False

    def exception(self, uc: Uc, number: int, data: object) -> None:
        if number in (TRAP_7, RETURN_FROM_EXCEPTION):
            self.raised = number
        else:
            self.fault = f'exception {number} at {uc.reg_read(UC_M68K_REG_PC):#x}'
        uc.emu_stop()

    def read(self, uc: Uc, access: int, address: int, size: int, value: int, data: object) -> None:
        if self.reads is not None:
            self.reads.update(range(address - MESSAGE, address - MESSAGE + size))

    def watch(self, uc: Uc, access: int, address: int, size: int, value: int, data: object) -> None:
        if self.watched is not None:
            pc = uc.reg_read(UC_M68K_REG_PC)
            self.watched.update((pc, byte) for byte in range(address, address + size))

    def ram(self) -> bytes:
        """RAM below the message."""
        return bytes(self.uc.mem_read(RAM, MESSAGE - RAM))

    def take_trap(self) -> None:
        """Enters the image's trap #7 handler as the processor does."""
        sr = self.uc.reg_read(UC_M68K_REG_SR)
        frame = struct.pack(FRAME, sr, self.uc.reg_read(UC_M68K_REG_PC) + TRAP_SIZE, TRAP_7 * VECTOR_SIZE)
        sp = self.uc.reg_read(UC_M68K_REG_A7) - len(frame)
        self.uc.mem_write(sp, frame)
        self.uc.reg_write(UC_M68K_REG_A7, sp)
        handler = self.vectors + TRAP_7 * VECTOR_SIZE
        self.uc.reg_write(UC_M68K_REG_PC, struct.unpack('>I', self.uc.mem_read(handler, VECTOR_SIZE))[0])

    def leave_trap(self) -> None:
        """Returns from the handler's format 0 frame as rte does."""
        sp = self.uc.reg_read(UC_M68K_REG_A7)
        sr, pc, _ = struct.unpack(FRAME, self.uc.mem_read(sp, struct.calcsize(FRAME)))
        self.uc.reg_write(UC_M68K_REG_SR, sr)
        self.uc.reg_write(UC_M68K_REG_A7, sp + struct.calcsize(FRAME))
        self.uc.reg_write(UC_M68K_REG_PC, pc)

    def call(self, routine: int, *arguments: int) -> int:
        """Calls a routine with long arguments on the stack; its D0."""
        return self.call_with(routine, struct.pack(f'>{len(arguments)}I', *arguments))

    def call_with(self, routine: int, arguments: bytes) -> int:
        """Calls a routine with its arguments as pushed; its D0."""
        frame = struct.pack('>I', RETURN) + arguments
        sp = STACK - len(frame)
        self.uc.mem_write(sp, frame)
        # SR first: setting S swaps in the supervisor's stack pointer.
        self.uc.reg_write(UC_M68K_REG_SR, SUPERVISOR)
        self.uc.reg_write(UC_M68K_REG_A7, sp)
        self.uc.reg_write(UC_M68K_REG_D0, 0)
        self.fault = None
        at = routine
        while at != RETURN:
            self.raised = None
            try:
                self.uc.emu_start(at, RETURN, count=CALL_LIMIT)
            except UcError as error:
                sys.exit(f'{self.image}: {routine:#x}: {self.fault or error}')
            if self.fault is not None:
                sys.exit(f'{self.image}: {routine:#x}: {self.fault}')
            if self.raised == TRAP_7:
                self.take_trap()
            elif self.raised == RETURN_FROM_EXCEPTION:
                self.leave_trap()
            elif self.uc.reg_read(UC_M68K_REG_PC) != RETURN:
                sys.exit(f'{self.image}: {routine:#x} did not return, at {self.uc.reg_read(UC_M68K_REG_PC):#x}')
            at = self.uc.reg_read(UC_M68K_REG_PC)
        return self.uc.reg_read(UC_M68K_REG_D0)

    def decode(self, entry: Entry, data: bytes) -> set[int]:
        """The offsets of a message of entry's with this data that its
        decoders read."""
        self.uc.mem_write(MESSAGE, b'@@' + entry.id.encode() + data + b'\x00\r\n')
        self.reads = set()
        cursor = MESSAGE + HEADER
        for decoder, record in entry.decoders:
            cursor = self.call(decoder, cursor, record)
        reads, self.reads = self.reads, None
        return reads


def filled(entry: Entry, fill: int) -> bytes:
    """The data of a message of entry's, every byte fill."""
    return bytes([fill]) * (entry.length - HEADER - TRAILER)


def machines(image: str, table: Requests) -> dict[int, Machine]:
    """A machine for each channel count the image's code arrives at,
    by the count."""
    rom = (FIRMWARE / 'images' / f'{image}.bin').read_bytes()
    update = names(image).get('gps_channel_count_update')
    fix_mode = next(entry.decoders[0][1] for entry in table.entries if entry.id == FIX_MODE and entry.decoders)
    counts = {6: False}
    if any(entry.id == STATUS_EIGHT and entry.decoders for entry in table.entries):
        counts[8] = True
    found = {}
    for count, eight in counts.items():
        machine = Machine(image, rom)
        machine.uc.mem_write(fix_mode, bytes([POSITION_FIX]))
        if update is not None:
            machine.call(update)
        if eight:
            for entry in table.entries:
                if entry.id == STATUS_EIGHT and entry.decoders:
                    machine.decode(entry, filled(entry, 0))
        found[count] = machine
    return found


def decoded(table: Requests, by_count: dict[int, Machine]) -> list[tuple[Entry, list[Machine]]]:
    """Each entry of an image's table that has a decoder and a message
    to decode, with the machines to decode it on: a six-channel
    message's the one of six channels, an eight-channel one's the one of
    eight, any other's each."""
    found = []
    for entry in table.entries:
        if not entry.decoders or entry.length < HEADER + TRAILER:
            continue
        if entry.id in SIX_CHANNEL:
            counts = [6]
        elif entry.id in EIGHT_CHANNEL:
            counts = [8]
        else:
            counts = list(by_count)
        found.append((entry, [by_count[count] for count in counts]))
    return found


def read_sets(table: Requests, by_count: dict[int, Machine]) -> dict[str, set[int]]:
    """The offsets an image's decoders read of each message that has
    any."""
    found: dict[str, set[int]] = {}
    for entry, on in decoded(table, by_count):
        reads = found.setdefault(entry.id, set())
        for machine in on:
            kept = machine.ram()
            for fill in FILLS:
                reads |= machine.decode(entry, filled(entry, fill))
            machine.uc.mem_write(RAM, kept)
    return found


def changed(before: bytes, after: bytes) -> set[int]:
    """The addresses at which two copies of RAM differ."""
    step = 0x1000
    return {
        RAM + at + index
        for at in range(0, len(before), step)
        if before[at : at + step] != after[at : at + step]
        for index in range(min(step, len(before) - at))
        if before[at + index] != after[at + index]
    }


def stores(
    rows: list[Row], table: Requests, by_count: dict[int, Machine]
) -> tuple[dict[int, set[int]], dict[str, set[tuple[int, int]]]]:
    """Where an image's decoders leave each field, by the row's line:
    the bytes of RAM that differ after a message is decoded with the
    field's bytes flipped, by each of FLIPS, from each of FILLS.  And
    the reads of RAM its decoders make of each message, as (PC,
    address), for the check of the static readers."""
    found: dict[int, set[int]] = {}
    seen: dict[str, set[tuple[int, int]]] = {}
    for entry, on in decoded(table, by_count):
        fields = [row for row in rows if row.message == entry.id and row.direction == 'response']
        for machine in on:
            kept = machine.ram()
            for fill in FILLS:
                data = filled(entry, fill)
                machine.watched = set()
                machine.decode(entry, data)
                seen.setdefault(entry.id, set()).update(machine.watched)
                machine.watched = None
                base = machine.ram()
                for row in fields:
                    for flip in sorted({flip & row.mask() for flip in FLIPS} - {0}):
                        flipped = bytearray(data)
                        for span in row.instances():
                            for byte in span:
                                if byte - HEADER < len(flipped):
                                    flipped[byte - HEADER] = fill ^ flip
                        machine.uc.mem_write(RAM, kept)
                        machine.decode(entry, bytes(flipped))
                        found.setdefault(row.line, set()).update(changed(base, machine.ram()))
                machine.uc.mem_write(RAM, kept)
    return found, seen


def compared(rows: list[Row], table: Requests, machine: Machine) -> dict[str, set[int]]:
    """The offsets of each response that an image's gps_response_match
    reads when asked whether it answers the command that sets it: a
    command of the table's fields, sent to set (mode 0), with the
    response and the command filled alike."""
    match = names(machine.image)[MATCH]
    found = {}
    for entry in table.entries:
        sent = [row for row in rows if row.message == entry.id and row.direction == 'command']
        if not sent or entry.length < HEADER + TRAILER:
            continue
        length = HEADER + sum(len(span) for row in sent for span in row.instances()) + TRAILER
        reads: set[int] = set()
        for fill in FILLS:
            machine.uc.mem_write(SENT, b'@@' + entry.id.encode() + bytes([fill]) * (length - HEADER - TRAILER) + b'\x00\r\n')
            machine.uc.mem_write(MESSAGE, b'@@' + entry.id.encode() + filled(entry, fill) + b'\x00\r\n')
            machine.reads = set()
            machine.call_with(
                match, struct.pack(MATCH_ARGUMENTS, entry.index, SENT, length, 0, entry.index, MESSAGE, entry.length)
            )
            reads |= {offset for offset in machine.reads if offset < entry.length}
            machine.reads = None
        found[entry.id] = reads
    return found


class Follow(record_access.Scan):
    """record_access's scan with two things followed that it leaves: an
    address pushed for a call, into the function called, and a
    postincrement under a dbf that loops on the one instruction.  And
    with a note of each place a followed address goes where the scan
    does not see what is read through it, a walk: (function,
    instruction, address, which of WALKS)."""

    def __init__(self, functions: dict[int, str], start: int, size: int, arguments: dict) -> None:
        super().__init__(functions, start, size, arguments)
        self.function: int | None = None
        self.previous: tuple[int, str, list[str]] | None = None
        self.pending: list[tuple[int, int]] = []  # (depth after the push, address) since the last call
        self.calls: list[tuple[int, int, int, int]] = []  # (function, callee, its stack offset, address)
        self.walks: list[tuple[int, int, int, str]] = []
        self.named: list[tuple[int, int, int]] = []  # (function, instruction, address an operand names)
        self.at = 0  # the instruction stepped

    def pushed(self, mnemonic: str, operands: list[str]) -> int | None:
        """The address an instruction pushes, where it is followed."""
        if mnemonic == 'pea':
            absolute = record_access.ABSOLUTE.fullmatch(operands[0])
            relative = re.fullmatch(r'%(a\d)@(?:\((-?\d+)\))?', operands[0])
            if absolute:
                return int(absolute[1], 16)
            if relative and self.registers.get(relative[1]) is not None:
                return self.registers[relative[1]] + int(relative[2] or 0)
        elif mnemonic == 'movel' and len(operands) == 2 and operands[1] == '%sp@-':
            return self.value(operands[0])
        return None

    def callee(self, operand: str) -> int | None:
        """The address a call's operand names: absolute, PC-relative,
        or in an address register the scan follows."""
        target = record_access.ABSOLUTE.fullmatch(operand) or re.fullmatch(r'%pc@\((0x[0-9a-f]+)\)', operand)
        if target:
            return int(target[1], 16)
        register = re.fullmatch(r'%(a\d)@', operand)
        return self.registers.get(register[1]) if register else None

    def step(self, function: int, address: int, mnemonic: str, operands: list[str]) -> None:
        if function != self.function:
            self.function, self.pending, self.previous = function, [], None
        self.at = address
        for operand in operands:
            constant = record_access.ABSOLUTE.fullmatch(operand) or re.fullmatch(r'#(\d+)', operand)
            if constant and self.inside(int(constant[1], 0)):
                self.named.append((function, address, int(constant[1], 0)))
        pushed = self.pushed(mnemonic, operands)
        callee = self.callee(operands[0]) if mnemonic.startswith(('jsr', 'bsr')) else None
        super().step(function, address, mnemonic, operands)
        if self.inside(pushed):
            self.pending.append((self.depth, pushed))
        if mnemonic.startswith(('jsr', 'bsr')):
            for depth, value in self.pending:
                if callee in self.functions:
                    self.calls.append((function, callee, VECTOR_SIZE + self.depth - depth, value))
                else:
                    self.walks.append((function, address, value, 'passed'))
            self.pending = []
        elif mnemonic in ('dbf', 'dbra') and self.previous and int(operands[1], 16) == self.previous[0]:
            self.repeat(function, operands[0][1:])
        self.previous = (address, mnemonic, operands)

    def repeat(self, function: int, counter: str) -> None:
        """The previous instruction's accesses again, for each further
        turn of the dbf that follows it."""
        address, mnemonic, operands = self.previous
        walked = [
            self.registers[moved[1]]
            for operand in operands
            if (moved := re.fullmatch(r'%(a\d)@\+', operand)) and self.inside(self.registers.get(moved[1]), record_access.FOLLOW_PAST)
        ]
        if not walked:
            return
        count = self.registers.get(counter)
        if count is None:
            self.walks += [(function, address, value, 'looped') for value in walked]
            return
        for _ in range(count & 0xFFFF):
            for index, operand in enumerate(operands):
                self.note(function, address, mnemonic, record_access.operand_size(mnemonic), operand, index == len(operands) - 1 and len(operands) > 1)

    def note(self, function: int, address: int, mnemonic: str, size: int | str, operand: str, written: bool) -> None:
        indexed = record_access.INDEXED.fullmatch(operand)
        stepped = record_access.REGISTER_INDIRECT.fullmatch(operand)
        through = indexed or (stepped if stepped and stepped[3] else None)
        base = self.registers.get(through[1]) if through else None
        noted = len(self.accesses)
        super().note(function, address, mnemonic, size, operand, written)
        if self.inside(base) and not stores_only(mnemonic, written):
            reached = self.start + self.accesses[-1][0] if len(self.accesses) > noted else base
            self.walks.append((function, address, reached, 'indexed' if indexed else 'stepped'))

    def write_register(self, mnemonic: str, operands: list[str]) -> None:
        register = re.fullmatch(r'%([ad]\d)', operands[1])
        before = self.registers.get(register[1]) if register else None
        source = self.value(operands[0]) if re.fullmatch(r'%[ad]\d|#-?\d+', operands[0]) else None
        super().write_register(mnemonic, operands)
        for value in (before, source):
            if self.inside(value) and not mnemonic.startswith(('move', 'lea', 'cmp', 'tst', 'btst')):
                self.walks.append((self.function, self.at, value, 'computed'))
        counter = re.fullmatch(r'%(d\d)', operands[1])
        immediate = re.fullmatch(r'#(\d+)', operands[0])
        if counter and immediate and mnemonic in ('moveq', 'movew'):
            self.registers[counter[1]] = int(immediate[1])
            self.stable.add(counter[1])


def stores_only(mnemonic: str, written: bool) -> bool:
    """Whether an access writes its operand without reading it: a move
    to it, a clr or an Scc.  record_access marks as written the last of
    two operands, which a cmp only reads and an add reads too."""
    if written:
        return mnemonic.startswith('move')
    return mnemonic.startswith('clr') or (mnemonic.startswith('s') and not mnemonic.startswith(('sub', 'swap')))


# How a followed address leaves the scan's sight: with an index
# register, by a postincrement or predecrement, by arithmetic on the
# register, under a dbf of unknown count, passed to code the scan does
# not follow or sees do nothing with it, named by an instruction of a
# function in which the scan notes nothing at it or after, or held by
# a ROM table the function names.
NAMED = 'named'
TABLED = 'tabled'
# The lowest ROM table the scan takes an instruction's number to name:
# a lower address is as likely a count, a mask or a size.
TABLE_FLOOR = 0x10000
WALKS = ('indexed', 'stepped', 'computed', 'looped', 'passed', NAMED, TABLED)


@dataclass
class Readers:
    """The functions of an image that read each byte of a span of RAM,
    by the byte's address, the functions the reading instructions are
    in, which differ where a read is through an address passed, each
    function's walks: (function, address, which of WALKS), and the
    walks by the function the walking instruction is in."""

    by_address: dict[int, set[str]]
    sites: dict[int, set[str]]
    walk_sites: set[tuple[str, int]]
    walks: set[tuple[str, int, str]]


def readers(image: str, first: int, last: int, seeds: list[tuple[int, int, int]]) -> Readers:
    """The functions of an image's primary that read RAM in [first,
    last], by a scan of its instructions.  A read through an address a
    function passes to another, however deep, is the passing function's;
    seeds are (function, stack offset, address) for arguments the scan
    cannot see passed, a decoder's record; so is a walk."""
    functions = record_access.primary_functions(image)
    instructions = record_access.disassemble(image)
    at = {address: index for index, (address, _, _) in enumerate(instructions)}
    starts = sorted(address for address in functions if address in at)

    def body(function: int) -> list[tuple[int, str, str]]:
        after = bisect.bisect_right(starts, function)
        return instructions[at[function] : at[starts[after]] if after < len(starts) else None]

    def scan_of(code: list[tuple[int, str, str]], arguments: dict) -> Follow:
        scan = Follow(functions, first, last + 1 - first, arguments)
        scan.run(code, image)
        return scan

    whole = scan_of(instructions, {})
    plain = {name: set(getattr(whole, name)) for name in ('accesses', 'calls', 'walks')}
    found: dict[str, set] = {
        'accesses': {(access[2], access) for access in plain['accesses']},
        'walks': {(walk[0], walk) for walk in plain['walks']},
    }
    noted = {(access[2], access[3]) for access in whole.accesses}
    noted |= {(function, address) for function, address, _, _ in whole.walks}
    noted |= {(function, address) for function, address, _ in whole.passes}
    for function, address, value in whole.named:
        if not any(noter == function and at >= address for noter, at in noted):
            found['walks'].add((function, (function, address, value, NAMED)))
    work = [(root, callee, offset, value, 1) for root, callee, offset, value in plain['calls']]
    work += [(function, function, offset, value, 1) for function, offset, value in seeds]
    done = set()
    while work:
        root, callee, offset, value, depth = work.pop()
        if (root, callee, offset, value) in done or callee not in at:
            continue
        done.add((root, callee, offset, value))
        if depth > FOLLOW_DEPTH:
            found['walks'].add((root, (callee, callee, value, 'passed')))
            continue
        scan = scan_of(body(callee), {callee: {offset: value}})
        for name in found:
            found[name] |= {(root, item) for item in set(getattr(scan, name)) - plain[name]}
        calls = set(scan.calls) - plain['calls']
        work += [(root, *call[1:], depth + 1) for call in calls]
        if not calls and not set(scan.accesses) - plain['accesses'] and root != callee:
            found['walks'].add((root, (callee, callee, value, 'passed')))
    by_address: dict[int, set[str]] = {}
    sites: dict[int, set[str]] = {}
    for root, (offset, size, function, _, _, written, mnemonic) in found['accesses']:
        if size == 0 or stores_only(mnemonic, written):
            continue
        for address in range(first + offset, first + offset + (size if isinstance(size, int) else 1)):
            by_address.setdefault(address, set()).add(functions[root])
            sites.setdefault(address, set()).add(functions[function])
    return Readers(
        by_address,
        sites,
        {(functions[walk[0]], walk[2]) for _, walk in found['walks']},
        {(functions[root], value, how) for root, (_, _, value, how) in found['walks']},
    )


def tabled(image: str, wanted: set[int]) -> set[tuple[str, int, str]]:
    """The walks of an image's ROM tables: (function, address, TABLED)
    for each wanted address that a long of a ROM label of names.tsv
    holds, and each function with an instruction naming the label,
    which runs to the next named address.  Only a label from
    TABLE_FLOOR up counts."""
    rom = (FIRMWARE / 'images' / f'{image}.bin').read_bytes()
    functions = record_access.primary_functions(image)
    starts = sorted(functions)
    marks, labels = set(functions), []
    for line in (FIRMWARE / 'analysis' / image / 'names.tsv').read_text().splitlines():
        cells = line.split('\t')
        if line.startswith('#') or len(cells) < 3 or '::' in cells[0]:
            continue
        marks.add(int(cells[0], 16))
        if cells[1] == 'label' and TABLE_FLOOR <= int(cells[0], 16) < min(RAM, len(rom)):
            labels.append(int(cells[0], 16))
    marks = sorted(marks)
    held = {}
    for label in labels:
        after = bisect.bisect_right(marks, label)
        end = min(marks[after] if after < len(marks) else len(rom), len(rom))
        longs = {
            struct.unpack_from('>I', rom, at)[0] for at in range(label, end - VECTOR_SIZE + 1, 2)
        } & wanted
        if longs:
            held[label] = (end, longs)
    found = set()
    for address, _, operands in record_access.disassemble(image):
        before = bisect.bisect_right(starts, address)
        for number in re.findall(r'#\d+|0x[0-9a-f]+', operands) if before else ():
            value = int(number.lstrip('#'), 0)
            _, longs = held.get(value, (0, set()))
            found |= {(functions[starts[before - 1]], long, TABLED) for long in longs}
    return found


def calls_of(image: str) -> dict[str, set[str]]:
    """The functions each of an image's functions calls, by name, from
    its conventions."""
    path = FIRMWARE / 'decompiled' / image / f'{image}.conventions.jsonl'
    entries = [json.loads(line) for line in path.read_text().splitlines()]
    return {entry['name']: set(entry.get('calls', ())) for entry in entries}


@dataclass
class Traced:
    """What one image does with the messages' fields: the offsets its
    decoders read of each message and its gps_response_match of each
    response, and by the row's line where its field is stored, which
    functions the scan sees read it there, which it sees walk from an
    address at or before it in its record without seeing the read, and
    which the run of the decoders has read it that the scan does not."""

    reads: dict[str, set[int]]
    compared: dict[str, set[int]]
    stores: dict[int, set[int]]
    readers: dict[int, set[str]]
    reach: dict[int, set[str]]
    ran: dict[int, set[str]]
    walks: set[tuple[str, int, str]]


def trace(image: str, rows: list[Row], table: Requests) -> Traced:
    """An image's Traced.  A function the decoders, run, read a field's
    store from is one the scan missed if the scan has no read of the
    field in it, no walk from before the read by it, and no reader of
    the field that calls it."""
    by_count = machines(image, table)
    reads = read_sets(table, by_count)
    stored, seen = stores(rows, table, by_count)
    every = set().union(*stored.values())
    first = min(min(every), globals_of(image)['gps'])
    seeds = [
        (decoder, RECORD_ARGUMENT, record)
        for entry in table.entries
        for decoder, record in entry.decoders
        if first <= record <= max(every)
    ]
    scan = readers(image, first, max(every), seeds)
    found = {
        line: set().union(*(scan.by_address.get(address, set()) for address in addresses))
        for line, addresses in stored.items()
    }
    functions = record_access.primary_functions(image)
    starts = sorted(functions)
    decoding = {functions[decoder] for entry in table.entries for decoder, _ in entry.decoders}
    records = sorted({record for entry in table.entries for _, record in entry.decoders})
    places = sorted(every)
    reach: dict[int, set[str]] = {line: set() for line in stored}
    scan.walks |= tabled(image, every)
    for function, address, how in scan.walks:
        after = bisect.bisect_left(places, address)
        if function in decoding or after == len(places) or places[after] - address > WALK_BEFORE:
            continue
        if how == NAMED and address not in every:
            continue
        record = bisect.bisect_right(records, places[after])
        end = records[record] if record < len(records) else places[-1] + 1
        for line, addresses in stored.items():
            if function not in found[line] and any(address <= place < end for place in addresses):
                reach[line].add(function)
    calls = calls_of(image)
    ran: dict[int, set[str]] = {line: set() for line in stored}
    for row in rows:
        entry = next((entry for entry in table.entries if entry.id == row.message), None)
        if row.direction != 'response' or row.line not in stored or entry is None:
            continue
        sites = set().union(*(scan.sites.get(address, set()) for address in stored[row.line]))
        for pc, address in sorted(seen.get(row.message, ())):
            function = functions[starts[bisect.bisect_right(starts, pc) - 1]]
            walked = any(name == function and start <= address for name, start in scan.walk_sites)
            called = any(function in calls.get(reader, ()) for reader in found[row.line] | reach[row.line])
            if address in stored[row.line] and function not in sites and not walked and not called:
                ran[row.line].add(function)
    return Traced(reads, compared(rows, table, by_count[min(by_count)]), stored, found, reach, ran, scan.walks)


def rows_of(text: str) -> list[Row]:
    lines = text.splitlines()
    if lines[0].split('\t') != list(COLUMNS):
        sys.exit(f'{TABLE.relative_to(ROOT)}: the columns are not {" ".join(COLUMNS)}')
    rows = []
    for number, line in enumerate(lines[1:], start=2):
        cells = line.split('\t')
        if len(cells) != len(COLUMNS):
            sys.exit(f'{TABLE.relative_to(ROOT)}:{number}: {len(cells)} cells')
        cell = dict(zip(COLUMNS, cells))
        numbers = {name: int(cell[name]) for name in ('offset', 'length', 'count')}
        stride = numbers['length'] if cell['stride'] == NONE else int(cell['stride'])
        rows.append(Row(**{**cell, **numbers, 'stride': stride, 'line': number}))
    return rows


def text_of(rows: list[Row]) -> str:
    return ''.join('\t'.join(cells) + '\n' for cells in [list(COLUMNS)] + [row.cells() for row in rows])


def spans(offsets: set[int]) -> str:
    """Offsets as inclusive runs."""
    runs: list[list[int]] = []
    for offset in sorted(offsets):
        if runs and runs[-1][1] == offset - 1:
            runs[-1][1] = offset
        else:
            runs.append([offset, offset])
    return ' '.join(f'{first}-{last}' if first != last else str(first) for first, last in runs) or NONE


def check_rows(rows: list[Row], tables: dict[str, Requests]) -> list[str]:
    """What is wrong with the table by itself and against the images'
    lengths."""
    problems = []
    record_fields = set(re.findall(r'^\s+pub (\w+):', RECORD.read_text(), re.MULTILINE))
    messages = {row.message for row in rows}
    groups: dict[tuple[str, str], list[Row]] = {}
    for row in rows:
        where = f'{TABLE.relative_to(ROOT)}:{row.line}'
        groups.setdefault((row.message, row.direction), []).append(row)
        if row.direction not in DIRECTIONS:
            problems.append(f'{where}: direction {row.direction}')
        manuals = row.manual.split(',')
        if row.manual != NONE and (
            any(manual not in MANUALS for manual in manuals) or len(manuals) != len(row.page.split(','))
        ):
            problems.append(f'{where}: manual {row.manual} page {row.page}')
        for term, _ in row.terms():
            kind, _, path = term.partition('.')
            if row.direction == 'command':
                good = term in HANDLINGS
            elif kind == 'record':
                good = bool(path) and all(part in record_fields for part in path.split('.'))
            elif kind == 'setting':
                good = path in messages
            else:
                good = term in SOURCES
            if not good:
                problems.append(f'{where}: engine term {term}')
    lengths: dict[str, set[int]] = {}
    for table in tables.values():
        for entry in table.entries:
            lengths.setdefault(entry.id, set()).add(entry.length)
    for (message, direction), group in groups.items():
        at = HEADER
        split: dict[tuple[int, int, int, int], list[Row]] = {}
        for row in sorted(group, key=lambda row: row.offset):
            where = f'{TABLE.relative_to(ROOT)}:{row.line}'
            if row.count > 1 and row.stride < row.length:
                problems.append(f'{where}: stride {row.stride} under length')
            if row.bits != NONE:
                if not BITS.fullmatch(row.bits) or not row.mask() or row.length != 1:
                    sys.exit(f'{where}: bits {row.bits}')
                split.setdefault((row.offset, row.length, row.count, row.stride), []).append(row)
        for bytes_split in split.values():
            masks = [row.mask() for row in bytes_split]
            if sum(masks) != (1 << BYTE_BITS) - 1 or any(a & b for a in masks for b in masks if a is not b):
                problems.append(
                    f'{TABLE.relative_to(ROOT)}:{bytes_split[0].line}: @@{message} {direction} byte '
                    f'{bytes_split[0].offset}: its rows do not give each bit once'
                )
        whole = [row for row in group if row.bits == NONE] + [bytes_split[0] for bytes_split in split.values()]
        covered = sorted((span.start, span.stop, row.line) for row in whole for span in row.instances())
        for start, stop, line in covered:
            if start != at:
                problems.append(f'{TABLE.relative_to(ROOT)}:{line}: @@{message} {direction} byte {at} then {start}')
            at = stop
        framed = lengths.get(message, set())
        if direction == 'response' and message not in FRAMED_SHORT and framed and framed != {at + TRAILER}:
            problems.append(f'@@{message}: rows end at {at}, the images frame it at {sorted(framed)}')
    for message, framed in lengths.items():
        if framed - {0, HEADER + TRAILER} and (message, 'response') not in groups:
            problems.append(f'@@{message}: no response rows, the images frame it at {sorted(framed)}')
    return problems


def images_of(rows: list[Row], tables: dict[str, Requests], reads: dict[str, dict[str, set[int]]]) -> list[str]:
    """Sets each row's images from the images; what stops that."""
    problems = []
    for image in IMAGES:
        for message, offsets in reads[image].items():
            data = {
                byte
                for row in rows
                if row.message == message and row.direction == 'response'
                for span in row.instances()
                for byte in span
            }
            if offsets - data:
                problems.append(f'{image}: @@{message}: reads {spans(offsets - data)}, outside its rows')
    for row in rows:
        found = []
        for image in IMAGES:
            if row.direction == 'command':
                if any(mode in SET_MODES for mode in tables[image].sent.get(row.message, ())):
                    found.append(image)
                continue
            offsets = reads[image].get(row.message, set())
            read = [sum(byte in offsets for byte in span) for span in row.instances()]
            if any(count not in (0, row.length) for count in read) or len(set(read)) != 1:
                problems.append(
                    f'{TABLE.relative_to(ROOT)}:{row.line}: {image} reads part of @@{row.message} {row.field}: '
                    f'{spans(offsets & {byte for span in row.instances() for byte in span})}'
                )
            elif read[0]:
                found.append(image)
        row.images = ','.join(found) or NONE
    return problems


@dataclass(frozen=True)
class Use:
    """One row of the hand table: what a function, or any, does with
    the fields it reads, in the images, messages and fields the row
    names, an empty set for any, and where that is seen."""

    line: int
    function: str
    images: frozenset[str]
    messages: frozenset[str]
    fields: frozenset[str]
    use: str
    evidence: str

    def matches(self, function: str, image: str, row: Row) -> bool:
        return (
            self.function in (ANY, function)
            and (not self.images or image in self.images)
            and (not self.messages or row.message in self.messages)
            and (not self.fields or row.field in self.fields)
        )

    def weight(self) -> tuple[bool, bool, bool, bool]:
        """How specific the row is: the most specific that matches a
        read is the read's."""
        return (bool(self.fields), self.function != ANY, bool(self.messages), bool(self.images))


def uses_of(text: str) -> tuple[list[Use], list[str]]:
    """The hand table's rows, and what is wrong with them."""
    lines = text.splitlines()
    if lines[0].split('\t') != list(USE_COLUMNS):
        sys.exit(f'{USES.relative_to(ROOT)}: the columns are not {" ".join(USE_COLUMNS)}')
    found, problems = [], []
    for number, line in enumerate(lines[1:], start=2):
        where = f'{USES.relative_to(ROOT)}:{number}'
        cells = line.split('\t')
        if len(cells) != len(USE_COLUMNS):
            sys.exit(f'{where}: {len(cells)} cells')
        cell = dict(zip(USE_COLUMNS, cells))
        listed = {
            name: frozenset() if cell[name] == ANY else frozenset(cell[name].split(separator))
            for name, separator in (('images', ','), ('message', ','), ('field', '; '))
        }
        if cell['use'] not in CLASSES:
            problems.append(f'{where}: use {cell["use"]}')
        if listed['images'] - set(IMAGES):
            problems.append(f'{where}: images {cell["images"]}')
        if not CITED.search(cell['evidence']):
            problems.append(f'{where}: the evidence cites no function()')
        found.append(
            Use(number, cell['function'], listed['images'], listed['message'], listed['field'], cell['use'], cell['evidence'])
        )
    return found, problems


@dataclass
class Found:
    """What an image does with one field its decoders read: the use,
    and the functions whose use it is, each with how the trace has it
    (which of KINDS)."""

    use: str
    by: dict[str, str]


def classify(
    rows: list[Row], traces: dict[str, Traced], hand: list[Use]
) -> tuple[dict[int, dict[str, Found]], list[str]]:
    """Each response row's Found in each image that reads it, by the
    row's line and the image: the strongest use the hand table gives a
    function the trace has reading it.  And what stops that: a reading
    function with no row or two as specific, a row that is no read's,
    or names an image, message or field where it is none's, or cites a
    function an image it speaks for lacks."""
    problems = []
    found: dict[int, dict[str, Found]] = {}
    won: dict[int, list[tuple[str, Row]]] = {use.line: [] for use in hand}
    missing: dict[tuple[str, str, str, str], list[str]] = {}
    for image, traced in traces.items():
        for row in rows:
            if row.direction != 'response' or image not in row.images.split(','):
                continue
            kinds = {
                function: kind
                for kind, readers in zip(KINDS, (traced.ran, traced.reach, traced.readers))
                for function in readers.get(row.line, ())
            }
            classes: dict[str, str] = {}
            for function, kind in kinds.items():
                matching = [use for use in hand if use.matches(function, image, row)]
                heaviest = max((use.weight() for use in matching), default=None)
                best = [use for use in matching if use.weight() == heaviest]
                if not best:
                    missing.setdefault((image, function, kind, row.message), []).append(row.field)
                elif len(best) > 1:
                    problems.append(
                        f'{USES.relative_to(ROOT)}:{best[0].line} and {best[1].line} both speak for '
                        f'{function} of @@{row.message} {row.field} in {image}'
                    )
                else:
                    classes[function] = best[0].use
                    won[best[0].line].append((image, row))
            use = max(classes.values(), key=CLASSES.index, default=CLASSES[0])
            by = {function: kinds[function] for function in sorted(classes) if classes[function] == use}
            found.setdefault(row.line, {})[image] = Found(STORED, {}) if use == CLASSES[0] else Found(use, by)
    for (image, function, kind, message), fields in missing.items():
        problems.append(
            f'{image}: {function} reads @@{message} {"; ".join(fields)} ({kind}): '
            f'no row of {USES.relative_to(ROOT)}'
        )
    known = {image: calls_of(image) for image in traces}
    for use in hand:
        where = f'{USES.relative_to(ROOT)}:{use.line}'
        if not won[use.line]:
            problems.append(f'{where}: no read is this row\'s')
            continue
        for listed, has in (
            (use.images, {image for image, _ in won[use.line]}),
            (use.messages, {row.message for _, row in won[use.line]}),
            (use.fields, {row.field for _, row in won[use.line]}),
        ):
            if listed - has:
                problems.append(f'{where}: no read is this row\'s in {", ".join(sorted(listed - has))}')
        for image in sorted({image for image, _ in won[use.line]}):
            for name in CITED.findall(use.evidence):
                if name not in known[image]:
                    problems.append(f'{where}: {image} has no {name}')
    return found, problems


def cells_of(rows: list[Row], traces: dict[str, Traced], found: dict[int, dict[str, Found]]) -> list[str]:
    """Sets each response row's compared and use; what stops that."""
    problems = []
    for row in rows:
        row.compared = row.use = NONE
        if row.direction != 'response':
            continue
        matched = []
        for image, traced in traces.items():
            offsets = traced.compared.get(row.message, set())
            read = sum(byte in offsets for span in row.instances() for byte in span)
            if read == row.count * row.length:
                matched.append(image)
            elif read:
                problems.append(
                    f'{TABLE.relative_to(ROOT)}:{row.line}: {image} compares part of @@{row.message} {row.field}'
                )
        row.compared = ','.join(matched) or NONE
        uses = {image: each.use for image, each in found.get(row.line, {}).items()}
        if len(set(uses.values())) == 1:
            row.use = next(iter(uses.values()))
        elif uses:
            row.use = ','.join(f'{image}={use}' for image, use in uses.items())
    return problems


def gap_terms(row: Row) -> str:
    """What makes a row a gap, or nothing."""
    if row.images == NONE:
        return ''
    if row.direction == 'command':
        wanted = [(term, note) for term, note in row.terms() if term != HONORED]
    else:
        wanted = [(term, note) for term, note in row.terms() if term in GAP_SOURCES]
    return '; '.join(f'{term}: {note}' if note else term for term, note in wanted)


def short(images: list[str]) -> str:
    """Images as the document lists them."""
    return 'all four' if images == list(IMAGES) else ', '.join(images)


def bytes_of(row: Row) -> str:
    last = row.offset + (row.count - 1) * row.stride + row.length - 1
    each = f', {row.count} x {row.length}' if row.count > 1 else ''
    bits = '' if row.bits == NONE else f', bit{"s" if "-" in row.bits else ""} {row.bits}'
    return (f'{row.offset}-{last}{each}' if last != row.offset else str(row.offset)) + bits


def where_used(row: Row, found: dict[str, Found]) -> str:
    """A row's uses and the images of each, strongest first, and the
    images that compare it."""
    parts = [
        f'{use}: {short(images)}'
        for use in (*reversed(CLASSES[1:]), STORED)
        if (images := [image for image, each in found.items() if each.use == use])
    ]
    if row.compared != NONE:
        parts.append(f'compared: {short(row.compared.split(","))}')
    return '; '.join(parts)


def gaps(rows: list[Row], found: dict[int, dict[str, Found]]) -> str:
    """The gaps as the document's tables: the response fields an image
    uses, those only a console word shows, those only stored, and the
    command fields."""
    wanting = [(row, found.get(row.line, {})) for row in rows if row.direction == 'response' and gap_terms(row)]
    strongest = {
        row.line: max((each.use for each in uses.values()), key=(STORED, *CLASSES[1:]).index, default=STORED)
        for row, uses in wanting
    }
    lines = ['| Message | Field | Bytes | Use | By | The engine sends |', '|---|---|---|---|---|---|']
    for row, uses in wanting:
        if strongest[row.line] in USED or row.compared != NONE:
            by = sorted({name for each in uses.values() if each.use in USED for name in each.by})
            lines.append(
                f'| `@@{row.message}` | {row.field} | {bytes_of(row)} | {where_used(row, uses)} | '
                f'{", ".join(f"`{name}`" for name in by)} | {gap_terms(row)} |'
            )
    for title, use in (('Console only', CLASSES[1]), ('Stored only', STORED)):
        lines += ['', f'| Message | Field | Bytes | {title} | The engine sends |', '|---|---|---|---|---|']
        for row, uses in wanting:
            if strongest[row.line] == use and row.compared == NONE:
                lines.append(
                    f'| `@@{row.message}` | {row.field} | {bytes_of(row)} | {where_used(row, uses)} | {gap_terms(row)} |'
                )
    lines += ['', '| Command | Field | Set by | The engine has it |', '|---|---|---|---|']
    for row in rows:
        if row.direction == 'command' and gap_terms(row):
            lines.append(f'| `@@{row.message}` | {row.field} | {short(row.images.split(","))} | {gap_terms(row)} |')
    return '\n'.join(lines) + '\n'


def between_markers(document: str, made: str) -> str | None:
    """The document with made between its markers, or nothing if it
    lacks them."""
    if BEGIN not in document or END not in document:
        return None
    head, _, rest = document.partition(BEGIN)
    return f'{head}{BEGIN}\n{made}{END}{rest.partition(END)[2]}'


def summary(
    rows: list[Row], tables: dict[str, Requests], traces: dict[str, Traced], found: dict[int, dict[str, Found]]
) -> str:
    """The table's size, each image's reads, uses and sends, and the
    gaps."""
    lines = ['rows by message (command, response):']
    for message in dict.fromkeys(row.message for row in rows):
        counts = [sum(row.message == message and row.direction == direction for row in rows) for direction in DIRECTIONS]
        lines.append(f'  {message} {counts[0]} {counts[1]}')
    lines.append(f'  total {len(rows)}')
    for image, traced in traces.items():
        lines.append(f'{image} reads:')
        for message, offsets in traced.reads.items():
            entry = next(entry for entry in tables[image].entries if entry.id == message)
            data = entry.length - HEADER - TRAILER
            lines.append(f'  {message} {len(offsets)} of {data} bytes: {spans(offsets)}')
        lines.append(f'{image} compares:')
        for message, offsets in traced.compared.items():
            lines.append(f'  {message} {spans(offsets)}')
        uses = [each[image].use for each in found.values() if image in each]
        counts = ', '.join(f'{use} {uses.count(use)}' for use in (*reversed(CLASSES[1:]), STORED))
        lines.append(f'{image} fields by use: {counts}, of {len(uses)} read')
        for kind, readers in zip(KINDS, (traced.ran, traced.reach, traced.readers)):
            functions = sorted(set().union(*readers.values()))
            lines.append(f'{image} functions {kind}: {len(functions)}')
        hows = [how for _, _, how in traced.walks]
        lines.append(f'{image} walks: ' + ', '.join(f'{how} {hows.count(how)}' for how in WALKS))
        sets = sorted(sender for sender, modes in tables[image].sent.items() if any(mode in SET_MODES for mode in modes))
        asks = sorted(sender for sender, modes in tables[image].sent.items() if not any(mode in SET_MODES for mode in modes))
        lines.append(f'{image} sets: {" ".join(sets)}')
        lines.append(f'{image} only asks: {" ".join(asks)}')
    lines.append('gaps:')
    lines.append(gaps(rows, found))
    return '\n'.join(lines)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--check', action='store_true', help='fail if the table or the document is stale')
    arguments = parser.parse_args()
    tables = {image: requests(image) for image in IMAGES}
    was = TABLE.read_text()
    rows = rows_of(was)
    problems = check_rows(rows, tables)
    hand, wrong = uses_of(USES.read_text())
    problems += wrong
    traces = {image: trace(image, rows, tables[image]) for image in IMAGES}
    problems += images_of(rows, tables, {image: traced.reads for image, traced in traces.items()})
    found, wrong = classify(rows, traces, hand)
    problems += wrong + cells_of(rows, traces, found)
    print(summary(rows, tables, traces, found))
    document = DOCUMENT.read_text()
    made = between_markers(document, gaps(rows, found))
    if made is None:
        problems.append(f'{DOCUMENT.relative_to(ROOT)}: no {BEGIN} ... {END}')
        made = document
    table = text_of(rows)
    if arguments.check:
        if table != was:
            problems.append(f'{TABLE.relative_to(ROOT)} is not what the images give; make gps-fields rewrites it')
        if made != document:
            problems.append(f'{DOCUMENT.relative_to(ROOT)}: the gaps are stale; make gps-fields rewrites them')
    elif not problems:
        TABLE.write_text(table)
        DOCUMENT.write_text(made)
    for problem in problems:
        print(problem, file=sys.stderr)
    sys.exit(1 if problems else 0)


if __name__ == '__main__':
    main()
