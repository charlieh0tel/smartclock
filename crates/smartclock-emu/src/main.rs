//! A SmartClock receiver's own firmware, booted in an emulator, on the
//! command line: its host port on standard input and output, or served
//! over TCP with `--host` as `smartclock-sim` serves its simulated
//! receiver, so that the daemon and the CLI connect to it by
//! `tcp://HOST:PORT`, and its SCI over TCP (docs/emulator.md).
//!
//! The command line (`cli`) is checked once into the run it asks for,
//! `smartclock_emu::config::RunConfig`, which a core of the library
//! runs, the host's side of it this binary's: standard input and output
//! (`stdio`), the TCP servers and the panel page's server (`serve`), the
//! file system the run's files are read from, and the host's clock,
//! which an mc683xx run is stepped to here (`run`).

mod cli;
mod serve;
mod stdio;

use std::rc::Rc;
use std::thread;
use std::time::Duration;
use std::time::Instant;

use anyhow::Context as _;
use anyhow::Result;
use clap::Parser;
use smartclock_emu::config::Core;
use smartclock_emu::config::HostPort;
use smartclock_emu::config::RunConfig;
use smartclock_emu::files::Disk;
use smartclock_emu::host::Ports;
#[cfg(feature = "mc683xx")]
use smartclock_emu::mc683xx::Run;
#[cfg(feature = "mc683xx")]
use smartclock_emu::mc683xx::Step;
use smartclock_emu::run::Pacing;

use crate::cli::Cli;
use crate::serve::Native;
use crate::stdio::terminal;

/// How long a paced run waits between frames once its board has caught
/// up with the wall clock: mc683xx's quantum, `pacing::QUANTUM`, the
/// board time a frame's quanta are cut to.
const FRAME: Duration = Duration::from_millis(5);

fn main() -> Result<()> {
    let config = cli::config(Cli::parse())?;
    // the terminal, raw only when the host port is on it, put back as
    // main returns, whichever way
    let raw = if config.host == HostPort::Stdio {
        terminal::raw().context("making the terminal raw")?
    } else {
        None
    };
    let mut ports = Native::new(raw.is_some());
    match config.core {
        Core::Unicorn => Ok(smartclock_emu::unicorn::run(&config, &mut ports, &Disk)?),
        #[cfg(feature = "mc683xx")]
        Core::Mc683xx => run(config, &mut ports),
    }
}

/// Runs `config` on mc683xx, its ports `ports`: flat out with
/// `--deterministic`, else a frame at a time to the wall clock, never
/// bounded, so that a board fallen behind catches up rather than drop
/// the time, and with a wait between frames once it has.
#[cfg(feature = "mc683xx")]
fn run(config: RunConfig, ports: &mut dyn Ports) -> Result<()> {
    let mut run = Run::start(config, ports, Rc::new(Disk), jiff::Timestamp::now())?;
    match run.pacing() {
        Pacing::Free => {
            run.step_until(None)?;
        }
        Pacing::Wall => {
            let origin = Instant::now();
            while let Step::Running { behind } = run.step_frame(origin.elapsed(), u64::MAX)? {
                if behind.is_zero() {
                    thread::sleep(FRAME);
                }
            }
        }
    }
    Ok(())
}
