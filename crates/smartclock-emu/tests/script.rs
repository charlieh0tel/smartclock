//! A script on the host port (docs/emulator.md, "Scripts"): its input
//! timed by the board's clock, a deterministic session replays the
//! same each time, to the cycle and the instruction, transcript and
//! trace alike.  On the default core, so only with `mc683xx`.

#![cfg(feature = "mc683xx")]

mod common;

use common::Scratch;
use common::assert_stopped;
use common::run;
use common::script;

/// The session: once the firmware has booted, a line to get its
/// prompt, then `*IDN?` and `:SYSTem:ERRor?`, each sent at its prompt.
const SESSION: &str = "# a session at the prompt
at 5
send
expect scpi >\\x20
send *IDN?
expect HEWLETT-PACKARD,Z3805A
expect scpi >\\x20
after 0.5
send :SYSTem:ERRor?
expect scpi >\\x20
stop
";

/// The longest the session may take, board seconds, should a step wait
/// for good.
const LIMIT: &str = "120";

/// The transcript and the trace of a fresh run of `SESSION`, without
/// the lines naming the ports, which the system chooses.
fn session(name: &str) -> (String, Vec<String>) {
    let eeprom = Scratch::new(name);
    let script = script(name, SESSION);
    let path = script.0.to_str().expect("a path");
    let (transcript, trace) = run(
        &eeprom.0,
        &[
            "--script",
            path,
            "--stop-after",
            LIMIT,
            "--trace",
            "exceptions",
        ],
    );
    let trace = trace
        .into_iter()
        .filter(|line| !line.contains(" port on "))
        .collect();
    (String::from_utf8_lossy(&transcript).into_owned(), trace)
}

#[test]
fn a_scripted_session_replays_the_same_each_time() {
    let (transcript, trace) = session("script-1");
    assert!(
        transcript.contains("HEWLETT-PACKARD,Z3805A,3625A01487,3543B-A"),
        "{transcript}"
    );
    assert!(transcript.contains("+0,\"No error\""), "{transcript}");
    let steps: Vec<&String> = trace
        .iter()
        .filter(|line| line.starts_with("smartclock-emu: script "))
        .collect();
    assert_eq!(steps.len(), 10, "every step taken: {steps:?}");
    assert!(steps[9].ends_with(" line=11 step=stop"), "{}", steps[9]);
    assert_stopped(&trace);
    let (again, trace_again) = session("script-2");
    assert!(transcript == again, "the transcripts differ");
    assert!(trace == trace_again, "the traces differ");
}
