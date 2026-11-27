//! What the synthetic engine can be given: a position and a power-up
//! time within the ranges the Oncore's messages carry (VPCommands.pdf,
//! @@Ad, @@Ae, @@Af and @@Ac), checked where the command line or a
//! board file gives them, so that one out of range is an error there
//! and not a wrong message or a panic later in the run.

use std::num::ParseFloatError;

use jiff::Timestamp;
use smartclock_gps::source::fake_receiver::SEPARATION;
use smartclock_gps::time::GpsSeconds;
use smartclock_gps::units::Seconds;
use thiserror::Error;

use super::sky::CENTIMETERS_PER_METER;

/// A quantity the Oncore carries only within a range.
#[derive(Debug)]
pub(crate) struct Bounds {
    /// What it is.
    what: &'static str,
    /// Its unit.
    unit: &'static str,
    /// Its least, inclusive.
    least: f64,
    /// Its greatest, inclusive.
    greatest: f64,
    /// The message the reference gives the range in.
    message: &'static str,
}

/// @@Ad's latitude, -324,000,000 to 324,000,000 mas.
const LATITUDE: Bounds = Bounds {
    what: "latitude",
    unit: "degrees",
    least: -90.0,
    greatest: 90.0,
    message: "@@Ad",
};

/// @@Ae's longitude, -648,000,000 to 648,000,000 mas.
const LONGITUDE: Bounds = Bounds {
    what: "longitude",
    unit: "degrees",
    least: -180.0,
    greatest: 180.0,
    message: "@@Ae",
};

/// @@Af's heights, the ellipsoid's and MSL, -100,000 to 1,800,000 cm
/// each.
const HEIGHT: Bounds = Bounds {
    what: "height",
    unit: "m",
    least: -1000.0,
    greatest: 18_000.0,
    message: "@@Af",
};

/// The last power-up second the engine takes: the end of 2077, the
/// last year @@Ac's date carries (VPCommands.pdf, Date, 1980 to 2077),
/// the narrowest of the reference's date ranges.
const LAST_START: &str = "2077-12-31T23:59:59Z";

/// A coordinate outside the range the Oncore carries it in.
#[derive(Debug, Error)]
#[error(
    "{value} {} is outside the Oncore's {what}, {} to {} {} (VPCommands.pdf, {})",
    bounds.unit,
    bounds.least,
    bounds.greatest,
    bounds.unit,
    bounds.message
)]
pub struct OutsideError {
    /// What it is, as the error names it.
    what: &'static str,
    /// The value.
    value: f64,
    /// Its range.
    bounds: &'static Bounds,
}

impl OutsideError {
    /// The quantity out of range, `latitude`, `longitude` or `height`,
    /// as a board file's `oncore` names its parameter.
    pub(crate) fn quantity(&self) -> &'static str {
        self.bounds.what
    }
}

/// Why a command line's position or power-up time is not one the
/// engine takes.
#[derive(Debug, Error)]
pub enum RangeError {
    /// Not a number.
    #[error("not a number: {0}")]
    Number(#[from] ParseFloatError),
    /// Not a time.
    #[error("not an RFC 3339 time: {0}")]
    Time(#[from] jiff::Error),
    /// A coordinate outside its range.
    #[error(transparent)]
    Outside(#[from] OutsideError),
    /// A power-up time outside the engine's dates.
    #[error(
        "{start} is outside the Oncore's dates, from the GPS epoch {first} to {last} \
         (VPCommands.pdf, @@Ac)"
    )]
    Start {
        /// The time.
        start: Timestamp,
        /// The first it takes.
        first: Timestamp,
        /// The last.
        last: Timestamp,
    },
}

impl Bounds {
    /// `value`, if within the bounds, `what` naming it if not.
    fn check(&'static self, what: &'static str, value: f64) -> Result<f64, OutsideError> {
        if (self.least..=self.greatest).contains(&value) {
            Ok(value)
        } else {
            Err(OutsideError {
                what,
                value,
                bounds: self,
            })
        }
    }
}

/// A latitude, degrees, north positive.
pub(crate) fn latitude(degrees: f64) -> Result<f64, OutsideError> {
    LATITUDE.check(LATITUDE.what, degrees)
}

/// A longitude, degrees, east positive.
pub(crate) fn longitude(degrees: f64) -> Result<f64, OutsideError> {
    LONGITUDE.check(LONGITUDE.what, degrees)
}

/// A height above mean sea level, meters, its ellipsoid height, the
/// geoid's separation below it, within the range too.
pub(crate) fn height(meters: f64) -> Result<f64, OutsideError> {
    let separation = f64::from(SEPARATION.0) / CENTIMETERS_PER_METER;
    HEIGHT.check("MSL height", meters)?;
    HEIGHT.check("ellipsoid height", meters + separation)?;
    Ok(meters)
}

/// A power-up time: to the second, from the GPS epoch to `LAST_START`.
pub(crate) fn start(time: Timestamp) -> Result<Timestamp, RangeError> {
    let epoch = GpsSeconds::EPOCH
        .to_utc(Seconds(0))
        .map_or(0, |utc| utc.since_epoch().0);
    let first = Timestamp::from_second(epoch)?;
    let last: Timestamp = LAST_START.parse()?;
    if !(first..=last).contains(&time) {
        return Err(RangeError::Start {
            start: time,
            first,
            last,
        });
    }
    Ok(Timestamp::from_second(time.as_second())?)
}

/// `--gps-latitude`'s.
pub fn parse_latitude(text: &str) -> Result<f64, RangeError> {
    Ok(latitude(text.parse()?)?)
}

/// `--gps-longitude`'s.
pub fn parse_longitude(text: &str) -> Result<f64, RangeError> {
    Ok(longitude(text.parse()?)?)
}

/// `--gps-height`'s.
pub fn parse_height(text: &str) -> Result<f64, RangeError> {
    Ok(height(text.parse()?)?)
}

/// `--gps-start`'s.
pub fn parse_start(text: &str) -> Result<Timestamp, RangeError> {
    start(text.parse()?)
}

#[cfg(test)]
mod tests {
    use super::RangeError;
    use super::parse_height;
    use super::parse_latitude;
    use super::parse_longitude;
    use super::parse_start;

    #[test]
    fn a_position_within_the_oncores_ranges_is_taken() {
        assert_eq!(parse_latitude("-90").unwrap(), -90.0);
        assert_eq!(parse_longitude("180").unwrap(), 180.0);
        assert_eq!(parse_height("18000").unwrap(), 18_000.0);
    }

    #[test]
    fn a_position_outside_them_is_refused_by_name() {
        for (parsed, what) in [
            (parse_latitude("90.5"), "latitude"),
            (parse_longitude("-181"), "longitude"),
            (parse_height("18000.01"), "MSL height"),
            // 28.41 m above the ellipsoid: the ellipsoid height is under
            (parse_height("-980"), "ellipsoid height"),
            (parse_latitude("NaN"), "latitude"),
        ] {
            let Err(RangeError::Outside(outside)) = parsed else {
                panic!("{what}: {parsed:?}");
            };
            assert_eq!(outside.what, what);
        }
        assert!(matches!(
            parse_latitude("north"),
            Err(RangeError::Number(_))
        ));
    }

    #[test]
    fn a_start_is_taken_to_the_second_within_the_oncores_dates() {
        let start = parse_start("2026-10-22T12:00:00.75Z").unwrap();
        assert_eq!(start.to_string(), "2026-10-22T12:00:00Z");
        assert!(parse_start("1980-01-06T00:00:00Z").is_ok());
        assert!(parse_start("2077-12-31T23:59:59Z").is_ok());
    }

    #[test]
    fn a_start_outside_them_is_refused_not_a_panic_later() {
        for text in [
            "9999-01-01T00:00:00Z",
            "2078-01-01T00:00:00Z",
            "1980-01-05T23:59:59Z",
        ] {
            let error = parse_start(text).expect_err(text);
            assert!(matches!(error, RangeError::Start { .. }), "{text}");
            assert!(error.to_string().contains("@@Ac"), "{error}");
        }
        assert!(matches!(parse_start("soon"), Err(RangeError::Time(_))));
    }
}
