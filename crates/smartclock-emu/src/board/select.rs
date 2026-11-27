//! How the board's `Bus` reaches the models on chip selects: a byte at
//! a time, at an address, on the select the board wires the model to.
//!
//! Through `Selected`, the models' own bus interface.  On mc683xx's
//! board the bus parts are mc683xx-peripherals' stock chips, but for
//! the EFC's strobe (docs/emulator.md, "The mc683xx device layer"), so
//! these models are the Unicorn board's alone.

use super::time::Cycles;

/// A model on a chip select, as the board reaches it: its bus side a
/// byte at a time, at the address on ADDR, which it decodes as the
/// board wires its address pins, and its own time.
pub(crate) trait Selected {
    /// The byte at `address` read at `now`.
    fn read_byte(&mut self, address: u32, now: Cycles) -> u8;
    /// `value` written at `address` at `now`.
    fn write_byte(&mut self, address: u32, value: u8, now: Cycles);
    /// When it next changes by itself, if it will.
    fn next_change(&self) -> Option<Cycles> {
        None
    }
    /// Time moves on to `now`.
    fn advance_to(&mut self, now: Cycles) {
        let _ = now;
    }
}

/// A chip select, by the number of its pin: CS`n`.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) struct Select(u8);

impl Select {
    /// Chip select 5: the latch, the switch byte, the FPGA, the EFC's
    /// strobe.
    pub(crate) const CS5: Self = Self(5);
    /// Chip select 8: the DUART.
    pub(crate) const CS8: Self = Self(8);
    /// Chip select 9: the EEPROM.
    pub(crate) const CS9: Self = Self(9);
}

/// The chip's time as the board counts it.
#[cfg(feature = "mc683xx")]
pub(crate) const fn board_time(now: mc683xx_cpu32::Cycle) -> Cycles {
    Cycles(now.clocks())
}

/// The byte at `address` on `select` read from `model` at `now`.
pub(crate) fn read(model: &mut impl Selected, select: Select, address: u32, now: Cycles) -> u8 {
    let _ = select;
    model.read_byte(address, now)
}

/// `value` written to `model` at `address` on `select` at `now`.
pub(crate) fn write(
    model: &mut impl Selected,
    select: Select,
    address: u32,
    value: u8,
    now: Cycles,
) {
    let _ = select;
    model.write_byte(address, value, now);
}

/// `model` moved on to `now`.
pub(crate) fn advance(model: &mut impl Selected, now: Cycles) {
    model.advance_to(now);
}

/// When `model` next changes by itself.
pub(crate) fn next_event(model: &impl Selected) -> Option<Cycles> {
    model.next_change()
}
