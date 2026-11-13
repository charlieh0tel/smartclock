//! Sources made up from a table: a sky that never changes, and one
//! that changes by a script.
//!
//! Both are [`Driven`](Clocking::Driven): a tick's record is worked out
//! from the tick alone, so any second can be asked for at any time and
//! comes out the same.

use crate::record::Antenna;
use crate::record::Fix;
use crate::record::GpsTime;
use crate::record::Position;
use crate::record::Record;
use crate::record::Timing;
use crate::record::Tracking;
use crate::source::Clocking;
use crate::source::Presence;
use crate::source::Source;
use crate::source::Tick;
use crate::source::announce;
use crate::time::GpsSeconds;
use crate::time::LeapEvent;
use crate::units::DecibelHertz;
use crate::units::Nanoseconds;
use crate::units::Prn;
use crate::units::Seconds;
use thiserror::Error;

/// One sky for ever: every second's record is `sky`, at the time of
/// its tick.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Fixed<'a> {
    /// The time of tick 0.
    start: GpsSeconds,
    /// The record of every second, but for its time.  Borrowed, so that
    /// it can stay in flash.
    sky: &'a Record,
}

impl<'a> Fixed<'a> {
    /// `sky` each second, tick 0 being `start`.
    #[must_use]
    pub const fn new(start: GpsSeconds, sky: &'a Record) -> Self {
        Self { start, sky }
    }

    /// Fills in the record of `tick`: [`Silent`](Presence::Silent) only
    /// past the end of the count of GPS seconds.
    const fn fill(&self, tick: Tick, record: &mut Record) -> Presence {
        let Some(seconds) = self.start.0.checked_add(tick.0) else {
            return Presence::Silent;
        };
        *record = *self.sky;
        record.time = Some(GpsTime {
            seconds: GpsSeconds(seconds),
            subsecond: Nanoseconds(0),
        });
        Presence::Fresh
    }
}

impl Source for Fixed<'_> {
    fn clocking(&self) -> Clocking {
        Clocking::Driven
    }

    fn record(&mut self, tick: Tick, record: &mut Record) -> Presence {
        self.fill(tick, record)
    }
}

/// What a [`Step`] changes in the record, from its tick on.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[non_exhaustive]
pub enum Change {
    /// A leap second is announced.  It is worked out after every other
    /// row in force, wherever in the table its own is: the UTC offset
    /// they leave is taken to be the one before the leap, and from the
    /// leap's second on, by the time they leave, the record's is the
    /// one the leap leaves.  One row of a table at most can be this,
    /// and none where the sky carries a leap second of its own
    /// ([`TableError`]).
    Leap(LeapEvent),
    /// The UTC offset is this: with a [`Self::Leap`] in force, the one
    /// before that leap.
    UtcOffset(Option<Seconds<i8>>),
    /// The time is moved by this much: the engine's clock steps.  A
    /// leap second stays where it is, as UTC's does when an engine's
    /// clock is wrong, and the UTC offset is the one for the stepped
    /// time: an engine stepped back across a leap second has it still
    /// to come.  [`FakeTime`](crate::source::wrap::FakeTime) is the
    /// other thing, a sky moved whole to another date, and moves the
    /// leap second with it.
    TimeStep(Seconds<i64>),
    /// The fix is of this kind.
    Fix(Fix),
    /// The position is this.
    Position(Option<Position>),
    /// The time solution's quality is this.
    Timing(Option<Timing>),
    /// A satellite's signal is this.  With none it is searched for and
    /// not used; with one it is tracked and used.
    Signal {
        /// Which satellite.
        prn: Prn,
        /// Its signal.
        signal: Option<DecibelHertz>,
    },
    /// A satellite leaves the sky.
    Set {
        /// Which satellite.
        prn: Prn,
    },
    /// The antenna's feed is this.
    Antenna(Antenna),
}

impl Change {
    /// Makes the change.  `false` if it takes the time out of range.
    fn apply(self, record: &mut Record) -> bool {
        match self {
            // Announced by `Scenario::record`, after every other row.
            Self::Leap(_) => {}
            Self::UtcOffset(offset) => record.utc_offset = offset,
            Self::TimeStep(shift) => {
                if let Some(time) = &mut record.time {
                    let Some(seconds) = time.seconds.checked_add(shift) else {
                        return false;
                    };
                    time.seconds = seconds;
                }
            }
            Self::Fix(fix) => record.fix = fix,
            Self::Position(position) => record.position = position,
            Self::Timing(timing) => record.timing = timing,
            Self::Signal { prn, signal } => {
                if let Some(satellite) = record.satellites.get_mut(prn) {
                    satellite.signal = signal;
                    satellite.used = signal.is_some();
                    satellite.tracking = if signal.is_some() {
                        Tracking::Position
                    } else {
                        Tracking::Searching
                    };
                }
            }
            Self::Set { prn } => {
                record.satellites.remove(prn);
            }
            Self::Antenna(antenna) => record.antenna = antenna,
        }
        true
    }
}

/// One row of a scenario: a change, and the tick it holds from.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Step {
    /// The first tick the change shows in.
    pub from: Tick,
    /// The change.
    pub change: Change,
}

/// Why a table cannot change a sky.  A record's UTC offset is the one
/// in effect at its time, with the step of the leap second it carries
/// already in it once that has passed, so a leap second announced over
/// another would step the offset a second time.  Rows are counted from
/// 0.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Error)]
pub enum TableError {
    /// A row announces a leap second, and the sky carries one.
    #[error("row {row} announces a leap second, and the sky it changes carries one already")]
    LeapOverSky {
        /// The row.
        row: usize,
    },
    /// A row announces a leap second, and an earlier row has.
    #[error("row {row} announces a leap second, and row {first} has announced one already")]
    LeapOverLeap {
        /// The earlier row.
        first: usize,
        /// The later.
        row: usize,
    },
}

/// A sky that changes by a table.  A tick's record is the base's with
/// the change of every row whose tick has come, made in the table's
/// order, so a later row has the last word; but for the leap second,
/// which is worked out last ([`Change::Leap`]).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Scenario<'a> {
    /// The sky before any change.
    base: Fixed<'a>,
    /// The changes.  Borrowed, so that they can stay in flash.
    steps: &'a [Step],
    /// The row of the table that announces a leap second, if one does.
    leap: Option<usize>,
}

impl<'a> Scenario<'a> {
    /// `base`, changed by `steps`.
    ///
    /// A table that would put a second leap second into a record is
    /// refused here, where a `static`'s initializer can fail the build
    /// on it, and not found out second by second.
    ///
    /// # Errors
    ///
    /// [`TableError::LeapOverSky`] if a row announces a leap second and
    /// `base`'s sky carries one; [`TableError::LeapOverLeap`] if two
    /// rows do.
    pub const fn new(base: Fixed<'a>, steps: &'a [Step]) -> Result<Self, TableError> {
        let mut leap = None;
        let mut row = 0;
        while row < steps.len() {
            if matches!(steps[row].change, Change::Leap(_)) {
                if base.sky.leap.is_some() {
                    return Err(TableError::LeapOverSky { row });
                }
                if let Some(first) = leap {
                    return Err(TableError::LeapOverLeap { first, row });
                }
                leap = Some(row);
            }
            row += 1;
        }
        Ok(Self { base, steps, leap })
    }
}

impl Source for Scenario<'_> {
    fn clocking(&self) -> Clocking {
        Clocking::Driven
    }

    fn record(&mut self, tick: Tick, record: &mut Record) -> Presence {
        if self.base.fill(tick, record) == Presence::Silent {
            return Presence::Silent;
        }
        let in_range = self
            .steps
            .iter()
            .filter(|step| step.from <= tick)
            .all(|step| step.change.apply(record));
        if !in_range {
            return Presence::Silent;
        }
        if let Some(row) = self.leap
            && let Step {
                from,
                change: Change::Leap(event),
            } = self.steps[row]
            && from <= tick
        {
            announce(record, event);
        }
        Presence::Fresh
    }
}

#[cfg(test)]
mod tests {
    use super::Change;
    use super::Fixed;
    use super::Scenario;
    use super::Step;
    use super::TableError;
    use crate::record::Antenna;
    use crate::record::Fix;
    use crate::record::Record;
    use crate::record::Satellite;
    use crate::record::Satellites;
    use crate::record::Tracking;
    use crate::source::Clocking;
    use crate::source::Presence;
    use crate::source::Source;
    use crate::source::Tick;
    use crate::time::DateTime;
    use crate::time::GpsSeconds;
    use crate::time::LEAP_SECOND;
    use crate::time::LeapChange;
    use crate::time::LeapEvent;
    use crate::units::DecibelHertz;
    use crate::units::Degrees;
    use crate::units::Prn;
    use crate::units::Seconds;
    use std::string::ToString;

    const HIGH: Prn = Prn::new(7).unwrap();
    const LOW: Prn = Prn::new(22).unwrap();
    /// GPS less UTC through 1998.
    const BEFORE: Seconds<i8> = Seconds(12);
    /// The second inserted at the end of 1998.
    const LEAP: LeapEvent = LeapEvent {
        at: GpsSeconds(
            GpsSeconds::from_utc(
                DateTime::new(1998, 12, 31, 23, 59, 59).unwrap().to_unix(),
                BEFORE,
            )
            .unwrap()
            .0 + 1,
        ),
        change: LeapChange::Insert,
    };
    /// Tick 60 is the leap second.
    const LEAP_TICK: u64 = 60;
    const START: GpsSeconds = GpsSeconds(LEAP.at.0 - LEAP_TICK);
    /// The tick the leap second is first announced in.
    const ANNOUNCED: u64 = 10;

    static SKY: Record = {
        let mut record = Record::EMPTY;
        record.utc_offset = Some(BEFORE);
        record.fix = Fix::ThreeD;
        record.antenna = Antenna::Connected;
        record.satellites = Satellites::from_slice(&[
            Satellite::new(HIGH, Degrees(70), Degrees(120)).tracked(DecibelHertz(48)),
            Satellite::new(LOW, Degrees(15), Degrees(300)).tracked(DecibelHertz(38)),
        ]);
        record
    };

    static STEPS: [Step; 6] = [
        Step {
            from: Tick(ANNOUNCED),
            change: Change::Leap(LEAP),
        },
        Step {
            from: Tick(20),
            change: Change::Signal {
                prn: LOW,
                signal: None,
            },
        },
        Step {
            from: Tick(30),
            change: Change::Signal {
                prn: LOW,
                signal: Some(DecibelHertz(35)),
            },
        },
        Step {
            from: Tick(40),
            change: Change::Set { prn: LOW },
        },
        Step {
            from: Tick(40),
            change: Change::Fix(Fix::TwoD),
        },
        Step {
            from: Tick(90),
            change: Change::TimeStep(Seconds(-5)),
        },
    ];

    /// The sky changed by `steps`, which are a table that can be.
    const fn scenario(steps: &'static [Step]) -> Scenario<'static> {
        match Scenario::new(Fixed::new(START, &SKY), steps) {
            Ok(scenario) => scenario,
            Err(_) => panic!("the table is refused"),
        }
    }

    fn at(source: &mut impl Source, tick: u64) -> Record {
        let mut record = Record::EMPTY;
        assert_eq!(source.record(Tick(tick), &mut record), Presence::Fresh);
        record
    }

    fn read(record: &Record) -> (u8, u8, u8) {
        let date = DateTime::of(
            record.time.unwrap().seconds,
            record.utc_offset.unwrap(),
            record.leap,
        )
        .unwrap();
        (date.hour, date.minute, date.second)
    }

    #[test]
    fn a_fixed_sky_changes_only_in_time() {
        let mut fixed = Fixed::new(START, &SKY);
        assert_eq!(fixed.clocking(), Clocking::Driven);
        let first = at(&mut fixed, 0);
        let later = at(&mut fixed, 1000);
        assert_eq!(first.time.unwrap().seconds, START);
        assert_eq!(later.time.unwrap().seconds, GpsSeconds(START.0 + 1000));
        let mut expected = SKY;
        expected.time = later.time;
        assert_eq!(later, expected);

        let mut record = Record::EMPTY;
        assert_eq!(fixed.record(Tick(u64::MAX), &mut record), Presence::Silent);
    }

    /// An inserted leap second, from before its announcement to after
    /// it: the seconds read 23:59:59, 23:59:60, 00:00:00, and the offset
    /// steps on the inserted one.
    #[test]
    fn a_scenario_inserts_a_leap_second() {
        let mut scenario = scenario(&STEPS);

        let unannounced = at(&mut scenario, ANNOUNCED - 1);
        assert_eq!(unannounced.leap, None);
        assert_eq!(unannounced.utc_offset, Some(BEFORE));

        let announced = at(&mut scenario, ANNOUNCED);
        assert_eq!(announced.leap, Some(LEAP));
        assert!(LEAP.pending(announced.time.unwrap().seconds));
        assert_eq!(announced.utc_offset, Some(BEFORE));

        let last = at(&mut scenario, LEAP_TICK - 1);
        assert_eq!(read(&last), (23, 59, 59));
        assert_eq!(last.utc_offset, Some(BEFORE));

        let inserted = at(&mut scenario, LEAP_TICK);
        assert_eq!(read(&inserted), (23, 59, LEAP_SECOND));
        assert_eq!(inserted.utc_offset, Some(Seconds(BEFORE.0 + 1)));
        assert!(!LEAP.pending(inserted.time.unwrap().seconds));

        let after = at(&mut scenario, LEAP_TICK + 1);
        assert_eq!(read(&after), (0, 0, 0));
        assert_eq!(after.utc_offset, Some(Seconds(BEFORE.0 + 1)));
        assert_eq!(after.leap, Some(LEAP));
    }

    #[test]
    fn a_scenario_changes_the_sky_by_its_table() {
        let mut scenario = scenario(&STEPS);

        let lost = at(&mut scenario, 20);
        let low = lost.satellites.get(LOW).unwrap();
        assert_eq!((low.signal, low.used), (None, false));
        assert_eq!(low.tracking, Tracking::Searching);
        assert!(lost.satellites.get(HIGH).unwrap().used);

        let back = at(&mut scenario, 30);
        let low = back.satellites.get(LOW).unwrap();
        assert_eq!((low.signal, low.used), (Some(DecibelHertz(35)), true));
        assert_eq!(back.fix, Fix::ThreeD);

        let gone = at(&mut scenario, 40);
        assert!(gone.satellites.get(LOW).is_none());
        assert_eq!(gone.satellites.len(), 1);
        assert_eq!(gone.fix, Fix::TwoD);

        let stepped = at(&mut scenario, 90);
        assert_eq!(stepped.time.unwrap().seconds, GpsSeconds(START.0 + 85));
    }

    /// Any second, in any order, any number of times: the same record.
    #[test]
    fn a_scenario_is_the_same_whenever_asked() {
        let mut scenario = scenario(&STEPS);
        let forward: std::vec::Vec<Record> = (0..100).map(|tick| at(&mut scenario, tick)).collect();
        for tick in (0..100).rev() {
            assert_eq!(
                at(&mut scenario, tick),
                forward[usize::try_from(tick).unwrap()]
            );
        }
    }

    #[test]
    fn a_step_out_of_the_count_is_silence() {
        static BACK: [Step; 1] = [Step {
            from: Tick(1),
            change: Change::TimeStep(Seconds(-10)),
        }];
        let mut scenario = Scenario::new(Fixed::new(GpsSeconds(0), &SKY), &BACK).unwrap();
        let mut record = Record::EMPTY;
        assert_eq!(scenario.record(Tick(0), &mut record), Presence::Fresh);
        assert_eq!(scenario.record(Tick(1), &mut record), Presence::Silent);
        assert_eq!(scenario.record(Tick(10), &mut record), Presence::Fresh);
    }

    /// A leap second over another is refused where a constant is, with
    /// the rows at fault.
    #[test]
    fn a_second_leap_second_is_refused_when_built() {
        /// A sky that carries the leap second itself, already past: its
        /// offset has the step in it.
        static LEAPT: Record = {
            let mut record = SKY;
            record.leap = Some(LEAP);
            record.utc_offset = Some(Seconds(BEFORE.0 + 1));
            record
        };
        static TWICE: [Step; 3] = [
            STEPS[0],
            Step {
                from: Tick(20),
                change: Change::Fix(Fix::TwoD),
            },
            Step {
                from: Tick(30),
                change: Change::Leap(LEAP),
            },
        ];
        const OVER_SKY: Result<Scenario<'static>, TableError> =
            Scenario::new(Fixed::new(START, &LEAPT), &TWICE);
        const OVER_LEAP: Result<Scenario<'static>, TableError> =
            Scenario::new(Fixed::new(START, &SKY), &TWICE);

        assert_eq!(OVER_SKY, Err(TableError::LeapOverSky { row: 0 }));
        assert_eq!(
            OVER_SKY.unwrap_err().to_string(),
            "row 0 announces a leap second, and the sky it changes carries one already"
        );
        assert_eq!(
            OVER_LEAP,
            Err(TableError::LeapOverLeap { first: 0, row: 2 })
        );
        assert_eq!(
            OVER_LEAP.unwrap_err().to_string(),
            "row 2 announces a leap second, and row 0 has announced one already"
        );

        // The sky's own leap second, with no row to add another, is
        // given as it is: the offset is not stepped again.
        let mut own = Scenario::new(Fixed::new(START, &LEAPT), &TWICE[1..2]).unwrap();
        let record = at(&mut own, LEAP_TICK + 1);
        assert_eq!(record.leap, Some(LEAP));
        assert_eq!(record.utc_offset, Some(Seconds(BEFORE.0 + 1)));
    }

    /// The engine's clock stepped back across the leap second: the leap
    /// second is where it was, the offset is the one for the stepped
    /// time, and so the inserted second comes when the stepped time
    /// reaches it.  The rows' order in the table does not matter.
    #[test]
    fn a_time_step_leaves_the_leap_second_where_it_is() {
        const BACK: i64 = 5;
        const STEPPED: u64 = LEAP_TICK - 2;
        const LEAP_ROW: Step = Step {
            from: Tick(ANNOUNCED),
            change: Change::Leap(LEAP),
        };
        const STEP_ROW: Step = Step {
            from: Tick(STEPPED),
            change: Change::TimeStep(Seconds(-BACK)),
        };
        static LEAP_FIRST: [Step; 2] = [LEAP_ROW, STEP_ROW];
        static STEP_FIRST: [Step; 2] = [STEP_ROW, LEAP_ROW];
        let mut leap_first = scenario(&LEAP_FIRST);
        let mut step_first = scenario(&STEP_FIRST);

        for tick in 0..2 * LEAP_TICK {
            assert_eq!(at(&mut leap_first, tick), at(&mut step_first, tick));
        }

        let before = at(&mut leap_first, STEPPED - 1);
        assert_eq!(read(&before), (23, 59, 57));

        // The tick that would have been the leap second is five seconds
        // short of it.
        let short = at(&mut leap_first, LEAP_TICK);
        assert_eq!(short.leap, Some(LEAP));
        assert!(LEAP.pending(short.time.unwrap().seconds));
        assert_eq!(short.utc_offset, Some(BEFORE));
        assert_eq!(read(&short), (23, 59, 55));

        let inserted = at(&mut leap_first, LEAP_TICK + BACK.cast_unsigned());
        assert_eq!(inserted.time.unwrap().seconds, LEAP.at);
        assert_eq!(inserted.utc_offset, Some(Seconds(BEFORE.0 + 1)));
        assert_eq!(read(&inserted), (23, 59, LEAP_SECOND));
    }

    /// An offset set by a row after the leap second's is still the one
    /// before the leap.
    #[test]
    fn a_leap_second_is_worked_out_after_the_other_rows() {
        const SET: Seconds<i8> = Seconds(BEFORE.0 - 1);
        static OFFSET_LAST: [Step; 2] = [
            STEPS[0],
            Step {
                from: Tick(ANNOUNCED),
                change: Change::UtcOffset(Some(SET)),
            },
        ];
        let mut scenario = scenario(&OFFSET_LAST);
        assert_eq!(at(&mut scenario, LEAP_TICK - 1).utc_offset, Some(SET));
        assert_eq!(
            at(&mut scenario, LEAP_TICK).utc_offset,
            Some(Seconds(SET.0 + 1))
        );
    }
}
