//! The Z3801A 3543's own firmware, booted in the emulator on the
//! Z3801A's board (docs/emulator.md, "The boards"), reaches
//! its SCPI prompt on the host port, runs the GPS engine's init script
//! to its `@@Bk` and the `@@Cg` after it, the steady state the Z3805A's
//! boot reaches, and answers there; every power-up test passes with the
//! synthetic engine.  Its port does not echo, so the transcript is the
//! prompts and the answers alone.  The session is a script under `--deterministic`, so
//! a loaded host cannot move it.  On each core the build has, so only
//! with `mc683xx`, and on Unicorn too with `unicorn`.

#![cfg(feature = "mc683xx")]

mod common;

use common::CORES;
use common::Scratch;
use common::Z3801A;
use common::run_receiver_with_engine;
use common::script;

/// The session: the power-up prompt, the GPS init script's last
/// exchange answered (its `@@Bk`, then the `@@Cg` after it,
/// docs/firmware/oncore/z3801a-3543.txt, script 0x48), then each query
/// at its prompt.
const SESSION: &str = "# the Z3801A's boot
expect scpi >\\x20 within 5
expect-gps @@Bk within 20
expect-gps @@Cg within 5
send *IDN?
expect scpi >\\x20 within 5
send :SYSTem:ERRor?
expect scpi >\\x20 within 5
send :DIAGnostic:TEST:RESult?
expect scpi >\\x20 within 30
stop
";

/// The longest the run may take, board seconds, should a step wait for
/// good.
const LIMIT: &str = "60";

/// What the port sends: the prompts and the answers, the image's
/// revision and the EEPROM's serial number, no error, and every test
/// passed.
const TRANSCRIPT: &str = "scpi > HEWLETT-PACKARD,Z3801A,3542A01548,3543-A\r\n\
                          scpi > +0,\"No error\"\r\n\
                          scpi > +0,ALL\r\n\
                          scpi > ";

#[test]
fn the_z3801a_boots_to_its_steady_state_on_each_core() {
    for core in CORES {
        let eeprom = Scratch::new(&format!("boot-z3801a-{core}"));
        let script = script(&format!("boot-z3801a-{core}"), SESSION);
        let path = script.0.to_str().expect("a path");
        let (transcript, trace) = run_receiver_with_engine(
            Z3801A,
            &eeprom.0,
            &[
                "--core",
                core,
                "--gps-start",
                "2026-10-22T12:00:00Z",
                "--script",
                path,
                "--stop-after",
                LIMIT,
            ],
        );
        assert_eq!(String::from_utf8_lossy(&transcript), TRANSCRIPT, "{core}");
        let steps = trace
            .iter()
            .filter(|line| line.starts_with("smartclock-emu: script "))
            .count();
        assert_eq!(steps, SESSION.lines().count() - 1, "{core}: every step");
    }
}
