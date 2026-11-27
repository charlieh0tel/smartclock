//! The board's time, counted in the CPU's clock cycles.
//!
//! The clock is the MC68331's synthesizer as the reset code programs
//! it, SYNCR `0xcf80` from the 32.768 kHz reference: 16.777216 MHz
//! (docs/firmware/README.md, the SIM's registers).  Every device's
//! timing, the periodic interrupt's 1024 ticks a second and the serial
//! lines' character times among them, is a count of these cycles, so a
//! boot is the same however fast the host runs it.

use std::ops::Add;
use std::ops::Sub;

/// The CPU's clock, cycles a second.
pub const CPU_HZ: u64 = 16_777_216;

/// The reference crystal (EXTAL), cycles a second, which clocks the
/// periodic timer and the software watchdog (MC68331UM, 4.2, 4.7).
pub(crate) const EXTAL_HZ: u64 = 32_768;

/// CPU cycles a reference cycle.
pub(crate) const CPU_PER_EXTAL: u64 = CPU_HZ / EXTAL_HZ;

/// A count of CPU cycles: a time on the board's clock, or a span of it.
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq, PartialOrd, Ord)]
#[cfg_attr(feature = "mc683xx", derive(serde::Serialize, serde::Deserialize))]
pub struct Cycles(pub u64);

impl Cycles {
    /// `seconds` of the board's time, if it is a time: finite and not
    /// negative.
    pub(crate) fn from_seconds(seconds: f64) -> Option<Self> {
        // a board's run is far shorter than u64's cycles
        (seconds.is_finite() && seconds >= 0.0).then_some(Self((seconds * CPU_HZ as f64) as u64))
    }

    /// `span` later, or the clock's last cycle if that is past it.
    pub(crate) fn saturating_add(self, span: Self) -> Self {
        Self(self.0.saturating_add(span.0))
    }

    /// The span until `later`, zero if it has passed.
    pub(crate) fn until(self, later: Self) -> Self {
        Self(later.0.saturating_sub(self.0))
    }
}

impl Add for Cycles {
    type Output = Self;

    fn add(self, other: Self) -> Self {
        Self(self.0 + other.0)
    }
}

impl Sub for Cycles {
    type Output = Self;

    fn sub(self, other: Self) -> Self {
        Self(self.0 - other.0)
    }
}

/// The time a serial character of `bits` bits takes at `baud`.
pub(crate) fn character(bits: u64, baud: u64) -> Cycles {
    Cycles(CPU_HZ * bits / baud.max(1))
}

#[cfg(test)]
mod tests {
    use super::CPU_HZ;
    use super::Cycles;
    use super::character;

    #[test]
    fn a_tick_of_the_periodic_timer_is_16384_cycles() {
        assert_eq!(CPU_HZ / 1024, 16384);
    }

    #[test]
    fn a_character_at_9600_baud_is_about_a_millisecond() {
        assert_eq!(character(10, 9600), Cycles(17476));
    }
}
