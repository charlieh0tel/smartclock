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
//! second's events in the file's order, half a second in, and the run
//! goes on past the last event until no answer waits.  What is sent
//! on consecutive lines is sent together, before any answer is written
//! down.  The test
//! reads the head and the events sent, runs the engine, writes the file
//! again from what the engine did and compares the two.  With
//! `ENGINE_VECTORS=write` in the environment it writes what the engine
//! did over each file in place of comparing.

mod common;

use common::EXCEPTIONS;
use common::Head;
use common::SETTING_EXCEPTIONS;
use common::Scene;
use common::WRITE;
use common::WRITE_VARIABLE;
use common::ecef;
use common::unhex;
use smartclock_gps::engine::Engine;
use smartclock_gps::engine::Options;
use smartclock_gps::engine::Output;
use smartclock_gps::engine::identity::Identity;
use smartclock_gps::engine::identity::UT_PLUS_2_2;
use smartclock_gps::engine::identity::VP_EIGHT;
use smartclock_gps::engine::identity::VP_EIGHT_WITHOUT_TIME_RAIM;
use smartclock_gps::engine::identity::VP_SIX;
use smartclock_gps::engine::saved::CONFIGURATION_58503A;
use smartclock_gps::engine::saved::ChannelCount;
use smartclock_gps::engine::saved::SavedConfiguration;
use smartclock_gps::engine::settings::PulseMode;
use smartclock_gps::record::Fix;
use smartclock_gps::record::Position;
use smartclock_gps::record::field::FieldSet;
use smartclock_gps::record::field::RecordField;
use smartclock_gps::record::field::UNFILLED;
use smartclock_gps::record::field::changed;
use smartclock_gps::source::scene;
use smartclock_gps::source::scene::DECLARATION;
use smartclock_gps::units::Centimeters;
use smartclock_gps::units::HundredNanoseconds;
use smartclock_gps::units::MilliArcSeconds;
use smartclock_gps::units::Milliseconds;
use smartclock_gps::units::Seconds;
use std::collections::BTreeMap;
use std::env;
use std::fmt::Write as _;
use std::fs;
use std::path::Path;
use std::path::PathBuf;

/// The format line every vector starts with.
const FORMAT: &str = "oncore engine vectors 1";
/// How many vectors there are, so that none goes missing unseen.
const VECTORS: usize = 56;

/// How far into a second its events fall.
const EVENTS_AT: u64 = 500;
const MILLISECONDS_PER_SECOND: u64 = 1000;

fn hex(bytes: &[u8]) -> String {
    bytes.iter().fold(String::new(), |mut text, byte| {
        write!(text, "{byte:02x}").expect("a string takes it");
        text
    })
}

/// The vector whose scene is held to [`DECLARATION`]: one with a leap second
/// announced.
const DECLARED_BY: &str = "vp-eight-leap.txt";

/// The configuration a vector's head names `status-every-other-second`,
/// of an engine that fixes from power-up and sends its position status
/// every other second, six-channel and eight-channel.
const STATUS_EVERY_OTHER_SECOND_SIX: SavedConfiguration = SavedConfiguration {
    channels: ChannelCount::Six,
    fixing: true,
    position_status_rate: Seconds(2),
    visibility_updates: false,
    time_raim_rate: Seconds(0),
    raim_algorithm: false,
    raim_alarm_limit: HundredNanoseconds(10),
    pulse_mode: PulseMode::Always,
};
const STATUS_EVERY_OTHER_SECOND_EIGHT: SavedConfiguration = SavedConfiguration {
    channels: ChannelCount::Eight,
    ..STATUS_EVERY_OTHER_SECOND_SIX
};

/// The engine a vector's head names.
fn identity(head: &Head) -> &'static Identity {
    match head.field("engine") {
        "vp-six" => &VP_SIX,
        "vp-eight" => &VP_EIGHT,
        "vp-eight-without-time-raim" => &VP_EIGHT_WITHOUT_TIME_RAIM,
        "ut-plus-2-2" => &UT_PLUS_2_2,
        other => panic!("no engine {other}"),
    }
}

/// How the engine a vector's head names departs from it.
fn options(head: &Head) -> Options {
    let mut options = Options::DEFAULT;
    options.deaf = head.field("answers") == "no";
    options.saved = match head.field("saved") {
        "none" => None,
        "status-every-other-second" => Some(match ChannelCount::of(identity(head).model()) {
            ChannelCount::Six => &STATUS_EVERY_OTHER_SECOND_SIX,
            ChannelCount::Eight => &STATUS_EVERY_OTHER_SECOND_EIGHT,
        }),
        "58503a" => Some(&CONFIGURATION_58503A),
        other => panic!("no saved configuration {other}"),
    };
    options
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

/// Runs one vector's events and gives the file they make.
fn run(text: &str) -> String {
    let mut head = Head::of("");
    let mut events: BTreeMap<u64, Vec<Event>> = BTreeMap::new();
    let mut last = 0;
    let mut together = false;
    for line in text.lines() {
        if let Some(field) = line.strip_prefix("# ") {
            head.take(line, field);
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
    let mut engine =
        Engine::new(identity(&head), options(&head)).expect("an engine that powers up");
    let mut antenna = head.field("antenna") == "on";
    let mut lines = Lines {
        text: head.lines.join("\n") + "\n",
        second: 0,
    };
    // On past the last event while an answer waits, so a self-test
    // asked near the end still answers.
    let mut second = 0;
    while second < last || engine.deadline().is_some() {
        second += 1;
        lines.second = second;
        let record = scene.record(second, antenna, &engine.settings());
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
                    let record = scene.record(second, antenna, &engine.settings());
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

/// The head of the vector a scene is held to.
fn declared_head() -> Head {
    let root = Path::new(env!("CARGO_MANIFEST_DIR"));
    let text =
        fs::read_to_string(root.join("tests/vectors/engine").join(DECLARED_BY)).expect("a vector");
    Head::of(&text)
}

/// A scene fills what it declares and nothing else, and all the
/// images use.  Of the settings the images set, it acts on those it
/// declares and is excused the rest, none for nothing.
#[test]
fn a_scene_fills_what_it_declares() {
    let root = Path::new(env!("CARGO_MANIFEST_DIR"));
    let exceptions = fs::read_to_string(root.join(EXCEPTIONS)).expect("the exceptions table");
    let shortfall = DECLARATION.shortfall(&exceptions);
    assert!(shortfall.is_empty(), "shortfall: {shortfall:?}");
    let exceptions =
        fs::read_to_string(root.join(SETTING_EXCEPTIONS)).expect("the setting exceptions table");
    let unacted = DECLARATION.unacted(&exceptions);
    assert!(unacted.is_empty(), "unacted: {unacted:?}");
    let needless = DECLARATION.needless(&exceptions);
    assert!(needless.is_empty(), "needless: {needless:?}");

    let head = declared_head();
    let engine = Engine::new(identity(&head), options(&head)).expect("an engine that powers up");
    let record = Scene::new(&head).record(1, true, &engine.settings());
    let undeclared = DECLARATION.undeclared(&UNFILLED, &record);
    assert!(undeclared.is_empty(), "undeclared: {undeclared:?}");
    let unshown = DECLARATION.unshown(changed(&UNFILLED, &record));
    assert!(unshown.is_empty(), "unshown: {unshown:?}");
}

/// `@@As` (VP p. 55): 10 degrees north, 20 east, 100 m above mean sea
/// level; and `@@At` (VP p. 57), position hold on.
const HOLD_AT: [u8; 20] = [
    0x40, 0x40, 0x41, 0x73, 0x02, 0x25, 0x51, 0x00, 0x04, 0x4a, 0xa2, 0x00, 0x00, 0x00, 0x27, 0x10,
    0x01, 0x9e, 0x0d, 0x0a,
];
const HOLD: [u8; 8] = [0x40, 0x40, 0x41, 0x74, 0x01, 0x34, 0x0d, 0x0a];
/// Where that is.
const HELD: Position = scene::position(
    MilliArcSeconds(36_000_000),
    MilliArcSeconds(72_000_000),
    Centimeters(10_000),
);

/// A scene acts on the two settings it declares: with the position
/// held, its record is at the position to hold and its fix of time
/// alone; a position to hold changes nothing until it is held.
#[test]
fn a_scene_holds_the_position_it_is_told_to() {
    let head = declared_head();
    let scene = Scene::new(&head);
    let mut engine =
        Engine::new(identity(&head), options(&head)).expect("an engine that powers up");
    let mut lines = Lines {
        text: String::new(),
        second: 1,
    };
    let now = Milliseconds(MILLISECONDS_PER_SECOND);
    let free = scene.record(1, true, &engine.settings());
    assert_eq!(free.fix, Fix::ThreeD);
    assert_ne!(free.position, Some(HELD));

    engine.receive(&HOLD_AT, &free, now, &mut lines);
    assert_eq!(scene.record(1, true, &engine.settings()), free);

    engine.receive(&HOLD, &free, now, &mut lines);
    let held = scene.record(1, true, &engine.settings());
    assert_eq!(held.fix, Fix::TimeOnly);
    assert_eq!(held.position, Some(HELD));
    assert_eq!(held.derived.ecef, Some(ecef(HELD)));
    assert_eq!(
        changed(&free, &held),
        FieldSet::of(&[
            RecordField::Fix,
            RecordField::Latitude,
            RecordField::Longitude,
            RecordField::EllipsoidHeight,
            RecordField::MeanSeaLevelHeight,
            RecordField::Ecef,
        ])
    );
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
