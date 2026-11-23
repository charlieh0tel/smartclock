//! `simulate-loop`: what TI and EFC the disciplining loop should give
//! for a disturbance, beside what a logged receiver gave.
//!
//! The loop is `pll_normal`'s, `smartclock::discipline`'s law at an
//! image's τ and G from `docs/firmware/loop-constants.json`, closed
//! around an oscillator of a given sensitivity.  It starts at rest, or
//! at a given time interval, or where a logged run stood, and runs no
//! `startup_pll` or holdover; its integrator is fed an aging slope, a
//! constant, a given fit's, the 58503A 3704's aging fit's as it
//! learns, or the receiver's, estimated from a logged run.  A replay
//! drives it with what a logged
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
use smartclock::discipline::EFC_TOP;
use smartclock::discipline::ENTRY_SECONDS;
use smartclock::discipline::Gains;
use smartclock::discipline::Reference;
use smartclock::discipline::Update;
use smartclock::discipline::average_current;
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

/// `--aging-window`'s default, seconds: the aging fit's window, the
/// interval at which a new fit can change d.
const ESTIMATE_WINDOW: u32 = smartclock::aging::ENTRY_SECONDS;

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
    /// the log's `oven_tempco` at each update by default; given, c is
    /// held through the log's changes of it.
    #[arg(long, allow_negative_numbers = true)]
    tc_coefficient: Option<f64>,
    /// s, the oscillator current, held constant.  A replay with c not 0
    /// at any update takes the log's `oven_current` by default.
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
    /// Estimates from the log the aging slope d the receiver fed its
    /// integrator at each update, printed beside it; a replay feeds it
    /// to the loop in place of the aging fit's.
    #[arg(long, requires = "log", conflicts_with_all = ["aging_slope", "fit_b", "fit_c", "aging_fit"])]
    estimate_aging: bool,
    /// The window, seconds, over which `--estimate-aging` averages the
    /// slope it finds at each update, centered on the update.
    #[arg(long, default_value_t = ESTIMATE_WINDOW, requires = "estimate_aging")]
    aging_window: u32,
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

/// c as the log held it at `at`: the latest `oven_tempco` at or before
/// it, however old.
fn logged_coefficient(rows: &[Logged], at: f64) -> Option<f64> {
    last(rows, at, |row| row.coefficient)
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

/// One update of a [`walk`] over the log.
#[derive(Debug, Clone, Copy)]
struct Walked {
    /// The latest row within [`LOG_GAP`].
    row: Logged,
    /// f, the prefilter run over the time intervals from the first
    /// update of the stretch without a gap, where it starts at that
    /// update's, seconds.
    filtered: f64,
    /// s, the health monitor's average of the log's oscillator current,
    /// its latest reading at or before each update of the stretch,
    /// where the log has it.
    current: Option<f64>,
}

/// The updates [`BLOCK_SECONDS`] apart from `start`, `count` of them,
/// as the log saw them: at each, the latest row within [`LOG_GAP`] if
/// it is `usable` at the update's time and has a time interval, with
/// f and s formed over the stretch without a gap it is in.
fn walk(
    rows: &[Logged],
    start: f64,
    count: u32,
    weight: f64,
    usable: impl Fn(&Logged, f64) -> bool,
) -> Vec<Option<Walked>> {
    let seconds = f64::from(BLOCK_SECONDS);
    let mut filtered: Option<f64> = None;
    let mut current: Option<f64> = None;
    (0..count)
        .map(|index| {
            let at = start + f64::from(index) * seconds;
            let row = latest(rows, at).filter(|row| usable(row, at));
            filtered = row.and_then(|row| row.interval).map(|interval| {
                filtered.map_or(interval, |f| (1.0 - weight) * f + weight * interval)
            });
            let seen = row.zip(filtered);
            current = seen
                .and_then(|_| last(rows, at, |row| row.current))
                .map(|fresh| current.map_or(fresh, |s| average_current(s, fresh)));
            seen.map(|(row, filtered)| Walked {
                row,
                filtered,
                current,
            })
        })
        .collect()
}

/// The update at `from` of a [`walk`] over the updates of the
/// [`WARM_UP`] time constants of the prefilter before it.
fn warm_up(rows: &[Logged], from: f64, weight: f64) -> Option<Walked> {
    let blocks = warm_up_blocks(weight);
    let start = from - f64::from(blocks) * f64::from(BLOCK_SECONDS);
    walk(rows, start, blocks + 1, weight, |_, _| true)
        .last()
        .copied()
        .flatten()
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

/// Whether a row is the update at `at` of `pll_normal`, whose law the
/// estimate of the aging slope runs backward: logged since the update
/// before, FFOM 0 where the log has it, and an EFC inside the range,
/// which the loop clamps u to.
fn in_normal_loop(row: &Logged, at: f64) -> bool {
    at - row.at < f64::from(BLOCK_SECONDS)
        && row.ffom.is_none_or(|ffom| ffom == NORMAL_FFOM)
        && row
            .efc
            .is_some_and(|efc| counts(efc) > 0.0 && counts(efc) < EFC_TOP)
}

/// One update of the log as the estimate of the aging slope rebuilds
/// the receiver's state at it.
#[derive(Debug, Clone, Copy)]
struct Rebuilt {
    /// B + I, u − c·s − K·f, counts.
    held: f64,
    /// f, seconds.
    filtered: f64,
    /// s, the monitor's average of the log's oscillator current, where
    /// the log has it.
    current: Option<f64>,
    /// Whether the prefilter has run its [`WARM_UP`] time constants
    /// since the stretch without a gap began.
    warm: bool,
}

/// What `--estimate-aging` finds in the log over a run.
#[derive(Debug, Clone)]
struct AgingEstimate {
    /// d at each update, counts a window, or nothing where the log is
    /// too thin.
    slopes: Vec<Option<f64>>,
    /// f at the run's start, as the estimate rebuilt it.
    filtered: Option<f64>,
    /// s at the run's start, as the estimate rebuilt it.
    current: Option<f64>,
}

/// d, the aging slope the receiver fed its integrator at each update of
/// a run of `seconds` from `from`, estimated from the log (`loop.md`,
/// "Simulating it"), counts a window.  At each update the law run
/// backward, [`Gains::applied_slope`], gives d from B + I, u − c·s − K·f,
/// before and after it, u the logged EFC, c the log's, s the monitor's
/// average of the log's oscillator current and f the prefilter run over
/// the log, once the prefilter has run [`WARM_UP`] time constants into
/// a stretch without a gap; an update's estimate is the mean of those
/// within half of `window` either side, where at least half of them
/// are.
fn estimate_slopes(
    rows: &[Logged],
    from: f64,
    seconds: u32,
    window: u32,
    gains: Gains<f64>,
) -> Result<AgingEstimate> {
    if !rows.iter().any(|row| row.coefficient.is_some()) {
        bail!("the log has no oven_tempco, the receiver's c, which --estimate-aging needs");
    }
    let warm_up = warm_up_blocks(gains.filter_weight);
    let half = window / (2 * BLOCK_SECONDS);
    let updates = seconds / BLOCK_SECONDS;
    let lead = warm_up + half + 1;
    let block = f64::from(BLOCK_SECONDS);
    let start = from - f64::from(lead) * block;
    let mut stretch: u32 = 0;
    let rebuilt: Vec<Option<Rebuilt>> = walk(
        rows,
        start,
        lead + updates + half + 1,
        gains.filter_weight,
        in_normal_loop,
    )
    .into_iter()
    .zip(0_u32..)
    .map(|(seen, index)| {
        let at = start + f64::from(index) * block;
        stretch = if seen.is_some() { stretch + 1 } else { 0 };
        let Walked {
            row,
            filtered,
            current,
        } = seen?;
        let coefficient = logged_coefficient(rows, at)?;
        let term = if coefficient == 0.0 {
            0.0
        } else {
            coefficient * current?
        };
        Some(Rebuilt {
            held: counts(row.efc?) - term - gains.proportional * filtered,
            filtered,
            current,
            warm: stretch > warm_up,
        })
    })
    .collect();
    // The estimate at each update but the walk's first, from its own
    // and the one before.
    let applied: Vec<Option<f64>> = rebuilt
        .windows(2)
        .map(|pair| {
            let (before, after) = (pair[0]?, pair[1]?);
            after
                .warm
                .then(|| gains.applied_slope(before.held, after.held, after.filtered))
        })
        .collect();
    let (lead, half) = (lead as usize, half as usize);
    let slopes = (0..updates as usize)
        .map(|update| {
            let center = lead + update;
            let found: Vec<f64> = applied[center - half..=center + half]
                .iter()
                .flatten()
                .copied()
                .collect();
            #[expect(clippy::cast_precision_loss, reason = "a few hundred updates")]
            let mean = found.iter().sum::<f64>() / found.len() as f64;
            (found.len() > half).then_some(mean)
        })
        .collect();
    let at_start = rebuilt[lead];
    Ok(AgingEstimate {
        slopes,
        filtered: at_start.map(|state| state.filtered),
        current: at_start.and_then(|state| state.current),
    })
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

/// What of the update's inputs a replay takes from the log at each
/// update.
#[derive(Debug, Clone, Copy, Default)]
struct Following {
    /// The fresh readings of the oscillator current, into s.
    current: bool,
    /// c, the latest `oven_tempco` at or before the update.
    coefficient: bool,
}

/// The loop at its start: at rest or at `--ti`, or where the log stood
/// at `from` for a replay, with the f and s an estimate of the aging
/// slope rebuilt where there is one; and for a replay, what it follows
/// of the log.
fn start(
    args: &SimulateLoopArgs,
    loop_at: impl Fn(f64) -> ClosedLoop,
    replay: Option<(&[Logged], f64)>,
    estimate: Option<&AgingEstimate>,
) -> Result<(ClosedLoop, Following)> {
    let Some((rows, from)) = replay else {
        let mut closed = loop_at(counts(args.efc)).with_current_term(
            args.tc_coefficient.unwrap_or_default(),
            args.oven_current.unwrap_or_default(),
        );
        let interval = args.ti / NANOSECONDS;
        closed.resume(interval, interval);
        return Ok((closed, Following::default()));
    };
    let (interval, efc) = logged_state(rows, from, 0)?;
    let closed = loop_at(counts(efc));
    let warmed = warm_up(rows, from, closed.gains().filter_weight);
    let follows_coefficient = args.tc_coefficient.is_none();
    let coefficient = args
        .tc_coefficient
        .or_else(|| logged_coefficient(rows, from))
        .context("the log has no oven_tempco at or before --from; give --tc-coefficient")?;
    let end = from + f64::from(args.seconds);
    let applied = coefficient != 0.0
        || (follows_coefficient
            && rows.iter().any(|row| {
                row.at > from && row.at <= end && row.coefficient.is_some_and(|c| c != 0.0)
            }));
    let (current, follows_current) = match args.oven_current {
        Some(current) => (current, false),
        None if !applied => (0.0, false),
        None => (
            estimate
                .and_then(|estimate| estimate.current)
                .or_else(|| warmed.and_then(|walked| walked.current))
                .context("the log has no oven_current at or before --from; give --oven-current")?,
            true,
        ),
    };
    let mut closed = closed.with_current_term(coefficient, current);
    let filtered = estimate
        .and_then(|estimate| estimate.filtered)
        .or_else(|| warmed.map(|walked| walked.filtered))
        .unwrap_or(interval);
    closed.resume(0.0, filtered);
    Ok((
        closed,
        Following {
            current: follows_current,
            coefficient: follows_coefficient,
        },
    ))
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
    let estimate = estimate(args, replay)?;
    let (closed, following) = start(
        args,
        |efc| {
            ClosedLoop::new(gain, tau, sensitivity, efc, slope).with_aging(
                args.fit_b.unwrap_or(slope),
                args.fit_c.unwrap_or_default(),
                args.uptime.unwrap_or_default(),
            )
        },
        replay,
        estimate.as_ref(),
    )?;
    let mut closed = if args.aging_fit {
        closed.with_aging_fit()
    } else {
        closed
    };
    let disturbances = disturbances(args);
    (0..args.seconds / BLOCK_SECONDS)
        .map(|block| {
            if let Some(estimate) = &estimate {
                let slope = estimate.slopes[block as usize].with_context(|| {
                    format!(
                        "no estimate of the aging slope at second {} of the replay: the log has \
                         fewer than half the updates within half of --aging-window of it, as \
                         near a gap, which the replay cannot cross",
                        (block + 1) * BLOCK_SECONDS
                    )
                })?;
                closed.hold_slope(slope);
            }
            let update = step(&mut closed, &disturbances, replay, following, block)?;
            if let Some(error) = closed.fit_error() {
                bail!("at second {}: {error}", update.second);
            }
            Ok((update, closed.curve().map(|curve| curve.mode)))
        })
        .collect()
}

/// What `--estimate-aging` finds in the log over the run, when it is
/// given.
fn estimate(
    args: &SimulateLoopArgs,
    log: Option<(&[Logged], f64)>,
) -> Result<Option<AgingEstimate>> {
    let Some((rows, from)) = log.filter(|_| args.estimate_aging) else {
        return Ok(None);
    };
    let (tau, gain) = tau_and_gain(args)?;
    estimate_slopes(
        rows,
        from,
        args.seconds,
        args.aging_window,
        Gains::new(gain, tau),
    )
    .map(Some)
}

/// An aging slope in counts a window as EFC percent a day.
fn per_day(slope: f64) -> f64 {
    slope / counts_per_percent() * DAY / ENTRY_SECONDS
}

/// One block of the run, `block` from the start, against the model's
/// reference or a replayed log's, taking from the log what the replay
/// follows of it as at the update ending the block.
fn step(
    closed: &mut ClosedLoop,
    disturbances: &[Disturbance],
    replay: Option<(&[Logged], f64)>,
    following: Following,
    block: u32,
) -> Result<Update> {
    match replay {
        None => Ok(closed.block(disturbances)),
        Some((rows, from)) => {
            let begun = block * BLOCK_SECONDS;
            let ended = begun + BLOCK_SECONDS;
            let at = from + f64::from(ended);
            if following.current
                && let Some(fresh) = last(rows, at, |row| row.current)
            {
                closed.read_current(fresh);
            }
            if following.coefficient
                && let Some(coefficient) = logged_coefficient(rows, at)
            {
                closed.set_coefficient(coefficient);
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
    let estimates = estimate(
        args,
        log.as_ref().map(|(rows, from)| (rows.as_slice(), *from)),
    )?
    .map(|estimate| estimate.slopes);
    let fitting = args.aging_fit
        || args.fit_b.is_some()
        || args.fit_c.is_some()
        || (args.estimate_aging && args.replay);
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
    if estimates.is_some() {
        write!(out, "\testimated_aging_slope_percent_per_day")?;
    }
    writeln!(out)?;
    for (
        index,
        (
            Update {
                second,
                interval,
                efc,
                slope,
            },
            mode,
        ),
    ) in updates.into_iter().enumerate()
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
            write!(out, "\t{:.6}", per_day(slope))?;
        }
        if args.aging_fit {
            write!(
                out,
                "\t{}",
                mode.map_or_else(String::new, |mode| (mode as u8).to_string())
            )?;
        }
        if let Some(estimates) = &estimates {
            write!(out, "\t{}", cell(estimates[index].map(per_day), 6))?;
        }
        writeln!(out)?;
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use anyhow::Result;
    use serde_json::Value;
    use smartclock::discipline::BLOCK_SECONDS;
    use smartclock::discipline::FILTER_SCALE;
    use smartclock::discipline::Gains;
    use smartclock::discipline::INTEGRAL_SCALE;
    use smartclock::discipline::step_limit;

    use smartclock::discipline::ClosedLoop;
    use smartclock::discipline::Disturbance;
    use smartclock::discipline::Update;

    use super::CONSTANTS;
    use super::ESTIMATE_WINDOW;
    use super::Logged;
    use super::SimulateLoopArgs;
    use super::counts;
    use super::counts_per_percent;
    use super::estimate;
    use super::latest;
    use super::percent;
    use super::simulate;
    use super::timed;
    use super::warm_up_blocks;

    /// The 58503A 3704's G, fractional frequency a count.
    const GAIN: f64 = 6.25e-13;

    /// The 58503A 3704's τ, seconds.
    const TAU: f64 = 700.0;

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
            estimate_aging: false,
            aging_window: super::ESTIMATE_WINDOW,
        }
    }

    /// The oscillator current at a second: steady until [`from`], so
    /// that runs with any c agree until then, and a swing over a few
    /// hours from there.
    fn current(second: u32) -> f64 {
        110.0 + 3.0 * (f64::from(second.saturating_sub(from())) / 5000.0).sin()
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
        logged_run(coefficient, aging).0
    }

    /// [`logged_with`], and the run's updates.
    fn logged_run(
        coefficient: f64,
        aging: impl Fn(ClosedLoop) -> ClosedLoop,
    ) -> (Vec<Logged>, Vec<Update>) {
        logged_changing(|_| coefficient, aging)
    }

    /// [`logged_run`], with c `coefficient` at each second, set as the
    /// receiver takes a new c: at the update after it, B kept.
    fn logged_changing(
        coefficient: impl Fn(u32) -> f64,
        aging: impl Fn(ClosedLoop) -> ClosedLoop,
    ) -> (Vec<Logged>, Vec<Update>) {
        let rest = counts(30.0);
        let mut closed = aging(
            ClosedLoop::new(GAIN, TAU, GAIN, rest, 0.0)
                .with_current_term(coefficient(0), current(0)),
        );
        let mut state = (0.0, rest);
        let mut rows = Vec::new();
        let mut updates = Vec::new();
        for second in 0..=SECONDS {
            if second > 0 && second % 10 == 0 {
                closed.read_current(current(second));
                closed.set_coefficient(coefficient(second));
                let update = closed.block(&disturbances());
                state = (update.interval, update.efc);
                updates.push(update);
            }
            rows.push(Logged {
                at: f64::from(second),
                interval: Some(state.0),
                efc: Some(percent(state.1)),
                current: Some(current(second)),
                coefficient: Some(coefficient(second)),
                ffom: Some(0.0),
            });
        }
        (rows, updates)
    }

    fn replay(rows: &[Logged], coefficient: Option<f64>) -> Vec<Update> {
        replay_from(rows, 0, coefficient)
    }

    /// A replay of `rows` from second `from` to their end, at c
    /// `coefficient`, the log's if none.
    fn replay_from(rows: &[Logged], from: u32, coefficient: Option<f64>) -> Vec<Update> {
        let args = SimulateLoopArgs {
            tc_coefficient: coefficient,
            replay: true,
            seconds: SECONDS - from,
            ..arguments()
        };
        simulate(&args, Some((rows, f64::from(from))))
            .expect("a replay")
            .into_iter()
            .map(|(update, _)| update)
            .collect()
    }

    /// The largest difference, seconds, between the time intervals of
    /// a replay from second `from` and those `against` logged.
    fn replay_departure(updates: &[Update], from: u32, against: &[Logged]) -> f64 {
        updates
            .iter()
            .map(|update| {
                let row = against[(from + update.second) as usize];
                (update.interval - row.interval.expect("an interval")).abs()
            })
            .fold(0.0, f64::max)
    }

    /// The second, between updates, at which [`changing`] changes c.
    const CHANGE: u32 = 15_005;

    /// A log whose c is [`COEFFICIENT`] until [`CHANGE`] and 0 from
    /// then on, as setting `:DIAGnostic:ROSCillator:TCOefficient` to 0
    /// leaves it, with the loop `aging` makes.
    fn changing(aging: impl Fn(ClosedLoop) -> ClosedLoop) -> (Vec<Logged>, Vec<Update>) {
        logged_changing(
            |second| if second < CHANGE { COEFFICIENT } else { 0.0 },
            aging,
        )
    }

    /// The log's own c, following its change, reproduces the log, from
    /// a c of 0 too, which needs s all the same; held at the c before
    /// it, the replay departs from it.
    #[test]
    fn a_replay_with_the_logged_coefficient_takes_its_change() {
        let (rows, updates) = changing(|closed| closed);
        let after = (CHANGE / BLOCK_SECONDS) as usize;
        let step = updates[after].efc - updates[after - 1].efc;
        assert!(
            step.abs() > 0.9 * COEFFICIENT.abs() * current(CHANGE),
            "the log's u stepped {step}"
        );
        let error = replay_departure(&replay(&rows, None), 0, &rows);
        assert!(error < EXACT_DEPARTURE, "{error:e}");
        let held = replay_departure(&replay(&rows, Some(COEFFICIENT)), 0, &rows);
        assert!(held > 1e-9, "holding c moved {held:e}");
        let (rows, _) = logged_changing(
            |second| if second < CHANGE { 0.0 } else { COEFFICIENT },
            |closed| closed,
        );
        let error = replay_departure(&replay(&rows, None), 0, &rows);
        assert!(error < EXACT_DEPARTURE, "from c 0: {error:e}");
    }

    /// With a c of its own, held through the log's change, the replay is
    /// the run that never changed c: it does not take the logged step.
    #[test]
    fn a_replay_with_another_coefficient_does_not_take_the_logged_change() {
        let (rows, _) = changing(|closed| closed);
        for coefficient in [COEFFICIENT, 0.0] {
            let error =
                replay_departure(&replay(&rows, Some(coefficient)), 0, &logged(coefficient));
            assert!(error < EXACT_DEPARTURE, "c {coefficient}: {error:e}");
        }
    }

    /// The estimate takes the change out, so a replay on it keeps to
    /// the log with the log's c, and is the run that never changed c
    /// with another.
    #[test]
    fn a_replay_on_the_estimate_across_a_change_of_coefficient() {
        let aging = |closed: ClosedLoop| closed.with_aging(FIT_B, FIT_C, UPTIME);
        let (rows, _) = changing(aging);
        let error = departure(&rows, &rows, None, ONE_UPDATE);
        assert!(error < EXACT_DEPARTURE, "{error:e}");
        let error = departure(
            &rows,
            &logged_with(COEFFICIENT, aging),
            Some(COEFFICIENT),
            ONE_UPDATE,
        );
        assert!(error < EXACT_DEPARTURE, "{error:e}");
    }

    /// How far a replay from the middle of a run may depart from the
    /// log, seconds: the prefilter's warm-up leaves e⁻⁵ of its starting
    /// error in f.  Starting s from one fresh reading instead, a replay
    /// of [`logged`] departs by a hundred times this.
    const WARM_UP_DEPARTURE: f64 = 2e-11;

    /// Started where the current has been swinging, a replay starts s
    /// from the monitor's average over the warm-up, as the receiver
    /// held it, and is the log.
    #[test]
    fn a_replay_from_the_middle_of_a_run_starts_s_from_the_warm_up() {
        let rows = logged(COEFFICIENT);
        for start in [from() + 2000, from() + 5000, from() + 10_000] {
            let error = replay_departure(&replay_from(&rows, start, None), start, &rows);
            assert!(error < WARM_UP_DEPARTURE, "from {start}: {error:e}");
        }
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

    /// Where the estimates' runs at a window of `window` seconds start
    /// in the log, seconds: where the estimate's walk, the prefilter's
    /// warm-up and half a window before its first update, begins at the
    /// log's start, from rest, so that it rebuilds f exactly.
    fn from_at(window: u32) -> u32 {
        let warm_up = warm_up_blocks(Gains::new(GAIN, TAU).filter_weight);
        (warm_up + window / (2 * BLOCK_SECONDS) + 1) * BLOCK_SECONDS
    }

    /// [`from_at`] the default window, the latest of them.
    fn from() -> u32 {
        from_at(ESTIMATE_WINDOW)
    }

    /// The arguments for a run from [`from`] to the log's end with
    /// `--estimate-aging` at a window of `window` seconds.
    fn estimating(window: u32) -> SimulateLoopArgs {
        SimulateLoopArgs {
            estimate_aging: true,
            aging_window: window,
            seconds: SECONDS - from_at(window),
            ..arguments()
        }
    }

    /// `--estimate-aging`'s estimates over a run from [`from`].
    fn estimated(rows: &[Logged], window: u32) -> Vec<Option<f64>> {
        estimate(
            &estimating(window),
            Some((rows, f64::from(from_at(window)))),
        )
        .expect("estimates")
        .expect("asked for")
        .slopes
    }

    /// The index in a logged run's updates of the `index`th estimate at
    /// a window of `window` seconds.
    fn logged_index(window: u32, index: usize) -> usize {
        (from_at(window) / BLOCK_SECONDS) as usize + index
    }

    /// The largest difference between estimates and what they estimate.
    fn worst(estimates: &[Option<f64>], truth: impl Fn(usize) -> f64) -> f64 {
        estimates
            .iter()
            .enumerate()
            .map(|(index, estimate)| (estimate.expect("an estimate") - truth(index)).abs())
            .fold(0.0, f64::max)
    }

    /// The log as the receiver reports it: u a whole DAC count, and
    /// `:EFControl:RELative?` to four decimals (`docs/efc.md`).
    fn quantized(rows: &[Logged]) -> Vec<Logged> {
        rows.iter()
            .map(|row| Logged {
                efc: row
                    .efc
                    .map(|efc| (percent(counts(efc).round()) * 1e4).round() / 1e4),
                ..*row
            })
            .collect()
    }

    /// The estimates' tolerance on a log quantized as the receiver
    /// reports it, counts a window, at the default window: the error's
    /// standard deviation is √2·σ·270/N, σ about 0.33 counts from the
    /// two roundings and N the updates found in the window, 271 but
    /// half that at the log's end; this is about three times that at
    /// the end.
    const QUANTIZED_TOLERANCE: f64 = 3.0;

    /// The tolerance on an exact log, counts a window: rounding alone.
    const EXACT_TOLERANCE: f64 = 1e-6;

    /// The window of one update: the estimate of each update alone.
    const ONE_UPDATE: u32 = BLOCK_SECONDS;

    /// A run whose d is constant gives it back, update by update from an
    /// exact log, and averaged over the window from a quantized one, on
    /// which a single update's estimate is noise.
    #[test]
    fn the_estimate_recovers_a_constant_slope() {
        let rows = logged_with(COEFFICIENT, |closed| closed.with_aging(FIT_B, 0.0, UPTIME));
        for window in [ONE_UPDATE, ESTIMATE_WINDOW] {
            let error = worst(&estimated(&rows, window), |_| FIT_B);
            assert!(error < EXACT_TOLERANCE, "window {window}: {error}");
        }
        let quantized = quantized(&rows);
        let error = worst(&estimated(&quantized, ESTIMATE_WINDOW), |_| FIT_B);
        assert!(error < QUANTIZED_TOLERANCE, "{error}");
        let error = worst(&estimated(&quantized, ONE_UPDATE), |_| FIT_B);
        assert!(error > 10.0 * QUANTIZED_TOLERANCE, "{error}");
    }

    /// The window's mean of each update's d in a run.
    fn window_mean(updates: &[Update], index: usize) -> f64 {
        let half = (ESTIMATE_WINDOW / (2 * BLOCK_SECONDS)) as usize;
        let window = &updates[index.saturating_sub(half)..=(index + half).min(updates.len() - 1)];
        #[expect(clippy::cast_precision_loss, reason = "a few hundred updates")]
        let count = window.len() as f64;
        window.iter().map(|update| update.slope).sum::<f64>() / count
    }

    /// A log of the 58503A 3704's fit as it learns, and the run's d.
    fn learning() -> (Vec<Logged>, Vec<Update>) {
        logged_run(COEFFICIENT, |closed| {
            closed.with_aging(FIT_B, FIT_C, UPTIME).with_aging_fit()
        })
    }

    /// The fit's d, stepping at each fit and moving with q between,
    /// comes back update by update, and as its window's mean.
    #[test]
    fn the_estimate_follows_the_3704_fit() {
        let (rows, updates) = learning();
        let error = worst(&estimated(&rows, ONE_UPDATE), |index| {
            updates[logged_index(ONE_UPDATE, index)].slope
        });
        assert!(error < EXACT_TOLERANCE, "{error}");
        let mean = |index| window_mean(&updates, logged_index(ESTIMATE_WINDOW, index));
        let error = worst(&estimated(&rows, ESTIMATE_WINDOW), mean);
        assert!(error < EXACT_TOLERANCE, "{error}");
        let error = worst(&estimated(&quantized(&rows), ESTIMATE_WINDOW), mean);
        assert!(error < QUANTIZED_TOLERANCE, "{error}");
    }

    /// A replay feeding the estimate, at a window of `window` and c
    /// `coefficient`, the log's if none.
    fn replay_estimating(
        rows: &[Logged],
        coefficient: Option<f64>,
        window: u32,
    ) -> Result<Vec<Update>> {
        let args = SimulateLoopArgs {
            tc_coefficient: coefficient,
            replay: true,
            ..estimating(window)
        };
        Ok(simulate(&args, Some((rows, f64::from(from_at(window)))))?
            .into_iter()
            .map(|(update, _)| update)
            .collect())
    }

    /// The largest difference, seconds, between the time intervals of
    /// [`replay_estimating`] `rows` and those `against` logged.
    fn departure(
        rows: &[Logged],
        against: &[Logged],
        coefficient: Option<f64>,
        window: u32,
    ) -> f64 {
        replay_estimating(rows, coefficient, window)
            .expect("a replay")
            .iter()
            .map(|update| {
                let row = against[(from_at(window) + update.second) as usize];
                (update.interval - row.interval.expect("an interval")).abs()
            })
            .fold(0.0, f64::max)
    }

    /// How far a replay may depart from the run it should be, seconds,
    /// where it should be that run but for rounding.
    const EXACT_DEPARTURE: f64 = 1e-15;

    /// How far a replay on the window's mean of a smooth d may depart
    /// from the log, seconds.
    const SMOOTH_DEPARTURE: f64 = 1e-10;

    /// How far a replay on the window's mean of the learning fit's d,
    /// which steps at each fit, may depart from the log, seconds.
    const STEPPING_DEPARTURE: f64 = 1e-8;

    /// Fed each update's estimate alone, a replay is the log by
    /// construction, whatever d was; fed the window's mean, it departs
    /// from it where d changes within the window, little where d moves
    /// smoothly with q and more where it steps at a fit.
    #[test]
    fn a_replay_on_the_estimate_is_the_log() {
        let (rows, _) = learning();
        let error = departure(&rows, &rows, None, ONE_UPDATE);
        assert!(error < EXACT_DEPARTURE, "{error:e}");
        let error = departure(&rows, &rows, None, ESTIMATE_WINDOW);
        assert!(error < STEPPING_DEPARTURE, "{error:e}");
        let rows = logged_with(COEFFICIENT, |closed| {
            closed.with_aging(FIT_B, FIT_C, UPTIME)
        });
        let error = departure(&rows, &rows, None, ESTIMATE_WINDOW);
        assert!(error < SMOOTH_DEPARTURE, "{error:e}");
    }

    /// With another c, a replay on the estimate is the run with that c
    /// and the receiver's d.
    #[test]
    fn a_replay_on_the_estimate_with_another_coefficient_is_that_run() {
        let aging = |closed: ClosedLoop| closed.with_aging(FIT_B, FIT_C, UPTIME);
        let rows = logged_with(COEFFICIENT, aging);
        let counterfactual = logged_with(0.0, aging);
        let error = departure(&rows, &counterfactual, Some(0.0), ONE_UPDATE);
        assert!(error < EXACT_DEPARTURE, "{error:e}");
        let error = departure(&rows, &counterfactual, Some(0.0), ESTIMATE_WINDOW);
        assert!(error < SMOOTH_DEPARTURE, "{error:e}");
        let moved = departure(&rows, &rows, Some(0.0), ESTIMATE_WINDOW);
        assert!(
            moved > 100.0 * SMOOTH_DEPARTURE,
            "the counterfactual moved {moved:e}"
        );
    }

    /// Across a gap there is no estimate, nor until the prefilter has
    /// warmed up again and half the window is past it.  Before it, the
    /// estimate is exact until the window reaches the row before the
    /// gap, which may stand for an update twice; after it, the
    /// prefilter's warm-up leaves e⁻⁵ of its error at the restart.  A
    /// replay still stops at it.
    #[test]
    fn the_estimate_stops_at_a_gap() {
        let gap = 15_000..18_000;
        let rows: Vec<Logged> =
            logged_with(COEFFICIENT, |closed| closed.with_aging(FIT_B, 0.0, UPTIME))
                .into_iter()
                .filter(|row| !gap.contains(&(row.at as u32)))
                .collect();
        let warm_up = warm_up_blocks(Gains::new(GAIN, TAU).filter_weight) * BLOCK_SECONDS;
        let near = ESTIMATE_WINDOW / 2 + warm_up;
        let mut after = 0;
        for (index, estimate) in estimated(&rows, ESTIMATE_WINDOW).into_iter().enumerate() {
            let second = from() + (index as u32 + 1) * BLOCK_SECONDS;
            if second > gap.start && second < gap.end {
                assert_eq!(estimate, None, "second {second}");
            }
            let Some(estimate) = estimate else {
                continue;
            };
            after += usize::from(second > gap.end);
            let error = (estimate - FIT_B).abs();
            let tolerance = if second + near < gap.start {
                EXACT_TOLERANCE
            } else {
                FIT_B / f64::from(ESTIMATE_WINDOW / (2 * BLOCK_SECONDS))
            };
            assert!(error < tolerance, "second {second}: {estimate}");
        }
        assert!(after > 0);
        let error = replay_estimating(&rows, None, ESTIMATE_WINDOW).expect_err("a gap");
        assert!(error.to_string().contains("gap"), "{error}");
    }

    /// d beyond ±M is applied at M, and the estimate finds M.
    #[test]
    fn the_estimate_finds_the_clamp() {
        let limit = step_limit(GAIN);
        let rows = logged_with(COEFFICIENT, |closed| {
            closed.with_aging(2.0 * limit, 0.0, UPTIME)
        });
        let error = worst(&estimated(&rows, ESTIMATE_WINDOW), |_| limit);
        assert!(error < EXACT_TOLERANCE, "{error}");
        for update in replay_estimating(&rows, None, ESTIMATE_WINDOW).expect("a replay") {
            assert!(update.slope <= limit);
        }
        let error = departure(&rows, &rows, None, ESTIMATE_WINDOW);
        assert!(error < EXACT_DEPARTURE, "{error:e}");
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
