//! The 1 PPS for an external engine on `--gps`, which sends no pulse
//! over TCP.  The pulse is timed, never taken from the engine's
//! messages: it rises on each whole second of the emulator's own clock,
//! the run's time, except when the emulator runs in real time, where it
//! rises on each whole UTC second of the host's clock, on which
//! `smartclock-gps-feed` paces its seconds too, so the two line up.
//! Messages arrive over TCP with jitter of tens to hundreds of
//! milliseconds, and a reply to a command looks like the continuous
//! output, so a pulse started or moved by them starts at a bad phase
//! and jumps later; a timed one never moves.  The second a rise is due
//! on is fixed at the start against board time, so a run that falls
//! behind the wall clock under load keeps its rises a second apart on
//! board time rather than moving them.
//!
//! On mc683xx the cable delay and the pulse offset the firmware sends
//! the engine move each rise from the next on by them, the seconds
//! staying where they are (`mc683xx::pps`); the pulse mode is the
//! external engine's alone, and the timed pulse always rises.  The
//! Unicorn core's pulse is the plain timed second.  The built-in
//! engine, which the emulator holds, applies all three
//! (`gps::engine`).
//!
//! `Rises` schedules the pulse for either core, in its board time;
//! `Pulse` gives it to the Unicorn board.

use std::cell::RefCell;
use std::rc::Rc;
use std::time::SystemTime;

use crate::board::Board;
use crate::board::time::CPU_HZ;
use crate::board::time::Cycles;
use crate::run::Alongside;
use crate::run::Flow;
use crate::run::Pacing;

/// Nanoseconds a second.
const NANOSECONDS_PER_SECOND: u128 = 1_000_000_000;

/// The clock whose whole seconds the pulse rises on.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) enum PulseClock {
    /// The emulator's own: the run's time.
    Own,
    /// The host's, its whole UTC seconds, in a real-time run.
    Host,
}

impl PulseClock {
    /// The clock for a run paced as `pacing` says.
    pub(crate) fn of(pacing: Pacing) -> Self {
        match pacing {
            Pacing::Wall => Self::Host,
            Pacing::Free => Self::Own,
        }
    }

    /// How far into its second this clock is, in board time of which
    /// `per_second` make a second, `run` being the run's time now.
    pub(crate) fn into_second(self, run: u64, per_second: u64) -> u64 {
        match self {
            Self::Own => run % per_second,
            Self::Host => {
                let nanoseconds = SystemTime::now()
                    .duration_since(SystemTime::UNIX_EPOCH)
                    .unwrap_or_default()
                    .subsec_nanos();
                into_second_of(nanoseconds, per_second)
            }
        }
    }
}

/// `nanoseconds` into a second, in board time of which `per_second`
/// make a second.
fn into_second_of(nanoseconds: u32, per_second: u64) -> u64 {
    let units = u128::from(nanoseconds) * u128::from(per_second) / NANOSECONDS_PER_SECOND;
    u64::try_from(units).unwrap_or(0)
}

/// The external engine's 1 PPS rises, in board time of which
/// `per_second` make a second, as each core keeps it (the Unicorn
/// board's cycles, the mc683xx board's picoseconds): each a second
/// after the last, from the first.
#[derive(Debug, Clone, PartialEq, Eq)]
pub(crate) struct Rises {
    /// The next whole second a rise is due on.
    next: u64,
    /// How far from its whole second each rise is, signed: the pulse's
    /// settings as the firmware sent them, where they are applied.
    offset: i64,
}

impl Rises {
    /// Rises from the next whole second of the clock: board time is
    /// `now`, and the clock `into_second` into its second.
    pub(crate) fn from(now: u64, into_second: u64, per_second: u64) -> Self {
        let into_second = into_second % per_second;
        let next = if into_second == 0 {
            now
        } else {
            now + per_second - into_second
        };
        Self { next, offset: 0 }
    }

    /// Each rise from now on `offset` from its whole second, later for
    /// a positive one: the seconds stay where they are, so the pulse
    /// steps by the change and no more.
    pub(crate) fn shift(&mut self, offset: i64) {
        self.offset = offset;
    }

    /// When the next rise is due.
    pub(crate) fn next(&self) -> Option<u64> {
        Some(self.next.saturating_add_signed(self.offset))
    }

    /// The rise due by `now`, if any, the next then a second after it.
    pub(crate) fn take(&mut self, now: u64, per_second: u64) -> Option<u64> {
        let at = self.next().filter(|at| *at <= now)?;
        self.next += per_second;
        Some(at)
    }
}

/// The Unicorn board's GPS 1 PPS, on the whole seconds of `PulseClock`.
pub(crate) struct Pulse {
    /// The board.
    board: Rc<RefCell<Board>>,
    /// The rises, cycles.
    rises: Rises,
}

impl Pulse {
    /// The pulse to `board`, its board time `now`, on `seconds`' whole
    /// seconds; the Unicorn board's time is the run's.
    pub(crate) fn new(board: Rc<RefCell<Board>>, now: Cycles, seconds: PulseClock) -> Self {
        let into_second = seconds.into_second(now.0, CPU_HZ);
        Self {
            board,
            rises: Rises::from(now.0, into_second, CPU_HZ),
        }
    }
}

impl Alongside for Pulse {
    fn advance(&mut self, now: Cycles) -> Flow {
        while let Some(at) = self.rises.take(now.0, CPU_HZ) {
            self.board.borrow_mut().gps_pps(Cycles(at));
        }
        Flow::Continue
    }

    fn next_event(&self) -> Option<Cycles> {
        self.rises.next().map(Cycles)
    }
}

#[cfg(test)]
mod tests {
    use std::cell::RefCell;
    use std::rc::Rc;

    use super::NANOSECONDS_PER_SECOND;
    use super::Pulse;
    use super::PulseClock;
    use super::Rises;
    use super::into_second_of;
    use crate::board::Board;
    use crate::board::eeprom::Eeprom;
    use crate::board::profile::Model;
    use crate::board::time::CPU_HZ;
    use crate::board::time::Cycles;
    use crate::line::Line;
    use crate::run::Alongside;
    use crate::run::Pacing;

    /// Units of board time a second the rises are tried in: whole
    /// microseconds and the CPU's cycles.
    const UNITS_PER_SECOND: [u64; 2] = [1_000_000, CPU_HZ];

    #[test]
    fn the_clock_is_the_hosts_only_in_real_time() {
        assert_eq!(PulseClock::of(Pacing::Wall), PulseClock::Host);
        assert_eq!(PulseClock::of(Pacing::Free), PulseClock::Own);
    }

    /// On its own clock the pulse rises on the run's whole seconds,
    /// each a second after the last, whenever it is started.
    #[test]
    fn on_its_own_clock_the_pulse_rises_on_the_runs_seconds() {
        for per_second in UNITS_PER_SECOND {
            let now = 7 * per_second + per_second / 3;
            let into_second = PulseClock::Own.into_second(now, per_second);
            let mut rises = Rises::from(now, into_second, per_second);
            assert_eq!(rises.next(), Some(8 * per_second));
            assert_eq!(rises.take(8 * per_second - 1, per_second), None);
            for second in 8..20 {
                assert_eq!(
                    rises.take(second * per_second, per_second),
                    Some(second * per_second)
                );
            }
            // on a whole second, the rise is now
            let rises = Rises::from(9 * per_second, 0, per_second);
            assert_eq!(rises.next(), Some(9 * per_second));
        }
    }

    /// In real time the pulse rises where the host's clock reaches its
    /// next whole second, wherever board time stands, and a run that
    /// falls behind takes each rise late, never moved: a second apart.
    #[test]
    fn in_real_time_the_pulse_rises_on_the_hosts_seconds() {
        for per_second in UNITS_PER_SECOND {
            // the host 0.75 s into its second, board time 3.1 s
            let into_second = into_second_of(750_000_000, per_second);
            let now = 3 * per_second + per_second / 10;
            let mut rises = Rises::from(now, into_second, per_second);
            let first = now + per_second / 4;
            assert_eq!(rises.next(), Some(first));
            // behind by 2.5 s: three rises due, each a second apart
            let late = first + 2 * per_second + per_second / 2;
            let taken: Vec<u64> = std::iter::from_fn(|| rises.take(late, per_second)).collect();
            assert_eq!(taken, [first, first + per_second, first + 2 * per_second]);
            assert_eq!(rises.next(), Some(first + 3 * per_second));
        }
        let into = PulseClock::Host.into_second(0, CPU_HZ);
        assert!(into < CPU_HZ);
        assert_eq!(
            into_second_of(u32::try_from(NANOSECONDS_PER_SECOND / 2).unwrap(), CPU_HZ),
            CPU_HZ / 2
        );
    }

    #[test]
    fn the_unicorn_pulse_rises_each_second_of_the_run() {
        let board = Rc::new(RefCell::new(Board::new(
            Model::Z3805A,
            Line::new("sci", false),
            Line::new("gps", false),
            Line::new("host", false),
            Eeprom::new(Vec::new(), None),
        )));
        let start = Cycles(CPU_HZ / 2);
        let mut pulse = Pulse::new(board, start, PulseClock::Own);
        assert_eq!(pulse.next_event(), Some(Cycles(CPU_HZ)));
        pulse.advance(Cycles(CPU_HZ));
        assert_eq!(pulse.next_event(), Some(Cycles(2 * CPU_HZ)));
    }
}
