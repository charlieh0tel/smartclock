//! The emulator's tooling (docs/emulator.md, "Tracing", "Scripts"):
//! the deterministic mode runs the same boot the same way each time, to
//! the instruction, and a script the same session; the trace's records
//! are lines a script can read; and with `--indicators` the LEDs are
//! reported lit from the reset and the lamps as the power-up lamp test
//! flashes them.  On the default core, so only with `mc683xx`.

#![cfg(feature = "mc683xx")]

mod common;

use common::Emulator;
use common::RECORD_STARTS;
use common::Scratch;
use common::stopped;

/// The board's seconds each run boots for: past the lamp test.
const SECONDS: &str = "3";

/// The trace of a fresh boot, with `options`, without the lines naming
/// the ports, which the system chooses, and the board file, which the
/// checkout's path names.
fn boot_with(name: &str, options: &[&str]) -> Vec<String> {
    let eeprom = Scratch::new(name);
    let base = ["--stop-after", SECONDS, "--trace", "position,exceptions"];
    Emulator::start(&eeprom.0, &[base.as_slice(), options].concat())
        .finish()
        .into_iter()
        .filter(|line| !line.contains(" port on ") && !line.contains(" core on "))
        .collect()
}

/// The trace of a fresh boot with the indicators reported.
fn boot(name: &str) -> Vec<String> {
    boot_with(name, &["--indicators"])
}

#[test]
fn a_deterministic_boot_runs_the_same_each_time() {
    let first = boot("deterministic-1");
    let second = boot("deterministic-2");
    let last = first.last().expect("a trace");
    assert!(
        stopped(last).is_some_and(|fields| fields.contains(" instructions=")),
        "{last}"
    );
    assert!(first.iter().any(|line| line.contains(" exception cycle=")));
    assert!(first.iter().any(|line| line.contains(" position cycle=")));
    assert!(first == second, "the two runs differ");
}

#[test]
fn records_are_ascii_key_value_lines() {
    // mc683xx-board's trace opens with a comment naming its format.
    for line in boot("records")
        .into_iter()
        .filter(|line| !line.starts_with('#'))
    {
        assert!(line.is_ascii(), "{line}");
        let record = RECORD_STARTS
            .iter()
            .find_map(|start| line.strip_prefix(start))
            .unwrap_or_else(|| panic!("{line}"));
        let mut words = record.split(' ');
        let kind = words.next().expect("a kind");
        assert!(
            ["position", "exception", "indicator", "reset", "stopped"].contains(&kind),
            "{line}"
        );
        assert!(words.all(|field| field.contains('=')), "{line}");
    }
}

#[test]
fn the_indicators_are_reported_only_when_asked_for() {
    let lines = boot_with("no-indicators", &[]);
    assert!(lines.iter().all(|line| !line.contains(" indicator ")));
}

#[test]
fn the_reset_lights_every_led_until_the_reset_code_puts_them_out() {
    const LEDS: [&str; 6] = ["heartbeat", "installer", "cpu", "ram", "duart", "selftest"];
    let lines = boot("leds");
    let leds: Vec<&String> = lines
        .iter()
        .filter(|line| line.contains(" bank=led "))
        .collect();
    for (index, led) in LEDS.iter().enumerate() {
        assert!(
            leds[index].ends_with(&format!(" cycle=0 bank=led name={led} lit=1")),
            "{}",
            leds[index]
        );
        let out = leds[LEDS.len() + index];
        assert!(
            out.ends_with(&format!(" bank=led name={led} lit=0")),
            "{out}"
        );
        assert!(!out.contains(" cycle=0 "), "the reset code's write: {out}");
    }
}

#[test]
fn the_lamp_test_lights_each_lamp() {
    let lines = boot("lamps");
    for lamp in ["enabled", "active", "alarm", "gps_lock", "holdover"] {
        let lit = format!(" bank=lamp name={lamp} lit=1");
        let out = format!(" bank=lamp name={lamp} lit=0");
        assert!(lines.iter().any(|line| line.ends_with(&lit)), "{lamp} lit");
        assert!(lines.iter().any(|line| line.ends_with(&out)), "{lamp} out");
    }
}
