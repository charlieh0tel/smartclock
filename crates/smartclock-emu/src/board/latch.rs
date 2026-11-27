//! The front-panel latch at `0x300000`, the first byte of chip select
//! 5's block: the lamps, which are the model's (`profile`).  The
//! Z3805A's (docs/firmware/outputs.md, "The Z3805A's lamps"):
//! `leds_update` (`0x464de`) writes it whole about once a second, and
//! `lamp_test` (`0x184ee`) flashes it at power-up; bits 3, 6 and 7
//! drive nothing it names.  The 58503A's three lamps are on bits 0 to
//! 2 and its front panel's control lines on bits 6 and 7
//! (docs/firmware/58503a-3704.md, "The front panel"): bit 6 frames a
//! command while low, bit 7 holds the panel in reset while low
//! (`front_panel`).  Each lamp is reported as it changes
//! (`indicators`).  Power is not the firmware's.
//!
//! What faces the board's nets is `outputs`, the levels on its eight
//! outputs; the bus side is the byte it holds, written whole and read
//! back.

use super::select::Selected;
use super::time::Cycles;
use crate::indicators::Bank;
use crate::indicators::Indicator;
use crate::indicators::Polarity;

/// The latch's offset in chip select 5's block.
pub(crate) const OFFSET: u32 = 0x0000;

/// The latch, its lamps, as a bank of indicators.
#[derive(Debug, Clone)]
pub(crate) struct Latch {
    /// The byte it holds, and the lamps it lights.
    lamps: Bank,
}

impl Latch {
    /// The latch, its `lamps` lit by a one, dark at power-up: what the
    /// board powers up with is not known, and `lamp_test` writes it
    /// first.
    pub(crate) fn new(lamps: &'static [Indicator]) -> Self {
        Self {
            lamps: Bank::new("lamp", lamps, Polarity::High, 0),
        }
    }

    /// Reports the lamps from `at` on: what is lit then, and each
    /// change after.
    pub(crate) fn report_from(&mut self, at: Cycles) {
        self.lamps.report_from(at);
    }

    /// The levels on its outputs, Q0 to Q7: the byte it holds.
    pub(crate) fn outputs(&self) -> u8 {
        self.lamps.value()
    }
}

/// No address lines: the board's decode of ADDR13 and ADDR14 is its
/// select.
impl Selected for Latch {
    fn read_byte(&mut self, address: u32, now: Cycles) -> u8 {
        let _ = (address, now);
        self.outputs()
    }

    fn write_byte(&mut self, address: u32, value: u8, now: Cycles) {
        let _ = address;
        self.lamps.write_reporting(now, value);
    }
}

#[cfg(test)]
mod tests {
    use super::Latch;
    use crate::board::profile::Model;
    use crate::board::select::Selected;
    use crate::board::time::Cycles;
    use crate::indicators::Bank;
    use crate::indicators::Polarity;

    /// The bank a latch of `model`'s lamps is.
    fn latch(model: Model) -> Bank {
        Bank::new("lamp", model.lamps(), Polarity::High, 0)
    }

    #[test]
    fn the_lamp_test_lights_every_lamp_and_then_none() {
        let mut latch = latch(Model::Z3805A);
        let lit = latch.write(Cycles(0), 0xff);
        assert_eq!(
            lit.iter().map(|change| change.name).collect::<Vec<_>>(),
            ["enabled", "active", "alarm", "gps_lock", "holdover"]
        );
        assert!(lit.iter().all(|change| change.lit));
        assert!(latch.write(Cycles(1), 0).iter().all(|change| !change.lit));
    }

    #[test]
    fn gps_lock_is_bit_4() {
        let mut latch = latch(Model::Z3805A);
        let changes = latch.write(Cycles(0), 0x10);
        assert_eq!(changes.len(), 1);
        assert_eq!(changes[0].name, "gps_lock");
        assert_eq!(latch.value(), 0x10);
    }

    #[test]
    fn the_58503a_has_gps_lock_on_bit_0_and_nothing_on_its_panel_lines() {
        let mut latch = latch(Model::Hp58503A);
        let changes = latch.write(Cycles(0), 0xc1);
        assert_eq!(
            changes.iter().map(|change| change.name).collect::<Vec<_>>(),
            ["gps_lock"]
        );
    }

    #[test]
    fn its_outputs_are_the_byte_written() {
        let mut latch = Latch::new(Model::Hp58503A.lamps());
        latch.write_byte(0, 0xc1, Cycles(0));
        assert_eq!(latch.outputs(), 0xc1);
        assert_eq!(latch.read_byte(0, Cycles(0)), 0xc1);
    }
}
