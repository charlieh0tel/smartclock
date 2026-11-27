//! A clock signal on the board, as a trace carries one from its source
//! to the pins it drives.  The source and the device it clocks know
//! nothing of each other; the board joins them (mod.rs).
//!
//! A clocked device here wants events, not edges: the FPGA's divider
//! pulses each time the oscillator has run ten million periods, so a
//! signal is its phase, the periods it has run, ticks, since power-up.
//! A rising edge is a whole tick, and a source answers in whole ticks
//! exactly: the cycle it names for an edge is the first at which its
//! phase has reached the edge, so that a device's event comes due on
//! the cycle it asked for.

use std::ops::Add;
use std::ops::Sub;

#[cfg(test)]
use super::time::CPU_HZ;
use super::time::Cycles;

/// The nominal 10 MHz.
pub(crate) const IDEAL_HZ: u64 = 10_000_000;

/// A clock's rising edge: the periods it has run since power-up, whole.
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq, PartialOrd, Ord)]
pub(crate) struct Tick(pub(crate) u64);

impl Add<u64> for Tick {
    type Output = Self;

    fn add(self, ticks: u64) -> Self {
        Self(self.0 + ticks)
    }
}

impl Sub<u64> for Tick {
    type Output = Self;

    fn sub(self, ticks: u64) -> Self {
        Self(self.0 - ticks)
    }
}

/// A clock's phase at a time: the last rising edge at or before it, and
/// the part of a period run since.
#[derive(Debug, Clone, Copy, Default, PartialEq)]
pub(crate) struct Phase {
    /// The last rising edge at or before it.
    pub(crate) tick: Tick,
    /// The part of a period past `tick`, from 0 up to 1.
    pub(crate) fraction: f64,
}

impl Phase {
    /// The rising edge `n` after it: the first is the next whole tick.
    pub(crate) fn edge(self, n: u64) -> Tick {
        self.tick + n
    }

    /// The periods from it to the rising edge `later`, not before it.
    pub(crate) fn until(self, later: Tick) -> f64 {
        // a span of ticks far inside f64's whole numbers
        (later.0 - self.tick.0) as f64 - self.fraction
    }

    /// Its seconds of the clock's own time, its ticks at the nominal
    /// frequency.
    #[cfg(test)]
    pub(crate) fn seconds(self) -> f64 {
        (self.tick.0 as f64 + self.fraction) / IDEAL_HZ as f64
    }
}

impl From<Tick> for Phase {
    /// The phase on the rising edge `tick`.
    fn from(tick: Tick) -> Self {
        Self {
            tick,
            fraction: 0.0,
        }
    }
}

/// A clock signal: its phase on the board's clock.
pub(crate) trait Clock {
    /// Its phase at `at`.
    fn phase(&self, at: Cycles) -> Phase;
    /// The first board cycle at which its phase has reached the rising
    /// edge `tick`: `phase` there is at `tick` or past it, and the cycle
    /// before short of it.
    fn reaching(&self, tick: Tick) -> Cycles;
}

/// A clock exactly on its nominal frequency on the board's clock, the
/// CPU's 16.777216 MHz: its phase is the board's time.  The tests'
/// reference.
#[cfg(test)]
#[derive(Debug, Clone, Copy, Default)]
pub(crate) struct Ideal;

#[cfg(test)]
impl Clock for Ideal {
    fn phase(&self, at: Cycles) -> Phase {
        let periods = u128::from(at.0) * u128::from(IDEAL_HZ);
        let cpu = u128::from(CPU_HZ);
        Phase {
            // a board time's ticks fit u64
            tick: Tick((periods / cpu) as u64),
            fraction: (periods % cpu) as f64 / CPU_HZ as f64,
        }
    }

    fn reaching(&self, tick: Tick) -> Cycles {
        // a tick's cycles fit u64
        Cycles((u128::from(tick.0) * u128::from(CPU_HZ)).div_ceil(u128::from(IDEAL_HZ)) as u64)
    }
}

#[cfg(test)]
mod tests {
    use super::super::time::CPU_HZ;
    use super::super::time::Cycles;
    use super::Clock;
    use super::IDEAL_HZ;
    use super::Ideal;
    use super::Tick;

    #[test]
    fn the_ideal_clocks_phase_is_the_boards_time() {
        assert_eq!(Ideal.phase(Cycles(3600 * CPU_HZ)).seconds(), 3600.0);
        assert_eq!(Ideal.reaching(Tick(IDEAL_HZ)), Cycles(CPU_HZ));
        // 1 us is 16.78 cycles in
        assert_eq!(Ideal.reaching(Tick(10)), Cycles(17));
        let phase = Ideal.phase(Cycles(17));
        assert_eq!(phase.tick, Tick(10));
        assert!((phase.until(Tick(11)) - (1.0 - 0.13)).abs() < 0.01);
    }
}
