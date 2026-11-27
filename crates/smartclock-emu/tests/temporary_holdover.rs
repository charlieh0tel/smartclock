//! A temporary holdover ends.  Setting the antenna delay puts the loop
//! in holdover until the engine has taken it (docs/firmware/gps.md,
//! "The cable delay"), and in holdover the firmware sets the EFC its
//! aging fit predicts, which soon after power-up is the EFC its
//! EEPROM's lifetime record holds.  On a board whose oscillator is the
//! one that EEPROM describes (docs/emulator.md, "The oscillator the
//! EEPROM describes") that EFC holds the frequency, so the holdover
//! ends with the TI within the hold threshold and the loop locks again.
//! The 58503A 3704 holds the EFC it has instead, and its holdover ends
//! too.  Slow unoptimized, so ignored but in `make emulator-slow`, which
//! firmware.yml's emulator job runs.  Only with `mc683xx`.

#![cfg(feature = "mc683xx")]

mod common;

use common::HP58503A;
use common::Receiver;
use common::Scratch;
use common::Z3801A;
use common::Z3805A;
use common::assert_stopped;
use common::run_receiver_with_engine;
use common::script;

/// The synthetic engine's UTC at power-up, fixed, so that the run
/// repeats (docs/emulator.md, "Scripts").
const GPS_START: &str = "2026-10-22T12:00:00Z";

/// The session: the loop locked, a 500 ns antenna delay set, and two
/// minutes on, past the holdover the set starts, the holdover's state
/// and the TI.
const SESSION: &str = "# an antenna delay set on a locked loop
expect scpi >\\x20 within 10
at 400
send :ROSCillator:HOLDover:DURation?
at 402
send :GPS:REFerence:ADELay 500E-9
at 520
send :ROSCillator:HOLDover:DURation?
at 522
send :PTIMe:TINTerval?
at 524
stop
";

/// The longest the run may take, board seconds.
const LIMIT: &str = "540";

/// The largest TI the loop may show once it has taken the step,
/// seconds.
const LOCKED_TI: f64 = 1e-7;

/// How `:ROSCillator:HOLDover:DURation?` ends when the loop is not in
/// holdover, and when it is: its second field (097-59551-02, 5-30).
const NOT_HOLDING: &str = ",0";
const HOLDING: &str = ",1";

/// The answers in `transcript`, echoes and prompts aside: each word
/// that starts with a sign, as every number these images answer with
/// does and no command.
fn answers(transcript: &str) -> Vec<&str> {
    transcript
        .split(|c: char| c.is_ascii_whitespace())
        .filter(|word| word.starts_with(['+', '-']))
        .collect()
}

/// `receiver`'s loop, locked, takes a 500 ns antenna delay through a
/// temporary holdover and locks again.
fn assert_holdover_ends(receiver: Receiver, name: &str) {
    let eeprom = Scratch::new(name);
    let script = script(name, SESSION);
    let path = script.0.to_str().expect("a path");
    let (transcript, trace) = run_receiver_with_engine(
        receiver,
        &eeprom.0,
        &[
            "--script",
            path,
            "--stop-after",
            LIMIT,
            "--skip-warm-up",
            "--gps-start",
            GPS_START,
        ],
    );
    let transcript = String::from_utf8_lossy(&transcript);
    let [before, after, ti] = answers(&transcript)[..] else {
        panic!("three answers: {transcript}");
    };
    assert!(before.ends_with(NOT_HOLDING), "{transcript}");
    assert!(
        !after.ends_with(HOLDING) && after.ends_with(NOT_HOLDING),
        "still in holdover: {transcript}"
    );
    let ti: f64 = ti.parse().expect("a TI");
    assert!(ti.abs() < LOCKED_TI, "{ti}");
    assert_stopped(&trace);
}

#[test]
#[ignore = "520 s of board time: minutes unoptimized; make emulator-slow runs it optimized"]
fn the_z3801as_temporary_holdover_ends() {
    assert_holdover_ends(Z3801A, "holdover-z3801a");
}

#[test]
#[ignore = "520 s of board time: minutes unoptimized; make emulator-slow runs it optimized"]
fn the_z3805as_temporary_holdover_ends() {
    assert_holdover_ends(Z3805A, "holdover-z3805a");
}

#[test]
#[ignore = "520 s of board time: minutes unoptimized; make emulator-slow runs it optimized"]
fn the_58503as_temporary_holdover_ends() {
    assert_holdover_ends(HP58503A, "holdover-58503a");
}
