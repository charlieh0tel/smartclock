//! The mc683xx backend (docs/emulator.md, "The mc683xx backend"): the
//! receiver's board file, `boards/*.toml`, loaded through
//! mc683xx-board with smartclock's device types and run on mc683xx's
//! MC68331 by the board's own run loop, `Board::run_until`.  smartclock's
//! map of the board (`board`, `machine`) and its SIM, QSM and GPT are
//! not on this path.
//!
//! The board side (`powered`) is one power-up's board, built from the
//! file (`load`) with the run's choices for its parts (`overrides`) and
//! what a power cycle carries (`carried`); the run side (`runner`) is
//! what outlives a power cycle: the lines bound to the board's
//! endpoints (`endpoints`, `pps`), what runs alongside, the controls
//! given and not yet applied, the pacing and the page (`page`).
//!
//! The command line's run-time choices are not the board file's: they
//! are parameters of the parts they belong to (`overrides`: the
//! EEPROM's working file and the dump it starts from, the GPS engine's
//! start and position, the oscillator's, the indicators' reports, the
//! lamp board's, the LED bank's and the front panel's), merged over the
//! file's,
//! and `--gps` swaps the synthetic engine for a run-time
//! endpoint.  The file's run-time endpoints are bound to the ports the
//! command line serves: `host` to standard input and output, a script,
//! `--host` or the panel page's console; `sci` to `--sci`; `gps` to `--gps`; `alarm` to the
//! indicators' reports.  `--panel` serves the panel page (`panel`), which
//! the run loop publishes to and takes clicked keys from.
//!
//! The host steps the run (`Run`): deterministically a slice of `SLICE`
//! at most at a time, each ending at the next step of what runs
//! alongside it, or paced a frame at a time to its clock's reading, in
//! mc683xx's quanta.  Between the board's runs bytes go in and out of
//! the endpoints and the trace is written.

mod carried;
mod eeprom;
mod endpoints;
mod load;
mod overrides;
mod page;
mod powered;
mod pps;
mod runner;
#[cfg(test)]
mod tests;
mod time_code;

use std::path::PathBuf;
use std::rc::Rc;
use std::time::Duration;

use mc683xx_board::EditError;
use mc683xx_board::LoadError;
use mc683xx_board::NoReceiver;
use mc683xx_board::RunError;
use mc683xx_board::SendError;
use mc683xx_board::registry::RegistryError;
use mc683xx_board::time::Span;
use mc683xx_board::time::Time;
use smartclock::http::ServeError;

use crate::config::RunConfig;
use crate::files::Files;
use crate::gps::range::OutsideError;
use crate::host::HostError;
use crate::host::Ports;
use crate::panel::state::Drawing;
use crate::run::Halted;
use crate::run::Pacing;
use carried::Carried;
use load::Loaded;
use powered::Powered;
use runner::Runner;

/// Why an mc683xx run could not start, or stopped short.
#[derive(Debug, thiserror::Error)]
pub enum Error {
    /// The board file not read.
    #[error("reading {}", path.display())]
    Read {
        /// The file.
        path: PathBuf,
        /// Why.
        #[source]
        source: std::io::Error,
    },
    /// The board file not TOML.
    #[error("parsing {}", path.display())]
    Parse {
        /// The file.
        path: PathBuf,
        /// Why.
        #[source]
        source: toml::de::Error,
    },
    /// The board file, `--gps`'s endpoint swapped in, not written back.
    #[error(transparent)]
    Write(#[from] toml::ser::Error),
    /// A path not made absolute.
    #[error("resolving {}", path.display())]
    Resolve {
        /// The path.
        path: PathBuf,
        /// Why.
        #[source]
        source: std::io::Error,
    },
    /// The board file, or the run's parameters for its parts, not the
    /// board's.
    #[error("loading {}", path.display())]
    Load {
        /// The file.
        path: PathBuf,
        /// Why.
        #[source]
        source: LoadError,
    },
    /// The run's parameters for the board file's parts not set.
    #[error("setting {}'s parameters", path.display())]
    Edit {
        /// The file.
        path: PathBuf,
        /// Why.
        #[source]
        source: EditError,
    },
    /// The EEPROM working file's directory not made.
    #[error("making the directory {}", path.display())]
    Directory {
        /// The directory.
        path: PathBuf,
        /// Why.
        #[source]
        source: std::io::Error,
    },
    /// The EEPROM in memory not on the board.
    #[error("the board has no AT28C64B {0} for the EEPROM in memory")]
    NoEeprom(String),
    /// The board not built.
    #[error("building {}", path.display())]
    Build {
        /// The file.
        path: PathBuf,
        /// Why.
        #[source]
        source: LoadError,
    },
    /// smartclock's types not registered.
    #[error("{0}")]
    Register(#[from] RegistryError),
    /// A part stalled the board.
    #[error("the mc683xx board stalled")]
    Stalled(#[source] RunError),
    /// A line's bytes not taken by the board's endpoint.
    #[error("sending to the board's endpoint {name}")]
    Send {
        /// The endpoint.
        name: &'static str,
        /// Why.
        #[source]
        source: SendError,
    },
    /// What the board sent an endpoint not taken.
    #[error(transparent)]
    Receive(#[from] NoReceiver),
    /// A line's far end not put on.
    #[error(transparent)]
    Host(#[from] HostError),
    /// An option for the SCI on a board without one.
    #[error("{option} needs an SCI, and {} has no `sci` endpoint", board.display())]
    NoSci {
        /// The option.
        option: &'static str,
        /// The board file.
        board: PathBuf,
    },
    /// The panel page not served.
    #[error("serving the panel page on {address}")]
    Panel {
        /// Where.
        address: String,
        /// Why.
        #[source]
        source: ServeError,
    },
    /// The synthetic engine's position or start not one it takes.
    #[error(transparent)]
    Position(#[from] OutsideError),
    /// The synthetic engine's start, a power cycle on, past its dates.
    #[error(transparent)]
    Start(#[from] jiff::Error),
    /// A count past what a parameter holds.
    #[error(transparent)]
    Count(#[from] std::num::TryFromIntError),
    /// The CPU halted.
    #[error(transparent)]
    Halted(#[from] Halted),
    /// What runs alongside failed the run: a script's expect not met in
    /// time.
    #[error("{0}")]
    Failed(String),
}

/// The EEPROM in memory as a run leaves it, for its host to keep and
/// give a later run (`config::Eeprom::Memory`): what a power cycle
/// carries of it.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct KeptEeprom {
    /// Its cells, the part's 8 KiB, which the later run is given as the
    /// board file's EEPROM image's bytes.
    pub cells: Vec<u8>,
    /// Whether its software data protection is on, the later run's
    /// `Eeprom::Memory`'s `protected`.
    pub protected: bool,
}

/// What a step of the run left.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Step {
    /// The run goes on, the board `behind` the host's clock: zero once
    /// it has caught up, and always for a deterministic step.
    Running {
        /// How far behind.
        behind: Duration,
    },
    /// The run has ended: its time is up, what runs alongside stopped
    /// it, or the board did.
    Ended,
}

/// A run of an image on its board file, which its host steps: a
/// deterministic run a slice at a time to a board time
/// (`Run::step_until`), a paced one a frame at a time to the board time
/// due by the host's clock (`Run::step_frame`).
#[derive(Debug)]
pub struct Run {
    /// The run, while it can go on: none once a step has failed.
    runner: Option<Runner>,
    /// How its board's time keeps to the host's clock.
    pacing: Pacing,
}

impl Run {
    /// The run of `config`'s image on its board file, powered up, its
    /// lines' far ends and the page's server `ports`', its files read
    /// from `files`, the page's console named for `started`, UTC as the
    /// run starts.
    ///
    /// # Errors
    ///
    /// The board not built, or a far end not put on.
    pub fn start(
        config: RunConfig,
        ports: &mut dyn Ports,
        files: Rc<dyn Files>,
        started: jiff::Timestamp,
    ) -> Result<Self, Error> {
        let pacing = config.pacing;
        let loaded = Loaded::new(&config, files)?;
        let mut powered = Powered::up(&config, &loaded, Carried::default())?;
        let drawing = Drawing::of(config.model, powered.live().has_front_panel());
        let runner =
            Runner::new(config, loaded, powered, ports)?.with_panel(drawing, ports, started)?;
        Ok(Self {
            runner: Some(runner),
            pacing,
        })
    }

    /// How the run's board time keeps to the host's clock: a host steps
    /// a paced run by frames, a free one by slices.
    #[must_use]
    pub fn pacing(&self) -> Pacing {
        self.pacing
    }

    /// The EEPROM as it stands, for the host to keep and start a later
    /// run from (`Eeprom::Memory`), when it is in memory; none when it is
    /// in a working file, or the run has failed.
    ///
    /// # Errors
    ///
    /// The EEPROM not on the board.
    pub fn eeprom(&mut self) -> Result<Option<KeptEeprom>, Error> {
        self.runner.as_mut().map_or(Ok(None), Runner::eeprom)
    }

    /// The run, deterministically, to `target`, board time since its
    /// start, or until it ends, or with none until it ends.
    ///
    /// # Errors
    ///
    /// The board stalled or halted, or what runs alongside failed it.
    pub fn step_until(&mut self, target: Option<Time>) -> Result<Step, Error> {
        self.step(|runner| runner.step_until(target))
    }

    /// A frame of the run, to the board time due by the host's clock
    /// reading `wall`, from any origin the host keeps for the run's
    /// life, never going back, but no more than `picoseconds` of board
    /// time (`Board::run_frame_with`): a frame so bounded drops the time
    /// it missed.  The first frame, and the first after a power cycle or
    /// a deterministic step, runs nothing: it starts the pacing.
    ///
    /// # Errors
    ///
    /// As `step_until`.
    pub fn step_frame(&mut self, wall: Duration, picoseconds: u64) -> Result<Step, Error> {
        self.step(|runner| runner.step_frame(wall, Span::from_picos(picoseconds)))
    }

    /// A step `step` of the run, which ends with its failure.
    fn step(
        &mut self,
        step: impl FnOnce(Runner) -> Result<(Runner, Step), Error>,
    ) -> Result<Step, Error> {
        let Some(runner) = self.runner.take() else {
            return Ok(Step::Ended);
        };
        let (runner, stepped) = step(runner)?;
        self.runner = Some(runner);
        Ok(stepped)
    }
}
