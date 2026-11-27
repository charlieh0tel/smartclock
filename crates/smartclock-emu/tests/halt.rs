//! A CPU that halts ends the run with an error saying where: an image
//! whose reset vectors are odd, so that the first fetch's address error
//! is taken onto an odd stack, a double bus fault (CPU32RM 6.2.1).

#![cfg(feature = "mc683xx")]

mod common;

use std::process::Command;
use std::process::Output;

use common::Scratch;
use common::script;

/// The flash's size, as the emulator takes an image.
const FLASH_SIZE: usize = 512 * 1024;
/// The reset vectors, the stack pointer's and the program counter's,
/// both odd.
const ODD_VECTORS: [u8; 8] = [0, 0, 0, 1, 0, 0, 0, 1];
/// Where the halted CPU is, the reset vector's program counter, in its
/// 32 bits as either core says it (`run::Halted`).
const HALTED_AT: &str = "Error: the CPU halted at 0x00000001: a double bus fault";

/// The emulator run on a Z3805A's board with the halting image, on
/// `core`, with `options`.  mc683xx's alone: Unicorn's core panics on
/// it, an overflow taking the first exception.
fn halting(name: &str, core: &str, options: &[&str]) -> Output {
    let image = Scratch::new(&format!("{name}-image"));
    let mut flash = vec![0xff; FLASH_SIZE];
    flash[..ODD_VECTORS.len()].copy_from_slice(&ODD_VECTORS);
    std::fs::write(&image.0, flash).expect("the image written");
    let eeprom = Scratch::new(name);
    Command::new(env!("CARGO_BIN_EXE_smartclock-emu"))
        .arg(&image.0)
        .args(["--model", "z3805a", "--core", core, "--eeprom"])
        .arg(&eeprom.0)
        .args(["--sci", "127.0.0.1:0", "--deterministic"])
        .args(["--stop-after", "1"])
        .args(options)
        .output()
        .expect("the emulator runs")
}

/// A power cycle given in the slice the CPU halts in comes after the
/// halt, and the run ends at the halt, the board not built again, as
/// it would end were the power cycle a slice later.
#[test]
fn a_halt_ends_the_run_before_a_power_cycle_in_its_slice() {
    let steps = script("halt-power-cycle", "event power-cycle\n");
    let path = steps.0.to_str().expect("a path");
    let output = halting("halt-power-cycle", "mc683xx", &["--script", path]);
    let errors = String::from_utf8_lossy(&output.stderr);
    assert!(!output.status.success(), "{errors}");
    assert!(errors.contains(HALTED_AT), "{errors}");
    assert_eq!(
        errors.matches(" core on ").count(),
        1,
        "built once: {errors}"
    );
}
