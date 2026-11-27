//! The command line, as clap parses it, and the run it asks for made of
//! it and checked as a whole (`config`).

use std::path::Path;
use std::path::PathBuf;

use anyhow::Context as _;
use anyhow::Result;
use clap::Parser;
use smartclock_emu::config::Core;
use smartclock_emu::config::Eeprom;
use smartclock_emu::config::GpsSource;
use smartclock_emu::config::HostPort;
use smartclock_emu::config::REPOSITORY_BOARDS;
use smartclock_emu::config::RunConfig;
use smartclock_emu::options::RunOptions;
use smartclock_emu::run::Pacing;

/// Boot a SmartClock flash image in an emulator: the host port, DUART
/// channel B, where SCPI and the pForth console are spoken, on standard
/// input and output, or over TCP with `--host`; the SCI, which sends
/// the ES message, over TCP.  DUART channel A, the GPS engine's port,
/// has a synthetic Oncore on it, or with `--gps` is served too.
#[derive(Debug, Parser)]
#[command(version = smartclock::VERSION)]
pub(crate) struct Cli {
    /// The full 512 KiB flash image, as `read-flash` writes it.
    image: PathBuf,
    /// The CPU core: mc683xx's MC68331, on the receiver's board file,
    /// the default; or Unicorn, on smartclock's own map of the board,
    /// for development, with the `unicorn` feature.
    #[arg(long, value_enum, default_value_t = Core::DEFAULT)]
    core: Core,
    /// The EEPROM's working file, which every write goes through to;
    /// made from `--eeprom-dump` if it does not exist, its directory
    /// too.
    #[arg(long)]
    eeprom: PathBuf,
    /// A dump of a real EEPROM to start the working file from; without
    /// it, a missing working file starts erased, or with `--core
    /// mc683xx` from the board file's dump.
    #[arg(long)]
    eeprom_dump: Option<PathBuf>,
    /// Serve the host port, DUART channel B (SCPI, and the console after
    /// `:SYSTem:LANGuage "PFORTH"`), over TCP here, in place of standard
    /// input and output, or with `--panel` of the page's console alone.
    #[arg(long, conflicts_with = "script")]
    host: Option<String>,
    /// Run the host port from this script, its input timed by the
    /// board's clock, in place of standard input; what the port sends
    /// goes to standard output (docs/emulator.md, "Scripts").
    #[arg(long, value_name = "FILE")]
    script: Option<PathBuf>,
    /// Where to serve the SCI, the second port: 127.0.0.1:5026 unless
    /// given; refused on a board file without an `sci` endpoint.
    #[arg(long, value_name = "ADDR")]
    sci: Option<String>,
    /// Write every byte the SCI sends to this file, raw, as it is sent,
    /// whoever is connected; refused as `--sci` is.
    #[arg(long, value_name = "FILE")]
    sci_capture: Option<PathBuf>,
    /// Serve DUART channel A, the GPS engine's port, here, for an
    /// external engine or a recording, in place of the synthetic Oncore.
    #[arg(long)]
    gps: Option<String>,
    /// The deterministic mode: run flat out, the board's time never held
    /// back to the wall clock's.
    #[arg(long)]
    deterministic: bool,
    /// Copy what each port sends to standard error, a line at a time.
    #[arg(long)]
    tap: bool,
    /// Serve a page here that draws the receiver's front panel as the
    /// run lights it and presses the keys clicked on it: an address, or
    /// a port alone on 127.0.0.1; the mc683xx core only
    /// (docs/emulator.md, "The panel page").  Without `--host` or
    /// `--script`, its console has the host port, in place of standard
    /// input and output.
    #[arg(long, value_name = "ADDR")]
    panel: Option<String>,
    /// On the Unicorn core, count the instructions run, for the position
    /// records and the stop; it slows the core, by as much as a
    /// `--stop-after` run with and without it shows.  Refused on
    /// mc683xx, which counts them always.
    #[arg(long)]
    count_instructions: bool,
    /// The options every host takes.
    #[command(flatten)]
    run: RunOptions,
}

/// The run `cli` asks for: the image read, a full flash's, its model
/// the one it names unless `--model` gives one, checked as a whole.
pub(crate) fn config(cli: Cli) -> Result<RunConfig> {
    let image =
        std::fs::read(&cli.image).with_context(|| format!("reading {}", cli.image.display()))?;
    let eeprom = Eeprom::File {
        path: cli.eeprom,
        dump: cli.eeprom_dump,
    };
    let board = cli.run.board().map(Path::to_owned);
    let mut config = cli.run.config(
        cli.image,
        image,
        eeprom,
        Path::new(REPOSITORY_BOARDS),
        jiff::Timestamp::now(),
    )?;
    config.core = cli.core;
    if let Some(board) = board {
        config.board = board;
    }
    config.host = match (cli.script, cli.host) {
        (Some(path), _) => HostPort::Script(path),
        (None, Some(address)) => HostPort::Tcp(address),
        (None, None) if cli.panel.is_some() => HostPort::Panel,
        (None, None) => HostPort::Stdio,
    };
    config.sci = cli.sci;
    config.sci_capture = cli.sci_capture;
    config.gps = cli.gps.map_or(GpsSource::Synthetic, GpsSource::External);
    config.tap = cli.tap;
    config.pacing = if cli.deterministic {
        Pacing::Free
    } else {
        Pacing::Wall
    };
    config.panel = cli.panel;
    config.trace.count_instructions = cli.count_instructions;
    config.check()?;
    Ok(config)
}

#[cfg(test)]
mod tests {
    use clap::Parser;
    use smartclock_emu::config::Eeprom;
    use smartclock_emu::config::GpsSource;
    use smartclock_emu::config::HostPort;
    use smartclock_emu::config::RunConfig;

    use super::Cli;

    /// The repository's Z3805A image.
    const IMAGE: &str = concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../../third_party/firmware/images/z3805a-3543b.bin"
    );

    /// The run of `arguments` after the image and an EEPROM.
    fn config(arguments: &[&str]) -> anyhow::Result<RunConfig> {
        let fixed = ["smartclock-emu", IMAGE, "--eeprom", "unused.bin"];
        super::config(Cli::parse_from(fixed.iter().chain(arguments)))
    }

    #[test]
    fn the_host_port_is_the_scripts_the_servers_the_pages_or_standard_inputs() {
        for (arguments, host) in [
            (&["--script", "s"][..], HostPort::Script("s".into())),
            (
                &["--host", "a", "--panel", "p"],
                HostPort::Tcp("a".to_owned()),
            ),
            (&["--panel", "p"], HostPort::Panel),
            (&[], HostPort::Stdio),
        ] {
            assert_eq!(
                config(arguments).expect("a run").host,
                host,
                "{arguments:?}"
            );
        }
    }

    #[test]
    fn a_run_stops_after_a_span_of_time_and_no_other() {
        assert!(
            config(&["--stop-after", "1.5"])
                .expect("a run")
                .until
                .is_some()
        );
        for seconds in ["inf", "NaN"] {
            let error = config(&["--stop-after", seconds]).expect_err(seconds);
            assert!(error.to_string().contains("--stop-after"), "{error}");
        }
    }

    #[test]
    fn count_instructions_is_refused_on_mc683xx_which_counts_them_always() {
        let error = config(&["--count-instructions"]).expect_err("refused");
        assert!(error.to_string().contains("mc683xx"), "{error}");
        assert!(
            config(&["--core", "unicorn", "--count-instructions"])
                .expect("taken")
                .trace
                .count_instructions
        );
    }

    #[test]
    fn gps_names_an_external_engine() {
        assert_eq!(config(&[]).expect("a run").gps, GpsSource::Synthetic);
        assert_eq!(
            config(&["--gps", "127.0.0.1:0"]).expect("a run").gps,
            GpsSource::External("127.0.0.1:0".to_owned())
        );
    }

    #[test]
    fn the_command_line_refuses_a_place_or_a_start_the_oncore_cannot_give() {
        for (option, value) in [
            ("--gps-start", "9999-01-01T00:00:00Z"),
            ("--gps-latitude", "91"),
            ("--gps-longitude", "-180.5"),
            ("--gps-height", "-1000"),
        ] {
            let parsed = Cli::try_parse_from(["smartclock-emu", "image.bin", option, value]);
            let error = parsed.map(|_| ()).expect_err(option).to_string();
            assert!(error.contains("VPCommands.pdf"), "{error}");
        }
    }

    /// The command line's defaults are a run's own.
    #[test]
    fn the_command_lines_defaults_are_a_runs() {
        let given = config(&["--gps-start", "2026-10-22T12:00:00Z"]).expect("a run");
        let image = std::fs::read(IMAGE).expect("the image");
        let start = "2026-10-22T12:00:00Z".parse().expect("a time");
        let eeprom = Eeprom::File {
            path: "unused.bin".into(),
            dump: None,
        };
        let default = RunConfig::new(IMAGE.into(), image, None, eeprom, start).expect("a run");
        assert_eq!(format!("{given:?}"), format!("{default:?}"));
    }
}
