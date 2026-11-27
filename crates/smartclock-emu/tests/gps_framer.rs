//! An `@@Bh` from the GPS engine overruns the 58503A 3704's Oncore
//! framer (docs/firmware/gps.md, "Framing"): `Bh`'s table length is 0,
//! so the body count starts at -6 and the framer stores past
//! `gps_response_buffer` over the top of `gps_response_cursor`, and the
//! engine's next bytes point the stores at `0xff1522`, where there is
//! no chip select.  The bus error in `gpsm` ends in the watchdog's
//! restart and its record in the second log.  Only with `mc683xx`:
//! Unicorn halts at the stray store, as at any write where nothing is.

#![cfg(feature = "mc683xx")]

mod common;

use common::HP58503A;
use common::Scratch;
use common::run_receiver_with_engine;
use common::script;

/// The session: the boot's prompt, the `@@Bh` (mode 0, its checksum
/// `0x2a`) once the engine's messages are flowing, the restart's
/// prompt, then the second log.
const SESSION: &str = "# an @@Bh overruns the framer
expect scpi >\\x20 within 10
at 30
write-gps @@Bh\\x00\\x2a\\r\\n
expect scpi >\\x20 within 20
at 60
send :DIAGnostic:SLOG:READ:ALL?
expect scpi >\\x20 within 5
stop
";

/// The longest the run may take, board seconds, should a step wait for
/// good.
const LIMIT: &str = "90";

/// The second log's record of the bus error: the framer's store
/// (`0x4c238`, the PC after it), at the address the overrun made.
const EXCEPTION: &str = "Exception: gpsm, vector C008, PC = 0004C23A, SR = 0000, addr = 00FF1522";

#[test]
fn a_bh_from_the_engine_overruns_the_58503a_framer_into_a_restart() {
    let eeprom = Scratch::new("gps-framer");
    let script = script("gps-framer", SESSION);
    let path = script.0.to_str().expect("a path");
    let (transcript, trace) = run_receiver_with_engine(
        HP58503A,
        &eeprom.0,
        &[
            "--core",
            "mc683xx",
            "--gps-start",
            "2026-10-27T00:00:00Z",
            "--script",
            path,
            "--stop-after",
            LIMIT,
        ],
    );
    let transcript = String::from_utf8_lossy(&transcript);
    assert!(transcript.contains(EXCEPTION), "{transcript}");
    assert!(
        trace
            .iter()
            .any(|line| line.starts_with("smartclock-emu: reset ")
                && line.ends_with("cause=Watchdog")),
        "the watchdog's restart"
    );
}
