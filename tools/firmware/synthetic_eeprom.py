"""Synthetic EEPROM images for the receivers no dump exists of, the
58503B 1.01.04, the Z3816A 4001, the Z3815A 4010 and the Z3805A 1.01.03,
whose one dump is not the repository's (docs/emulator.md,
"The EEPROM"): the records each image's own power-up reads, built as
its own writers build them, from the values its own code would write,
read out of the image itself.  Nothing here was dumped from a
receiver; the files are named synthetic for that.

Each image's layout is the 58503A 3704's (docs/firmware/58503a-3704.md,
"EEPROM"; docs/firmware/structures.md, "EEPROM records"), every address
below read from its image.  Each record's values, marked as the board
files mark theirs (boards/README.md, "Confidence"):

- the parameter record at 0x400000, 16 bytes: read, the SCI's defaults
  as save_parameter_record builds them;
- the identification record at 0x400010, 48 bytes: the model, the tag
  and the suffix A read, the image's own for a bad record; the serial
  number inferred, SERIAL_NUMBER, made up;
- the NV register's slot 0 at 0x4000c0, 160 bytes: read, nv_save's
  record of the settings at power-up, nv_settings_defaults and the
  image's initialized data, as the firmware's own first save on an
  erased part writes it (sequence 0, the erased slot's 0xffffffff plus
  one);
- the reset records at 0x400200 and the event log's pages at 0x400d80:
  read, what the same first power-up writes once it has saved the
  slot, its settings defaulted, log_erase's and log_clear's work
  (main's root task): every page cleared, with its write count the
  erased 0xffff plus one, then "Log cleared" in page 0's first entry
  and in the first reset record, each at the clock's time at power-up,
  0:00:00 on the erased lifetime record's date, 1994-01-01;
- every other byte erased, inferred: slot 1, the lifetime record and
  the calibration block, which the firmware reads as absent and fills
  with its own defaults, and none of which its power-up self-test
  reads.

The log is not left erased: the firmware never reads an erased page
as an empty one.  Its first entry on such a page reads the page in,
keeps its other entries' 0xffffffff formats, and takes the oldest
entry past the one it writes; :DIAGnostic:LOG:READ? then prints an
entry whose format is 0xffffffff, and the bus error stops the receiver
until its watchdog resets it, at every read on the Z3815A 4010, at the
first on the Z3805A 1.01.03.

tests/eeprom.rs holds the read records to the firmware: the NV slot
each image's first power-up on an erased part saves, the parameter
record the 58503B's and the Z3815A's write back, and the reset records
and event log pages it clears, are these bytes.

`--check` fails if a checked-in image is not what this builds; without
it the images are written.
"""

import argparse
import struct
import sys
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
IMAGES = ROOT / "third_party/firmware/images"

# The EEPROM, chip select 9's 8 KiB from 0x400000 (CSBAR9 0x4001,
# boards/58503b.toml, boards/z3816a.toml).
EEPROM_BASE = 0x400000
EEPROM_SIZE = 0x2000
ERASED = 0xFF

# The serial number every synthetic image holds: an HP serial's shape,
# four digits, a letter and five digits, as the bench 58503A's 3710A01056, but
# all zeros, which no unit's date code is.  Made up: no unit has it.
SERIAL_NUMBER = b"0000A00000"

# The checksummed records' word at +0: record_checksum's sum of the
# bytes after it and its complement (structures.md, "EEPROM records").
CHECKSUM = slice(0, 2)
BYTE_MASK = 0xFF
LONG_BITS = 32

# The parameter record (0x400000, 16 bytes), as save_parameter_record
# builds it: channel B's fields at +2 to +8, which these images, with no
# channel B port, fill from the SCI's settings and three constants, and
# the SCI's +2 to +5 at +9 to +0xc.
PARAMETER_RECORD = 0x400000
PARAMETER_RECORD_SIZE = 0x10
# The SCI's settings record, 6 bytes: two bytes no writer stores, then
# baud, parity, pacing and echo (structures.md, "The serial settings").
SCI_SETTINGS_SIZE = 6
SCI_BAUD, SCI_PARITY, SCI_PACING, SCI_ECHO = 2, 3, 4, 5
# save_parameter_record's channel B bytes: baud, parity and pacing,
# then 0 at +5, 1 at +6 (eight bits), 0 at +7, and echo at +8; the
# SCI's four at +9.
PARAMETER_CHANNEL_B = 2
PARAMETER_CONSTANTS = bytes([0, 1, 0])
PARAMETER_SCI = 9

# The identification record (0x400010, 48 bytes): the model at +2 and
# the serial number at +0x14, each 18 bytes, NUL padded (strncpy), the
# revision's suffix at +0x26, and the image's tag, three bytes, at
# +0x27.
IDENTIFICATION_RECORD = 0x400010
IDENTIFICATION_RECORD_SIZE = 0x30
IDENTIFICATION_MODEL = 2
IDENTIFICATION_SERIAL = 0x14
IDENTIFICATION_FIELD_SIZE = 0x12
IDENTIFICATION_SUFFIX = 0x26
IDENTIFICATION_TAG = 0x27
IDENTIFICATION_TAG_SIZE = 3

# The NV register's two slots (0x4000c0, 160 bytes each), nv_save's
# record: the tag's first two bytes at +0, the checksum at +2, the
# sequence number at +4, the firmware revision, 8 bytes, NUL padded, at
# +8, then the settings packed into longs most significant bit first,
# a value that would cross a long's end starting the next, and 0xff to
# the end.  nv_checksum sums the bytes from +8 on.
NV_SLOT_0 = 0x4000C0
NV_RECORD_SIZE = 0xA0
NV_TAG_SIZE = 2
NV_CHECKSUM = slice(2, 4)
NV_SEQUENCE = slice(4, 8)
NV_REVISION = slice(8, 0x10)
NV_PACKED = 0x10
NV_CHECKSUMMED = 8
# The sequence number of the firmware's first save to an erased slot:
# its 0xffffffff plus one, a long.
NV_FIRST_SEQUENCE = 0

# The reset records, 32 of 0x5c bytes from 0x400200, each a sequence
# number, then the checksum over the bytes after it (reset_record_write;
# eeprom_reset_records in each image's names.tsv).
RESET_RECORDS = 0x400200
RESET_RECORD_COUNT = 0x20
RESET_RECORD_SIZE = 0x5C
RESET_CHECKSUM = slice(2, 4)
# The event log's pages, 64 bytes from 0x400d80 to the part's end, each
# a write count, then the checksum over the bytes after it (log.c's
# LOG_EEPROM_PAGES in each image's rewritten C).
LOG_PAGES = 0x400D80
LOG_PAGE_SIZE = 64
LOG_WRITE_COUNT = slice(0, 2)
LOG_CHECKSUM = slice(2, 4)
# A page's first entry (smartclock.h's log_entry, 20 bytes): its time,
# whose first byte is the log's lap, its date, and the address of its
# format, a long each, then two argument longs.
LOG_ENTRIES = 4
LOG_ENTRY_TIME = slice(0, 4)
LOG_ENTRY_DATE = slice(4, 8)
LOG_ENTRY_FORMAT = slice(8, 0xC)
# A reset record's fields after the checksum: its time and date, a long
# each, and its text, 80 bytes, NUL padded (strncpy).
RESET_SEQUENCE = slice(0, 2)
RESET_TIME = slice(4, 8)
RESET_DATE = slice(8, 0xC)
RESET_TEXT = slice(0xC, RESET_RECORD_SIZE)
# The write count of a page's and the sequence number of a reset
# record's first write to an erased part: its 0xffff plus one, a word.
FIRST_WRITE = 0
# The clock at power-up on an erased lifetime record, which log_erase
# and log_clear stamp their entries with: clock_init starts the clock
# at 0:00:00 on the record's date, lifetime_record_read's default,
# 1994-01-01, year << 16 | month << 8 | day (rewritten/common/records.c,
# clock.c); the log's lap is 0, log_erase's.
POWER_UP_TIME = 0
POWER_UP_DATE = 1994 << 16 | 1 << 8 | 1

# A descriptor (rewritten/common/types/scpi_numeric_param.h,
# scpi_setting_param.h): its values less one at +1, its NV widths'
# pointer at +2, its value's pointer at +0x16 for a numeric one (longs)
# and +0xc for a setting (bytes).
DESCRIPTOR_LAST_INDEX = 1
DESCRIPTOR_WIDTHS = 2
NUMERIC_VALUE = 0x16
SETTING_VALUE = 0xC
LONG = 4


@dataclass(frozen=True)
class Copy:
    """A block of RAM the firmware fills from its image before nv_restore
    saves the settings: `size` bytes at `ram` from the image's `rom`."""

    ram: int
    rom: int
    size: int


@dataclass(frozen=True)
class Layout:
    """One image's EEPROM records, every address its own code's."""

    image: str
    output: str
    # The model, the identification record's tag and the NV slot's
    # (its first two bytes), and the firmware revision, each a string
    # in the image.
    model: int
    tag: int
    revision: int
    # The SCI's defaults, which parameter_record_read copies when the
    # record is bad.
    sci_defaults: int
    # "Log cleared", log_erase's entry's format, kept by address.
    log_cleared: int
    # nv_save's two descriptor tables and their lengths.
    numeric_params: int
    numeric_count: int
    setting_params: int
    setting_count: int
    # What the settings hold when nv_restore saves them on an erased
    # part: nv_settings_defaults over nv_settings, and the initialized
    # data system_init copies before it.
    copies: tuple[Copy, ...]


LAYOUTS = (
    Layout(
        image="58503b-1.01.04.bin",
        output="58503b-synthetic-eeprom.bin",
        model=0x43326,  # "58503B", the bad record's model (0x43178)
        tag=0x4332D,  # "CQB", compared at +0x27 (0x431ba), "CQ" at the slot's +0 (0x42bf6)
        revision=0x2307C,  # "1.01.04", the slot's revision (0x42c28)
        sci_defaults=0x4089E,  # parameter_record_read's (0x40f4e)
        log_cleared=0x42002,  # log_event's LOG_CLEARED (rewritten/58503b-1.01.04/log.c)
        numeric_params=0x4308A,  # nv_save (0x42c46), 23 (0x42ccc)
        numeric_count=23,
        setting_params=0x430E6,  # nv_save (0x42cd8), 19 (0x42d46)
        setting_count=19,
        copies=(
            Copy(ram=0x1028D6, rom=0x407D2, size=0xCB),  # nv_restore (0x42b02)
            Copy(ram=0x1027F8, rom=0x406F4, size=0xBE),  # system_init (0x22de0)
            Copy(ram=0x1029A2, rom=0x4089E, size=SCI_SETTINGS_SIZE),  # the SCI's (0x40f4e)
        ),
    ),
    Layout(
        image="z3816a-4001.bin",
        output="z3816a-synthetic-eeprom.bin",
        model=0x428E0,  # "Z3816A", the bad record's model (0x42736)
        tag=0x428E7,  # "S2", compared at +0x27 (0x42774), at the slot's +0 (0x421b4)
        revision=0x22FF8,  # "4001", the slot's revision (0x421e6)
        sci_defaults=0x4025A,  # parameter_record_read's (0x405e0)
        log_cleared=0x41562,  # log_event's LOG_CLEARED (rewritten/z3816a-4001/log.c)
        numeric_params=0x42648,  # nv_save (0x42204), 23 (0x4228a)
        numeric_count=23,
        setting_params=0x426A4,  # nv_save (0x42296), 18 (0x42304)
        setting_count=18,
        copies=(
            Copy(ram=0x10254E, rom=0x4018E, size=0xCB),  # nv_restore (0x420ea)
            Copy(ram=0x10249E, rom=0x400DE, size=0x95),  # system_init (0x22cd6)
            Copy(ram=0x10261A, rom=0x4025A, size=SCI_SETTINGS_SIZE),  # the SCI's (0x405e0)
        ),
    ),
    Layout(
        image="z3815a-4010.bin",
        output="z3815a-synthetic-eeprom.bin",
        model=0x47764,  # "Z3815A", the bad record's model (0x475ba)
        tag=0x4776B,  # "S3", compared at +0x27 (0x475f8), at the slot's +0 (0x47030)
        revision=0x23090,  # "U-4010.0", the slot's revision (0x47062), eight bytes
        sci_defaults=0x44D88,  # parameter_record_read's (0x4533c)
        log_cleared=0x463DE,  # log_event's LOG_CLEARED (rewritten/z3815a-4010/log.c)
        numeric_params=0x474C4,  # nv_save (0x4701e), 24 (0x47106)
        numeric_count=24,
        setting_params=0x47524,  # nv_save, 19 (0x47180)
        setting_count=19,
        copies=(
            Copy(ram=0x102570, rom=0x44CB8, size=0xCF),  # nv_restore (0x46f66)
            Copy(ram=0x1024A2, rom=0x44BEA, size=0xB2),  # system_init (0x22d74)
            Copy(ram=0x102640, rom=0x44D88, size=SCI_SETTINGS_SIZE),  # the SCI's (0x4533c)
        ),
    ),
    Layout(
        image="z3805a-1.01.03.bin",
        output="z3805a-1.01.03-synthetic-eeprom.bin",
        model=0x34F64,  # "Z3805A", the bad record's model (records.h)
        tag=0x34F6B,  # "S", the identification record's and the slot's tag (records.h)
        revision=0x13048,  # "1.01.03", the slot's revision (records.h)
        sci_defaults=0x3248E,  # sci_settings_defaults, parameter_record_read's
        log_cleared=0x33C7C,  # log_event's LOG_CLEARED (rewritten/z3805a-1.01.03/log.c)
        numeric_params=0x34CC8,  # nv_save (0x34822), 23 (nv.c)
        numeric_count=23,
        setting_params=0x34D24,  # nv_save, 19 (nv.c)
        setting_count=19,
        copies=(
            Copy(ram=0x102938, rom=0x323C2, size=0xCB),  # nv_restore's nv_settings_defaults (0x3475c)
            Copy(ram=0x10285A, rom=0x322E4, size=0xBE),  # system_init's sample_values_defaults (0x12da8)
            Copy(ram=0x102A04, rom=0x3248E, size=SCI_SETTINGS_SIZE),  # the SCI's
        ),
    ),
)


def long_at(image: bytes, address: int) -> int:
    """The big-endian long at an address of the image."""
    return struct.unpack_from(">I", image, address)[0]


def string_at(image: bytes, address: int) -> bytes:
    """The NUL-terminated string at an address of the image."""
    return image[address : image.index(b"\0", address)]


def strncpy(text: bytes, size: int) -> bytes:
    """`size` bytes of `text`, NUL padded, as the images' strncpy."""
    return text[:size].ljust(size, b"\0")


def record_checksum(record: bytearray) -> None:
    """Stores record_checksum's word over the bytes after it at +0."""
    record[CHECKSUM] = checksum_word(record[CHECKSUM.stop :])


def checksum_word(data: bytes) -> bytes:
    """The bytes' sum, a byte, and its complement, a big-endian word."""
    total = sum(data) & BYTE_MASK
    return bytes([total, total ^ BYTE_MASK])


def initial_value(image: bytes, layout: Layout, address: int, size: int) -> bytes:
    """What `size` bytes of RAM at `address` hold when nv_restore saves
    the settings: the copy of the image's that covers them."""
    for copy in layout.copies:
        if copy.ram <= address and address + size <= copy.ram + copy.size:
            offset = copy.rom + address - copy.ram
            return image[offset : offset + size]
    sys.exit(f"{layout.image}: no copy covers 0x{address:x}, {size} bytes")


def descriptor_values(image: bytes, layout: Layout) -> list[tuple[int, int]]:
    """Each value nv_save packs, in its order, with its width in bits:
    the numeric descriptors' longs, then the settings' bytes."""
    tables = (
        (layout.numeric_params, layout.numeric_count, NUMERIC_VALUE, LONG),
        (layout.setting_params, layout.setting_count, SETTING_VALUE, 1),
    )
    values = []
    for table, count, value_offset, value_size in tables:
        for index in range(count):
            descriptor = long_at(image, table + LONG * index)
            last_index = image[descriptor + DESCRIPTOR_LAST_INDEX]
            widths = long_at(image, descriptor + DESCRIPTOR_WIDTHS)
            value = long_at(image, descriptor + value_offset)
            for v in range(last_index + 1):
                width = abs(struct.unpack_from(">b", image, widths + v)[0])
                cell = initial_value(image, layout, value + value_size * v, value_size)
                values.append((int.from_bytes(cell, "big", signed=value_size == 1), width))
    return values


def pack(values: list[tuple[int, int]], size: int) -> bytes:
    """nv_save's packing of the values into `size` bytes of longs, the
    rest 0xff."""
    longs = []
    bits = used = 0
    for value, width in values:
        if used + width > LONG_BITS:
            longs.append(bits << (LONG_BITS - used))
            bits = used = 0
        bits = (bits << width) | (value & ((1 << width) - 1))
        used += width
    if used:
        longs.append(bits << (LONG_BITS - used))
    packed = b"".join(struct.pack(">I", value) for value in longs)
    if len(packed) > size:
        sys.exit("the settings do not fit the NV register")
    return packed.ljust(size, bytes([ERASED]))


def parameter_record(image: bytes, layout: Layout) -> bytearray:
    """save_parameter_record's record of the SCI's defaults."""
    sci = image[layout.sci_defaults : layout.sci_defaults + SCI_SETTINGS_SIZE]
    record = bytearray([ERASED] * PARAMETER_RECORD_SIZE)
    channel_b = (
        bytes([sci[SCI_BAUD], sci[SCI_PARITY], sci[SCI_PACING]])
        + PARAMETER_CONSTANTS
        + bytes([sci[SCI_ECHO]])
    )
    record[PARAMETER_CHANNEL_B : PARAMETER_CHANNEL_B + len(channel_b)] = channel_b
    record[PARAMETER_SCI : PARAMETER_SCI + 4] = sci[SCI_BAUD : SCI_ECHO + 1]
    record_checksum(record)
    return record


def identification_record(image: bytes, layout: Layout) -> bytearray:
    """identification_record_write's record of the image's model, the
    made-up serial number, the suffix A the firmware takes for a bad
    record, and the image's tag."""
    record = bytearray([ERASED] * IDENTIFICATION_RECORD_SIZE)
    for offset, text in (
        (IDENTIFICATION_MODEL, string_at(image, layout.model)),
        (IDENTIFICATION_SERIAL, SERIAL_NUMBER),
    ):
        field = slice(offset, offset + IDENTIFICATION_FIELD_SIZE)
        record[field] = strncpy(text, IDENTIFICATION_FIELD_SIZE)
    record[IDENTIFICATION_SUFFIX] = ord("A")
    record[IDENTIFICATION_TAG : IDENTIFICATION_TAG + IDENTIFICATION_TAG_SIZE] = strncpy(
        string_at(image, layout.tag), IDENTIFICATION_TAG_SIZE
    )
    record_checksum(record)
    return record


def nv_record(image: bytes, layout: Layout) -> bytearray:
    """nv_save's record of the settings at power-up, as its first save
    to an erased slot writes it."""
    record = bytearray(NV_RECORD_SIZE)
    record[:NV_TAG_SIZE] = strncpy(string_at(image, layout.tag), NV_TAG_SIZE)
    record[NV_SEQUENCE] = struct.pack(">I", NV_FIRST_SEQUENCE)
    revision = string_at(image, layout.revision)
    record[NV_REVISION] = strncpy(revision, NV_REVISION.stop - NV_REVISION.start)
    record[NV_PACKED:] = pack(descriptor_values(image, layout), NV_RECORD_SIZE - NV_PACKED)
    record[NV_CHECKSUM] = checksum_word(record[NV_CHECKSUMMED:])
    return record


def log_pages(image: bytes, layout: Layout) -> bytearray:
    """log_erase's pages: each cleared and written, then page 0 written
    again with its "Log cleared" entry."""
    pages = bytearray()
    for page in range((EEPROM_SIZE - (LOG_PAGES - EEPROM_BASE)) // LOG_PAGE_SIZE):
        record = bytearray(LOG_PAGE_SIZE)
        record[LOG_WRITE_COUNT] = struct.pack(">H", FIRST_WRITE + (page == 0))
        if page == 0:
            entry = bytearray(record[LOG_ENTRIES:])
            entry[LOG_ENTRY_TIME] = struct.pack(">I", POWER_UP_TIME)
            entry[LOG_ENTRY_DATE] = struct.pack(">I", POWER_UP_DATE)
            entry[LOG_ENTRY_FORMAT] = struct.pack(">I", layout.log_cleared)
            record[LOG_ENTRIES:] = entry
        record[LOG_CHECKSUM] = checksum_word(record[LOG_ENTRIES:])
        pages += record
    return pages


def reset_record(image: bytes, layout: Layout) -> bytearray:
    """log_clear's reset record, "Log cleared", the first."""
    record = bytearray(RESET_RECORD_SIZE)
    record[RESET_SEQUENCE] = struct.pack(">H", FIRST_WRITE)
    record[RESET_TIME] = struct.pack(">I", POWER_UP_TIME)
    record[RESET_DATE] = struct.pack(">I", POWER_UP_DATE)
    record[RESET_TEXT] = strncpy(string_at(image, layout.log_cleared), RESET_TEXT.stop - RESET_TEXT.start)
    record[RESET_CHECKSUM] = checksum_word(record[RESET_CHECKSUM.stop :])
    return record


def build(layout: Layout) -> bytes:
    """The synthetic EEPROM for one image."""
    image = (IMAGES / layout.image).read_bytes()
    eeprom = bytearray([ERASED] * EEPROM_SIZE)
    for address, record in (
        (PARAMETER_RECORD, parameter_record(image, layout)),
        (IDENTIFICATION_RECORD, identification_record(image, layout)),
        (NV_SLOT_0, nv_record(image, layout)),
        (RESET_RECORDS, reset_record(image, layout)),
        (LOG_PAGES, log_pages(image, layout)),
    ):
        offset = address - EEPROM_BASE
        eeprom[offset : offset + len(record)] = record
    return bytes(eeprom)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--check", action="store_true", help="fail if a checked-in image differs")
    arguments = parser.parse_args()
    failed = False
    for layout in LAYOUTS:
        path = IMAGES / "eeprom" / layout.output
        eeprom = build(layout)
        if not arguments.check:
            path.write_bytes(eeprom)
        elif not path.exists() or path.read_bytes() != eeprom:
            print(f"{path.relative_to(ROOT)} is not what {Path(__file__).name} builds")
            failed = True
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
