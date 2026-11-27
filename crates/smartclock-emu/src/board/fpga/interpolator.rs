//! The interpolator beside the FPGA: not in the part, it measures the
//! start's interval, AH.Y (out on P81), from the GPS 1 PPS, or mode 3's
//! start, to the edge the gate opens on, and returns a byte on P3 to
//! P11.  Its model reads its low reference, one clock, and its high,
//! two, under the hex DAC's trims, and an interval between in
//! proportion.

use super::super::dac::HexDac;

/// The interpolator's references, as the hex DAC trims them: the slot
/// that trims each (ti.c, `TI_HDAC_SLOT_LOW`, `TI_HDAC_SLOT_HIGH`), and
/// its reading with the trim at the DAC's middle, `TI_HDAC_START`.  The
/// readings fall as a trim rises (`dac_search_bisect` lowers the DAC
/// for a reading below its window), by `TRIM_GAIN` counts a step.  The
/// calibration wants the low in 1 to 25 and the high 200 above it, one
/// 100 ns tick.  The middle readings and the gain are picks, not
/// measurements of a receiver: each starts a little off, so that
/// `interpolator_calibrate` has to search, and the gain lets it settle
/// within the DACs' range.
const TRIM_SLOT_LOW: usize = 2;
const TRIM_SLOT_HIGH: usize = 3;
const TRIM_MIDDLE: f64 = 31.0;
const TRIM_GAIN: f64 = 1.5;
const LOW_AT_MIDDLE: f64 = 31.0;
const HIGH_AT_MIDDLE: f64 = 222.0;
/// The interpolator's references, the intervals it reads low and high:
/// one and two periods of a nominal 10 MHz, seconds.
const REFERENCE_LOW_SECONDS: f64 = 100e-9;
const REFERENCE_HIGH_SECONDS: f64 = 200e-9;

/// The hex DAC's trims of the interpolator's references: each
/// reference's six-bit code, as `hdac_set` (dac.c) puts it in its slot.
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub(crate) struct Trims {
    /// The low reference's, slot 2.
    low: u8,
    /// The high reference's, slot 3.
    high: u8,
}

impl Trims {
    /// The trims of codes `low` and `high`.
    pub(crate) const fn new(low: u8, high: u8) -> Self {
        Self { low, high }
    }

    /// The trims as `dac` holds them.
    pub(crate) fn of(dac: &HexDac) -> Self {
        Self::new(dac.channel(TRIM_SLOT_LOW), dac.channel(TRIM_SLOT_HIGH))
    }

    /// The interpolator's reading of its low or high reference under
    /// the present trim for it, before rounding to its byte.
    pub(super) fn reading(self, high: bool) -> f64 {
        let (code, at_middle) = if high {
            (self.high, HIGH_AT_MIDDLE)
        } else {
            (self.low, LOW_AT_MIDDLE)
        };
        at_middle - TRIM_GAIN * (f64::from(code) - TRIM_MIDDLE)
    }
}

/// The external interpolator: the byte it holds on P3 to P11.
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub(super) struct Interpolator {
    pub(super) byte: u8,
}

impl Interpolator {
    /// It measures `seconds`, the start's interval, under the hex DAC's
    /// trims: its low reference's reading, and its high's, and between
    /// in proportion.
    pub(super) fn measure(&mut self, seconds: f64, trims: Trims) {
        let low = trims.reading(false);
        let high = trims.reading(true);
        let reading = low
            + (high - low) * (seconds - REFERENCE_LOW_SECONDS)
                / (REFERENCE_HIGH_SECONDS - REFERENCE_LOW_SECONDS);
        // clamped to the byte it holds
        self.byte = reading.round().clamp(0.0, f64::from(u8::MAX)) as u8;
    }
}

#[cfg(test)]
mod tests {
    use super::super::super::dac::HexDac;
    use super::Trims;

    /// The hex DAC's frame puts slots 2 and 3 in its middle word, the
    /// even slot in the high six bits (dac.rs, `HexDac::channel`).
    #[test]
    fn the_trims_are_the_hex_dacs_slots_2_and_3() {
        let mut dac = HexDac::default();
        for (entry, word) in [(0, 0), (1, 31 << 6 | 17), (2, 0)] {
            dac.exchange(entry, word);
        }
        assert_eq!(Trims::of(&dac), Trims::new(31, 17));
        assert_eq!(Trims::of(&HexDac::default()), Trims::default());
    }
}
