//! The EFC's strobe at `0x306000`, the fourth device in chip select 5's
//! block: `efc_write` writes 0 there after each word, and the board
//! loads the EFC's DAC's word into the oscillator on it (*inferred*:
//! `qspi_efc_test` writes words without one; docs/emulator.md, "The
//! oscillator").  The value written is nothing: the write cycle is the
//! strobe.
//!
//! What faces the board's nets is `take`, the time of the last strobe
//! not yet taken, at which its output pulses low for the write; the bus
//! side is the write.  A read strobes nothing and reads 0.

#[cfg(feature = "mc683xx")]
use mc683xx_board::adapter::Driver;
#[cfg(feature = "mc683xx")]
use mc683xx_board::peripheral::Drive;
#[cfg(feature = "mc683xx")]
use mc683xx_board::signal::Signal;
#[cfg(feature = "mc683xx")]
use mc683xx_cpu32::BusError;
#[cfg(feature = "mc683xx")]
use mc683xx_cpu32::peripheral::BusCycle;
#[cfg(feature = "mc683xx")]
use mc683xx_cpu32::peripheral::MappedPeripheral;
#[cfg(feature = "mc683xx")]
use mc683xx_modules::peripheral::Timed;

use super::select::Selected;
#[cfg(feature = "mc683xx")]
use super::select::board_time;
#[cfg(feature = "mc683xx")]
use super::system_clock::time;
use super::time::Cycles;

/// The strobe's offset in chip select 5's block.
pub(crate) const OFFSET: u32 = 0x6000;
/// The least a write holds the strobe low: a bus cycle's three clocks
/// (MC68331UM 4.5.1).
#[cfg(feature = "mc683xx")]
const WIDTH: Cycles = Cycles(3);
/// Its one port, the strobe out.
#[cfg(feature = "mc683xx")]
const OUT: usize = 0;

/// The strobe.
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
#[cfg_attr(feature = "mc683xx", derive(serde::Serialize, serde::Deserialize))]
pub(crate) struct Strobe {
    /// When it was last written, until taken.
    strobed: Option<Cycles>,
}

impl Strobe {
    /// The time of the last strobe, if one has come since the last
    /// take.
    pub(crate) fn take(&mut self) -> Option<Cycles> {
        self.strobed.take()
    }
}

/// No address lines: the board's decode of ADDR13 and ADDR14 is its
/// select.
impl Selected for Strobe {
    fn read_byte(&mut self, address: u32, now: Cycles) -> u8 {
        let _ = (address, now);
        0
    }

    fn write_byte(&mut self, address: u32, value: u8, now: Cycles) {
        let _ = (address, value);
        self.strobed = Some(now);
    }
}

#[cfg(feature = "mc683xx")]
impl MappedPeripheral for Strobe {
    fn read(&mut self, cycle: BusCycle) -> Result<u32, BusError> {
        let _ = cycle;
        Ok(0)
    }

    fn write(&mut self, cycle: BusCycle, value: u32) -> Result<(), BusError> {
        self.write_byte(cycle.address.0, value as u8, board_time(cycle.now));
        Ok(())
    }
}

/// No time of its own.
#[cfg(feature = "mc683xx")]
impl Timed for Strobe {}

/// Each write a low pulse on its port, its rising edge the strobe's end.
#[cfg(feature = "mc683xx")]
impl Driver for Strobe {
    fn drive(&mut self, drive: &mut Drive<'_>) {
        if let Some(strobed) = self.take() {
            drive.drive_at(time(drive, strobed), OUT, Signal::Level(false));
            drive.drive_at(time(drive, strobed + WIDTH), OUT, Signal::Level(true));
        }
    }
}

/// The strobe's snapshot (`snapshot`): a strobe not yet taken.
#[cfg(feature = "mc683xx")]
mod save {
    use mc683xx_cpu32::state::Save;
    use mc683xx_cpu32::state::StateError;

    use super::Strobe;
    use crate::board::snapshot;

    impl Save for Strobe {
        fn save(&self) -> Result<Vec<u8>, StateError> {
            Ok(snapshot::save(self))
        }

        fn restore(&mut self, snapshot: &[u8]) -> Result<(), StateError> {
            *self = snapshot::load(snapshot)?;
            Ok(())
        }
    }
}

#[cfg(test)]
mod tests {
    use super::Strobe;
    use crate::board::select::Selected;
    use crate::board::time::Cycles;

    #[test]
    fn a_write_strobes_and_a_read_does_not() {
        let mut strobe = Strobe::default();
        assert_eq!(strobe.read_byte(0, Cycles(5)), 0);
        assert_eq!(strobe.take(), None);
        strobe.write_byte(0, 0, Cycles(7));
        assert_eq!(strobe.take(), Some(Cycles(7)));
        assert_eq!(strobe.take(), None, "taken");
    }
}
