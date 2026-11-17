//! A made-up sky over a place: the scene the engine's recorded
//! exchanges are under (`tests/vectors/engine`), for any tool that puts
//! an engine under one.
//!
//! Eight satellites, all tracked and used, a 3D fix at the place, its
//! DOPs, a time solution's spread, an almanac of the eight, the antenna
//! connected, a self-test that passes and Time RAIM's result for such a
//! sky.  [`sky`] is the record less its time; a [`Fixed`] gives it each
//! second, and a leap second comes from a scenario's row or a
//! [`LeapSchedule`].  What commands set that the scene acts on,
//! position hold and the satellites its fix leaves out, [`act`] does to
//! a record.
//!
//! The position in Earth-centered coordinates is left to whoever holds
//! the scene, being arithmetic this crate does not do
//! ([`Derived`](crate::record::Derived)); [`DECLARATION`] counts it
//! filled, since every holder here works it out.
//!
//! [`Fixed`]: crate::source::scenario::Fixed
//! [`LeapSchedule`]: crate::source::wrap::LeapSchedule

use crate::engine::settings::HeightKind;
use crate::engine::settings::Location;
use crate::engine::settings::Setting;
use crate::engine::settings::SettingSet;
use crate::engine::settings::Settings;
use crate::record::Almanac;
use crate::record::Antenna;
use crate::record::Dops;
use crate::record::Fix;
use crate::record::Position;
use crate::record::Raim;
use crate::record::RaimAbility;
use crate::record::RaimSolution;
use crate::record::Record;
use crate::record::Satellite;
use crate::record::SelfTest;
use crate::record::Timing;
use crate::record::field::Declaration;
use crate::record::field::FieldSet;
use crate::record::field::RecordField;
use crate::time::GpsWeek;
use crate::units::Centimeters;
use crate::units::DecibelHertz;
use crate::units::Degrees;
use crate::units::Hertz;
use crate::units::MilliArcSeconds;
use crate::units::Nanoseconds;
use crate::units::Prn;
use crate::units::PrnSet;
use crate::units::Seconds;
use crate::units::Tenths;

/// The sky: satellite, elevation and azimuth in degrees, C/N0 in dB-Hz
/// and Doppler shift in hertz.
const SATELLITES: [(u8, i8, u16, u8, i16); 8] = [
    (3, 78, 281, 44, -312),
    (4, 51, 306, 42, 1450),
    (31, 41, 46, 43, -2210),
    (26, 40, 91, 40, 2875),
    (16, 29, 133, 38, -3120),
    (28, 17, 214, 37, 3390),
    (6, 8, 319, 33, -3870),
    (9, 5, 160, 31, 4105),
];
/// The ellipsoid's height above mean sea level, wherever the place.
pub const SEPARATION: Centimeters = Centimeters(-2841);
/// The DOPs, in tenths.
const DOPS: Dops = Dops {
    geometric: Tenths(25),
    position: Tenths(20),
    horizontal: Tenths(12),
    vertical: Tenths(16),
    time: Tenths(10),
};
/// The time solution's spread.
const SIGMA: Nanoseconds<u16> = Nanoseconds(12);
/// The almanac's reference time, in the units `@@Bd` carries.
const TIME_OF_ALMANAC: u8 = 144;
/// What Time RAIM makes of the sky with the antenna on: eight
/// satellites are enough to isolate a bad one under any alarm limit
/// (VP p. 106), and none of them is bad.
const RAIM: Raim = Raim::new(RaimSolution::Ok, RaimAbility::Isolation);
/// And with it off, when no satellite is heard (VP pp. 105 and 106).
const RAIM_WITHOUT_ANTENNA: Raim = Raim::new(RaimSolution::Unknown, RaimAbility::Neither);

/// The place the receivers' own request scripts set
/// (`docs/firmware/gps.md`, "Requests"): 37.3255833 N, 121.9978333 W,
/// 55 m above mean sea level.
pub const PLACE: Position = position(
    MilliArcSeconds(134_372_100),
    MilliArcSeconds(-439_192_200),
    Centimeters(5500),
);

/// What the scene fills of a record, with the position in
/// Earth-centered coordinates its holder works out, and the settings
/// it acts on ([`act`]): in position hold its receiver is where it was
/// told it is, and its fix leaves out the satellites below the mask and
/// those ignored.  It makes no pulse, so whoever times one from its
/// records acts on the pulse settings.  The exceptions tables name it
/// `scene`, for the vectors and the host feed alike.
pub const DECLARATION: Declaration = Declaration {
    name: "scene",
    fills: FieldSet::of(&[
        RecordField::Time,
        RecordField::UtcOffset,
        RecordField::Leap,
        RecordField::Fix,
        RecordField::Latitude,
        RecordField::Longitude,
        RecordField::EllipsoidHeight,
        RecordField::MeanSeaLevelHeight,
        RecordField::GeometricDop,
        RecordField::PositionDop,
        RecordField::HorizontalDop,
        RecordField::VerticalDop,
        RecordField::TimeDop,
        RecordField::Sigma,
        RecordField::Satellites,
        RecordField::SatelliteElevation,
        RecordField::SatelliteAzimuth,
        RecordField::SatelliteSignal,
        RecordField::SatelliteTracking,
        RecordField::SatelliteUsed,
        RecordField::SatelliteHealth,
        RecordField::SatelliteEphemeris,
        RecordField::SatelliteDoppler,
        RecordField::Almanac,
        RecordField::Antenna,
        RecordField::SelfTest,
        RecordField::Ecef,
        RecordField::RaimSolution,
        RecordField::RaimAbility,
    ]),
    acts: SettingSet::of(&[
        Setting::MaskAngle,
        Setting::PositionHold,
        Setting::HeldPosition,
        Setting::IgnoreList,
    ]),
    makes_pulse: false,
};

/// A place at `latitude` and `longitude`, `mean_sea_level` above mean
/// sea level: both its heights, by [`SEPARATION`].
#[must_use]
pub const fn position(
    latitude: MilliArcSeconds,
    longitude: MilliArcSeconds,
    mean_sea_level: Centimeters,
) -> Position {
    Position {
        latitude,
        longitude,
        ellipsoid: Centimeters(mean_sea_level.0 + SEPARATION.0),
        mean_sea_level,
    }
}

/// The position a command gave, as a record has one: both its heights,
/// by [`SEPARATION`].
#[must_use]
pub const fn located(location: Location) -> Position {
    let height = location.height.height;
    let mean_sea_level = match location.height.kind {
        HeightKind::Ellipsoid => Centimeters(height.0 - SEPARATION.0),
        HeightKind::MeanSeaLevel => height,
    };
    position(location.latitude, location.longitude, mean_sea_level)
}

/// The scene's record at `place`, less its time: GPS less UTC
/// `offset`, no leap second and the almanac of `week`.
#[must_use]
pub const fn sky(place: Position, offset: Seconds<i8>, week: GpsWeek) -> Record {
    let mut timing = Timing::ZERO;
    timing.sigma = SIGMA;
    let mut record = Record::EMPTY;
    record.utc_offset = Some(offset);
    record.fix = Fix::ThreeD;
    record.position = Some(place);
    record.dops = Some(DOPS);
    record.timing = Some(timing);
    // A scene's engine has no part that fails.
    record.self_test = SelfTest::PASS;
    record.antenna = Antenna::Connected;
    record.derived.raim = Some(RAIM);
    let mut held = PrnSet::EMPTY;
    let mut index = 0;
    while index < SATELLITES.len() {
        let (id, elevation, azimuth, signal, doppler) = SATELLITES[index];
        let Some(prn) = Prn::new(id) else {
            panic!("a satellite of the scene is no satellite");
        };
        let mut satellite =
            Satellite::new(prn, Degrees(elevation), Degrees(azimuth)).tracked(DecibelHertz(signal));
        satellite.doppler = Some(Hertz(doppler));
        record.satellites.push(satellite);
        held = held.with(prn);
        index += 1;
    }
    record.almanac = Some(Almanac::new(week, TIME_OF_ALMANAC, held));
    record
}

/// Acts on `settings`: in position hold the receiver takes the
/// position it was given for its own and solves for time alone (VP
/// pp. 56 and 73), and its fix leaves out the satellites below the mask
/// angle and those it is told to ignore ([`Settings::leave_out`]).  The
/// holder works out the Earth-centered position again from the
/// record's.
pub fn act(record: &mut Record, settings: &Settings) {
    if settings.position_hold {
        record.fix = Fix::TimeOnly;
        record.position = Some(located(settings.held_position));
    }
    settings.leave_out(record);
}

/// The antenna taken off: no satellite heard, and so Time RAIM's
/// result unknown and its ability neither.
pub const fn unplug(record: &mut Record) {
    record.antenna = Antenna::Open;
    record.derived.raim = Some(RAIM_WITHOUT_ANTENNA);
}
