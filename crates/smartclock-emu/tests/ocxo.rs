//! The loop pulls the oscillator in from an offset (docs/emulator.md,
//! "The oscillator"): booted 1e-8 fast at the EFC's middle with the
//! synthetic engine, the firmware locks and steers its EFC to where the
//! oscillator is on frequency, 16000 counts of 6.25e-13 below the
//! middle.  On the default core, so only with `mc683xx`.

#![cfg(feature = "mc683xx")]

mod common;

use common::Scratch;
use common::run_with_engine;

/// The oscillator's offset at the EFC's middle.
const OFFSET: f64 = 1e-8;
/// The firmware's G, an EFC count, and the EFC's middle, in counts.
const GAIN: f64 = 6.25e-13;
const MIDDLE: f64 = 524_288.0;
/// When the script reads the loop's state, board seconds: well past
/// the warm-up's 480 s and the lock that follows.
const READ_AT: u32 = 1500;
/// The residual frequency the lock must leave, at most.
const RESIDUAL: f64 = 1e-10;

/// A script reading the loop's state and EFC at `READ_AT`, each
/// answer awaited past its query's echo, since the port may have given
/// more than one prompt by then.
fn script() -> String {
    format!(
        "at 5
send
expect >\\x20
at {READ_AT}
send :SYNChronization:STATe?
expect STATe?
expect >\\x20
send :DIAGnostic:ROSCillator:EFControl:ABSolute?
expect ABSolute?
expect >\\x20
stop
"
    )
}

/// The line after the echo of `query` in `transcript`.
fn answer<'a>(transcript: &'a str, query: &str) -> &'a str {
    let after = transcript
        .split(query)
        .nth(1)
        .unwrap_or_else(|| panic!("{query} sent: {transcript}"));
    after.trim_start().lines().next().unwrap_or("").trim()
}

#[test]
#[ignore = "9 to 14 minutes of host time in a debug build"]
fn the_loop_pulls_in_an_oscillator_off_frequency() {
    let eeprom = Scratch::new("ocxo");
    let path = Scratch::new("ocxo-script");
    std::fs::write(&path.0, script()).expect("the script written");
    let offset = OFFSET.to_string();
    let limit = (READ_AT + 100).to_string();
    let (transcript, _) = run_with_engine(
        &eeprom.0,
        &[
            "--script",
            path.0.to_str().expect("a path"),
            "--stop-after",
            &limit,
            "--ocxo-offset",
            &offset,
            "--gps-start",
            "2026-10-23T00:00:00Z",
        ],
    );
    let transcript = String::from_utf8_lossy(&transcript);
    assert_eq!(
        answer(&transcript, ":SYNChronization:STATe?"),
        "LOCK",
        "{transcript}"
    );
    let efc: f64 = answer(&transcript, ":DIAGnostic:ROSCillator:EFControl:ABSolute?")
        .trim_start_matches('+')
        .parse()
        .expect("an EFC");
    let residual = OFFSET + GAIN * (efc - MIDDLE);
    assert!(
        residual.abs() < RESIDUAL,
        "EFC {efc}, residual {residual:e}"
    );
}
