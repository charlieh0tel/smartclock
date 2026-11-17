//! The navigation results' messages, `UBX-NAV-*`.  Each decoder keeps
//! every field of its message, in the message's own units; where a
//! record holds the same thing, a method gives it in the record's.

use smartclock_gps::record::Dops;
use smartclock_gps::record::Ecef;
use smartclock_gps::record::Fix;
use smartclock_gps::record::Health;
use smartclock_gps::record::Position;
use smartclock_gps::record::Satellite;
use smartclock_gps::record::Satellites;
use smartclock_gps::record::Tracking;
use smartclock_gps::record::Velocity;
use smartclock_gps::time::ALMANAC_WEEKS;
use smartclock_gps::time::BROADCAST_WEEKS;
use smartclock_gps::time::GpsSeconds;
use smartclock_gps::time::GpsWeek;
use smartclock_gps::time::LeapChange;
use smartclock_gps::time::LeapEvent;
use smartclock_gps::time::SECONDS_PER_DAY;
use smartclock_gps::units::Centimeters;
use smartclock_gps::units::CentimetersPerSecond;
use smartclock_gps::units::DecibelHertz;
use smartclock_gps::units::Decimeters;
use smartclock_gps::units::Degrees;
use smartclock_gps::units::MilliArcSeconds;
use smartclock_gps::units::Milliseconds;
use smartclock_gps::units::Nanoseconds;
use smartclock_gps::units::Prn;
use smartclock_gps::units::Seconds;
use smartclock_gps::units::TenthDegrees;
use smartclock_gps::units::Tenths;

use crate::fields::Fields;
use crate::fields::bit_field;
use crate::message::SYSTEM_GPS;
use crate::units;
use crate::units::HundredNanodegrees;
use crate::units::HundredthDegrees;
use crate::units::Hundredths;
use crate::units::Millimeters;
use crate::units::MillimetersPerSecond;
use crate::units::NanosecondsPerSecond;
use crate::units::PicosecondsPerSecond;
use crate::units::TenMicrodegrees;

/// `NAV-SAT`'s payload before its first satellite.
pub(crate) const NAV_SAT_HEADER: usize = 8;
/// One satellite's block of `NAV-SAT`.
pub(crate) const NAV_SAT_BLOCK: usize = 12;
/// Where in `NAV-SAT`'s header its count of blocks, `numSvs`, is.
pub(crate) const NAV_SAT_COUNT: usize = 5;

/// The length of a `NAV-SAT` payload that reports `satellites`: what a
/// [`Deframer`](crate::frame::Deframer)'s or an
/// [`Assembler`](crate::assembler::Assembler)'s `N` must reach for a
/// receiver that reports so many, of every system it tracks.
#[must_use]
pub const fn nav_sat_length(satellites: usize) -> usize {
    NAV_SAT_HEADER + NAV_SAT_BLOCK * satellites
}

/// `value` times `numerator` over `denominator`, to the nearest whole.
const fn rescale(value: i64, numerator: i64, denominator: i64) -> i64 {
    (value * numerator + denominator / 2).div_euclid(denominator)
}

/// `value` as an `i32`, held to its ends.
fn to_i32(value: i64) -> i32 {
    i32::try_from(value).unwrap_or(if value < 0 { i32::MIN } else { i32::MAX })
}

/// `value` as a `u16`, held to its ends.
fn to_u16(value: i64) -> u16 {
    u16::try_from(value.max(0)).unwrap_or(u16::MAX)
}

/// The units of `lon` and `lat` in a degree: they are scaled 1e-7.
const ANGLE_UNITS_PER_DEGREE: i64 = 10_000_000;
const MILLI_ARC_SECONDS_PER_DEGREE: i64 = 3_600_000;
/// The units of `headMot` in a tenth of a degree: it is scaled 1e-5.
const HEADING_UNITS_PER_TENTH_DEGREE: i64 = 10_000;
const TENTH_DEGREES_PER_TURN: i64 = 3600;
const MILLIMETERS_PER_CENTIMETER: i64 = 10;
/// The units of a dilution of precision in a tenth: it is scaled 0.01.
const DOP_UNITS_PER_TENTH: i64 = 10;
const DEGREES_PER_TURN: i16 = 360;

/// An angle scaled 1e-7 degrees, in milliarcseconds.
fn milli_arc_seconds(angle: HundredNanodegrees) -> MilliArcSeconds {
    MilliArcSeconds(to_i32(rescale(
        i64::from(angle.0),
        MILLI_ARC_SECONDS_PER_DEGREE,
        ANGLE_UNITS_PER_DEGREE,
    )))
}

/// Millimeters, in centimeters.
fn centimeters(length: Millimeters<i32>) -> Centimeters {
    Centimeters(to_i32(rescale(
        i64::from(length.0),
        1,
        MILLIMETERS_PER_CENTIMETER,
    )))
}

/// A dilution of precision scaled 0.01, in tenths.
fn tenths(dop: Hundredths) -> Tenths {
    Tenths(to_u16(rescale(i64::from(dop.0), 1, DOP_UNITS_PER_TENTH)))
}

/// `NAV-PVT`'s `fixType` (p. 375).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum FixType {
    /// 0: no fix.
    None,
    /// 1: dead reckoning only.
    DeadReckoning,
    /// 2: a 2D fix.
    TwoD,
    /// 3: a 3D fix.
    ThreeD,
    /// 4: satellites combined with dead reckoning.
    WithDeadReckoning,
    /// 5: time only.
    TimeOnly,
    /// A value the document does not give.
    Other(u8),
}

impl FixType {
    const fn new(value: u8) -> Self {
        match value {
            0 => Self::None,
            1 => Self::DeadReckoning,
            2 => Self::TwoD,
            3 => Self::ThreeD,
            4 => Self::WithDeadReckoning,
            5 => Self::TimeOnly,
            other => Self::Other(other),
        }
    }
}

/// `NAV-PVT`'s `valid` bits (the bitfield graphic, p. 376).
const PVT_VALID_DATE: u32 = 0;
const PVT_VALID_TIME: u32 = 1;
const PVT_FULLY_RESOLVED: u32 = 2;
const PVT_VALID_MAGNETIC: u32 = 3;
/// `NAV-PVT`'s `flags` (the bitfield graphic, p. 377): `gnssFixOK`
/// bit 0, `diffSoln` bit 1, `psmState` bits 2 to 4, `headVehValid` bit
/// 5, `carrSoln` bits 6 and 7.  The table under the graphic leaves
/// `psmState` out; the graphic has it.
const PVT_FIX_OK: u32 = 0;
const PVT_DIFFERENTIAL: u32 = 1;
const PVT_POWER_SHIFT: u32 = 2;
const PVT_POWER_WIDTH: u32 = 3;
const PVT_HEADING_VALID: u32 = 5;
const PVT_CARRIER_SHIFT: u32 = 6;
const PVT_CARRIER_WIDTH: u32 = 2;
/// `NAV-PVT`'s `flags2` (the bitfield graphic, p. 377).
const PVT_CONFIRMED_AVAILABLE: u32 = 5;
const PVT_CONFIRMED_DATE: u32 = 6;
const PVT_CONFIRMED_TIME: u32 = 7;
/// `NAV-PVT`'s `flags3` (the bitfield graphic, p. 378), two bytes: `invalidLlh` bit 0,
/// `lastCorrectionAge` bits 1 to 4.
const PVT_INVALID_POSITION: u16 = 1 << 0;
const PVT_CORRECTION_AGE_SHIFT: u32 = 1;
const PVT_CORRECTION_AGE_WIDTH: u32 = 4;

/// `NAV-PVT`'s UTC date and time, as the receiver gives them, with
/// their validity (pp. 375-377).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct PvtDate {
    /// `year`.
    pub year: u16,
    /// `month`, 1 to 12.
    pub month: u8,
    /// `day`, 1 to 31.
    pub day: u8,
    /// `hour`, 0 to 23.
    pub hour: u8,
    /// `min`, 0 to 59.
    pub minute: u8,
    /// `sec`, 0 to 60.
    pub second: u8,
    /// `nano`: the fraction of the second, -1e9 to 1e9.
    pub nanoseconds: Nanoseconds<i32>,
    /// `valid` bit 0, `validDate`.
    pub date_valid: bool,
    /// `valid` bit 1, `validTime`.
    pub time_valid: bool,
    /// `valid` bit 2, `fullyResolved`: no uncertainty of a second.
    pub fully_resolved: bool,
    /// `flags2` bit 5, `confirmedAvai`: whether the two below are
    /// known.
    pub confirmation_available: bool,
    /// `flags2` bit 6, `confirmedDate`.
    pub date_confirmed: bool,
    /// `flags2` bit 7, `confirmedTime`.
    pub time_confirmed: bool,
}

/// `UBX-NAV-PVT` (section 32.17.17, pp. 375-378), every field.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct NavPvt {
    /// `iTOW`: the epoch.
    pub time_of_week: Milliseconds<u32>,
    /// The UTC date and time.
    pub date: PvtDate,
    /// `tAcc`: the time's accuracy estimate.
    pub time_accuracy: Nanoseconds<u32>,
    /// `fixType`.
    pub fix_type: FixType,
    /// `flags` bit 0, `gnssFixOK`: a valid fix, within the masks.
    pub fix_ok: bool,
    /// `flags` bit 1, `diffSoln`: differential corrections applied.
    pub differential: bool,
    /// `flags` bit 5, `headVehValid`.
    pub vehicle_heading_valid: bool,
    /// `flags` bits 2 to 4, `psmState`: the power save mode's state,
    /// which the graphic gives and the table does not describe.
    pub power_save: u8,
    /// `flags` bits 6 and 7, `carrSoln`: 0 none, 1 carrier phase
    /// with floating ambiguities, 2 with fixed ones.
    pub carrier_solution: u8,
    /// `numSV`: the satellites used.
    pub satellites: u8,
    /// `lon`.
    pub longitude: HundredNanodegrees,
    /// `lat`.
    pub latitude: HundredNanodegrees,
    /// `height`, above the ellipsoid.
    pub height: Millimeters<i32>,
    /// `hMSL`, above mean sea level.
    pub mean_sea_level: Millimeters<i32>,
    /// `hAcc`.
    pub horizontal_accuracy: Millimeters<u32>,
    /// `vAcc`.
    pub vertical_accuracy: Millimeters<u32>,
    /// `velN`.
    pub north: MillimetersPerSecond<i32>,
    /// `velE`.
    pub east: MillimetersPerSecond<i32>,
    /// `velD`.
    pub down: MillimetersPerSecond<i32>,
    /// `gSpeed`, the ground speed in two dimensions.
    pub ground_speed: MillimetersPerSecond<i32>,
    /// `headMot`, the heading of motion in two dimensions.
    pub motion_heading: TenMicrodegrees<i32>,
    /// `sAcc`.
    pub speed_accuracy: MillimetersPerSecond<u32>,
    /// `headAcc`, of both headings.
    pub heading_accuracy: TenMicrodegrees<u32>,
    /// `pDOP`.
    pub position_dop: Hundredths,
    /// `flags3` bit 0, `invalidLlh`: the position and heights are not
    /// valid.
    pub position_invalid: bool,
    /// `flags3` bits 1 to 4, `lastCorrectionAge`: the age of the last
    /// differential correction, 0 none, 1 under a second, up to 12 and
    /// more for two minutes and more.
    pub correction_age: u8,
    /// `headVeh`, the vehicle's heading, if `headVehValid`.
    pub vehicle_heading: TenMicrodegrees<i32>,
    /// `magDec`, the magnetic declination, if `validMag`.
    pub magnetic_declination: HundredthDegrees<i16>,
    /// `magAcc`.
    pub magnetic_accuracy: HundredthDegrees<u16>,
    /// `valid` bit 3, `validMag`.
    pub magnetic_valid: bool,
}

impl NavPvt {
    pub(crate) fn parse(fields: Fields<'_>) -> Self {
        let flags = u32::from(fields.u8(21));
        let flags3 = fields.u16(78);
        Self {
            time_of_week: fields.time_of_week(),
            date: PvtDate {
                year: fields.u16(4),
                month: fields.u8(6),
                day: fields.u8(7),
                hour: fields.u8(8),
                minute: fields.u8(9),
                second: fields.u8(10),
                nanoseconds: Nanoseconds(fields.i32(16)),
                date_valid: fields.bit(11, PVT_VALID_DATE),
                time_valid: fields.bit(11, PVT_VALID_TIME),
                fully_resolved: fields.bit(11, PVT_FULLY_RESOLVED),
                confirmation_available: fields.bit(22, PVT_CONFIRMED_AVAILABLE),
                date_confirmed: fields.bit(22, PVT_CONFIRMED_DATE),
                time_confirmed: fields.bit(22, PVT_CONFIRMED_TIME),
            },
            time_accuracy: Nanoseconds(fields.u32(12)),
            fix_type: FixType::new(fields.u8(20)),
            fix_ok: fields.bit(21, PVT_FIX_OK),
            differential: fields.bit(21, PVT_DIFFERENTIAL),
            vehicle_heading_valid: fields.bit(21, PVT_HEADING_VALID),
            power_save: bit_field(flags, PVT_POWER_SHIFT, PVT_POWER_WIDTH),
            carrier_solution: bit_field(flags, PVT_CARRIER_SHIFT, PVT_CARRIER_WIDTH),
            satellites: fields.u8(23),
            longitude: HundredNanodegrees(fields.i32(24)),
            latitude: HundredNanodegrees(fields.i32(28)),
            height: Millimeters(fields.i32(32)),
            mean_sea_level: Millimeters(fields.i32(36)),
            horizontal_accuracy: Millimeters(fields.u32(40)),
            vertical_accuracy: Millimeters(fields.u32(44)),
            north: MillimetersPerSecond(fields.i32(48)),
            east: MillimetersPerSecond(fields.i32(52)),
            down: MillimetersPerSecond(fields.i32(56)),
            ground_speed: MillimetersPerSecond(fields.i32(60)),
            motion_heading: TenMicrodegrees(fields.i32(64)),
            speed_accuracy: MillimetersPerSecond(fields.u32(68)),
            heading_accuracy: TenMicrodegrees(fields.u32(72)),
            position_dop: Hundredths(fields.u16(76)),
            position_invalid: flags3 & PVT_INVALID_POSITION != 0,
            correction_age: bit_field(
                u32::from(flags3),
                PVT_CORRECTION_AGE_SHIFT,
                PVT_CORRECTION_AGE_WIDTH,
            ),
            vehicle_heading: TenMicrodegrees(fields.i32(84)),
            magnetic_declination: HundredthDegrees(fields.i16(88)),
            magnetic_accuracy: HundredthDegrees(fields.u16(90)),
            magnetic_valid: fields.bit(11, PVT_VALID_MAGNETIC),
        }
    }

    /// The fix as a record holds it: [`Fix::None`] unless `gnssFixOK`
    /// is set and `fixType` is 2 to 5.  A fix combined with dead
    /// reckoning counts as three-dimensional; dead reckoning alone is
    /// no fix from satellites.
    #[must_use]
    pub const fn fix(&self) -> Fix {
        if !self.fix_ok {
            return Fix::None;
        }
        match self.fix_type {
            FixType::TwoD => Fix::TwoD,
            FixType::ThreeD | FixType::WithDeadReckoning => Fix::ThreeD,
            FixType::TimeOnly => Fix::TimeOnly,
            FixType::None | FixType::DeadReckoning | FixType::Other(_) => Fix::None,
        }
    }

    /// `lat`, `lon`, `height` and `hMSL`, to the record's units; `None`
    /// with `invalidLlh` set.
    #[must_use]
    pub fn position(&self) -> Option<Position> {
        (!self.position_invalid).then(|| Position {
            longitude: milli_arc_seconds(self.longitude),
            latitude: milli_arc_seconds(self.latitude),
            ellipsoid: centimeters(self.height),
            mean_sea_level: centimeters(self.mean_sea_level),
        })
    }

    /// `gSpeed` and `headMot`, to the record's units.
    #[must_use]
    pub fn velocity(&self) -> Velocity {
        let speed = rescale(
            i64::from(self.ground_speed.0),
            1,
            MILLIMETERS_PER_CENTIMETER,
        );
        let heading = rescale(
            i64::from(self.motion_heading.0),
            1,
            HEADING_UNITS_PER_TENTH_DEGREE,
        )
        .rem_euclid(TENTH_DEGREES_PER_TURN);
        Velocity {
            speed: CentimetersPerSecond(to_u16(speed)),
            heading: TenthDegrees(to_u16(heading)),
        }
    }
}

/// `NAV-TIMEGPS`'s `valid` bits (p. 397).
const TIMEGPS_TIME_OF_WEEK_VALID: u32 = 0;
const TIMEGPS_WEEK_VALID: u32 = 1;
const TIMEGPS_LEAP_VALID: u32 = 2;

/// `UBX-NAV-TIMEGPS` (section 32.17.30, pp. 397-398), every field.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct NavTimeGps {
    /// `iTOW`: the epoch, and with [`Self::fraction`] its time of week.
    pub time_of_week: Milliseconds<u32>,
    /// `fTOW`: what to add to `iTOW` for the precise time of week.
    pub fraction: Nanoseconds<i32>,
    /// `week`, as the receiver resolved it.
    pub week_number: i16,
    /// `leapS`, GPS less UTC.
    pub gps_less_utc: Seconds<i8>,
    /// `valid` bit 0, `towValid`.
    pub time_of_week_valid: bool,
    /// `valid` bit 1, `weekValid`.
    pub week_valid: bool,
    /// `valid` bit 2, `leapSValid`.
    pub leap_seconds_valid: bool,
    /// `tAcc`, the receiver's estimate of the time's accuracy.
    pub accuracy: Nanoseconds<u32>,
}

impl NavTimeGps {
    pub(crate) fn parse(fields: Fields<'_>) -> Self {
        Self {
            time_of_week: fields.time_of_week(),
            fraction: Nanoseconds(fields.i32(4)),
            week_number: fields.i16(8),
            gps_less_utc: Seconds(fields.i8(10)),
            time_of_week_valid: fields.bit(11, TIMEGPS_TIME_OF_WEEK_VALID),
            week_valid: fields.bit(11, TIMEGPS_WEEK_VALID),
            leap_seconds_valid: fields.bit(11, TIMEGPS_LEAP_VALID),
            accuracy: Nanoseconds(fields.u32(12)),
        }
    }

    /// `week`; `None` without `towValid` and `weekValid` both, or for a
    /// week below zero.
    #[must_use]
    pub fn week(&self) -> Option<GpsWeek> {
        u16::try_from(self.week_number)
            .ok()
            .filter(|_| self.time_of_week_valid && self.week_valid)
            .map(GpsWeek::new)
    }

    /// `leapS`; `None` without `leapSValid`.
    #[must_use]
    pub const fn leap_seconds(&self) -> Option<Seconds<i8>> {
        if self.leap_seconds_valid {
            Some(self.gps_less_utc)
        } else {
            None
        }
    }

    /// The epoch's second, the nearest to its time of week.  With a
    /// `reference`, the week is taken for its ten broadcast bits alone
    /// and is the one nearest `reference` that has them: for a receiver
    /// whose own resolution cannot be trusted.  `None` without a valid
    /// time of week and week.
    #[must_use]
    pub fn seconds(&self, reference: Option<GpsWeek>) -> Option<GpsSeconds> {
        let week = self.week()?;
        let week = match reference {
            Some(reference) => reference.nearest(week.get(), BROADCAST_WEEKS)?,
            None => week,
        };
        GpsSeconds::nearest(week, self.time_of_week, self.fraction)
    }
}

/// `NAV-TIMELS`'s version this crate knows (p. 398).
const TIMELS_VERSION: u8 = 0;
/// `srcOfCurrLs`: the firmware's built-in count, which "can be
/// outdated", and no source known (p. 398).
const TIMELS_COUNT_DEFAULT: u8 = 0;
const TIMELS_COUNT_UNKNOWN: u8 = 255;
/// `srcOfLsChange`: the systems whose week and day are GPS's, Galileo's
/// week being GPS's less a multiple of [`ALMANAC_WEEKS`] (p. 399).
const TIMELS_CHANGE_GPS: u8 = 2;
const TIMELS_CHANGE_GALILEO: u8 = 5;
/// `NAV-TIMELS`'s `valid` bits (p. 400).
const TIMELS_COUNT_VALID: u32 = 0;
const TIMELS_EVENT_VALID: u32 = 1;
/// `dateOfLsGpsDn` runs from 1, Sunday, to 7, Saturday (p. 399).
const DAYS: core::ops::RangeInclusive<u16> = 1..=7;

/// How far a leap second placed by its week and day may lie from where
/// `timeToLsEvent` puts it and still be believed.  The document does
/// not say which second the countdown ends on, the inserted one or the
/// midnight after it, so they may differ by one.
const LEAP_SLACK: i64 = 2;

/// A leap second as `UBX-NAV-TIMELS` announces it (p. 399).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct LeapNotice {
    /// `lsChange`: which way.
    pub change: LeapChange,
    /// `timeToLsEvent`: seconds until it, zero or less once it has
    /// come.
    pub until: Seconds<i32>,
    /// `dateOfLsGpsWn`: the week of the day it ends.  Whether this is
    /// the week in full or the eight bits a satellite sends, the
    /// document does not say.
    pub week: u16,
    /// `dateOfLsGpsDn`: that day, 1 for Sunday to 7 for Saturday.
    pub day: u16,
}

impl LeapNotice {
    /// Whether it is still to come.
    #[must_use]
    pub const fn pending(&self) -> bool {
        self.until.0 > 0
    }

    /// The leap second, `now` being the second the notice was given in
    /// and `before` GPS less UTC until the leap.
    ///
    /// It is put at the end of [`Self::day`] of [`Self::week`], the
    /// week read modulo [`ALMANAC_WEEKS`] and resolved nearest the week
    /// [`Self::until`] counts to, so it is found whether the receiver
    /// gives the week in full or cut short, at a week's end as on any
    /// other day.  `None` if that is not where the countdown ends, to
    /// within two seconds.
    #[must_use]
    pub fn event(&self, now: GpsSeconds, before: Seconds<i8>) -> Option<LeapEvent> {
        let counted = now.checked_add(Seconds(i64::from(self.until.0)))?;
        let week = counted.week()?.nearest(self.week, ALMANAC_WEEKS)?;
        let days = Seconds(i64::from(self.day) * SECONDS_PER_DAY);
        // GPS time with no offset reads as UTC's calendar does.
        let midnight = GpsSeconds::week_start(week)
            .checked_add(days)?
            .to_utc(Seconds(0))?;
        let event = LeapEvent::at_day_end(midnight, before, self.change)?;
        let apart = event.at.checked_since(counted)?;
        (apart.0.abs() <= LEAP_SLACK).then_some(event)
    }
}

/// `UBX-NAV-TIMELS` (section 32.17.31, pp. 398-400), every field.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct NavTimeLs {
    /// `iTOW`: the epoch.
    pub time_of_week: Milliseconds<u32>,
    /// `srcOfCurrLs`: where `currLs` comes from.
    pub count_source: u8,
    /// `currLs`, GPS less UTC.
    pub current: Seconds<i8>,
    /// `srcOfLsChange`: where the leap second's notice comes from.
    pub change_source: u8,
    /// `lsChange`: the leap second's step, -1, 0 or 1.
    pub change: i8,
    /// `timeToLsEvent`.
    pub until: Seconds<i32>,
    /// `dateOfLsGpsWn`.
    pub event_week: u16,
    /// `dateOfLsGpsDn`.
    pub event_day: u16,
    /// `valid` bit 0, `validCurrLs`.
    pub count_valid: bool,
    /// `valid` bit 1, `validTimeToLsEvent`.
    pub event_valid: bool,
}

impl NavTimeLs {
    /// `None` for a version other than 0.
    pub(crate) fn parse(fields: Fields<'_>) -> Option<Self> {
        (fields.u8(4) == TIMELS_VERSION).then(|| Self {
            time_of_week: fields.time_of_week(),
            count_source: fields.u8(8),
            current: Seconds(fields.i8(9)),
            change_source: fields.u8(10),
            change: fields.i8(11),
            until: Seconds(fields.i32(12)),
            event_week: fields.u16(16),
            event_day: fields.u16(18),
            count_valid: fields.bit(23, TIMELS_COUNT_VALID),
            event_valid: fields.bit(23, TIMELS_EVENT_VALID),
        })
    }

    /// `currLs`; `None` without `validCurrLs`, and when its source is
    /// the firmware's built-in value or unknown.
    #[must_use]
    pub const fn count(&self) -> Option<Seconds<i8>> {
        match self.count_source {
            _ if !self.count_valid => None,
            TIMELS_COUNT_DEFAULT | TIMELS_COUNT_UNKNOWN => None,
            _ => Some(self.current),
        }
    }

    /// The leap second announced or last seen; `None` without
    /// `validTimeToLsEvent`, with an `lsChange` that is not one second
    /// either way, with a day outside 1 to 7, and from a source whose
    /// week and day are not GPS's.
    #[must_use]
    pub fn notice(&self) -> Option<LeapNotice> {
        let change = match self.change {
            1 => LeapChange::Insert,
            -1 => LeapChange::Remove,
            _ => return None,
        };
        let dated = self.event_valid
            && matches!(
                self.change_source,
                TIMELS_CHANGE_GPS | TIMELS_CHANGE_GALILEO
            )
            && DAYS.contains(&self.event_day);
        dated.then_some(LeapNotice {
            change,
            until: self.until,
            week: self.event_week,
            day: self.event_day,
        })
    }
}

/// `UBX-NAV-DOP` (section 32.17.6, pp. 361-362), every field.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct NavDop {
    /// `iTOW`: the epoch.
    pub time_of_week: Milliseconds<u32>,
    /// `gDOP`.
    pub geometric: Hundredths,
    /// `pDOP`.
    pub position: Hundredths,
    /// `tDOP`.
    pub time: Hundredths,
    /// `vDOP`.
    pub vertical: Hundredths,
    /// `hDOP`.
    pub horizontal: Hundredths,
    /// `nDOP`.
    pub northing: Hundredths,
    /// `eDOP`.
    pub easting: Hundredths,
}

impl NavDop {
    pub(crate) fn parse(fields: Fields<'_>) -> Self {
        Self {
            time_of_week: fields.time_of_week(),
            geometric: Hundredths(fields.u16(4)),
            position: Hundredths(fields.u16(6)),
            time: Hundredths(fields.u16(8)),
            vertical: Hundredths(fields.u16(10)),
            horizontal: Hundredths(fields.u16(12)),
            northing: Hundredths(fields.u16(14)),
            easting: Hundredths(fields.u16(16)),
        }
    }

    /// The record's dilutions, to the nearest tenth.
    #[must_use]
    pub fn dops(&self) -> Dops {
        Dops {
            geometric: tenths(self.geometric),
            position: tenths(self.position),
            time: tenths(self.time),
            vertical: tenths(self.vertical),
            horizontal: tenths(self.horizontal),
        }
    }
}

/// `UBX-NAV-POSECEF` (section 32.17.15, p. 374), every field.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct NavPosEcef {
    /// `iTOW`: the epoch.
    pub time_of_week: Milliseconds<u32>,
    /// `ecefX`, `ecefY` and `ecefZ`, which are centimeters.
    pub position: Ecef,
    /// `pAcc`, the position's accuracy estimate.
    pub accuracy: units::Centimeters<u32>,
}

impl NavPosEcef {
    pub(crate) fn parse(fields: Fields<'_>) -> Self {
        Self {
            time_of_week: fields.time_of_week(),
            position: Ecef {
                x: Centimeters(fields.i32(4)),
                y: Centimeters(fields.i32(8)),
                z: Centimeters(fields.i32(12)),
            },
            accuracy: units::Centimeters(fields.u32(16)),
        }
    }
}

/// `UBX-NAV-VELECEF` (section 32.17.33, p. 401), every field: the velocity in
/// Earth-centered coordinates.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct NavVelEcef {
    /// `iTOW`: the epoch.
    pub time_of_week: Milliseconds<u32>,
    /// `ecefVX`.
    pub x: units::CentimetersPerSecond<i32>,
    /// `ecefVY`.
    pub y: units::CentimetersPerSecond<i32>,
    /// `ecefVZ`.
    pub z: units::CentimetersPerSecond<i32>,
    /// `sAcc`, the speed's accuracy estimate.
    pub accuracy: units::CentimetersPerSecond<u32>,
}

impl NavVelEcef {
    pub(crate) fn parse(fields: Fields<'_>) -> Self {
        Self {
            time_of_week: fields.time_of_week(),
            x: units::CentimetersPerSecond(fields.i32(4)),
            y: units::CentimetersPerSecond(fields.i32(8)),
            z: units::CentimetersPerSecond(fields.i32(12)),
            accuracy: units::CentimetersPerSecond(fields.u32(16)),
        }
    }
}

/// `UBX-NAV-CLOCK` (section 32.17.3, p. 359), every field: the receiver's
/// clock solution.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct NavClock {
    /// `iTOW`: the epoch.
    pub time_of_week: Milliseconds<u32>,
    /// `clkB`, the clock's bias.
    pub bias: Nanoseconds<i32>,
    /// `clkD`, its drift.
    pub drift: NanosecondsPerSecond,
    /// `tAcc`, the time's accuracy estimate.
    pub time_accuracy: Nanoseconds<u32>,
    /// `fAcc`, the frequency's accuracy estimate.
    pub frequency_accuracy: PicosecondsPerSecond,
}

impl NavClock {
    pub(crate) fn parse(fields: Fields<'_>) -> Self {
        Self {
            time_of_week: fields.time_of_week(),
            bias: Nanoseconds(fields.i32(4)),
            drift: NanosecondsPerSecond(fields.i32(8)),
            time_accuracy: Nanoseconds(fields.u32(12)),
            frequency_accuracy: PicosecondsPerSecond(fields.u32(16)),
        }
    }
}

/// `UBX-NAV-EOE` (section 32.17.8, p. 363): the epoch's navigation
/// messages have all been sent.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct NavEoe {
    /// `iTOW`: the epoch.
    pub time_of_week: Milliseconds<u32>,
}

/// `NAV-SAT`'s version this crate knows (p. 381).
const SAT_VERSION: u8 = 1;
/// `NAV-SAT`'s `flags` (the bitfield graphic, p. 382): each bit's
/// place, and the shift and width of each field of more than one.
const SAT_QUALITY_SHIFT: u32 = 0;
const SAT_QUALITY_WIDTH: u32 = 3;
const SAT_USED: u32 = 3;
const SAT_HEALTH_SHIFT: u32 = 4;
const SAT_HEALTH_WIDTH: u32 = 2;
const SAT_DIFFERENTIAL: u32 = 6;
const SAT_SMOOTHED: u32 = 7;
const SAT_ORBIT_SHIFT: u32 = 8;
const SAT_ORBIT_WIDTH: u32 = 3;
const SAT_EPHEMERIS: u32 = 11;
const SAT_ALMANAC: u32 = 12;
const SAT_ASSIST_NOW_OFFLINE: u32 = 13;
const SAT_ASSIST_NOW_AUTONOMOUS: u32 = 14;
const SAT_SBAS_CORRECTIONS: u32 = 16;
const SAT_RTCM_CORRECTIONS: u32 = 17;
const SAT_SLAS_CORRECTIONS: u32 = 18;
const SAT_SPARTN_CORRECTIONS: u32 = 19;
const SAT_PSEUDORANGE_CORRECTIONS: u32 = 20;
const SAT_CARRIER_CORRECTIONS: u32 = 21;
const SAT_DOPPLER_CORRECTIONS: u32 = 22;
const SAT_CLAS_CORRECTIONS: u32 = 23;
/// `qualityInd` values (p. 382).
const QUALITY_NO_SIGNAL: u8 = 0;
const QUALITY_SEARCHING: u8 = 1;
const QUALITY_CODE_LOCKED: u8 = 4;
/// `health` values (p. 382).
const HEALTH_HEALTHY: u8 = 1;
const HEALTH_UNHEALTHY: u8 = 2;
/// The elevations of a satellite above the horizon.  `elev` is "unknown
/// if out of range" of 90 degrees either way (p. 381).
const ELEVATIONS: core::ops::RangeInclusive<i8> = 0..=90;

/// Where a satellite's orbit comes from: `NAV-SAT`'s `orbitSource`
/// (p. 382).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum OrbitSource {
    /// 0: no orbit information.
    None,
    /// 1: the ephemeris.
    Ephemeris,
    /// 2: the almanac.
    Almanac,
    /// 3: `AssistNow` Offline.
    AssistNowOffline,
    /// 4: `AssistNow` Autonomous.
    AssistNowAutonomous,
    /// 5 to 7: other.
    Other(u8),
}

impl OrbitSource {
    const fn new(value: u8) -> Self {
        match value {
            0 => Self::None,
            1 => Self::Ephemeris,
            2 => Self::Almanac,
            3 => Self::AssistNowOffline,
            4 => Self::AssistNowAutonomous,
            other => Self::Other(other),
        }
    }
}

/// `UBX-NAV-SAT`'s header (pp. 381-382), before its satellites.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct NavSatHeader {
    /// `iTOW`: the epoch.
    pub time_of_week: Milliseconds<u32>,
    /// `numSvs`: how many satellites' blocks follow.
    pub satellites: u8,
}

impl NavSatHeader {
    /// The header of a payload with `blocks` satellites' blocks after
    /// it.  `None` for a version other than 1, and for a `numSvs` that
    /// is not `blocks`: a payload not of its own exact length.
    pub(crate) fn parse(fields: Fields<'_>, blocks: usize) -> Option<Self> {
        let satellites = fields.u8(NAV_SAT_COUNT);
        (fields.u8(4) == SAT_VERSION && usize::from(satellites) == blocks).then(|| Self {
            time_of_week: fields.time_of_week(),
            satellites,
        })
    }
}

/// One satellite's block of `UBX-NAV-SAT` (pp. 381-383), every field.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct SatelliteInfo {
    /// `gnssId`: which system's it is, [`SYSTEM_GPS`] for GPS.
    pub system: u8,
    /// `svId`: its number in that system.
    pub id: u8,
    /// `cno`: its signal.
    pub signal: DecibelHertz,
    /// `elev`: its elevation, not known if beyond 90 either way.
    pub elevation: Degrees<i8>,
    /// `azim`: its azimuth, not known if the elevation is not.
    pub azimuth: Degrees<i16>,
    /// `prRes`: its pseudorange residual.
    pub residual: Decimeters,
    /// `qualityInd`, 0 to 7: 0 no signal, 1 searching, 2 acquired, 3
    /// unusable, 4 code locked, 5 to 7 code and carrier locked.
    pub quality: u8,
    /// `svUsed`: whether the fix uses it.
    pub used: bool,
    /// `health`.
    pub health: Health,
    /// `diffCorr`: differential correction data is available.
    pub differential: bool,
    /// `smoothed`: carrier smoothed pseudorange used.
    pub smoothed: bool,
    /// `orbitSource`.
    pub orbit_source: OrbitSource,
    /// `ephAvail`: whether its ephemeris is held.
    pub ephemeris: bool,
    /// `almAvail`: whether its almanac is held.
    pub almanac: bool,
    /// `anoAvail`: `AssistNow` Offline data is available.
    pub assist_now_offline: bool,
    /// `aopAvail`: `AssistNow` Autonomous data is available.
    pub assist_now_autonomous: bool,
    /// `sbasCorrUsed`: SBAS corrections used.
    pub sbas_corrections: bool,
    /// `rtcmCorrUsed`: RTCM corrections used.
    pub rtcm_corrections: bool,
    /// `slasCorrUsed`: QZSS SLAS corrections used.
    pub slas_corrections: bool,
    /// `spartnCorrUsed`: SPARTN corrections used.
    pub spartn_corrections: bool,
    /// `prCorrUsed`: pseudorange corrections used.
    pub pseudorange_corrections: bool,
    /// `crCorrUsed`: carrier range corrections used.
    pub carrier_corrections: bool,
    /// `doCorrUsed`: range rate (Doppler) corrections used.
    pub doppler_corrections: bool,
    /// `clasCorrUsed`: CLAS corrections used.
    pub clas_corrections: bool,
}

impl SatelliteInfo {
    pub(crate) fn parse(fields: Fields<'_>) -> Self {
        let flags = fields.u32(8);
        let bit = |place: u32| flags & (1 << place) != 0;
        Self {
            system: fields.u8(0),
            id: fields.u8(1),
            signal: DecibelHertz(fields.u8(2)),
            elevation: Degrees(fields.i8(3)),
            azimuth: Degrees(fields.i16(4)),
            residual: Decimeters(fields.i16(6)),
            quality: bit_field(flags, SAT_QUALITY_SHIFT, SAT_QUALITY_WIDTH),
            used: bit(SAT_USED),
            health: match bit_field(flags, SAT_HEALTH_SHIFT, SAT_HEALTH_WIDTH) {
                HEALTH_HEALTHY => Health::Healthy,
                HEALTH_UNHEALTHY => Health::Unhealthy,
                _ => Health::Unknown,
            },
            differential: bit(SAT_DIFFERENTIAL),
            smoothed: bit(SAT_SMOOTHED),
            orbit_source: OrbitSource::new(bit_field(flags, SAT_ORBIT_SHIFT, SAT_ORBIT_WIDTH)),
            ephemeris: bit(SAT_EPHEMERIS),
            almanac: bit(SAT_ALMANAC),
            assist_now_offline: bit(SAT_ASSIST_NOW_OFFLINE),
            assist_now_autonomous: bit(SAT_ASSIST_NOW_AUTONOMOUS),
            sbas_corrections: bit(SAT_SBAS_CORRECTIONS),
            rtcm_corrections: bit(SAT_RTCM_CORRECTIONS),
            slas_corrections: bit(SAT_SLAS_CORRECTIONS),
            spartn_corrections: bit(SAT_SPARTN_CORRECTIONS),
            pseudorange_corrections: bit(SAT_PSEUDORANGE_CORRECTIONS),
            carrier_corrections: bit(SAT_CARRIER_CORRECTIONS),
            doppler_corrections: bit(SAT_DOPPLER_CORRECTIONS),
            clas_corrections: bit(SAT_CLAS_CORRECTIONS),
        }
    }

    /// This satellite as a record holds one: `None` unless it is a GPS
    /// satellite at a known place above the horizon.
    ///
    /// How far a channel has got with it is this crate's reading of
    /// `qualityInd`, which does not name an Oncore's stages: searching
    /// below 2, code lock at 2 and 3, and from 4, where the receiver
    /// has its time, reading its ephemeris until `ephAvail`, then
    /// ready for the fix.
    #[must_use]
    pub fn satellite(&self) -> Option<Satellite> {
        let prn = Prn::new(self.id).filter(|_| self.system == SYSTEM_GPS)?;
        if !ELEVATIONS.contains(&self.elevation.0) {
            return None;
        }
        let azimuth = u16::try_from(self.azimuth.0.rem_euclid(DEGREES_PER_TURN)).ok()?;
        let mut satellite = Satellite::new(prn, self.elevation, Degrees(azimuth));
        satellite.signal = (self.quality != QUALITY_NO_SIGNAL).then_some(self.signal);
        satellite.tracking = match self.quality {
            QUALITY_NO_SIGNAL | QUALITY_SEARCHING => Tracking::Searching,
            quality if quality < QUALITY_CODE_LOCKED => Tracking::CodeLock,
            _ if self.ephemeris => Tracking::Position,
            _ => Tracking::Ephemeris,
        };
        satellite.used = self.used;
        satellite.health = self.health;
        satellite.ephemeris = self.ephemeris;
        Some(satellite)
    }
}

/// `UBX-NAV-SAT` (section 32.17.20, pp. 381-383), whole.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct NavSat<'a> {
    /// Its header.
    pub header: NavSatHeader,
    /// The satellites' blocks.
    blocks: &'a [u8],
}

impl<'a> NavSat<'a> {
    /// `None` as for [`NavSatHeader::parse`].
    pub(crate) fn parse(payload: &'a [u8]) -> Option<Self> {
        let blocks = payload.get(NAV_SAT_HEADER..)?;
        Some(Self {
            header: NavSatHeader::parse(Fields(payload), blocks.len() / NAV_SAT_BLOCK)?,
            blocks,
        })
    }

    /// The satellites, in the message's order.
    pub fn satellites(&self) -> impl Iterator<Item = SatelliteInfo> + use<'a> {
        self.blocks
            .as_chunks::<NAV_SAT_BLOCK>()
            .0
            .iter()
            .map(|block| SatelliteInfo::parse(Fields(block)))
    }

    /// The GPS satellites with a known place in the sky, as a record
    /// holds them: those the fix uses first, then the rest, each group
    /// in the message's order.  Past a record's room the lowest in the
    /// sky go, as [`Satellites::push`] has it.
    #[must_use]
    pub fn sky(&self) -> Satellites {
        let mut sky = Satellites::EMPTY;
        for used in [true, false] {
            self.satellites()
                .filter(|info| info.used == used)
                .filter_map(|info| info.satellite())
                .for_each(|satellite| sky.push(satellite));
        }
        sky
    }
}
