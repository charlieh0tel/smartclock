//! The 58503B's EEPROM image in `third_party/firmware/images/eeprom/`
//! has the layout `docs/firmware/58503b-1.01.04.md` ("A unit's
//! EEPROM") says it confirms: the records where and as long as
//! `docs/firmware/structures.md` gives them, every checksum good, and
//! the serial number and the position its replacements.

use std::fs;
use std::path::PathBuf;

/// The EEPROM image, 8 KB from `0x400000`; offsets below are from its
/// start.
const EEPROM: &str = "eeprom/58503b-L00000-eeprom.bin";

/// The firmware whose EEPROM it is: the event log holds addresses of
/// its texts.
const FIRMWARE: &str = "58503b-1.01.04.bin";

/// The EEPROM's size, chip select 9's window.
const EEPROM_LENGTH: usize = 0x2000;

/// An erased byte.
const ERASED: u8 = 0xff;

/// The parameter, identification, lifetime and calibration records:
/// each one's offset and length, its checksum `record_checksum`'s, in
/// its first two bytes over the rest.
const RECORDS: [(usize, usize); 4] = [(0x00, 16), (0x10, 48), (0x60, 32), (0x80, 64)];

/// A checksum's length, the sum and its complement.
const CHECKSUM_LENGTH: usize = 2;

/// The identification record's model, serial number, suffix and tag:
/// each one's offset in the image and its text, NULs after it.
const IDENTIFICATION: [(usize, &str); 4] = [
    (0x12, "58503B\0"),
    (0x24, "L00000\0"),
    (0x36, "D"),
    (0x37, "CQB"),
];

/// The two NV register records.
const NV_RECORDS: [usize; 2] = [0xc0, 0x160];

/// An NV register record's length.
const NV_LENGTH: usize = 0xa0;

/// Where in an NV register record its checksum is, and the first byte
/// the checksum covers, the revision's.
const NV_CHECKSUM: usize = 0x02;
const NV_REVISION: usize = 0x08;

/// The revision an NV register record carries.
const NV_REVISION_TEXT: &[u8; 8] = b"1.01.04\0";

/// The erased NV slot's tag and the live one's: `nv_slot_erase`'s
/// byte, the high four bits set, over the "C" of "CQ".
const NV_TAGS: [[u8; 2]; 2] = [[0xf1, b'Q'], *b"CQ"];

/// Where in an NV register record the position is, three longs:
/// latitude and longitude in milliarcseconds, then the height in
/// centimeters in the top 24 bits.
const NV_POSITION: usize = 0x20;

/// The positions the two slots hold: none, and the one
/// `docs/firmware/gps.md` gives for `master_reset`, 37.3255833 N,
/// 121.9978333 W, with the height that replaced the unit's.
const NV_POSITIONS: [[i32; 3]; 2] = [[0, 0, 0], [134_372_100, -439_192_200, 2659]];

/// Bits of the height's long below the height.
const NV_HEIGHT_SHIFT: u32 = 8;

/// The first of an NV register record's bytes that no setting reaches,
/// erased from there to its end.
const NV_UNUSED: usize = 0x68;

/// The reset-cause records: where they start, how many, and each
/// one's length.
const RESET_RECORDS: usize = 0x200;
const RESET_RECORD_COUNT: usize = 32;
const RESET_RECORD_LENGTH: usize = 0x5c;

/// Where in a reset-cause record its checksum is, and the first byte
/// the checksum covers, the time's.
const RESET_CHECKSUM: usize = 0x02;
const RESET_TIME: usize = 0x04;

/// The reset-cause records that are written; the rest are erased.
const RESET_RECORDS_WRITTEN: usize = 2;

/// The event log's pages: where they start, how many, and each one's
/// length, a write count, a checksum and three entries.
const LOG_PAGES: usize = 0xd80;
const LOG_PAGE_COUNT: usize = 74;
const LOG_PAGE_LENGTH: usize = 0x40;

/// Where in a log page its checksum is, and its first entry.
const LOG_CHECKSUM: usize = 0x02;
const LOG_ENTRIES: usize = 0x04;

/// A log entry's length, and where in it the format's address is.
const LOG_ENTRY_LENGTH: usize = 20;
const LOG_ENTRY_FORMAT: usize = 8;

fn image(name: &str) -> Vec<u8> {
    let path: PathBuf = [
        env!("CARGO_MANIFEST_DIR"),
        "../../third_party/firmware/images",
        name,
    ]
    .iter()
    .collect();
    fs::read(&path).unwrap_or_else(|e| panic!("{}: {e}", path.display()))
}

/// A big-endian long at `offset`.
fn long(data: &[u8], offset: usize) -> u32 {
    u32::from_be_bytes(
        data[offset..offset + 4]
            .try_into()
            .expect("four bytes of the image"),
    )
}

/// The records' checksum: the byte sum of `data` and its complement.
fn sum_and_complement(data: &[u8]) -> [u8; CHECKSUM_LENGTH] {
    let sum = data.iter().fold(0u8, |sum, byte| sum.wrapping_add(*byte));
    [sum, !sum]
}

/// Whether `rom` holds printable text at `address`, NUL-terminated.
fn is_text(rom: &[u8], address: usize) -> bool {
    rom.get(address..).is_some_and(|rest| {
        let length = rest.iter().take_while(|byte| **byte != 0).count();
        length > 0
            && length < rest.len()
            && rest[..length]
                .iter()
                .all(|byte| byte.is_ascii_graphic() || *byte == b' ')
    })
}

#[test]
fn the_image_is_the_whole_eeprom() {
    assert_eq!(image(EEPROM).len(), EEPROM_LENGTH);
}

#[test]
fn the_four_records_checksums_hold() {
    let eeprom = image(EEPROM);
    for (offset, length) in RECORDS {
        let record = &eeprom[offset..offset + length];
        assert_eq!(
            record[..CHECKSUM_LENGTH],
            sum_and_complement(&record[CHECKSUM_LENGTH..]),
            "record at {offset:#x}"
        );
    }
}

#[test]
fn the_identification_record_holds_the_replacement_serial() {
    let eeprom = image(EEPROM);
    for (offset, text) in IDENTIFICATION {
        assert_eq!(
            &eeprom[offset..offset + text.len()],
            text.as_bytes(),
            "text at {offset:#x}"
        );
    }
}

#[test]
fn the_nv_records_are_good_and_hold_only_the_replacement_position() {
    let eeprom = image(EEPROM);
    for ((offset, tag), position) in NV_RECORDS.into_iter().zip(NV_TAGS).zip(NV_POSITIONS) {
        let record = &eeprom[offset..offset + NV_LENGTH];
        assert_eq!(record[..tag.len()], tag, "tag at {offset:#x}");
        assert_eq!(
            record[NV_CHECKSUM..NV_CHECKSUM + CHECKSUM_LENGTH],
            sum_and_complement(&record[NV_REVISION..]),
            "record at {offset:#x}"
        );
        assert_eq!(
            &record[NV_REVISION..NV_REVISION + NV_REVISION_TEXT.len()],
            NV_REVISION_TEXT,
            "revision at {offset:#x}"
        );
        let [latitude, longitude, height] =
            [0, 4, 8].map(|field| long(record, NV_POSITION + field).cast_signed());
        assert_eq!(
            [latitude, longitude, height >> NV_HEIGHT_SHIFT],
            position,
            "position at {offset:#x}"
        );
        assert!(
            record[NV_UNUSED..].iter().all(|byte| *byte == ERASED),
            "record at {offset:#x} from {NV_UNUSED:#x}"
        );
    }
}

#[test]
fn the_reset_records_are_good_or_erased() {
    let eeprom = image(EEPROM);
    for index in 0..RESET_RECORD_COUNT {
        let offset = RESET_RECORDS + index * RESET_RECORD_LENGTH;
        let record = &eeprom[offset..offset + RESET_RECORD_LENGTH];
        if index < RESET_RECORDS_WRITTEN {
            assert_eq!(
                record[RESET_CHECKSUM..RESET_CHECKSUM + CHECKSUM_LENGTH],
                sum_and_complement(&record[RESET_TIME..]),
                "record at {offset:#x}"
            );
        } else {
            assert!(
                record.iter().all(|byte| *byte == ERASED),
                "record at {offset:#x}"
            );
        }
    }
    assert_eq!(
        RESET_RECORDS + RESET_RECORD_COUNT * RESET_RECORD_LENGTH,
        LOG_PAGES
    );
}

#[test]
fn the_log_pages_are_good_and_name_the_firmwares_texts() {
    let eeprom = image(EEPROM);
    let rom = image(FIRMWARE);
    assert_eq!(LOG_PAGES + LOG_PAGE_COUNT * LOG_PAGE_LENGTH, EEPROM_LENGTH);
    for index in 0..LOG_PAGE_COUNT {
        let offset = LOG_PAGES + index * LOG_PAGE_LENGTH;
        let page = &eeprom[offset..offset + LOG_PAGE_LENGTH];
        assert_eq!(
            page[LOG_CHECKSUM..LOG_CHECKSUM + CHECKSUM_LENGTH],
            sum_and_complement(&page[LOG_ENTRIES..]),
            "page at {offset:#x}"
        );
        for entry in page[LOG_ENTRIES..].as_chunks::<LOG_ENTRY_LENGTH>().0 {
            let format = usize::try_from(long(entry, LOG_ENTRY_FORMAT)).expect("an address");
            assert!(
                is_text(&rom, format),
                "page at {offset:#x}: format {format:#x}"
            );
        }
    }
}

#[test]
fn nothing_else_is_written() {
    let eeprom = image(EEPROM);
    let written: Vec<(usize, usize)> = RECORDS
        .into_iter()
        .chain(NV_RECORDS.map(|offset| (offset, NV_LENGTH)))
        .chain([(RESET_RECORDS, EEPROM_LENGTH - RESET_RECORDS)])
        .collect();
    for (at, byte) in eeprom.iter().enumerate() {
        let in_a_record = written
            .iter()
            .any(|(offset, length)| (*offset..offset + length).contains(&at));
        assert!(in_a_record || *byte == ERASED, "byte at {at:#x}");
    }
}
