//! The Z3805A 1.01.03's own firmware, booted in the emulator on its
//! board file, `boards/z3805a-10103.toml`, which its revision picks:
//! the Z3805A's board with the synthetic engine a Furuno GT-74
//! (docs/emulator.md, "The boards", "The synthetic GPS engine"), its
//! EEPROM the synthetic image, there being no dump of one in the
//! repository.  The image commands its engine in `$PFEC` sentences,
//! `int,acc00` among them, which the engine answers with `acc`.  It
//! reaches its SCPI prompt on DUART channel B configured, names the
//! engine a GT-74 and passes every power-up test.  Its event log reads
//! back from ten seconds after power-on, with no reset, its first entry
//! the synthetic EEPROM's "Log cleared".  The engine sends a
//! second's sentences half a second after the pulse before it.  A
//! second test, ignored, runs the board to its lock.  The session is a
//! script under `--deterministic`.  Only with `mc683xx`.

#![cfg(feature = "mc683xx")]

mod common;

use std::path::Path;

use common::Scratch;
use common::Z3805A_10103;
use common::answer;
use common::reset_causes;
use common::run_receiver_with_engine;
use common::script;

/// The session: the image's request for `acc`, its first prompt, then
/// each query at its prompt once the power-up tests have run.
const SESSION: &str = "# the Z3805A 1.01.03's boot
expect-gps $PFEC,GPint,acc00 within 30
expect scpi >\\x20 within 30
at 10
send :DIAGnostic:LOG:READ? 1
expect scpi >\\x20 within 5
at 40
send *IDN?
expect scpi >\\x20 within 5
send :DIAGnostic:IDENtification:GPSystem?
expect scpi >\\x20 within 5
send :DIAGnostic:TEST:RESult?
expect scpi >\\x20 within 30
stop
";

/// The longest a run may take, board seconds, should a step wait for
/// good.
const LIMIT: &str = "90";

/// The image's revision and the synthetic EEPROM's serial number.
const IDN: &str = "SYMMETRICOM,Z3805A,0000A00000,1.01.03-A";
/// The engine as the image names it from `tst`: the synthetic GT-74's
/// made-up software, 4850120 version 1 (`furuno::GT_74`), which the
/// image takes for a GT-74 (docs/firmware/z3805a-1.01.03.md, "The GPS
/// engine").
const GPS_IDENTITY: &str = "\"--\",\"SFTW P/N # 4850120\",\"SOFTWARE VER # 001\",\
                            \"--\",\"--\",\"MODEL # FURUNO GT-74\",\"--\",\"--\",\"--\",\"--\"";
/// The power-up tests' failures from the synthetic EEPROM: none (the
/// table at `0x18b5c`: DISP, PROC, RAM, EEPROM, UART, QSPI, FPGA,
/// INTERP, `INT_REF`, GPS, POWER).
const NO_FAILURES: &str = "+0,ALL";
/// The event log's first entry: the synthetic EEPROM's, which
/// `log_erase` writes at an erased part's first power-up
/// (`tools/firmware/synthetic_eeprom.py`).
const FIRST_LOG_ENTRY: &str = "\"Log 001:19940101.00:00:00: Log cleared\"";

/// The session `steps` with `options`: the transcript and the trace,
/// every step taken, the board never reset past its power-on.
fn run(name: &str, steps: &str, limit: &str, options: &[&str]) -> (String, Vec<String>) {
    let eeprom = Scratch::new(name);
    let script = script(name, steps);
    let path = script.0.to_str().expect("a path");
    let (transcript, trace) = run_receiver_with_engine(
        Z3805A_10103,
        &eeprom.0,
        &[
            &[
                "--gps-start",
                "2026-10-22T12:00:00Z",
                "--script",
                path,
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
    assert_eq!(taken, steps.lines().count() - 1, "{name}: every step");
    assert_eq!(reset_causes(&trace), ["PowerOn"], "{name}: no reset");
    (String::from_utf8_lossy(&transcript).into_owned(), trace)
}

#[test]
fn the_z3805a_10103_boots_on_its_furuno() {
    let (transcript, trace) = run("boot-z3805a-10103", SESSION, LIMIT, &["--tap"]);
    assert!(
        trace
            .iter()
            .filter_map(|line| line.strip_prefix("smartclock-emu: mc683xx core on "))
            .any(|board| Path::new(board).ends_with("boards/z3805a-10103.toml")),
        "its own board file: {:?}",
        trace.first()
    );
    assert_eq!(answer(&transcript, "*IDN?"), IDN);
    assert_eq!(
        answer(&transcript, ":DIAGnostic:IDENtification:GPSystem?"),
        GPS_IDENTITY
    );
    assert_eq!(answer(&transcript, ":DIAGnostic:TEST:RESult?"), NO_FAILURES);
    assert_eq!(
        answer(&transcript, ":DIAGnostic:LOG:READ? 1"),
        FIRST_LOG_ENTRY
    );
    // the engine answers `int,acc00` with `acc`, beside GGA and tps
    let sent = |prefix: &str| {
        trace
            .iter()
            .filter(|line| line.contains(&format!("gps > {prefix}")))
            .count()
    };
    assert!(sent("$PFEC,GPtps") > 0 && sent("$GPGGA") > 0);
    assert!(sent("$PFEC,GPacc") > 0);
}

/// The long session: the loop's state well past its power-up stages,
/// then the event log.
const LOCK_SESSION: &str = "# the Z3805A 1.01.03 to its lock
expect scpi >\\x20 within 60
at 300
send :SYNChronization:STATe?
expect scpi >\\x20 within 5
send :DIAGnostic:LOG:READ? 1
expect scpi >\\x20 within 5
send :DIAGnostic:TEST:RESult?
expect scpi >\\x20 within 30
stop
";

/// The board run past its power-up from the synthetic EEPROM, warm-up
/// and all: the loop locks to the GT-74's pulse.
#[test]
#[ignore = "300 s of the board: a minute or more, optimized"]
fn the_z3805a_10103_locks_on_its_furuno() {
    let (transcript, _) = run("long-z3805a-10103", LOCK_SESSION, "400", &[]);
    assert_eq!(answer(&transcript, ":SYNChronization:STATe?"), "LOCK");
    assert_eq!(
        answer(&transcript, ":DIAGnostic:LOG:READ? 1"),
        FIRST_LOG_ENTRY
    );
    assert_eq!(answer(&transcript, ":DIAGnostic:TEST:RESult?"), NO_FAILURES);
}
