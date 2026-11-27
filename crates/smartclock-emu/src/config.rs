//! The run a host asks for, checked once, before anything runs: the
//! image read and its model known, the ports' far ends, the synthetic
//! engine's settings, the oscillator's, the tracing and the run's end.
//! Both cores run from it.  A run starts with every choice the command
//! line's default (`RunConfig::new`), a host sets those it is given,
//! and `RunConfig::check` checks the whole.

use std::path::Path;
use std::path::PathBuf;

use clap::ValueEnum;
use smartclock_gps::engine::Options;
use smartclock_gps::units::Seconds;

use crate::board::FLASH_SIZE;
use crate::board::NOISE_SEED;
use crate::board::ocxo::Terms as OcxoTerms;
use crate::board::profile::Model;
use crate::board::time::CPU_HZ;
use crate::board::time::Cycles;
use crate::gps::Config as GpsConfig;
use crate::gps::GPS_LESS_UTC;
use crate::gps::range;
use crate::gps::range::OutsideError;
use crate::gps::range::RangeError;
use crate::gps::sky;
use crate::gps::sky::FIRMWARE_DEFAULT;
use crate::run::Pacing;
use crate::trace::Tracing;

/// Where the SCI is served unless `--sci` says.
const SCI_ADDRESS: &str = "127.0.0.1:5026";

/// The repository's board files, where a run's are by default.
pub const REPOSITORY_BOARDS: &str = concat!(env!("CARGO_MANIFEST_DIR"), "/../../boards");

/// A run, checked.
#[derive(Debug)]
pub struct RunConfig {
    /// The flash image's file.
    pub image_path: PathBuf,
    /// Its bytes, a full flash's.
    pub image: Vec<u8>,
    /// The receiver whose board is modeled.
    pub model: Model,
    /// The CPU core.
    pub core: Core,
    /// The board file the mc683xx core runs: `--board`'s, or the
    /// model's in the repository's.
    pub board: PathBuf,
    /// Where the EEPROM's cells are kept.
    pub eeprom: Eeprom,
    /// The host port's far end.
    pub host: HostPort,
    /// Where `--sci` serves the SCI, if it is given.
    pub sci: Option<String>,
    /// Where every byte the SCI sends is written, if anywhere.
    pub sci_capture: Option<PathBuf>,
    /// The GPS engine's port's far end.
    pub gps: GpsSource,
    /// The synthetic engine's settings, used when it is on the board.
    pub synthetic: SyntheticGps,
    /// Copy what each port sends to standard error.
    pub tap: bool,
    /// Report the lamps and the LEDs.
    pub indicators: bool,
    /// How the board's time keeps to the wall clock's.
    pub pacing: Pacing,
    /// The seed of the board's modeled noise.
    pub seed: u64,
    /// The oscillator's terms the run gives, over the board file's,
    /// which are over the model's (`Model::ocxo`).
    pub ocxo: OcxoTerms,
    /// Whether every power-up starts at the warm-up curve's settled
    /// end, as if the skip warm-up control were taken at its first
    /// instant: the run's start and each power cycle's, not a reset's.
    pub skip_warm_up: bool,
    /// When the run stops by itself, if it does.
    pub until: Option<StopAfter>,
    /// Where the panel page is served, if it is.
    pub panel: Option<String>,
    /// What is traced.
    pub trace: TraceConfig,
    /// The FPGA's design in place of the board file's, `--fpga-design`:
    /// `lca` runs the bitstream's netlist (docs/emulator.md, "The
    /// boards"); the mc683xx core only.
    pub fpga_design: Option<String>,
    /// The synthetic engine's model in place of the board file's,
    /// `--gps-engine`, by its name (`gps::ENGINES`): `b4121` puts a VP
    /// Oncore on the 58503B (docs/emulator.md, "The synthetic GPS
    /// engine"); the mc683xx core only.
    pub gps_engine: Option<String>,
    /// Whether the emulator sends `:PTIMe:TCODe:CONTinuous OFF` on the
    /// host port once each power-up's time code starts,
    /// `--time-code-off`: a client's first command, not the board's; a
    /// model whose image starts the time code at power-up only.
    pub time_code_off: bool,
}

/// Where the EEPROM's cells are kept.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Eeprom {
    /// A working file every write goes through to, `--eeprom`.
    File {
        /// The working file.
        path: PathBuf,
        /// The dump it starts from, if it does not exist,
        /// `--eeprom-dump`; without it, the board file's on the mc683xx
        /// core, erased on Unicorn's.
        dump: Option<PathBuf>,
    },
    /// In memory alone, for a host with no files, a browser: from the
    /// board file's `image`, read through the host's files, written to
    /// no file, and its cells and its protection carried across a power
    /// cycle; the mc683xx core only.  A host keeps them
    /// (`mc683xx::Run::eeprom`) and gives a later run the cells as the
    /// image's bytes and the protection here.
    Memory {
        /// Whether its software data protection is on at the run's
        /// start; without, the board file's `protected`.
        protected: Option<bool>,
    },
}

impl Eeprom {
    /// The working file, if the cells are kept in one.
    #[must_use]
    pub fn working_file(&self) -> Option<&Path> {
        match self {
            Self::File { path, .. } => Some(path),
            Self::Memory { .. } => None,
        }
    }
}

/// The host port's far end.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum HostPort {
    /// Standard input and output, the default.
    Stdio,
    /// A TCP server at this address, `--host`.
    Tcp(String),
    /// The script in this file, `--script`.
    Script(PathBuf),
    /// The panel page's console alone, `--panel` without the others.
    Panel,
}

/// What is on the GPS engine's port, DUART channel A.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum GpsSource {
    /// The synthetic Oncore.
    Synthetic,
    /// An external engine or a recording, served at this address,
    /// `--gps`.
    External(String),
}

/// The synthetic engine's settings, each within the Oncore's ranges
/// (`gps::range`).
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct SyntheticGps {
    /// Its antenna's latitude, degrees, north positive.
    pub latitude: f64,
    /// Its longitude, degrees, east positive.
    pub longitude: f64,
    /// Its height above mean sea level, meters.
    pub height: f64,
    /// UTC at the run's start, a whole second: `--gps-start`, or the
    /// host's clock.
    pub start: jiff::Timestamp,
    /// GPS time less UTC, seconds.
    pub leap_seconds: i8,
}

impl SyntheticGps {
    /// The engine `model` has, set up as these say.
    pub(crate) fn config(&self, model: Model) -> Result<GpsConfig, OutsideError> {
        Ok(GpsConfig {
            position: sky::place(self.latitude, self.longitude, self.height)?,
            start: self.start,
            leap_seconds: Seconds(self.leap_seconds),
            engine: model.gps_engine(),
            options: Options::DEFAULT,
        })
    }
}

/// A span of the board's time a run stops after, `--stop-after`:
/// finite and not negative, seconds.
#[derive(Debug, Clone, Copy, PartialEq, PartialOrd)]
pub struct StopAfter(f64);

impl StopAfter {
    /// `seconds`, if it is a span of time.
    pub fn new(seconds: f64) -> Result<Self, ConfigError> {
        if seconds.is_finite() && seconds >= 0.0 {
            Ok(Self(seconds))
        } else {
            Err(ConfigError::StopAfter(seconds))
        }
    }

    /// The span in CPU cycles, as the Unicorn machine counts it.
    pub(crate) fn cycles(self) -> Cycles {
        // a board's run is far shorter than u64's cycles
        Cycles((self.0 * CPU_HZ as f64) as u64)
    }

    /// The span in seconds.
    pub(crate) fn seconds(self) -> f64 {
        self.0
    }
}

/// What is traced, and how.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct TraceConfig {
    /// What `--trace` names.
    pub traced: Vec<Traced>,
    /// The device addresses whose accesses are traced alone.
    pub watch: Vec<u32>,
    /// The instructions traced each time they run.
    pub watch_code: Vec<u32>,
    /// Count the instructions run.
    pub count_instructions: bool,
}

impl TraceConfig {
    /// Whether `--trace` names `traced`.
    pub(crate) fn traces(&self, traced: Traced) -> bool {
        self.traced.contains(&traced)
    }

    /// The Unicorn machine's tracing.
    pub(crate) fn tracing(&self) -> Tracing {
        Traced::tracing(&self.traced)
    }

    /// Whether anything is traced at all.
    pub(crate) fn any(&self) -> bool {
        !self.traced.is_empty() || !self.watch.is_empty() || !self.watch_code.is_empty()
    }
}

impl RunConfig {
    /// A run of the full flash image `image`, read from `image_path`, on
    /// `model`'s board, or the one the image names, its EEPROM's cells
    /// kept as `eeprom` says, the synthetic engine's UTC at power-up `start`, a
    /// whole second (the host's clock, natively): every other choice the
    /// command line's default.
    pub fn new(
        image_path: PathBuf,
        image: Vec<u8>,
        model: Option<Model>,
        eeprom: Eeprom,
        start: jiff::Timestamp,
    ) -> Result<Self, ConfigError> {
        if image.len() != FLASH_SIZE as usize {
            return Err(ConfigError::NotAFlash {
                path: image_path,
                bytes: image.len(),
            });
        }
        let model =
            model
                .or_else(|| Model::of_image(&image))
                .ok_or_else(|| ConfigError::NoModel {
                    path: image_path.clone(),
                })?;
        let (latitude, longitude, height) = sky::degrees(FIRMWARE_DEFAULT);
        Ok(Self {
            board: Path::new(REPOSITORY_BOARDS).join(model.board_file_for(&image)),
            image_path,
            image,
            model,
            core: Core::DEFAULT,
            eeprom,
            host: HostPort::Stdio,
            sci: None,
            sci_capture: None,
            gps: GpsSource::Synthetic,
            synthetic: SyntheticGps {
                latitude,
                longitude,
                height,
                start: range::start(start)?,
                leap_seconds: GPS_LESS_UTC,
            },
            tap: false,
            indicators: false,
            pacing: Pacing::Wall,
            seed: NOISE_SEED,
            ocxo: OcxoTerms::default(),
            skip_warm_up: false,
            until: None,
            panel: None,
            trace: TraceConfig::default(),
            fpga_design: None,
            gps_engine: None,
            time_code_off: false,
        })
    }

    /// The run, if its choices go together: the panel page, the EEPROM
    /// in memory, the skipped warm-up, the FPGA's design, the GPS
    /// engine's model and the instruction count each on its own core, the time code turned off on a model that starts it, and
    /// the synthetic engine's position checked as a whole, its ellipsoid
    /// height too.
    pub fn check(&self) -> Result<(), ConfigError> {
        if self.core == Core::Unicorn {
            if !self.model.on_unicorn() || self.model.has_variant_for(&self.image) {
                return Err(ConfigError::ModelOnUnicorn);
            }
            if self.panel.is_some() {
                return Err(ConfigError::PanelOnUnicorn);
            }
            if self.eeprom.working_file().is_none() {
                return Err(ConfigError::MemoryOnUnicorn);
            }
            if self.skip_warm_up {
                return Err(ConfigError::SkipWarmUpOnUnicorn);
            }
            if self.fpga_design.is_some() {
                return Err(ConfigError::FpgaDesignOnUnicorn);
            }
            if self.gps_engine.is_some() {
                return Err(ConfigError::GpsEngineOnUnicorn);
            }
        } else if self.trace.count_instructions {
            return Err(ConfigError::CountOnMc683xx);
        }
        if self.time_code_off && !self.model.starts_time_code() {
            return Err(ConfigError::TimeCodeNotStarted(self.model.name()));
        }
        self.synthetic.config(self.model)?;
        Ok(())
    }

    /// Where the SCI is served.
    pub(crate) fn sci_address(&self) -> &str {
        self.sci.as_deref().unwrap_or(SCI_ADDRESS)
    }

    /// The option that asks for the SCI, if one does.
    pub(crate) fn sci_asked(&self) -> Option<&'static str> {
        if self.sci.is_some() {
            Some("--sci")
        } else {
            self.sci_capture.as_ref().map(|_| "--sci-capture")
        }
    }

    /// Whether a script runs the host port.
    pub(crate) fn scripted(&self) -> bool {
        matches!(self.host, HostPort::Script(_))
    }
}

/// A CPU core the emulator runs the image on.
#[derive(Debug, Clone, Copy, PartialEq, Eq, ValueEnum)]
pub enum Core {
    /// Unicorn's m68k, on the board as `board` maps it.
    Unicorn,
    /// mc683xx's MC68331, on the board file through mc683xx-board.
    #[cfg(feature = "mc683xx")]
    Mc683xx,
}

impl Core {
    /// The core a run takes by default: mc683xx's when this build has
    /// it.
    #[cfg(feature = "mc683xx")]
    pub const DEFAULT: Self = Self::Mc683xx;
    /// The core a run takes by default: mc683xx's when this build has
    /// it.
    #[cfg(not(feature = "mc683xx"))]
    pub const DEFAULT: Self = Self::Unicorn;
}

/// What `--trace` traces.
#[derive(Debug, Clone, Copy, PartialEq, Eq, ValueEnum)]
pub enum Traced {
    /// Where the CPU is, ten times a second of the board's time.
    Position,
    /// Every device access.
    Devices,
    /// Every exception, interrupts among them.
    Exceptions,
    /// Every QSPI transfer; the mc683xx core only.
    #[cfg(feature = "mc683xx")]
    Qspi,
    /// Every level change of a chip pin on a net; the mc683xx core
    /// only.
    #[cfg(feature = "mc683xx")]
    Pins,
}

impl Traced {
    /// The tracing `traced` asks for.
    pub(crate) fn tracing(traced: &[Self]) -> Tracing {
        Tracing {
            position: traced.contains(&Self::Position),
            devices: traced.contains(&Self::Devices),
            exceptions: traced.contains(&Self::Exceptions),
        }
    }
}

/// A run that cannot be: each as the command line's option names it.
#[derive(Debug, thiserror::Error)]
pub enum ConfigError {
    /// An image not a full flash's.
    #[error(
        "{} holds {bytes} bytes, not a full {} KiB flash image",
        path.display(),
        FLASH_SIZE / 1024
    )]
    NotAFlash {
        /// The image's file.
        path: PathBuf,
        /// Its size.
        bytes: usize,
    },
    /// An image that names no model, and none given.
    #[error("{} names no model the emulator knows; give --model", path.display())]
    NoModel {
        /// The image's file.
        path: PathBuf,
    },
    /// The panel page on the Unicorn core.
    #[error("--panel needs the mc683xx core")]
    PanelOnUnicorn,
    /// The EEPROM in memory on the Unicorn core.
    #[error("an EEPROM in memory needs the mc683xx core")]
    MemoryOnUnicorn,
    /// The warm-up skipped on the Unicorn core, which takes no controls.
    #[error("--skip-warm-up needs the mc683xx core")]
    SkipWarmUpOnUnicorn,
    /// The FPGA's design chosen on the Unicorn core, whose board runs
    /// the hand model alone.
    #[error("--fpga-design needs the mc683xx core")]
    FpgaDesignOnUnicorn,
    /// The GPS engine's model chosen on the Unicorn core, whose board
    /// takes the model's engine alone.
    #[error("--gps-engine needs the mc683xx core")]
    GpsEngineOnUnicorn,
    /// A model whose board the Unicorn core does not have, or an image
    /// of a revision with a board of its own.
    #[error(
        "this image's board needs the mc683xx core: the Unicorn board is the DUART boards' with an Oncore"
    )]
    ModelOnUnicorn,
    /// The time code turned off on a model whose image does not start
    /// it at power-up.
    #[error(
        "--time-code-off needs a receiver whose image starts the time code at power-up; the {0}'s does not"
    )]
    TimeCodeNotStarted(&'static str),
    /// The instruction count asked of mc683xx.
    #[error("--count-instructions is the Unicorn core's: mc683xx counts the instructions always")]
    CountOnMc683xx,
    /// A stop that is not a span of time.
    #[error("--stop-after takes a number of seconds, not {0}")]
    StopAfter(f64),
    /// A power-up time the engine does not take.
    #[error(transparent)]
    Start(#[from] RangeError),
    /// A position the engine does not take.
    #[error(transparent)]
    Position(#[from] OutsideError),
}

#[cfg(test)]
mod tests {
    use std::path::PathBuf;

    use super::ConfigError;
    use super::Core;
    use super::Eeprom;
    use super::RunConfig;
    use super::StopAfter;
    use crate::board::FLASH_SIZE;
    use crate::board::profile::Model;
    use crate::board::time::CPU_HZ;
    use crate::board::time::Cycles;

    /// The FPGA's design is the mc683xx board file's to choose: Unicorn's
    /// board runs the hand model alone.
    #[test]
    fn the_fpga_design_is_mc683xxs_alone() {
        let mut config = RunConfig::new(
            PathBuf::from("image.bin"),
            vec![0; FLASH_SIZE as usize],
            Some(Model::Z3805A),
            Eeprom::File {
                path: PathBuf::from("eeprom.bin"),
                dump: None,
            },
            jiff::Timestamp::UNIX_EPOCH + jiff::SignedDuration::from_hours(500_000),
        )
        .expect("a run");
        config.fpga_design = Some("lca".to_owned());
        config.core = Core::Mc683xx;
        assert!(config.check().is_ok());
        config.core = Core::Unicorn;
        assert!(matches!(
            config.check(),
            Err(ConfigError::FpgaDesignOnUnicorn)
        ));
    }

    /// The GPS engine's model is the mc683xx board file's to choose:
    /// Unicorn's board takes the model's engine.
    #[test]
    fn the_gps_engine_is_mc683xxs_alone() {
        let mut config = RunConfig::new(
            PathBuf::from("image.bin"),
            vec![0; FLASH_SIZE as usize],
            Some(Model::Z3805A),
            Eeprom::File {
                path: PathBuf::from("eeprom.bin"),
                dump: None,
            },
            jiff::Timestamp::UNIX_EPOCH + jiff::SignedDuration::from_hours(500_000),
        )
        .expect("a run");
        config.gps_engine = Some("b4121".to_owned());
        config.core = Core::Mc683xx;
        assert!(config.check().is_ok());
        config.core = Core::Unicorn;
        assert!(matches!(
            config.check(),
            Err(ConfigError::GpsEngineOnUnicorn)
        ));
    }

    /// The 58503B's board is its board file's: Unicorn's board in code
    /// has no ACIA.
    #[test]
    fn the_58503b_is_mc683xxs_alone() {
        let mut config = RunConfig::new(
            PathBuf::from("image.bin"),
            vec![0; FLASH_SIZE as usize],
            Some(Model::Hp58503B),
            Eeprom::File {
                path: PathBuf::from("eeprom.bin"),
                dump: None,
            },
            jiff::Timestamp::UNIX_EPOCH + jiff::SignedDuration::from_hours(500_000),
        )
        .expect("a run");
        config.core = Core::Mc683xx;
        assert!(config.check().is_ok());
        config.core = Core::Unicorn;
        assert!(matches!(config.check(), Err(ConfigError::ModelOnUnicorn)));
    }

    /// An image of a revision with a board of its own runs on that board
    /// file by default, the Z3805A 1.01.03's with its Furuno, and on
    /// mc683xx alone: Unicorn's board in code has the model's Oncore.
    #[test]
    fn a_revisions_own_board_is_its_images_and_mc683xxs_alone() {
        let mut image = vec![0; FLASH_SIZE as usize];
        image[0x100..0x107].copy_from_slice(b"Z3805A\0");
        let config = |image: Vec<u8>| {
            RunConfig::new(
                PathBuf::from("image.bin"),
                image,
                None,
                Eeprom::File {
                    path: PathBuf::from("eeprom.bin"),
                    dump: None,
                },
                jiff::Timestamp::UNIX_EPOCH + jiff::SignedDuration::from_hours(500_000),
            )
            .expect("a run")
        };
        assert!(config(image.clone()).board.ends_with("boards/z3805a.toml"));
        image[0x200..0x208].copy_from_slice(b"1.01.03\0");
        let mut config = config(image);
        assert_eq!(config.model, Model::Z3805A);
        assert!(config.board.ends_with("boards/z3805a-10103.toml"));
        config.core = Core::Mc683xx;
        assert!(config.check().is_ok());
        config.core = Core::Unicorn;
        assert!(matches!(config.check(), Err(ConfigError::ModelOnUnicorn)));
    }

    /// The time code is turned off only where the image starts it at
    /// power-up: the Z3816A's, not the Z3805A's.
    #[test]
    fn the_time_code_is_turned_off_only_where_it_starts() {
        let run = |model| {
            let mut config = RunConfig::new(
                PathBuf::from("image.bin"),
                vec![0; FLASH_SIZE as usize],
                Some(model),
                Eeprom::File {
                    path: PathBuf::from("eeprom.bin"),
                    dump: None,
                },
                jiff::Timestamp::UNIX_EPOCH + jiff::SignedDuration::from_hours(500_000),
            )
            .expect("a run");
            config.core = Core::Mc683xx;
            config.time_code_off = true;
            config.check()
        };
        assert!(run(Model::Z3816A).is_ok());
        let refused = run(Model::Z3805A).expect_err("refused");
        assert!(
            matches!(refused, ConfigError::TimeCodeNotStarted("z3805a")),
            "{refused}"
        );
    }

    #[test]
    fn a_stop_is_a_span_of_time_in_cycles() {
        assert_eq!(
            StopAfter::new(1.5).expect("a span").cycles(),
            Cycles(CPU_HZ * 3 / 2)
        );
        for seconds in [f64::INFINITY, f64::NAN, -1.0] {
            assert!(StopAfter::new(seconds).is_err(), "{seconds}");
        }
    }
}
