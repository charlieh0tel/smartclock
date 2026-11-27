//! The 58503B 1.01.04's own firmware, booted in the emulator on its
//! board file (boards/58503b.toml; docs/emulator.md, "The boards"): its
//! ACIA talks to the synthetic engine, a UT Plus Oncore as a shipped
//! 58503B reports, or with `--gps-engine b4121` a VP Oncore, its FPGA
//! runs its own bitstream's netlist on the LCA model, and its EEPROM starts from a 58503B's,
//! its serial number and position replaced
//! (`tools/firmware/scrubbed_eeprom.py`).  It reaches its SCPI prompt on
//! the SCI and answers there, with the EEPROM's position, passes its
//! power-up self-test, loads the FPGA again as it boots, and the loop
//! locks to the
//! synthetic engine's 1 PPS, on the oscillator the EEPROM's calibration
//! describes, and from the synthetic EEPROM on the oscillator its
//! defaults do.  The boot's first seconds are quick on the
//! LCA model; once the GPS 1 PPS runs its counter it is far slower than
//! the hand models, so the lock is ignored but when asked, in release.
//! Only with `mc683xx`.

#![cfg(feature = "mc683xx")]

mod common;

use std::path::Path;

use common::HP58503B;
use common::HP58503B_SYNTHETIC;
use common::Output;
use common::Receiver;
use common::Scratch;
use common::answer;
use common::assert_stopped;
use common::run_receiver_with_engine;
use common::script;
use common::unechoed_answers;

/// The synthetic engine's UTC at power-up, fixed, so that the run
/// repeats (docs/emulator.md, "Scripts").
const GPS_START: &str = "2026-10-22T12:00:00Z";

/// The trace's line once the configuration has picked the 58503B's
/// netlist.
const RUNNING: &str = "smartclock-emu: fpga: the LCA model runs 58503b.bit's netlist";

/// The emulator run to its end on the 58503B's image, its EEPROM's
/// working file `eeprom` from `receiver`'s dump, flat out, with the
/// synthetic engine and `options`.
fn run(receiver: Receiver, eeprom: &Path, options: &[&str]) -> Output {
    run_receiver_with_engine(
        receiver,
        eeprom,
        &[&["--gps-start", GPS_START], options].concat(),
    )
}

/// The boot: the prompt, then, once the power-up self-test is done,
/// each query at its prompt.
const SESSION: &str = "# the 58503B's boot
expect scpi >\\x20 within 10
at 10
send *IDN?
expect scpi >\\x20 within 5
send :SYSTem:ERRor?
expect scpi >\\x20 within 5
send :DIAGnostic:TEST:RESult?
expect scpi >\\x20 within 5
at 30
send :PTIMe:GPSystem:POSition?
expect scpi >\\x20 within 5
stop
";

/// The position the EEPROM holds, HP Santa Clara Division's, as
/// `:PTIMe:GPSystem:POSition?` gives it: the ellipsoid height, 55 m MSL
/// plus the synthetic engine's geoid separation, as the 58503B keeps
/// it (docs/firmware/gps.md, "The 58503B's height datum").  It answers
/// once the engine has reported, so it is asked late.
const POSITION: &str = "N,+37,+19,+3.21000E+001,W,+121,+59,+5.22000E+001,+2.65900E+001";

/// The power-up tests' failures, a bit a test (the Z3816A's table,
/// `boot_z3816a.rs`): none (docs/emulator.md, "The boards").
const FAILURES: u32 = 0;

/// The VP Oncore in place of the board file's UT Plus
/// (docs/firmware/gps.md, "The UT Plus").
const VP: [&str; 2] = ["--gps-engine", "b4121"];

/// The boot, with `options`: the prompt, the answers and the netlist.
fn assert_boots(name: &str, options: &[&str]) {
    let eeprom = Scratch::new(name);
    let script = script(name, SESSION);
    let path = script.0.to_str().expect("a path");
    let (transcript, trace) = run(
        HP58503B,
        &eeprom.0,
        &[&["--script", path, "--stop-after", "35"], options].concat(),
    );
    let transcript = String::from_utf8_lossy(&transcript);

    // the image's revision and the EEPROM's serial number and suffix,
    // no error, the self-test's result and the EEPROM's position, then
    // the last prompt
    let result = format!("+{FAILURES},ALL");
    assert_eq!(
        unechoed_answers(&transcript),
        [
            "HEWLETT-PACKARD,58503B,L00000,1.01.04-D",
            "+0,\"No error\"",
            &result,
            POSITION,
            "",
        ],
        "{transcript}"
    );
    assert!(trace.iter().any(|line| line == RUNNING), "its own netlist");
    assert_stopped(&trace);
}

#[test]
fn the_58503b_boots_to_its_prompt_and_answers() {
    assert_boots("boot-58503b", &[]);
}

#[test]
fn the_58503b_boots_on_a_vp_oncore() {
    assert_boots("boot-58503b-vp", &VP);
}

/// The boot's first 3 s, in which the image loads the FPGA twice, its
/// loader (0x31d68) called from 0x31d4a and again from 0x28b8e.
const RELOAD_SESSION: &str = "# the boot's two loads
at 3
stop
";

/// The second load drives DONE Low with PROGRAM (0x31da6, 0x31dae),
/// which clears the configuration (XC3000 Figure 20), so that the
/// configuration picks the netlist again, as the first did.
#[test]
fn a_second_load_configures_the_fpga_again() {
    let name = "reload-58503b";
    let eeprom = Scratch::new(name);
    let script = script(name, RELOAD_SESSION);
    let path = script.0.to_str().expect("a path");
    let (_, trace) = run(
        HP58503B,
        &eeprom.0,
        &["--script", path, "--stop-after", "10"],
    );
    let configured = trace.iter().filter(|line| *line == RUNNING).count();
    assert_eq!(configured, 2, "configured at each load");
    assert_stopped(&trace);
}

/// The lock: the warm-up skipped, the loop's state asked as it goes
/// through its stages, and the GPS Lock lamp's flag with it.
const LOCK_SESSION: &str = "# the 58503B's lock
expect scpi >\\x20 within 10
at 15
event skip-warm-up
at 300
send :SYNChronization:STATe?
send :LED:GPSLock?
send :SYNChronization:TINTerval?
at 302
stop
";

/// The queries `LOCK_SESSION` sends, in order.
const LOCK_QUERIES: [&str; 3] = [
    ":SYNChronization:STATe?",
    ":LED:GPSLock?",
    ":SYNChronization:TINTerval?",
];

/// The largest TI the locked loop may show, seconds.
const LOCKED_TI: f64 = 1e-7;

/// The oscillator the synthetic EEPROM's defaults describe, oscillator
/// type 0's G rather than the board's, which is the dump's
/// (docs/emulator.md, "The boards"); its offset is the EEPROM's, none,
/// its lifetime record erased (docs/emulator.md, "The oscillator the
/// EEPROM describes").
const SYNTHETIC_OSCILLATOR: [&str; 1] = ["--ocxo-efc-gain=-1.25e-12"];

/// The loop locked, its lamp lit and its TI pulled in, from
/// `receiver`'s EEPROM, with `options`.
fn assert_locks(receiver: Receiver, name: &str, options: &[&str]) {
    let eeprom = Scratch::new(name);
    let script = script(name, LOCK_SESSION);
    let path = script.0.to_str().expect("a path");
    let (transcript, trace) = run(
        receiver,
        &eeprom.0,
        &[
            &["--script", path, "--stop-after", "320", "--indicators"],
            options,
        ]
        .concat(),
    );
    let transcript = String::from_utf8_lossy(&transcript);
    let answers: Vec<&str> = if receiver.echoes() {
        LOCK_QUERIES
            .iter()
            .map(|query| answer(&transcript, query))
            .collect()
    } else {
        unechoed_answers(&transcript)
    };
    let [state, lamp, ti, ..] = answers.as_slice() else {
        panic!("three answers: {transcript}");
    };

    assert_eq!(*state, "LOCK", "{transcript}");
    assert_eq!(*lamp, "1");
    let ti: f64 = ti.parse().expect("a TI");
    assert!(ti.abs() < LOCKED_TI, "{ti}");
    // the lamp test lights it at power-up too: the last change is the
    // loop's
    let gps_lock = trace
        .iter()
        .rfind(|line| line.contains(" bank=lamp name=gps_lock "))
        .expect("the GPS Lock lamp's changes");
    assert!(gps_lock.ends_with(" lit=1"), "{gps_lock}");
    assert_stopped(&trace);
}

#[test]
#[ignore = "300 s of board time on the LCA model: about two minutes of host time, release"]
fn the_58503b_locks_to_the_synthetic_engine() {
    assert_locks(HP58503B, "lock-58503b", &[]);
}

#[test]
#[ignore = "300 s of board time on the LCA model: about two minutes of host time, release"]
fn the_58503b_locks_from_its_synthetic_eeprom() {
    assert_locks(
        HP58503B_SYNTHETIC,
        "lock-58503b-synthetic",
        &SYNTHETIC_OSCILLATOR,
    );
}

#[test]
#[ignore = "300 s of board time on the LCA model: about two minutes of host time, release"]
fn the_58503b_locks_to_a_vp_oncore() {
    assert_locks(HP58503B, "lock-58503b-vp", &VP);
}

#[test]
#[ignore = "300 s of board time on the LCA model: about two minutes of host time, release"]
fn the_58503b_locks_from_its_synthetic_eeprom_on_a_vp_oncore() {
    assert_locks(
        HP58503B_SYNTHETIC,
        "lock-58503b-synthetic-vp",
        &[&SYNTHETIC_OSCILLATOR[..], &VP].concat(),
    );
}
