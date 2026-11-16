//! The navigation results' messages, `UBX-NAV-*`.

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
use smartclock_gps::units::Degrees;
use smartclock_gps::units::MilliArcSeconds;
use smartclock_gps::units::Milliseconds;
use smartclock_gps::units::Nanoseconds;
use smartclock_gps::units::Prn;
use smartclock_gps::units::Seconds;
use smartclock_gps::units::TenthDegrees;
use smartclock_gps::units::Tenths;

use crate::fields::Fields;
/// `NAV-SAT`'s payload before its first satellite.
pub(crate) const NAV_SAT_HEADER: usize = 8;
/// One satellite's block of `NAV-SAT`.
pub(crate) const NAV_SAT_BLOCK: usize = 12;

/// The length of a `NAV-SAT` payload that reports `satellites`: what a
/// [`Deframer`](crate::frame::Deframer)'s or an [`Assembler`](crate::assembler::Assembler)'s `N` must reach for a receiver
/// that reports so many, of every system it tracks.
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
fn milli_arc_seconds(angle: i32) -> MilliArcSeconds {
    MilliArcSeconds(to_i32(rescale(
        i64::from(angle),
        MILLI_ARC_SECONDS_PER_DEGREE,
        ANGLE_UNITS_PER_DEGREE,
    )))
}

/// Millimeters, in centimeters.
fn centimeters(millimeters: i32) -> Centimeters {
    Centimeters(to_i32(rescale(
        i64::from(millimeters),
        1,
        MILLIMETERS_PER_CENTIMETER,
    )))
}

/// A dilution of precision scaled 0.01, in tenths.
fn tenths(dop: u16) -> Tenths {
    Tenths(to_u16(rescale(i64::from(dop), 1, DOP_UNITS_PER_TENTH)))
}

/// `NAV-PVT`'s `fixType` values (p. 375).  1, dead reckoning alone, is
/// no fix from satellites, and is left out.
const FIX_TWO_D: u8 = 2;
const FIX_THREE_D: u8 = 3;
const FIX_WITH_DEAD_RECKONING: u8 = 4;
const FIX_TIME_ONLY: u8 = 5;
/// `NAV-PVT`'s `flags` bit 0, `gnssFixOK` (p. 377).
const PVT_FIX_OK: u8 = 1 << 0;
/// `NAV-PVT`'s `flags3` bit 0, `invalidLlh` (p. 378).
const PVT_INVALID_POSITION: u16 = 1 << 0;

/// `UBX-NAV-PVT` (section 32.17.17, pp. 375-378): what of it a record
/// holds.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct NavPvt {
    /// `iTOW`: the epoch.
    pub time_of_week: Milliseconds<u32>,
    /// The fix: `fixType`, and [`Fix::None`] unless `gnssFixOK` is set.
    /// A fix combined with dead reckoning counts as three-dimensional.
    pub fix: Fix,
    /// `lat`, `lon`, `height` and `hMSL`, to the record's units; `None`
    /// with `invalidLlh` set.
    pub position: Option<Position>,
    /// `gSpeed` and `headMot`, to the record's units.
    pub velocity: Velocity,
}

impl NavPvt {
    pub(crate) fn parse(fields: Fields<'_>) -> Self {
        let fix = match fields.u8(20) {
            _ if fields.u8(21) & PVT_FIX_OK == 0 => Fix::None,
            FIX_TWO_D => Fix::TwoD,
            FIX_THREE_D | FIX_WITH_DEAD_RECKONING => Fix::ThreeD,
            FIX_TIME_ONLY => Fix::TimeOnly,
            _ => Fix::None,
        };
        let position = (fields.u16(78) & PVT_INVALID_POSITION == 0).then(|| Position {
            longitude: milli_arc_seconds(fields.i32(24)),
            latitude: milli_arc_seconds(fields.i32(28)),
            ellipsoid: centimeters(fields.i32(32)),
            mean_sea_level: centimeters(fields.i32(36)),
        });
        let speed = rescale(i64::from(fields.i32(60)), 1, MILLIMETERS_PER_CENTIMETER);
        let heading = rescale(i64::from(fields.i32(64)), 1, HEADING_UNITS_PER_TENTH_DEGREE)
            .rem_euclid(TENTH_DEGREES_PER_TURN);
        Self {
            time_of_week: fields.time_of_week(),
            fix,
            position,
            velocity: Velocity {
                speed: CentimetersPerSecond(to_u16(speed)),
                heading: TenthDegrees(to_u16(heading)),
            },
        }
    }
}

/// `NAV-TIMEGPS`'s `valid` bits (p. 397).
const TIMEGPS_TIME_OF_WEEK_VALID: u8 = 1 << 0;
const TIMEGPS_WEEK_VALID: u8 = 1 << 1;
const TIMEGPS_LEAP_VALID: u8 = 1 << 2;

/// `UBX-NAV-TIMEGPS` (section 32.17.30, pp. 397-398).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct NavTimeGps {
    /// `iTOW`: the epoch, and with [`Self::fraction`] its time of week.
    pub time_of_week: Milliseconds<u32>,
    /// `fTOW`: what to add to `iTOW` for the precise time of week.
    pub fraction: Nanoseconds<i32>,
    /// `week`, as the receiver resolved it; `None` without `towValid`
    /// and `weekValid` both, or for a week below zero.
    pub week: Option<GpsWeek>,
    /// `leapS`, GPS less UTC; `None` without `leapSValid`.
    pub leap_seconds: Option<Seconds<i8>>,
    /// `tAcc`, the receiver's estimate of the time's accuracy.
    pub accuracy: Nanoseconds<u32>,
}

impl NavTimeGps {
    pub(crate) fn parse(fields: Fields<'_>) -> Self {
        let valid = fields.u8(11);
        let timed = TIMEGPS_TIME_OF_WEEK_VALID | TIMEGPS_WEEK_VALID;
        let week = u16::try_from(fields.i16(8)).ok().map(GpsWeek::new);
        Self {
            time_of_week: fields.time_of_week(),
            fraction: Nanoseconds(fields.i32(4)),
            week: week.filter(|_| valid & timed == timed),
            leap_seconds: (valid & TIMEGPS_LEAP_VALID != 0).then(|| Seconds(fields.i8(10))),
            accuracy: Nanoseconds(fields.u32(12)),
        }
    }

    /// The epoch's second, the nearest to its time of week.  With a
    /// `reference`, the week is taken for its ten broadcast bits alone
    /// and is the one nearest `reference` that has them: for a receiver
    /// whose own resolution cannot be trusted.  `None` without a valid
    /// time of week and week.
    #[must_use]
    pub fn seconds(&self, reference: Option<GpsWeek>) -> Option<GpsSeconds> {
        let week = self.week?;
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
const TIMELS_COUNT_VALID: u8 = 1 << 0;
const TIMELS_EVENT_VALID: u8 = 1 << 1;
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

/// `UBX-NAV-TIMELS` (section 32.17.31, pp. 398-400).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct NavTimeLs {
    /// `iTOW`: the epoch.
    pub time_of_week: Milliseconds<u32>,
    /// `currLs`, GPS less UTC; `None` without `validCurrLs`, and when
    /// its source is the firmware's built-in value or unknown.
    pub count: Option<Seconds<i8>>,
    /// The leap second announced or last seen; `None` without
    /// `validTimeToLsEvent`, with an `lsChange` that is not one second
    /// either way, with a day outside 1 to 7, and from a source whose
    /// week and day are not GPS's.
    pub notice: Option<LeapNotice>,
}

impl NavTimeLs {
    /// `None` for a version other than 0.
    pub(crate) fn parse(fields: Fields<'_>) -> Option<Self> {
        if fields.u8(4) != TIMELS_VERSION {
            return None;
        }
        let valid = fields.u8(23);
        let count_known = valid & TIMELS_COUNT_VALID != 0
            && !matches!(fields.u8(8), TIMELS_COUNT_DEFAULT | TIMELS_COUNT_UNKNOWN);
        let change = match fields.i8(11) {
            1 => Some(LeapChange::Insert),
            -1 => Some(LeapChange::Remove),
            _ => None,
        };
        let day = fields.u16(18);
        let dated = valid & TIMELS_EVENT_VALID != 0
            && matches!(fields.u8(10), TIMELS_CHANGE_GPS | TIMELS_CHANGE_GALILEO)
            && DAYS.contains(&day);
        Some(Self {
            time_of_week: fields.time_of_week(),
            count: count_known.then(|| Seconds(fields.i8(9))),
            notice: change.filter(|_| dated).map(|change| LeapNotice {
                change,
                until: Seconds(fields.i32(12)),
                week: fields.u16(16),
                day,
            }),
        })
    }
}

/// `UBX-NAV-DOP` (section 32.17.6, pp. 361-362).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct NavDop {
    /// `iTOW`: the epoch.
    pub time_of_week: Milliseconds<u32>,
    /// `gDOP`, `pDOP`, `hDOP`, `vDOP` and `tDOP`, to the nearest tenth.
    pub dops: Dops,
}

impl NavDop {
    pub(crate) fn parse(fields: Fields<'_>) -> Self {
        Self {
            time_of_week: fields.time_of_week(),
            dops: Dops {
                geometric: tenths(fields.u16(4)),
                position: tenths(fields.u16(6)),
                time: tenths(fields.u16(8)),
                vertical: tenths(fields.u16(10)),
                horizontal: tenths(fields.u16(12)),
            },
        }
    }
}

/// `UBX-NAV-POSECEF` (section 32.17.15, p. 374).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct NavPosEcef {
    /// `iTOW`: the epoch.
    pub time_of_week: Milliseconds<u32>,
    /// `ecefX`, `ecefY` and `ecefZ`, which are centimeters.
    pub position: Ecef,
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
/// `gnssId` for GPS (appendix A, p. 446).
const SYSTEM_GPS: u8 = 0;
/// `NAV-SAT`'s `flags` (p. 382): `qualityInd` in bits 0 to 2, `svUsed`
/// bit 3, `health` in bits 4 and 5, `ephAvail` bit 11.
const SAT_QUALITY: u32 = 0b111;
const SAT_USED: u32 = 1 << 3;
const SAT_HEALTH_SHIFT: u32 = 4;
const SAT_HEALTH: u32 = 0b11;
const SAT_EPHEMERIS: u32 = 1 << 11;
/// `qualityInd` values (p. 382).
const QUALITY_NO_SIGNAL: u8 = 0;
const QUALITY_SEARCHING: u8 = 1;
const QUALITY_CODE_LOCKED: u8 = 4;
/// `health` values (p. 382).
const HEALTH_HEALTHY: u32 = 1;
const HEALTH_UNHEALTHY: u32 = 2;
/// The elevations of a satellite above the horizon.  `elev` is "unknown
/// if out of range" of 90 degrees either way (p. 381).
const ELEVATIONS: core::ops::RangeInclusive<i8> = 0..=90;

/// One satellite's block of `UBX-NAV-SAT` (pp. 381-383).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct SatelliteInfo {
    /// `gnssId`: which system's it is, 0 for GPS.
    pub system: u8,
    /// `svId`: its number in that system.
    pub id: u8,
    /// `cno`: its signal.
    pub signal: DecibelHertz,
    /// `elev`: its elevation, not known if beyond 90 either way.
    pub elevation: Degrees<i8>,
    /// `azim`: its azimuth, not known if the elevation is not.
    pub azimuth: Degrees<i16>,
    /// `qualityInd`, 0 to 7: 0 no signal, 1 searching, 2 acquired, 3
    /// unusable, 4 code locked, 5 to 7 code and carrier locked.
    pub quality: u8,
    /// `svUsed`: whether the fix uses it.
    pub used: bool,
    /// `health`.
    pub health: Health,
    /// `ephAvail`: whether its ephemeris is held.
    pub ephemeris: bool,
}

impl SatelliteInfo {
    pub(crate) fn parse(fields: Fields<'_>) -> Self {
        let flags = fields.u32(8);
        Self {
            system: fields.u8(0),
            id: fields.u8(1),
            signal: DecibelHertz(fields.u8(2)),
            elevation: Degrees(fields.i8(3)),
            azimuth: Degrees(fields.i16(4)),
            quality: u8::try_from(flags & SAT_QUALITY).unwrap_or(QUALITY_NO_SIGNAL),
            used: flags & SAT_USED != 0,
            health: match (flags >> SAT_HEALTH_SHIFT) & SAT_HEALTH {
                HEALTH_HEALTHY => Health::Healthy,
                HEALTH_UNHEALTHY => Health::Unhealthy,
                _ => Health::Unknown,
            },
            ephemeris: flags & SAT_EPHEMERIS != 0,
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

/// `UBX-NAV-SAT` (section 32.17.20, pp. 381-383).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct NavSat<'a> {
    /// `iTOW`: the epoch.
    pub time_of_week: Milliseconds<u32>,
    /// The satellites' blocks.
    blocks: &'a [u8],
}

impl<'a> NavSat<'a> {
    /// `None` for a version other than 1.
    pub(crate) fn parse(payload: &'a [u8]) -> Option<Self> {
        let fields = Fields(payload);
        Some(Self {
            time_of_week: fields.time_of_week(),
            blocks: payload
                .get(NAV_SAT_HEADER..)
                .filter(|_| fields.u8(4) == SAT_VERSION)?,
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
