//! The options every host's command line takes alike (`RunOptions`), as
//! clap parses them: the model and its board file, the synthetic
//! engine's settings, the noise's seed, the oscillator's terms, the run's
//! stop, the indicators' reports and the tracing.  A host flattens them
//! into its own command line, beside the options of its own ports and
//! files, and sets them on the run it makes (`RunOptions::config`).

use std::path::Path;
use std::path::PathBuf;

use clap::Args;

use crate::board::NOISE_SEED;
use crate::board::ocxo::Terms as OcxoTerms;
use crate::board::profile::Model;
use crate::config::ConfigError;
use crate::config::Eeprom;
use crate::config::RunConfig;
use crate::config::StopAfter;
use crate::config::TraceConfig;
use crate::config::Traced;
use crate::gps::GPS_LESS_UTC;
use crate::gps::range;
use crate::gps::sky::CENTIMETERS_PER_METER;
use crate::gps::sky::FIRMWARE_DEFAULT;
use crate::gps::sky::MAS_PER_DEGREE;

/// The options every host takes alike.
#[derive(Debug, Args)]
pub struct RunOptions {
    /// The receiver whose board to model; by default the one the image
    /// names.
    #[arg(long, value_enum)]
    model: Option<Model>,
    /// With `--core mc683xx`, the board file; by default the model's
    /// in the repository's `boards`, or its image's revision's where it
    /// has one of its own (the Z3805A 1.01.03's).
    #[arg(long, value_name = "FILE")]
    board: Option<PathBuf>,
    /// With `--core mc683xx`, the FPGA's design in place of the board
    /// file's: `lca` runs the netlist of the bitstream the firmware
    /// sends, exact, fast-forwarded, and slower than the hand models
    /// (docs/emulator.md, "The boards").
    #[arg(long, value_name = "DESIGN")]
    fpga_design: Option<String>,
    /// With `--core mc683xx`, the synthetic engine in place of the board
    /// file's: an Oncore, `b1121`, `b4121`, `b4121-no-time-raim` or
    /// `r5122`, the UT Plus, or a Furuno, `gt-74`,
    /// `gt-74-without-time-raim` or `gt-74-pulse-early`; `b4121` puts a
    /// VP on the 58503B (docs/emulator.md, "The synthetic GPS engine").
    #[arg(long, value_name = "MODEL")]
    gps_engine: Option<String>,
    /// The synthetic engine's latitude, degrees, north positive.
    #[arg(
        long,
        default_value_t = f64::from(FIRMWARE_DEFAULT.latitude.0) / MAS_PER_DEGREE,
        allow_negative_numbers = true,
        value_parser = range::parse_latitude
    )]
    gps_latitude: f64,
    /// Its longitude, degrees, east positive.
    #[arg(
        long,
        default_value_t = f64::from(FIRMWARE_DEFAULT.longitude.0) / MAS_PER_DEGREE,
        allow_negative_numbers = true,
        value_parser = range::parse_longitude
    )]
    gps_longitude: f64,
    /// Its height above mean sea level, meters.
    #[arg(
        long,
        default_value_t = f64::from(FIRMWARE_DEFAULT.mean_sea_level.0) / CENTIMETERS_PER_METER,
        allow_negative_numbers = true,
        value_parser = range::parse_height
    )]
    gps_height: f64,
    /// UTC at power-up, RFC 3339 (`2026-10-22T12:00:00Z`), to the
    /// second, from the GPS epoch to the end of 2077; the host's clock
    /// without it.
    #[arg(long, value_parser = range::parse_start)]
    gps_start: Option<jiff::Timestamp>,
    /// GPS time less UTC for the synthetic engine, seconds: the leap
    /// seconds since 1980.
    #[arg(
        long,
        value_name = "N",
        default_value_t = GPS_LESS_UTC,
        value_parser = clap::value_parser!(i8).range(0..)
    )]
    gps_leap_seconds: i8,
    /// The seed of the board's modeled noise, the ADC's readings and the
    /// oscillator's: the same seed gives the same noise.
    #[arg(long, default_value_t = NOISE_SEED)]
    seed: u64,
    /// The oscillator's fractional frequency offset at the EFC's middle,
    /// past its warm-up (docs/emulator.md, "The oscillator"); without
    /// it, and each `--ocxo-*` below, the board file's term, or with
    /// none the firmware's own gain, the model's, the offset that puts
    /// the oscillator on frequency at its EEPROM's lifetime record's EFC
    /// on mc683xx, 0 on Unicorn (docs/emulator.md, "The oscillator the
    /// EEPROM describes"), and the other terms 0.
    #[arg(long, value_name = "Y", allow_negative_numbers = true)]
    ocxo_offset: Option<f64>,
    /// Its aging, fractional frequency a day.
    #[arg(long, value_name = "Y", allow_negative_numbers = true)]
    ocxo_aging: Option<f64>,
    /// Its warm-up's offset at power-up, falling as the oven settles.
    #[arg(long, value_name = "Y", allow_negative_numbers = true)]
    ocxo_warm_up: Option<f64>,
    /// Its EFC's gain, fractional frequency an EFC count.
    #[arg(long, value_name = "Y", allow_negative_numbers = true)]
    ocxo_efc_gain: Option<f64>,
    /// Its white frequency noise, the spread of each second's frequency.
    #[arg(long, value_name = "Y")]
    ocxo_white_fm: Option<f64>,
    /// Its random walk of frequency, the spread of each second's step.
    #[arg(long, value_name = "Y")]
    ocxo_random_walk_fm: Option<f64>,
    /// Start every power-up, the run's and each power cycle's, at the
    /// warm-up's settled end, as the skip-warm-up control puts it.
    #[arg(long)]
    skip_warm_up: bool,
    /// Send `:PTIMe:TCODe:CONTinuous OFF` on the host port once each
    /// power-up's time code starts, as a client does first, so that the
    /// port answers: the Z3816A's alone, whose image starts the time
    /// code at every power-up (docs/emulator.md, "The boards").
    #[arg(long)]
    time_code_off: bool,
    /// Stop once this many seconds of the board's time have run, and
    /// say when and, if counted, after how many instructions.
    #[arg(long, value_name = "SECONDS")]
    stop_after: Option<f64>,
    /// Report the lamps and the LEDs on standard error: what is lit at
    /// power-up, and each change after.
    #[arg(long)]
    indicators: bool,
    /// Trace, on standard error, a record a line: where the CPU is ten
    /// times a second of the board's time, every device access, every
    /// exception; comma-separated, or given more than once.
    #[arg(long, value_enum, value_delimiter = ',')]
    trace: Vec<Traced>,
    /// Trace each access to a device address, hexadecimal; may be given
    /// more than once.
    #[arg(long, value_parser = address)]
    watch: Vec<u32>,
    /// Trace each time the instruction at an address, hexadecimal,
    /// runs, with the registers and the top of the stack; may be given
    /// more than once.
    #[arg(long, value_parser = address)]
    watch_code: Vec<u32>,
}

/// An address as `--watch` takes it: hexadecimal, `0x` or not.
fn address(text: &str) -> Result<u32, std::num::ParseIntError> {
    u32::from_str_radix(text.trim_start_matches("0x"), 16)
}

impl RunOptions {
    /// `--board`, if it is given.
    pub fn board(&self) -> Option<&Path> {
        self.board.as_deref()
    }

    /// The run of the full flash image `image`, read from `image_path`,
    /// its EEPROM's cells kept as `eeprom` says, with these options set,
    /// its board file `--board` or the model's in `boards` (the
    /// repository's natively, `config::REPOSITORY_BOARDS`), the synthetic
    /// engine's start `--gps-start` or the host's clock's reading `now`:
    /// every other choice the command line's default, for the host to
    /// set, then check.
    ///
    /// # Errors
    ///
    /// The image not a full flash's, or naming no model and none given;
    /// a start the engine does not take; a stop that is no span of time.
    pub fn config(
        self,
        image_path: PathBuf,
        image: Vec<u8>,
        eeprom: Eeprom,
        boards: &Path,
        now: jiff::Timestamp,
    ) -> Result<RunConfig, ConfigError> {
        let start = self.gps_start.unwrap_or(now);
        let mut config = RunConfig::new(image_path, image, self.model, eeprom, start)?;
        config.board = self
            .board
            .unwrap_or_else(|| boards.join(config.model.board_file_for(&config.image)));
        config.synthetic.latitude = self.gps_latitude;
        config.synthetic.longitude = self.gps_longitude;
        config.synthetic.height = self.gps_height;
        config.synthetic.leap_seconds = self.gps_leap_seconds;
        config.indicators = self.indicators;
        config.seed = self.seed;
        config.ocxo = OcxoTerms {
            offset: self.ocxo_offset,
            aging: self.ocxo_aging,
            warm_up: self.ocxo_warm_up,
            efc_gain: self.ocxo_efc_gain,
            white_fm: self.ocxo_white_fm,
            random_walk_fm: self.ocxo_random_walk_fm,
        };
        config.skip_warm_up = self.skip_warm_up;
        config.time_code_off = self.time_code_off;
        config.until = self.stop_after.map(StopAfter::new).transpose()?;
        config.fpga_design = self.fpga_design;
        config.gps_engine = self.gps_engine;
        config.trace = TraceConfig {
            traced: self.trace,
            watch: self.watch,
            watch_code: self.watch_code,
            count_instructions: false,
        };
        Ok(config)
    }
}
