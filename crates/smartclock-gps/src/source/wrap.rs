//! Sources that alter another's records on their way past.
//!
//! Each wraps any [`Source`] by value and is one itself, so they nest,
//! and a nest is one value with no pointers between its layers: it can
//! be a `static`.  Each has a setting that changes nothing, so a tool
//! can hold one fixed nest and switch its layers on by configuration.
//! None changes how its source takes ticks: [`Source::clocking`] is the
//! wrapped source's.

use crate::record::GpsTime;
use crate::record::Record;
use crate::source::Clocking;
use crate::source::Presence;
use crate::source::Source;
use crate::source::Tick;
use crate::source::announce;
use crate::source::difference;
use crate::time::GpsSeconds;
use crate::time::LeapEvent;
use crate::units::Nanoseconds;
use crate::units::Seconds;

/// What a [`FakeTime`] makes of a record's time.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Relabel {
    /// The source's own.
    Keep,
    /// Tick 0 is this second and each tick the next, whatever time the
    /// source gave, or none.
    Start(GpsSeconds),
    /// The source's, moved by this much.
    Shift(Seconds<i64>),
}

/// Gives a source's records another time: a recording replayed as if
/// now, or a sky run at a date of interest.  A leap second the source
/// announces moves with the time, where the source gave one, so the
/// record's UTC offset is still the one for its time.  That is what
/// sets this apart from a scenario's
/// [`TimeStep`](crate::source::scenario::Change::TimeStep), an engine's
/// clock gone wrong under a leap second that stays where it is.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct FakeTime<S> {
    /// The source.
    inner: S,
    /// What to make of its time.
    relabel: Relabel,
}

impl<S> FakeTime<S> {
    /// `inner`, its time changed as `relabel` says.
    #[must_use]
    pub const fn new(inner: S, relabel: Relabel) -> Self {
        Self { inner, relabel }
    }

    /// The source, to read what it has of its own, as a
    /// [`Latest`](crate::source::Latest) its record's age.
    #[must_use]
    pub const fn inner(&self) -> &S {
        &self.inner
    }

    /// The source, to reach what it has of its own, as a
    /// [`Latest`](crate::source::Latest) its slot.
    pub const fn inner_mut(&mut self) -> &mut S {
        &mut self.inner
    }
}

impl<S: Source> Source for FakeTime<S> {
    fn clocking(&self) -> Clocking {
        self.inner.clocking()
    }

    fn record(&mut self, tick: Tick, record: &mut Record) -> Presence {
        if self.inner.record(tick, record) == Presence::Silent {
            return Presence::Silent;
        }
        match self.relabel {
            Relabel::Keep => Presence::Fresh,
            Relabel::Shift(shift) => {
                let Some(time) = &mut record.time else {
                    return Presence::Fresh;
                };
                let Some(seconds) = time.seconds.checked_add(shift) else {
                    return Presence::Silent;
                };
                time.seconds = seconds;
                move_leap(record, shift)
            }
            Relabel::Start(start) => {
                let Some(seconds) = start.0.checked_add(tick.0) else {
                    return Presence::Silent;
                };
                let seconds = GpsSeconds(seconds);
                let Some(time) = &mut record.time else {
                    record.time = Some(GpsTime {
                        seconds,
                        subsecond: Nanoseconds(0),
                    });
                    return Presence::Fresh;
                };
                let Some(shift) = difference(seconds.0, time.seconds.0) else {
                    return Presence::Silent;
                };
                time.seconds = seconds;
                move_leap(record, Seconds(shift))
            }
        }
    }
}

/// Moves the leap second a record announces by `shift`.
/// [`Silent`](Presence::Silent) if that is out of range.
fn move_leap(record: &mut Record, shift: Seconds<i64>) -> Presence {
    if let Some(leap) = &mut record.leap {
        let Some(at) = leap.at.checked_add(shift) else {
            return Presence::Silent;
        };
        leap.at = at;
    }
    Presence::Fresh
}

/// Announces a leap second in the records of a source that announces
/// none.  The source's UTC offset is taken to be the one before the
/// leap; from the leap's second on, the record's is the one it leaves.
///
/// A record that carries a leap second of its own is passed on as the
/// source gave it, and this one is withheld from it.  The record's
/// offset is the one in effect at its time, with its own leap second's
/// step in it once that has passed, so to take it for the one before
/// this leap would step it twice.  No wrapper can know when it is built
/// what the source it wraps will say, a recording's least of all, so
/// this is found out record by record and not refused beforehand; and
/// it is not silent: [`Self::withheld`] counts the records.  A tool
/// that set a leap second and sees that count move has a source that
/// already announces one, and can say so.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct LeapSchedule<S> {
    /// The source.
    inner: S,
    /// The leap second, if any.
    event: Option<LeapEvent>,
    /// Records it was withheld from, for having their own.
    withheld: u32,
}

impl<S> LeapSchedule<S> {
    /// `inner`, announcing `event`; with `None`, `inner` unchanged.
    #[must_use]
    pub const fn new(inner: S, event: Option<LeapEvent>) -> Self {
        Self {
            inner,
            event,
            withheld: 0,
        }
    }

    /// How many records came with a leap second of their own, and were
    /// passed on with that one and not this.  Each time a record is
    /// asked for counts.
    #[must_use]
    pub const fn withheld(&self) -> u32 {
        self.withheld
    }

    /// The source, to read what it has of its own, as a
    /// [`Latest`](crate::source::Latest) its record's age.
    #[must_use]
    pub const fn inner(&self) -> &S {
        &self.inner
    }

    /// The source, to reach what it has of its own, as a
    /// [`Latest`](crate::source::Latest) its slot.
    pub const fn inner_mut(&mut self) -> &mut S {
        &mut self.inner
    }
}

impl<S: Source> Source for LeapSchedule<S> {
    fn clocking(&self) -> Clocking {
        self.inner.clocking()
    }

    fn record(&mut self, tick: Tick, record: &mut Record) -> Presence {
        let presence = self.inner.record(tick, record);
        if presence == Presence::Fresh
            && let Some(event) = self.event
        {
            if record.leap.is_some() {
                self.withheld = self.withheld.saturating_add(1);
            } else {
                announce(record, event);
            }
        }
        presence
    }
}

/// A stretch of ticks: `from` and those after it that are before
/// `until`.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Gap {
    /// Its first tick.
    pub from: Tick,
    /// The first tick after it.
    pub until: Tick,
}

/// Silences a source through each of a table's gaps, as an engine that
/// stops talking.  The source is still asked for the ticks of a gap,
/// so one that reads a stream stays in step.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Dropouts<'a, S> {
    /// The source.
    inner: S,
    /// When it is silenced.  Borrowed, so that it can stay in flash.
    gaps: &'a [Gap],
}

impl<'a, S> Dropouts<'a, S> {
    /// `inner`, silent through `gaps`; with none, `inner` unchanged.
    #[must_use]
    pub const fn new(inner: S, gaps: &'a [Gap]) -> Self {
        Self { inner, gaps }
    }

    /// The source, to read what it has of its own, as a
    /// [`Latest`](crate::source::Latest) its record's age.
    #[must_use]
    pub const fn inner(&self) -> &S {
        &self.inner
    }

    /// The source, to reach what it has of its own, as a
    /// [`Latest`](crate::source::Latest) its slot.
    pub const fn inner_mut(&mut self) -> &mut S {
        &mut self.inner
    }
}

impl<S: Source> Source for Dropouts<'_, S> {
    fn clocking(&self) -> Clocking {
        self.inner.clocking()
    }

    fn record(&mut self, tick: Tick, record: &mut Record) -> Presence {
        let presence = self.inner.record(tick, record);
        if self
            .gaps
            .iter()
            .any(|gap| gap.from <= tick && tick < gap.until)
        {
            Presence::Silent
        } else {
            presence
        }
    }
}

#[cfg(test)]
mod tests {
    use super::Dropouts;
    use super::FakeTime;
    use super::Gap;
    use super::LeapSchedule;
    use super::Relabel;
    use crate::record::Fix;
    use crate::record::Record;
    use crate::source::Clocking;
    use crate::source::Driver;
    use crate::source::Latest;
    use crate::source::Presence;
    use crate::source::Source;
    use crate::source::Tick;
    use crate::source::admit;
    use crate::source::scenario::Change;
    use crate::source::scenario::Fixed;
    use crate::source::scenario::Scenario;
    use crate::source::scenario::Step;
    use crate::time::DateTime;
    use crate::time::GpsSeconds;
    use crate::time::GpsWeek;
    use crate::time::LEAP_SECOND;
    use crate::time::LeapChange;
    use crate::time::LeapEvent;
    use crate::time::SECONDS_PER_WEEK;
    use crate::time::TimeOfWeek;
    use crate::units::Seconds;
    use std::sync::Mutex;

    /// The time the sky was made for.
    const RECORDED: GpsSeconds = GpsSeconds::from_week(GpsWeek(1500), TimeOfWeek(1000));
    /// GPS less UTC before the leap.
    const BEFORE: Seconds<i8> = Seconds(17);
    /// The time it is run at: 30 seconds before the leap.
    const LEAP_TICK: u64 = 30;
    const LEAP: LeapEvent = LeapEvent {
        at: GpsSeconds(
            GpsSeconds::from_utc(
                DateTime::new(2016, 12, 31, 23, 59, 59).unwrap().to_unix(),
                BEFORE,
            )
            .unwrap()
            .0 + 1,
        ),
        change: LeapChange::Insert,
    };
    const RUN: GpsSeconds = GpsSeconds(LEAP.at.0 - LEAP_TICK);

    static SKY: Record = {
        let mut record = Record::EMPTY;
        record.utc_offset = Some(BEFORE);
        record.fix = Fix::ThreeD;
        record
    };
    static STEPS: [Step; 1] = [Step {
        from: Tick(20),
        change: Change::Fix(Fix::None),
    }];
    static GAPS: [Gap; 2] = [
        Gap {
            from: Tick(3),
            until: Tick(5),
        },
        Gap {
            from: Tick(40),
            until: Tick(41),
        },
    ];

    /// The nest of the next test.
    type Chain = Dropouts<'static, LeapSchedule<FakeTime<Scenario<'static>>>>;

    /// Three wrappers deep, in a `static`: built by `const` functions
    /// alone, with nowhere to allocate from.
    static CHAIN: Mutex<Chain> = Mutex::new(Dropouts::new(
        LeapSchedule::new(
            FakeTime::new(scenario(RECORDED, &STEPS), Relabel::Start(RUN)),
            Some(LEAP),
        ),
        &GAPS,
    ));

    /// The sky from `start`, changed by `steps`, which are a table that
    /// can be.
    const fn scenario(start: GpsSeconds, steps: &'static [Step]) -> Scenario<'static> {
        match Scenario::new(Fixed::new(start, &SKY), steps) {
            Ok(scenario) => scenario,
            Err(_) => panic!("the table is refused"),
        }
    }

    /// The date and time a record reads.
    fn read(record: &Record) -> DateTime {
        DateTime::of(
            record.time.unwrap().seconds,
            record.utc_offset.unwrap(),
            record.leap,
        )
        .unwrap()
    }

    #[test]
    fn wrappers_nest_three_deep_in_a_static() {
        let mut chain = CHAIN.lock().unwrap();
        // The nest borrows its tables and holds no record.
        assert!(size_of::<Chain>() < size_of::<Record>());
        assert_eq!(chain.clocking(), Clocking::Driven);
        for driver in [
            Driver::Paced,
            Driver::Free,
            Driver::Restoring,
            Driver::Sandboxed,
        ] {
            assert_eq!(admit("chain", chain.clocking(), driver), Ok(()));
        }

        let mut record = Record::EMPTY;
        let mut ask = |tick: u64| {
            let presence = chain.record(Tick(tick), &mut record);
            (presence == Presence::Fresh).then_some(record)
        };

        // The innermost layer's sky, the next one's time, the third's
        // leap second.
        let first = ask(0).unwrap();
        assert_eq!(first.fix, Fix::ThreeD);
        assert_eq!(first.time.unwrap().seconds, RUN);
        assert_eq!(first.leap, Some(LEAP));
        assert_eq!(first.utc_offset, Some(BEFORE));

        // The outermost layer's gaps, half open.
        assert!(ask(2).is_some());
        assert!(ask(3).is_none());
        assert!(ask(4).is_none());
        assert!(ask(5).is_some());
        assert!(ask(40).is_none());

        assert_eq!(ask(20).unwrap().fix, Fix::None);

        let inserted = ask(LEAP_TICK).unwrap();
        assert_eq!(inserted.utc_offset, Some(Seconds(BEFORE.0 + 1)));
        assert_eq!(
            read(&inserted),
            DateTime::new(2016, 12, 31, 23, 59, LEAP_SECOND).unwrap()
        );
        assert_eq!(ask(LEAP_TICK - 1).unwrap().utc_offset, Some(BEFORE));
        // The source had no leap second of its own in any of them.
        assert_eq!(chain.inner().withheld(), 0);
    }

    /// The same nest over a source that announces the leap second
    /// itself: the source's is the one given, the offset steps once,
    /// the inserted second still reads 23:59:60 through all three
    /// layers, and the wrapper's count says what happened.
    #[test]
    fn a_leap_second_is_not_announced_over_the_sources_own() {
        /// The tick the source first announces its leap second in.
        const ANNOUNCED: u64 = 10;
        /// The wrapper's, a week after the source's: were it applied
        /// it would show.
        const LATER: LeapEvent = LeapEvent {
            at: GpsSeconds(LEAP.at.0 + SECONDS_PER_WEEK as u64),
            change: LeapChange::Remove,
        };
        static OWN: [Step; 1] = [Step {
            from: Tick(ANNOUNCED),
            change: Change::Leap(LEAP),
        }];
        let mut chain = Dropouts::new(
            LeapSchedule::new(
                FakeTime::new(scenario(RUN, &OWN), Relabel::Shift(Seconds(0))),
                Some(LATER),
            ),
            &[],
        );
        let mut record = Record::EMPTY;

        // Until the source announces its own, the wrapper's is given.
        for tick in 0..ANNOUNCED {
            assert_eq!(chain.record(Tick(tick), &mut record), Presence::Fresh);
            assert_eq!(record.leap, Some(LATER));
        }
        assert_eq!(chain.inner().withheld(), 0);

        // From then on, the source's, and each record is counted.
        assert_eq!(chain.record(Tick(ANNOUNCED), &mut record), Presence::Fresh);
        assert_eq!(record.leap, Some(LEAP));
        assert_eq!(chain.inner().withheld(), 1);

        assert_eq!(
            chain.record(Tick(LEAP_TICK - 1), &mut record),
            Presence::Fresh
        );
        assert_eq!(record.utc_offset, Some(BEFORE));
        assert_eq!(
            read(&record),
            DateTime::new(2016, 12, 31, 23, 59, 59).unwrap()
        );

        assert_eq!(chain.record(Tick(LEAP_TICK), &mut record), Presence::Fresh);
        assert_eq!(record.leap, Some(LEAP));
        assert_eq!(record.utc_offset, Some(Seconds(BEFORE.0 + 1)));
        assert_eq!(
            read(&record),
            DateTime::new(2016, 12, 31, 23, 59, LEAP_SECOND).unwrap()
        );

        assert_eq!(
            chain.record(Tick(LEAP_TICK + 1), &mut record),
            Presence::Fresh
        );
        assert_eq!(record.utc_offset, Some(Seconds(BEFORE.0 + 1)));
        assert_eq!(read(&record), DateTime::new(2017, 1, 1, 0, 0, 0).unwrap());
        assert_eq!(chain.inner().withheld(), 4);
    }

    /// The case that would step twice: the wrapper is given the very
    /// leap second the source's records already carry, past and with
    /// its step in their offset.
    #[test]
    fn the_same_leap_second_twice_steps_the_offset_once() {
        static PAST: Record = {
            let mut record = SKY;
            record.leap = Some(LEAP);
            record.utc_offset = Some(Seconds(BEFORE.0 + 1));
            record
        };
        let mut plain = Fixed::new(LEAP.at, &PAST);
        let mut wrapped = LeapSchedule::new(plain, Some(LEAP));
        let (mut expected, mut record) = (Record::EMPTY, Record::EMPTY);
        for tick in 0..3 {
            assert_eq!(wrapped.record(Tick(tick), &mut record), Presence::Fresh);
            assert_eq!(plain.record(Tick(tick), &mut expected), Presence::Fresh);
            assert_eq!(record, expected);
            assert_eq!(record.utc_offset, Some(Seconds(BEFORE.0 + 1)));
        }
        assert_eq!(wrapped.withheld(), 3);

        // A silent second has no record to withhold it from.
        let mut silent = LeapSchedule::new(Dropouts::new(plain, &GAPS), Some(LEAP));
        assert_eq!(silent.record(Tick(3), &mut record), Presence::Silent);
        assert_eq!(silent.withheld(), 0);
    }

    /// The two ways to move a sky's time, over the same leap second.
    /// A relabeled sky takes its leap second with it and is the same
    /// sky at another date; a stepped engine clock leaves the leap
    /// second behind, and has it to come again.
    #[test]
    fn fake_time_moves_a_leap_second_and_a_time_step_does_not() {
        const BACK: i64 = 5;
        const AT: Tick = Tick(LEAP_TICK);
        static LEAP_ROW: [Step; 1] = [Step {
            from: Tick(0),
            change: Change::Leap(LEAP),
        }];
        static STEPPED: [Step; 2] = [
            LEAP_ROW[0],
            Step {
                from: Tick(0),
                change: Change::TimeStep(Seconds(-BACK)),
            },
        ];
        let moved_time = LEAP.at.checked_add(Seconds(-BACK)).unwrap();
        let mut record = Record::EMPTY;

        let mut relabeled = FakeTime::new(scenario(RUN, &LEAP_ROW), Relabel::Shift(Seconds(-BACK)));
        assert_eq!(relabeled.record(AT, &mut record), Presence::Fresh);
        assert_eq!(record.time.unwrap().seconds, moved_time);
        assert_eq!(record.leap.unwrap().at, moved_time);
        assert_eq!(record.utc_offset, Some(Seconds(BEFORE.0 + 1)));
        assert_eq!(read(&record).second, LEAP_SECOND);

        let mut stepped = scenario(RUN, &STEPPED);
        assert_eq!(stepped.record(AT, &mut record), Presence::Fresh);
        assert_eq!(record.time.unwrap().seconds, moved_time);
        assert_eq!(record.leap, Some(LEAP));
        assert_eq!(record.utc_offset, Some(BEFORE));
        assert_eq!(
            read(&record),
            DateTime::new(2016, 12, 31, 23, 59, 55).unwrap()
        );
    }

    /// Each wrapper has a setting that leaves its source as it was.
    #[test]
    fn a_wrapper_can_do_nothing() {
        let mut plain = scenario(RECORDED, &STEPS);
        let mut wrapped = Dropouts::new(
            LeapSchedule::new(FakeTime::new(plain, Relabel::Keep), None),
            &[],
        );
        let (mut expected, mut record) = (Record::EMPTY, Record::EMPTY);
        for tick in 0..50 {
            assert_eq!(
                wrapped.record(Tick(tick), &mut record),
                plain.record(Tick(tick), &mut expected)
            );
            assert_eq!(record, expected);
        }
    }

    /// A source's own leap second moves with its time, under either
    /// kind of change.
    #[test]
    fn fake_time_moves_the_leap_second_too() {
        const AHEAD: u64 = 100;
        static ANNOUNCED: Record = {
            let mut record = Record::EMPTY;
            record.leap = Some(LeapEvent {
                at: GpsSeconds(RECORDED.0 + AHEAD),
                change: LeapChange::Insert,
            });
            record
        };
        let base = Fixed::new(RECORDED, &ANNOUNCED);
        let mut record = Record::EMPTY;

        let mut started = FakeTime::new(base, Relabel::Start(RUN));
        assert_eq!(started.record(Tick(7), &mut record), Presence::Fresh);
        assert_eq!(record.time.unwrap().seconds, GpsSeconds(RUN.0 + 7));
        assert_eq!(record.leap.unwrap().at, GpsSeconds(RUN.0 + AHEAD));

        let mut shifted = FakeTime::new(base, Relabel::Shift(Seconds(-50)));
        assert_eq!(shifted.record(Tick(7), &mut record), Presence::Fresh);
        assert_eq!(record.time.unwrap().seconds, GpsSeconds(RECORDED.0 - 43));
        assert_eq!(record.leap.unwrap().at, GpsSeconds(RECORDED.0 + 50));

        let mut too_far = FakeTime::new(base, Relabel::Shift(Seconds(i64::MIN)));
        assert_eq!(too_far.record(Tick(7), &mut record), Presence::Silent);
    }

    /// A wrapper is as live as what it wraps, and gives a timeless
    /// record a time when told the start.
    #[test]
    fn a_wrapper_keeps_its_sources_clocking() {
        let mut live = Dropouts::new(
            LeapSchedule::new(FakeTime::new(Latest::new(), Relabel::Start(RUN)), None),
            &GAPS,
        );
        assert_eq!(live.clocking(), Clocking::Live);
        assert!(admit("live", live.clocking(), Driver::Free).is_err());

        let mut record = Record::EMPTY;
        assert_eq!(live.record(Tick(1), &mut record), Presence::Silent);
        let mut timed = FakeTime::new(Latest::new(), Relabel::Start(RUN));
        timed.inner_mut().commit(Tick(8));
        assert_eq!(timed.record(Tick(9), &mut record), Presence::Fresh);
        assert_eq!(record.time.unwrap().seconds, GpsSeconds(RUN.0 + 9));
        assert_eq!(timed.inner().age(Tick(9)), Some(Seconds(1)));
    }
}
