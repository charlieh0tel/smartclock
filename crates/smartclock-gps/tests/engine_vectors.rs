//! The engine's test vectors: for each Oncore and each scenario, what
//! it is sent second by second and every byte it answers.
//!
//! A vector is a text file under `tests/vectors/engine`.  Its head
//! gives the Oncore, the time it starts at and the place it is; each
//! line after is one event, headed by the second it falls in:
//!
//! - `to` a command, in hex, and `to-raw` bytes that are not one;
//! - `from` a frame the engine sent;
//! - `antenna` `on` or `off`, the antenna put on or taken off.
//!
//! In each second the engine is first given the second, then that
//! second's events in the file's order, half a second in.  What is sent
//! on consecutive lines is sent together, before any answer is written
//! down.  The test
//! reads the head and the events sent, runs the engine, writes the file
//! again from what the engine did and compares the two.  With
//! `ENGINE_VECTORS=write` in the environment it writes what the engine
//! did over each file in place of comparing.

use smartclock_gps::engine::Engine;
use smartclock_gps::engine::Options;
use smartclock_gps::engine::Output;
use smartclock_gps::engine::identity::Identity;
use smartclock_gps::engine::identity::UT_PLUS_2_2;
use smartclock_gps::engine::identity::VP_EIGHT;
use smartclock_gps::engine::identity::VP_EIGHT_WITHOUT_TIME_RAIM;
use smartclock_gps::engine::identity::VP_SIX;
use smartclock_gps::record::Almanac;
use smartclock_gps::record::Antenna;
use smartclock_gps::record::Dops;
use smartclock_gps::record::Ecef;
use smartclock_gps::record::Fix;
use smartclock_gps::record::GpsTime;
use smartclock_gps::record::Position;
use smartclock_gps::record::Record;
use smartclock_gps::record::Satellite;
use smartclock_gps::record::Timing;
use smartclock_gps::time::DateTime;
use smartclock_gps::time::GpsSeconds;
use smartclock_gps::time::LeapChange;
use smartclock_gps::time::LeapEvent;
use smartclock_gps::units::Centimeters;
use smartclock_gps::units::DecibelHertz;
use smartclock_gps::units::Degrees;
use smartclock_gps::units::Hertz;
use smartclock_gps::units::MilliArcSeconds;
use smartclock_gps::units::Milliseconds;
use smartclock_gps::units::Nanoseconds;
use smartclock_gps::units::Prn;
use smartclock_gps::units::PrnSet;
use smartclock_gps::units::Seconds;
use smartclock_gps::units::Tenths;
use std::collections::BTreeMap;
use std::env;
use std::fmt::Write as _;
use std::fs;
use std::path::Path;
use std::path::PathBuf;

/// The format line every vector starts with.
const FORMAT: &str = "oncore engine vectors 1";
/// The environment variable that has the vectors written, and the value
/// that does it.
const WRITE_VARIABLE: &str = "ENGINE_VECTORS";
const WRITE: &str = "write";
/// How many vectors there are, so that none goes missing unseen.
const VECTORS: usize = 52;

/// How far into a second its events fall.
const EVENTS_AT: u64 = 500;
const MILLISECONDS_PER_SECOND: u64 = 1000;

/// The sky every vector is under: satellite, elevation and azimuth in
/// degrees, C/N0 in dB-Hz and Doppler shift in hertz.
const SKY: [(u8, i8, u16, u8, i16); 8] = [
    (3, 78, 281, 44, -312),
    (4, 51, 306, 42, 1450),
    (31, 41, 46, 43, -2210),
    (26, 40, 91, 40, 2875),
    (16, 29, 133, 38, -3120),
    (28, 17, 214, 37, 3390),
    (6, 8, 319, 33, -3870),
    (9, 5, 160, 31, 4105),
];
/// The ellipsoid's height above mean sea level there, in centimeters.
const SEPARATION: i32 = -2841;
/// The DOPs, in tenths: geometric, position, horizontal, vertical and
/// time.
const DOPS: Dops = Dops {
    geometric: Tenths(25),
    position: Tenths(20),
    horizontal: Tenths(12),
    vertical: Tenths(16),
    time: Tenths(10),
};
/// The time solution's spread, in nanoseconds.
const SIGMA: u16 = 12;
/// The almanac's reference time, in the units `@@Bd` carries.
const TIME_OF_ALMANAC: u8 = 144;

/// WGS-84's semi-major axis in meters and its inverse flattening.
const AXIS: f64 = 6_378_137.0;
const INVERSE_FLATTENING: f64 = 298.257_223_563;
const MILLI_ARC_SECONDS_PER_DEGREE: f64 = 3_600_000.0;
const CENTIMETERS_PER_METER: f64 = 100.0;

/// What a vector's head says.
struct Head {
    /// The lines of the head, as they are.
    lines: Vec<String>,
    /// Its fields by key.
    fields: BTreeMap<String, String>,
}

impl Head {
    fn field(&self, key: &str) -> &str {
        self.fields
            .get(key)
            .unwrap_or_else(|| panic!("no {key} in the head"))
    }

    fn number(&self, key: &str) -> i32 {
        self.field(key).parse().expect("a number")
    }

    /// A time as the head writes one, `2026-10-22T12:00:00Z`.
    fn time(&self, key: &str) -> Option<DateTime> {
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

    fn identity(&self) -> &'static Identity {
        match self.field("engine") {
            "vp-six" => &VP_SIX,
            "vp-eight" => &VP_EIGHT,
            "vp-eight-without-time-raim" => &VP_EIGHT_WITHOUT_TIME_RAIM,
            "ut-plus-2-2" => &UT_PLUS_2_2,
            other => panic!("no engine {other}"),
        }
    }

    fn options(&self) -> Options {
        let mut options = Options::DEFAULT;
        options.deaf = self.field("answers") == "no";
        options.status_rate = Seconds(u8::try_from(self.number("status-rate")).expect("a rate"));
        options
    }
}

/// One thing done to the engine.
enum Event {
    /// Bytes sent it together, each line's with the word the line
    /// names them by.
    To(Vec<(&'static str, Vec<u8>)>),
    /// The antenna put on or taken off.
    Antenna(bool),
}

/// The frames an engine sends, as the vector's lines.
struct Lines {
    text: String,
    second: u64,
}

impl Output for Lines {
    fn send(&mut self, frame: &[u8]) {
        writeln!(self.text, "{} from {}", self.second, hex(frame)).expect("a string takes it");
    }
}

fn hex(bytes: &[u8]) -> String {
    bytes.iter().fold(String::new(), |mut text, byte| {
        write!(text, "{byte:02x}").expect("a string takes it");
        text
    })
}

fn unhex(text: &str) -> Vec<u8> {
    (0..text.len())
        .step_by(2)
        .map(|at| u8::from_str_radix(&text[at..at + 2], 16).expect("hex"))
        .collect()
}

/// The place's Earth-centered coordinates, which a record's filler
/// works out.
fn ecef(position: Position) -> Ecef {
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
struct Scene {
    /// The GPS second before the first.
    start: GpsSeconds,
    /// GPS less UTC before any leap.
    offset: i8,
    /// The leap second announced.
    leap: Option<LeapEvent>,
    /// The record less its time and antenna.
    record: Record,
}

impl Scene {
    fn new(head: &Head) -> Self {
        let offset = i8::try_from(head.number("leap-seconds")).expect("an offset");
        let gps = |time: DateTime| GpsSeconds::from_utc(time.to_unix(), Seconds(offset));
        let start = gps(head.time("start").expect("a start")).expect("a GPS time");
        let leap = head.time("pending-leap").map(|time| LeapEvent {
            at: gps(time).expect("a GPS time"),
            change: LeapChange::Insert,
        });
        let mean_sea_level = head.number("height-msl-cm");
        let position = Position {
            latitude: MilliArcSeconds(head.number("latitude-mas")),
            longitude: MilliArcSeconds(head.number("longitude-mas")),
            ellipsoid: Centimeters(mean_sea_level + SEPARATION),
            mean_sea_level: Centimeters(mean_sea_level),
        };
        let mut timing = Timing::ZERO;
        timing.sigma = Nanoseconds(SIGMA);
        let mut record = Record::EMPTY;
        record.leap = leap;
        record.fix = Fix::ThreeD;
        record.position = Some(position);
        record.dops = Some(DOPS);
        record.timing = Some(timing);
        record.derived.ecef = Some(ecef(position));
        let mut held = PrnSet::EMPTY;
        for (id, elevation, azimuth, signal, doppler) in SKY {
            let prn = Prn::new(id).expect("a satellite");
            let mut satellite = Satellite::new(prn, Degrees(elevation), Degrees(azimuth))
                .tracked(DecibelHertz(signal));
            satellite.doppler = Some(Hertz(doppler));
            record.satellites.push(satellite);
            held = held.with(prn);
        }
        let week = start.week().expect("a week of GPS time");
        record.almanac = Some(Almanac::new(week, TIME_OF_ALMANAC, held));
        Self {
            start,
            offset,
            leap,
            record,
        }
    }

    /// The record of the vector's `second`th second.
    fn record(&self, second: u64, antenna: bool) -> Record {
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
        record.antenna = if antenna {
            Antenna::Connected
        } else {
            Antenna::Open
        };
        record
    }
}

/// Runs one vector's events and gives the file they make.
fn run(text: &str) -> String {
    let mut head = Head {
        lines: Vec::new(),
        fields: BTreeMap::new(),
    };
    let mut events: BTreeMap<u64, Vec<Event>> = BTreeMap::new();
    let mut last = 0;
    let mut together = false;
    for line in text.lines() {
        if let Some(field) = line.strip_prefix("# ") {
            let (key, value) = field.split_once(": ").expect("a key and a value");
            head.fields.insert(key.to_owned(), value.to_owned());
            head.lines.push(line.to_owned());
            continue;
        }
        let mut words = line.split(' ');
        let second: u64 = words.next().expect("a second").parse().expect("a number");
        let kind = words.next().expect("a kind");
        let value = words.next().expect("a value");
        last = last.max(second);
        let events = events.entry(second).or_default();
        let sent = match kind {
            "to" => ("to", unhex(value)),
            "to-raw" => ("to-raw", unhex(value)),
            "antenna" => {
                together = false;
                events.push(Event::Antenna(value == "on"));
                continue;
            }
            "from" => {
                together = false;
                continue;
            }
            other => panic!("no event {other}"),
        };
        match events.last_mut() {
            Some(Event::To(run)) if together => run.push(sent),
            _ => events.push(Event::To(vec![sent])),
        }
        together = true;
    }
    assert_eq!(head.field("format"), FORMAT);

    let scene = Scene::new(&head);
    let mut engine = Engine::new(head.identity(), head.options());
    let mut antenna = head.field("antenna") == "on";
    let mut lines = Lines {
        text: head.lines.join("\n") + "\n",
        second: 0,
    };
    for second in 1..=last {
        lines.second = second;
        let record = scene.record(second, antenna);
        engine.second(
            &record,
            Milliseconds(second * MILLISECONDS_PER_SECOND),
            &mut lines,
        );
        let now = Milliseconds(second * MILLISECONDS_PER_SECOND + EVENTS_AT);
        engine.poll(now, &mut lines);
        for event in events.get(&second).into_iter().flatten() {
            match event {
                Event::To(run) => {
                    for (kind, bytes) in run {
                        writeln!(lines.text, "{second} {kind} {}", hex(bytes))
                            .expect("a string takes it");
                    }
                    let record = scene.record(second, antenna);
                    for (_, bytes) in run {
                        engine.receive(bytes, &record, now, &mut lines);
                    }
                }
                Event::Antenna(on) => {
                    antenna = *on;
                    let state = if antenna { "on" } else { "off" };
                    writeln!(lines.text, "{second} antenna {state}").expect("a string takes it");
                }
            }
        }
    }
    lines.text
}

fn vectors() -> Vec<PathBuf> {
    let directory = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/vectors/engine");
    let mut paths: Vec<PathBuf> = fs::read_dir(directory)
        .expect("the vectors' directory")
        .map(|entry| entry.expect("an entry").path())
        .collect();
    paths.sort();
    paths
}

#[test]
fn the_engine_sends_every_vectors_bytes() {
    let write = env::var(WRITE_VARIABLE).is_ok_and(|value| value == WRITE);
    let paths = vectors();
    assert_eq!(paths.len(), VECTORS);
    let mut differing = Vec::new();
    for path in paths {
        let expected = fs::read_to_string(&path).expect("a vector");
        let actual = run(&expected);
        if actual == expected {
            continue;
        }
        if write {
            fs::write(&path, actual).expect("the vector written");
            continue;
        }
        let line = expected
            .lines()
            .zip(actual.lines())
            .position(|(expected, actual)| expected != actual)
            .unwrap_or_else(|| expected.lines().count().min(actual.lines().count()));
        differing.push(format!(
            "{}:{}\n  expected {}\n  actual   {}",
            path.display(),
            line + 1,
            expected.lines().nth(line).unwrap_or("nothing"),
            actual.lines().nth(line).unwrap_or("nothing"),
        ));
    }
    assert!(
        differing.is_empty(),
        "{} vectors differ:\n{}",
        differing.len(),
        differing.join("\n")
    );
}
