//! What the engines' test vectors share: a vector's head, the scene a
//! vector runs under and the Earth-centered position its provider works
//! out.

use smartclock_gps::engine::settings::Settings;
use smartclock_gps::record::Ecef;
use smartclock_gps::record::GpsTime;
use smartclock_gps::record::Position;
use smartclock_gps::record::Record;
use smartclock_gps::source::scene;
use smartclock_gps::time::DateTime;
use smartclock_gps::time::GpsSeconds;
use smartclock_gps::time::LeapChange;
use smartclock_gps::time::LeapEvent;
use smartclock_gps::units::Centimeters;
use smartclock_gps::units::MilliArcSeconds;
use smartclock_gps::units::Nanoseconds;
use smartclock_gps::units::Seconds;
use std::collections::BTreeMap;

/// The exceptions tables, the fields' and the settings', from the
/// crate's directory.
pub(crate) const EXCEPTIONS: &str = "../../docs/firmware/gps-field-exceptions.tsv";
pub(crate) const SETTING_EXCEPTIONS: &str = "../../docs/firmware/gps-setting-exceptions.tsv";

/// The environment variable that has the vectors written, and the value
/// that does it.
pub(crate) const WRITE_VARIABLE: &str = "ENGINE_VECTORS";
pub(crate) const WRITE: &str = "write";

/// WGS-84's semi-major axis in meters and its inverse flattening.
const AXIS: f64 = 6_378_137.0;
const INVERSE_FLATTENING: f64 = 298.257_223_563;
const MILLI_ARC_SECONDS_PER_DEGREE: f64 = 3_600_000.0;
const CENTIMETERS_PER_METER: f64 = 100.0;

/// What a vector's head says.
pub(crate) struct Head {
    /// The lines of the head, as they are.
    pub(crate) lines: Vec<String>,
    /// Its fields by key.
    pub(crate) fields: BTreeMap<String, String>,
}

impl Head {
    /// The head of a vector's text, its lines that start `# `.
    pub(crate) fn of(text: &str) -> Self {
        let mut head = Self {
            lines: Vec::new(),
            fields: BTreeMap::new(),
        };
        for line in text.lines() {
            if let Some(field) = line.strip_prefix("# ") {
                head.take(line, field);
            }
        }
        head
    }

    /// Takes one line of the head, `field` being what follows its `# `.
    pub(crate) fn take(&mut self, line: &str, field: &str) {
        let (key, value) = field.split_once(": ").expect("a key and a value");
        self.fields.insert(key.to_owned(), value.to_owned());
        self.lines.push(line.to_owned());
    }

    pub(crate) fn field(&self, key: &str) -> &str {
        self.fields
            .get(key)
            .unwrap_or_else(|| panic!("no {key} in the head"))
    }

    pub(crate) fn number(&self, key: &str) -> i32 {
        self.field(key).parse().expect("a number")
    }

    /// A time as the head writes one, `2026-10-22T12:00:00Z`.
    pub(crate) fn time(&self, key: &str) -> Option<DateTime> {
        let text = self.fields.get(key)?;
        let part = |range: std::ops::Range<usize>| text[range].parse::<u16>().expect("a number");
        let byte = |range| u8::try_from(part(range)).expect("a byte");
        DateTime::new(
            part(0..4),
            byte(5..7),
            byte(8..10),
            byte(11..13),
            byte(14..16),
            byte(17..19),
        )
    }
}

pub(crate) fn unhex(text: &str) -> Vec<u8> {
    (0..text.len())
        .step_by(2)
        .map(|at| u8::from_str_radix(&text[at..at + 2], 16).expect("hex"))
        .collect()
}

/// The place's Earth-centered coordinates, which a record's provider
/// works out.
pub(crate) fn ecef(position: Position) -> Ecef {
    let flattening = 1.0 / INVERSE_FLATTENING;
    let eccentricity_squared = flattening * (2.0 - flattening);
    let latitude = (f64::from(position.latitude.0) / MILLI_ARC_SECONDS_PER_DEGREE).to_radians();
    let longitude = (f64::from(position.longitude.0) / MILLI_ARC_SECONDS_PER_DEGREE).to_radians();
    let height = f64::from(position.ellipsoid.0) / CENTIMETERS_PER_METER;
    let normal = AXIS / (1.0 - eccentricity_squared * latitude.sin().powi(2)).sqrt();
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

/// What does not change through a vector.
pub(crate) struct Scene {
    /// The GPS second before the first.
    start: GpsSeconds,
    /// GPS less UTC before any leap.
    offset: i8,
    /// The leap second announced.
    leap: Option<LeapEvent>,
    /// The record less its time.
    record: Record,
}

impl Scene {
    pub(crate) fn new(head: &Head) -> Self {
        let offset = i8::try_from(head.number("leap-seconds")).expect("an offset");
        let gps = |time: DateTime| GpsSeconds::from_utc(time.to_unix(), Seconds(offset));
        let start = gps(head.time("start").expect("a start")).expect("a GPS time");
        let leap = head.time("pending-leap").map(|time| LeapEvent {
            at: gps(time).expect("a GPS time"),
            change: LeapChange::Insert,
        });
        let place = scene::position(
            MilliArcSeconds(head.number("latitude-mas")),
            MilliArcSeconds(head.number("longitude-mas")),
            Centimeters(head.number("height-msl-cm")),
        );
        let week = start.week().expect("a week of GPS time");
        let mut record = scene::sky(place, Seconds(offset), week);
        record.leap = leap;
        Self {
            start,
            offset,
            leap,
            record,
        }
    }

    /// The record of the vector's `second`th second, of an engine set
    /// to `settings`.  In position hold the receiver takes the position
    /// it was given for its own and solves for time alone (VP pp. 56
    /// and 73).
    pub(crate) fn record(&self, second: u64, antenna: bool, settings: &Settings) -> Record {
        let seconds = self.start + Seconds(second.cast_signed());
        let offset = self.leap.map_or(Seconds(self.offset), |leap| {
            leap.offset(seconds, Seconds(self.offset))
        });
        let mut record = self.record;
        record.time = Some(GpsTime {
            seconds,
            subsecond: Nanoseconds(0),
        });
        record.utc_offset = Some(offset);
        scene::hold(&mut record, settings);
        record.derived.ecef = record.position.map(ecef);
        if !antenna {
            scene::unplug(&mut record);
        }
        record
    }
}
