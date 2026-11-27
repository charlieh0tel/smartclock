//! The synthetic engine, the shared one on the board's clock, against
//! the firmware's side of the messages: its response lengths
//! (`oncore_messages`, read from the Z3805A 3543B's table at
//! `0x4fece`), its field programs (`0x50fd2` on, docs/firmware/gps.md,
//! "Decoding") and its matching (`gps_oncore.c`, `gps_response_match`).
//! What the engine answers, field by field, smartclock-gps's own tests
//! hold; these hold the emulator's hold on it: its clock, its hooks,
//! its sky and its names.

use std::cell::RefCell;
use std::path::Path;
use std::rc::Rc;

use jiff::Timestamp;
use smartclock_gps::engine::Engine;
use smartclock_gps::engine::Options;
use smartclock_gps::engine::identity::VP_EIGHT;
use smartclock_gps::engine::saved::CONFIGURATION_58503A;
use smartclock_gps::oncore::MessageId;
use smartclock_gps::oncore::OVERHEAD;
use smartclock_gps::oncore::encode as encode_into;
use smartclock_gps::record::field::FieldSet;
use smartclock_gps::record::field::RecordField;
use smartclock_gps::record::field::UNFILLED;
use smartclock_gps::record::field::changed;
use smartclock_gps::source::fake_receiver::DECLARATION;
use smartclock_gps::units::Nanoseconds;
use smartclock_gps::units::Seconds;

use super::B1121;
use super::B4121;
use super::B4121_WITHOUT_TIME_RAIM;
use super::Config;
#[cfg(feature = "mc683xx")]
use super::ENGINES;
#[cfg(feature = "mc683xx")]
use super::Family;
use super::GPS_LESS_UTC;
use super::GT_74;
use super::GT_74_PULSE_EARLY;
#[cfg(feature = "mc683xx")]
use super::MOTOROLA_BINARY;
#[cfg(feature = "mc683xx")]
use super::NMEA;
use super::PICOSECONDS_PER_NANOSECOND;
use super::PICOSECONDS_PER_SECOND;
use super::Pulse;
use super::UT_PLUS;
use super::engine::EngineError;
use super::engine::Synthetic;
use super::sky::FIRMWARE_DEFAULT;
use super::sky::Sky;
use crate::board::time::CPU_HZ;
use crate::board::time::Cycles;
use crate::line::Line;

/// The engine on a line, as the firmware's end of the line sees it.
struct Bench {
    /// The firmware's end.
    line: Line,
    /// The engine.
    engine: Synthetic,
    /// Now.
    now: Cycles,
}

/// 2026-10-22T12:00:00Z.
const START: i64 = 1_792_670_400;
/// Long enough for a self-test's answer, which comes six seconds after
/// its command.
const SELF_TEST: Cycles = Cycles(7 * CPU_HZ);

/// `id`'s frame with `body`.
fn encode(id: [u8; 2], body: &[u8]) -> Vec<u8> {
    let mut frame = vec![0; OVERHEAD + body.len()];
    encode_into(MessageId::new(id), body, &mut frame).expect("room for the frame");
    frame
}

/// The engine's configuration: `engine` with GPS time `leap_seconds`
/// ahead of UTC, as it is made.
fn config(leap_seconds: i8, engine: &'static super::Engine) -> Config {
    Config {
        position: FIRMWARE_DEFAULT,
        start: Timestamp::from_second(START).expect("a time"),
        leap_seconds: Seconds(leap_seconds),
        engine,
        options: Options::DEFAULT,
    }
}

impl Bench {
    /// The six-channel engine, the Z3805A's.
    fn new() -> Self {
        Self::of(config(GPS_LESS_UTC, &B1121))
    }

    /// The eight-channel engine, the 58503A's.
    fn eight() -> Self {
        Self::of(config(GPS_LESS_UTC, &B4121))
    }

    /// The UT Plus, the 58503B's.
    fn ut_plus() -> Self {
        Self::of(config(GPS_LESS_UTC, &UT_PLUS))
    }

    /// The engine `config` makes.
    fn of(config: Config) -> Self {
        let line = Line::new("gps", false);
        let engine = Synthetic::new(line.attach(), config, false).expect("an engine");
        Self {
            line,
            engine,
            now: Cycles(0),
        }
    }

    /// Sends `id` with `body`, as the firmware would, and moves time
    /// on by `span`; what the engine sent meanwhile.
    fn exchange(&mut self, id: &[u8; 2], body: &[u8], span: Cycles) -> Vec<u8> {
        for byte in encode(*id, body) {
            self.line.transmit(byte);
        }
        self.wait(span)
    }

    /// Moves time on by `span`; what the engine sent meanwhile.
    fn wait(&mut self, span: Cycles) -> Vec<u8> {
        self.now = self.now + span;
        self.engine.advance(self.now);
        std::iter::from_fn(|| self.line.receive()).collect()
    }

    /// Sends a command and takes its one answer.
    fn ask(&mut self, id: &[u8; 2], body: &[u8]) -> Vec<u8> {
        self.exchange(id, body, Cycles(1))
    }
}

/// The messages in `bytes`, split by the firmware's lengths.
fn split(mut bytes: &[u8]) -> Vec<Vec<u8>> {
    let mut messages = Vec::new();
    while bytes.len() >= 4 {
        let length = response_length(&[bytes[2], bytes[3]]).expect("a known response");
        messages.push(bytes[..length].to_vec());
        bytes = &bytes[length..];
    }
    assert!(bytes.is_empty(), "left over: {bytes:?}");
    messages
}

/// The IDs of `messages`.
fn ids(messages: &[Vec<u8>]) -> Vec<&[u8]> {
    messages.iter().map(|message| &message[2..4]).collect()
}

/// The firmware's response lengths, `@@` to LF (`oncore_messages`).
fn response_length(id: &[u8; 2]) -> Option<usize> {
    const LENGTHS: [(&[u8; 2], usize); 41] = [
        (b"Aa", 10),
        (b"Ab", 10),
        (b"Ac", 11),
        (b"Ad", 11),
        (b"Ae", 11),
        (b"Af", 15),
        (b"Ag", 8),
        (b"Ah", 8),
        (b"Ai", 9),
        (b"Aj", 8),
        (b"Ak", 9),
        (b"Al", 9),
        (b"Am", 12),
        (b"An", 8),
        (b"Ap", 25),
        (b"Aq", 8),
        (b"Ar", 8),
        (b"As", 20),
        (b"At", 8),
        (b"Aw", 8),
        (b"Ay", 11),
        (b"Az", 11),
        (b"AD", 9),
        (b"Ba", 68),
        (b"Bb", 92),
        (b"Bc", 82),
        (b"Bd", 23),
        (b"Bj", 8),
        (b"Bk", 69),
        (b"Bn", 59),
        (b"Ca", 9),
        (b"Cf", 7),
        (b"Cg", 8),
        (b"Cj", 294),
        // the 58503A 3704's eight-channel messages (0x51178)
        (b"Ea", 76),
        (b"Ek", 71),
        (b"En", 69),
        (b"Fa", 9),
        (b"AB", 8),
        // the 58503B 1.01.04's UTC offset status (message 0x2a, 0x58ae0)
        // and its DCXO query (message 0x39)
        (b"Bo", 8),
        (b"Ta", 12),
    ];
    LENGTHS
        .iter()
        .find(|(known, _)| *known == id)
        .map(|(_, length)| *length)
}

/// Runs a field program as `oncore_decode_fields` does over a body;
/// the record, and how many bytes of the body it read.
fn decode(program: &[u8], source: &[u8]) -> (Vec<u8>, usize) {
    let mut record = Vec::new();
    let mut read = 0;
    let mut repeats = 1_i32;
    let mut group = 0;
    let mut at = 0;
    loop {
        let code = program[at];
        match code {
            0x80 => return (record, read),
            0x81 => {
                repeats -= 1;
                if repeats > 0 {
                    at = group;
                    continue;
                }
            }
            1..=0x7f => {
                let count = usize::from(code);
                record.extend(&source[read..read + count]);
                read += count;
            }
            0x00 | 0x84 => record.push(0),
            0x82 => read += 1,
            0x83 => {
                read += 1;
                record.push(0);
            }
            0x85 => record.push(if source[read] & 0x80 != 0 { 0xff } else { 0 }),
            _ => {
                repeats = 0x100 - i32::from(code);
                group = at + 1;
            }
        }
        at += 1;
    }
}

/// A message's body: between its ID and its checksum.
fn body(message: &[u8]) -> &[u8] {
    &message[4..message.len() - 3]
}

/// The engine set going: fixing, @@Ba and @@Bn each second.
fn fixing() -> Bench {
    set_going(Bench::new())
}

/// `bench` set going as `fixing` says.
fn set_going(mut bench: Bench) -> Bench {
    bench.ask(b"Cg", &[1]);
    bench.ask(b"Ba", &[1]);
    bench.ask(b"Bn", &[1, 1, 0, 10, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0]);
    bench
}

#[test]
fn injected_bytes_go_out_as_they_are_after_what_is_due() {
    let mut bench = fixing();
    let injected = Rc::new(RefCell::new(Vec::new()));
    bench.engine.on_injected(Rc::clone(&injected));
    injected.borrow_mut().extend_from_slice(b"@@Bh\x00\x2a\r\n");
    let sent = bench.wait(Cycles(CPU_HZ));
    assert!(
        sent.ends_with(b"@@Bh\x00\x2a\r\n"),
        "after the second's messages: {}",
        sent.escape_ascii()
    );
    assert!(
        sent.len() > b"@@Bh\x00\x2a\r\n".len(),
        "the second's messages too"
    );
    assert!(injected.borrow().is_empty(), "taken");
}

/// Each command the firmware sends a VP, with a body it sends, is
/// answered once at once, in the length the firmware frames it by.
#[test]
fn each_answer_has_the_length_the_firmware_frames_it_by() {
    let mut bench = fixing();
    let commands: [(&[u8; 2], &[u8]); 19] = [
        (b"Aa", &[0xff; 3]),
        (b"Ab", &[0xff; 3]),
        (b"Ac", &[0xff; 4]),
        (b"Ad", &[0x99; 4]),
        (b"Af", &[0x99; 5]),
        (b"Ai", &[1, 0xff]),
        (b"Am", &[0xff, 0, 0, 0, 0]),
        (b"Ao", &[49]),
        (
            b"As",
            &[
                0x7f, 0xff, 0xff, 0xff, 0x7f, 0xff, 0xff, 0xff, 0x7f, 0xff, 0xff, 0xff, 0xff,
            ],
        ),
        (b"Bb", &[0]),
        (b"Bc", &[0]),
        (b"Bd", &[0]),
        (b"Bj", &[0]),
        (b"Bk", &[0]),
        (b"Bn", &[0xff; 15]),
        (b"Cj", &[]),
        (b"AD", &[0, 32]),
        (b"AB", &[4]),
        // the 58503B's and the Z3816A's DCXO query, after @@Cj in their
        // init scripts, which start over unanswered
        (b"Ta", &[0xff; 5]),
    ];
    for (id, body) in commands {
        let answer = bench.ask(id, body);
        let messages = split(&answer);
        assert_eq!(messages.len(), 1, "{}", id.escape_ascii());
    }
}

/// `@@Ao` is answered with `@@Ap`, which is where the firmware's table
/// takes its answer from (`gps_response_match`).
#[test]
fn the_datum_is_answered_by_the_user_datum_message() {
    for mut bench in [Bench::new(), Bench::ut_plus()] {
        let answer = bench.ask(b"Ao", &[49]);
        assert_eq!(&answer[..4], b"@@Ap");
    }
}

#[test]
fn the_receiver_id_has_the_time_raim_option() {
    let mut bench = Bench::new();
    let answer = bench.ask(b"Cj", &[]);
    assert_eq!(answer.len(), 294);
    // oncore_receiver_id_field_codes (0x51004), from past the ID: the
    // firmware reads two bytes past the body, the checksum and CR
    let (record, read) = decode(
        &[
            0x82, 0x82, 0x21, 0x83, 0x82, 0xf7, 0x1a, 0x83, 0x82, 0x81, 0x80,
        ],
        &answer[4..],
    );
    assert_eq!(read, body(&answer).len() + 2);
    // oncore_receiver_id: the copyright's 34 bytes, then 27 a line
    let options = &record[34 + 8 * 27..];
    assert!(
        options.starts_with(b"OPTIONS LIST "),
        "{}",
        options.escape_ascii()
    );
    assert!(options[13..].contains(&b'I'));
}

/// Each name is the Oncore it says: its model, its software and its
/// options.
#[test]
fn each_engine_is_the_oncore_its_name_says() {
    for (engine, lines) in [
        (
            &B1121,
            &[
                "MODEL #    B1121P1114",
                "SOFTWARE REV # 4",
                "OPTIONS LIST    IB",
            ][..],
        ),
        (
            &B4121,
            &[
                "MODEL #    B4121P1115",
                "SOFTWARE REV # 8",
                "OPTIONS LIST    IB",
            ],
        ),
        (
            &B4121_WITHOUT_TIME_RAIM,
            &["MODEL #    B4121P1115", "OPTIONS LIST    B "],
        ),
        (
            &UT_PLUS,
            &[
                "MODEL #    R5122U1152",
                "SOFTWARE VER # 2",
                "SOFTWARE REV # 2",
            ],
        ),
    ] {
        let mut bench = Bench::of(config(GPS_LESS_UTC, engine));
        let answer = bench.ask(b"Cj", &[]);
        assert_eq!(answer.len(), 294, "{}", engine.name);
        let text = answer.escape_ascii().to_string();
        for line in lines {
            assert!(text.contains(line), "{}: {line}: {text}", engine.name);
        }
    }
}

#[test]
fn the_periodic_messages_come_each_second_as_the_firmware_decodes_them() {
    let mut bench = fixing();
    let messages = split(&bench.wait(Cycles(CPU_HZ)));
    assert_eq!(ids(&messages), [b"Ba", b"Bn"]);
    // oncore_status_field_codes (0x50fd2) reads @@Ba's body whole
    let status = &messages[0];
    let (record, read) = decode(&[0x04, 0x00, 0x1d, 0x00, 0x1c, 0x80], body(status));
    assert_eq!(read, body(status).len());
    // 6 visible and tracked, the two lowest of the sky's eight under the
    // 10-degree mask: record offsets 36 and 37, after the skipped bytes
    // and the DOP type
    assert_eq!(record[36..38], [6, 6]);
    // oncore_raim_field_codes (0x50fe6) reads @@Bn's whole
    let traim = &messages[1];
    let (_, read) = decode(
        &[
            0x02, 0x00, 0x03, 0x00, 0x85, 0x03, 0x04, 0x00, 0x0a, 0x00, 0xfa, 0x01, 0x00, 0x04,
            0x81, 0x80,
        ],
        body(traim),
    );
    assert_eq!(read, body(traim).len());
    assert_eq!(split(&bench.wait(Cycles(CPU_HZ))).len(), 2);
}

/// The engine's time is the run's: UTC at power-up plus the seconds
/// since, and GPS time ahead of it by the leap seconds given.
#[test]
fn time_is_the_runs_and_gps_time_is_ahead_by_the_leap_seconds_given() {
    for leap_seconds in [GPS_LESS_UTC, 37] {
        let mut bench = set_going(Bench::of(config(leap_seconds, &B1121)));
        let utc = bench.ask(b"Aa", &[0xff; 3]);
        assert_eq!(body(&utc), [12, 0, 0]);
        let date = bench.ask(b"Ac", &[0xff; 4]);
        assert_eq!(body(&date), [10, 22, 0x07, 0xea]);
        bench.ask(b"Aw", &[0]);
        let gps = bench.ask(b"Aa", &[0xff; 3]);
        assert_eq!(body(&gps), [12, 0, leap_seconds.cast_unsigned()]);
    }
}

#[test]
fn idle_sends_nothing_each_second() {
    let mut bench = Bench::new();
    bench.ask(b"Ba", &[1]);
    assert!(bench.wait(Cycles(3 * CPU_HZ)).is_empty());
}

/// A self-test's answer is held back, goes out when due, between two
/// seconds, and passes.
#[test]
fn the_self_test_answers_later_and_passes() {
    for (mut bench, id) in [(Bench::new(), b"Ca"), (Bench::eight(), b"Fa")] {
        assert!(bench.exchange(id, &[], Cycles(1)).is_empty());
        let early = split(&bench.wait(Cycles(5 * CPU_HZ)));
        assert!(ids(&early).iter().all(|sent| sent != id), "not yet");
        let messages = split(&bench.wait(Cycles(2 * CPU_HZ)));
        let answer = messages
            .iter()
            .find(|message| &message[2..4] == id)
            .expect("the self-test's answer");
        assert_eq!(body(answer), [0, 0]);
    }
}

/// The pulses `bench` puts out by `span` from now, each's picoseconds
/// from power-up.
fn pulses(bench: &mut Bench, span: Cycles) -> Vec<u64> {
    let pulses = Rc::new(RefCell::new(Vec::<Pulse>::new()));
    let seen = Rc::clone(&pulses);
    bench
        .engine
        .on_pulse(Box::new(move |pulse| seen.borrow_mut().push(*pulse)));
    bench.wait(span);
    pulses
        .borrow()
        .iter()
        .map(|pulse| pulse.picoseconds)
        .collect()
}

/// With the pulse settings at their defaults, each second's pulse is
/// at its start.
#[test]
fn each_pulse_is_at_its_seconds_start() {
    let mut bench = fixing();
    assert_eq!(
        pulses(&mut bench, Cycles(2 * CPU_HZ + 5)),
        [PICOSECONDS_PER_SECOND, 2 * PICOSECONDS_PER_SECOND]
    );
}

/// The emulator makes the pulse, so it acts on the engine's settings:
/// a cable delay of N ns puts it N ns earlier (`@@Az`, VP p. 68), a
/// pulse offset of N ns N ns later (`@@Ay`, VP p. 66), each from the
/// next pulse on.
#[test]
fn the_cable_delay_and_the_pulse_offset_move_the_pulse() {
    const CABLE_DELAY: u32 = 250_000;
    const OFFSET: u32 = 40_000;
    let picoseconds = |nanoseconds: u32| u64::from(nanoseconds) * PICOSECONDS_PER_NANOSECOND;
    let mut bench = fixing();
    bench.ask(b"Az", &CABLE_DELAY.to_be_bytes());
    assert_eq!(
        pulses(&mut bench, Cycles(2 * CPU_HZ)),
        [
            PICOSECONDS_PER_SECOND - picoseconds(CABLE_DELAY),
            2 * PICOSECONDS_PER_SECOND - picoseconds(CABLE_DELAY),
        ]
    );
    bench.ask(b"Ay", &OFFSET.to_be_bytes());
    assert_eq!(
        pulses(&mut bench, Cycles(CPU_HZ)),
        [3 * PICOSECONDS_PER_SECOND + picoseconds(OFFSET) - picoseconds(CABLE_DELAY)]
    );
}

/// The pulse mode says when the pulse is put out (`@@Bn`, VP p. 104):
/// mode 0 never; mode 2 only while a satellite is tracked, so not
/// while the engine is idle.
#[test]
fn the_pulse_mode_puts_the_pulse_out_or_not() {
    // `set_going`'s Time RAIM setup with the 1PPS control mode, its
    // fifth byte, as given
    let traim = |mode: u8| [1, 1, 0, 10, mode, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0];
    let mut bench = fixing();
    bench.ask(b"Bn", &traim(0));
    assert!(pulses(&mut bench, Cycles(2 * CPU_HZ)).is_empty(), "off");
    bench.ask(b"Bn", &traim(1));
    assert_eq!(pulses(&mut bench, Cycles(CPU_HZ)).len(), 1, "always");
    bench.ask(b"Bn", &traim(2));
    assert_eq!(pulses(&mut bench, Cycles(CPU_HZ)).len(), 1, "tracking");
    bench.ask(b"Cg", &[0]);
    assert!(pulses(&mut bench, Cycles(CPU_HZ)).is_empty(), "idle");
}

#[test]
fn a_six_channel_engine_leaves_the_eight_channel_messages_unanswered() {
    let mut bench = fixing();
    for (id, body) in [
        (b"Ea", &[0][..]),
        (b"Ek", &[0]),
        (b"En", &[0xff; 15]),
        (b"Fa", &[]),
    ] {
        let mut sent = bench.ask(id, body);
        sent.extend(bench.wait(SELF_TEST));
        assert!(
            ids(&split(&sent)).iter().all(|answered| answered != id),
            "{}",
            id.escape_ascii()
        );
    }
}

/// The antenna is a control, the mc683xx core's alone (`set_antenna`).
#[cfg(feature = "mc683xx")]
#[test]
fn a_disconnected_antenna_tracks_nothing_until_it_is_connected_again() {
    // @@Ba's tracked count: after the date, time, fraction, four
    // positions, three words and two bytes (VP p. 70)
    const TRACKED: usize = 4 + 3 + 4 + 16 + 6 + 2;
    let tracked = |bench: &mut Bench| {
        let messages = split(&bench.wait(Cycles(CPU_HZ)));
        body(&messages[0])[TRACKED]
    };
    let mut bench = fixing();
    assert_eq!(tracked(&mut bench), 6);
    bench.engine.set_antenna(false);
    assert_eq!(tracked(&mut bench), 0);
    bench.engine.set_antenna(true);
    assert_eq!(tracked(&mut bench), 6);
}

/// The UT Plus has no idle mode: it sends what is asked from power-up,
/// with no @@Cg.
#[test]
fn the_ut_plus_fixes_without_being_told_to() {
    let mut bench = Bench::ut_plus();
    assert!(bench.wait(Cycles(2 * CPU_HZ)).is_empty(), "no output asked");
    bench.ask(b"Ea", &[1]);
    let messages = split(&bench.wait(Cycles(CPU_HZ)));
    assert_eq!(ids(&messages), [b"Ea"]);
}

/// Deaf and saved as a 58503A leaves it, as the Z3817A's board has it:
/// it answers nothing, and sends `@@Ea` and `@@En` each second from
/// power-up all the same.
#[test]
fn a_deaf_engine_saved_as_a_58503a_leaves_it_streams_unasked() {
    let mut options = Options::DEFAULT;
    options.deaf = true;
    options.saved = Some(CONFIGURATION_58503A);
    let mut bench = Bench::of(Config {
        options,
        ..config(GPS_LESS_UTC, &B4121_WITHOUT_TIME_RAIM)
    });
    assert!(bench.ask(b"Cj", &[]).is_empty(), "no answer");
    let messages = split(&bench.wait(Cycles(CPU_HZ)));
    let sent = ids(&messages);
    assert!(sent.contains(&&b"Ea"[..]), "{sent:?}");
    assert!(sent.contains(&&b"En"[..]), "{sent:?}");
}

/// The sky fills what the fake receiver declares and nothing else, all the
/// images use, its epoch on the second unless placed elsewhere, and acts on the settings it declares, excused the rest
/// by the exceptions tables' `fake receiver` rows, none for nothing.
#[test]
fn the_sky_fills_what_it_declares() {
    let root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../../docs/firmware");
    let read = |name: &str| std::fs::read_to_string(root.join(name)).expect("the table");
    let fields = read("gps-field-exceptions.tsv");
    let shortfall = DECLARATION.shortfall(&fields);
    assert!(shortfall.is_empty(), "shortfall: {shortfall:?}");
    let settings = read("gps-setting-exceptions.tsv");
    let unacted = DECLARATION.unacted(&settings);
    assert!(unacted.is_empty(), "unacted: {unacted:?}");
    let needless = DECLARATION.needless(&settings);
    assert!(needless.is_empty(), "needless: {needless:?}");

    let config = config(GPS_LESS_UTC, &B4121);
    // An epoch off the second, so that its part of a second shows.
    let after_second = Nanoseconds(12_726);
    let sky = Sky::new_at(
        config.position,
        config.start,
        config.leap_seconds,
        after_second,
    )
    .expect("a sky");
    let settings = Engine::new(&VP_EIGHT, Options::DEFAULT)
        .expect("an engine")
        .settings();
    let record = sky.record(1, &settings);
    assert_eq!(record.time.map(|time| time.subsecond), Some(after_second));
    let on_the_second = Sky::new(config.position, config.start, config.leap_seconds)
        .expect("a sky")
        .record(1, &settings);
    assert_eq!(
        on_the_second.time.map(|time| time.subsecond),
        Some(Nanoseconds(0)),
        "on the second unless placed elsewhere"
    );
    let undeclared = DECLARATION.undeclared(&UNFILLED, &record);
    assert!(undeclared.is_empty(), "undeclared: {undeclared:?}");
    // with no leap second pending the leap field is as unfilled; the
    // feed's test shows the fake receiver's with one
    let unshown = DECLARATION
        .unshown(changed(&UNFILLED, &record))
        .without(FieldSet::of(&[RecordField::Leap]));
    assert!(unshown.is_empty(), "unshown: {unshown:?}");
}

/// What a GT-74 on `bench` sends in `span`, a sentence a line, its CR
/// LF taken off.
fn sentences(bench: &mut Bench, span: Cycles) -> Vec<String> {
    String::from_utf8(bench.wait(span))
        .expect("ASCII")
        .split_terminator("\r\n")
        .map(str::to_owned)
        .collect()
}

/// Sends `sentence` to a GT-74 on `bench` as the firmware would, CR LF
/// and all.
fn tell(bench: &mut Bench, sentence: &str) {
    for byte in format!("{sentence}\r\n").bytes() {
        bench.line.transmit(byte);
    }
}

/// A GT-74 sends its sentences each second, as the engine's power-up
/// intervals have them (GT pp. 4, 5): `GGA`, `GSV` and `tps` every
/// second, and what an `int` asks for at interval 0 at once.
#[test]
fn a_gt_74_sends_its_sentences_each_second() {
    let mut bench = Bench::of(config(GPS_LESS_UTC, &GT_74));
    let names = |sentences: &[String]| -> Vec<String> {
        sentences
            .iter()
            .map(|sentence| sentence.split(',').take(2).collect::<Vec<_>>().join(","))
            .collect()
    };
    let first = sentences(&mut bench, Cycles(CPU_HZ));
    assert_eq!(
        names(&first),
        ["$GPGGA,120001", "$GPGSV,2", "$GPGSV,2", "$PFEC,GPtps"],
        "{first:?}"
    );
    assert!(
        first[3].starts_with("$PFEC,GPtps,261022120001,"),
        "{first:?}"
    );
    tell(&mut bench, "$PFEC,GPint,tst00");
    let tst = sentences(&mut bench, Cycles(1));
    assert!(tst[0].starts_with("$PFEC,GPtst,"), "{tst:?}");
}

/// A GT-74's 1 PPS goes out as its own `tps` says it is available
/// (`furuno::Engine::pulsing`): in the default control mode, 2, from the
/// first second, the fake receiver's sky giving a first fix of eight satellites
/// under a PDOP of 6 (GT p. 47); in mode 0 never; and `set,t` moves it,
/// a negative correction earlier (GT p. 11).
#[test]
fn a_gt_74s_pulse_is_where_and_when_its_settings_say() {
    const CORRECTION: u64 = 250;
    let mut bench = Bench::of(config(GPS_LESS_UTC, &GT_74));
    assert_eq!(
        pulses(&mut bench, Cycles(2 * CPU_HZ + 5)),
        [PICOSECONDS_PER_SECOND, 2 * PICOSECONDS_PER_SECOND]
    );
    tell(&mut bench, "$PFEC,GPrrs,1,100,0");
    assert!(pulses(&mut bench, Cycles(2 * CPU_HZ)).is_empty(), "mode 0");
    tell(&mut bench, "$PFEC,GPrrs,1,100,2");
    tell(&mut bench, &format!("$PFEC,GPset,t-{CORRECTION:06}"));
    assert_eq!(
        pulses(&mut bench, Cycles(CPU_HZ)),
        [5 * PICOSECONDS_PER_SECOND - CORRECTION * PICOSECONDS_PER_NANOSECOND]
    );
}

/// The GT-74 whose software puts its pulse 400 ns early does so with no
/// correction given, the image's correction making it good.
#[test]
fn the_early_gt_74s_pulse_is_400_ns_early() {
    const EARLY: u64 = 400;
    let mut bench = Bench::of(config(GPS_LESS_UTC, &GT_74_PULSE_EARLY));
    // second 1's sentences, half a second before its pulse, give the
    // fix the pulse waits for, so the first is second 1's
    assert_eq!(
        pulses(&mut bench, Cycles(2 * CPU_HZ + 5)),
        [
            PICOSECONDS_PER_SECOND - EARLY * PICOSECONDS_PER_NANOSECOND,
            2 * PICOSECONDS_PER_SECOND - EARLY * PICOSECONDS_PER_NANOSECOND
        ]
    );
}

/// A GT-74's sentences of each second go out half a second after the
/// pulse before it, of the second the next pulse marks
/// (`engine::furuno::SENTENCES_AFTER_PULSE`): nothing before 0.5 s, then
/// second 1's, and second 2's at 1.5 s.
#[test]
fn a_gt_74_sends_half_a_second_after_the_pulse() {
    let mut bench = Bench::of(config(GPS_LESS_UTC, &GT_74));
    assert!(sentences(&mut bench, Cycles(CPU_HZ / 2 - 1)).is_empty());
    let first = sentences(&mut bench, Cycles(1));
    assert!(first[0].starts_with("$GPGGA,120001,"), "{first:?}");
    assert!(sentences(&mut bench, Cycles(CPU_HZ - 1)).is_empty());
    let second = sentences(&mut bench, Cycles(1));
    assert!(second[0].starts_with("$GPGGA,120002,"), "{second:?}");
}

/// A GT-74 with its antenna off hears no satellite and puts out no
/// pulse until it is connected again.
#[cfg(feature = "mc683xx")]
#[test]
fn a_gt_74_with_its_antenna_off_puts_out_no_pulse() {
    let mut bench = Bench::of(config(GPS_LESS_UTC, &GT_74));
    assert_eq!(pulses(&mut bench, Cycles(CPU_HZ + 5)).len(), 1);
    bench.engine.set_antenna(false);
    assert!(pulses(&mut bench, Cycles(2 * CPU_HZ)).is_empty(), "off");
    bench.engine.set_antenna(true);
    assert_eq!(pulses(&mut bench, Cycles(CPU_HZ)).len(), 1, "on again");
}

/// A GT-74 has no options: neither deaf nor powered up configured.
#[test]
fn a_gt_74_takes_no_options() {
    let line = Line::new("gps", false);
    let mut config = config(GPS_LESS_UTC, &GT_74);
    config.options.deaf = true;
    assert!(matches!(
        Synthetic::new(line.attach(), config, false),
        Err(EngineError::FurunoOptions)
    ));
}

/// Each name is the engine of the family it says, which speaks the
/// protocol a board file's part names.
#[cfg(feature = "mc683xx")]
#[test]
fn each_engine_names_its_family_and_its_protocol() {
    for engine in ENGINES {
        assert_eq!(
            super::Engine::named(engine.name).map(|named| named.name),
            Some(engine.name)
        );
        let furuno = engine.name.starts_with("gt-74");
        assert_eq!(
            matches!(engine.family, Family::Furuno(_)),
            furuno,
            "{}",
            engine.name
        );
        assert_eq!(
            engine.family.protocol(),
            if furuno { NMEA } else { MOTOROLA_BINARY }
        );
    }
}
