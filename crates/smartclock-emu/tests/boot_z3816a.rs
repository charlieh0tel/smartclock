//! The Z3816A 4001's own firmware, booted in the emulator on the
//! Z3816A's board (docs/emulator.md, "The boards"), its EEPROM the
//! synthetic image.  The SCI is its one port, and at power-up it sends
//! the continuous time code, a binary frame a second, and drops every
//! other write to the port while it does, though it echoes and parses
//! what comes in: the setting is not the EEPROM's but the image's
//! initialized data, on at every power-up (docs/firmware/console.md,
//! "Which port").  So the session first turns the time code off, and
//! from then the port gives the prompt and the answers.  On the board file's FPGA,
//! the LCA model running the bitstream's netlist, the boot is slow, so
//! ignored but when asked; with `--fpga-design unmodeled`, the part
//! running no design, the firmware finds the FPGA dead, but the port
//! answers all the same, on every `cargo test`.  The session is a
//! script under `--deterministic`.  With `--time-code-off` the emulator
//! sends the command itself, and a session without it answers the
//! same; without the option and the command the port gives no prompt.
//! A last test, ignored, runs the
//! board with `--skip-warm-up` on the LCA model to its lock.  Only with `mc683xx`.

#![cfg(feature = "mc683xx")]

mod common;

use common::Scratch;
use common::Z3816A;
use common::run_receiver_with_engine;
use common::script;

/// The session: the time code turned off once the SCPI task listens,
/// then each query at its prompt; the self-test's result once the
/// power-up tests have run.
const SESSION: &str = "# the Z3816A's boot
at 25
send :PTIMe:TCODe:CONTinuous OFF
expect scpi >\\x20 within 10
send *IDN?
expect scpi >\\x20 within 5
send :SYSTem:ERRor?
expect scpi >\\x20 within 5
at 40
send :DIAGnostic:TEST:RESult?
expect scpi >\\x20 within 30
stop
";

/// The session with `--time-code-off`: the same queries, the time code
/// left to the emulator, the first prompt waited for.
const OFF_SESSION: &str = "# the Z3816A's boot, its time code turned off by the emulator
expect scpi >\\x20 within 40
send *IDN?
expect scpi >\\x20 within 5
send :SYSTem:ERRor?
expect scpi >\\x20 within 5
at 40
send :DIAGnostic:TEST:RESult?
expect scpi >\\x20 within 30
stop
";

/// A session that neither sends the command nor asks the emulator to:
/// a query once the port would have answered, unanswered.
const TIME_CODE_SESSION: &str = "# the Z3816A's boot, its time code left on
at 25
send *IDN?
at 35
stop
";

/// The trace line of `--time-code-off`'s command sent.
const TIME_CODE_OFF_SENT: &str = "smartclock-emu: time-code-off ";

/// The longest a run may take, board seconds, should a step wait for
/// good.
const LIMIT: &str = "90";

/// The prompt, from which on the port sends only the session.
const PROMPT: &str = "scpi > ";

/// What the port sends from its first prompt: the answers, the image's
/// revision and the synthetic EEPROM's serial number, no error, and the
/// power-up tests' failures, a bit a test (`0x28888`'s table: DISP,
/// PROC, RAM, EEPROM, UART, QSPI, FPGA, INTERP, `INT_REF`, GPS, POWER).
fn transcript(failures: u32) -> String {
    format!(
        "{PROMPT}*IDN?\r\nHEWLETT-PACKARD,Z3816A,0000A00000,4001-A\r\n\
         {PROMPT}:SYSTem:ERRor?\r\n+0,\"No error\"\r\n\
         {PROMPT}:DIAGnostic:TEST:RESult?\r\n+{failures},ALL\r\n\
         {PROMPT}"
    )
}

/// The power-up tests' failures on the LCA model: none.
const LCA_FAILURES: u32 = 0;
/// The same with no design: the FPGA's, the interpolator's and its
/// reference's.
const UNMODELED_FAILURES: u32 = 448;

/// The session `steps` on the Z3816A's board with `options`: the
/// transcript and the trace, every step taken.
fn run(name: &str, steps: &str, options: &[&str]) -> (String, Vec<String>) {
    let eeprom = Scratch::new(name);
    let script = script(name, steps);
    let path = script.0.to_str().expect("a path");
    let (transcript, trace) = run_receiver_with_engine(
        Z3816A,
        &eeprom.0,
        &[
            &[
                "--gps-start",
                "2026-10-22T12:00:00Z",
                "--script",
                path,
                "--stop-after",
                LIMIT,
            ],
            options,
        ]
        .concat(),
    );
    let taken = trace
        .iter()
        .filter(|line| line.starts_with("smartclock-emu: script "))
        .count();
    assert_eq!(taken, steps_given(steps), "{name}: every step");
    (String::from_utf8_lossy(&transcript).into_owned(), trace)
}

/// How many steps `steps` gives: every line but its first, a comment.
fn steps_given(steps: &str) -> usize {
    steps.lines().count() - 1
}

/// The session `steps` with `options`: the transcript from the first
/// prompt, and how many times the emulator sent `--time-code-off`'s
/// command.
fn session_of(name: &str, steps: &str, options: &[&str]) -> (String, usize) {
    let (transcript, trace) = run(name, steps, options);
    let prompt = transcript.find(PROMPT).expect("a prompt");
    let sent = trace
        .iter()
        .filter(|line| line.starts_with(TIME_CODE_OFF_SENT))
        .count();
    (transcript[prompt..].to_owned(), sent)
}

/// `SESSION` with `options`: the transcript from the first prompt, the
/// emulator sending no command of its own.
fn session(name: &str, options: &[&str]) -> String {
    let (answered, sent) = session_of(name, SESSION, options);
    assert_eq!(sent, 0, "{name}: no --time-code-off");
    answered
}

#[test]
fn the_z3816a_answers_once_its_time_code_is_off() {
    let answered = session("boot-z3816a-unmodeled", &["--fpga-design", "unmodeled"]);
    assert_eq!(answered, transcript(UNMODELED_FAILURES));
}

/// With `--time-code-off` the emulator turns the time code off once,
/// at the power-up's first frame, and the session that does not
/// answers as the one that does.
#[test]
fn the_z3816a_answers_with_its_time_code_turned_off_by_the_emulator() {
    let (answered, sent) = session_of(
        "boot-z3816a-time-code-off",
        OFF_SESSION,
        &["--fpga-design", "unmodeled", "--time-code-off"],
    );
    assert_eq!(sent, 1);
    assert_eq!(answered, transcript(UNMODELED_FAILURES));
}

/// Without the option the port is a real unit's: no prompt and no
/// answer while the time code runs.
#[test]
fn the_z3816a_keeps_its_time_code_by_default() {
    let (transcript, trace) = run(
        "boot-z3816a-time-code",
        TIME_CODE_SESSION,
        &["--fpga-design", "unmodeled"],
    );
    assert!(!transcript.contains(PROMPT), "{transcript:?}");
    assert!(!transcript.contains("HEWLETT-PACKARD"), "{transcript:?}");
    assert!(
        !trace
            .iter()
            .any(|line| line.starts_with(TIME_CODE_OFF_SENT)),
        "no command sent"
    );
}

#[test]
#[ignore = "forty seconds of the board on the LCA model: minutes, optimized"]
fn the_z3816a_boots_on_its_bitstreams_netlist() {
    let answered = session("boot-z3816a-lca", &[]);
    assert_eq!(answered, transcript(LCA_FAILURES));
}

/// The long session: the time code off, then the loop's state and the
/// self-test's result well past where the loop's power-up stages end.
const LONG_SESSION: &str = "# the Z3816A's warm-up skipped
at 25
send :PTIMe:TCODe:CONTinuous OFF
expect scpi >\\x20 within 10
at 600
send :SYNChronization:STATe?
expect STATe? within 5
expect scpi >\\x20 within 5
send :DIAGnostic:TEST:RESult?
expect RESult? within 5
expect scpi >\\x20 within 30
stop
";

/// The long session's limit, board seconds.
const LONG_LIMIT: &str = "700";

/// The board run with its warm-up skipped on its bitstream's netlist,
/// the LCA model fast-forwarded throughout: the loop locks, its phase
/// aligned by the sync field's steps, which the board, the PLL not
/// modeled, infers from the field's writes and makes itself
/// (docs/emulator.md, "The boards"), and the self-test's result is the
/// boot's.
#[test]
#[ignore = "600 s of the board on the LCA model: minutes, optimized"]
fn the_z3816a_locks_on_its_bitstreams_netlist() {
    let name = "long-z3816a-lca";
    let eeprom = Scratch::new(name);
    let script = script(name, LONG_SESSION);
    let path = script.0.to_str().expect("a path");
    let (transcript, trace) = run_receiver_with_engine(
        Z3816A,
        &eeprom.0,
        &[
            "--gps-start",
            "2026-10-22T12:00:00Z",
            "--script",
            path,
            "--stop-after",
            LONG_LIMIT,
            "--skip-warm-up",
        ],
    );
    let steps = trace
        .iter()
        .filter(|line| line.starts_with("smartclock-emu: script "))
        .count();
    assert_eq!(steps, steps_given(LONG_SESSION), "every step");
    let transcript = String::from_utf8_lossy(&transcript);
    let answer = |query: &str| {
        transcript
            .rsplit_once(query)
            .and_then(|(_, after)| after.trim_start().lines().next())
            .map(str::trim)
            .unwrap_or_default()
            .to_owned()
    };
    assert_eq!(answer(":SYNChronization:STATe?"), "LOCK", "{transcript}");
    assert_eq!(
        answer(":DIAGnostic:TEST:RESult?"),
        format!("+{LCA_FAILURES},ALL")
    );
}
