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
use crate::source::at_tick;
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
/// now, or a sky run at a date of interest.  The new time differs from
/// the source's by whole seconds; the fraction of a second is the
/// source's.
///
/// The source's leap second does not come along.  UTC has a leap
/// second only where a day ends, and a real one moved by whatever the
/// two times happen to differ by lands in the middle of a day of the
/// UTC the new time reads: 23:59:60 some afternoon.  So under a fake
/// time the leap second a record carries is dropped, the event and the
/// change it schedules both, and GPS less UTC is frozen at what it was
/// at the fake start, tick 0: a record whose leap second has passed
/// since then has that step taken back out of its offset.  The source's
/// time at tick 0 is taken to be a record's time less its tick.  A
/// scenario's [`TimeStep`](crate::source::scenario::Change::TimeStep)
/// is the other thing, an engine's clock gone wrong under a leap second
/// that stays where it is.
///
/// A leap second under a fake time is one scheduled in that time, by a
/// [`LeapSchedule`] around this wrapper; [`LeapEvent::at_day_end`]
/// makes one where a day of it ends.  A schedule or a scenario's leap
/// row inside this wrapper is the source's, and dropped with the rest.
/// What the source will say cannot be known when the wrapper is built,
/// so nothing is refused then, and nothing is dropped unseen:
/// [`Self::dropped`] counts the records.
///
/// [`Relabel::Keep`] changes nothing, the leap second included.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct FakeTime<S> {
    /// The source.
    inner: S,
    /// What to make of its time.
    relabel: Relabel,
    /// Records a leap second of the source's was dropped from.
    dropped: u32,
}

impl<S> FakeTime<S> {
    /// `inner`, its time changed as `relabel` says.
    #[must_use]
    pub const fn new(inner: S, relabel: Relabel) -> Self {
        Self {
            inner,
            relabel,
            dropped: 0,
        }
    }

    /// How many records came with a leap second of the source's, and
    /// were passed on without it.  Each time a record is asked for
    /// counts.
    #[must_use]
    pub const fn dropped(&self) -> u32 {
        self.dropped
    }

    /// Takes the source's leap second out of the record of `tick`, and
    /// its step out of the offset if it has passed since tick 0.  This
    /// is done before the time is changed: the event is in the source's
    /// time.
    fn freeze(&mut self, tick: Tick, record: &mut Record) {
        let Some(event) = record.leap.take() else {
            return;
        };
        self.dropped = self.dropped.saturating_add(1);
        let (Some(time), Some(offset)) = (record.time, record.utc_offset) else {
            return;
        };
        if let Ok(ticks) = i64::try_from(tick.0)
            && let Some(start) = time.seconds.checked_sub(Seconds(ticks))
            && event.pending(start)
        {
            record.utc_offset = Some(event.offset_before(time.seconds, offset));
        }
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
            Relabel::Keep => {}
            Relabel::Shift(shift) => {
                self.freeze(tick, record);
                if let Some(time) = &mut record.time {
                    let Some(seconds) = time.seconds.checked_add(shift) else {
                        return Presence::Silent;
                    };
                    time.seconds = seconds;
                }
            }
            Relabel::Start(start) => {
                self.freeze(tick, record);
                let Some(seconds) = at_tick(start, tick) else {
                    return Presence::Silent;
                };
                match &mut record.time {
                    Some(time) => time.seconds = seconds,
                    None => {
                        record.time = Some(GpsTime {
                            seconds,
                            subsecond: Nanoseconds(0),
                        });
                    }
                }
            }
        }
        Presence::Fresh
    }
}

/// Announces a leap second in the records of a source that announces
/// none.  The source's UTC offset is taken to be the one before the
/// leap; from the leap's second on, the record's is the one it leaves.
/// The event is in the time of the records this wrapper is given: around
/// a [`FakeTime`], the fake time, and that is the one way to have a leap
/// second under it.
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
    const RECORDED: GpsSeconds =
        GpsSeconds::from_week(GpsWeek::new(1500), TimeOfWeek::new(1000).unwrap());
    /// GPS less UTC before the leap.
    const BEFORE: Seconds<i8> = Seconds(17);
    /// The time it is run at: 30 seconds before the leap.
    const LEAP_TICK: u64 = 30;
    const LEAP: LeapEvent = LeapEvent::at_day_end(
        DateTime::new(2017, 1, 1, 0, 0, 0).unwrap().to_unix(),
        BEFORE,
        LeapChange::Insert,
    )
    .unwrap();
    const RUN: GpsSeconds = LEAP
        .at
        .checked_sub(Seconds(LEAP_TICK.cast_signed()))
        .unwrap();

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
    /// A source that announces the leap second itself.
    static LEAP_ROW: [Step; 1] = [Step {
        from: Tick(0),
        change: Change::Leap(LEAP),
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
            at: LEAP
                .at
                .checked_add(Seconds(SECONDS_PER_WEEK as i64))
                .unwrap(),
            change: LeapChange::Remove,
        };
        static OWN: [Step; 1] = [Step {
            from: Tick(ANNOUNCED),
            change: Change::Leap(LEAP),
        }];
        let mut chain = Dropouts::new(
            LeapSchedule::new(
                FakeTime::new(scenario(RUN, &OWN), Relabel::Keep),
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
    /// A relabeled sky loses its leap second, event and step, and reads
    /// a plain second; a stepped engine clock keeps the leap second
    /// where it was, and has it to come again.
    #[test]
    fn fake_time_drops_a_leap_second_and_a_time_step_keeps_it() {
        const BACK: i64 = 5;
        const AT: Tick = Tick(LEAP_TICK);
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
        assert_eq!(record.leap, None);
        assert_eq!(record.utc_offset, Some(BEFORE));
        assert_eq!(
            read(&record),
            DateTime::new(2016, 12, 31, 23, 59, 55).unwrap()
        );
        assert_eq!(relabeled.dropped(), 1);

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

    /// GPS less UTC under a fake time is the one at the fake start,
    /// through the source's leap second and after it, under either kind
    /// of change, and no second reads 23:59:60.
    #[test]
    fn fake_time_freezes_the_leap_count() {
        /// Noon on a day with no leap second.
        const FAKE: GpsSeconds = GpsSeconds::from_utc(
            DateTime::new(2020, 3, 4, 12, 0, 0).unwrap().to_unix(),
            BEFORE,
        )
        .unwrap();
        const LAST: u64 = LEAP_TICK + 5;
        let shift = FAKE.checked_since(RUN).unwrap();
        let mut started = FakeTime::new(scenario(RUN, &LEAP_ROW), Relabel::Start(FAKE));
        let mut shifted = FakeTime::new(scenario(RUN, &LEAP_ROW), Relabel::Shift(shift));
        let mut record = Record::EMPTY;
        for tick in 0..=LAST {
            for source in [&mut started, &mut shifted] {
                assert_eq!(source.record(Tick(tick), &mut record), Presence::Fresh);
                assert_eq!(record.utc_offset, Some(BEFORE));
                assert_eq!(record.leap, None);
                assert_eq!(
                    read(&record),
                    DateTime::new(2020, 3, 4, 12, 0, u8::try_from(tick).unwrap()).unwrap()
                );
            }
        }
        assert_eq!(started.dropped(), u32::try_from(LAST).unwrap() + 1);

        // Any second can be asked for in any order.
        assert_eq!(shifted.record(Tick(0), &mut record), Presence::Fresh);
        assert_eq!(record.utc_offset, Some(BEFORE));

        // A leap second that had passed before the fake start is in
        // the count at the start, and stays in it.
        let after = Seconds(BEFORE.0 + 1);
        let mut late = FakeTime::new(scenario(LEAP.at, &LEAP_ROW), Relabel::Start(FAKE));
        assert_eq!(late.record(Tick(3), &mut record), Presence::Fresh);
        assert_eq!(record.utc_offset, Some(after));
        assert_eq!(record.leap, None);
    }

    /// The source's leap second is dropped whole: the event, and with
    /// it the change it schedules, whether or not the record has a time
    /// or an offset to freeze, and each record is counted.  A source
    /// that announces none is not counted.
    #[test]
    fn fake_time_drops_the_sources_leap_second() {
        static ANNOUNCED: Record = {
            let mut record = Record::EMPTY;
            record.leap = Some(LEAP);
            record
        };
        let mut record = Record::EMPTY;

        let mut bare = FakeTime::new(Fixed::new(RUN, &ANNOUNCED), Relabel::Start(RECORDED));
        assert_eq!(bare.record(Tick(LEAP_TICK), &mut record), Presence::Fresh);
        assert_eq!(record.leap, None);
        assert_eq!(record.utc_offset, None);
        assert_eq!(bare.dropped(), 1);

        let mut kept = FakeTime::new(Fixed::new(RUN, &ANNOUNCED), Relabel::Keep);
        assert_eq!(kept.record(Tick(LEAP_TICK), &mut record), Presence::Fresh);
        assert_eq!(record.leap, Some(LEAP));
        assert_eq!(kept.dropped(), 0);

        let mut plain = FakeTime::new(scenario(RUN, &STEPS), Relabel::Start(RECORDED));
        assert_eq!(plain.record(Tick(LEAP_TICK), &mut record), Presence::Fresh);
        assert_eq!(plain.dropped(), 0);
    }

    /// A leap second under a fake time is the one scheduled in it, by
    /// the wrapper around: honored where it says, over a source whose
    /// own leap second, at another second, is dropped and so does not
    /// have this one withheld.
    #[test]
    fn a_leap_second_scheduled_in_fake_time_is_honored() {
        /// Ticks from the fake start to the scheduled leap second.
        const SCHEDULED: u64 = 10;
        const FAKE_LEAP: LeapEvent = LeapEvent::at_day_end(
            DateTime::new(2015, 7, 1, 0, 0, 0).unwrap().to_unix(),
            BEFORE,
            LeapChange::Insert,
        )
        .unwrap();
        const FAKE: GpsSeconds = FAKE_LEAP
            .at
            .checked_sub(Seconds(SCHEDULED.cast_signed()))
            .unwrap();
        let after = Seconds(BEFORE.0 + 1);
        let mut source = LeapSchedule::new(
            FakeTime::new(scenario(RUN, &LEAP_ROW), Relabel::Start(FAKE)),
            Some(FAKE_LEAP),
        );
        let mut record = Record::EMPTY;

        assert_eq!(
            source.record(Tick(SCHEDULED - 1), &mut record),
            Presence::Fresh
        );
        assert_eq!(record.leap, Some(FAKE_LEAP));
        assert_eq!(record.utc_offset, Some(BEFORE));
        assert_eq!(
            read(&record),
            DateTime::new(2015, 6, 30, 23, 59, 59).unwrap()
        );

        assert_eq!(source.record(Tick(SCHEDULED), &mut record), Presence::Fresh);
        assert_eq!(record.utc_offset, Some(after));
        assert_eq!(
            read(&record),
            DateTime::new(2015, 6, 30, 23, 59, LEAP_SECOND).unwrap()
        );

        // Through the source's own leap second, nothing more happens.
        for (tick, second) in [(LEAP_TICK - 1, 18), (LEAP_TICK, 19), (LEAP_TICK + 1, 20)] {
            assert_eq!(source.record(Tick(tick), &mut record), Presence::Fresh);
            assert_eq!(record.leap, Some(FAKE_LEAP));
            assert_eq!(record.utc_offset, Some(after));
            assert_eq!(
                read(&record),
                DateTime::new(2015, 7, 1, 0, 0, second).unwrap()
            );
        }
        assert_eq!(source.withheld(), 0);
        assert_eq!(source.inner().dropped(), 5);
    }

    /// A time can be moved back past the start of GPS time, by whole
    /// seconds; one moved out of the count is silence.
    #[test]
    fn fake_time_moves_a_time_before_gps_time() {
        const BEHIND: Seconds<i64> = Seconds(43);
        let tick = Tick(7);
        let back = GpsSeconds::EPOCH
            .checked_since(RECORDED + Seconds(50))
            .unwrap();
        assert!(back < Seconds(0));
        let mut record = Record::EMPTY;

        let mut shifted = FakeTime::new(scenario(RECORDED, &LEAP_ROW), Relabel::Shift(back));
        assert_eq!(shifted.record(tick, &mut record), Presence::Fresh);
        let time = record.time.unwrap().seconds;
        assert_eq!(time, GpsSeconds::EPOCH - BEHIND);
        assert_eq!(time.week(), None);
        assert_eq!(record.leap, None);
        assert_eq!(record.utc_offset, Some(BEFORE));
        assert_eq!(read(&record), DateTime::new(1980, 1, 5, 23, 59, 0).unwrap());

        let start = GpsSeconds::EPOCH - BEHIND - Seconds(7);
        let mut started = FakeTime::new(scenario(RECORDED, &LEAP_ROW), Relabel::Start(start));
        assert_eq!(started.record(tick, &mut record), Presence::Fresh);
        assert_eq!(record.time.unwrap().seconds, time);

        let last = GpsSeconds::from_epoch(Seconds(i64::MAX));
        for too_far in [Relabel::Shift(Seconds(i64::MAX)), Relabel::Start(last)] {
            let mut source = FakeTime::new(scenario(RECORDED, &STEPS), too_far);
            assert_eq!(source.record(tick, &mut record), Presence::Silent);
        }
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
        assert_eq!(record.time.unwrap().seconds, RUN + Seconds(9));
        assert_eq!(timed.inner().age(Tick(9)), Some(Seconds(1)));
    }
}
