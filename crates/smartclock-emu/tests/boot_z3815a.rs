//! The Z3815A 4010's own firmware, booted in the emulator on its board
//! file, `boards/z3815a.toml`, with the Z3816A's `--model`, the emulator
//! having none of its own (docs/emulator.md, "The boards"): its GPS
//! engine the synthetic Furuno GT-74, its EEPROM the synthetic image,
//! its ADC's channels and its oscillator's puck the board file's.  On
//! the board file's FPGA, the LCA model running the bitstream's
//! netlist, the boot is slow, so ignored but when asked: it passes every
//! power-up test, and a longer run locks.  With `--fpga-design
//! unmodeled`, the part running no design, the firmware finds the FPGA
//! dead and stays in power-up, but it reaches its prompt on the SCI,
//! runs its engine's init, names the engine and tracks, on every `cargo
//! test`.  Either way the event log reads back, its first entry the
//! synthetic EEPROM's "Log cleared", with no reset, and "PAL Fail" is
//! not among the hardware alarms.  The sessions are
//! scripts under `--deterministic`.  Only with `mc683xx`.

#![cfg(feature = "mc683xx")]

mod common;

use common::Scratch;
use common::Z3815A;
use common::answer;
use common::answer_lines;
use common::repository;
use common::reset_causes;
use common::run_receiver_with_engine;
use common::script;

/// The session: the image's request for `tst`, its first prompt, then
/// each query once the power-up tests have run.
const SESSION: &str = "# the Z3815A's boot
expect-gps $PFEC,GPint,tst00 within 30
expect scpi >\\x20 within 30
at 40
send *IDN?
expect scpi >\\x20 within 5
send :DIAGnostic:IDENtification:GPSystem?
expect scpi >\\x20 within 5
send :DIAGnostic:TEST:RESult?
expect scpi >\\x20 within 30
send :DIAGnostic:LOG:READ? 1
expect scpi >\\x20 within 5
at 60
send :SYNChronization:STATe?
expect scpi >\\x20 within 5
send :ALARm:HARDware?
expect scpi >\\x20 within 5
stop
";

/// The long session: the self-test's result, then the loop's state well
/// past its lock.
const LOCK_SESSION: &str = "# the Z3815A locks
expect scpi >\\x20 within 30
at 40
send :DIAGnostic:TEST:RESult?
expect scpi >\\x20 within 30
at 300
send :SYNChronization:STATe?
expect scpi >\\x20 within 5
send :DIAGnostic:LOG:READ? 1
expect scpi >\\x20 within 5
send :ALARm:HARDware?
expect scpi >\\x20 within 5
send :SYSTem:STATus?
expect scpi >\\x20 within 30
stop
";

/// The longest a run may take, board seconds, should a step wait for
/// good.
const LIMIT: &str = "90";
const LOCK_LIMIT: &str = "330";

/// The power-up tests' failures, bits of the 4010's table at `0x28e54`,
/// PROC to POWER from bit 1: none on the LCA model; with no design, the
/// FPGA's, the interpolator's and the internal reference's (6 to 8).
const LCA_FAILURES: &str = "+0,ALL";
const UNMODELED_FAILURES: &str = "+448,ALL";

/// The event log's first entry: the synthetic EEPROM's, which
/// `log_erase` writes at an erased part's first power-up
/// (`tools/firmware/synthetic_eeprom.py`).
const FIRST_LOG_ENTRY: &str = "\"Log 001:19940101.00:00:00: Log cleared\"";

/// `alarm_hardware`'s bit for "PAL Fail", which `alarm_hardware_update`
/// sets with the switch word's bit 3 set and the clocks good (`0x34258`),
/// and its word with no fault at all (`0x341b8`).
const PAL_ALARM: u16 = 1 << 8;
const NO_HARDWARE_ALARM: &str = "+0";

/// The status screen's heading for a card whose output is in alarm, the
/// output controller's hardware fault (`status_screen_card_lines`,
/// `0x59716`).
const ALARM_CARD: &str = "[ ALARM ]";

/// `session` on the Z3815A's board with `options`: the transcript,
/// every step taken, the board never reset past its power-on.
fn run(name: &str, session: &str, limit: &str, options: &[&str]) -> String {
    let eeprom = Scratch::new(name);
    let script = script(name, session);
    let board = repository("boards/z3815a.toml");
    let (transcript, trace) = run_receiver_with_engine(
        Z3815A,
        &eeprom.0,
        &[
            &[
                "--model",
                "z3816a",
                "--board",
                board.to_str().expect("a path"),
                "--gps-start",
                "2026-10-22T12:00:00Z",
                "--script",
                script.0.to_str().expect("a path"),
                "--stop-after",
                limit,
            ],
            options,
        ]
        .concat(),
    );
    let taken = trace
        .iter()
        .filter(|line| line.starts_with("smartclock-emu: script "))
        .count();
    assert_eq!(taken, session.lines().count() - 1, "{name}: every step");
    assert_eq!(reset_causes(&trace), ["PowerOn"], "{name}: no reset");
    String::from_utf8_lossy(&transcript).into_owned()
}

/// The boot's answers: the image's revision and the synthetic EEPROM's
/// serial number, the engine, the self-test's `failures`, the event
/// log's first entry and the loop's `state`.
fn assert_boot(transcript: &str, failures: &str, state: &str) {
    assert_eq!(
        answer(transcript, "*IDN?"),
        "HEWLETT-PACKARD,Z3815A,0000A00000,U-4010.0-A"
    );
    assert!(
        answer(transcript, ":DIAGnostic:IDENtification:GPSystem?")
            .contains("\"MODEL # FURUNO GT-74\""),
        "{transcript}"
    );
    assert_eq!(answer(transcript, ":DIAGnostic:TEST:RESult?"), failures);
    assert_eq!(
        answer(transcript, ":DIAGnostic:LOG:READ? 1"),
        FIRST_LOG_ENTRY
    );
    assert_eq!(answer(transcript, ":SYNChronization:STATe?"), state);
}

/// `:ALARm:HARDware?`'s word in `transcript`, its PAL bit clear.
fn assert_no_pal_alarm(transcript: &str) {
    let word: u16 = answer(transcript, ":ALARm:HARDware?")
        .parse()
        .expect("a word");
    assert_eq!(word & PAL_ALARM, 0, "PAL Fail: {word}");
}

#[test]
fn the_z3815a_boots_on_its_furuno_and_with_no_design_stays_in_power_up() {
    let transcript = run(
        "boot-z3815a-unmodeled",
        SESSION,
        LIMIT,
        &["--fpga-design", "unmodeled"],
    );
    assert_boot(&transcript, UNMODELED_FAILURES, "POW");
    assert_no_pal_alarm(&transcript);
}

#[test]
#[ignore = "sixty seconds of the board on the LCA model: a minute, optimized"]
fn the_z3815a_boots_on_its_bitstreams_netlist() {
    let transcript = run("boot-z3815a-lca", SESSION, LIMIT, &[]);
    assert_boot(&transcript, LCA_FAILURES, "POW");
    assert_no_pal_alarm(&transcript);
}

/// The board on its bitstream's netlist, the LCA model fast-forwarded
/// throughout, its puck warm at once: every power-up test passes, the
/// loop locks, the event log reads back, and no hardware alarm stands
/// nor heads the status screen.
#[test]
#[ignore = "300 s of the board on the LCA model: minutes, optimized"]
fn the_z3815a_locks_on_its_bitstreams_netlist() {
    let transcript = run("long-z3815a-lca", LOCK_SESSION, LOCK_LIMIT, &[]);
    assert_eq!(
        answer(&transcript, ":DIAGnostic:TEST:RESult?"),
        LCA_FAILURES
    );
    assert_eq!(answer(&transcript, ":SYNChronization:STATe?"), "LOCK");
    assert_eq!(
        answer(&transcript, ":DIAGnostic:LOG:READ? 1"),
        FIRST_LOG_ENTRY
    );
    assert_eq!(answer(&transcript, ":ALARm:HARDware?"), NO_HARDWARE_ALARM);
    let status = answer_lines(&transcript, ":SYSTem:STATus?");
    let heading = status
        .iter()
        .find(|line| line.contains(" Status ---"))
        .unwrap_or_else(|| panic!("no heading: {status:?}"));
    assert!(!heading.starts_with(ALARM_CARD), "{heading}");
}
