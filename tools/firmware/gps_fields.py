# /// script
# requires-python = ">=3.11"
# dependencies = ["unicorn==2.1.4"]
# ///
"""Checks docs/firmware/gps-fields.tsv, the fields of every message the
GPS engine of crates/smartclock-gps speaks, against what the images do:
which bytes of each message an image's own decoders read, measured by
running them in Unicorn, what becomes of each field read, and which
commands it sends, by every path (gps_paths.py).

    uv run -q tools/firmware/gps_fields.py [--check]

Without --check it rewrites the table's images, compared, use and
fields columns, the list of gaps and the table of paths between their
markers in docs/firmware/gps.md, and the fields the images use and the
settings they set between the markers in
crates/smartclock-gps/src/record/field.rs; with it, it fails if any
is not what the images give.  Either way it fails if a message's rows do not tile its
data, if an image reads part of a field or outside the data, if a
row's engine column is not in the vocabulary below, or if
docs/firmware/gps-field-uses.tsv does not account for each function
found reading a field, and prints the table's size, each image's read
set, compared set and counts of fields by use, and the gaps.

The images are the six with an Oncore message table and rewritten C:
the Z3805A 3543B, the Z3801A 3543, the 58503A 3704, the Z3817A 3752,
the Z3816A 4001 and the 58503B 1.01.04.  The Z3815A 4010 and the
Z3805A 1.01.03 (NMEA_IMAGES) speak NMEA with Furuno's sentences, which
the Furuno model speaks (engine::furuno), and their rows, manual GT,
count fields: a row's
offset is the field's index as the images number it, the address's
fields first, and its length one field.  Their reads, stores, compared
fields and sets are measured as nmea_trace says, and the rest of what
follows holds of them as of an Oncore's bytes.

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

A command field's images are those of the images above with a path
(gps_paths.py) that sends the command to set it, mode 0 or 2, and that
the GPS task takes: a script, a list, a sequence, a monitor, a SCPI
descriptor or a console word.  The console's gps_change, which sends
whatever request it is given, counts for none.  A query sends no
field.  gps_paths.py's paths of every image, those above and the rest
it reads, are the table of docs/firmware/gps.md, "Every path to a
setting", which this rewrites and checks.

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

A record.PATH names a RecordField of
crates/smartclock-gps/src/record/field.rs: the one whose path it is, or
whose path it goes on from.  The fields column, made here, lists the
RecordFields a row's terms name, the clock being the record's time.
The unknown column, by hand, is the value the manual gives the field
for not known, where it gives one.  The fields the images use
(IMAGE_USES) are those of the rows some image uses as control or
reported.

and what it does with a command field:

    honored       the engine acts on it: what it changes is in the
                  bytes the engine builds
    held:FIELD    the engine keeps it in the FIELD of its Settings and
                  tells its holder of a change; the holder acts on it
    given:FIELD   beside honored: the engine acts on it and gives it
                  in the FIELD of its Settings as well, telling its
                  holder of a change, for whatever fills what the
                  engine acts on; no holder is held to it
    stored        kept and answered, with no effect and no Setting
    echoed        answered, not kept
    ignored       not looked at

The acts column, made here, is a command field's class: engine (every
term honored or given), holder (held), engine;holder (both), or gap (a
stored, echoed or ignored term), and "-" for a response field.  A held
or given term names a field of the Settings of
crates/smartclock-gps/src/engine/settings.rs, each of which some row
names.  The settings the images set (IMAGE_SETS) are those of the held
and given terms of the rows of what some path of any image sets, and
of them those a holder acts on (IMAGE_HOLDS) are the held ones: a
given one is the engine's to act on, and a provider is held to those
of IMAGE_HOLDS alone.

A gap is a response field with a constant, zero or unknown term that
an image uses, as control or reported, or compares, or a command field
an image sets whose class is gap.  Such a response field that an
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
    UC_HOOK_CODE,
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

import gps_paths
import record_access
from loop_trace import CALL_LIMIT, RAM, RAM_SIZE, RETURN, ROM_SIZE, STACK, SUPERVISOR, globals_of, names

ROOT = pathlib.Path(__file__).resolve().parents[2]
FIRMWARE = ROOT / 'third_party/firmware'
TABLE = ROOT / 'docs/firmware/gps-fields.tsv'
USES = ROOT / 'docs/firmware/gps-field-uses.tsv'
DOCUMENT = ROOT / 'docs/firmware/gps.md'
RECORD = ROOT / 'crates/smartclock-gps/src/record.rs'
FIELD = ROOT / 'crates/smartclock-gps/src/record/field.rs'
SETTINGS = ROOT / 'crates/smartclock-gps/src/engine/settings.rs'
ONCORE = ROOT / 'docs/firmware/oncore'

# Each image's primary vector table, as harness.py's profiles have it.
IMAGES = {
    'z3805a-3543b': 0x10000,
    'z3801a-3543': 0x10000,
    '58503a-3704': 0x10000,
    'z3817a-3752': 0x20000,
    'z3816a-4001': 0x20000,
    '58503b-1.01.04': 0x20000,
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
    'unknown',
    'fields',
    'acts',
    'notes',
)
NONE = '-'
BYTE_BITS = 8
DIRECTIONS = ('command', 'response')
MANUALS = ('VP', 'UT', 'M12+', 'GT')
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
HANDLINGS = ('honored', 'held', 'given', 'stored', 'echoed', 'ignored')
HONORED = 'honored'
HELD = 'held'
GIVEN = 'given'
# The handlings whose note names a field of the engine's Settings.
NAMES_SETTING = (HELD, GIVEN)
# A command field's classes, and which of them a term of each handling
# puts it in.
ENGINE_ACTS = 'engine'
HOLDER_ACTS = 'holder'
GAP = 'gap'
ACTS = {HONORED: ENGINE_ACTS, GIVEN: ENGINE_ACTS, HELD: HOLDER_ACTS}
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
# The same around the table of every path to a setting.
PATHS_BEGIN = '<!-- gps_fields.py paths -->'
# A message the engine speaks, as oncore.rs names it.
SPOKEN = re.compile(r'MessageId::new\(\*b"(\w\w)"\)')
# The same in field.rs, around the fields the images use.
USES_BEGIN = '// gps_fields.py image uses'
USES_END = '// end gps_fields.py'
# A RecordField and its path, as RecordField::path gives them.
FIELD_PATH = re.compile(r'Self::(\w+) => "([\w.]+)",')
# The RecordField the engine's clock is set from.
CLOCK_FIELD = 'Time'
# A field of the engine's Settings, and a member of its Setting.
SETTINGS_FIELD = re.compile(r'^    pub (\w+):', re.MULTILINE)
SETTING_MEMBER = re.compile(r'^    (\w+),$', re.MULTILINE)


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
    """An image's message table and the IDs its scripts and lists send
    and its code posts, each with the modes it is sent in."""

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
    unknown: str
    fields: str
    acts: str
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

    def held(self) -> list[str]:
        """The fields of the engine's Settings its terms hold for a
        holder to act on."""
        return [note for term, note in self.terms() if term == HELD]

    def settings(self) -> list[str]:
        """The fields of the engine's Settings its terms name, held or
        given."""
        return [note for term, note in self.terms() if term in NAMES_SETTING]

    def acts_of(self) -> str:
        """A command field's class, from its terms."""
        classes = {ACTS.get(term, GAP) for term, _ in self.terms()}
        if GAP in classes:
            return GAP
        return ';'.join(name for name in (ENGINE_ACTS, HOLDER_ACTS) if name in classes)

    def cells(self) -> list[str]:
        stride = NONE if self.count == 1 else str(self.stride)
        return [stride if name == 'stride' else str(getattr(self, name)) for name in COLUMNS]


def requests(image: str, found: list[gps_paths.Path]) -> Requests:
    """An image's message table, from dump-oncore's text, and the IDs its
    paths send, each with the modes, of those the GPS task takes."""
    entries = []
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
    sent: dict[str, set[int]] = {}
    for path in found:
        if not path.refused and path.target.kind == 'message' and path.mode is not None:
            sent.setdefault(path.target.message, set()).add(path.mode)
    return Requests(tuple(entries), {sender: frozenset(modes) for sender, modes in sent.items()})


class Machine:
    """One image's ROM in Unicorn, noting the reads of a message."""

    def __init__(self, image: str, rom: bytes) -> None:
        self.image = image
        self.vectors = IMAGES.get(image) or NMEA_IMAGES[image]
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


# The images whose engine speaks NMEA with Furuno's sentences
# (docs/firmware/gps.md, "In every image"), each with its primary's
# vector table, as harness.py's profiles have it.  Where each keeps
# its tables and its records is where its exported C's globals place
# them (Nmea), and its request codes are gps_paths.py's.
NMEA_IMAGES = {'z3815a-4010': 0x20000, 'z3805a-1.01.03': 0x10000}
# The sizes of an nmea_sentence_entry and an nmea_field_entry, and of a
# gps_sentence_descriptor (rewritten/IMAGE/types/smartclock.h).
NMEA_ENTRY = 10
NMEA_FIELD_ENTRY = 10
NMEA_DESCRIPTOR = 0x20
# The framer's record, gps_response_buffer, an nmea_frame: its text,
# cursor, count of fields, checksum digits and each field's start.
FRAME_TEXT = 0x52
FRAME_CURSOR = 0x52
FRAME_FIELDS = 0x56
FRAME_STARTS = 0x58
FRAME_STARTS_COUNT = 0x14
FRAME_SIZE = 0x6C
# The routines the run stubs, returning at once: gps_periodic_send, which
# tps's commit calls to send the engine a sentence through the DUART
# (gps_requests.c), touching no field; and psos_mode, a kernel call.
NMEA_STUBS = ('gps_periodic_send', 'psos_mode')
# A field's address in a decoder's call, its first argument, and the
# decoder's place, its second, as nmea_sentence_decode pushes them.
PLACE_ARGUMENT = 8
# The functions of gps_nmea.c that decode and commit the sentences, which
# copy the fields into the records and so are not their readers; but
# nmea_position_message, the 58503A's oncore_position_message, which the
# commits call and which uses them.
NMEA_DECODING = re.compile(r'nmea_(?!position_message$)\w+')
# What the engine sends of each sentence the image decodes, as the
# Furuno model sends it (crates/smartclock-gps/tests/vectors/furuno), in
# the order of a second, GSV twice; each taken by the image.
NMEA_SECOND = (
    '$PFEC,GPtst,0,4850120001,0,0',
    '$GPGGA,120001,3719.5350,N,12159.8700,W,1,08,02.00,000055.0,M,-028.4,M,,',
    '$GPGSA,A,3,03,04,06,09,16,26,28,31,,,,,02.00,01.20,01.60',
    '$GPGSV,2,1,08,03,78,281,44,04,51,306,42,06,08,319,33,09,05,160,31',
    '$GPGSV,2,2,08,16,29,133,38,26,40,091,40,28,17,214,37,31,41,046,43',
    '$GPRMC,120001,A,3719.5350,N,12159.8700,W,020.0,123.5,221026,001.0,W',
    '$PFEC,GPanc,261024195006,00220200200000020000000002020020',
    '$PFEC,GPacc,XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX',
    '$PFEC,GPssd,A1,D06,E06,G001,H000055.0,M05,S01,T+0000,U00000000,W2,X2,Z2,t+000000',
    '$PFEC,GPrrm,0,0,00,00,00,00,+000,+05',
    '$PFEC,GPrsd,1,100,2',
    '$PFEC,GPtps,261022120001,3,1,2,280101000000,+1,18,261024195006,2441,388819',
)
# The same second with tps's leap second due, at the end of the month
# (nmea_leap_second_due), where NMEA_SECOND's is a year off and none of
# its variants makes it due: the leap fields' variants then change what
# the image keeps of a leap second pending.  A field's stores are those
# its variants change in either second.
NMEA_LEAP_DUE = tuple(
    sentence.replace(',280101000000,', ',261101000000,') if sentence.startswith('$PFEC,GPtps,') else sentence
    for sentence in NMEA_SECOND
)
NMEA_SECONDS = (NMEA_SECOND, NMEA_LEAP_DUE)
# The commands gps_response_match is asked about, each with the response
# that answers it: the set of each parameter the image sets, GLL and rrs.
NMEA_COMMANDS = ('set', 'GLL', 'rrs')
# The fields of an image's gps record, where its check-layout.c places
# them: the records the commits fill, each a record a walk runs to the
# end of, as an Oncore decoder's record is; and gps.position_hold, which
# gps_response_match tests before it compares a position or a height
# (gps_nmea.c).
GPS_FIELD = re.compile(r'CHECK_FIELD\(gps_state, \w+, (0x[0-9a-f]+) ')
POSITION_HOLD = re.compile(r'CHECK_FIELD\(gps_state, position_hold, (0x[0-9a-f]+) ')


@dataclass(frozen=True)
class Nmea:
    """Where one of NMEA_IMAGES keeps what the run needs: its table of
    the sentences it decodes, its sentence descriptors and the first
    code and count of them, the framer's record, its gps record, the
    address of gps.position_hold and the fields of gps."""

    image: str
    sentences: int
    descriptors: int
    first_code: int
    codes: int
    frame: int
    gps: int
    position_hold: int
    gps_fields: frozenset[int]


def nmea_of(image: str) -> Nmea:
    known = globals_of(image)
    layout = (FIRMWARE / 'rewritten' / image / 'types' / 'check-layout.c').read_text()
    first_code, codes = gps_paths.NMEA_IMAGES[image]
    return Nmea(
        image,
        known['nmea_sentences'],
        known['gps_sentence_descriptors'],
        first_code,
        codes,
        known['gps_response_buffer'],
        known['gps'],
        int(POSITION_HOLD.search(layout)[1], 16),
        frozenset(int(address, 16) for address in GPS_FIELD.findall(layout)),
    )


def nmea_name(sentence: str) -> str:
    """A sentence's name: what follows GP in its address."""
    fields = sentence[1:].split(',')
    return (fields[1] if fields[0] == 'PFEC' else fields[0])[2:]


def nmea_shown(message: str) -> str:
    """A message as the document names it."""
    if len(message) != 3:
        return f'@@{message}'
    return f'$PFEC,GP{message}' if message[0].islower() else f'$GP{message}'


def is_nmea(message: str) -> bool:
    return len(message) == 3


@dataclass(frozen=True)
class Sentence:
    """One sentence of the image's table: its length and its field ends
    where positive, and its field entries: index, length, decoder and
    place."""

    name: str
    length: int
    fields: int
    entries: tuple[tuple[int, int, int, int], ...]


def nmea_table(rom: bytes, nmea: Nmea) -> dict[str, Sentence]:
    """An image's table of the sentences it decodes, by name."""

    def long(at: int) -> int:
        return struct.unpack_from('>I', rom, at)[0]

    def text(at: int) -> str:
        return rom[at : rom.index(0, at)].decode('ascii')

    found = {}
    at = nmea.sentences
    while long(at):
        length, fields = struct.unpack_from('>bb', rom, at + 4)
        entries = []
        place = long(at + 6)
        while place:
            index, size = struct.unpack_from('>bb', rom, place)
            entries.append((index, size, long(place + 2), long(place + 6)))
            if index == 0:
                break
            place += NMEA_FIELD_ENTRY
        found[text(long(at))] = Sentence(text(long(at)), length, fields, tuple(entries))
        at += NMEA_ENTRY
    return found


def nmea_sent(found: list[gps_paths.Path]) -> set[tuple[str, str | None]]:
    """What an image's paths send to set: each sentence by its name and,
    for set and int, the parameter."""
    return {(path.target.message, path.target.key) for path in found if sets(path)}


def sets(path: gps_paths.Path) -> bool:
    """Whether a path sets a message's value: one the GPS task takes, of
    mode 0 or 2."""
    return not path.refused and path.target.kind == 'message' and path.mode in SET_MODES


def nmea_frame(sentence: str, frame: int) -> tuple[bytes, list[int]]:
    """A sentence as the framer leaves it in its record, at frame, and
    each field's offset in the text."""
    text = bytearray(b'$')
    starts = [1]
    for character in sentence[1:].encode('ascii'):
        if character == ord(','):
            text.append(0)
            starts.append(len(text))
        else:
            text.append(character)
    text += b'\x00\n'
    record = bytearray(FRAME_SIZE)
    record[: len(text)] = text
    struct.pack_into('>I', record, FRAME_CURSOR, frame + len(text))
    record[FRAME_FIELDS] = len(starts)
    for index, start in enumerate(starts[:FRAME_STARTS_COUNT]):
        record[FRAME_STARTS + index] = start
    return bytes(record), starts


class NmeaMachine(Machine):
    """An NMEA image's ROM in Unicorn, decoding its sentences as
    nmea_sentence_decode does, with NMEA_STUBS returning at once."""

    def __init__(self, rom: bytes, table: dict[str, Sentence], nmea: Nmea) -> None:
        super().__init__(nmea.image, rom)
        self.table = table
        self.nmea = nmea
        self.frame_reads: set[int] | None = None
        known = names(nmea.image)
        for stub in NMEA_STUBS:
            self.uc.hook_add(UC_HOOK_CODE, self.stub, begin=known[stub], end=known[stub])
        self.uc.hook_add(UC_HOOK_MEM_READ, self.frame_read, begin=nmea.frame, end=nmea.frame + FRAME_TEXT - 1)

    def stub(self, uc: Uc, address: int, size: int, data: object) -> None:
        sp = uc.reg_read(UC_M68K_REG_A7)
        uc.reg_write(UC_M68K_REG_PC, struct.unpack('>I', uc.mem_read(sp, VECTOR_SIZE))[0])
        uc.reg_write(UC_M68K_REG_A7, sp + VECTOR_SIZE)
        uc.reg_write(UC_M68K_REG_D0, 0)

    def frame_read(self, uc: Uc, access: int, address: int, size: int, value: int, data: object) -> None:
        if self.frame_reads is not None:
            self.frame_reads.update(range(address - self.nmea.frame, address - self.nmea.frame + size))

    def sentence(self, sentence: str) -> set[int]:
        """Decodes one sentence, each field's decoder called with the
        field and its place and the commit last, as nmea_sentence_decode
        calls them; the indices of the fields they read, a field's NUL,
        which ends it, its own, and the address's left out."""
        record, starts = nmea_frame(sentence, self.nmea.frame)
        self.uc.mem_write(self.nmea.frame, record)
        self.frame_reads = set()
        for index, _, decode, place in self.table[nmea_name(sentence)].entries:
            if decode and index < len(starts):
                self.call(decode, self.nmea.frame + starts[index], place)
        reads, self.frame_reads = self.frame_reads, None
        address = 2 if sentence.startswith('$PFEC') else 1
        return {
            index
            for index in range(address, len(starts))
            if any(starts[index] <= offset < (starts[index + 1] if index + 1 < len(starts) else FRAME_TEXT) for offset in reads)
        }

    def second(self, sentences: tuple[str, ...]) -> dict[str, set[int]]:
        """Decodes those of a second's sentences the image decodes; the
        fields read of each."""
        found: dict[str, set[int]] = {}
        for sentence in sentences:
            if nmea_name(sentence) in self.table:
                found.setdefault(nmea_name(sentence), set()).update(self.sentence(sentence))
        return found


# What a field's characters are changed to, each in turn, to find where
# it is stored: a hemisphere, a mode or a status letter the other; a
# satellite's health in anc, healthy, unhealthy; and its accuracy in
# acc, one of 7 to F, which the 1.01.03 marks (nmea_decode_satellite_bits),
# 0, and any other, not available among them, F.
LETTERS = {'N': 'S', 'S': 'N', 'E': 'W', 'W': 'E', 'A': 'V', 'V': 'A', 'M': 'A', '+': '-', '-': '+'}
HEALTH = str.maketrans('21', '12')
ACCURACY = str.maketrans('0123456789ABCDEFX', 'FFFFFFF000000000F')
ACCURACIES = frozenset('0123456789ABCDEFX')
SATELLITES = 32


def variants(field: str) -> list[str]:
    """Other values of a field, of its length, as the flips are of an
    Oncore's bytes: each digit's lowest bit flipped in turn, a single
    digit of 1 or 2 the other too, each letter LETTERS has the other,
    anc's healths each the other at once and acc's accuracies each
    across the 1.01.03's mark at once."""
    found = []
    if len(field) == SATELLITES and set(field) <= set('012'):
        found.append(field.translate(HEALTH))
    elif len(field) == SATELLITES and set(field) <= ACCURACIES:
        found.append(field.translate(ACCURACY))
    for at, character in enumerate(field):
        if character.isdigit():
            found.append(field[:at] + str(int(character) ^ 1) + field[at + 1 :])
        elif character in LETTERS:
            found.append(field[:at] + LETTERS[character] + field[at + 1 :])
    if field in ('1', '2'):
        found.append('2' if field == '1' else '1')
    return found or ['0']


def with_field(sentence: str, index: int, value: str) -> str:
    fields = sentence.split(',')
    fields[index] = value
    return ','.join(fields)


def nmea_compared(machine: NmeaMachine, rom: bytes) -> dict[str, set[int]]:
    """The fields of each response gps_response_match reads whole when
    asked whether it answers a command of the same values sent to set,
    with the position held: each of set's parameters the image sets
    against ssd, GLL against GGA and rrs against rsd."""
    nmea = machine.nmea
    match = names(nmea.image)[MATCH]
    machine.uc.mem_write(nmea.position_hold, b'\x01')
    second = {nmea_name(sentence): sentence for sentence in NMEA_SECOND}
    ssd = second['ssd'].split(',')[2:]
    gga = second['GGA'].split(',')
    rsd = second['rsd'].split(',')[2:]
    asked = [(f'$PFEC,GPset,{field}', 'set,' + field[0], second['ssd']) for field in ssd]
    asked.append((f'$GPGLL,{",".join(gga[2:6])},,', 'GLL,', second['GGA']))
    asked.append((f'$PFEC,GPrrs,{",".join(rsd)}', 'rrs,', second['rsd']))
    codes = {
        rom[(at := struct.unpack_from('>I', rom, nmea.descriptors + code * NMEA_DESCRIPTOR)[0]) : rom.index(0, at)].decode(
            'ascii'
        ): nmea.first_code + code
        for code in range(nmea.codes)
    }
    found: dict[str, set[int]] = {}
    for sent, address, response in asked:
        if address not in codes:
            continue
        text = sent.encode('ascii') + b'\r\n'
        record, starts = nmea_frame(response, nmea.frame)
        machine.uc.mem_write(SENT, text)
        machine.uc.mem_write(nmea.frame, record)
        machine.frame_reads = set()
        code = codes[address]
        machine.call_with(match, struct.pack(MATCH_ARGUMENTS, code, SENT, len(text), 0, code, nmea.frame, len(response) + 2))
        reads, machine.frame_reads = machine.frame_reads, None
        ends = starts[1:] + [len(response) + 1]
        found.setdefault(nmea_name(response), set()).update(
            index for index, start in enumerate(starts) if ends[index] - 1 > start and set(range(start, ends[index] - 1)) <= reads
        )
    return found


def nmea_trace(image: str, rows: list[Row]) -> tuple[Traced, dict[str, Sentence]]:
    """An NMEA image's Traced, and its sentences' fields by index.
    Where a field is stored is where RAM differs after a
    second's sentences are decoded with the field's value another, each
    of its variants, from a second decoded before, for each of
    NMEA_SECONDS; its readers are the scan's, the decoding functions
    (NMEA_DECODING) left out."""
    nmea = nmea_of(image)
    rom = (FIRMWARE / 'images' / f'{image}.bin').read_bytes()
    table = nmea_table(rom, nmea)
    machine = NmeaMachine(rom, table, nmea)
    reads: dict[str, set[int]] = {}
    seen_all: set[tuple[int, int]] = set()
    stored: dict[int, set[int]] = {}
    primed_by: dict[tuple[str, ...], bytes] = {}
    for second in NMEA_SECONDS:
        machine.second(second)
        primed = primed_by[second] = machine.ram()
        machine.watched = set()
        for name, indices in machine.second(second).items():
            reads.setdefault(name, set()).update(indices)
        seen_all |= machine.watched
        machine.watched = None
        base = machine.ram()
        for row in rows:
            if row.direction != 'response' or row.message not in table:
                continue
            changes = stored.setdefault(row.line, set())
            for span in row.instances():
                index = span.start
                sentences = list(second)
                at = next((place for place, sentence in enumerate(sentences) if nmea_name(sentence) == row.message), None)
                if at is None or index >= len(sentences[at].split(',')):
                    continue
                for value in variants(sentences[at].split(',')[index]):
                    altered = list(sentences)
                    altered[at] = with_field(sentences[at], index, value)
                    machine.uc.mem_write(RAM, primed)
                    machine.second(tuple(altered))
                    # The framer's record holds the sentence itself.
                    changes |= {
                        address for address in changed(base, machine.ram()) if not nmea.frame <= address < nmea.frame + FRAME_SIZE
                    }
    machine.uc.mem_write(RAM, primed_by[NMEA_SECOND])
    compared_fields = nmea_compared(machine, rom)
    every = set().union(*stored.values()) or {nmea.gps}
    first = min(min(every), nmea.gps)
    last = max(every)
    seeds = [
        (decode, PLACE_ARGUMENT, place)
        for sentence in table.values()
        for _, _, decode, place in sentence.entries
        if decode and first <= place <= last
    ]
    scan = readers(image, first, last, seeds)
    decoding = {name for name in record_access.primary_functions(image).values() if NMEA_DECODING.fullmatch(name)}
    found = {
        line: set().union(*(scan.by_address.get(address, set()) for address in addresses)) - decoding
        for line, addresses in stored.items()
    }
    functions = record_access.primary_functions(image)
    starts = sorted(functions)
    places = sorted(every)
    records = sorted(
        {place for sentence in table.values() for _, _, decode, place in sentence.entries if decode and place}
        | nmea.gps_fields
    )
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
        if row.line not in stored:
            continue
        sites = set().union(*(scan.sites.get(address, set()) for address in stored[row.line]))
        for pc, address in sorted(seen_all):
            function = functions[starts[bisect.bisect_right(starts, pc) - 1]]
            if function in decoding or address not in stored[row.line] or function in sites:
                continue
            walked = any(name == function and start <= address for name, start in scan.walk_sites)
            called = any(function in calls.get(reader, ()) for reader in found[row.line] | reach[row.line])
            if not walked and not called:
                ran[row.line].add(function)
    traced = Traced(reads, compared_fields, stored, found, reach, ran, scan.walks)
    return traced, table


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


def field_paths() -> dict[str, str]:
    """Each RecordField by its path, in the order of RecordField::ALL."""
    return {path: name for name, path in FIELD_PATH.findall(FIELD.read_text())}


def field_of(path: str, paths: dict[str, str]) -> str | None:
    """The RecordField a record path names: the one whose path it is,
    or the longest whose path it goes on from."""
    parts = path.split('.')
    for length in range(len(parts), 0, -1):
        if (name := paths.get('.'.join(parts[:length]))) is not None:
            return name
    return None


def fields_of(row: Row, paths: dict[str, str]) -> list[str]:
    """The RecordFields a row's terms name, in the order of
    RecordField::ALL."""
    named = set()
    for term, _ in row.terms():
        kind, _, path = term.partition('.')
        if term == 'clock':
            named.add(CLOCK_FIELD)
        elif kind == 'record' and (name := field_of(path, paths)) is not None:
            named.add(name)
    return [name for name in paths.values() if name in named]


def settings_fields() -> dict[str, str]:
    """Each field of the engine's Settings with the member of Setting
    that names it, in the struct's order."""
    source = SETTINGS.read_text()
    body = source.partition('pub struct Settings {')[2].partition('\n}')[0]
    members = set(SETTING_MEMBER.findall(source.partition('pub enum Setting {')[2].partition('\n}')[0]))
    fields = {}
    for name in SETTINGS_FIELD.findall(body):
        member = ''.join(part.capitalize() for part in name.split('_'))
        if member not in members:
            sys.exit(f'{SETTINGS.relative_to(ROOT)}: no Setting::{member} for {name}')
        fields[name] = member
    return fields


def targeted(rows: list[Row], target: gps_paths.Target) -> list[Row]:
    """The command rows of what a path sends: its message's, of set and
    int its parameter's alone."""
    if target.kind != 'message':
        return []
    return [
        row
        for row in rows
        if row.direction == 'command'
        and row.message == target.message
        and (target.message not in ANY_ORDER or row.field.split()[0] == target.key)
    ]


def path_settings(rows: list[Row], found: dict[str, list[gps_paths.Path]]) -> tuple[set[str], set[str]]:
    """The fields of the engine's Settings some image's path sets, and of
    them those a holder acts on: the held and given terms, and the held,
    of the command rows of what each path that sets sends."""
    setting: set[str] = set()
    holding: set[str] = set()
    for each in found.values():
        for path in each:
            if sets(path):
                for row in targeted(rows, path.target):
                    setting.update(row.settings())
                    holding.update(row.held())
    return setting, holding


def image_uses(
    rows: list[Row], found: 'dict[int, dict[str, Found]]', paths: dict[str, str], settings: tuple[set[str], set[str]]
) -> str:
    """IMAGE_USES, IMAGE_SETS and IMAGE_HOLDS as field.rs has them
    between its markers: the fields of the rows some image uses as
    control or reported, the settings some image's path sets
    (path_settings), and of them those a holder acts on."""
    setting, holding = settings
    members = settings_fields()
    sets = [
        '',
        "/// The settings a receiver's image sets: those a command row of the",
        '/// field table holds or gives whose message a path of some image',
        '/// sends to set (`docs/firmware/gps.md`, "Every path to a setting").',
        'pub const IMAGE_SETS: SettingSet = SettingSet::of(&[',
        *(f'    Setting::{member},' for name, member in members.items() if name in setting),
        ']);',
        '',
        '/// Of [`IMAGE_SETS`], those a holder acts on: some such row holds',
        '/// them.  A provider is held to each.',
        'pub const IMAGE_HOLDS: SettingSet = SettingSet::of(&[',
        *(f'    Setting::{member},' for name, member in members.items() if name in holding),
        ']);',
    ]
    used = {
        name
        for row in rows
        if any(each.use in USED for each in found.get(row.line, {}).values())
        for name in row.fields.split(';')
        if name != NONE
    }
    lines = [
        "/// The fields a receiver's image uses, to control something or to",
        '/// report it: those behind a row of the field table whose use is',
        '/// control or reported.',
        'pub const IMAGE_USES: FieldSet = FieldSet::of(&[',
        *(f'    RecordField::{name},' for name in paths.values() if name in used),
        ']);',
        *sets,
    ]
    return '\n'.join(lines) + '\n'


def check_rows(rows: list[Row], tables: dict[str, Requests], sentences: 'dict[str, dict[str, Sentence]]') -> list[str]:
    """What is wrong with the table by itself and against the images'
    lengths and the NMEA images' counts of fields."""
    problems = []
    record_fields = set(re.findall(r'^\s+pub (\w+):', RECORD.read_text(), re.MULTILINE))
    paths = field_paths()
    for path, name in paths.items():
        if not all(part in record_fields for part in path.split('.')):
            problems.append(f'{FIELD.relative_to(ROOT)}: {name}: no {path} in a record')
    messages = {row.message for row in rows}
    settings = settings_fields()
    unnamed = set(settings) - {name for row in rows if row.direction == 'command' for name in row.settings()}
    for name in sorted(unnamed):
        problems.append(f'{SETTINGS.relative_to(ROOT)}: no command row holds or gives {name}')
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
        for term, note in row.terms():
            kind, _, path = term.partition('.')
            if row.direction == 'command':
                good = term in HANDLINGS and (term not in NAMES_SETTING or note in settings)
            elif kind == 'record':
                good = (
                    bool(path)
                    and all(part in record_fields for part in path.split('.'))
                    and field_of(path, paths) is not None
                )
            elif kind == 'setting':
                good = path in messages
            else:
                good = term in SOURCES
            if not good:
                problems.append(f'{where}: engine term {term}')
        if row.direction == 'command' and row.unknown != NONE:
            problems.append(f'{where}: a command field with an unknown value')
        row.fields = ';'.join(fields_of(row, paths)) or NONE
        row.acts = row.acts_of() if row.direction == 'command' else NONE
    lengths: dict[str, set[int]] = {}
    for table in tables.values():
        for entry in table.entries:
            lengths.setdefault(entry.id, set()).add(entry.length)
    for (message, direction), group in groups.items():
        if is_nmea(message):
            problems += check_sentence(message, direction, group, sentences)
            continue
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


# The commands whose parameters come in any order, each its own field,
# and whose rows are by parameter, each at the field the image sends it
# in (GT pp. 11 and 13).
ANY_ORDER = ('set', 'int')
# The most fields nmea_sentence_fields_allowed lets GSV have.
GSV_FIELDS = 20


def check_sentence(
    message: str, direction: str, group: list[Row], sentences: 'dict[str, dict[str, Sentence]]'
) -> list[str]:
    """What is wrong with a sentence's rows: each a whole field, they
    tile its fields from the first after its address, and a response's
    end where each NMEA image's table counts its fields: one fewer for
    RMC, whose checksum's field the count takes in, and at most its most
    where the count is negative."""
    problems = []
    for row in group:
        if row.length != 1 or row.bits != NONE:
            problems.append(f'{TABLE.relative_to(ROOT)}:{row.line}: a field of a sentence is one, its bits -')
    if message in ANY_ORDER and direction == 'command':
        return problems
    at = 2 if message[0].islower() else 1
    covered = sorted((span.start, span.stop, row.line) for row in group for span in row.instances())
    for start, stop, line in covered:
        if start != at:
            problems.append(f'{TABLE.relative_to(ROOT)}:{line}: {nmea_shown(message)} {direction} field {at} then {start}')
        at = stop
    for image, table in sentences.items():
        taken = table.get(message)
        if direction != 'response' or taken is None:
            continue
        fields = GSV_FIELDS if taken.fields < 0 else taken.fields - (1 if message == 'RMC' else 0)
        if (taken.fields > 0 and at != fields) or (taken.fields < 0 and at > fields):
            problems.append(f'{nmea_shown(message)}: rows end at field {at}, {image} counts {taken.fields}')
    return problems


def images_of(
    rows: list[Row],
    tables: dict[str, Requests],
    reads: dict[str, dict[str, set[int]]],
    sent: dict[str, set[tuple[str, str | None]]],
) -> list[str]:
    """Sets each row's images from the images; what stops that.  A
    sentence's are the NMEA images': each whose decoders read a
    response field, each that sends a command field to set, a parameter
    of set or int by its letter or name, the first word of the row's
    field."""
    problems = []
    for image in NMEA_IMAGES:
        for message, indices in reads[image].items():
            fields = {index for row in rows if row.message == message and row.direction == 'response' for span in row.instances() for index in span}
            if indices - fields:
                problems.append(f'{image}: {nmea_shown(message)}: reads fields {spans(indices - fields)}, outside its rows')
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
        if is_nmea(row.message):
            found = []
            for image in NMEA_IMAGES:
                if row.direction == 'command':
                    key = row.field.split()[0] if row.message in ANY_ORDER else None
                    taken = (row.message, key) in sent[image]
                else:
                    indices = reads[image].get(row.message, set())
                    read = [span.start in indices for span in row.instances()]
                    if len(set(read)) != 1:
                        problems.append(f'{TABLE.relative_to(ROOT)}:{row.line}: {image} reads some of {nmea_shown(row.message)} {row.field}')
                    taken = any(read)
                if taken:
                    found.append(image)
            row.images = ','.join(found) or NONE
            continue
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
        if listed['images'] - set(IMAGES) - set(NMEA_IMAGES):
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
            f'{image}: {function} reads {nmea_shown(message)} {"; ".join(fields)} ({kind}): '
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
        wanted = [(term, note) for term, note in row.terms() if term not in ACTS]
    else:
        wanted = [(term, note) for term, note in row.terms() if term in GAP_SOURCES]
    return '; '.join(f'{term}: {note}' if note else term for term, note in wanted)


def short(images: list[str]) -> str:
    """Images as the document lists them."""
    return 'every Oncore image' if images == list(IMAGES) else ', '.join(images)


def bytes_of(row: Row) -> str:
    if is_nmea(row.message):
        last = row.offset + (row.count - 1) * row.stride
        each = f', {row.count} x 1' if row.count > 1 else ''
        return f'field {row.offset}' if last == row.offset else f'fields {row.offset}-{last}{each}'
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
    command fields; then every command field with who acts on it."""
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
                f'| `{nmea_shown(row.message)}` | {row.field} | {bytes_of(row)} | {where_used(row, uses)} | '
                f'{", ".join(f"`{name}`" for name in by)} | {gap_terms(row)} |'
            )
    for title, use in (('Console only', CLASSES[1]), ('Stored only', STORED)):
        lines += ['', f'| Message | Field | Bytes | {title} | The engine sends |', '|---|---|---|---|---|']
        for row, uses in wanting:
            if strongest[row.line] == use and row.compared == NONE:
                lines.append(
                    f'| `{nmea_shown(row.message)}` | {row.field} | {bytes_of(row)} | {where_used(row, uses)} | {gap_terms(row)} |'
                )
    lines += ['', '| Command | Field | Set by | The engine has it |', '|---|---|---|---|']
    for row in rows:
        if row.direction == 'command' and gap_terms(row):
            lines.append(f'| `{nmea_shown(row.message)}` | {row.field} | {short(row.images.split(","))} | {gap_terms(row)} |')
    lines += ['', '| Command | Field | Set by | Who acts | How |', '|---|---|---|---|---|']
    for row in rows:
        if row.direction == 'command':
            how = '; '.join(
                f'`{note}`' if term == HELD else f'{term}: {note}' if note else term for term, note in row.terms()
            )
            by = 'none' if row.images == NONE else short(row.images.split(','))
            lines.append(f'| `{nmea_shown(row.message)}` | {row.field} | {by} | {row.acts.replace(";", ", ")} | {how} |')
    return '\n'.join(lines) + '\n'


def spoken_messages() -> set[str]:
    """The Oncore messages the engine speaks, by oncore.rs's MessageIds."""
    return set(SPOKEN.findall((ROOT / 'crates/smartclock-gps/src/oncore.rs').read_text()))


def path_table(rows: list[Row], found: dict[str, list[gps_paths.Path]]) -> str:
    """Every path of every image as the document's table: each kind, name,
    what it sends and does, with the command rows' fields and settings
    of what it sends, and the images it is theirs in."""
    images: dict[tuple, list[str]] = {}
    for image, each in found.items():
        for path in each:
            if path.target.kind == 'any':
                does = 'any request, as set' if path.mode in SET_MODES else 'any request, as a query'
            elif path.mode is None:
                does = 'posts'
            else:
                does = 'sets' if path.mode in SET_MODES else 'asks'
            if path.refused:
                does += ', refused'
            key = (gps_paths.PATH_KINDS.index(path.kind), path.name, path.target.shown(), does, path.target)
            images.setdefault(key, [])
            if image not in images[key]:
                images[key].append(image)
    lines = ['| Path | Name | Sends | Fields | Settings | Does | Images |', '|---|---|---|---|---|---|---|']
    for key in sorted(images, key=lambda key: key[:4]):
        kind, name, shown, does, target = key
        matched = targeted(rows, target)
        if target.kind != 'message':
            fields = settings = NONE
        elif not matched:
            if is_nmea(target.message):
                spoken = target.message not in ANY_ORDER and any(row.message == target.message for row in rows)
            else:
                spoken = target.message in spoken_messages()
            fields, settings = 'no command row' if spoken else 'not spoken', NONE
        else:
            fields = ', '.join(row.field for row in matched)
            named = dict.fromkeys(
                f'`{note}` ({HOLDER_ACTS if term == HELD else ENGINE_ACTS})'
                for row in matched
                for term, note in row.terms()
                if term in NAMES_SETTING
            )
            settings = ', '.join(named) or NONE
        lines.append(
            f'| {gps_paths.PATH_KINDS[kind]} | `{name}` | {shown} | {fields} | {settings} | {does} | '
            f'{", ".join(images[key])} |'
        )
    return '\n'.join(lines) + '\n'


def between_markers(document: str, made: str, begin: str = BEGIN, end: str = END) -> str | None:
    """The document with made between its markers, or nothing if it
    lacks them."""
    if begin not in document or end not in document:
        return None
    head, _, rest = document.partition(begin)
    return f'{head}{begin}\n{made}{end}{rest.partition(end)[2]}'


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
            if image in NMEA_IMAGES:
                lines.append(f'  {message} fields {spans(offsets)}')
                continue
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
        if image not in tables:
            continue
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
    found_paths = {image: gps_paths.paths(image) for image in gps_paths.PATH_IMAGES}
    tables = {image: requests(image, found_paths[image]) for image in IMAGES}
    sent = {image: nmea_sent(found_paths[image]) for image in NMEA_IMAGES}
    was = TABLE.read_text()
    rows = rows_of(was)
    nmea = {image: nmea_trace(image, rows) for image in NMEA_IMAGES}
    problems = check_rows(rows, tables, {image: sentences for image, (_, sentences) in nmea.items()})
    hand, wrong = uses_of(USES.read_text())
    problems += wrong
    traces = {image: trace(image, rows, tables[image]) for image in IMAGES}
    traces |= {image: traced for image, (traced, _) in nmea.items()}
    problems += images_of(rows, tables, {image: traced.reads for image, traced in traces.items()}, sent)
    found, wrong = classify(rows, traces, hand)
    problems += wrong + cells_of(rows, traces, found)
    print(summary(rows, tables, traces, found))
    document = DOCUMENT.read_text()
    made = between_markers(document, gaps(rows, found))
    if made is None:
        problems.append(f'{DOCUMENT.relative_to(ROOT)}: no {BEGIN} ... {END}')
        made = document
    with_paths = between_markers(made, path_table(rows, found_paths), PATHS_BEGIN, END)
    if with_paths is None:
        problems.append(f'{DOCUMENT.relative_to(ROOT)}: no {PATHS_BEGIN} ... {END}')
    else:
        made = with_paths
    table = text_of(rows)
    source = FIELD.read_text()
    made_uses = image_uses(rows, found, field_paths(), path_settings(rows, found_paths))
    uses = between_markers(source, made_uses, USES_BEGIN, USES_END)
    if uses is None:
        problems.append(f'{FIELD.relative_to(ROOT)}: no {USES_BEGIN} ... {USES_END}')
        uses = source
    if arguments.check:
        if uses != source:
            problems.append(
                f'{FIELD.relative_to(ROOT)}: IMAGE_USES, IMAGE_SETS or IMAGE_HOLDS is not what the table gives; '
                'make gps-fields rewrites it'
            )
        if table != was:
            problems.append(f'{TABLE.relative_to(ROOT)} is not what the images give; make gps-fields rewrites it')
        if made != document:
            problems.append(f'{DOCUMENT.relative_to(ROOT)}: the gaps or the paths are stale; make gps-fields rewrites them')
    elif not problems:
        TABLE.write_text(table)
        DOCUMENT.write_text(made)
        FIELD.write_text(uses)
    for problem in problems:
        print(problem, file=sys.stderr)
    sys.exit(1 if problems else 0)


if __name__ == '__main__':
    main()
