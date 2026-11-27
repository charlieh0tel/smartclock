//! The EEPROM's lifetime record as the firmware reads it at power-up:
//! the EFC the loop last locked at, from which `pll_init` starts the
//! loop (structures.md, "The lifetime record").  Every image the
//! emulator runs reads it with the same `lifetime_record_read`, at the
//! same place (`rewritten/common/records.c`; each image's `names.tsv`,
//! `eeprom_lifetime_record` at `0x400060`), into the same layout
//! (`rewritten/common/types/identification_record.h`, `lifetime_record`).

use crate::board::eeprom::ERASED;

/// The record's place in the part, `0x400060`, and its length.
const RECORD: usize = 0x60;
const LENGTH: usize = 32;
/// Its checksum word, the first two bytes, which covers the rest:
/// their sum in its high byte, the sum's complement in its low
/// (common/records.h, `records_checksum`).
const CHECKSUM: usize = 2;
/// Its EFC, a big-endian float, counts, at `+0xa`.
const EFC: usize = 0xa;
/// An EFC with these bits all set, as an erased record's has, the
/// firmware takes as the middle (`LIFETIME_EFC_INVALID_BITS`).
const INVALID_BITS: u32 = 0xe800_0000;
/// The EFC's middle, which the firmware takes for a record that fails
/// either test (`PLL_EFC_MID_SCALE`, common/pll.h), and its range, to
/// which `pll_init` clamps it (`efc_clamp`, `PLL_EFC_MOST`), counts.
const MIDDLE: f32 = 524_288.0;
const MOST: f32 = 1_048_560.0;

/// The EFC the firmware starts its loop from on an EEPROM of `cells`,
/// counts: the lifetime record's, clamped as `pll_init` clamps it, or
/// the middle where the checksum fails or the EFC is marked invalid,
/// as on an erased part, or is not a number.  Cells past the end of `cells` read erased, as
/// the part pads a short image.
pub(crate) fn start_efc(cells: &[u8]) -> f64 {
    let mut record = [ERASED; LENGTH];
    if let Some(found) = cells.get(RECORD..) {
        let length = found.len().min(LENGTH);
        record[..length].copy_from_slice(&found[..length]);
    }
    let sum = record[CHECKSUM..]
        .iter()
        .fold(0u8, |sum, byte| sum.wrapping_add(*byte));
    let checksum = u16::from_be_bytes([sum, !sum]);
    let bits = u32::from_be_bytes(record[EFC..EFC + 4].try_into().expect("four bytes"));
    let efc = if u16::from_be_bytes([record[0], record[1]]) != checksum
        || bits & INVALID_BITS == INVALID_BITS
    {
        MIDDLE
    } else {
        f32::from_bits(bits)
    };
    // efc_clamp's range; a NaN, which it passes, would leave the
    // oscillator no frequency, so it is taken as the middle
    f64::from(if efc.is_nan() {
        MIDDLE
    } else {
        efc.clamp(0.0, MOST)
    })
}

#[cfg(test)]
mod tests {
    use super::CHECKSUM;
    use super::EFC;
    use super::LENGTH;
    use super::MIDDLE;
    use super::MOST;
    use super::RECORD;
    use super::start_efc;
    use crate::board::eeprom::ERASED;
    use crate::board::eeprom::SIZE;

    /// A part erased but for a lifetime record holding the EFC of bits
    /// `bits`, its checksum right.
    fn holding(bits: u32) -> Vec<u8> {
        let mut cells = vec![ERASED; SIZE as usize];
        let record = &mut cells[RECORD..RECORD + LENGTH];
        record[EFC..EFC + 4].copy_from_slice(&bits.to_be_bytes());
        let sum = record[CHECKSUM..]
            .iter()
            .fold(0u8, |sum, byte| sum.wrapping_add(*byte));
        record[..CHECKSUM].copy_from_slice(&[sum, !sum]);
        cells
    }

    #[test]
    fn a_record_gives_its_efc() {
        // the Z3805A bench unit's dump's
        assert_eq!(start_efc(&holding(0x4936_e8f9)), 749_199.562_5);
    }

    #[test]
    fn an_erased_part_a_short_one_or_none_gives_the_middle() {
        for cells in [vec![ERASED; SIZE as usize], vec![0x5a; RECORD + 4], vec![]] {
            assert_eq!(start_efc(&cells), f64::from(MIDDLE));
        }
    }

    #[test]
    fn a_record_whose_checksum_fails_gives_the_middle() {
        let mut cells = holding(0x4936_e8f9);
        cells[RECORD + EFC] ^= 1;
        assert_eq!(start_efc(&cells), f64::from(MIDDLE));
    }

    #[test]
    fn an_efc_marked_invalid_gives_the_middle() {
        assert_eq!(start_efc(&holding(0xe800_0000)), f64::from(MIDDLE));
    }

    #[test]
    fn an_efc_not_a_number_gives_the_middle() {
        assert_eq!(start_efc(&holding(f32::NAN.to_bits())), f64::from(MIDDLE));
    }

    #[test]
    fn an_efc_out_of_range_is_clamped() {
        assert_eq!(start_efc(&holding((-1.0f32).to_bits())), 0.0);
        assert_eq!(start_efc(&holding(2e6f32.to_bits())), f64::from(MOST));
    }
}
