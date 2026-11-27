//! The emulator's run as a browser hosts it (`Emulator`): started from
//! command-line-style arguments, parsed by the native command line's
//! rules for the options every host takes (`options::RunOptions`), and
//! from files given as bytes; stepped a frame at a time to the page's
//! clock; the panel page's requests answered; the EEPROM's cells and
//! protection given back to keep.

use std::path::Path;
use std::path::PathBuf;
use std::rc::Rc;
use std::time::Duration;

use clap::Parser;
use smartclock::http::Request;
use smartclock::http::Response;
use smartclock_emu::config::Eeprom;
use smartclock_emu::config::HostPort;
use smartclock_emu::config::RunConfig;
use smartclock_emu::files::Files as _;
use smartclock_emu::files::InMemory;
use smartclock_emu::mc683xx::KeptEeprom;
use smartclock_emu::mc683xx::Run;
use smartclock_emu::mc683xx::Step;
use smartclock_emu::options::RunOptions;
use smartclock_emu::panel::routes::Shared;

use crate::error::Error;
use crate::host::Browser;

/// The directory the model's board file is in, among the files given,
/// without `--board`: the static build's, as the repository's.
const BOARDS: &str = "boards";

/// The address the panel page is served at, which nothing listens on:
/// the page's requests are answered in the page.
const PAGE: &str = "page";

/// The most board time a frame runs, milliseconds: three of a 60 Hz
/// display's frames, so that a board fallen behind catches up, but a
/// tab that was hidden, whose frames stopped, drops the time it missed
/// rather than running it all at once.
const FRAME_LIMIT_MS: u64 = 50;

/// Picoseconds a millisecond, as the board counts its time.
const PICOSECONDS_PER_MILLISECOND: u64 = 1_000_000_000;

/// The command line a browser's run takes: the image by its path among
/// the files given, the EEPROM's protection kept, and the options every
/// host takes.  The EEPROM's cells are the board file's EEPROM image,
/// among the files: the dump, or the cells a run before kept.
#[derive(Debug, Parser)]
#[command(name = "smartclock-emu-web", no_binary_name = true)]
struct WebCli {
    /// The full 512 KiB flash image, its path among the files.
    image: PathBuf,
    /// Whether the EEPROM's software data protection is on at the start,
    /// as a run before kept it (`Emulator::eeprom`); without, the board
    /// file's.
    #[arg(long, value_name = "BOOL")]
    eeprom_protected: Option<bool>,
    /// The options every host takes.
    #[command(flatten)]
    run: RunOptions,
}

/// A run in a browser.
#[derive(Debug)]
pub struct Emulator {
    /// The run.
    run: Run,
    /// The panel page's answers.
    page: Shared,
    /// The page's clock's latest reading, from its own origin: the
    /// frames are stepped to it and the page's requests answered at it.
    wall: Duration,
}

impl Emulator {
    /// The run `arguments` ask for, as the command line's, without the
    /// program's name, its files read from `files`, its host port the
    /// panel page's console and its EEPROM in memory, from the board
    /// file's image among `files`; the synthetic engine's start `--gps-start` or
    /// the host's clock's reading `now`, UTC.
    ///
    /// # Errors
    ///
    /// Arguments the command line refuses, a file not given, a run that
    /// cannot be, or its board not built.
    pub fn start(
        arguments: &[String],
        files: InMemory,
        now: jiff::Timestamp,
    ) -> Result<Self, Error> {
        let cli = WebCli::try_parse_from(arguments)?;
        let read = |path: &Path| {
            files.read(path).map_err(|source| Error::File {
                path: path.to_owned(),
                source,
            })
        };
        let image = read(&cli.image)?;
        let config = cli.run.config(
            cli.image,
            image,
            Eeprom::Memory {
                protected: cli.eeprom_protected,
            },
            Path::new(BOARDS),
            now,
        )?;
        let config = RunConfig {
            host: HostPort::Panel,
            panel: Some(PAGE.to_owned()),
            ..config
        };
        config.check()?;
        let mut ports = Browser::default();
        let run = Run::start(config, &mut ports, Rc::new(files), now)?;
        let page = ports.page.ok_or(Error::NoPage)?;
        Ok(Self {
            run,
            page,
            wall: Duration::ZERO,
        })
    }

    /// A frame of the run, to the board time due by the page's clock
    /// reading `wall`, from its own origin, but no more than
    /// `FRAME_LIMIT_MS` of board time.  A reading before the last is
    /// taken as the last.  The first frame runs nothing: it starts the
    /// pacing.
    ///
    /// # Errors
    ///
    /// The board stalled or halted.
    pub fn frame(&mut self, wall: Duration) -> Result<Step, Error> {
        self.wall = self.wall.max(wall);
        Ok(self
            .run
            .step_frame(self.wall, FRAME_LIMIT_MS * PICOSECONDS_PER_MILLISECOND)?)
    }

    /// The answer to the page's `request`, at the page's clock's latest
    /// reading.
    #[must_use]
    pub fn respond(&self, request: &Request) -> Response {
        self.page.respond(request, self.wall)
    }

    /// The EEPROM's cells and protection as they stand, to keep and
    /// start a later run from: the cells as the board file's EEPROM
    /// image, the protection as `--eeprom-protected`; none once the run
    /// has failed.
    ///
    /// # Errors
    ///
    /// The EEPROM not on the board.
    pub fn eeprom(&mut self) -> Result<Option<KeptEeprom>, Error> {
        Ok(self.run.eeprom()?)
    }
}
