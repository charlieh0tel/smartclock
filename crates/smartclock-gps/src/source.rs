//! Where each second's [`Record`] comes from.
//!
//! A tool that stands in for an engine sends, each second, what an
//! engine would know in it.  A [`Source`] is where that comes from: a
//! sky that never changes ([`scenario::Fixed`]), a table of changes to
//! one ([`scenario::Scenario`]), a recording ([`replay::Replay`]), or
//! whatever a receiver on the bench last said ([`Latest`]).  The
//! wrappers of [`wrap`] alter another source's records on their way
//! past.
//!
//! The tool's clock drives every source.  A source keeps no clock and
//! is not asked the time: its driver calls [`Source::record`] with the
//! [`Tick`] it has reached, and the record that comes back is that
//! second's.  No source carries a pulse, either: the driver times its
//! own 1 PPS, and a record is the data that goes with the pulse of its
//! tick.
//!
//! A record's UTC offset is the one in effect at its time, so a leap
//! second can be put into a record only once: a second one would take
//! the stepped offset for the one before the leap and step it again.
//! Where both leap seconds are in tables, that is refused when the
//! source is built ([`scenario::Scenario::new`]); where one is whatever
//! another source turns out to say, the source's own is kept and the
//! other is not applied but counted ([`wrap::LeapSchedule::withheld`]).
//!
//! Not every source can serve every driver.  One that follows real time
//! means nothing to a driver that runs faster than it, and
//! [`Clocking`], [`Driver`] and [`admit`] say which pairs are refused,
//! and in what words.
//!
//! Nothing here allocates.  A source is built by `const` functions, so
//! it can be a `static`'s initializer; a table it reads is borrowed, so
//! it can stay in flash; and it fills the caller's record in place
//! rather than return one.

use crate::record::Record;
use crate::time::GpsSeconds;
use crate::time::LeapEvent;
use crate::units::Seconds;
use thiserror::Error;

pub mod replay;
pub mod scenario;
pub mod wrap;

/// A second of the driver's clock: whole seconds since the driver
/// started, whatever GPS time its source gives that second.
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct Tick(pub u64);

/// How a source takes the ticks it is asked for.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Clocking {
    /// It gives the record of any tick, asked in any order, and the
    /// same record each time.
    Driven,
    /// It gives the record of a tick only if no later one has been
    /// asked for: a stream that cannot be read again.
    Sequential,
    /// It gives the newest record it has, whatever the tick: it follows
    /// real time, not its driver.
    Live,
}

/// How a driver's clock runs, and so what it asks of a source.  A
/// driver to which more than one applies asks [`admit`] about each.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Driver {
    /// One tick a second of real time.
    Paced,
    /// Ticks as fast as it can count them.
    Free,
    /// Starting again from state saved at an earlier tick.
    Restoring,
    /// With no network or serial port to reach, as in a browser.
    Sandboxed,
}

/// Why a source cannot serve a driver.  The text names the source as
/// the tool's user did.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Error)]
pub enum Refusal<'a> {
    /// A source that follows real time, under a driver that does not.
    #[error("gps source \"{name}\" follows real time, and its driver is not paced to real time")]
    NotPaced {
        /// The source's name.
        name: &'a str,
    },
    /// A source that follows real time, asked to go on from saved
    /// state.
    #[error(
        "gps source \"{name}\" follows real time and cannot resume saved state: the saved GPS time is not the source's"
    )]
    NoResume {
        /// The source's name.
        name: &'a str,
    },
    /// A source that reads forward only, asked to go on from saved
    /// state.
    #[error("gps source \"{name}\" cannot be rewound to the saved second")]
    NoRewind {
        /// The source's name.
        name: &'a str,
    },
    /// A source that follows real time, where nothing can be reached.
    #[error("gps source \"{name}\" needs a network or serial connection, which this build has not")]
    NoConnection {
        /// The source's name.
        name: &'a str,
    },
}

/// Whether a source of `clocking` can serve `driver`.
///
/// | | [`Paced`](Driver::Paced) | [`Free`](Driver::Free) | [`Restoring`](Driver::Restoring) | [`Sandboxed`](Driver::Sandboxed) |
/// | --- | --- | --- | --- | --- |
/// | [`Driven`](Clocking::Driven) | yes | yes | yes | yes |
/// | [`Sequential`](Clocking::Sequential) | yes | yes | [`NoRewind`](Refusal::NoRewind) | yes |
/// | [`Live`](Clocking::Live) | yes | [`NotPaced`](Refusal::NotPaced) | [`NoResume`](Refusal::NoResume) | [`NoConnection`](Refusal::NoConnection) |
///
/// This is asked before the first tick.  A live source that then jumps
/// or falls silent while it runs is the driver's to notice:
/// [`Latest::slip`] and [`Latest::age`] measure it.
///
/// # Errors
///
/// The [`Refusal`] of the table, carrying `name`.
pub const fn admit(name: &str, clocking: Clocking, driver: Driver) -> Result<(), Refusal<'_>> {
    match (clocking, driver) {
        (Clocking::Driven, _)
        | (Clocking::Sequential, Driver::Paced | Driver::Free | Driver::Sandboxed)
        | (Clocking::Live, Driver::Paced) => Ok(()),
        (Clocking::Sequential, Driver::Restoring) => Err(Refusal::NoRewind { name }),
        (Clocking::Live, Driver::Free) => Err(Refusal::NotPaced { name }),
        (Clocking::Live, Driver::Restoring) => Err(Refusal::NoResume { name }),
        (Clocking::Live, Driver::Sandboxed) => Err(Refusal::NoConnection { name }),
    }
}

/// Whether a source had a record for a tick.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[must_use]
pub enum Presence {
    /// It had, and filled the record in.
    Fresh,
    /// It had none: the engine it stands for is silent this second.
    /// The record's contents are not to be used.
    Silent,
}

/// Where each second's record comes from.
pub trait Source {
    /// How it takes ticks.  This does not change while it runs.
    fn clocking(&self) -> Clocking;

    /// Fills `record` with the record of `tick`, the whole of it: what
    /// `record` held is gone either way.
    fn record(&mut self, tick: Tick, record: &mut Record) -> Presence;
}

impl<S: Source + ?Sized> Source for &mut S {
    fn clocking(&self) -> Clocking {
        (**self).clocking()
    }

    fn record(&mut self, tick: Tick, record: &mut Record) -> Presence {
        (**self).record(tick, record)
    }
}

/// How far `to` is after `from`, `None` if an `i64` cannot say.
const fn difference(to: u64, from: u64) -> Option<i64> {
    let distance = to.abs_diff(from);
    if distance > i64::MAX as u64 {
        return None;
    }
    let distance = distance.cast_signed();
    Some(if to >= from { distance } else { -distance })
}

/// Announces a leap second in a record that carries none: the event,
/// and from its second on the offset it leaves.  The record's own
/// offset is taken to be the one before the leap, which is why the
/// record must not carry one already: its offset would have that one's
/// step in it.  Each caller sees to that in its own way.
const fn announce(record: &mut Record, event: LeapEvent) {
    record.leap = Some(event);
    if let Some(time) = record.time
        && let Some(before) = record.utc_offset
    {
        record.utc_offset = Some(event.offset(time.seconds, before));
    }
}

/// The newest record something outside has delivered: a [`Live`]
/// source for whatever reads a receiver or a network.
///
/// Whoever delivers fills [`Self::slot`] and calls [`Self::commit`]
/// with the tick the driver's clock is at; the driver takes each
/// committed record once.  A record half filled is never seen, and one
/// not taken before the next is committed is dropped and counted.  It
/// holds two records.
///
/// Each record is stamped with the tick it arrived in, so the two
/// things a driver must watch a live source for are measured here.
/// [`Self::age`] is how long ago the newest record arrived: asked
/// after [`Source::record`] gave one, how old that record is; asked
/// after it gave none, how long the source has been silent.
/// [`Self::slip`] is how far the newest record's time moved against
/// the ticks since the one before: other than zero when the source's
/// time jumped, or records were lost between the two.
///
/// Nothing here is atomic, and every method takes the whole value.
/// Where the deliverer can interrupt the driver, the caller wraps each
/// of [`Self::slot`] with its filling, [`Self::commit`] and
/// [`Source::record`] in a critical section.  That is the only way on
/// a Cortex-M0 (`thumbv6m-none-eabi`), which has no atomic
/// compare-and-swap to build a lock from.
///
/// [`Live`]: Clocking::Live
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Latest {
    /// The record being filled.
    filling: Record,
    /// The last one committed.
    committed: Record,
    /// The tick it was committed in, `None` before the first.
    arrival: Option<Tick>,
    /// How its time moved against the ticks since the one before.
    slip: Option<Seconds<i64>>,
    /// Whether the driver has yet to take it.
    fresh: bool,
    /// Records committed over one not taken.
    dropped: u32,
}

impl Latest {
    /// With nothing delivered.
    #[must_use]
    pub const fn new() -> Self {
        Self {
            filling: Record::EMPTY,
            committed: Record::EMPTY,
            arrival: None,
            slip: None,
            fresh: false,
            dropped: 0,
        }
    }

    /// The record to fill next: [`Record::EMPTY`] after each commit.
    pub const fn slot(&mut self) -> &mut Record {
        &mut self.filling
    }

    /// Delivers the slot's record, in place of one not yet taken.
    /// `arrival` is the tick the driver's clock is at.
    pub const fn commit(&mut self, arrival: Tick) {
        if self.fresh {
            self.dropped = self.dropped.saturating_add(1);
        }
        self.slip = match (self.committed.time, self.arrival, self.filling.time) {
            (Some(before), Some(before_arrival), Some(time)) => {
                slip(before.seconds, before_arrival, time.seconds, arrival)
            }
            _ => None,
        };
        self.committed = self.filling;
        self.filling = Record::EMPTY;
        self.arrival = Some(arrival);
        self.fresh = true;
    }

    /// How many records were dropped untaken.
    #[must_use]
    pub const fn dropped(&self) -> u32 {
        self.dropped
    }

    /// The tick the newest record arrived in, taken or not.  `None`
    /// before the first.
    #[must_use]
    pub const fn arrival(&self) -> Option<Tick> {
        self.arrival
    }

    /// How long before `now` the newest record arrived, taken or not:
    /// zero in the tick it arrived in, and for a `now` before that.
    /// `None` before the first.
    #[must_use]
    pub const fn age(&self, now: Tick) -> Option<Seconds<u64>> {
        match self.arrival {
            Some(arrival) => Some(Seconds(now.0.saturating_sub(arrival.0))),
            None => None,
        }
    }

    /// How much further the newest record's time is from that of the
    /// record committed before it than their arrivals are apart: zero
    /// for a source in step with the driver's clock, positive where its
    /// time jumped ahead or a record between the two was lost, negative
    /// where it fell back or stood still.  `None` before the second
    /// record, where either has no time, and where an `i64` cannot say.
    #[must_use]
    pub const fn slip(&self) -> Option<Seconds<i64>> {
        self.slip
    }
}

/// How much further `time` is from `before` than the ticks they arrived
/// in are apart.
const fn slip(
    before: GpsSeconds,
    before_arrival: Tick,
    time: GpsSeconds,
    arrival: Tick,
) -> Option<Seconds<i64>> {
    let Some(moved) = difference(time.0, before.0) else {
        return None;
    };
    let Some(ticked) = difference(arrival.0, before_arrival.0) else {
        return None;
    };
    match moved.checked_sub(ticked) {
        Some(slip) => Some(Seconds(slip)),
        None => None,
    }
}

impl Default for Latest {
    fn default() -> Self {
        Self::new()
    }
}

impl Source for Latest {
    fn clocking(&self) -> Clocking {
        Clocking::Live
    }

    fn record(&mut self, _tick: Tick, record: &mut Record) -> Presence {
        if !self.fresh {
            return Presence::Silent;
        }
        *record = self.committed;
        self.fresh = false;
        Presence::Fresh
    }
}

#[cfg(test)]
mod tests {
    use super::Clocking;
    use super::Driver;
    use super::Latest;
    use super::Presence;
    use super::Refusal;
    use super::Source;
    use super::Tick;
    use super::admit;
    use crate::record::Antenna;
    use crate::record::GpsTime;
    use crate::record::Record;
    use crate::time::GpsSeconds;
    use crate::time::SECONDS_PER_WEEK;
    use crate::units::Nanoseconds;
    use crate::units::Seconds;
    use std::string::ToString;

    /// Every cell of the table, and for each refusal its exact text.
    #[test]
    fn admit_refuses_by_the_table() {
        const NAME: &str = "bench";
        let cells: [(Clocking, Driver, Option<&str>); 12] = [
            (Clocking::Driven, Driver::Paced, None),
            (Clocking::Driven, Driver::Free, None),
            (Clocking::Driven, Driver::Restoring, None),
            (Clocking::Driven, Driver::Sandboxed, None),
            (Clocking::Sequential, Driver::Paced, None),
            (Clocking::Sequential, Driver::Free, None),
            (
                Clocking::Sequential,
                Driver::Restoring,
                Some("gps source \"bench\" cannot be rewound to the saved second"),
            ),
            (Clocking::Sequential, Driver::Sandboxed, None),
            (Clocking::Live, Driver::Paced, None),
            (
                Clocking::Live,
                Driver::Free,
                Some(
                    "gps source \"bench\" follows real time, and its driver is not paced to real time",
                ),
            ),
            (
                Clocking::Live,
                Driver::Restoring,
                Some(
                    "gps source \"bench\" follows real time and cannot resume saved state: the saved GPS time is not the source's",
                ),
            ),
            (
                Clocking::Live,
                Driver::Sandboxed,
                Some(
                    "gps source \"bench\" needs a network or serial connection, which this build has not",
                ),
            ),
        ];
        for (clocking, driver, refusal) in cells {
            let text = admit(NAME, clocking, driver)
                .err()
                .map(|refusal| refusal.to_string());
            assert_eq!(text.as_deref(), refusal, "{clocking:?} under {driver:?}");
        }
    }

    /// A refusal is decided where a constant is, and says which it is.
    #[test]
    fn admit_is_const() {
        const REFUSAL: Result<(), Refusal<'static>> = admit("bench", Clocking::Live, Driver::Free);
        assert_eq!(REFUSAL, Err(Refusal::NotPaced { name: "bench" }));
    }

    #[test]
    fn latest_gives_each_record_once() {
        static START: Latest = Latest::new();
        let mut latest = START;
        let mut record = Record::EMPTY;
        assert_eq!(latest.clocking(), Clocking::Live);
        assert_eq!(latest.record(Tick(0), &mut record), Presence::Silent);

        latest.slot().antenna = Antenna::Open;
        // Not committed: not seen.
        assert_eq!(latest.record(Tick(1), &mut record), Presence::Silent);
        latest.commit(Tick(1));
        assert_eq!(latest.slot().antenna, Antenna::Unknown);
        assert_eq!(latest.record(Tick(2), &mut record), Presence::Fresh);
        assert_eq!(record.antenna, Antenna::Open);
        assert_eq!(latest.record(Tick(3), &mut record), Presence::Silent);
        assert_eq!(latest.dropped(), 0);
    }

    #[test]
    fn latest_drops_the_older_and_counts_it() {
        let mut latest = Latest::new();
        latest.slot().antenna = Antenna::Open;
        latest.commit(Tick(7));
        latest.slot().antenna = Antenna::Shorted;
        latest.commit(Tick(8));
        assert_eq!(latest.dropped(), 1);

        let mut record = Record::EMPTY;
        // Through a borrow, as a driver that does not own it would.
        fn take(mut source: impl Source, record: &mut Record) -> Presence {
            source.record(Tick(9), record)
        }
        assert_eq!(take(&mut latest, &mut record), Presence::Fresh);
        assert_eq!(record.antenna, Antenna::Shorted);
        assert_eq!(latest.dropped(), 1);
    }

    /// Delivers a record of GPS second `seconds` in tick `arrival`.
    fn deliver(latest: &mut Latest, seconds: u64, arrival: u64) {
        latest.slot().time = Some(GpsTime {
            seconds: GpsSeconds(seconds),
            subsecond: Nanoseconds(0),
        });
        latest.commit(Tick(arrival));
    }

    /// The age of the record handed out, and then of the silence.
    #[test]
    fn latest_says_how_old_its_record_is() {
        /// Stamped with its arrival where a constant is.
        static DELIVERED: Latest = {
            let mut latest = Latest::new();
            latest.commit(Tick(10));
            latest
        };
        assert_eq!(Latest::new().age(Tick(5)), None);
        assert_eq!(Latest::new().arrival(), None);

        let mut latest = DELIVERED;
        assert_eq!(latest.arrival(), Some(Tick(10)));
        let mut record = Record::EMPTY;
        // Taken two ticks late: the record handed out is two old.
        assert_eq!(latest.record(Tick(12), &mut record), Presence::Fresh);
        assert_eq!(latest.age(Tick(12)), Some(Seconds(2)));
        // Nothing since: the source has been silent that long.
        assert_eq!(latest.record(Tick(40), &mut record), Presence::Silent);
        assert_eq!(latest.age(Tick(40)), Some(Seconds(30)));
        // A tick before the arrival is no negative age.
        assert_eq!(latest.age(Tick(3)), Some(Seconds(0)));

        // A new arrival is a new stamp, taken or not.
        latest.commit(Tick(41));
        assert_eq!(latest.age(Tick(41)), Some(Seconds(0)));
        assert_eq!(latest.arrival(), Some(Tick(41)));
    }

    /// A source in step slips nothing; a jump either way, a time that
    /// stands still and a lost record all show.
    #[test]
    fn latest_measures_a_jump_in_its_sources_time() {
        const START: u64 = 1_000_000;
        const WEEK: u64 = SECONDS_PER_WEEK as u64;
        let mut latest = Latest::new();
        assert_eq!(latest.slip(), None);
        deliver(&mut latest, START, 0);
        // One record is nothing to measure against.
        assert_eq!(latest.slip(), None);
        deliver(&mut latest, START + 1, 1);
        assert_eq!(latest.slip(), Some(Seconds(0)));
        // Three ticks of silence and three seconds on: still in step.
        deliver(&mut latest, START + 4, 4);
        assert_eq!(latest.slip(), Some(Seconds(0)));
        // A week ahead in one tick.
        deliver(&mut latest, START + 5 + WEEK, 5);
        assert_eq!(latest.slip(), Some(Seconds(WEEK.cast_signed())));
        // And back.
        deliver(&mut latest, START + 6, 6);
        assert_eq!(latest.slip(), Some(Seconds(-WEEK.cast_signed())));
        // The same second twice.
        deliver(&mut latest, START + 6, 7);
        assert_eq!(latest.slip(), Some(Seconds(-1)));
        // Two seconds in one tick: one was lost on the way.
        deliver(&mut latest, START + 8, 8);
        assert_eq!(latest.slip(), Some(Seconds(1)));

        // A record with no time measures nothing, nor does the next.
        latest.commit(Tick(9));
        assert_eq!(latest.slip(), None);
        deliver(&mut latest, START + 10, 10);
        assert_eq!(latest.slip(), None);
        deliver(&mut latest, START + 11, 11);
        assert_eq!(latest.slip(), Some(Seconds(0)));

        // Further than an i64 says.
        deliver(&mut latest, u64::MAX, 12);
        deliver(&mut latest, 0, 13);
        assert_eq!(latest.slip(), None);
    }
}
