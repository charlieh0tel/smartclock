//! The host port on standard input and output, the default
//! (docs/emulator.md, "Running it"): a pipe's bytes go to the port once
//! the firmware has given its prompt, the port's answer comes back on
//! standard output and nothing else does, and the run stops by itself
//! once input has ended and the port has gone idle; with
//! `--deterministic` it repeats.  On the default core, so only with
//! `mc683xx`.

#![cfg(feature = "mc683xx")]

mod common;

use std::io::Write;
use std::process::Command;
use std::process::Stdio;

use common::CPU_HZ;
use common::Scratch;
use common::stopped as stopped_fields;

/// Board seconds the run may take at most, should it not stop itself.
const LIMIT: &str = "120";

/// The emulator with `input` piped to its standard input: its standard
/// output and standard error.
fn piped(name: &str, input: &[u8]) -> (Vec<u8>, String) {
    let eeprom = Scratch::new(name);
    let mut child = Command::new(env!("CARGO_BIN_EXE_smartclock-emu"))
        .arg(common::image())
        .arg("--eeprom")
        .arg(&eeprom.0)
        .arg("--eeprom-dump")
        .arg(common::dump())
        .args([
            "--sci",
            "127.0.0.1:0",
            "--gps",
            "127.0.0.1:0",
            "--deterministic",
            "--stop-after",
            LIMIT,
        ])
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .spawn()
        .expect("the emulator starts");
    child
        .stdin
        .take()
        .expect("its standard input")
        .write_all(input)
        .expect("the input written");
    let output = child.wait_with_output().expect("it exits");
    assert!(output.status.success(), "{}", output.status);
    let errors = String::from_utf8_lossy(&output.stderr)
        .lines()
        .filter(|line| !line.contains(" port on tcp://"))
        .collect::<Vec<_>>()
        .join("\n");
    (output.stdout, errors)
}

#[test]
fn a_piped_query_is_answered_and_the_run_ends_by_itself() {
    let (output, errors) = piped("stdio-1", b"*IDN?\n");
    let text = String::from_utf8_lossy(&output);
    assert!(
        text.contains("HEWLETT-PACKARD,Z3805A,3625A01487,3543B-A"),
        "{text}"
    );
    assert!(
        !text.contains("smartclock-emu:"),
        "only the port on standard output: {text}"
    );
    let stopped = errors.lines().last().expect("a trace");
    let cycle: u64 = stopped_fields(stopped)
        .and_then(|fields| fields.strip_prefix("cycle="))
        .and_then(|rest| rest.split(' ').next())
        .and_then(|cycle| cycle.parse().ok())
        .unwrap_or_else(|| panic!("{stopped}"));
    let limit: u64 = LIMIT.parse::<u64>().expect("seconds") * CPU_HZ;
    // a stop at the limit lands a few hundred cycles under it, the
    // clock slow before the reset code programs SYNCR, so a stop by
    // itself is one more than a second short of it
    assert!(
        cycle + CPU_HZ < limit,
        "stopped by itself, not the limit: {stopped}"
    );
    let (again, errors_again) = piped("stdio-2", b"*IDN?\n");
    assert!(output == again, "the outputs differ");
    assert!(errors == errors_again, "the traces differ");
}
