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
//! Nothing here reads a clock.  Every function is `const`, so a start
//! time or a leap second can be worked out in a `static`'s initializer.

use crate::units::Seconds;

const SECONDS_PER_MINUTE: i64 = 60;
const MINUTES_PER_HOUR: i64 = 60;
const HOURS_PER_DAY: i64 = 24;
const SECONDS_PER_HOUR: i64 = SECONDS_PER_MINUTE * MINUTES_PER_HOUR;
const SECONDS_PER_DAY: i64 = SECONDS_PER_HOUR * HOURS_PER_DAY;
const DAYS_PER_WEEK: u32 = 7;
const MONTHS_PER_YEAR: u8 = 12;

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
/// inserted leap second.
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct UnixSeconds(pub i64);

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

/// Seconds of GPS time since its start, with no leap seconds and no
/// rollover.
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct GpsSeconds(pub u64);

/// A count of weeks since the start of GPS time, with no rollover.
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct GpsWeek(pub u16);

/// Seconds since the start of a GPS week, less than
/// [`SECONDS_PER_WEEK`].
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct TimeOfWeek(pub u32);

impl GpsWeek {
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

impl GpsSeconds {
    /// The week this second is in.  A week past `u16::MAX`, some 1250
    /// years on, is given as that.
    #[must_use]
    #[expect(clippy::cast_possible_truncation, reason = "checked just before")]
    pub const fn week(self) -> GpsWeek {
        let week = self.0 / SECONDS_PER_WEEK as u64;
        if week > u16::MAX as u64 {
            GpsWeek(u16::MAX)
        } else {
            GpsWeek(week as u16)
        }
    }

    /// How far into its week this second is.
    #[must_use]
    #[expect(clippy::cast_possible_truncation, reason = "less than 604800")]
    pub const fn time_of_week(self) -> TimeOfWeek {
        TimeOfWeek((self.0 % SECONDS_PER_WEEK as u64) as u32)
    }

    /// The second a week and a time of week name.
    #[must_use]
    pub const fn from_week(week: GpsWeek, time_of_week: TimeOfWeek) -> Self {
        Self(week.0 as u64 * SECONDS_PER_WEEK as u64 + time_of_week.0 as u64)
    }

    /// This second moved by `shift`, `None` if that is before the start
    /// of GPS time or past the count's end.
    #[must_use]
    pub const fn checked_add(self, shift: Seconds<i64>) -> Option<Self> {
        match self.0.checked_add_signed(shift.0) {
            Some(seconds) => Some(Self(seconds)),
            None => None,
        }
    }

    /// The UTC count of this second, `offset` being GPS less UTC at it.
    /// `None` past the count's end.
    #[must_use]
    pub const fn to_utc(self, offset: Seconds<i8>) -> Option<UnixSeconds> {
        if self.0 > i64::MAX as u64 {
            return None;
        }
        let Some(unix) = GPS_EPOCH.0.checked_add_unsigned(self.0) else {
            return None;
        };
        match unix.checked_sub(offset.0 as i64) {
            Some(unix) => Some(UnixSeconds(unix)),
            None => None,
        }
    }

    /// The GPS second of a UTC count, `offset` being GPS less UTC at it.
    /// For an inserted leap second, whose count is that of the second
    /// after it, `offset` is the one before the leap.  `None` before the
    /// start of GPS time.
    #[must_use]
    pub const fn from_utc(utc: UnixSeconds, offset: Seconds<i8>) -> Option<Self> {
        let Some(seconds) = utc.0.checked_sub(GPS_EPOCH.0) else {
            return None;
        };
        let Some(seconds) = seconds.checked_add(offset.0 as i64) else {
            return None;
        };
        if seconds < 0 {
            None
        } else {
            Some(Self(seconds.cast_unsigned()))
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
            Seconds(before.0.saturating_add(self.step().0))
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

const fn days_in_month(year: u16, month: u8) -> u8 {
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
        let Some(utc) = time.to_utc(offset) else {
            return None;
        };
        let Some(read) = Self::from_unix(utc) else {
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
    use crate::units::Seconds;

    fn date(year: u16, month: u8, day: u8, hour: u8, minute: u8, second: u8) -> DateTime {
        DateTime::new(year, month, day, hour, minute, second).unwrap()
    }

    /// GPS time's own date: UTC with no offset.
    fn gps_date(time: GpsSeconds) -> DateTime {
        DateTime::of(time, Seconds(0), None).unwrap()
    }

    #[test]
    fn gps_time_starts_on_january_6_1980() {
        assert_eq!(GPS_EPOCH, UnixSeconds(315_964_800));
        assert_eq!(gps_date(GpsSeconds(0)), date(1980, 1, 6, 0, 0, 0));
        assert_eq!(
            GpsSeconds::from_utc(GPS_EPOCH, Seconds(0)),
            Some(GpsSeconds(0))
        );
        assert_eq!(
            GpsSeconds::from_utc(UnixSeconds(GPS_EPOCH.0 - 1), Seconds(0)),
            None
        );
    }

    #[test]
    fn the_unix_count_starts_in_1970() {
        assert_eq!(date(1970, 1, 1, 0, 0, 0).to_unix(), UnixSeconds(0));
        assert_eq!(
            DateTime::from_unix(UnixSeconds(0)),
            Some(date(1970, 1, 1, 0, 0, 0))
        );
        assert_eq!(
            DateTime::from_unix(UnixSeconds(-1)),
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
            unix = UnixSeconds(unix.0 + 86_400);
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
        assert_eq!(time.week(), GpsWeek(1945));
        assert_eq!(time.week().broadcast(), 921);
        // A Wednesday: three days and twelve hours into its week.
        assert_eq!(time.time_of_week(), TimeOfWeek((3 * 24 + 12) * 3600));
        assert_eq!(
            GpsSeconds::from_week(time.week(), time.time_of_week()),
            time
        );
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
            let week = GpsWeek(count * BROADCAST_WEEKS);
            let start = GpsSeconds::from_week(week, TimeOfWeek(0));
            assert_eq!(gps_date(start), first);
            assert_eq!(start.week(), week);
            assert_eq!(start.week().broadcast(), 0);

            let before = GpsSeconds(start.0 - 1);
            assert_eq!(before.week().broadcast(), BROADCAST_WEEKS - 1);
            assert_eq!(before.time_of_week(), TimeOfWeek(SECONDS_PER_WEEK - 1));
            let eve = gps_date(before);
            assert_eq!((eve.hour, eve.minute, eve.second), (23, 59, 59));
            assert_eq!(before.to_utc(Seconds(0)).unwrap().0 + 1, first.to_unix().0);
        }
    }

    #[test]
    fn the_almanac_week_rolls_over() {
        assert_eq!(GpsWeek(ALMANAC_WEEKS - 1).almanac(), 255);
        assert_eq!(GpsWeek(ALMANAC_WEEKS).almanac(), 0);
        // 1945 = 7 * 256 + 153.
        assert_eq!(GpsWeek(1945).almanac(), 153);
    }

    #[test]
    fn a_week_past_the_count_is_its_end() {
        assert_eq!(GpsSeconds(u64::MAX).week(), GpsWeek(u16::MAX));
        assert_eq!(DateTime::of(GpsSeconds(u64::MAX), Seconds(0), None), None);
        assert_eq!(GpsSeconds(u64::MAX).checked_add(Seconds(1)), None);
        assert_eq!(GpsSeconds(0).checked_add(Seconds(-1)), None);
        assert_eq!(GpsSeconds(5).checked_add(Seconds(-5)), Some(GpsSeconds(0)));
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
            at: GpsSeconds(last.0 + 1),
            change: LeapChange::Insert,
        };
        assert_eq!(leap.step(), Seconds(1));

        let read = |time: GpsSeconds| {
            let offset = leap.offset(time, before);
            (
                DateTime::of(time, offset, Some(leap)).unwrap(),
                offset,
                leap.pending(time),
            )
        };
        assert_eq!(
            read(GpsSeconds(last.0 - 1)),
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
            read(GpsSeconds(leap.at.0 + 1)),
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

    /// A removal, which has never happened, as the specification draws
    /// it on p. 37: 23:59:58, then 00:00:00.
    #[test]
    fn a_removed_leap_second() {
        let before = Seconds(13);
        let last = GpsSeconds::from_utc(date(1999, 6, 30, 23, 59, 58).to_unix(), before).unwrap();
        let leap = LeapEvent {
            at: GpsSeconds(last.0 + 1),
            change: LeapChange::Remove,
        };
        assert_eq!(leap.step(), Seconds(-1));
        let read =
            |time: GpsSeconds| DateTime::of(time, leap.offset(time, before), Some(leap)).unwrap();
        assert_eq!(read(last), date(1999, 6, 30, 23, 59, 58));
        assert_eq!(read(leap.at), date(1999, 7, 1, 0, 0, 0));
        assert_eq!(leap.offset(leap.at, before), Seconds(12));
    }

    /// A leap second in a week's last second, as one at the end of a
    /// Saturday would be: the week turns over on the count, not the
    /// calendar.
    #[test]
    fn the_week_ignores_leap_seconds() {
        let saturday = GpsSeconds::from_week(GpsWeek(1000), TimeOfWeek(SECONDS_PER_WEEK - 1));
        let sunday = GpsSeconds(saturday.0 + 1);
        assert_eq!(sunday.week(), GpsWeek(1001));
        assert_eq!(sunday.time_of_week(), TimeOfWeek(0));
        let utc = DateTime::of(sunday, Seconds(13), None).unwrap();
        assert_eq!((utc.hour, utc.minute, utc.second), (23, 59, 47));
    }
}
