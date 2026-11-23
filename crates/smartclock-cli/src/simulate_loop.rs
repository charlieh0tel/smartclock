//! `simulate-loop`: what TI and EFC the disciplining loop should give
//! for a disturbance, beside what a logged receiver gave.
//!
//! The loop is `pll_normal`'s, `smartclock::discipline`'s law at an
//! image's τ and G from `docs/firmware/loop-constants.json`, closed
//! around an oscillator of a given sensitivity.  It starts at rest, its
//! time interval 0, and runs no `startup_pll`, holdover or aging fit;
//! an aging slope may be fed to its integrator as a constant.

use std::fs;
use std::io::Write;
use std::path::Path;
use std::path::PathBuf;

use anyhow::Context as _;
use anyhow::Result;
use anyhow::bail;
use clap::Args;
use jiff::Timestamp;
use serde_json::Value;
use smartclock::discipline::ClosedLoop;
use smartclock::discipline::Disturbance;
use smartclock::discipline::ENTRY_SECONDS;
use smartclock::discipline::Update;
use smartclock::types::EfcPercent;

/// The loop's constants by image, which `make check-loop-constants`
/// holds to the images.
const CONSTANTS: &str = include_str!("../../../docs/firmware/loop-constants.json");

/// Seconds in a day, for rates given a day.
const DAY: f64 = 86_400.0;

/// Nanoseconds in a second.
const NANOSECONDS: f64 = 1e9;

/// How far before an update the latest logged row may be and still
/// stand beside it, seconds: a minute without one is a gap.
const LOG_GAP: f64 = 60.0;

/// EFC counts in one percent of the EFC's range: −100 % to +100 % is
/// the 20-bit range (`EfcPercent::from_raw`).
fn counts_per_percent() -> f64 {
    f64::from(EfcPercent::FULL_SCALE) / 200.0
}

/// The bottom of the EFC's range, percent: count 0.
const EFC_BOTTOM_PERCENT: f64 = -100.0;

/// An EFC in percent as counts.
fn counts(percent: f64) -> f64 {
    (percent - EFC_BOTTOM_PERCENT) * counts_per_percent()
}

/// An EFC in counts as percent.
fn percent(counts: f64) -> f64 {
    counts / counts_per_percent() + EFC_BOTTOM_PERCENT
}

#[derive(Args)]
pub(crate) struct SimulateLoopArgs {
    /// The image whose τ and G to take, as `docs/firmware/loop-constants.json`
    /// names it, such as `58503a-3704`.
    #[arg(long)]
    image: Option<String>,
    /// τ, seconds, in place of the image's; needed when it has more
    /// than one.
    #[arg(long)]
    tau: Option<f64>,
    /// G, fractional frequency an EFC count, in place of the image's;
    /// needed when it has more than one.
    #[arg(long)]
    gain: Option<f64>,
    /// The oscillator's sensitivity, fractional frequency an EFC
    /// percent, in G's sign: the time interval moves by −S·ΔEFC a
    /// second.  By default G's.
    #[arg(long)]
    sensitivity: Option<f64>,
    /// The EFC at the start, percent, where the undisturbed oscillator
    /// is on frequency.
    #[arg(long, default_value_t = 0.0, allow_negative_numbers = true)]
    efc: f64,
    /// An aging slope fed to the integrator, EFC percent a day,
    /// standing in for the aging fit's; clamped to ±M as the firmware
    /// clamps it.
    #[arg(long, default_value_t = 0.0, allow_negative_numbers = true)]
    aging_slope: f64,
    /// Seconds to simulate.
    #[arg(long)]
    seconds: u32,
    /// A phase step, `AT:NANOSECONDS`, at AT seconds.  May be repeated.
    #[arg(long = "phase-step", value_parser = timed, allow_negative_numbers = true)]
    phase_steps: Vec<(u32, f64)>,
    /// A step of the free-running fractional frequency, `AT:SIZE`, at
    /// AT seconds.  May be repeated, for a list of steps.
    #[arg(long = "frequency-step", value_parser = timed, allow_negative_numbers = true)]
    frequency_steps: Vec<(u32, f64)>,
    /// A linear drift of the free-running fractional frequency,
    /// `AT:RATE`, RATE a day from AT seconds on.  May be repeated.
    #[arg(long, value_parser = timed, allow_negative_numbers = true)]
    drift: Vec<(u32, f64)>,
    /// A snapshot log as TSV with columns `at` (nanoseconds since the
    /// Unix epoch), `time_interval_s` and `efc_percent`, whose latest
    /// row at each update is printed beside it.  Needs `--from`.
    #[arg(long, requires = "from")]
    log: Option<PathBuf>,
    /// The log's time at the simulation's second 0, RFC 3339.
    #[arg(long, requires = "log")]
    from: Option<Timestamp>,
}

/// An `AT:VALUE`: seconds and a number.
fn timed(text: &str) -> Result<(u32, f64), String> {
    let (at, value) = text
        .split_once(':')
        .ok_or_else(|| format!("{text} is not AT:VALUE"))?;
    let at = at
        .parse()
        .map_err(|e| format!("{at} is not a second: {e}"))?;
    let value: f64 = value
        .parse()
        .map_err(|e| format!("{value} is not a number: {e}"))?;
    if !value.is_finite() {
        return Err(format!("{value} is not finite"));
    }
    Ok((at, value))
}

/// A JSON string's text, or nothing.
fn text(value: &Value) -> &str {
    value.as_str().unwrap_or_default()
}

/// The one value of an image's `name` in the constants, or the
/// override; an error naming the choices if there is more than one.
fn constant(image: &Value, name: &str, flag: &str, given: Option<f64>) -> Result<f64> {
    if let Some(value) = given {
        return Ok(value);
    }
    let values: Vec<f64> = image[name]["values"]
        .as_array()
        .into_iter()
        .flatten()
        .filter_map(|entry| entry["value"].as_f64())
        .collect();
    match values[..] {
        [value] => Ok(value),
        [] => bail!("{} holds no {name}; give {flag}", text(&image["image"])),
        _ => bail!(
            "{} holds {name} {values:?}, {}; give {flag}",
            text(&image["image"]),
            text(&image[name]["by"]["text"])
        ),
    }
}

/// τ and G, from the image and the overrides.
fn tau_and_gain(args: &SimulateLoopArgs) -> Result<(f64, f64)> {
    let Some(name) = &args.image else {
        return match (args.tau, args.gain) {
            (Some(tau), Some(gain)) => Ok((tau, gain)),
            _ => bail!("give --image, or both --tau and --gain"),
        };
    };
    let constants: Value = serde_json::from_str(CONSTANTS).context("loop-constants.json")?;
    let images = constants["images"].as_array().into_iter().flatten();
    let names: Vec<&str> = images
        .clone()
        .filter_map(|image| image["image"].as_str())
        .collect();
    let Some(image) = images
        .into_iter()
        .find(|image| image["image"].as_str() == Some(name))
    else {
        bail!("no image {name}; the images are {}", names.join(", "));
    };
    Ok((
        constant(image, "tau", "--tau", args.tau)?,
        constant(image, "gain", "--gain", args.gain)?,
    ))
}

/// The disturbances the arguments give, in the library's units.
fn disturbances(args: &SimulateLoopArgs) -> Vec<Disturbance> {
    let phase = args
        .phase_steps
        .iter()
        .map(|&(at, size)| Disturbance::PhaseStep {
            at,
            size: size / NANOSECONDS,
        });
    let frequency = args
        .frequency_steps
        .iter()
        .map(|&(at, size)| Disturbance::FrequencyStep { at, size });
    let drift = args.drift.iter().map(|&(at, rate)| Disturbance::Drift {
        at,
        rate: rate / DAY,
    });
    phase.chain(frequency).chain(drift).collect()
}

/// A logged row: when, and its time interval and EFC where it had
/// them.
#[derive(Debug, Clone, Copy, PartialEq)]
struct Logged {
    /// Seconds since the Unix epoch.
    at: f64,
    /// Seconds.
    interval: Option<f64>,
    /// Percent.
    efc: Option<f64>,
}

/// The rows of a snapshot TSV, in its order.
fn read_log(path: &Path) -> Result<Vec<Logged>> {
    let text = fs::read_to_string(path).with_context(|| format!("reading {}", path.display()))?;
    let mut lines = text.lines();
    let header: Vec<&str> = lines.next().unwrap_or_default().split('\t').collect();
    let column = |name: &str| {
        header
            .iter()
            .position(|cell| *cell == name)
            .with_context(|| format!("{} has no {name} column", path.display()))
    };
    let (at, interval, efc) = (
        column("at")?,
        column("time_interval_s")?,
        column("efc_percent")?,
    );
    lines
        .enumerate()
        .map(|(index, line)| {
            let cells: Vec<&str> = line.split('\t').collect();
            let number = |column: usize| -> Result<Option<f64>> {
                match cells.get(column).copied().unwrap_or_default() {
                    "" => Ok(None),
                    cell => cell
                        .parse()
                        .map(Some)
                        .with_context(|| format!("{} line {}: {cell}", path.display(), index + 2)),
                }
            };
            let nanoseconds: i64 = cells
                .get(at)
                .copied()
                .unwrap_or_default()
                .parse()
                .with_context(|| format!("{} line {}: at", path.display(), index + 2))?;
            #[expect(
                clippy::cast_precision_loss,
                reason = "microseconds are plenty beside a 10 s update"
            )]
            let at = nanoseconds as f64 / NANOSECONDS;
            Ok(Logged {
                at,
                interval: number(interval)?,
                efc: number(efc)?,
            })
        })
        .collect()
}

/// The latest row at or before `at` and within [`LOG_GAP`] of it.
fn latest(rows: &[Logged], at: f64) -> Option<Logged> {
    let after = rows.partition_point(|row| row.at <= at);
    after
        .checked_sub(1)
        .map(|index| rows[index])
        .filter(|row| at - row.at <= LOG_GAP)
}

/// A cell: the value to `places` decimals, or empty.
fn cell(value: Option<f64>, places: usize) -> String {
    value.map_or_else(String::new, |value| format!("{value:.places$}"))
}

pub(crate) fn run(args: &SimulateLoopArgs) -> Result<()> {
    let (tau, gain) = tau_and_gain(args)?;
    let per_percent = counts_per_percent();
    let sensitivity = args.sensitivity.map_or(gain, |s| s / per_percent);
    let slope = args.aging_slope * per_percent * ENTRY_SECONDS / DAY;
    let mut closed = ClosedLoop::new(gain, tau, sensitivity, counts(args.efc), slope);
    let updates = closed.run(&disturbances(args), args.seconds);
    let log = match (&args.log, args.from) {
        (Some(path), Some(from)) => {
            let mut rows = read_log(path)?;
            rows.sort_by(|a, b| a.at.total_cmp(&b.at));
            Some((rows, from.as_millisecond()))
        }
        _ => None,
    };
    let mut out = std::io::stdout().lock();
    write!(out, "second\tti_ns\tefc_percent")?;
    if log.is_some() {
        write!(out, "\tlogged_ti_ns\tlogged_efc_percent")?;
    }
    writeln!(out)?;
    for Update {
        second,
        interval,
        efc,
    } in updates
    {
        write!(
            out,
            "{second}\t{:.3}\t{:.5}",
            interval * NANOSECONDS,
            percent(efc)
        )?;
        if let Some((rows, from)) = &log {
            #[expect(clippy::cast_precision_loss, reason = "milliseconds since 1970")]
            let at = *from as f64 / 1e3 + f64::from(second);
            let row = latest(rows, at);
            write!(
                out,
                "\t{}\t{}",
                cell(
                    row.and_then(|row| row.interval)
                        .map(|seconds| seconds * NANOSECONDS),
                    3
                ),
                cell(row.and_then(|row| row.efc), 5)
            )?;
        }
        writeln!(out)?;
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use serde_json::Value;
    use smartclock::discipline::BLOCK_SECONDS;
    use smartclock::discipline::FILTER_SCALE;
    use smartclock::discipline::INTEGRAL_SCALE;

    use super::CONSTANTS;
    use super::Logged;
    use super::latest;
    use super::timed;

    #[test]
    fn the_constants_law_is_the_library_s() {
        let constants: Value = serde_json::from_str(CONSTANTS).expect("the constants");
        let law = &constants["law"];
        assert_eq!(law["prefilter"].as_f64(), Some(FILTER_SCALE));
        assert_eq!(law["integral_scale"].as_f64(), Some(INTEGRAL_SCALE));
        assert_eq!(
            law["update_seconds"].as_u64(),
            Some(u64::from(BLOCK_SECONDS))
        );
    }

    #[test]
    fn a_timed_value_is_seconds_and_a_number() {
        assert_eq!(timed("600:-1e-9"), Ok((600, -1e-9)));
        assert!(timed("600").is_err());
        assert!(timed("-1:2").is_err());
        assert!(timed("1:NaN").is_err());
    }

    #[test]
    fn the_latest_row_stands_beside_an_update_until_a_gap() {
        let row = |at| Logged {
            at,
            interval: Some(at),
            efc: None,
        };
        let rows = [row(0.0), row(10.0)];
        assert_eq!(latest(&rows, 9.0), Some(row(0.0)));
        assert_eq!(latest(&rows, 10.0), Some(row(10.0)));
        assert_eq!(latest(&rows, 100.0), None);
        assert_eq!(latest(&rows, -1.0), None);
    }
}
