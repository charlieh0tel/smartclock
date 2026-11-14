//! GPS time, UTC and the calendar, in integers.
//!
//! GPS time is a continuous count of seconds from 0000 UTC on January
//! 6, 1980 (Furuno's GT-8031 protocol specification,
//! `third_party/furuno/Furuno_GT8031_Protocol_Specification_Rev1.pdf`,
//! p. 37 as printed), and [`GpsSeconds`] is that count.  UTC falls
//! behind it by a second at each leap second, so a date and time of day
//! come from a count only with the offset between the two, and around a
//! leap second only with the [`LeapEvent`] as well: [`DateTime::of`].
//!
//! Both counts are signed.  A clock set behind UTC reads a date before
//! the start of GPS time in the first hours after it, which is where an
//! engine with no time starts its own, and the difference of two times
//! is as often negative as not.  Neither count's number can be reached
//! from outside: a time is made from a calendar date, a week and a time
//! of week, or a typed count of seconds, and two of them differ by a
//! [`Seconds`], so that a GPS second is not taken for a UTC one, nor a
//! time for a length of time.
//!
//! Nothing here reads a clock.  Every named function is `const`, so a
//! start time or a leap second can be worked out in a `static`'s
//! initializer; the operators are the same arithmetic for code that
//! runs, and stop at the count's ends where the `checked_` functions
//! give `None`.

use crate::units::Deciseconds;
use crate::units::Milliseconds;
use crate::units::Nanoseconds;
use crate::units::Seconds;
use core::ops::Add;
use core::ops::AddAssign;
use core::ops::Sub;
use core::ops::SubAssign;

pub(crate) const SECONDS_PER_MINUTE: i64 = 60;
const MINUTES_PER_HOUR: i64 = 60;
const HOURS_PER_DAY: i64 = 24;
const SECONDS_PER_HOUR: i64 = SECONDS_PER_MINUTE * MINUTES_PER_HOUR;
const SECONDS_PER_DAY: i64 = SECONDS_PER_HOUR * HOURS_PER_DAY;
const DAYS_PER_WEEK: u32 = 7;
const MONTHS_PER_YEAR: u8 = 12;
const MILLISECONDS_PER_SECOND: u32 = 1000;
const NANOSECONDS_PER_MILLISECOND: i64 = 1_000_000;
const NANOSECONDS_PER_SECOND: i64 = 1_000_000_000;
const DECISECONDS_PER_SECOND: u32 = 10;

/// The seconds in a week: a time of week is less than this
/// (GT-8031 specification p. 38: 000000 to 604799).
#[expect(clippy::cast_possible_truncation, reason = "86400 fits")]
pub const SECONDS_PER_WEEK: u32 = SECONDS_PER_DAY as u32 * DAYS_PER_WEEK;

/// The weeks a satellite's 10-bit week number counts before it returns
/// to 0 (Furuno's week number rollover note,
/// `third_party/furuno/GNSS_Receiver_WeekNumberRollover_SE18-100-034-00_en.pdf`,
/// p. 1 as printed).
pub const BROADCAST_WEEKS: u16 = 1024;

/// The weeks an almanac's week number counts before it returns to 0
/// (`@@Bd`, `third_party/motorola/VPCommands.pdf` p. 79: 0 to 255).
pub const ALMANAC_WEEKS: u16 = 256;

/// The value of a time's seconds field in an inserted leap second:
/// 23:59:60 (VP Oncore command reference p. 71: seconds 0 to 60).
pub const LEAP_SECOND: u8 = 60;

/// Seconds of UTC since 1970-01-01 00:00:00, leap seconds not counted:
/// every day is 86400 long, so the count has no place of its own for an
/// inserted leap second.  Negative before 1970.
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct UnixSeconds(i64);

/// Seconds of GPS time since its start, with no leap seconds and no
/// rollover.  Negative before its start.
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct GpsSeconds(i64);

/// What a count of seconds from a start can do, the same for each.
macro_rules! count_of_seconds {
    ($count:ident) => {
        impl $count {
            /// The count's start.
            pub const EPOCH: Self = Self(0);

            /// The second `since` after the count's start, or before it.
            #[must_use]
            pub const fn from_epoch(since: Seconds<i64>) -> Self {
                Self(since.0)
            }

            /// How long after the count's start this second is.
            #[must_use]
            pub const fn since_epoch(self) -> Seconds<i64> {
                Seconds(self.0)
            }

            /// This second moved on by `shift`, `None` past either end
            /// of the count.
            #[must_use]
            pub const fn checked_add(self, shift: Seconds<i64>) -> Option<Self> {
                match self.0.checked_add(shift.0) {
                    Some(seconds) => Some(Self(seconds)),
                    None => None,
                }
            }

            /// This second moved back by `shift`, `None` past either
            /// end of the count.
            #[must_use]
            pub const fn checked_sub(self, shift: Seconds<i64>) -> Option<Self> {
                match self.0.checked_sub(shift.0) {
                    Some(seconds) => Some(Self(seconds)),
                    None => None,
                }
            }

            /// How long after `earlier` this second is, negative if it
            /// is before.  `None` if an `i64` cannot say.
            #[must_use]
            pub const fn checked_since(self, earlier: Self) -> Option<Seconds<i64>> {
                match self.0.checked_sub(earlier.0) {
                    Some(seconds) => Some(Seconds(seconds)),
                    None => None,
                }
            }
        }

        /// Stops at the count's end.
        impl Add<Seconds<i64>> for $count {
            type Output = Self;

            fn add(self, shift: Seconds<i64>) -> Self {
                Self(self.0.saturating_add(shift.0))
            }
        }

        /// Stops at the count's end.
        impl AddAssign<Seconds<i64>> for $count {
            fn add_assign(&mut self, shift: Seconds<i64>) {
                *self = *self + shift;
            }
        }

        /// Stops at the count's end.
        impl Sub<Seconds<i64>> for $count {
            type Output = Self;

            fn sub(self, shift: Seconds<i64>) -> Self {
                Self(self.0.saturating_sub(shift.0))
            }
        }

        /// Stops at the count's end.
        impl SubAssign<Seconds<i64>> for $count {
            fn sub_assign(&mut self, shift: Seconds<i64>) {
                *self = *self - shift;
            }
        }

        /// How long after `earlier`, negative if before; the largest or
        /// smallest length an `i64` holds where the true one is past it.
        impl Sub for $count {
            type Output = Seconds<i64>;

            fn sub(self, earlier: Self) -> Seconds<i64> {
                Seconds(self.0.saturating_sub(earlier.0))
            }
        }
    };
}

count_of_seconds!(UnixSeconds);
count_of_seconds!(GpsSeconds);

/// The start of GPS time.
pub const GPS_EPOCH: UnixSeconds = DateTime {
    year: 1980,
    month: 1,
    day: 6,
    hour: 0,
    minute: 0,
    second: 0,
}
.to_unix();

/// A count of weeks since the start of GPS time, with no rollover.
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct GpsWeek(u16);

/// Seconds since the start of a GPS week, less than
/// [`SECONDS_PER_WEEK`].
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct TimeOfWeek(u32);

impl GpsWeek {
    /// The week numbered `week` in full, not as a satellite broadcasts
    /// it.
    #[must_use]
    pub const fn new(week: u16) -> Self {
        Self(week)
    }

    /// Its number in full.
    #[must_use]
    pub const fn get(self) -> u16 {
        self.0
    }

    /// The week before, `None` for the first.
    #[must_use]
    pub const fn previous(self) -> Option<Self> {
        match self.0.checked_sub(1) {
            Some(week) => Some(Self(week)),
            None => None,
        }
    }

    /// The week number a satellite broadcasts: this one modulo
    /// [`BROADCAST_WEEKS`].
    #[must_use]
    pub const fn broadcast(self) -> u16 {
        self.0 % BROADCAST_WEEKS
    }

    /// The week number an almanac carries: this one modulo
    /// [`ALMANAC_WEEKS`].
    #[must_use]
    #[expect(clippy::cast_possible_truncation, reason = "less than 256")]
    pub const fn almanac(self) -> u8 {
        (self.0 % ALMANAC_WEEKS) as u8
    }
}

impl TimeOfWeek {
    /// The week's first second.
    pub const START: Self = Self(0);

    /// `None` unless `seconds` is less than [`SECONDS_PER_WEEK`].
    #[must_use]
    pub const fn new(seconds: u32) -> Option<Self> {
        if seconds < SECONDS_PER_WEEK {
            Some(Self(seconds))
        } else {
            None
        }
    }

    /// The second a count of milliseconds into the week is in, what is
    /// left of a second dropped.  `None` for a week's worth or more.
    #[must_use]
    pub const fn from_milliseconds(milliseconds: Milliseconds<u32>) -> Option<Self> {
        Self::new(milliseconds.0 / MILLISECONDS_PER_SECOND)
    }

    /// The count.
    #[must_use]
    pub const fn get(self) -> u32 {
        self.0
    }

    /// How long into the week it is.
    #[must_use]
    pub const fn seconds(self) -> Seconds<i64> {
        Seconds(self.0 as i64)
    }

    /// The same in tenths of a second, as `@@Ce` carries a time of
    /// week (`third_party/motorola/VPCommands.pdf` p. 115).
    #[must_use]
    pub const fn deciseconds(self) -> Deciseconds {
        Deciseconds(self.0 * DECISECONDS_PER_SECOND)
    }
}

impl GpsSeconds {
    /// The week this second is in.  `None` before the start of GPS
    /// time, and past the last week a `u16` numbers, some 1250 years
    /// on.
    #[must_use]
    #[expect(
        clippy::cast_possible_truncation,
        clippy::cast_sign_loss,
        reason = "checked just before"
    )]
    pub const fn week(self) -> Option<GpsWeek> {
        let week = self.0.div_euclid(SECONDS_PER_WEEK as i64);
        if week < 0 || week > u16::MAX as i64 {
            None
        } else {
            Some(GpsWeek(week as u16))
        }
    }

    /// How far into its week this second is: for one before the start
    /// of GPS time, how far after the Sunday before it.
    #[must_use]
    #[expect(clippy::cast_possible_truncation, reason = "0 to 604799")]
    pub const fn time_of_week(self) -> TimeOfWeek {
        TimeOfWeek(self.0.rem_euclid(SECONDS_PER_WEEK as i64) as u32)
    }

    /// The second a week and a time of week name.
    #[must_use]
    pub const fn from_week(week: GpsWeek, time_of_week: TimeOfWeek) -> Self {
        Self(week.0 as i64 * SECONDS_PER_WEEK as i64 + time_of_week.0 as i64)
    }

    /// A week's first second.
    #[must_use]
    pub const fn week_start(week: GpsWeek) -> Self {
        Self::from_week(week, TimeOfWeek::START)
    }

    /// The second nearest a time given as a week, milliseconds into it
    /// and a fraction of a millisecond either way: half a second or
    /// more past one second is the next, which may be the next week's
    /// first.  `None` for a week's worth of milliseconds or more.
    #[must_use]
    pub const fn nearest(
        week: GpsWeek,
        time_of_week: Milliseconds<u32>,
        fraction: Nanoseconds<i32>,
    ) -> Option<Self> {
        if TimeOfWeek::from_milliseconds(time_of_week).is_none() {
            return None;
        }
        let nanoseconds = time_of_week.0 as i64 * NANOSECONDS_PER_MILLISECOND + fraction.0 as i64;
        let seconds = (nanoseconds + NANOSECONDS_PER_SECOND / 2).div_euclid(NANOSECONDS_PER_SECOND);
        Some(Self(Self::week_start(week).0 + seconds))
    }

    /// The UTC count of this second, `offset` being GPS less UTC at it.
    /// `None` past the count's end.
    #[must_use]
    pub const fn to_utc(self, offset: Seconds<i8>) -> Option<UnixSeconds> {
        let Some(unix) = GPS_EPOCH.0.checked_add(self.0) else {
            return None;
        };
        match unix.checked_sub(offset.0 as i64) {
            Some(unix) => Some(UnixSeconds(unix)),
            None => None,
        }
    }

    /// The GPS second of a UTC count, `offset` being GPS less UTC at it.
    /// For an inserted leap second, whose count is that of the second
    /// after it, `offset` is the one before the leap.  `None` past the
    /// count's end.
    #[must_use]
    pub const fn from_utc(utc: UnixSeconds, offset: Seconds<i8>) -> Option<Self> {
        let Some(seconds) = utc.0.checked_sub(GPS_EPOCH.0) else {
            return None;
        };
        match seconds.checked_add(offset.0 as i64) {
            Some(seconds) => Some(Self(seconds)),
            None => None,
        }
    }
}

/// Which way a leap second moves UTC.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum LeapChange {
    /// A second is inserted, 23:59:60, and GPS less UTC grows by one.
    Insert,
    /// 23:59:59 is left out, and GPS less UTC shrinks by one.
    Remove,
}

/// A leap second, pending or past.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct LeapEvent {
    /// The first second counted with the new offset.  For
    /// [`LeapChange::Insert`] that is the inserted second itself,
    /// 23:59:60; for [`LeapChange::Remove`], the 00:00:00 that follows
    /// 23:59:58.
    pub at: GpsSeconds,
    /// Which way.
    pub change: LeapChange,
}

impl LeapEvent {
    /// The leap second at the end of a UTC day, where UTC has them:
    /// `midnight` is the 00:00:00 that starts the next day and `before`
    /// GPS less UTC until then.  `None` if `midnight` is not one, or
    /// past the count's end.
    #[must_use]
    pub const fn at_day_end(
        midnight: UnixSeconds,
        before: Seconds<i8>,
        change: LeapChange,
    ) -> Option<Self> {
        if midnight.0.rem_euclid(SECONDS_PER_DAY) != 0 {
            return None;
        }
        // With the old offset an inserted second has midnight's count;
        // a removal's midnight comes a second sooner than that.
        let Some(inserted) = GpsSeconds::from_utc(midnight, before) else {
            return None;
        };
        let at = match change {
            LeapChange::Insert => Some(inserted),
            LeapChange::Remove => inserted.checked_sub(Seconds(1)),
        };
        match at {
            Some(at) => Some(Self { at, change }),
            None => None,
        }
    }

    /// The change in GPS less UTC.
    #[must_use]
    pub const fn step(self) -> Seconds<i8> {
        match self.change {
            LeapChange::Insert => Seconds(1),
            LeapChange::Remove => Seconds(-1),
        }
    }

    /// Whether it is still to come at `now`.
    #[must_use]
    pub const fn pending(self, now: GpsSeconds) -> bool {
        now.0 < self.at.0
    }

    /// GPS less UTC at `now`, `before` being what it was before the leap.
    #[must_use]
    pub const fn offset(self, now: GpsSeconds, before: Seconds<i8>) -> Seconds<i8> {
        if self.pending(now) {
            before
        } else {
            before.saturating_add(self.step())
        }
    }

    /// GPS less UTC before the leap, `offset` being what it is at
    /// `now`: what [`Self::offset`] undoes.
    #[must_use]
    pub const fn offset_before(self, now: GpsSeconds, offset: Seconds<i8>) -> Seconds<i8> {
        if self.pending(now) {
            offset
        } else {
            offset.saturating_add(Seconds(-self.step().0))
        }
    }
}

/// A date and time of day.  Any field values can be held; [`Self::new`]
/// makes only real ones.
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct DateTime {
    /// The year, in full.
    pub year: u16,
    /// 1 to 12.
    pub month: u8,
    /// 1 to the month's length.
    pub day: u8,
    /// 0 to 23.
    pub hour: u8,
    /// 0 to 59.
    pub minute: u8,
    /// 0 to 59, or [`LEAP_SECOND`] in an inserted leap second.
    pub second: u8,
}

// The calendar arithmetic is Howard Hinnant's "chrono-Compatible
// Low-Level Date Algorithms": years are counted from March, so that the
// leap day is a year's last, in eras of 400 years, each the same length.
const YEARS_PER_ERA: i64 = 400;
const DAYS_PER_ERA: i64 = 146_097;
const DAYS_PER_YEAR: i64 = 365;
/// Days from 0000-03-01 to 1970-01-01.
const DAYS_TO_UNIX_EPOCH: i64 = 719_468;
/// The first month of the shifted year.
const MARCH: i64 = 3;
/// Days in the five months from March through July, the pattern the
/// shifted year's months repeat.
const DAYS_PER_FIVE_MONTHS: i64 = 153;
const FOUR_YEARS: i64 = 4;
const CENTURY: i64 = 100;

const fn is_leap_year(year: u16) -> bool {
    let year = year as i64;
    year % FOUR_YEARS == 0 && (year % CENTURY != 0 || year % YEARS_PER_ERA == 0)
}

/// The days in a month, 1 to 12, of a year; 31 for any other number.
#[must_use]
pub const fn days_in_month(year: u16, month: u8) -> u8 {
    match month {
        2 if is_leap_year(year) => 29,
        2 => 28,
        4 | 6 | 9 | 11 => 30,
        _ => 31,
    }
}

/// Days from 1970-01-01 to a date.
const fn days_from_civil(year: u16, month: u8, day: u8) -> i64 {
    let month = month as i64;
    let year = year as i64 - (month < MARCH) as i64;
    let era = year.div_euclid(YEARS_PER_ERA);
    let year_of_era = year.rem_euclid(YEARS_PER_ERA);
    let shifted_month = (month - MARCH).rem_euclid(MONTHS_PER_YEAR as i64);
    let day_of_year = (DAYS_PER_FIVE_MONTHS * shifted_month + 2) / 5 + day as i64 - 1;
    let day_of_era = year_of_era * DAYS_PER_YEAR + year_of_era / FOUR_YEARS - year_of_era / CENTURY
        + day_of_year;
    era * DAYS_PER_ERA + day_of_era - DAYS_TO_UNIX_EPOCH
}

impl DateTime {
    /// A real date and time: `None` if a field is out of range, the day
    /// past its month's end, or the second [`LEAP_SECOND`] other than at
    /// 23:59.
    #[must_use]
    pub const fn new(
        year: u16,
        month: u8,
        day: u8,
        hour: u8,
        minute: u8,
        second: u8,
    ) -> Option<Self> {
        let date = month >= 1 && month <= MONTHS_PER_YEAR && day >= 1;
        if !date || day > days_in_month(year, month) {
            return None;
        }
        let last_minute = hour as i64 == HOURS_PER_DAY - 1 && minute as i64 == MINUTES_PER_HOUR - 1;
        let second_fits =
            (second as i64) < SECONDS_PER_MINUTE || (second == LEAP_SECOND && last_minute);
        if hour as i64 >= HOURS_PER_DAY || minute as i64 >= MINUTES_PER_HOUR || !second_fits {
            return None;
        }
        Some(Self {
            year,
            month,
            day,
            hour,
            minute,
            second,
        })
    }

    /// Its UTC count.  An inserted leap second has the count of the
    /// second after it.
    #[must_use]
    pub const fn to_unix(self) -> UnixSeconds {
        UnixSeconds(
            days_from_civil(self.year, self.month, self.day) * SECONDS_PER_DAY
                + self.hour as i64 * SECONDS_PER_HOUR
                + self.minute as i64 * SECONDS_PER_MINUTE
                + self.second as i64,
        )
    }

    /// The date and time of a UTC count, `None` outside the years a
    /// `u16` holds.
    #[must_use]
    #[expect(
        clippy::cast_possible_truncation,
        clippy::cast_sign_loss,
        reason = "each value is within its field's range by the arithmetic, the year by the check"
    )]
    pub const fn from_unix(utc: UnixSeconds) -> Option<Self> {
        let days = utc.0.div_euclid(SECONDS_PER_DAY);
        let second_of_day = utc.0.rem_euclid(SECONDS_PER_DAY);

        let Some(days) = days.checked_add(DAYS_TO_UNIX_EPOCH) else {
            return None;
        };
        let era = days.div_euclid(DAYS_PER_ERA);
        let day_of_era = days.rem_euclid(DAYS_PER_ERA);
        let year_of_era = (day_of_era - day_of_era / (FOUR_YEARS * DAYS_PER_YEAR)
            + day_of_era / (CENTURY * DAYS_PER_YEAR + CENTURY / FOUR_YEARS - 1)
            - day_of_era / (DAYS_PER_ERA - 1))
            / DAYS_PER_YEAR;
        let day_of_year = day_of_era
            - (DAYS_PER_YEAR * year_of_era + year_of_era / FOUR_YEARS - year_of_era / CENTURY);
        let shifted_month = (5 * day_of_year + 2) / DAYS_PER_FIVE_MONTHS;
        let day = day_of_year - (DAYS_PER_FIVE_MONTHS * shifted_month + 2) / 5 + 1;
        let month = (shifted_month + MARCH - 1) % MONTHS_PER_YEAR as i64 + 1;
        let year = year_of_era + era * YEARS_PER_ERA + (month < MARCH) as i64;
        if year < 0 || year > u16::MAX as i64 {
            return None;
        }
        Some(Self {
            year: year as u16,
            month: month as u8,
            day: day as u8,
            hour: (second_of_day / SECONDS_PER_HOUR) as u8,
            minute: (second_of_day % SECONDS_PER_HOUR / SECONDS_PER_MINUTE) as u8,
            second: (second_of_day % SECONDS_PER_MINUTE) as u8,
        })
    }

    /// The UTC date and time of a GPS second.  `offset` is GPS less UTC
    /// at that second, so the new one from a leap's
    /// [`at`](LeapEvent::at) on ([`LeapEvent::offset`]); with `leap`, the
    /// second an insertion adds reads 23:59:60.  `None` outside the
    /// years a `u16` holds.
    #[must_use]
    pub const fn of(
        time: GpsSeconds,
        offset: Seconds<i8>,
        leap: Option<LeapEvent>,
    ) -> Option<Self> {
        Self::local(time, offset, leap, Seconds(0))
    }

    /// The date and time of a GPS second on a clock `ahead` of UTC, as
    /// an engine labels a second for a host that has set a GMT offset
    /// (`@@Ab`, `third_party/motorola/VPCommands.pdf` p. 9).  `offset`
    /// and `leap` are as for [`Self::of`]; the second an insertion adds
    /// reads 60 in whatever minute of the hour the clock puts it.  With
    /// an `offset` of zero and no `leap`, the label is of GPS time
    /// itself.  The label may fall before the start of GPS time.
    /// `None` outside the years a `u16` holds.
    #[must_use]
    pub const fn local(
        time: GpsSeconds,
        offset: Seconds<i8>,
        leap: Option<LeapEvent>,
        ahead: Seconds<i64>,
    ) -> Option<Self> {
        let Some(utc) = time.to_utc(offset) else {
            return None;
        };
        let Some(local) = utc.checked_add(ahead) else {
            return None;
        };
        let Some(read) = Self::from_unix(local) else {
            return None;
        };
        // With the new offset the inserted second has the UTC count of
        // the second before it, and so reads 23:59:59 a second time.
        let inserted = matches!(
            leap,
            Some(LeapEvent { at, change: LeapChange::Insert }) if at.0 == time.0
        );
        Some(Self {
            second: if inserted { LEAP_SECOND } else { read.second },
            ..read
        })
    }
}

#[cfg(test)]
mod tests {
    use super::ALMANAC_WEEKS;
    use super::BROADCAST_WEEKS;
    use super::DateTime;
    use super::GPS_EPOCH;
    use super::GpsSeconds;
    use super::GpsWeek;
    use super::LEAP_SECOND;
    use super::LeapChange;
    use super::LeapEvent;
    use super::SECONDS_PER_WEEK;
    use super::TimeOfWeek;
    use super::UnixSeconds;
    use super::days_in_month;
    use crate::units::Deciseconds;
    use crate::units::Milliseconds;
    use crate::units::Minutes;
    use crate::units::Nanoseconds;
    use crate::units::Seconds;

    const SECOND: Seconds<i64> = Seconds(1);
    const DAY: Seconds<i64> = Seconds(86_400);

    fn date(year: u16, month: u8, day: u8, hour: u8, minute: u8, second: u8) -> DateTime {
        DateTime::new(year, month, day, hour, minute, second).unwrap()
    }

    /// GPS time's own date: UTC with no offset.
    fn gps_date(time: GpsSeconds) -> DateTime {
        DateTime::of(time, Seconds(0), None).unwrap()
    }

    fn time_of_week(seconds: u32) -> TimeOfWeek {
        TimeOfWeek::new(seconds).unwrap()
    }

    #[test]
    fn gps_time_starts_on_january_6_1980() {
        assert_eq!(GPS_EPOCH, UnixSeconds::from_epoch(Seconds(315_964_800)));
        assert_eq!(gps_date(GpsSeconds::EPOCH), date(1980, 1, 6, 0, 0, 0));
        assert_eq!(
            GpsSeconds::from_utc(GPS_EPOCH, Seconds(0)),
            Some(GpsSeconds::EPOCH)
        );
        assert_eq!(GpsSeconds::EPOCH.since_epoch(), Seconds(0));
    }

    /// The count goes on before its start: the last second of January
    /// 5, 1980 is one before it, in no week, a second short of a
    /// Sunday.
    #[test]
    fn a_second_before_gps_time_is_negative() {
        let eve = GpsSeconds::from_utc(GPS_EPOCH - SECOND, Seconds(0)).unwrap();
        assert_eq!(eve, GpsSeconds::from_epoch(Seconds(-1)));
        assert_eq!(eve, GpsSeconds::EPOCH - SECOND);
        assert!(eve < GpsSeconds::EPOCH);
        assert_eq!(eve - GpsSeconds::EPOCH, Seconds(-1));
        assert_eq!(GpsSeconds::EPOCH - eve, SECOND);
        assert_eq!(gps_date(eve), date(1980, 1, 5, 23, 59, 59));
        assert_eq!(eve.week(), None);
        assert_eq!(eve.time_of_week(), time_of_week(SECONDS_PER_WEEK - 1));
        assert_eq!(eve.to_utc(Seconds(0)), Some(GPS_EPOCH - SECOND));
    }

    #[test]
    fn the_unix_count_starts_in_1970() {
        assert_eq!(date(1970, 1, 1, 0, 0, 0).to_unix(), UnixSeconds::EPOCH);
        assert_eq!(
            DateTime::from_unix(UnixSeconds::EPOCH),
            Some(date(1970, 1, 1, 0, 0, 0))
        );
        assert_eq!(
            DateTime::from_unix(UnixSeconds::EPOCH - SECOND),
            Some(date(1969, 12, 31, 23, 59, 59))
        );
    }

    /// Every day from 1980 through 2100 converts both ways, and each
    /// follows the one before: leap years, 2000 a leap year and 2100 not.
    #[test]
    fn dates_round_trip_day_by_day() {
        let mut expected = date(1980, 1, 1, 12, 34, 56);
        let last = date(2101, 1, 1, 12, 34, 56);
        let mut unix = expected.to_unix();
        while expected < last {
            assert_eq!(DateTime::from_unix(unix), Some(expected));
            assert_eq!(expected.to_unix(), unix);
            unix += DAY;
            let next_day = DateTime::new(
                expected.year,
                expected.month,
                expected.day + 1,
                expected.hour,
                expected.minute,
                expected.second,
            );
            let next_month = DateTime::new(expected.year, expected.month + 1, 1, 12, 34, 56);
            expected = next_day
                .or(next_month)
                .unwrap_or_else(|| date(expected.year + 1, 1, 1, 12, 34, 56));
        }
        assert_eq!(DateTime::new(2000, 2, 29, 0, 0, 0).map(|d| d.day), Some(29));
        assert_eq!(DateTime::new(2100, 2, 29, 0, 0, 0), None);
        assert_eq!(DateTime::new(2024, 2, 29, 0, 0, 0).map(|d| d.day), Some(29));
        assert_eq!(DateTime::new(2023, 2, 29, 0, 0, 0), None);
        assert_eq!(days_in_month(2000, 2), 29);
        assert_eq!(days_in_month(2100, 2), 28);
        assert_eq!(days_in_month(2023, 4), 30);
        assert_eq!(days_in_month(2023, 12), 31);
    }

    #[test]
    fn new_refuses_what_is_not_a_time() {
        assert_eq!(DateTime::new(2000, 0, 1, 0, 0, 0), None);
        assert_eq!(DateTime::new(2000, 13, 1, 0, 0, 0), None);
        assert_eq!(DateTime::new(2000, 1, 0, 0, 0, 0), None);
        assert_eq!(DateTime::new(2000, 4, 31, 0, 0, 0), None);
        assert_eq!(DateTime::new(2000, 1, 1, 24, 0, 0), None);
        assert_eq!(DateTime::new(2000, 1, 1, 0, 60, 0), None);
        assert_eq!(DateTime::new(2000, 1, 1, 0, 0, LEAP_SECOND), None);
        assert_eq!(DateTime::new(2000, 1, 1, 23, 59, LEAP_SECOND + 1), None);
        assert!(DateTime::new(2000, 1, 1, 23, 59, LEAP_SECOND).is_some());
    }

    /// Furuno's rollover note, p. 2: 1945 weeks, 1024 + 921, lie between
    /// 1980/1/6 and 2017/4/19, when the satellites broadcast week 921.
    #[test]
    fn the_week_of_a_date() {
        let noon = date(2017, 4, 19, 12, 0, 0).to_unix();
        let time = GpsSeconds::from_utc(noon, Seconds(0)).unwrap();
        let week = time.week().unwrap();
        assert_eq!(week, GpsWeek::new(1945));
        assert_eq!(week.get(), 1945);
        assert_eq!(week.broadcast(), 921);
        assert_eq!(week.previous(), Some(GpsWeek::new(1944)));
        assert_eq!(GpsWeek::new(0).previous(), None);
        // A Wednesday: three days and twelve hours into its week.
        let into = (3 * 24 + 12) * 3600;
        assert_eq!(time.time_of_week(), time_of_week(into));
        assert_eq!(time.time_of_week().get(), into);
        assert_eq!(time.time_of_week().seconds(), Seconds(i64::from(into)));
        assert_eq!(time.time_of_week().deciseconds(), Deciseconds(into * 10));
        assert_eq!(GpsSeconds::from_week(week, time.time_of_week()), time);
        assert_eq!(
            time - GpsSeconds::week_start(week),
            time.time_of_week().seconds()
        );
    }

    #[test]
    fn a_time_of_week_is_less_than_a_week() {
        assert_eq!(TimeOfWeek::new(0), Some(TimeOfWeek::START));
        assert!(TimeOfWeek::new(SECONDS_PER_WEEK - 1).is_some());
        assert_eq!(TimeOfWeek::new(SECONDS_PER_WEEK), None);
        assert_eq!(
            TimeOfWeek::from_milliseconds(Milliseconds(1999)),
            Some(time_of_week(1))
        );
        assert_eq!(
            TimeOfWeek::from_milliseconds(Milliseconds(SECONDS_PER_WEEK * 1000)),
            None
        );
    }

    /// A time in milliseconds and a fraction is the second nearest it,
    /// either side of a second and of a week.
    #[test]
    fn the_nearest_second() {
        let week = GpsWeek::new(1945);
        let start = GpsSeconds::week_start(week);
        let nearest = |milliseconds: u32, fraction: i32| {
            GpsSeconds::nearest(week, Milliseconds(milliseconds), Nanoseconds(fraction))
        };
        assert_eq!(nearest(0, 0), Some(start));
        assert_eq!(nearest(5000, 400_000), Some(start + Seconds(5)));
        assert_eq!(nearest(4999, 999_000), Some(start + Seconds(5)));
        assert_eq!(nearest(5499, 999_999), Some(start + Seconds(5)));
        assert_eq!(nearest(5500, 0), Some(start + Seconds(6)));
        // Just short of the week's start, and of the next one's.
        assert_eq!(nearest(0, -400_000), Some(start));
        let last = SECONDS_PER_WEEK * 1000 - 1;
        assert_eq!(
            nearest(last, 999_000),
            Some(GpsSeconds::week_start(GpsWeek::new(1946)))
        );
        assert_eq!(nearest(last + 1, 0), None);
    }

    /// The same note's time line, p. 2: the broadcast week returns to
    /// 0 on 1999/8/22, 2019/4/7, 2038/11/21 and 2058/7/7.
    #[test]
    fn the_broadcast_week_rolls_over() {
        let rollovers = [
            date(1999, 8, 22, 0, 0, 0),
            date(2019, 4, 7, 0, 0, 0),
            date(2038, 11, 21, 0, 0, 0),
            date(2058, 7, 7, 0, 0, 0),
        ];
        for (index, first) in rollovers.into_iter().enumerate() {
            let count = u16::try_from(index + 1).unwrap();
            let week = GpsWeek::new(count * BROADCAST_WEEKS);
            let start = GpsSeconds::week_start(week);
            assert_eq!(gps_date(start), first);
            assert_eq!(start.week(), Some(week));
            assert_eq!(week.broadcast(), 0);

            let before = start - SECOND;
            assert_eq!(before.week(), week.previous());
            assert_eq!(before.week().unwrap().broadcast(), BROADCAST_WEEKS - 1);
            assert_eq!(before.time_of_week(), time_of_week(SECONDS_PER_WEEK - 1));
            let eve = gps_date(before);
            assert_eq!((eve.hour, eve.minute, eve.second), (23, 59, 59));
            assert_eq!(before.to_utc(Seconds(0)).unwrap() + SECOND, first.to_unix());
        }
    }

    #[test]
    fn the_almanac_week_rolls_over() {
        assert_eq!(GpsWeek::new(ALMANAC_WEEKS - 1).almanac(), 255);
        assert_eq!(GpsWeek::new(ALMANAC_WEEKS).almanac(), 0);
        // 1945 = 7 * 256 + 153.
        assert_eq!(GpsWeek::new(1945).almanac(), 153);
    }

    /// At the count's ends the checked functions say so and the
    /// operators stop.
    #[test]
    fn the_count_has_ends() {
        let last = GpsSeconds::from_epoch(Seconds(i64::MAX));
        let first = GpsSeconds::from_epoch(Seconds(i64::MIN));
        assert_eq!(last.week(), None);
        assert_eq!(DateTime::of(last, Seconds(0), None), None);
        assert_eq!(last.to_utc(Seconds(0)), None);
        assert_eq!(last.checked_add(SECOND), None);
        assert_eq!(first.checked_sub(SECOND), None);
        assert_eq!(last.checked_since(first), None);
        assert_eq!(last + SECOND, last);
        assert_eq!(first - SECOND, first);
        assert_eq!(last - first, Seconds(i64::MAX));
        assert_eq!(first - last, Seconds(i64::MIN));

        let five = GpsSeconds::from_epoch(Seconds(5));
        assert_eq!(five.checked_add(Seconds(-5)), Some(GpsSeconds::EPOCH));
        assert_eq!(five.checked_sub(Seconds(5)), Some(GpsSeconds::EPOCH));
        assert_eq!(five.checked_since(GpsSeconds::EPOCH), Some(Seconds(5)));
        let mut moved = five;
        moved += Seconds(2);
        moved -= Seconds(10);
        assert_eq!(moved.since_epoch(), Seconds(-3));

        // The last week a u16 numbers, and the second after it.
        let end = GpsSeconds::from_week(GpsWeek::new(u16::MAX), time_of_week(SECONDS_PER_WEEK - 1));
        assert_eq!(end.week(), Some(GpsWeek::new(u16::MAX)));
        assert_eq!((end + SECOND).week(), None);

        let unix = UnixSeconds::from_epoch(Seconds(i64::MAX));
        assert_eq!(unix.checked_add(SECOND), None);
        assert_eq!(unix + SECOND, unix);
        assert_eq!(
            UnixSeconds::from_epoch(Seconds(5)).checked_add(Seconds(-6)),
            Some(UnixSeconds::EPOCH - SECOND)
        );
        let first = UnixSeconds::from_epoch(Seconds(i64::MIN));
        assert_eq!(GpsSeconds::from_utc(first, Seconds(0)), None);
    }

    /// The leap second at the end of 1998, which took GPS less UTC from
    /// 12 to 13 (GT-8031 specification p. 37: 13 as of June 1999), read
    /// as the specification draws an insertion on the same page:
    /// 23:59:58, 23:59:59, 23:59:60, 00:00:00.
    #[test]
    fn an_inserted_leap_second() {
        let before = Seconds(12);
        let last = GpsSeconds::from_utc(date(1998, 12, 31, 23, 59, 59).to_unix(), before).unwrap();
        let leap = LeapEvent {
            at: last + SECOND,
            change: LeapChange::Insert,
        };
        assert_eq!(leap.step(), Seconds(1));
        assert_eq!(
            LeapEvent::at_day_end(
                date(1999, 1, 1, 0, 0, 0).to_unix(),
                before,
                LeapChange::Insert
            ),
            Some(leap)
        );

        let read = |time: GpsSeconds| {
            let offset = leap.offset(time, before);
            (
                DateTime::of(time, offset, Some(leap)).unwrap(),
                offset,
                leap.pending(time),
            )
        };
        assert_eq!(
            read(last - SECOND),
            (date(1998, 12, 31, 23, 59, 58), Seconds(12), true)
        );
        assert_eq!(
            read(last),
            (date(1998, 12, 31, 23, 59, 59), Seconds(12), true)
        );
        assert_eq!(
            read(leap.at),
            (date(1998, 12, 31, 23, 59, LEAP_SECOND), Seconds(13), false)
        );
        assert_eq!(
            read(leap.at + SECOND),
            (date(1999, 1, 1, 0, 0, 0), Seconds(13), false)
        );

        // The inserted second converts back with the offset before it.
        let inserted = date(1998, 12, 31, 23, 59, LEAP_SECOND);
        assert_eq!(inserted.to_unix(), date(1999, 1, 1, 0, 0, 0).to_unix());
        assert_eq!(
            GpsSeconds::from_utc(inserted.to_unix(), before),
            Some(leap.at)
        );

        // Without the event the same second can only read 23:59:59 again.
        assert_eq!(
            DateTime::of(leap.at, Seconds(13), None),
            Some(date(1998, 12, 31, 23, 59, 59))
        );
    }

    /// A clock set ahead of or behind UTC labels the same seconds, the
    /// inserted one still 60 though no longer at 23:59, and may read a
    /// date before the start of GPS time.
    #[test]
    fn a_local_label() {
        let before = Seconds(12);
        let last = GpsSeconds::from_utc(date(1998, 12, 31, 23, 59, 59).to_unix(), before).unwrap();
        let leap = LeapEvent {
            at: last + SECOND,
            change: LeapChange::Insert,
        };
        let behind = Minutes(-8 * 60).seconds();
        assert_eq!(behind, Seconds(-8 * 3600));
        let read = |time: GpsSeconds| {
            DateTime::local(time, leap.offset(time, before), Some(leap), behind).unwrap()
        };
        assert_eq!(read(last), date(1998, 12, 31, 15, 59, 59));
        assert_eq!(
            read(leap.at),
            DateTime {
                second: LEAP_SECOND,
                ..date(1998, 12, 31, 15, 59, 0)
            }
        );
        assert_eq!(read(leap.at + SECOND), date(1998, 12, 31, 16, 0, 0));

        assert_eq!(
            DateTime::local(GpsSeconds::EPOCH, Seconds(0), None, behind),
            Some(date(1980, 1, 5, 16, 0, 0))
        );
        assert_eq!(
            DateTime::local(GpsSeconds::EPOCH, Seconds(0), None, Seconds(0)),
            DateTime::of(GpsSeconds::EPOCH, Seconds(0), None)
        );
        // A time before the start of GPS time is labeled as any other.
        assert_eq!(
            DateTime::local(
                GpsSeconds::EPOCH - Seconds(3600),
                Seconds(0),
                None,
                Seconds(0)
            ),
            Some(date(1980, 1, 5, 23, 0, 0))
        );
    }

    /// A removal, which has never happened, as the specification draws
    /// it on p. 37: 23:59:58, then 00:00:00.
    #[test]
    fn a_removed_leap_second() {
        let before = Seconds(13);
        let last = GpsSeconds::from_utc(date(1999, 6, 30, 23, 59, 58).to_unix(), before).unwrap();
        let leap = LeapEvent {
            at: last + SECOND,
            change: LeapChange::Remove,
        };
        assert_eq!(leap.step(), Seconds(-1));
        let midnight = date(1999, 7, 1, 0, 0, 0).to_unix();
        assert_eq!(
            LeapEvent::at_day_end(midnight, before, LeapChange::Remove),
            Some(leap)
        );
        let read =
            |time: GpsSeconds| DateTime::of(time, leap.offset(time, before), Some(leap)).unwrap();
        assert_eq!(read(last), date(1999, 6, 30, 23, 59, 58));
        assert_eq!(read(leap.at), date(1999, 7, 1, 0, 0, 0));
        assert_eq!(leap.offset(leap.at, before), Seconds(12));

        // UTC has a leap second only where a day ends.
        assert_eq!(
            LeapEvent::at_day_end(midnight + SECOND, before, LeapChange::Remove),
            None
        );
        assert_eq!(
            LeapEvent::at_day_end(midnight - Seconds(12 * 3600), before, LeapChange::Insert),
            None
        );
    }

    /// A leap second in a week's last second, as one at the end of a
    /// Saturday would be: the week turns over on the count, not the
    /// calendar.
    #[test]
    fn the_week_ignores_leap_seconds() {
        let saturday =
            GpsSeconds::from_week(GpsWeek::new(1000), time_of_week(SECONDS_PER_WEEK - 1));
        let sunday = saturday + SECOND;
        assert_eq!(sunday.week(), Some(GpsWeek::new(1001)));
        assert_eq!(sunday.time_of_week(), TimeOfWeek::START);
        let utc = DateTime::of(sunday, Seconds(13), None).unwrap();
        assert_eq!((utc.hour, utc.minute, utc.second), (23, 59, 47));
    }
}
