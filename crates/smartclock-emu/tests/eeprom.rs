//! A setting the firmware saves to its NV register, in the EEPROM,
//! reaches the working file at once and is there after the emulator is
//! killed and started again; and the synthetic EEPROMs' records, the
//! cleared reset records and event log among them, are what each
//! image's own first power-up on an erased part writes
//! (docs/emulator.md, "The EEPROM").  On the default core, so only with
//! `mc683xx`.

#![cfg(feature = "mc683xx")]

mod common;

use std::time::Duration;
use std::time::Instant;

use common::Emulator;
use common::HP58503B_SYNTHETIC;
use common::Receiver;
use common::Scratch;
use common::Z3805A_10103;
use common::Z3815A;
use common::Z3816A;
use common::repository;
use common::run_receiver_erased;
use smartclock::session::Session;
use smartclock::transport::Transport;

/// A setting `nv_save` keeps, harmless to change: the holdover
/// duration past which the status reports it, 1000 to 3888000 s
/// (structures.md, "Numeric SCPI parameters" and "The NV register"),
/// `:SOURce:ROSCillator:HOLDover:DURation:STATus:THReshold`.
const THRESHOLD: &str = ":SOURce:ROSCillator:HOLDover:DURation:STATus:THReshold";

/// The NV register's two slots in the EEPROM (structures.md, "The NV
/// register"): `0x4000c0`, two records of `0x80` bytes.
const NV_RECORDS: std::ops::Range<usize> = 0xc0..0x1c0;

/// The threshold set: twice the dump's 86400.
const WANTED: f64 = 172_800.0;

/// How long the firmware may take to write its record through.
const SAVE_LIMIT: Duration = Duration::from_secs(30);

/// The threshold, read over `session`.
fn threshold(session: &mut Session<Box<dyn Transport + Send>>) -> f64 {
    let reply = session
        .query(&format!("{THRESHOLD}?"))
        .expect("the threshold answers");
    let text = reply.one_line("a threshold").expect("one line");
    text.trim()
        .parse()
        .unwrap_or_else(|error| panic!("{text:?}: {error}"))
}

#[test]
fn a_saved_setting_survives_a_kill_and_a_restart() {
    let eeprom = Scratch::new("persist");
    let dump = std::fs::read(common::dump()).expect("the dump");

    let first = Emulator::start_past_self_test(&eeprom.0, &[]);
    let mut session = first.session();
    let unsaved = std::fs::read(&eeprom.0).expect("the working file");
    assert_ne!(
        threshold(&mut session),
        WANTED,
        "the dump's own, to be changed"
    );
    session
        .query(&format!("{THRESHOLD} {WANTED}"))
        .expect("the threshold is set");
    let started = Instant::now();
    while std::fs::read(&eeprom.0).expect("the working file")[NV_RECORDS] == unsaved[NV_RECORDS] {
        assert!(
            started.elapsed() < SAVE_LIMIT,
            "the NV register not written through within {SAVE_LIMIT:?}"
        );
        std::thread::sleep(Duration::from_millis(100));
    }
    // as a power cut would stop it, a moment after the record changed
    std::thread::sleep(Duration::from_millis(500));
    first.kill();

    let held = std::fs::read(&eeprom.0).expect("the working file");
    assert_eq!(held.len(), dump.len(), "whole");

    let second = Emulator::start_past_self_test(&eeprom.0, &[]);
    let mut session = second.session();
    assert_eq!(threshold(&mut session), WANTED, "kept across the restart");
    let error = session
        .query(":SYSTem:ERRor?")
        .expect(":SYSTem:ERRor? answers");
    assert_eq!(
        error.one_line("an error").expect("one line"),
        "+0,\"No error\"",
        "the configuration memory not lost"
    );
}

/// The board time an erased part's first power-up has written its
/// records by, seconds: `nv_restore` saves the defaults in
/// `system_init`, before the tasks start.
const FIRST_POWER_UP: &str = "3";

/// The NV register's slot 0 in the second family's EEPROM, 160 bytes
/// (docs/firmware/58503a-3704.md, "EEPROM"), which `nv_restore` saves
/// the defaults to on an erased part.
const NV_SLOT_0: std::ops::Range<usize> = 0xc0..0x160;

/// The parameter record, which the 58503B's `parameter_record_read`
/// writes back with the SCI's defaults when it is bad (`0x40f64`), as
/// the Z3815A's does (`0x4528e`).
const PARAMETER_RECORD: std::ops::Range<usize> = 0x00..0x10;

/// The reset records, 32 of `0x5c` bytes from `0x400200`, which the
/// first power-up's `log_clear` leaves holding "Log cleared" in the
/// first (structures.md, "EEPROM records").
const RESET_RECORDS: std::ops::Range<usize> = 0x200..0xd80;

/// The first entry of the event log's page 0, from `0x400d80` past its
/// write count and checksum: the first power-up's `log_erase`'s "Log
/// cleared", past which the power-up logs on and writes the page again.
const LOG_FIRST_ENTRY: std::ops::Range<usize> = 0xd84..0xd98;

/// The rest of the event log's pages, to the part's end, each cleared.
const LOG_PAGES_PAST_0: std::ops::RangeFrom<usize> = 0xdc0..;

/// Checks the reset records and the event log `written` at the first
/// power-up are the synthetic image's, but for the entries it logs
/// past "Log cleared" and page 0's count and checksum.
fn assert_logs_cleared(written: &[u8], synthetic: &[u8]) {
    assert_eq!(written[RESET_RECORDS], synthetic[RESET_RECORDS]);
    assert_eq!(written[LOG_FIRST_ENTRY], synthetic[LOG_FIRST_ENTRY]);
    assert_eq!(written[LOG_PAGES_PAST_0], synthetic[LOG_PAGES_PAST_0]);
}

/// `receiver`'s EEPROM after its first power-up from erased, with
/// `options`, and its synthetic image.
fn first_power_up(receiver: Receiver, name: &str, options: &[&str]) -> (Vec<u8>, Vec<u8>) {
    let eeprom = Scratch::new(name);
    run_receiver_erased(
        receiver,
        &eeprom.0,
        &[&["--stop-after", FIRST_POWER_UP], options].concat(),
    );
    let written = std::fs::read(&eeprom.0).expect("the working file");
    let synthetic = std::fs::read(receiver.dump()).expect("the synthetic image");
    (written, synthetic)
}

#[test]
fn the_z3816a_saves_its_synthetic_eeproms_settings() {
    let (written, synthetic) =
        first_power_up(Z3816A, "first-z3816a", &["--fpga-design", "unmodeled"]);
    assert_eq!(written[NV_SLOT_0], synthetic[NV_SLOT_0]);
    assert_logs_cleared(&written, &synthetic);
}

/// The Z3815A's, on its board file with the Z3816A's `--model`.
#[test]
fn the_z3815a_saves_its_synthetic_eeproms_records() {
    let board = repository("boards/z3815a.toml");
    let (written, synthetic) = first_power_up(
        Z3815A,
        "first-z3815a",
        &[
            "--model",
            "z3816a",
            "--board",
            board.to_str().expect("a path"),
            "--fpga-design",
            "unmodeled",
        ],
    );
    assert_eq!(written[NV_SLOT_0], synthetic[NV_SLOT_0]);
    assert_eq!(written[PARAMETER_RECORD], synthetic[PARAMETER_RECORD]);
    assert_logs_cleared(&written, &synthetic);
}

/// The Z3805A 1.01.03's, on the board file its revision picks.
#[test]
fn the_z3805a_10103_saves_its_synthetic_eeproms_records() {
    let (written, synthetic) = first_power_up(Z3805A_10103, "first-z3805a-10103", &[]);
    assert_eq!(written[NV_SLOT_0], synthetic[NV_SLOT_0]);
    assert_eq!(written[PARAMETER_RECORD], synthetic[PARAMETER_RECORD]);
    assert_logs_cleared(&written, &synthetic);
}

#[test]
fn the_58503b_saves_its_synthetic_eeproms_records() {
    let (written, synthetic) = first_power_up(HP58503B_SYNTHETIC, "first-58503b", &[]);
    assert_eq!(written[NV_SLOT_0], synthetic[NV_SLOT_0]);
    assert_eq!(written[PARAMETER_RECORD], synthetic[PARAMETER_RECORD]);
    assert_logs_cleared(&written, &synthetic);
}
