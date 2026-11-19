//! The decoded messages in a record's units: where a record holds what
//! a message says, a trait of the message's type gives it.

use smartclock_gps::record::Antenna;
use smartclock_gps::record::Dops;
use smartclock_gps::record::Ecef;
use smartclock_gps::record::Fix;
use smartclock_gps::record::Health;
use smartclock_gps::record::Position;
use smartclock_gps::record::Raim;
use smartclock_gps::record::RaimAbility;
use smartclock_gps::record::RaimSolution;
use smartclock_gps::record::ReceiverMode;
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
use smartclock_ubx::message::SYSTEM_GPS;
use smartclock_ubx::mon::AntennaStatus;
use smartclock_ubx::mon::MonHw;
use smartclock_ubx::nav::FixType;
use smartclock_ubx::nav::LeapChange as UbxLeapChange;
use smartclock_ubx::nav::LeapNotice;
use smartclock_ubx::nav::NavDop;
use smartclock_ubx::nav::NavPosEcef;
use smartclock_ubx::nav::NavPvt;
use smartclock_ubx::nav::NavSat;
use smartclock_ubx::nav::NavTimeGps;
use smartclock_ubx::nav::SatelliteHealth;
use smartclock_ubx::nav::SatelliteInfo;
use smartclock_ubx::tim::TimTp;
use smartclock_ubx::units;
use smartclock_ubx::units::HundredNanodegrees;
use smartclock_ubx::units::Hundredths;
use smartclock_ubx::units::Millimeters;

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

/// `UBX-NAV-PVT` in a record's units.
pub trait NavPvtRecord {
    /// The fix as a record holds it: [`Fix::None`] unless `gnssFixOK`
    /// is set and `fixType` is 2 to 5.  A fix combined with dead
    /// reckoning counts as three-dimensional; dead reckoning alone is
    /// no fix from satellites.
    fn fix(&self) -> Fix;

    /// What `fixType` tells of the receiver's mode, whatever
    /// `gnssFixOK`: position hold at 5, the time-only fix of a receiver
    /// whose position is fixed (section 25.1, p. 85); navigating at 2 to
    /// 4, a fix of its position.  `None` at 0, 1 or a value the document
    /// does not give: no fix from satellites tells nothing of the mode.
    fn receiver_mode(&self) -> Option<ReceiverMode>;

    /// `lat`, `lon`, `height` and `hMSL`, to the record's units; `None`
    /// with `invalidLlh` set.
    fn position(&self) -> Option<Position>;

    /// `gSpeed` and `headMot`, to the record's units.
    fn velocity(&self) -> Velocity;
}

impl NavPvtRecord for NavPvt {
    fn fix(&self) -> Fix {
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

    fn receiver_mode(&self) -> Option<ReceiverMode> {
        match self.fix_type {
            FixType::TimeOnly => Some(ReceiverMode::Holding),
            FixType::TwoD | FixType::ThreeD | FixType::WithDeadReckoning => {
                Some(ReceiverMode::Navigating)
            }
            FixType::None | FixType::DeadReckoning | FixType::Other(_) => None,
        }
    }

    fn position(&self) -> Option<Position> {
        (!self.position_invalid).then(|| Position {
            longitude: milli_arc_seconds(self.longitude),
            latitude: milli_arc_seconds(self.latitude),
            ellipsoid: centimeters(self.height),
            mean_sea_level: centimeters(self.mean_sea_level),
        })
    }

    fn velocity(&self) -> Velocity {
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

/// `UBX-NAV-TIMEGPS` in a record's units.
pub trait NavTimeGpsRecord {
    /// `week`; `None` without `towValid` and `weekValid` both, or for a
    /// week below zero.
    fn week(&self) -> Option<GpsWeek>;

    /// The epoch's second, the nearest to its time of week.  With a
    /// `reference`, the week is taken for its ten broadcast bits alone
    /// and is the one nearest `reference` that has them: for a receiver
    /// whose own resolution cannot be trusted.  `None` without a valid
    /// time of week and week.
    fn seconds(&self, reference: Option<GpsWeek>) -> Option<GpsSeconds>;
}

impl NavTimeGpsRecord for NavTimeGps {
    fn week(&self) -> Option<GpsWeek> {
        u16::try_from(self.week_number)
            .ok()
            .filter(|_| self.time_of_week_valid && self.week_valid)
            .map(GpsWeek::new)
    }

    fn seconds(&self, reference: Option<GpsWeek>) -> Option<GpsSeconds> {
        let week = self.week()?;
        let week = match reference {
            Some(reference) => reference.nearest(week.get(), BROADCAST_WEEKS)?,
            None => week,
        };
        GpsSeconds::nearest(
            week,
            Milliseconds(self.time_of_week.0),
            Nanoseconds(self.fraction.0),
        )
    }
}

/// How far a leap second placed by its week and day may lie from where
/// `timeToLsEvent` puts it and still be believed.  The document does
/// not say which second the countdown ends on, the inserted one or the
/// midnight after it, so they may differ by one.
const LEAP_SLACK: i64 = 2;

/// A leap second's step as a record holds it.
pub(crate) const fn leap_change(change: UbxLeapChange) -> LeapChange {
    match change {
        UbxLeapChange::Insert => LeapChange::Insert,
        UbxLeapChange::Remove => LeapChange::Remove,
    }
}

/// A `UBX-NAV-TIMELS` leap second notice in a record's units.
pub trait LeapNoticeRecord {
    /// The leap second, `now` being the second the notice was given in
    /// and `before` GPS less UTC until the leap.
    ///
    /// It is put at the end of [`LeapNotice::day`] of
    /// [`LeapNotice::week`], the week read modulo [`ALMANAC_WEEKS`] and
    /// resolved nearest the week [`LeapNotice::until`] counts to, so it
    /// is found whether the receiver gives the week in full or cut
    /// short, at a week's end as on any other day.  `None` if that is
    /// not where the countdown ends, to within two seconds.
    fn event(&self, now: GpsSeconds, before: Seconds<i8>) -> Option<LeapEvent>;
}

impl LeapNoticeRecord for LeapNotice {
    fn event(&self, now: GpsSeconds, before: Seconds<i8>) -> Option<LeapEvent> {
        let counted = now.checked_add(Seconds(i64::from(self.until.0)))?;
        let week = counted.week()?.nearest(self.week, ALMANAC_WEEKS)?;
        let days = Seconds(i64::from(self.day) * SECONDS_PER_DAY);
        // GPS time with no offset reads as UTC's calendar does.
        let midnight = GpsSeconds::week_start(week)
            .checked_add(days)?
            .to_utc(Seconds(0))?;
        let event = LeapEvent::at_day_end(midnight, before, leap_change(self.change))?;
        let apart = event.at.checked_since(counted)?;
        (apart.0.abs() <= LEAP_SLACK).then_some(event)
    }
}

/// `UBX-NAV-DOP` in a record's units.
pub trait NavDopRecord {
    /// The record's dilutions, to the nearest tenth.
    fn dops(&self) -> Dops;
}

impl NavDopRecord for NavDop {
    fn dops(&self) -> Dops {
        Dops {
            geometric: tenths(self.geometric),
            position: tenths(self.position),
            time: tenths(self.time),
            vertical: tenths(self.vertical),
            horizontal: tenths(self.horizontal),
        }
    }
}

/// `UBX-NAV-POSECEF` in a record's units.
pub trait NavPosEcefRecord {
    /// `ecefX`, `ecefY` and `ecefZ`, which are centimeters already.
    fn ecef(&self) -> Ecef;
}

impl NavPosEcefRecord for NavPosEcef {
    fn ecef(&self) -> Ecef {
        Ecef {
            x: Centimeters(self.x.0),
            y: Centimeters(self.y.0),
            z: Centimeters(self.z.0),
        }
    }
}

/// `qualityInd` values (p. 382).
const QUALITY_NO_SIGNAL: u8 = 0;
const QUALITY_SEARCHING: u8 = 1;
const QUALITY_CODE_LOCKED: u8 = 4;
/// The elevations of a satellite above the horizon.  `elev` is "unknown
/// if out of range" of 90 degrees either way (p. 381).
const ELEVATIONS: core::ops::RangeInclusive<i8> = 0..=90;

/// A satellite's health, `NAV-SAT`'s `health` (p. 382), as a record
/// holds it.
#[must_use]
pub const fn satellite_health(health: SatelliteHealth) -> Health {
    match health {
        SatelliteHealth::Healthy => Health::Healthy,
        SatelliteHealth::Unhealthy => Health::Unhealthy,
        SatelliteHealth::Unknown => Health::Unknown,
    }
}

/// One satellite's block of `UBX-NAV-SAT` in a record's units.
pub trait SatelliteInfoRecord {
    /// This satellite as a record holds one: `None` unless it is a GPS
    /// satellite at a known place above the horizon.
    ///
    /// How far a channel has got with it is this crate's reading of
    /// `qualityInd`, which does not name an Oncore's stages: searching
    /// below 2, code lock at 2 and 3, and from 4, where the receiver
    /// has its time, reading its ephemeris until `ephAvail`, then
    /// ready for the fix.
    fn satellite(&self) -> Option<Satellite>;
}

impl SatelliteInfoRecord for SatelliteInfo {
    fn satellite(&self) -> Option<Satellite> {
        let prn = Prn::new(self.id).filter(|_| self.system == SYSTEM_GPS)?;
        if !ELEVATIONS.contains(&self.elevation.0) {
            return None;
        }
        let azimuth = u16::try_from(self.azimuth.0.rem_euclid(DEGREES_PER_TURN)).ok()?;
        let mut satellite = Satellite::new(prn, Degrees(self.elevation.0), Degrees(azimuth));
        satellite.signal =
            (self.quality != QUALITY_NO_SIGNAL).then_some(DecibelHertz(self.signal.0));
        satellite.tracking = match self.quality {
            QUALITY_NO_SIGNAL | QUALITY_SEARCHING => Tracking::Searching,
            quality if quality < QUALITY_CODE_LOCKED => Tracking::CodeLock,
            _ if self.ephemeris => Tracking::Position,
            _ => Tracking::Ephemeris,
        };
        satellite.used = self.used;
        satellite.health = satellite_health(self.health);
        satellite.ephemeris = self.ephemeris;
        Some(satellite)
    }
}

/// `UBX-NAV-SAT` in a record's units.
pub trait NavSatRecord {
    /// The GPS satellites with a known place in the sky, as a record
    /// holds them: those the fix uses first, then the rest, each group
    /// in the message's order.  Past a record's room the lowest in the
    /// sky go, as [`Satellites::push`] has it.
    fn sky(&self) -> Satellites;
}

impl NavSatRecord for NavSat<'_> {
    fn sky(&self) -> Satellites {
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

/// `UBX-MON-HW` in a record's units.
pub trait MonHwRecord {
    /// `aStatus`, the antenna supervisor's state, as a record holds
    /// the antenna's feed: unknown while it starts up or does not know.
    fn antenna(&self) -> Antenna;
}

impl MonHwRecord for MonHw {
    fn antenna(&self) -> Antenna {
        match self.antenna {
            AntennaStatus::Ok => Antenna::Connected,
            AntennaStatus::Short => Antenna::Shorted,
            AntennaStatus::Open => Antenna::Open,
            AntennaStatus::Unknown => Antenna::Unknown,
        }
    }
}

/// What a record makes of `TIM-TP`'s `raim` ("From messages to a
/// record" in the crate's documentation): the solution is never known,
/// the message giving no RAIM result or alarm.
pub(crate) fn time_raim(pulse: &TimTp) -> Option<Raim> {
    pulse.raim.map(|active| {
        let ability = if active {
            RaimAbility::Detection
        } else {
            RaimAbility::Neither
        };
        Raim::new(RaimSolution::Unknown, ability)
    })
}

/// GPS less UTC as a record holds it.
pub(crate) const fn utc_offset(seconds: units::Seconds<i8>) -> Seconds<i8> {
    Seconds(seconds.0)
}
