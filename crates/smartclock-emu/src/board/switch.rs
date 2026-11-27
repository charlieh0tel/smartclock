//! The switch byte at `0x302000`, the second device in chip select 5's
//! block, which no Z3805A firmware reads (docs/firmware/restart.md,
//! "The chip selects"): a byte of positions read through a buffer.  With
//! nothing known of the board's switches, every position reads open,
//! high.
//!
//! What faces the board's nets is `set`, the level on each of its
//! eight inputs; the bus side is the byte they make, read.  A write
//! reaches nothing.

use super::select::Selected;
use super::time::Cycles;

/// The switch byte's offset in chip select 5's block.
pub(crate) const OFFSET: u32 = 0x2000;
/// What it reads with every position open.
pub(crate) const OPEN: u8 = 0xff;

/// The switch byte.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) struct Switch {
    /// The levels on its inputs, a one high.
    positions: u8,
}

impl Default for Switch {
    fn default() -> Self {
        Self::new(OPEN)
    }
}

impl Switch {
    /// The byte with `positions` on its inputs.
    pub(crate) const fn new(positions: u8) -> Self {
        Self { positions }
    }

    /// Input `input`, 0 to 7, at `high`.
    #[cfg(test)]
    pub(crate) fn set(&mut self, input: u8, high: bool) {
        let bit = 1 << input;
        self.positions = if high {
            self.positions | bit
        } else {
            self.positions & !bit
        };
    }
}

/// No address lines: the board's decode of ADDR13 and ADDR14 is its
/// select.
impl Selected for Switch {
    fn read_byte(&mut self, address: u32, now: Cycles) -> u8 {
        let _ = (address, now);
        self.positions
    }

    fn write_byte(&mut self, address: u32, value: u8, now: Cycles) {
        let _ = (address, value, now);
    }
}

#[cfg(test)]
mod tests {
    use super::OPEN;
    use super::Switch;
    use crate::board::select::Selected;
    use crate::board::time::Cycles;

    #[test]
    fn every_position_reads_open_until_an_input_is_low() {
        let mut switch = Switch::default();
        assert_eq!(switch.read_byte(0, Cycles(0)), OPEN);
        switch.set(3, false);
        assert_eq!(switch.read_byte(0, Cycles(0)), 0xf7);
        switch.write_byte(0, 0, Cycles(0));
        assert_eq!(
            switch.read_byte(0, Cycles(0)),
            0xf7,
            "a write reaches nothing"
        );
    }
}
