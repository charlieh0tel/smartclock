//! The aging fit: the EFC history the loop samples and the curve the
//! `curv` task fits to it, whose slope the loop feeds its integrator
//! (`docs/firmware/loop.md`, "The aging fit").
//!
//! This is the 58503A 3704's, transcribed from its hand-written C,
//! `third_party/firmware/rewritten/58503a-3704/pll_aging.c` and the
//! shared `rewritten/common/pll_aging.c`, which the function check holds
//! to the image (`docs/firmware/rewrite.md`).  It runs in single
//! precision, with doubles where the C has them, so that its searches
//! take the image's steps.  The C's names are kept in the comments.
//! Other images differ: the Z3805A 3543B times an entry by its samples,
//! the Z3815A fits halves of 32.  The predicted holdover uncertainty,
//! HQ, is not computed; the loop does not use it.

use thiserror::Error;

/// Entries in the history's ring.
pub const ENTRIES: usize = 64;

/// The seconds of uptime an entry spans: one sample a second.
pub const ENTRY_SECONDS: u32 = 2700;

/// The ring's index mask, for its wrap.
const INDEX_MASK: usize = ENTRIES - 1;

/// Of an entry's samples, fewer than this summed make its weight
/// partial (`QUARTER_SAMPLES`).
const QUARTER_SAMPLES: u32 = 675;

/// Before this uptime, seconds, every entry is partial
/// (`PARTIAL_UPTIME`).
const PARTIAL_UPTIME: u32 = 10_800;

/// An entry's time over this is past four days, 32 entries a day
/// (`FOUR_DAYS`).
const FOUR_DAYS: u32 = 128;

/// Below this many well weighted entries, no line through them alone
/// (`FEW_WELL_WEIGHTED`).
const FEW_WELL_WEIGHTED: usize = 3;

/// From this many well weighted entries, the line or the log curve
/// through them (`SOME_WELL_WEIGHTED`).
const SOME_WELL_WEIGHTED: usize = 6;

/// Below this many entries, a constant (`FEW_ENTRIES`).
const FEW_ENTRIES: usize = 3;

/// From this many entries, the line or the log curve through them all
/// (`SOME_ENTRIES`).
const SOME_ENTRIES: usize = 10;

/// Below this many well weighted entries the log curve is taken
/// whatever the line's rms (`ENOUGH_FOR_LINE`).
const ENOUGH_FOR_LINE: usize = 16;

/// The log curve is taken when its rms is under this times the line's
/// (`LINE_ADVANTAGE`).
const LINE_ADVANTAGE: f32 = 0.75;

/// `fit_log_initial`'s floor on the line's change over the span, times
/// its rms (`RMS_MULTIPLE`).
const RMS_MULTIPLE: f64 = 10.0;

/// `fit_logarithmic` divides its step by this each pass
/// (`STEP_SHRINK`).
const STEP_SHRINK: f32 = 10.0;

/// `fit_logarithmic` stops when a pass gains no more than this fraction
/// of the rms (`LEAST_GAIN`).
const LEAST_GAIN: f64 = 0.01;

/// `fit_linear_halves`: the halves' sums are 32 entries apart, of 32
/// entries each (`HALVES_SCALE`).
const HALVES_SCALE: f32 = 1024.0;

/// `fit_linear_halves`: the offset from the newest entry's time to the
/// ring's middle (`MIDDLE_OFFSET`).
const MIDDLE_OFFSET: f32 = -32.5;

/// `fit_linear_halves`: the degrees of freedom of its rms over the
/// whole ring (`DEGREES_OF_FREEDOM`).
const HALVES_DEGREES_OF_FREEDOM: f32 = 62.0;

/// An entry's weight: of how many of its samples were summed.
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq, PartialOrd, Ord)]
pub enum Weight {
    /// No entry written.
    #[default]
    Empty = 0,
    /// Fewer than a quarter of its samples, or in the first three
    /// hours of uptime.
    Partial = 1,
    /// A quarter or more.
    Most = 2,
    /// All 2700.
    Full = 3,
}

/// The EFC's history, `pll.efc_history`: a ring of entries, each the
/// mean of the EFC less c·s over 2700 seconds.
#[derive(Debug, Clone, PartialEq)]
pub struct History {
    /// Each entry's weight; `Empty` until written.
    weights: [Weight; ENTRIES],
    /// Each entry's mean EFC less c·s, counts (`averages`).
    averages: [f32; ENTRIES],
    /// Each entry's time y, the uptime at its end over 2700 (`times`).
    times: [f32; ENTRIES],
    /// Each entry's log term w, the mean of ln t over its window
    /// (`logs`).
    logs: [f32; ENTRIES],
    /// The next entry to write (`stop_ptr`).
    head: usize,
}

impl Default for History {
    fn default() -> Self {
        Self {
            weights: [Weight::Empty; ENTRIES],
            averages: [0.0; ENTRIES],
            times: [0.0; ENTRIES],
            logs: [0.0; ENTRIES],
            head: 0,
        }
    }
}

/// An entry's log term from its time y: ln(y − 1) + y·ln(y / (y − 1))
/// − 1, or −1 at a time of 1 or less (`pll_aging_log_term`).
pub fn log_term(time: f32) -> f32 {
    if time <= 1.0 {
        return -1.0;
    }
    let ratio = time / (time - 1.0);
    let scaled = f64::from(ratio).ln() * f64::from(time);
    let sum = f64::from(time - 1.0).ln() + scaled;
    narrow(-1.0 + sum)
}

/// A double as the image's float.
#[expect(
    clippy::cast_possible_truncation,
    reason = "the image converts to single precision here"
)]
fn narrow(value: f64) -> f32 {
    value as f32
}

/// A count as a float.
#[expect(
    clippy::cast_precision_loss,
    reason = "counts of at most a few thousand"
)]
fn float(count: u32) -> f32 {
    count as f32
}

/// The square root of the magnitude of `sum / count`
/// (`pll_aging_root_mean`).
fn root_mean(sum: f32, count: f32) -> f32 {
    narrow(f64::from((sum / count).abs()).sqrt())
}

/// The sampler, `efc_history_update` while collecting: once a second
/// it sums the EFC less c·s, and each 2700 seconds of uptime writes
/// their mean into the history.  Only the loop's own stages are
/// modeled: every second is summed.
#[derive(Debug, Clone, PartialEq)]
pub struct Sampler {
    /// The history it writes.
    history: History,
    /// The uptime at which the next entry is written (`field_34a`).
    deadline: u32,
    /// The sum of the entry's samples so far (`efc_sum`).
    sum: f32,
    /// The samples summed (`summed_count`).
    summed: u32,
}

impl Sampler {
    /// Collecting from uptime `uptime`, seconds, into an empty history,
    /// its first entry due 2700 seconds on.
    pub fn new(uptime: u32) -> Self {
        Self {
            history: History::default(),
            deadline: uptime.saturating_add(ENTRY_SECONDS),
            sum: 0.0,
            summed: 0,
        }
    }

    /// The history so far.
    pub fn history(&self) -> &History {
        &self.history
    }

    /// One second's sample, the EFC in force less c·s, at uptime
    /// `uptime`; whether an entry was written, for the fit to run.
    pub fn second(&mut self, efc_less_current: f32, uptime: u32) -> bool {
        if self.summed == 0 {
            self.sum = 0.0;
        }
        self.sum += efc_less_current;
        self.summed += 1;
        if uptime < self.deadline {
            return false;
        }
        self.deadline = self.deadline.saturating_add(ENTRY_SECONDS);
        let history = &mut self.history;
        let entry = history.head;
        history.weights[entry] = if self.summed < QUARTER_SAMPLES || uptime < PARTIAL_UPTIME {
            Weight::Partial
        } else if self.summed == ENTRY_SECONDS {
            Weight::Full
        } else {
            Weight::Most
        };
        history.averages[entry] = self.sum / float(self.summed);
        let time = narrow(f64::from(uptime) / f64::from(ENTRY_SECONDS));
        history.times[entry] = time;
        history.logs[entry] = if uptime <= ENTRY_SECONDS {
            -1.0
        } else {
            log_term(time)
        };
        history.head = (history.head + 1) & INDEX_MASK;
        self.summed = 0;
        self.sum = 0.0;
        true
    }
}

/// Which fit `aging_fit` made, `aging_curve.mode`.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Mode {
    /// Fewer than three entries: their mean.
    Constant = 0,
    /// Fewer than ten entries: a line through them all.
    Few = 1,
    /// The three-term fit, a + b·y + c·w.
    Log = 2,
    /// A line.
    Line = 3,
    /// A line from the means of the ring's halves.
    Halves = 4,
}

/// A fitted curve, a + b·y + c·w, the parts of `aging_curve` the loop
/// takes.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Curve {
    /// The fit made.
    pub mode: Mode,
    /// a, moved so the curve meets the newest entry, counts.
    pub a: f32,
    /// b, counts an entry: the loop's r.
    pub b: f32,
    /// c, counts a unit of w: the loop's p.
    pub c: f32,
    /// The residuals' rms, counts.
    pub rms: f32,
}

/// A fit the image's code does not determine.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Error)]
pub enum FitError {
    /// `fit_log_search` turned down but took no step down, its rms
    /// unchanged by the step up or a regression failed, so the image
    /// takes the vertex of three points one of which it never set: the
    /// result is whatever its stack held.
    #[error(
        "the log fit's search took no step down; the image then reads values it never set, so \
         the fit is not determined"
    )]
    UnsetSearchPoint,
}

/// `aging_curve_state`: a fit routine's working record.
#[derive(Debug, Clone, Copy, PartialEq)]
struct Working {
    /// Only entries of this weight or more are fitted.
    minimum_weight: Weight,
    /// The fitted entries' least time, truncated.
    oldest_time: u32,
    /// Their greatest.
    newest_time: u32,
    /// The entry with `oldest_time`.
    oldest_index: usize,
    /// The entry with `newest_time`.
    newest_index: usize,
    /// a.
    a: f32,
    /// b.
    b: f32,
    /// c.
    c: f32,
    /// `fit_log_search`'s step in c.
    c_step: f32,
    /// The residuals' rms.
    rms: f32,
}

impl Working {
    fn new(minimum_weight: Weight) -> Self {
        Self {
            minimum_weight,
            oldest_time: 0,
            newest_time: 0,
            oldest_index: 0,
            newest_index: 0,
            a: 0.0,
            b: 0.0,
            c: 0.0,
            c_step: 0.0,
            rms: 0.0,
        }
    }

    fn curve(&self, mode: Mode) -> Curve {
        Curve {
            mode,
            a: self.a,
            b: self.b,
            c: self.c,
            rms: self.rms,
        }
    }
}

/// `linear_regression`'s result: the line, or with no slope the mean,
/// and its rms.
#[derive(Debug, Clone, Copy, PartialEq)]
struct Regression {
    /// 0 unless a slope was fitted.
    slope: f32,
    /// The mean of y when no slope is fitted.
    intercept: f32,
    /// The residuals'.
    rms: f32,
    /// Two entries or fewer, or all their x the same: zeros.
    failed: bool,
}

impl History {
    /// The entries of `minimum` weight or more.
    fn fitted(&self, minimum: Weight) -> impl Iterator<Item = usize> + '_ {
        (0..ENTRIES).filter(move |&index| self.weights[index] >= minimum)
    }

    /// `fit_span`: the least and greatest time of the curve's entries,
    /// truncated, and their indexes.
    fn span(&self, curve: &mut Working) {
        curve.oldest_time = 0;
        curve.newest_time = 0;
        for index in self.fitted(curve.minimum_weight) {
            let time = self.times[index];
            if float(curve.newest_time) < time || curve.newest_time == 0 {
                curve.newest_index = index;
                curve.newest_time = truncate(time);
            }
            if float(curve.oldest_time) > time || curve.oldest_time == 0 {
                curve.oldest_index = index;
                curve.oldest_time = truncate(time);
            }
        }
    }

    /// `linear_regression`: `y` on the times, over the entries of
    /// `minimum` weight or more.
    fn regression(&self, y: &[f32; ENTRIES], minimum: Weight, with_slope: bool) -> Regression {
        let (mut x_sum, mut y_sum, mut count) = (0.0_f32, 0.0_f32, 0_u32);
        for index in self.fitted(minimum) {
            x_sum += self.times[index];
            y_sum += y[index];
            count += 1;
        }
        let (x_mean, y_mean) = if count == 0 {
            (0.0, 0.0)
        } else {
            (x_sum / float(count), y_sum / float(count))
        };
        let (mut sxx, mut sxy, mut syy) = (0.0_f32, 0.0_f32, 0.0_f32);
        for index in self.fitted(minimum) {
            let dx = self.times[index] - x_mean;
            let dy = y[index] - y_mean;
            sxx += dx * dx;
            sxy += dx * dy;
            syy += dy * dy;
        }
        if count <= 2 || sxx == 0.0 {
            return Regression {
                slope: 0.0,
                intercept: 0.0,
                rms: 0.0,
                failed: true,
            };
        }
        let degrees_of_freedom = float(count - 2);
        if !with_slope {
            return Regression {
                slope: 0.0,
                intercept: y_mean,
                rms: root_mean(syy, degrees_of_freedom),
                failed: false,
            };
        }
        let slope = sxy / sxx;
        let explained = slope * slope * sxx + syy;
        let cross = slope * sxy * 2.0;
        Regression {
            slope,
            intercept: y_mean - slope * x_mean,
            rms: root_mean(explained - cross, degrees_of_freedom),
            failed: false,
        }
    }

    /// `fit_evaluate_latest`: a moved so the curve meets the newest
    /// entry.
    fn evaluate_latest(&self, curve: &mut Working) {
        let fitted = float(curve.newest_time) * curve.b + curve.c * self.logs[curve.newest_index];
        curve.a = self.averages[curve.newest_index] - fitted;
    }

    /// `fit_linear`: a line in time.
    fn linear(&self, curve: &mut Working) -> bool {
        self.span(curve);
        let line = self.regression(&self.averages, curve.minimum_weight, true);
        curve.a = line.intercept;
        curve.b = line.slope;
        curve.c = 0.0;
        curve.rms = line.rms;
        self.evaluate_latest(curve);
        line.failed
    }

    /// `fit_constant`: the entries' mean.
    fn constant(&self, curve: &mut Working) -> bool {
        let (mut sum, mut count) = (0.0_f32, 0_u32);
        for index in self.fitted(curve.minimum_weight) {
            sum += self.averages[index];
            count += 1;
        }
        curve.a = if count == 0 { 0.0 } else { sum / float(count) };
        curve.b = 0.0;
        curve.c = 0.0;
        curve.rms = 0.0;
        count == 0
    }

    /// `fit_linear_halves`: a line from the means of the ring's newer
    /// and older halves, for a full ring.
    fn linear_halves(&self, curve: &mut Working) {
        const HALF: usize = ENTRIES / 2;
        self.span(curve);
        let newest = curve.newest_index;
        let older_end = newest.wrapping_sub(HALF) & INDEX_MASK;
        let mut difference = 0.0_f32;
        let mut index = newest;
        while index != older_end {
            difference += self.averages[index];
            index = index.wrapping_sub(1) & INDEX_MASK;
        }
        let mut total = difference;
        let mut index = older_end;
        while index != newest {
            difference -= self.averages[index];
            total += self.averages[index];
            index = index.wrapping_sub(1) & INDEX_MASK;
        }
        curve.b = difference / HALVES_SCALE;
        curve.c = 0.0;
        let from_middle = (self.times[newest] + MIDDLE_OFFSET) * curve.b;
        #[expect(clippy::cast_precision_loss, reason = "64")]
        let entries = ENTRIES as f32;
        curve.a = total / entries - from_middle;
        let squares = (0..ENTRIES)
            .map(|index| {
                let residual = self.averages[index] - (curve.b * self.times[index] + curve.a);
                residual * residual
            })
            .fold(0.0_f32, |sum, square| sum + square);
        curve.rms = root_mean(squares, HALVES_DEGREES_OF_FREEDOM);
        self.evaluate_latest(curve);
    }

    /// `fit_log_residuals`: the history less the curve's log term,
    /// regressed on time, into its a, b and rms.
    fn log_residuals(&self, curve: &mut Working) -> bool {
        let mut residuals = [0.0_f32; ENTRIES];
        for index in self.fitted(curve.minimum_weight) {
            residuals[index] = self.averages[index] - curve.c * self.logs[index];
        }
        let line = self.regression(&residuals, curve.minimum_weight, true);
        curve.rms = line.rms;
        curve.a = line.intercept;
        curve.b = line.slope;
        line.failed
    }

    /// `fit_log_initial`: c's first value and step, from a line's change
    /// over the span, at least ten times its rms.
    fn log_initial(&self, curve: &mut Working) {
        let line = self.regression(&self.averages, curve.minimum_weight, true);
        let mut change = float(curve.newest_time - curve.oldest_time) * line.slope;
        if RMS_MULTIPLE * f64::from(line.rms) > f64::from(change.abs()) {
            change = narrow(RMS_MULTIPLE * f64::from(line.rms));
        }
        let c = change / self.averages[curve.newest_index] - self.averages[curve.oldest_index];
        curve.c_step = c;
        curve.c = c;
        curve.rms = line.rms;
    }

    /// `fit_log_search`: steps c by its step, the way the rms falls,
    /// while it falls, then takes the vertex of the parabola through the
    /// last three; whether a regression or the vertex failed.
    fn log_search(&self, curve: &mut Working) -> Result<bool, FitError> {
        let mut rms_values = [0.0_f32; 3];
        let mut c_values = [0.0_f32; 3];
        let first_failed = self.log_residuals(curve);
        rms_values[1] = curve.rms;
        c_values[1] = curve.c;
        curve.c += curve.c_step;
        c_values[2] = curve.c;
        let mut failed = self.log_residuals(curve) | first_failed;
        rms_values[2] = curve.rms;
        if rms_values[1] <= rms_values[2] {
            // Up did not help: go down, from the first two swapped.
            curve.c -= curve.c_step;
            c_values.swap(1, 2);
            rms_values.swap(1, 2);
            if rms_values[1] <= rms_values[2] || failed {
                return Err(FitError::UnsetSearchPoint);
            }
            while rms_values[1] > rms_values[2] && !failed {
                curve.c -= curve.c_step;
                c_values = [c_values[1], c_values[2], curve.c];
                failed = self.log_residuals(curve);
                rms_values = [rms_values[1], rms_values[2], curve.rms];
            }
        } else {
            while rms_values[2] < rms_values[1] {
                curve.c += curve.c_step;
                c_values = [c_values[1], c_values[2], curve.c];
                failed |= self.log_residuals(curve);
                rms_values = [rms_values[1], rms_values[2], curve.rms];
            }
        }
        let (vertex, vertex_failed) = three_point(c_values, rms_values);
        curve.c = vertex;
        Ok(vertex_failed | failed | self.log_residuals(curve))
    }

    /// `fit_logarithmic`: the three-term fit, searching c in steps a
    /// tenth as large each pass until a pass gains no more than 1 % of
    /// the rms or fails.
    fn logarithmic(&self, curve: &mut Working) -> Result<bool, FitError> {
        self.span(curve);
        self.log_initial(curve);
        let failed = loop {
            let previous = curve.rms;
            let failed = self.log_search(curve)?;
            curve.c_step /= STEP_SHRINK;
            let gain = f64::from(previous - curve.rms);
            if f64::from(previous) * LEAST_GAIN >= gain || failed {
                break failed;
            }
        };
        self.evaluate_latest(curve);
        Ok(failed)
    }

    /// `pll_aging_fit_best`: the line or the log curve, the log curve
    /// when its rms is under three quarters of the line's or fewer than
    /// 16 entries are well weighted.
    fn best(&self, minimum: Weight, well_weighted: usize) -> Result<Curve, FitError> {
        let mut line = Working::new(minimum);
        self.linear(&mut line);
        let mut candidate = Working::new(minimum);
        self.logarithmic(&mut candidate)?;
        Ok(
            if line.rms * LINE_ADVANTAGE > candidate.rms || well_weighted < ENOUGH_FOR_LINE {
                candidate.curve(Mode::Log)
            } else {
                line.curve(Mode::Line)
            },
        )
    }

    /// `aging_fit`: the curve the `curv` task fits to the history.
    pub fn fit(&self) -> Result<Curve, FitError> {
        let mut curve = Working::new(Weight::Most);
        self.span(&mut curve);
        let well_weighted = self.fitted(Weight::Most).count();
        let entries = self.fitted(Weight::Partial).count();
        if well_weighted == ENTRIES && curve.newest_time > FOUR_DAYS {
            self.linear_halves(&mut curve);
            Ok(curve.curve(Mode::Halves))
        } else if curve.oldest_time <= FOUR_DAYS || well_weighted < FEW_WELL_WEIGHTED {
            if well_weighted >= SOME_WELL_WEIGHTED {
                self.best(Weight::Most, well_weighted)
            } else if entries >= SOME_ENTRIES {
                self.best(Weight::Partial, well_weighted)
            } else {
                let mut curve = Working::new(Weight::Partial);
                if entries < FEW_ENTRIES {
                    self.constant(&mut curve);
                    Ok(curve.curve(Mode::Constant))
                } else {
                    self.linear(&mut curve);
                    Ok(curve.curve(Mode::Few))
                }
            }
        } else {
            self.linear(&mut curve);
            Ok(curve.curve(Mode::Line))
        }
    }
}

/// A positive time truncated to whole entries (`float_to_unsigned`).
#[expect(
    clippy::cast_possible_truncation,
    clippy::cast_sign_loss,
    reason = "the image truncates the time to an unsigned"
)]
fn truncate(time: f32) -> u32 {
    time as u32
}

/// `fit_three_point`: the x of the vertex of the parabola through three
/// points, and whether it failed, giving 0: two share an x, the points
/// are in line, or the outer two's y are equal.  With the curvature 0 it
/// gives the line through the outer two's crossing of zero instead.
fn three_point(x: [f32; 3], y: [f32; 3]) -> (f32, bool) {
    let [x0, x1, x2] = x;
    let [y0, y1, y2] = y;
    let numerator = (y0 - y1) * (x1 - x2) - (y1 - y2) * (x0 - x1);
    let denominator = (x0 * x0 - x1 * x1) * (x1 - x2) - (x1 * x1 - x2 * x2) * (x0 - x1);
    #[expect(clippy::float_cmp, reason = "the image compares exactly")]
    let degenerate =
        x0 == x1 || x1 == x2 || x2 == x0 || denominator == 0.0 || numerator == 0.0 || y2 == y0;
    if degenerate {
        return (0.0, true);
    }
    let curvature = numerator / denominator;
    if curvature == 0.0 {
        return ((x0 * y2 - x2 * y0) / (y2 - y0), false);
    }
    let offset = (y0 - y2) / (x0 - x2) / 2.0 / curvature;
    ((x0 + x2) / 2.0 - offset, false)
}

#[cfg(test)]
mod tests {
    use super::ENTRIES;
    use super::ENTRY_SECONDS;
    use super::History;
    use super::Mode;
    use super::Sampler;
    use super::Weight;
    use super::log_term;
    use super::three_point;

    /// An EFC less c·s, counts, low enough that 2700 samples of it sum
    /// without rounding in single precision; near the DAC's middle the
    /// sum rounds, as the image's does.
    const LEVEL: f32 = 1000.0;

    /// A history sampled from uptime `start` for `entries` entries of a
    /// constant EFC `efc(entry)` each.
    fn sampled(start: u32, entries: u32, efc: impl Fn(u32) -> f32) -> History {
        let mut sampler = Sampler::new(start);
        let mut uptime = start;
        for entry in 0..entries {
            for _ in 0..ENTRY_SECONDS {
                uptime += 1;
                sampler.second(efc(entry), uptime);
            }
        }
        sampler.history().clone()
    }

    /// w is the mean of ln t over the window from y − 1 to y.
    #[test]
    fn the_log_term_is_the_window_mean_of_ln_t() {
        for time in [1.5_f32, 2.0, 10.0, 100.0] {
            let steps = 100_000;
            let mean = (0..steps)
                .map(|step| {
                    let t = f64::from(time) - 1.0 + (f64::from(step) + 0.5) / f64::from(steps);
                    t.ln()
                })
                .sum::<f64>()
                / f64::from(steps);
            assert!(
                (f64::from(log_term(time)) - mean).abs() < 1e-5,
                "{time}: {} against {mean}",
                log_term(time)
            );
        }
        assert!((log_term(1.0) + 1.0).abs() < f32::EPSILON);
    }

    /// Every entry of the first three hours is partial; after them a
    /// full window's is full.
    #[test]
    fn entries_are_weighted_by_uptime_and_samples() {
        let history = sampled(0, 6, |_| 1000.0);
        let weights: Vec<Weight> = (0..6).map(|index| history.weights[index]).collect();
        assert_eq!(
            weights,
            [
                Weight::Partial,
                Weight::Partial,
                Weight::Partial,
                Weight::Full,
                Weight::Full,
                Weight::Full
            ]
        );
        assert!((history.times[3] - 4.0).abs() < f32::EPSILON);
        assert!((history.logs[0] + 1.0).abs() < f32::EPSILON);
        assert!((history.averages[5] - 1000.0).abs() < f32::EPSILON);
    }

    #[test]
    fn the_mode_follows_the_entries() {
        let line = |entry| LEVEL + 5.0 * f32::from(u16::try_from(entry).expect("small"));
        let modes = [(2, Mode::Constant), (5, Mode::Few)];
        for (entries, mode) in modes {
            assert_eq!(sampled(0, entries, line).fit().expect("a fit").mode, mode);
        }
        // Six well weighted, after the three partial hours: the log
        // curve, taken below 16 whatever the line's rms.
        assert_eq!(sampled(0, 9, line).fit().expect("a fit").mode, Mode::Log);
        // Past four days, a straight line.
        let late = 200 * ENTRY_SECONDS;
        assert_eq!(
            sampled(late, 4, line).fit().expect("a fit").mode,
            Mode::Line
        );
        // A full ring past four days: its halves.
        let full = u32::try_from(ENTRIES).expect("64");
        assert_eq!(
            sampled(late, full, line).fit().expect("a fit").mode,
            Mode::Halves
        );
    }

    /// Each line fit finds a straight history's slope, and meets its
    /// newest entry.
    #[test]
    fn a_line_finds_the_slope() {
        let slope = 5.0;
        let line = |entry| LEVEL + slope * f32::from(u16::try_from(entry).expect("small"));
        let late = 200 * ENTRY_SECONDS;
        for entries in [4, 64] {
            let curve = sampled(late, entries, line).fit().expect("a fit");
            assert!((curve.b - slope).abs() < 1e-2, "{curve:?}");
            assert!(curve.c.abs() < f32::EPSILON, "{curve:?}");
        }
    }

    /// The three-term fit finds c in a history of a + c·w and noise.
    /// Without the noise this history's search ends with its vertex
    /// failed and c at 0, as the image's code would.
    #[test]
    fn the_log_fit_finds_its_coefficient() {
        let c = -400.0;
        let history = sampled(3 * ENTRY_SECONDS, 30, |entry| {
            let entry = f32::from(u16::try_from(entry).expect("small"));
            let noise = 2.0 * (entry * 2.4).sin();
            LEVEL + c * log_term(entry + 4.0) + noise
        });
        let curve = history.fit().expect("a fit");
        assert_eq!(curve.mode, Mode::Log);
        assert!((curve.c / c - 1.0).abs() < 0.05, "{curve:?}");
        assert!(curve.b.abs() < 0.05 * c.abs(), "{curve:?}");
    }

    #[test]
    fn the_vertex_of_a_parabola() {
        let parabola = |x: f32| 3.0 * (x - 2.0) * (x - 2.0) + 1.0;
        let (vertex, failed) = three_point([0.0, 1.0, 5.0], [0.0, 1.0, 5.0].map(parabola));
        assert!(!failed);
        assert!((vertex - 2.0).abs() < 1e-5, "{vertex}");
        assert_eq!(three_point([0.0, 1.0, 2.0], [1.0, 2.0, 3.0]), (0.0, true));
    }
}
