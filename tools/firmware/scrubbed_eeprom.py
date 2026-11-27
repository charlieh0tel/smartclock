"""The 58503B's EEPROM image, third_party/firmware/images/eeprom/
58503b-L00000-eeprom.bin: a 58503B's EEPROM, read with pForth, its
serial number replaced by L00000, a serial's shape, a letter and five
digits, that no unit has, and its stored position by the one the
emulator's synthetic engine places its antenna at by default, HP's
Santa Clara Division (docs/emulator.md, "The synthetic GPS engine";
docs/firmware/gps.md, "Requests"), every other byte as read.

The layout is synthetic_eeprom.py's, the 58503A 3704's
(docs/firmware/structures.md, "EEPROM records").  The serial number
is the identification record's at +0x14, NUL padded; the new one is
as long as the dump's, so the padding stays as read, and the record's
checksum is made again.  The dump holds it nowhere else.  The position is
:PTIMe:GPSystem:POSition's descriptor's three values (0x4438a, the
seventh of nv_save's numeric table 0x4308a; its setter 0x3c76a reads
it at 0x3c784, its query is 0x3cbae): latitude and longitude, 32 bits
each, milliarcseconds, north and east positive, and the height, 24
bits signed, centimeters, -1000 m to 18000 m (its limits 0x4435a and
0x44366).  The 58503B holds and commands the GPS-ellipsoid height
(gps.md, "The 58503B's height datum"), so the height written is the
MSL height plus the synthetic engine's geoid separation, the
ellipsoid height the engine reports (smartclock-emu's gps/sky.rs,
GEOID_SEPARATION).
A slot that holds a position gets it and its checksum is made again;
one whose position is the defaults' zeros, which place nothing, is
left as read: on this unit slot 0, invalidated.  Nothing else in the part holds a position or
the serial number: the parameter, identification, lifetime and
calibration records, the reset records and the event log's formats
and arguments were read for them (docs/emulator.md, "The EEPROM").

The dump is not in the repository, so the image cannot be rebuilt
from it: `python3 -I scrubbed_eeprom.py DUMP` writes the image from a
dump of the part's 8 KiB; `--check` checks the checked-in image: every
checksummed record good, the serial number L00000, the identification
record's and the live slot's tags, both slots' revision, slot 0's
erase mark, slot 1's position the one above, and slot 0 the synthetic
image's slot, its position the defaults' zeros, but for its first byte
and sequence number.
"""

import argparse
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import synthetic_eeprom  # noqa: E402
from synthetic_eeprom import (  # noqa: E402
    CHECKSUM,
    EEPROM_BASE,
    ERASED,
    EEPROM_SIZE,
    IDENTIFICATION_RECORD,
    IDENTIFICATION_FIELD_SIZE,
    IDENTIFICATION_RECORD_SIZE,
    IDENTIFICATION_SERIAL,
    IDENTIFICATION_TAG,
    IDENTIFICATION_TAG_SIZE,
    IMAGES,
    LOG_CHECKSUM,
    LOG_PAGE_SIZE,
    LOG_PAGES,
    LONG,
    LONG_BITS,
    NV_CHECKSUM,
    NV_CHECKSUMMED,
    NV_PACKED,
    NV_RECORD_SIZE,
    NV_REVISION,
    NV_SEQUENCE,
    NV_SLOT_0,
    NV_TAG_SIZE,
    PARAMETER_RECORD,
    PARAMETER_RECORD_SIZE,
    RESET_CHECKSUM,
    RESET_RECORD_COUNT,
    RESET_RECORD_SIZE,
    RESET_RECORDS,
    ROOT,
    checksum_word,
    descriptor_values,
    long_at,
    string_at,
    strncpy,
)

LAYOUT = synthetic_eeprom.LAYOUTS[0]
OUTPUT = IMAGES / "eeprom" / "58503b-L00000-eeprom.bin"
SYNTHETIC = IMAGES / "eeprom" / LAYOUT.output

# The NV register's two slots, slot 1 right after slot 0.
NV_SLOTS = (NV_SLOT_0, NV_SLOT_0 + NV_RECORD_SIZE)
# The 58503B's slot erase: 0xf0 with a setting's value in the slot's
# first byte (structures.md, "EEPROM records"; 0x42a44).
ERASE_MARK = 0xF0
ERASE_MARK_MASK = 0xF0

# The other records record_checksum keeps, each (address, size): the
# lifetime record and the calibration block (structures.md).
LIFETIME_RECORD = (0x400060, 0x20)
CALIBRATION_BLOCK = (0x400080, 0x40)

# The serial number the image holds: an HP serial's shape, as the 58503B's
# are, a letter and five digits, all zeros, which no unit's is.
SERIAL_NUMBER = b"L00000"
SERIAL = slice(IDENTIFICATION_SERIAL, IDENTIFICATION_SERIAL + IDENTIFICATION_FIELD_SIZE)

# :PTIMe:GPSystem:POSition's descriptor, nv_save's numeric table's
# seventh (0x430a2), the setter's (0x3c784).
POSITION_DESCRIPTOR = 0x4438A
# HP's Santa Clara Division, as master_reset's @@Ad, @@Ae and @@Af give
# it (gps.md, "Requests"): milliarcseconds, and centimeters above mean
# sea level.
LATITUDE = 134_372_100
LONGITUDE = -439_192_200
HEIGHT_MSL = 5_500
# The synthetic engine's geoid separation, centimeters: an ellipsoid
# height is the MSL height plus it (gps/sky.rs, GEOID_SEPARATION).
GEOID_SEPARATION = -2_841
POSITION = (LATITUDE, LONGITUDE, HEIGHT_MSL + GEOID_SEPARATION)


def eeprom_offset(address: int) -> int:
    """An EEPROM address's offset in the part."""
    return address - EEPROM_BASE


def value_bits(image: bytes) -> list[tuple[int, int]]:
    """Each value nv_save packs, in its order, as (bit offset from the
    slot's +0x10, width): its packing, a value that would cross a long's
    end starting the next (synthetic_eeprom.py's pack)."""
    places = []
    long_index = used = 0
    for _, width in descriptor_values(image, LAYOUT):
        if used + width > LONG_BITS:
            long_index += 1
            used = 0
        places.append((long_index * LONG_BITS + used, width))
        used += width
    return places


def position_bits(image: bytes) -> list[tuple[int, int]]:
    """The position's three values' places, as value_bits gives them."""
    index = 0
    for table_index in range(LAYOUT.numeric_count):
        descriptor = long_at(image, LAYOUT.numeric_params + LONG * table_index)
        count = image[descriptor + synthetic_eeprom.DESCRIPTOR_LAST_INDEX] + 1
        if descriptor == POSITION_DESCRIPTOR:
            return value_bits(image)[index : index + count]
        index += count
    sys.exit(f"0x{POSITION_DESCRIPTOR:x} is not in nv_save's numeric table")


def packed_field(slot: bytes | bytearray, place: tuple[int, int]) -> int:
    """The signed value at `place` in a slot's packed values."""
    offset, width = place
    packed = int.from_bytes(slot[NV_PACKED:], "big")
    value = packed >> ((len(slot) - NV_PACKED) * 8 - offset - width) & ((1 << width) - 1)
    return value - (1 << width) if value >> (width - 1) else value


def set_packed_field(slot: bytearray, place: tuple[int, int], value: int) -> None:
    """Stores `value` at `place` in a slot's packed values."""
    offset, width = place
    bits = (len(slot) - NV_PACKED) * 8
    shift = bits - offset - width
    packed = int.from_bytes(slot[NV_PACKED:], "big")
    packed &= ~(((1 << width) - 1) << shift)
    packed |= (value & ((1 << width) - 1)) << shift
    slot[NV_PACKED:] = packed.to_bytes(bits // 8, "big")


def checksummed_records() -> list[tuple[int, int, slice, int]]:
    """Every checksummed record, as (offset, size, checksum, first byte
    summed); the erased ones are left out by the caller."""
    records = [
        (eeprom_offset(PARAMETER_RECORD), PARAMETER_RECORD_SIZE, CHECKSUM, CHECKSUM.stop),
        (eeprom_offset(IDENTIFICATION_RECORD), IDENTIFICATION_RECORD_SIZE, CHECKSUM, CHECKSUM.stop),
        (eeprom_offset(LIFETIME_RECORD[0]), LIFETIME_RECORD[1], CHECKSUM, CHECKSUM.stop),
        (eeprom_offset(CALIBRATION_BLOCK[0]), CALIBRATION_BLOCK[1], CHECKSUM, CHECKSUM.stop),
    ]
    records += [(eeprom_offset(slot), NV_RECORD_SIZE, NV_CHECKSUM, NV_CHECKSUMMED) for slot in NV_SLOTS]
    records += [
        (eeprom_offset(RESET_RECORDS) + RESET_RECORD_SIZE * index, RESET_RECORD_SIZE, RESET_CHECKSUM, RESET_CHECKSUM.stop)
        for index in range(RESET_RECORD_COUNT)
    ]
    records += [
        (offset, LOG_PAGE_SIZE, LOG_CHECKSUM, LOG_CHECKSUM.stop)
        for offset in range(eeprom_offset(LOG_PAGES), EEPROM_SIZE, LOG_PAGE_SIZE)
    ]
    return records


def bad_checksums(eeprom: bytes) -> list[str]:
    """The checksummed records whose checksum fails, erased ones aside."""
    bad = []
    for offset, size, checksum, summed in checksummed_records():
        record = eeprom[offset : offset + size]
        if set(record) == {ERASED}:
            continue
        if record[checksum] != checksum_word(record[summed:]):
            bad.append(f"0x{EEPROM_BASE + offset:x}")
    return bad


def serial_number(eeprom: bytes | bytearray) -> bytes:
    """The identification record's serial number, its padding left out."""
    offset = eeprom_offset(IDENTIFICATION_RECORD)
    return bytes(eeprom[offset:][SERIAL]).split(b"\0")[0]


def scrub(dump: bytes, image: bytes) -> bytes:
    """The dump with the serial number L00000, each slot's position that
    is not the defaults' zeros HP's, and the checksums made again; fails if a record of the dump
    is bad or its serial number is not as long as L00000."""
    if len(dump) != EEPROM_SIZE:
        sys.exit(f"a dump is {EEPROM_SIZE} bytes, not {len(dump)}")
    if bad := bad_checksums(dump):
        sys.exit(f"the dump's records at {', '.join(bad)} fail their checksums")
    eeprom = bytearray(dump)
    serial = serial_number(dump)
    if len(serial) != len(SERIAL_NUMBER):
        sys.exit(f"the dump's serial number is {len(serial)} bytes, not {len(SERIAL_NUMBER)}")
    if dump.count(serial) != 1:
        sys.exit("the dump holds its serial number outside the identification record")
    offset = eeprom_offset(IDENTIFICATION_RECORD)
    record = bytearray(eeprom[offset : offset + IDENTIFICATION_RECORD_SIZE])
    record[SERIAL.start : SERIAL.start + len(SERIAL_NUMBER)] = SERIAL_NUMBER
    record[CHECKSUM] = checksum_word(record[CHECKSUM.stop :])
    eeprom[offset : offset + IDENTIFICATION_RECORD_SIZE] = record
    places = position_bits(image)
    for slot_address in NV_SLOTS:
        offset = eeprom_offset(slot_address)
        slot = bytearray(eeprom[offset : offset + NV_RECORD_SIZE])
        if not any(packed_field(slot, place) for place in places):
            continue
        for place, value in zip(places, POSITION, strict=True):
            set_packed_field(slot, place, value)
        slot[NV_CHECKSUM] = checksum_word(slot[NV_CHECKSUMMED:])
        eeprom[offset : offset + NV_RECORD_SIZE] = slot
    return bytes(eeprom)


def problems(eeprom: bytes, image: bytes, synthetic: bytes) -> list[str]:
    """What is wrong with a scrubbed image, nothing if it is good."""
    found = [f"record {address} fails its checksum" for address in bad_checksums(eeprom)]
    identification = eeprom[eeprom_offset(IDENTIFICATION_RECORD) :]
    if serial_number(eeprom) != SERIAL_NUMBER:
        found.append(f"the serial number is not {SERIAL_NUMBER.decode()}")
    tag = strncpy(string_at(image, LAYOUT.tag), IDENTIFICATION_TAG_SIZE)
    if identification[IDENTIFICATION_TAG : IDENTIFICATION_TAG + IDENTIFICATION_TAG_SIZE] != tag:
        found.append("the identification record's tag is not the image's")
    revision = strncpy(string_at(image, LAYOUT.revision), NV_REVISION.stop - NV_REVISION.start)
    places = position_bits(image)
    slots = [eeprom[eeprom_offset(slot) : eeprom_offset(slot) + NV_RECORD_SIZE] for slot in NV_SLOTS]
    if slots[1][:NV_TAG_SIZE] != tag[:NV_TAG_SIZE]:
        found.append("slot 1's tag is not the image's")
    if slots[0][0] & ERASE_MARK_MASK != ERASE_MARK:
        found.append("slot 0 is not erase marked")
    for number, slot in enumerate(slots):
        if slot[NV_REVISION] != revision:
            found.append(f"slot {number}'s revision is not the image's")
    if tuple(packed_field(slots[1], place) for place in places) != POSITION:
        found.append("slot 1's position is not HP Santa Clara Division's")
    # slot 0 holds the defaults: the synthetic image's slot but for the
    # bytes the erase mark and the sequence number change, which the
    # checksum does not cover
    offset = eeprom_offset(NV_SLOT_0)
    expected = bytearray(synthetic[offset : offset + NV_RECORD_SIZE])
    expected[0] = slots[0][0]
    expected[NV_SEQUENCE] = slots[0][NV_SEQUENCE]
    if slots[0] != expected:
        found.append("slot 0 is not the synthetic image's defaults")
    return found


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("dump", nargs="?", type=Path, help="the part's 8 KiB, read from 0x400000")
    parser.add_argument("--check", action="store_true", help="check the checked-in image")
    arguments = parser.parse_args()
    image = (IMAGES / LAYOUT.image).read_bytes()
    if arguments.check:
        found = problems(OUTPUT.read_bytes(), image, SYNTHETIC.read_bytes())
        for problem in found:
            print(f"{OUTPUT.relative_to(ROOT)}: {problem}")
        return 1 if found else 0
    if arguments.dump is None:
        parser.error("a dump, or --check")
    OUTPUT.write_bytes(scrub(arguments.dump.read_bytes(), image))
    return 0


if __name__ == "__main__":
    sys.exit(main())
