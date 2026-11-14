# /// script
# requires-python = ">=3.11"
# dependencies = ["unicorn==2.1.4"]
# ///
"""Checks docs/firmware/gps-fields.tsv, the fields of every message the
GPS engine of crates/smartclock-gps speaks, against what the images do:
which bytes of each message an image's own decoders read, measured by
running them in Unicorn, and which commands its request scripts send.

    uv run -q tools/firmware/gps_fields.py [--check]

Without --check it rewrites the table's images column and the list of
gaps between the markers in docs/firmware/gps.md; with it, it fails if
either is not what the images give.  Either way it fails if a
message's rows do not tile its data, if an image reads part of a field
or outside the data, or if a row's engine column is not in the
vocabulary below, and prints the table's size, each image's read set
and the gaps.

The images are the four with an Oncore message table and rewritten C:
the Z3805A 3543B, the Z3801A 3543, the 58503A 3704 and the Z3817A
3752.  The Z3815A speaks NMEA with Furuno's sentences, which the engine
does not, and has no row.

A message's rows cover its data alone, by offset from the frame's
first @: the four bytes of @@ and ID before it and the checksum, CR and
LF after it are every message's and have no row.  A field repeated,
one a channel or a satellite, is one row with a count and the stride
from each to the next.

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
byte: a field copied to a record that nothing uses counts, as does one
only a console word shows.  Bytes are filled with each of FILLS in
turn and the reads united.  Trap #7, by which a status decoder asks
its own status register, runs the image's handler; any other exception
or an access outside ROM and RAM fails the run.

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
gap is a response field an image reads with a constant, zero or
unknown term, or a command field an image sets that is not honored.
"""

import argparse
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

from loop_trace import CALL_LIMIT, RAM, RAM_SIZE, RETURN, ROM_SIZE, STACK, SUPERVISOR, names

ROOT = pathlib.Path(__file__).resolve().parents[2]
FIRMWARE = ROOT / 'third_party/firmware'
TABLE = ROOT / 'docs/firmware/gps-fields.tsv'
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
MESSAGE = RAM + RAM_SIZE - 0x10000
MESSAGE_SPACE = 0x1000
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
    'manual',
    'page',
    'images',
    'engine',
    'notes',
)
NONE = '-'
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
# The request modes that set a value (gps.md, "Requests").
SET_MODES = (0, 2)

BEGIN = '<!-- gps_fields.py gaps -->'
END = '<!-- end gps_fields.py -->'


@dataclass(frozen=True)
class Entry:
    """One entry of an image's message table: its ID, the length it
    frames the response at (0 for none) and its decoders, each with the
    record it fills."""

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
    manual: str
    page: str
    images: str
    engine: str
    notes: str

    def instances(self) -> list[range]:
        """The bytes of each field of the row."""
        return [
            range(self.offset + index * self.stride, self.offset + index * self.stride + self.length)
            for index in range(self.count)
        ]

    def terms(self) -> list[tuple[str, str]]:
        """The engine column's terms, each with its note."""
        return [tuple(term.partition(':')[::2]) for term in self.engine.split(';')]

    def cells(self) -> list[str]:
        stride = NONE if self.count == 1 else str(self.stride)
        return [
            self.message,
            self.direction,
            self.field,
            str(self.offset),
            str(self.length),
            str(self.count),
            stride,
            self.manual,
            self.page,
            self.images,
            self.engine,
            self.notes,
        ]


def requests(image: str) -> Requests:
    """An image's message table and what it sends, from dump-oncore's
    text."""
    entries = []
    sent: dict[str, set[int]] = {}
    for line in (ONCORE / f'{image}.txt').read_text().splitlines():
        entry = re.match(r'message 0x[0-9a-f]+ (\w\w) kind \d+ length (\d+)(.*)', line)
        if entry:
            decoders = re.findall(r'decoder (0x[0-9a-f]+) (0x[0-9a-f]+)', entry[3])
            entries.append(
                Entry(entry[1], int(entry[2]), tuple((int(decode, 16), int(record, 16)) for decode, record in decoders))
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
        self.uc = Uc(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN)
        self.uc.ctl_set_cpu_model(UC_CPU_M68K_M68020)
        self.uc.mem_map(0, ROM_SIZE)
        self.uc.mem_write(0, rom[:ROM_SIZE])
        self.uc.mem_map(RAM, RAM_SIZE)
        self.uc.hook_add(UC_HOOK_MEM_INVALID, self.invalid)
        self.uc.hook_add(UC_HOOK_INTR, self.exception)
        self.uc.hook_add(UC_HOOK_MEM_READ, self.read, begin=MESSAGE, end=MESSAGE + MESSAGE_SPACE - 1)

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
        sp = STACK - VECTOR_SIZE * (1 + len(arguments))
        self.uc.mem_write(sp, struct.pack(f'>{1 + len(arguments)}I', RETURN, *arguments))
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

    def decode(self, entry: Entry, fill: int) -> set[int]:
        """The offsets of a message of entry's, every data byte fill,
        that its decoders read."""
        data = bytes([fill]) * (entry.length - HEADER - TRAILER)
        self.uc.mem_write(MESSAGE, b'@@' + entry.id.encode() + data + b'\x00\r\n')
        self.reads = set()
        cursor = MESSAGE + HEADER
        for decoder, record in entry.decoders:
            cursor = self.call(decoder, cursor, record)
        reads, self.reads = self.reads, None
        return reads


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
                    machine.decode(entry, 0)
        found[count] = machine
    return found


def read_sets(image: str, table: Requests) -> dict[str, set[int]]:
    """The offsets an image's decoders read of each message that has
    any."""
    by_count = machines(image, table)
    found: dict[str, set[int]] = {}
    for entry in table.entries:
        if not entry.decoders or entry.length < HEADER + TRAILER:
            continue
        if entry.id in SIX_CHANNEL:
            counts = [6]
        elif entry.id in EIGHT_CHANNEL:
            counts = [8]
        else:
            counts = list(by_count)
        reads = found.setdefault(entry.id, set())
        for count in counts:
            for fill in FILLS:
                reads |= by_count[count].decode(entry, fill)
    return found


def rows_of(text: str) -> list[Row]:
    lines = text.splitlines()
    if lines[0].split('\t') != list(COLUMNS):
        sys.exit(f'{TABLE.relative_to(ROOT)}: the columns are not {" ".join(COLUMNS)}')
    rows = []
    for number, line in enumerate(lines[1:], start=2):
        cells = line.split('\t')
        if len(cells) != len(COLUMNS):
            sys.exit(f'{TABLE.relative_to(ROOT)}:{number}: {len(cells)} cells')
        message, direction, name, offset, length, count, stride, manual, page, images, engine, notes = cells
        rows.append(
            Row(
                number,
                message,
                direction,
                name,
                int(offset),
                int(length),
                int(count),
                int(length) if stride == NONE else int(stride),
                manual,
                page,
                images,
                engine,
                notes,
            )
        )
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
        for row in sorted(group, key=lambda row: row.offset):
            if row.count > 1 and row.stride < row.length:
                problems.append(f'{TABLE.relative_to(ROOT)}:{row.line}: stride {row.stride} under length')
        covered = sorted(
            (span.start, span.stop, row.line) for row in group for span in row.instances()
        )
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


def gap_terms(row: Row) -> str:
    """What makes a row a gap, or nothing."""
    if row.images == NONE:
        return ''
    if row.direction == 'command':
        wanted = [(term, note) for term, note in row.terms() if term != HONORED]
    else:
        wanted = [(term, note) for term, note in row.terms() if term in GAP_SOURCES]
    return '; '.join(f'{term}: {note}' if note else term for term, note in wanted)


def short(images: str) -> str:
    """A row's images as the document lists them."""
    return 'all four' if images.split(',') == list(IMAGES) else ', '.join(images.split(','))


def bytes_of(row: Row) -> str:
    last = row.offset + (row.count - 1) * row.stride + row.length - 1
    each = f', {row.count} x {row.length}' if row.count > 1 else ''
    return f'{row.offset}-{last}{each}' if last != row.offset else str(row.offset)


def gaps(rows: list[Row]) -> str:
    """The gaps as the document's two tables."""
    lines = [
        '| Message | Field | Bytes | Read by | The engine sends |',
        '|---|---|---|---|---|',
    ]
    for row in rows:
        if row.direction == 'response' and gap_terms(row):
            lines.append(
                f'| `@@{row.message}` | {row.field} | {bytes_of(row)} | {short(row.images)} | {gap_terms(row)} |'
            )
    lines += ['', '| Command | Field | Set by | The engine has it |', '|---|---|---|---|']
    for row in rows:
        if row.direction == 'command' and gap_terms(row):
            lines.append(f'| `@@{row.message}` | {row.field} | {short(row.images)} | {gap_terms(row)} |')
    return '\n'.join(lines) + '\n'


def summary(rows: list[Row], tables: dict[str, Requests], reads: dict[str, dict[str, set[int]]]) -> str:
    """The table's size, each image's reads and sends, and the gaps."""
    lines = ['rows by message (command, response):']
    for message in dict.fromkeys(row.message for row in rows):
        counts = [sum(row.message == message and row.direction == direction for row in rows) for direction in DIRECTIONS]
        lines.append(f'  {message} {counts[0]} {counts[1]}')
    lines.append(f'  total {len(rows)}')
    for image in IMAGES:
        lines.append(f'{image} reads:')
        for message, offsets in reads[image].items():
            entry = next(entry for entry in tables[image].entries if entry.id == message)
            data = entry.length - HEADER - TRAILER
            lines.append(f'  {message} {len(offsets)} of {data} bytes: {spans(offsets)}')
        sets = sorted(sender for sender, modes in tables[image].sent.items() if any(mode in SET_MODES for mode in modes))
        asks = sorted(sender for sender, modes in tables[image].sent.items() if not any(mode in SET_MODES for mode in modes))
        lines.append(f'{image} sets: {" ".join(sets)}')
        lines.append(f'{image} only asks: {" ".join(asks)}')
    lines.append('gaps:')
    lines.append(gaps(rows))
    return '\n'.join(lines)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--check', action='store_true', help='fail if the table or the document is stale')
    arguments = parser.parse_args()
    tables = {image: requests(image) for image in IMAGES}
    was = TABLE.read_text()
    rows = rows_of(was)
    problems = check_rows(rows, tables)
    reads = {image: read_sets(image, tables[image]) for image in IMAGES}
    problems += images_of(rows, tables, reads)
    print(summary(rows, tables, reads))
    document = DOCUMENT.read_text()
    if BEGIN not in document or END not in document:
        problems.append(f'{DOCUMENT.relative_to(ROOT)}: no {BEGIN} ... {END}')
        made = document
    else:
        head, _, rest = document.partition(BEGIN)
        made = f'{head}{BEGIN}\n{gaps(rows)}{END}{rest.partition(END)[2]}'
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
