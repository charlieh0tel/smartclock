//! The Z3805A 3543B's own firmware, booted on the mc683xx backend
//! (docs/emulator.md, "The mc683xx backend"): its board file loaded
//! through mc683xx-board with smartclock's device types and run on
//! mc683xx's MC68331, it reaches its SCPI prompt on the host port and
//! answers `*IDN?` there.  The session is a script, its query at a
//! fixed board time under `--deterministic`, so the run is the same on
//! a loaded host.  Only with `mc683xx`.

#![cfg(feature = "mc683xx")]

mod common;

use common::Scratch;
use common::Z3805A;
use common::assert_stopped;
use common::reset_causes;
use common::run_receiver;
use common::run_receiver_with_engine;
use common::script;

/// The session: the power-up prompt (about 1.1 s of board time), then
/// the query at 2 s, and its prompt.
const SESSION: &str = "# the Z3805A's boot on mc683xx
expect scpi >\\x20 within 5
at 2
send *IDN?
expect scpi >\\x20 within 5
stop
";

/// The longest the run may take, board seconds, should a step wait for
/// good.
const LIMIT: &str = "30";

#[test]
fn the_z3805a_boots_on_mc683xx_to_its_prompt_and_answers() {
    let eeprom = Scratch::new("boot-mc683xx");
    let script = Scratch::new("boot-mc683xx-script");
    std::fs::write(&script.0, SESSION).expect("the script written");
    let path = script.0.to_str().expect("a path");
    let (transcript, trace) = run_receiver(
        Z3805A,
        &eeprom.0,
        &["--core", "mc683xx", "--script", path, "--stop-after", LIMIT],
    );
    let transcript = String::from_utf8_lossy(&transcript);
    assert_eq!(
        transcript, "scpi > *IDN?\r\nHEWLETT-PACKARD,Z3805A,3625A01487,3543B-A\r\nscpi > ",
        "the image's revision and the EEPROM's serial number"
    );
    let stopped = trace.last().expect("a trace");
    assert!(
        stopped.starts_with("mc68331: stopped cycle="),
        "the script's stop, in mc68331's trace: {stopped}"
    );
}

/// A reset at once after a power cycle, inside the reset code's first
/// few hundred cycles (docs/emulator.md, "The controls"): the chip's
/// watchdog reset, and the board boots again and answers, its query
/// well after the second boot's prompt (about 1.1 s of board time).
const RESET_AFTER_POWER_CYCLE: &str = "# a reset just after a power cycle
at 3
event power-cycle
event reset
at 6
send *IDN?
expect HEWLETT-PACKARD within 5
expect scpi >\\x20 within 5
stop
";

#[test]
fn a_reset_just_after_a_power_cycle_boots_without_a_fault() {
    let eeprom = Scratch::new("reset-after-power-cycle");
    let script = script("reset-after-power-cycle", RESET_AFTER_POWER_CYCLE);
    let path = script.0.to_str().expect("a path");
    let (transcript, trace) = run_receiver_with_engine(
        Z3805A,
        &eeprom.0,
        &["--script", path, "--stop-after", LIMIT],
    );
    assert_eq!(reset_causes(&trace), ["PowerOn", "PowerOn", "Watchdog"]);
    assert_eq!(
        String::from_utf8_lossy(&transcript),
        "scpi > scpi > *IDN?\r\nHEWLETT-PACKARD,Z3805A,3625A01487,3543B-A\r\nscpi > ",
        "each boot's prompt, then the answer"
    );
    assert_stopped(&trace);
}
