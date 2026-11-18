//! The Furuno engine's test vectors: for each GT-74 and each scenario,
//! what it is sent second by second and every sentence it answers.
//!
//! A vector is a text file under `tests/vectors/furuno`.  Its head
//! gives the GT-74, the time it starts at, the place it is and the
//! second its record first tells anything, `known-from`, before which
//! the record is empty, as an engine's is that has heard no satellite;
//! each line after is one event, headed by the second it falls in:
//!
//! - `to` a sentence, as text without its CR LF, and `to-raw` bytes, in
//!   hex, that are not one;
//! - `from` a sentence the engine sent, as text without its CR LF;
//! - `antenna` `on` or `off`, the antenna put on or taken off.
//!
//! In each second the engine is first given the second, then that
//! second's events in the file's order.  What is sent on consecutive
//! lines is sent together, before any answer is written down.  The test
//! reads the head and the events sent, runs the engine, writes the file
//! again from what the engine did and compares the two.  With
//! `ENGINE_VECTORS=write` in the environment it writes what the engine
//! did over each file in place of comparing.

mod common;

use common::EXCEPTIONS;
use common::FakeReceiver;
use common::Head;
use common::SETTING_EXCEPTIONS;
use common::WRITE;
use common::WRITE_VARIABLE;
use common::ecef;
use common::unhex;
use smartclock_gps::engine::Output;
use smartclock_gps::engine::furuno::Engine;
use smartclock_gps::engine::furuno::GT_74;
use smartclock_gps::engine::furuno::GT_74_PULSE_EARLY;
use smartclock_gps::engine::furuno::GT_74_WITHOUT_TIME_RAIM;
use smartclock_gps::engine::furuno::Identity;
use smartclock_gps::record::Fix;
use smartclock_gps::record::Record;
use smartclock_gps::source::fake_receiver;
use smartclock_gps::source::fake_receiver::DECLARATION;
use smartclock_gps::units::Centimeters;
use smartclock_gps::units::MilliArcSeconds;
use std::collections::BTreeMap;
use std::env;
use std::fmt::Write as _;
use std::fs;
use std::path::Path;
use std::path::PathBuf;

/// The format line every vector starts with.
const FORMAT: &str = "furuno engine vectors 1";
/// How many vectors there are, so that none goes missing unseen.
const VECTORS: usize = 8;
/// What ends every sentence, which the vectors leave off.
const LINE_END: &[u8] = b"\r\n";

/// The GT-74 a vector's head names.
fn identity(head: &Head) -> &'static Identity {
    match head.field("engine") {
        "gt-74" => &GT_74,
        "gt-74-without-time-raim" => &GT_74_WITHOUT_TIME_RAIM,
        "gt-74-pulse-early" => &GT_74_PULSE_EARLY,
        other => panic!("no engine {other}"),
    }
}

/// One thing done to the engine.
enum Event {
    /// Bytes sent it together, each line's with the word the line
    /// names them by and its text.
    To(Vec<(&'static str, String)>),
    /// The antenna put on or taken off.
    Antenna(bool),
}

/// The sentences an engine sends, as the vector's lines.
struct Lines {
    text: String,
    second: u64,
}

impl Output for Lines {
    fn send(&mut self, frame: &[u8]) {
        let sentence = frame
            .strip_suffix(LINE_END)
            .expect("a sentence ends with CR LF");
        let sentence = std::str::from_utf8(sentence).expect("a sentence is ASCII");
        writeln!(self.text, "{} from {sentence}", self.second).expect("a string takes it");
    }
}

/// The bytes a line of the vector sends.
fn bytes(kind: &str, text: &str) -> Vec<u8> {
    match kind {
        "to" => [text.as_bytes(), LINE_END].concat(),
        _ => unhex(text),
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
        let mut words = line.splitn(3, ' ');
        let second: u64 = words.next().expect("a second").parse().expect("a number");
        let kind = words.next().expect("a kind");
        let value = words.next().expect("a value");
        last = last.max(second);
        let events = events.entry(second).or_default();
        let sent = match kind {
            "to" => ("to", value.to_owned()),
            "to-raw" => ("to-raw", value.to_owned()),
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

    let receiver = FakeReceiver::new(&head);
    let known_from = u64::try_from(head.number("known-from")).expect("a second");
    let mut engine = Engine::new(identity(&head));
    let mut antenna = head.field("antenna") == "on";
    let record = |second: u64, antenna: bool, engine: &Engine| {
        if second < known_from {
            Record::EMPTY
        } else {
            receiver.record(second, antenna, &engine.settings())
        }
    };
    let mut lines = Lines {
        text: head.lines.join("\n") + "\n",
        second: 0,
    };
    for second in 1..=last {
        lines.second = second;
        engine.second(&record(second, antenna, &engine), &mut lines);
        for event in events.get(&second).into_iter().flatten() {
            match event {
                Event::To(run) => {
                    for (kind, text) in run {
                        writeln!(lines.text, "{second} {kind} {text}").expect("a string takes it");
                    }
                    let now = record(second, antenna, &engine);
                    for (kind, text) in run {
                        engine.receive(&bytes(kind, text), &now, &mut lines);
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
    let directory = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/vectors/furuno");
    let mut paths: Vec<PathBuf> = fs::read_dir(directory)
        .expect("the vectors' directory")
        .map(|entry| entry.expect("an entry").path())
        .collect();
    paths.sort();
    paths
}

/// The vectors' fake receiver under the Furuno engine is the Oncore
/// vectors' provider, held to the same declaration and the same exceptions; and
/// in position hold, set by the GT-74's sentences, its record is at the
/// position entered.
#[test]
fn the_fake_receiver_is_the_one_declared() {
    let root = Path::new(env!("CARGO_MANIFEST_DIR"));
    let exceptions = fs::read_to_string(root.join(EXCEPTIONS)).expect("the exceptions table");
    assert!(DECLARATION.shortfall(&exceptions).is_empty());
    let exceptions =
        fs::read_to_string(root.join(SETTING_EXCEPTIONS)).expect("the setting exceptions table");
    assert!(DECLARATION.unacted(&exceptions).is_empty());
    assert!(DECLARATION.needless(&exceptions).is_empty());

    let path = root.join("tests/vectors/furuno/gt-74-hold.txt");
    let head = Head::of(&fs::read_to_string(path).expect("a vector"));
    let receiver = FakeReceiver::new(&head);
    let mut engine = Engine::new(identity(&head));
    let mut lines = Lines {
        text: String::new(),
        second: 1,
    };
    let free = receiver.record(1, true, &engine.settings());
    for sentence in [
        "$PFEC,GPset,Z2",
        "$GPGLL,1000.0000,N,02000.0000,E,,",
        "$PFEC,GPset,H000100.0",
    ] {
        engine.receive(&bytes("to", sentence), &free, &mut lines);
    }
    let held = receiver.record(1, true, &engine.settings());
    let place = fake_receiver::position(
        MilliArcSeconds(36_000_000),
        MilliArcSeconds(72_000_000),
        Centimeters(10_000),
    );
    assert_eq!(free.fix, Fix::ThreeD);
    assert_eq!(held.fix, Fix::TimeOnly);
    assert_eq!(held.position, Some(place));
    assert_eq!(held.derived.ecef, Some(ecef(place)));
}

#[test]
fn the_engine_sends_every_vectors_sentences() {
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
