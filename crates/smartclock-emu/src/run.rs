//! What both cores' run loops share: what runs alongside the board on
//! its clock, whether a run goes on after a slice, and the board's time
//! kept to the wall clock's.
//!
//! The loops themselves are each core's: the Unicorn machine's
//! (`machine.rs`) delivers the board's interrupts and resets between
//! its slices, and the mc683xx runner's (`mc683xx/runner.rs`) leaves
//! them to the board crate and moves bytes, keys and controls instead.
//!
//! The board's time is counted, never read from the wall clock, so a
//! run is the same each time given the same input at the same board
//! times: with `Pacing::Free`, the deterministic mode, it runs flat out;
//! with `Pacing::Wall`, the default, it is held back to the wall clock.
//! The Unicorn machine keeps it so itself, never ahead of the wall
//! clock by more than `AHEAD` (`Pace`); an mc683xx run's host steps it a
//! frame at a time to the host's clock (`mc683xx::Run::step_frame`),
//! which it reads, the board never.  Only where the host cannot keep up
//! does the board fall behind the wall clock, and that is reported once
//! it is `BEHIND`.

use std::time::Duration;
#[cfg(not(target_family = "wasm"))]
use std::time::Instant;

use crate::board::front_panel::Key;
#[cfg(not(target_family = "wasm"))]
use crate::board::time::CPU_HZ;
use crate::board::time::Cycles;
use crate::names::Named as _;
use crate::trace;

/// The longest slice of a run, either core's: a quarter of a periodic
/// tick.
pub(crate) const SLICE: Cycles = Cycles(4096);
/// How far ahead of the wall clock the board may run before the
/// machine waits for it, when it keeps to real time.
#[cfg(not(target_family = "wasm"))]
const AHEAD: Duration = Duration::from_millis(20);
/// How far behind the wall clock the board may fall before the machine
/// says so.
pub(crate) const BEHIND: Duration = Duration::from_secs(1);
/// Nanoseconds a second.
#[cfg(not(target_family = "wasm"))]
const NANOSECONDS_PER_SECOND: u128 = 1_000_000_000;

/// Something that runs off the board on its clock.
pub trait Alongside {
    /// Advanced to the run's time `now` after each slice: whether the
    /// run goes on.
    fn advance(&mut self, now: Cycles) -> Flow;

    /// When it next acts by itself, so that a slice ends then.
    fn next_event(&self) -> Option<Cycles> {
        None
    }
}

/// A closure advanced alongside, with no events of its own.
impl<F: FnMut(Cycles) -> Flow> Alongside for F {
    fn advance(&mut self, now: Cycles) -> Flow {
        self(now)
    }
}

/// Whether a run goes on after a slice.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Flow {
    /// It goes on.
    Continue,
    /// It stops: a script's end.
    Stop,
    /// It fails, and why: a script's expect not met in time.
    Fail(String),
}

/// What runs alongside a board, in the order it was added: a serial
/// line's far end such as the synthetic GPS engine, a script, standard
/// input.
#[derive(Default)]
pub(crate) struct Alongsides(Vec<Box<dyn Alongside>>);

impl std::fmt::Debug for Alongsides {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        formatter
            .debug_tuple("Alongsides")
            .field(&self.0.len())
            .finish()
    }
}

impl Alongsides {
    /// `alongside` advanced after each slice, after those before it.
    pub(crate) fn push(&mut self, alongside: Box<dyn Alongside>) {
        self.0.push(alongside);
    }

    /// The earliest of their next events, if any has one.
    pub(crate) fn next_event(&self) -> Option<Cycles> {
        self.0
            .iter()
            .filter_map(|alongside| alongside.next_event())
            .min()
    }

    /// Each advanced to `now`: whether the run goes on, a failure first,
    /// then a stop.
    pub(crate) fn advance(&mut self, now: Cycles) -> Flow {
        self.0.iter_mut().fold(Flow::Continue, |flow, alongside| {
            match (flow, alongside.advance(now)) {
                (Flow::Fail(why), _) | (_, Flow::Fail(why)) => Flow::Fail(why),
                (Flow::Stop, _) | (_, Flow::Stop) => Flow::Stop,
                (Flow::Continue, Flow::Continue) => Flow::Continue,
            }
        })
    }
}

/// Where and why the CPU halted, either core's, in one form: the run
/// ends with it.
#[derive(Debug, Clone, PartialEq, Eq, thiserror::Error)]
#[error("the CPU halted at {pc:#010x}: {reason}")]
pub struct Halted {
    /// Where: the program counter.
    pub(crate) pc: u32,
    /// Why, as the core says it.
    pub(crate) reason: String,
}

/// How the board's time keeps to the wall clock's.
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub enum Pacing {
    /// Held back to the wall clock.
    #[default]
    Wall,
    /// Flat out: the deterministic mode.
    Free,
}

/// The board's time kept to the wall clock's, as `Pacing` says: the
/// Unicorn machine's, native alone, which reads `Instant` and sleeps.
#[cfg(not(target_family = "wasm"))]
#[derive(Debug)]
pub(crate) struct Pace {
    /// When the run started on the wall clock, when paced to it.
    started: Option<Instant>,
    /// Whether the board has been reported behind the wall clock.
    behind: bool,
}

#[cfg(not(target_family = "wasm"))]
impl Pace {
    /// Pacing from now, as `pacing` says.
    pub(crate) fn new(pacing: Pacing) -> Self {
        Self {
            started: (pacing == Pacing::Wall).then(Instant::now),
            behind: false,
        }
    }

    /// Waits while the board, at `now`, is ahead of the wall clock,
    /// when paced to it; says so, once, if it falls behind.
    pub(crate) fn keep(&mut self, now: Cycles) {
        self.keep_time(nanoseconds(now));
    }

    /// As `keep`, the board at `nanoseconds` of its time.
    fn keep_time(&mut self, nanoseconds: u64) {
        let Some(started) = self.started else {
            return;
        };
        let board = Duration::from_nanos(nanoseconds);
        let wall = started.elapsed();
        if let Some(ahead) = board.checked_sub(wall)
            && ahead > AHEAD
        {
            std::thread::sleep(ahead);
        } else if !self.behind && wall.saturating_sub(board) > BEHIND {
            self.behind = true;
            trace::note(format_args!(
                "the board is {:.1} s behind the wall clock: the host cannot \
                 keep up, and the periodic ticks come late",
                (wall - board).as_secs_f64()
            ));
        }
    }
}

/// `cycles` of board time, nanoseconds, worked in u128: a u64 product
/// of cycles and a second's nanoseconds overflows past about 18 minutes
/// of board time.
#[cfg(not(target_family = "wasm"))]
fn nanoseconds(cycles: Cycles) -> u64 {
    let nanoseconds = u128::from(cycles.0) * NANOSECONDS_PER_SECOND / u128::from(CPU_HZ);
    // a u64 of nanoseconds is some 580 years of board time
    u64::try_from(nanoseconds).unwrap_or(u64::MAX)
}

/// Says that `key`, a script's or the page's, found no front panel to
/// press.
pub(crate) fn no_front_panel(key: Key) {
    trace::note(format_args!(
        "no front panel on this board; key {} not pressed",
        key.name()
    ));
}

#[cfg(test)]
mod tests {
    use super::Alongside;
    use super::Alongsides;
    use super::Flow;
    use super::NANOSECONDS_PER_SECOND;
    use super::Pace;
    use super::Pacing;
    use super::nanoseconds;
    use crate::board::time::CPU_HZ;
    use crate::board::time::Cycles;

    /// Seconds of board time past where a u64 product of cycles and a
    /// second's nanoseconds overflows, about 1099 s.
    const PAST_OVERFLOW_SECONDS: u64 = 2000;

    #[test]
    fn board_time_past_a_u64_product_is_paced() {
        let seconds = PAST_OVERFLOW_SECONDS;
        assert!(u64::MAX / (seconds * CPU_HZ) < NANOSECONDS_PER_SECOND as u64);
        assert_eq!(
            u128::from(nanoseconds(Cycles(seconds * CPU_HZ))),
            u128::from(seconds) * NANOSECONDS_PER_SECOND
        );
    }

    #[test]
    fn free_running_board_time_past_a_u64_product_is_kept() {
        Pace::new(Pacing::Free).keep(Cycles(PAST_OVERFLOW_SECONDS * CPU_HZ));
    }

    /// Something alongside with an event at a time of its own, its run
    /// as `flow` says.
    struct At(Cycles, Flow);

    impl Alongside for At {
        fn advance(&mut self, _: Cycles) -> Flow {
            self.1.clone()
        }

        fn next_event(&self) -> Option<Cycles> {
            Some(self.0)
        }
    }

    #[test]
    fn a_halt_says_where_in_a_whole_32_bit_address() {
        let halted = super::Halted {
            pc: 0x15f88,
            reason: "a double bus fault".to_owned(),
        };
        assert_eq!(
            halted.to_string(),
            "the CPU halted at 0x00015f88: a double bus fault"
        );
    }

    #[test]
    fn the_next_event_alongside_is_the_earliest_and_a_failure_outranks_a_stop() {
        let mut alongside = Alongsides::default();
        assert_eq!(alongside.next_event(), None);
        alongside.push(Box::new(At(Cycles(9), Flow::Stop)));
        alongside.push(Box::new(|_| Flow::Continue));
        alongside.push(Box::new(At(Cycles(5), Flow::Fail("why".to_owned()))));
        assert_eq!(alongside.next_event(), Some(Cycles(5)));
        assert_eq!(alongside.advance(Cycles(0)), Flow::Fail("why".to_owned()));
    }
}
