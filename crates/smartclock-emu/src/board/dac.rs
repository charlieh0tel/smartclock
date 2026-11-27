//! The DACs on the QSPI: the hex DAC on chip select 2, three 12-bit
//! words, and the EFC's AD569 on chip select 7, one 16-bit word
//! (structures.md, "The QSPI's devices"; efc.md).  Each shifts back
//! what it held as the next word goes in, which the self-test reads
//! back (selftest.c): the hex DAC its 36 bits a bit early, so that
//! `qspi_hex_dac_test` shifts them right one to compare
//! (`shift_right_36`), the EFC DAC its word as it was.
//!
//! The QSM gives the hex DAC each word's place in its queue, on the
//! Unicorn board and on mc683xx's QSPI (`QspiTransfer::entry`):
//! `qspi_queue_words` (qspi.c) puts a frame's words in entries 0 to 2.
//! What the DACs hold, their outputs, the board reads with `channel`
//! and `word`.

/// The hex DAC's words, and their bits.
const WORDS: usize = 3;
const WORD_BITS: u32 = 12;
const WORD_MASK: u64 = 0xfff;
const ALL_BITS: u64 = (1 << (WORD_BITS * WORDS as u32)) - 1;
/// Its six channels' bits, two to a word.
const CHANNEL_BITS: u64 = 6;
const CHANNEL_MASK: u64 = 0x3f;

/// The hex DAC: the 36 bits it holds, and those coming in.
#[derive(Debug, Default)]
pub(crate) struct HexDac {
    /// What the last whole transfer left in it.
    held: u64,
    /// The transfer coming in, word by word.
    incoming: u64,
}

impl HexDac {
    /// The `entry`th word of a transfer: what comes back for it.
    pub(crate) fn exchange(&mut self, entry: usize, word: u16) -> u16 {
        let shift = WORD_BITS * (WORDS - 1 - entry.min(WORDS - 1)) as u32;
        let early = (self.held << 1) & ALL_BITS;
        self.incoming =
            self.incoming & !(WORD_MASK << shift) | (u64::from(word) & WORD_MASK) << shift;
        if entry + 1 >= WORDS {
            self.held = self.incoming;
        }
        // twelve bits fit a word
        (early >> shift & WORD_MASK) as u16
    }

    /// Slot `slot`'s six bits as `hdac_set` (dac.c) places them: in
    /// word `slot / 2`, the high half for an even slot, the low for an
    /// odd.
    pub(crate) fn channel(&self, slot: usize) -> u8 {
        let word = self.held >> (WORD_BITS * (WORDS - 1 - slot / 2) as u32) & WORD_MASK;
        let half = if slot.is_multiple_of(2) {
            word >> CHANNEL_BITS
        } else {
            word
        };
        // six bits fit a byte
        (half & CHANNEL_MASK) as u8
    }
}

/// The EFC's DAC: the word it holds.
#[derive(Debug, Default)]
pub(crate) struct EfcDac {
    /// The last word written.
    held: u16,
}

impl EfcDac {
    /// A word written: what it held comes back.
    pub(crate) fn exchange(&mut self, word: u16) -> u16 {
        std::mem::replace(&mut self.held, word)
    }

    /// The word it holds, which its strobe loads into its output.
    pub(crate) fn word(&self) -> u16 {
        self.held
    }
}

#[cfg(test)]
mod tests {
    use super::EfcDac;
    use super::HexDac;

    /// `shift_right_36` (selftest.c): the three words right one bit as
    /// one 36-bit number.
    fn shifted_right(words: [u16; 3]) -> [u16; 3] {
        let all = words
            .iter()
            .fold(0u64, |all, word| all << 12 | u64::from(word & 0xfff))
            >> 1;
        [
            (all >> 24) as u16 & 0xfff,
            (all >> 12) as u16 & 0xfff,
            all as u16 & 0xfff,
        ]
    }

    #[test]
    fn the_hex_dac_reads_back_as_its_self_test_wants() {
        let mut dac = HexDac::default();
        for pattern in [[0x555, 0x123, 0x456], [0x2aa, 0xfed, 0xcba]] {
            for (entry, word) in pattern.iter().enumerate() {
                dac.exchange(entry, *word);
            }
            let mut back = [0; 3];
            for (entry, word) in pattern.iter().enumerate() {
                back[entry] = dac.exchange(entry, *word);
            }
            assert_eq!(shifted_right(back), pattern);
        }
    }

    #[test]
    fn a_slot_reads_as_hdac_set_placed_it() {
        let mut dac = HexDac::default();
        // hdac_shadow with slot 2 at 0x2a and slot 3 at 0x15
        for (entry, word) in [0, 0x2a << 6 | 0x15, 0].into_iter().enumerate() {
            dac.exchange(entry, word);
        }
        assert_eq!(dac.channel(2), 0x2a);
        assert_eq!(dac.channel(3), 0x15);
        assert_eq!(dac.channel(0), 0);
    }

    #[test]
    fn the_efc_dac_gives_back_what_it_held() {
        let mut dac = EfcDac::default();
        assert_eq!(dac.exchange(0x5555), 0);
        assert_eq!(dac.exchange(0x5555), 0x5555);
    }
}
