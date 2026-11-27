//! What the synthetic engine is told each second: the made-up sky of
//! smartclock-gps's `fake_receiver`, at the antenna's place and the
//! run's time, completed as its holder must complete it
//! (docs/emulator.md, "The synthetic GPS engine").
//!
//! The record of second *n* is the fake receiver's sky at *n* seconds
//! past the power-up time, by `scenario::Fixed`; position hold, which
//! the fake receiver acts on, put on it from the engine's settings
//! (`fake_receiver::act`); the antenna taken off by the `antenna-off`
//! control (`fake_receiver::unplug`); and the position in
//! Earth-centered coordinates worked out, which the fake receiver
//! leaves to its holder.  Its epoch's part of a second is its holder's
//! to give too, as the fake receiver's is: on the second unless the sky
//! is placed elsewhere in it (`Sky::new_at`), as a test does so that
//! the field shows.  So it fills what the fake receiver's
//! `Declaration` says, and the exceptions tables' `fake receiver` rows
//! are its own.

use jiff::Timestamp;
use smartclock_gps::engine::settings::Settings;
use smartclock_gps::record::Ecef;
use smartclock_gps::record::Position;
use smartclock_gps::record::Record;
use smartclock_gps::source::Presence;
use smartclock_gps::source::Source;
use smartclock_gps::source::Tick;
use smartclock_gps::source::fake_receiver;
use smartclock_gps::source::scenario::Fixed;
use smartclock_gps::time::GpsSeconds;
use smartclock_gps::time::UnixSeconds;
use smartclock_gps::units::Centimeters;
use smartclock_gps::units::MilliArcSeconds;
use smartclock_gps::units::Nanoseconds;
use smartclock_gps::units::Seconds;
use thiserror::Error;

use super::range;
use super::range::OutsideError;

/// Milliarcseconds a degree.
pub const MAS_PER_DEGREE: f64 = 3_600_000.0;
/// Centimeters a meter.
pub const CENTIMETERS_PER_METER: f64 = 100.0;
/// WGS-84's semi-major axis, meters, and its inverse flattening (NIMA
/// TR8350.2, 3.2).
const SEMI_MAJOR_AXIS: f64 = 6_378_137.0;
const INVERSE_FLATTENING: f64 = 298.257_223_563;
/// The sky's epoch unless placed elsewhere: on the second, as the
/// vectors' and the host feed's are.
const ON_THE_SECOND: Nanoseconds<u32> = Nanoseconds(0);

/// The place the antenna is at unless told otherwise: the position the
/// Z380x's `master_reset` script (0x47), not its power-up one, sends,
/// 37.3255833 degrees north, 121.9978333 west, 55 m MSL
/// (docs/firmware/gps.md, "Requests"), which the fake receiver's place is.
pub const FIRMWARE_DEFAULT: Position = fake_receiver::PLACE;

/// The place at `latitude` and `longitude`, degrees, `height` meters
/// above mean sea level, if the Oncore can give it (`range`): both its
/// heights, by the fake receiver's separation.
pub(crate) fn place(latitude: f64, longitude: f64, height: f64) -> Result<Position, OutsideError> {
    #[expect(
        clippy::cast_possible_truncation,
        reason = "within the Oncore's ranges, which range checks first"
    )]
    let rounded = |value: f64| value.round() as i32;
    Ok(fake_receiver::position(
        MilliArcSeconds(rounded(range::latitude(latitude)? * MAS_PER_DEGREE)),
        MilliArcSeconds(rounded(range::longitude(longitude)? * MAS_PER_DEGREE)),
        Centimeters(rounded(range::height(height)? * CENTIMETERS_PER_METER)),
    ))
}

/// A place's latitude, longitude and height above mean sea level, in
/// degrees and meters, as the command line gives them.
pub(crate) fn degrees(place: Position) -> (f64, f64, f64) {
    (
        f64::from(place.latitude.0) / MAS_PER_DEGREE,
        f64::from(place.longitude.0) / MAS_PER_DEGREE,
        f64::from(place.mean_sea_level.0) / CENTIMETERS_PER_METER,
    )
}

/// A power-up time the fake receiver cannot start at: before GPS time's start,
/// which `range::start` refuses first, or past its count.
#[derive(Debug, Error)]
#[error("{0} is outside GPS time's range")]
pub struct StartError(Timestamp);

/// The emulator's provider.
#[derive(Debug, Clone)]
pub(crate) struct Sky {
    /// GPS time at power-up.
    start: GpsSeconds,
    /// The fake receiver's record at the place, less its time.
    sky: Record,
    /// How far into each second its epoch lies.
    after_second: Nanoseconds<u32>,
    /// Whether the antenna is connected.
    antenna: bool,
}

impl Sky {
    /// The fake receiver at `place` from `start`, UTC at power-up, GPS time
    /// `leap_seconds` ahead of UTC, its epoch on the second.
    pub(crate) fn new(
        place: Position,
        start: Timestamp,
        leap_seconds: Seconds<i8>,
    ) -> Result<Self, StartError> {
        Self::new_at(place, start, leap_seconds, ON_THE_SECOND)
    }

    /// [`Sky::new`], its epoch `after_second` into each second.
    pub(crate) fn new_at(
        place: Position,
        start: Timestamp,
        leap_seconds: Seconds<i8>,
        after_second: Nanoseconds<u32>,
    ) -> Result<Self, StartError> {
        let gps = GpsSeconds::from_utc(
            UnixSeconds::from_epoch(Seconds(start.as_second())),
            leap_seconds,
        )
        .ok_or(StartError(start))?;
        let week = gps.week().ok_or(StartError(start))?;
        Ok(Self {
            start: gps,
            sky: fake_receiver::sky(place, leap_seconds, week),
            after_second,
            antenna: true,
        })
    }

    /// The antenna connected, or not, from the next record on.
    pub(crate) fn set_antenna(&mut self, connected: bool) {
        self.antenna = connected;
    }

    /// The record of second `n` from power-up, under `settings`.
    pub(crate) fn record(&self, n: u64, settings: &Settings) -> Record {
        let mut record = Record::EMPTY;
        if Fixed::new(self.start, &self.sky).record(Tick(n), &mut record) == Presence::Silent {
            // past GPS time's count: no time, the engine's clock runs on
            record = self.sky;
        }
        if let Some(time) = record.time.as_mut() {
            time.subsecond = self.after_second;
        }
        fake_receiver::act(&mut record, settings);
        if !self.antenna {
            fake_receiver::unplug(&mut record);
        }
        record.derived.ecef = record.position.map(earth_centered);
        record
    }
}

/// A place in WGS-84's Earth-centered, Earth-fixed frame (NIMA
/// TR8350.2, 4.1 and 4.2).
fn earth_centered(place: Position) -> Ecef {
    let flattening = 1.0 / INVERSE_FLATTENING;
    let eccentricity_squared = flattening * (2.0 - flattening);
    let latitude = (f64::from(place.latitude.0) / MAS_PER_DEGREE).to_radians();
    let longitude = (f64::from(place.longitude.0) / MAS_PER_DEGREE).to_radians();
    let height = f64::from(place.ellipsoid.0) / CENTIMETERS_PER_METER;
    let normal = SEMI_MAJOR_AXIS / (1.0 - eccentricity_squared * latitude.sin().powi(2)).sqrt();
    #[expect(
        clippy::cast_possible_truncation,
        reason = "a place on Earth is within an i32 of centimeters"
    )]
    let centimeters = |meters: f64| Centimeters((meters * CENTIMETERS_PER_METER).round() as i32);
    Ecef {
        x: centimeters((normal + height) * latitude.cos() * longitude.cos()),
        y: centimeters((normal + height) * latitude.cos() * longitude.sin()),
        z: centimeters((normal * (1.0 - eccentricity_squared) + height) * latitude.sin()),
    }
}
