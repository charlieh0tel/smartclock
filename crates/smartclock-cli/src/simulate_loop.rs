//! `simulate-loop`: what TI and EFC the disciplining loop should give
//! for a disturbance, beside what a logged receiver gave.
//!
//! The loop is `pll_normal`'s, `smartclock::discipline`'s law at an
//! image's τ and G from `docs/firmware/loop-constants.json`, closed
//! around an oscillator of a given sensitivity.  It starts at rest, or
//! at a given time interval, or where a logged run stood, and runs no
//! `startup_pll` or holdover; its integrator is fed an aging slope, a
//! constant, a given fit's, or the 58503A 3704's aging fit's as it
//! learns.  A replay drives it with what a logged
//! run's oscillator did, recovered from the log's time interval and
//! EFC (`docs/firmware/loop.md`, "Simulating it").

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
use smartclock::aging::Mode;
use smartclock::discipline::BLOCK_SECONDS;
use smartclock::discipline::ClosedLoop;
use smartclock::discipline::Disturbance;
use smartclock::discipline::ENTRY_SECONDS;
use smartclock::discipline::Reference;
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

/// The one image whose aging fit `smartclock::aging` transcribes.
const FIT_IMAGE: &str = "58503a-3704";

/// The FFOM of `normal pll` ("The FFOM"), the one stage a replay runs.
const NORMAL_FFOM: f64 = 0.0;

/// The prefilter's time constants, 1/a updates each, over which a
/// replay runs it on the log before its start.
const WARM_UP: f64 = 5.0;

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
    #[arg(
        long,
        default_value_t = 0.0,
        allow_negative_numbers = true,
        conflicts_with = "replay"
    )]
    efc: f64,
    /// The time interval at the start, nanoseconds, the loop running
    /// with its filtered error there and its EFC at `--efc`.
    #[arg(
        long,
        default_value_t = 0.0,
        allow_negative_numbers = true,
        conflicts_with = "replay"
    )]
    ti: f64,
    /// c, `:DIAGnostic:ROSCillator:TCOefficient?`, EFC counts a unit of
    /// oscillator current, for the update's c·s term.  A replay takes
    /// the log's `oven_tempco` by default.
    #[arg(long, allow_negative_numbers = true)]
    tc_coefficient: Option<f64>,
    /// s, the oscillator current, held constant.  A replay with c not 0
    /// takes the log's `oven_current` by default.
    #[arg(long, allow_negative_numbers = true)]
    oven_current: Option<f64>,
    /// An aging slope fed to the integrator, EFC percent a day,
    /// standing in for the aging fit's; clamped to ±M as the firmware
    /// clamps it.
    #[arg(
        long,
        default_value_t = 0.0,
        allow_negative_numbers = true,
        conflicts_with_all = ["fit_b", "fit_c"]
    )]
    aging_slope: f64,
    /// The aging fit's b the loop holds at the start, r, EFC counts a
    /// 2700 s window, as the `curv` debug line's `b=` prints it.
    #[arg(long, allow_negative_numbers = true)]
    fit_b: Option<f64>,
    /// The aging fit's c the loop holds at the start, p, as the `curv`
    /// debug line's `c=` prints it: d is p·2700/q + r.  Needs
    /// `--uptime`.
    #[arg(long, allow_negative_numbers = true, requires = "uptime")]
    fit_c: Option<f64>,
    /// q, the receiver's uptime at the start, seconds.
    #[arg(long)]
    uptime: Option<u32>,
    /// Runs the 58503A 3704's aging fit from an empty history, as after
    /// the fit is cleared, its fits replacing r and p as they come.
    /// Needs `--image 58503a-3704` and `--uptime`.
    #[arg(long, requires = "uptime")]
    aging_fit: bool,
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
    /// Replays the log from `--from`: starts where it stood and drives
    /// the loop with what its oscillator did.
    #[arg(long, requires = "log")]
    replay: bool,
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

/// A logged row: when, and what it held of the columns the simulator
/// reads.
#[derive(Debug, Clone, Copy, PartialEq)]
struct Logged {
    /// Seconds since the Unix epoch.
    at: f64,
    /// `time_interval_s`, seconds.
    interval: Option<f64>,
    /// `efc_percent`, percent.
    efc: Option<f64>,
    /// `oven_current`, the fresh reading of the oscillator current.
    current: Option<f64>,
    /// `oven_tempco`, c, counts a unit of oscillator current.
    coefficient: Option<f64>,
    /// `ffom`.
    ffom: Option<f64>,
}

/// The rows of a snapshot TSV, in its order.  `at`, `time_interval_s`
/// and `efc_percent` are required; `oven_current`, `oven_tempco` and
/// `ffom` are read where the log has them.
fn read_log(path: &Path) -> Result<Vec<Logged>> {
    let text = fs::read_to_string(path).with_context(|| format!("reading {}", path.display()))?;
    let mut lines = text.lines();
    let header: Vec<&str> = lines.next().unwrap_or_default().split('\t').collect();
    let optional = |name: &str| header.iter().position(|cell| *cell == name);
    let column = |name: &str| {
        optional(name).with_context(|| format!("{} has no {name} column", path.display()))
    };
    let (at, interval, efc) = (
        column("at")?,
        column("time_interval_s")?,
        column("efc_percent")?,
    );
    let (current, coefficient, ffom) = (
        optional("oven_current"),
        optional("oven_tempco"),
        optional("ffom"),
    );
    lines
        .enumerate()
        .map(|(index, line)| {
            let cells: Vec<&str> = line.split('\t').collect();
            let number = |column: Option<usize>| -> Result<Option<f64>> {
                match column.and_then(|column| cells.get(column).copied()) {
                    None | Some("") => Ok(None),
                    Some(cell) => cell
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
                interval: number(Some(interval))?,
                efc: number(Some(efc))?,
                current: number(current)?,
                coefficient: number(coefficient)?,
                ffom: number(ffom)?,
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

/// The latest value of a column at or before `at`, however old.
fn last(rows: &[Logged], at: f64, value: impl Fn(&Logged) -> Option<f64>) -> Option<f64> {
    rows[..rows.partition_point(|row| row.at <= at)]
        .iter()
        .rev()
        .find_map(value)
}

/// What a replay reads from the log at second `second` of the run: the
/// latest row's time interval and EFC, within [`LOG_GAP`], in a stage
/// the replay models.
fn logged_state(rows: &[Logged], from: f64, second: u32) -> Result<(f64, f64)> {
    let at = from + f64::from(second);
    let row = latest(rows, at);
    let (Some(interval), Some(efc)) = (
        row.and_then(|row| row.interval),
        row.and_then(|row| row.efc),
    ) else {
        bail!(
            "the log has no time interval and EFC within {LOG_GAP} s before second {second} of \
             the replay; it cannot cross a gap"
        );
    };
    if let Some(ffom) = row.and_then(|row| row.ffom)
        && ffom != NORMAL_FFOM
    {
        bail!(
            "the log's FFOM is {ffom} at second {second} of the replay; it replays pll_normal only"
        );
    }
    Ok((interval, efc))
}

/// The updates [`BLOCK_SECONDS`] apart from `start`, `count` of them,
/// as the log saw them: at each, the latest row within [`LOG_GAP`] if
/// it is `usable` and has a time interval, with f, the prefilter run
/// over the time intervals from the first of a stretch without a gap,
/// where it starts at that update's.
fn walk(
    rows: &[Logged],
    start: f64,
    count: u32,
    weight: f64,
    usable: impl Fn(&Logged) -> bool,
) -> Vec<Option<(Logged, f64)>> {
    let seconds = f64::from(BLOCK_SECONDS);
    let mut filtered: Option<f64> = None;
    (0..count)
        .map(|index| {
            let row = latest(rows, start + f64::from(index) * seconds).filter(&usable);
            filtered = row.and_then(|row| row.interval).map(|interval| {
                filtered.map_or(interval, |f| (1.0 - weight) * f + weight * interval)
            });
            row.zip(filtered)
        })
        .collect()
}

/// f, the prefilter's state at `from`: the filter run over the log's
/// time intervals at the updates of the [`WARM_UP`] time constants
/// before it, from the first of a stretch without a gap.
fn warm_up(rows: &[Logged], from: f64, weight: f64) -> Option<f64> {
    let blocks = warm_up_blocks(weight);
    let start = from - f64::from(blocks) * f64::from(BLOCK_SECONDS);
    walk(rows, start, blocks + 1, weight, |_| true)
        .last()
        .copied()
        .flatten()
        .map(|(_, filtered)| filtered)
}

/// The updates in [`WARM_UP`] time constants of the prefilter.
fn warm_up_blocks(weight: f64) -> u32 {
    #[expect(
        clippy::cast_possible_truncation,
        clippy::cast_sign_loss,
        reason = "a = 29.75 / τ gives a few hundred updates"
    )]
    let blocks = (WARM_UP / weight).ceil() as u32;
    blocks
}

/// A cell: the value to `places` decimals, or empty.
fn cell(value: Option<f64>, places: usize) -> String {
    value.map_or_else(String::new, |value| format!("{value:.places$}"))
}

/// Seconds since the Unix epoch.
fn seconds(at: Timestamp) -> f64 {
    #[expect(clippy::cast_precision_loss, reason = "milliseconds since 1970")]
    let milliseconds = at.as_millisecond() as f64;
    milliseconds / 1e3
}

/// The loop at its start: at rest or at `--ti`, or where the log stood
/// at `from` for a replay; and for a replay, whether s is the log's.
fn start(
    args: &SimulateLoopArgs,
    loop_at: impl Fn(f64) -> ClosedLoop,
    replay: Option<(&[Logged], f64)>,
) -> Result<(ClosedLoop, bool)> {
    let Some((rows, from)) = replay else {
        let mut closed = loop_at(counts(args.efc)).with_current_term(
            args.tc_coefficient.unwrap_or_default(),
            args.oven_current.unwrap_or_default(),
        );
        let interval = args.ti / NANOSECONDS;
        closed.resume(interval, interval);
        return Ok((closed, false));
    };
    let (interval, efc) = logged_state(rows, from, 0)?;
    let coefficient = args
        .tc_coefficient
        .or_else(|| last(rows, from, |row| row.coefficient))
        .context("the log has no oven_tempco at or before --from; give --tc-coefficient")?;
    let (current, logged_current) = match args.oven_current {
        Some(current) => (current, false),
        None if coefficient == 0.0 => (0.0, false),
        None => (
            last(rows, from, |row| row.current)
                .context("the log has no oven_current at or before --from; give --oven-current")?,
            true,
        ),
    };
    let mut closed = loop_at(counts(efc)).with_current_term(coefficient, current);
    let filtered = warm_up(rows, from, closed.gains().filter_weight).unwrap_or(interval);
    closed.resume(0.0, filtered);
    Ok((closed, logged_current))
}

/// The updates the arguments give, with the aging fit's mode at each
/// when it runs, replaying `log`, sorted, from its time at second 0 when
/// they ask for a replay.
fn simulate(
    args: &SimulateLoopArgs,
    log: Option<(&[Logged], f64)>,
) -> Result<Vec<(Update, Option<Mode>)>> {
    if args.aging_fit && args.image.as_deref() != Some(FIT_IMAGE) {
        bail!("--aging-fit runs the {FIT_IMAGE}'s fit; give --image {FIT_IMAGE}");
    }
    let (tau, gain) = tau_and_gain(args)?;
    let per_percent = counts_per_percent();
    let sensitivity = args.sensitivity.map_or(gain, |s| s / per_percent);
    let slope = args.aging_slope * per_percent * ENTRY_SECONDS / DAY;
    let replay = log.filter(|_| args.replay);
    let (closed, logged_current) = start(
        args,
        |efc| {
            ClosedLoop::new(gain, tau, sensitivity, efc, slope).with_aging(
                args.fit_b.unwrap_or(slope),
                args.fit_c.unwrap_or_default(),
                args.uptime.unwrap_or_default(),
            )
        },
        replay,
    )?;
    let mut closed = if args.aging_fit {
        closed.with_aging_fit()
    } else {
        closed
    };
    let disturbances = disturbances(args);
    (0..args.seconds / BLOCK_SECONDS)
        .map(|block| {
            let update = step(&mut closed, &disturbances, replay, logged_current, block)?;
            if let Some(error) = closed.fit_error() {
                bail!("at second {}: {error}", update.second);
            }
            Ok((update, closed.curve().map(|curve| curve.mode)))
        })
        .collect()
}

/// One block of the run, `block` from the start, against the model's
/// reference or a replayed log's.
fn step(
    closed: &mut ClosedLoop,
    disturbances: &[Disturbance],
    replay: Option<(&[Logged], f64)>,
    logged_current: bool,
    block: u32,
) -> Result<Update> {
    match replay {
        None => Ok(closed.block(disturbances)),
        Some((rows, from)) => {
            let begun = block * BLOCK_SECONDS;
            let ended = begun + BLOCK_SECONDS;
            if logged_current
                && let Some(fresh) = last(rows, from + f64::from(ended), |row| row.current)
            {
                closed.read_current(fresh);
            }
            let (_, in_force) = logged_state(rows, from, begun)?;
            let (mean, _) = logged_state(rows, from, ended)?;
            let reference = Reference {
                efc: counts(in_force),
                interval: mean,
            };
            Ok(closed.block_against(disturbances, reference))
        }
    }
}

pub(crate) fn run(args: &SimulateLoopArgs) -> Result<()> {
    let log = match (&args.log, args.from) {
        (Some(path), Some(from)) => {
            let mut rows = read_log(path)?;
            rows.sort_by(|a, b| a.at.total_cmp(&b.at));
            Some((rows, seconds(from)))
        }
        _ => None,
    };
    let updates = simulate(
        args,
        log.as_ref().map(|(rows, from)| (rows.as_slice(), *from)),
    )?;
    let fitting = args.aging_fit || args.fit_b.is_some() || args.fit_c.is_some();
    let mut out = std::io::stdout().lock();
    write!(out, "second\tti_ns\tefc_percent")?;
    if log.is_some() {
        write!(out, "\tlogged_ti_ns\tlogged_efc_percent")?;
    }
    if fitting {
        write!(out, "\taging_slope_percent_per_day")?;
    }
    if args.aging_fit {
        write!(out, "\tfit_mode")?;
    }
    writeln!(out)?;
    for (
        Update {
            second,
            interval,
            efc,
            slope,
        },
        mode,
    ) in updates
    {
        write!(
            out,
            "{second}\t{:.3}\t{:.5}",
            interval * NANOSECONDS,
            percent(efc)
        )?;
        if let Some((rows, from)) = &log {
            let row = latest(rows, from + f64::from(second));
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
        if fitting {
            write!(
                out,
                "\t{:.6}",
                slope / counts_per_percent() * DAY / ENTRY_SECONDS
            )?;
        }
        if args.aging_fit {
            write!(
                out,
                "\t{}",
                mode.map_or_else(String::new, |mode| (mode as u8).to_string())
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

    use smartclock::discipline::ClosedLoop;
    use smartclock::discipline::Disturbance;
    use smartclock::discipline::Update;

    use super::CONSTANTS;
    use super::Logged;
    use super::SimulateLoopArgs;
    use super::counts;
    use super::counts_per_percent;
    use super::latest;
    use super::percent;
    use super::simulate;
    use super::timed;

    /// The 58503A 3704's G, fractional frequency a count.
    const GAIN: f64 = 6.25e-13;

    /// The run's length, seconds.
    const SECONDS: u32 = 30_000;

    /// The bench 58503A's c, counts a unit of oscillator current.
    const COEFFICIENT: f64 = -33.65;

    /// The arguments for a run of [`SECONDS`] of the 58503A 3704, as
    /// the command line would give them.
    fn arguments() -> SimulateLoopArgs {
        SimulateLoopArgs {
            image: Some("58503a-3704".to_owned()),
            tau: None,
            gain: None,
            sensitivity: None,
            efc: 0.0,
            ti: 0.0,
            tc_coefficient: None,
            oven_current: None,
            aging_slope: 0.0,
            fit_b: None,
            fit_c: None,
            uptime: None,
            aging_fit: false,
            seconds: SECONDS,
            phase_steps: Vec::new(),
            frequency_steps: Vec::new(),
            drift: Vec::new(),
            log: None,
            from: None,
            replay: false,
        }
    }

    /// The oscillator current at a second: a swing over a few hours.
    fn current(second: u32) -> f64 {
        110.0 + 3.0 * (f64::from(second) / 5000.0).sin()
    }

    /// What the free oscillator does.
    fn disturbances() -> [Disturbance; 2] {
        [
            Disturbance::Drift {
                at: 0,
                rate: 2e-10 / 86_400.0,
            },
            Disturbance::FrequencyStep {
                at: 4000,
                size: 3e-11,
            },
        ]
    }

    /// A run of the loop applying `coefficient` to [`current`], logged
    /// a row a second as the daemon logs one: each update's time
    /// interval and EFC until the next, and that second's current.
    fn logged(coefficient: f64) -> Vec<Logged> {
        logged_with(coefficient, |closed| closed)
    }

    /// [`logged`], with the loop `aging` makes of it.
    fn logged_with(coefficient: f64, aging: impl Fn(ClosedLoop) -> ClosedLoop) -> Vec<Logged> {
        let rest = counts(30.0);
        let mut closed = aging(
            ClosedLoop::new(GAIN, 700.0, GAIN, rest, 0.0)
                .with_current_term(coefficient, current(0)),
        );
        let mut state = (0.0, rest);
        let mut rows = Vec::new();
        for second in 0..=SECONDS {
            if second > 0 && second % 10 == 0 {
                closed.read_current(current(second));
                let update = closed.block(&disturbances());
                state = (update.interval, update.efc);
            }
            rows.push(Logged {
                at: f64::from(second),
                interval: Some(state.0),
                efc: Some(percent(state.1)),
                current: Some(current(second)),
                coefficient: Some(coefficient),
                ffom: Some(0.0),
            });
        }
        rows
    }

    fn replay(rows: &[Logged], coefficient: Option<f64>) -> Vec<Update> {
        let args = SimulateLoopArgs {
            tc_coefficient: coefficient,
            replay: true,
            ..arguments()
        };
        simulate(&args, Some((rows, 0.0)))
            .expect("a replay")
            .into_iter()
            .map(|(update, _)| update)
            .collect()
    }

    /// With the log's own c the replay is the logged run.
    #[test]
    fn a_replay_with_the_logged_coefficient_is_the_log() {
        let rows = logged(COEFFICIENT);
        for update in replay(&rows, None) {
            let row = rows[update.second as usize];
            let interval = row.interval.expect("an interval");
            assert!(
                (update.interval - interval).abs() < 1e-15,
                "second {}: {:e} against {interval:e}",
                update.second,
                update.interval
            );
        }
    }

    /// With another c the replay is what a run with that c would have
    /// logged against the same oscillator.
    #[test]
    fn a_replay_with_another_coefficient_is_that_run() {
        let rows = logged(COEFFICIENT);
        let counterfactual = logged(0.0);
        let mut largest: f64 = 0.0;
        for update in replay(&rows, Some(0.0)) {
            let row = counterfactual[update.second as usize];
            let interval = row.interval.expect("an interval");
            largest = largest.max(interval.abs());
            assert!(
                (update.interval - interval).abs() < 1e-14,
                "second {}: {:e} against {interval:e}",
                update.second,
                update.interval
            );
        }
        assert!(largest > 1e-9, "the counterfactual moved {largest:e}");
    }

    /// Reproducing the log says nothing of S: the replay recovers the
    /// oscillator through S and runs it back through S.
    #[test]
    fn a_replay_reproduces_the_log_whatever_its_sensitivity() {
        let rows = logged(COEFFICIENT);
        let args = SimulateLoopArgs {
            sensitivity: Some(0.5 * GAIN * counts_per_percent()),
            replay: true,
            ..arguments()
        };
        for (update, _) in simulate(&args, Some((&rows, 0.0))).expect("a replay") {
            let interval = rows[update.second as usize].interval.expect("an interval");
            assert!((update.interval - interval).abs() < 1e-15);
        }
    }

    /// The fit the receiver held at the start.
    const FIT_B: f64 = 20.0;

    /// Its log coefficient.
    const FIT_C: f64 = -300.0;

    /// The receiver's uptime at the start, seconds.
    const UPTIME: u32 = 20_000;

    /// With the receiver's fit given, the replay is the log; running the
    /// fit from empty instead, it is the run that cleared its fit.
    #[test]
    fn a_replay_with_the_aging_fit() {
        let rows = logged_with(COEFFICIENT, |closed| {
            closed.with_aging(FIT_B, FIT_C, UPTIME)
        });
        let args = SimulateLoopArgs {
            fit_b: Some(FIT_B),
            fit_c: Some(FIT_C),
            uptime: Some(UPTIME),
            replay: true,
            ..arguments()
        };
        for (update, _) in simulate(&args, Some((&rows, 0.0))).expect("a replay") {
            let interval = rows[update.second as usize].interval.expect("an interval");
            assert!((update.interval - interval).abs() < 1e-15);
        }
        let cleared = logged_with(COEFFICIENT, |closed| {
            closed.with_aging(FIT_B, FIT_C, UPTIME).with_aging_fit()
        });
        let args = SimulateLoopArgs {
            aging_fit: true,
            ..args
        };
        let replayed = simulate(&args, Some((&rows, 0.0))).expect("a replay");
        assert!(replayed.iter().any(|(_, mode)| mode.is_some()));
        for (update, _) in replayed {
            let interval = cleared[update.second as usize]
                .interval
                .expect("an interval");
            assert!(
                (update.interval - interval).abs() < 1e-12,
                "second {}: {:e} against {interval:e}",
                update.second,
                update.interval
            );
        }
    }

    #[test]
    fn only_the_3704_runs_the_aging_fit() {
        let args = SimulateLoopArgs {
            image: Some("z3805a-3543b".to_owned()),
            uptime: Some(UPTIME),
            aging_fit: true,
            ..arguments()
        };
        let error = simulate(&args, None).expect_err("another image");
        assert!(error.to_string().contains("58503a-3704"), "{error}");
    }

    #[test]
    fn a_replay_stops_at_a_gap() {
        let rows: Vec<Logged> = logged(COEFFICIENT)
            .into_iter()
            .filter(|row| !(10_000.0..10_100.0).contains(&row.at))
            .collect();
        let args = SimulateLoopArgs {
            replay: true,
            ..arguments()
        };
        let error = simulate(&args, Some((&rows, 0.0))).expect_err("a gap");
        assert!(error.to_string().contains("gap"), "{error}");
    }

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
            current: None,
            coefficient: None,
            ffom: None,
        };
        let rows = [row(0.0), row(10.0)];
        assert_eq!(latest(&rows, 9.0), Some(row(0.0)));
        assert_eq!(latest(&rows, 10.0), Some(row(10.0)));
        assert_eq!(latest(&rows, 100.0), None);
        assert_eq!(latest(&rows, -1.0), None);
    }
}
