//! A SmartClock receiver's own firmware, booted in an emulator: the
//! flash image runs on an emulated CPU32 among models of the board's
//! devices (docs/emulator.md).
//!
//! This is the board's side, what any host runs: the run, checked
//! (`config`), the board's models (`board`, `gps`), the cores' runs, and
//! the panel page's answers as requests and responses (`panel`).  What
//! a host gives it is behind `host::Ports`, the far ends of the board's
//! lines (standard input and output, TCP servers) and the page's
//! server, `files::Files`, the files it reads, and the clock's readings
//! it is stepped to (`mc683xx::Run`).  The `smartclock-emu` binary is one host, a command line.
//!
//! The CPU core is mc683xx's MC68331, which runs the receiver's board
//! file (`mc683xx`), behind the `mc683xx` feature, on by default
//! (docs/emulator.md, "The mc683xx backend").  Unicorn, a development
//! core (`unicorn`), is native alone, its binding behind the `unicorn`
//! feature, off by default: `--core unicorn` without it refuses to run.

#![cfg_attr(
    all(not(feature = "unicorn"), not(test)),
    expect(
        dead_code,
        reason = "without its CPU core nothing reaches the board's devices; the tests do"
    )
)]

pub mod board;
pub mod config;
mod control;
#[cfg(not(target_family = "wasm"))]
mod cpu;
pub mod files;
pub mod gps;
pub mod host;
mod indicators;
pub mod line;
#[cfg(not(target_family = "wasm"))]
mod machine;
#[cfg(feature = "mc683xx")]
pub mod mc683xx;
pub mod names;
pub mod options;
#[cfg(feature = "mc683xx")]
pub mod panel;
mod ports;
pub mod run;
pub mod script;
pub mod trace;
#[cfg(not(target_family = "wasm"))]
pub mod unicorn;
