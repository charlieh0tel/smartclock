//! The disciplining loop's update law, as `docs/firmware/loop.md`
//! states it ("The loop"), and that law closed around a model
//! oscillator.
//!
//! [`Steering`] is the update itself, generic over the float it runs
//! in: `f32` replays the firmware, which computes in single precision;
//! `f64` simulates.  [`ClosedLoop`] steps it a second at a time against
//! an oscillator whose phase, the receiver's time interval
//! `t_OCXO − t_GPS`, moves by `y − S·(u − u₀)` a second: `y` the
//! free-running fractional frequency offset, `u` the EFC in counts, `u₀`
//! the EFC the undisturbed oscillator is on frequency at and `S` the
//! oscillator's sensitivity, which the loop assumes is its gain G.  A
//! change of EFC acts from the second after the update that makes it,
//! and the counter's reading and the DAC's write are taken as
//! immediate.  The update adds `c·s`, a coefficient times the
//! oscillator current as the health monitor averages it ("s, the
//! oscillator current"), and may run the aging fit, [`crate::aging`],
//! for the slope it feeds its integrator.

use std::ops::Add;
use std::ops::Div;
use std::ops::Mul;
use std::ops::Neg;
use std::ops::Sub;

use crate::aging::Curve;
use crate::aging::FitError;
use crate::aging::Sampler;

/// A float the law runs in.
pub trait Real:
    Copy
    + PartialOrd
    + Add<Output = Self>
    + Sub<Output = Self>
    + Mul<Output = Self>
    + Div<Output = Self>
    + Neg<Output = Self>
{
    /// The nearest value to `value`.
    fn from_f64(value: f64) -> Self;
}

impl Real for f32 {
    #[expect(
        clippy::cast_possible_truncation,
        reason = "the firmware's constants are singles"
    )]
    fn from_f64(value: f64) -> Self {
        value as f32
    }
}

impl Real for f64 {
    fn from_f64(value: f64) -> Self {
        value
    }
}

/// The prefilter's numerator: a = `FILTER_SCALE` / τ.
pub const FILTER_SCALE: f64 = 29.75;

/// The integral gain's divisor: k = 1 / (`INTEGRAL_SCALE`·G·τ²).
pub const INTEGRAL_SCALE: f64 = 4.0;

/// The seconds between updates, each the mean of a reading a second.
pub const BLOCK_SECONDS: u32 = 10;

/// The aging fit's window, seconds: its slope is EFC counts a window.
pub const ENTRY_SECONDS: f64 = 2700.0;

/// The aging slope's clamp's numerator: M = `STEP_LIMIT` / |G|.
pub const STEP_LIMIT: f64 = 6.25e-10;

/// The weight of a fresh reading in the health monitor's average of
/// the oscillator current, s ← w·fresh + (1 − w)·s.
pub const CURRENT_WEIGHT: f64 = 0.1;

/// The EFC's top, counts; its bottom is 0.
pub const EFC_TOP: f64 = 1_048_560.0;

/// K, k and a for a gain and a time constant.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Gains<T> {
    /// K = 1 / (G·τ).
    pub proportional: T,
    /// k = 1 / (4·G·τ²).
    pub integral: T,
    /// a = 29.75 / τ.
    pub filter_weight: T,
}

impl<T: Real> Gains<T> {
    /// The gains for G and τ, computed in the order the firmware does.
    pub fn new(gain: T, tau: T) -> Self {
        let reciprocal = T::from_f64(1.0) / gain;
        Self {
            proportional: reciprocal / tau,
            integral: reciprocal / tau / tau / T::from_f64(INTEGRAL_SCALE),
            filter_weight: T::from_f64(FILTER_SCALE) / tau,
        }
    }
}

/// M, the bound on the aging slope, for a gain.
pub fn step_limit<T: Real>(gain: T) -> T {
    let magnitude = if gain < T::from_f64(0.0) { -gain } else { gain };
    T::from_f64(STEP_LIMIT) / magnitude
}

/// d, the aging fit's slope now, c·2700/q + b, within ±M: EFC counts a
/// window.
pub fn aging_slope<T: Real>(fit_b: T, fit_c: T, uptime: T, gain: T) -> T {
    let limit = step_limit(gain);
    let slope = fit_c * T::from_f64(ENTRY_SECONDS) / uptime + fit_b;
    if slope > limit {
        limit
    } else if slope < -limit {
        -limit
    } else {
        slope
    }
}

/// The state the update keeps between blocks.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Steering<T> {
    /// f, the filtered error, seconds.
    pub filtered: T,
    /// I, the integrator, EFC counts.
    pub integral: T,
    /// B, the base EFC set on entry to the stage, counts.
    pub base: T,
}

impl<T: Real> Steering<T> {
    /// At rest at an EFC of `base`.
    pub fn new(base: T) -> Self {
        let zero = T::from_f64(0.0);
        Self {
            filtered: zero,
            integral: zero,
            base,
        }
    }

    /// One update from a block's error e = x̄ − x₀ and the aging slope
    /// d: f ← (1 − a)·f + a·e, I ← I + 10·k·(f + d / (2700·k)), and
    /// u = K·f + B + I, before the c·s term and the clamp.
    pub fn steer(&mut self, gains: Gains<T>, error: T, slope: T) -> T {
        let one = T::from_f64(1.0);
        self.filtered = (one - gains.filter_weight) * self.filtered + gains.filter_weight * error;
        let drift = slope / gains.integral / T::from_f64(ENTRY_SECONDS) + self.filtered;
        self.integral =
            self.integral + drift * gains.integral * T::from_f64(f64::from(BLOCK_SECONDS));
        gains.proportional * self.filtered + self.base + self.integral
    }

    /// `startup_pll`'s adjustment for a step of its time constant from
    /// `previous` to `tau`: I ← I + (f / G)·(1/previous − 1/τ).
    pub fn rescale(&mut self, gain: T, previous: T, tau: T) {
        let one = T::from_f64(1.0);
        self.integral = self.integral + one / gain * self.filtered * (one / previous + -one / tau);
    }
}

/// A change in what the oscillator does, free of the loop, from a
/// second on.
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum Disturbance {
    /// The phase steps by `size` seconds at second `at`, as a move of
    /// the reference would.
    PhaseStep {
        /// The second it happens.
        at: u32,
        /// Seconds.
        size: f64,
    },
    /// The free-running fractional frequency steps by `size` at second
    /// `at`.
    FrequencyStep {
        /// The second it happens.
        at: u32,
        /// Fractional frequency.
        size: f64,
    },
    /// The free-running fractional frequency drifts by `rate` a second
    /// from second `at` on.
    Drift {
        /// The second it starts.
        at: u32,
        /// Fractional frequency a second.
        rate: f64,
    },
}

impl Disturbance {
    /// The phase it adds over the second ending at `second`.
    fn phase(self, second: u32) -> f64 {
        match self {
            Self::PhaseStep { at, size } => {
                if second == at {
                    size
                } else {
                    0.0
                }
            }
            Self::FrequencyStep { at, size } => {
                if second > at {
                    size
                } else {
                    0.0
                }
            }
            // The drift's integral over the second: its value at the
            // second's middle.
            Self::Drift { at, rate } => {
                if second > at {
                    rate * (f64::from(second - at) - 0.5)
                } else {
                    0.0
                }
            }
        }
    }
}

/// What the oscillator is measured against over a block: an EFC in
/// force and the mean time interval it gave.  The simulated phase is
/// kept as an offset from the reference's, moving by
/// `y − S·(u − efc)` a second, so the block's mean time interval is the
/// reference's plus the offset's mean.  The model's own reference is
/// the undisturbed oscillator, on frequency at its rest EFC with time
/// interval 0; a logged run's is what it logged.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Reference {
    /// The EFC in force over the block, counts.
    pub efc: f64,
    /// The block's mean time interval, seconds.
    pub interval: f64,
}

/// One update of a simulated loop.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Update {
    /// The second it ran, from the simulation's start.
    pub second: u32,
    /// x̄, the block's mean time interval, seconds: what the receiver
    /// reports.
    pub interval: f64,
    /// u, the EFC it set, counts.
    pub efc: f64,
    /// d, the aging slope it fed the integrator, counts a window.
    pub slope: f64,
}

/// The normal loop, `pll_normal`, closed around an oscillator.
#[derive(Debug, Clone, PartialEq)]
pub struct ClosedLoop {
    /// The time interval now less the [`Reference`]'s, seconds.
    pub phase: f64,
    /// The update's state.
    pub steering: Steering<f64>,
    /// The EFC in force, counts.
    pub efc: f64,
    /// The oscillator's fractional frequency a count, S.
    sensitivity: f64,
    /// The EFC at which the undisturbed oscillator is on frequency,
    /// counts.
    rest: f64,
    /// c, EFC counts a unit of oscillator current.
    coefficient: f64,
    /// s, the oscillator current as the health monitor averages it.
    current: f64,
    /// G, fractional frequency a count.
    gain: f64,
    /// K, k and a.
    gains: Gains<f64>,
    /// r, the aging fit's b the loop holds, counts a window.
    fit_b: f64,
    /// p, the aging fit's c the loop holds, counts a unit of w.
    fit_c: f64,
    /// q, the uptime, seconds.
    uptime: u32,
    /// The aging fit's sampler, when the fit is run.
    sampler: Option<Sampler>,
    /// A fit made, for the loop to take at the next second.
    pending: Option<Curve>,
    /// The fit the loop last took.
    curve: Option<Curve>,
    /// A fit the image's code does not determine; the fit stops there.
    fit_error: Option<FitError>,
    /// The seconds run.
    second: u32,
}

impl ClosedLoop {
    /// At rest, with time interval 0 and EFC `efc`, for the firmware's
    /// gain G and time constant τ, an oscillator of sensitivity S and an
    /// aging slope fed to the integrator (counts a window, clamped to
    /// ±M as the firmware does), at uptime 0.
    pub fn new(gain: f64, tau: f64, sensitivity: f64, efc: f64, slope: f64) -> Self {
        Self {
            phase: 0.0,
            steering: Steering::new(efc),
            efc,
            sensitivity,
            rest: efc,
            coefficient: 0.0,
            current: 0.0,
            gain,
            gains: Gains::new(gain, tau),
            fit_b: slope,
            fit_c: 0.0,
            uptime: 0,
            sampler: None,
            pending: None,
            curve: None,
            fit_error: None,
            second: 0,
        }
    }

    /// With the aging fit's b and c the loop holds, r and p, counts a
    /// window and a unit of w, at uptime `uptime`, seconds: d is then
    /// p·2700/q + r, within ±M, q the uptime at each update.
    #[must_use]
    pub fn with_aging(mut self, fit_b: f64, fit_c: f64, uptime: u32) -> Self {
        self.fit_b = fit_b;
        self.fit_c = fit_c;
        self.uptime = uptime;
        self
    }

    /// Running the 58503A 3704's aging fit (`crate::aging`) from an
    /// empty history: from now on its sampler sums u − c·s a second,
    /// each 2700 s it fits the history, and the loop takes the fit's b
    /// and c as r and p a second later.
    #[must_use]
    pub fn with_aging_fit(mut self) -> Self {
        self.sampler = Some(Sampler::new(self.uptime));
        self
    }

    /// The fit the loop last took from the aging fit.
    pub fn curve(&self) -> Option<Curve> {
        self.curve
    }

    /// A fit the image's code does not determine, which stopped the
    /// aging fit.
    pub fn fit_error(&self) -> Option<FitError> {
        self.fit_error
    }

    /// One second of the aging fit: the loop takes a fit made, and the
    /// sampler sums this second's u − c·s, fitting at an entry.
    fn age(&mut self) {
        if let Some(curve) = self.pending.take() {
            self.fit_b = f64::from(curve.b);
            self.fit_c = f64::from(curve.c);
            self.curve = Some(curve);
        }
        let sample = f32::from_f64(self.efc - self.coefficient * self.current);
        let Some(sampler) = self.sampler.as_mut().filter(|_| self.fit_error.is_none()) else {
            return;
        };
        if sampler.second(sample, self.uptime) {
            match sampler.history().fit() {
                Ok(curve) => self.pending = Some(curve),
                Err(error) => self.fit_error = Some(error),
            }
        }
    }

    /// With the term c·s, c in counts a unit of oscillator current and s
    /// that current, B set to the EFC less c·s so the EFC holds, as the
    /// firmware sets it on entering the loop.
    #[must_use]
    pub fn with_current_term(mut self, coefficient: f64, current: f64) -> Self {
        self.coefficient = coefficient;
        self.current = current;
        self.steering.base = self.efc - coefficient * current;
        self
    }

    /// Picks up a running loop: time interval `interval`, filtered error
    /// f = `filtered`, and the integrator taken into B so that the EFC
    /// in force, K·f + B + I + c·s, is where it is.
    pub fn resume(&mut self, interval: f64, filtered: f64) {
        self.phase = interval;
        self.steering.filtered = filtered;
        self.steering.integral = 0.0;
        self.steering.base =
            self.efc - self.coefficient * self.current - self.gains.proportional * filtered;
    }

    /// A fresh reading of the oscillator current into s, the health
    /// monitor's average: s ← w·fresh + (1 − w)·s.
    pub fn read_current(&mut self, fresh: f64) {
        self.current = CURRENT_WEIGHT * fresh + (1.0 - CURRENT_WEIGHT) * self.current;
    }

    /// K, k and a.
    pub fn gains(&self) -> Gains<f64> {
        self.gains
    }

    /// Ten seconds of readings and the update at their end, against the
    /// undisturbed oscillator.
    pub fn block(&mut self, disturbances: &[Disturbance]) -> Update {
        let rest = Reference {
            efc: self.rest,
            interval: 0.0,
        };
        self.block_against(disturbances, rest)
    }

    /// Ten seconds of readings and the update at their end, against
    /// `reference`.
    pub fn block_against(&mut self, disturbances: &[Disturbance], reference: Reference) -> Update {
        let mut sum = 0.0;
        for _ in 0..BLOCK_SECONDS {
            self.second += 1;
            self.uptime = self.uptime.saturating_add(1);
            self.age();
            let free: f64 = disturbances
                .iter()
                .map(|disturbance| disturbance.phase(self.second))
                .sum();
            self.phase += free - self.sensitivity * (self.efc - reference.efc);
            sum += self.phase;
        }
        let interval = reference.interval + sum / f64::from(BLOCK_SECONDS);
        let slope = aging_slope(self.fit_b, self.fit_c, f64::from(self.uptime), self.gain);
        // No clamp to the DAC's range: the oscillator model is linear.
        self.efc =
            self.steering.steer(self.gains, interval, slope) + self.coefficient * self.current;
        Update {
            second: self.second,
            interval,
            efc: self.efc,
            slope,
        }
    }

    /// The updates over `seconds`, from rest.
    pub fn run(&mut self, disturbances: &[Disturbance], seconds: u32) -> Vec<Update> {
        (0..seconds / BLOCK_SECONDS)
            .map(|_| self.block(disturbances))
            .collect()
    }
}

/// The update as a matrix on (time interval, f, I) at an update, with
/// the EFC K·f + I acting until the next, for τ and G = S = 1 (G
/// cancels): each column what one block makes of a unit of one of
/// them.  The loop's poles are its eigenvalues.
pub fn update_matrix(tau: f64) -> [[f64; 3]; 3] {
    let columns = [0, 1, 2].map(|unit| {
        let mut closed = ClosedLoop::new(1.0, tau, 1.0, 0.0, 0.0);
        let state = [0, 1, 2].map(|index| if index == unit { 1.0 } else { 0.0 });
        closed.phase = state[0];
        closed.steering.filtered = state[1];
        closed.steering.integral = state[2];
        closed.efc = closed.gains.proportional * state[1] + state[2];
        closed.block(&[]);
        [
            closed.phase,
            closed.steering.filtered,
            closed.steering.integral,
        ]
    });
    [0, 1, 2].map(|row| [0, 1, 2].map(|column| columns[column][row]))
}

#[cfg(test)]
mod tests {
    use super::BLOCK_SECONDS;
    use super::ClosedLoop;
    use super::Disturbance;
    use super::ENTRY_SECONDS;
    use super::INTEGRAL_SCALE;
    use super::Reference;
    use super::Update;

    /// The 58503A's G, fractional frequency a count.
    const GAIN: f64 = 6.25e-13;

    /// The 58503A's τ, seconds.
    const TAU: f64 = 700.0;

    /// Long enough for the slow pole, near 2.7·τ, to die away.
    const SETTLE: u32 = 40 * 700;

    /// A loop with the 58503A's constants at EFC 0, its oscillator `S`
    /// times as sensitive as G says.
    fn closed(ratio: f64, slope: f64) -> ClosedLoop {
        ClosedLoop::new(GAIN, TAU, ratio * GAIN, 0.0, slope)
    }

    fn last(updates: &[Update]) -> Update {
        *updates.last().expect("an update")
    }

    #[test]
    fn a_loop_at_rest_stays_there() {
        let mut closed = ClosedLoop::new(GAIN, TAU, GAIN, 1000.0, 0.0);
        for update in closed.run(&[], 3600) {
            assert!(update.interval.abs() < f64::EPSILON);
            assert!((update.efc - 1000.0).abs() < f64::EPSILON);
        }
    }

    #[test]
    fn a_phase_step_is_removed_and_the_efc_comes_back() {
        let step = 100e-9;
        let updates = closed(1.0, 0.0).run(&[Disturbance::PhaseStep { at: 1, size: step }], SETTLE);
        let final_update = last(&updates);
        assert!(final_update.interval.abs() < 1e-3 * step);
        assert!(final_update.efc.abs() < 1e-3 * step / GAIN / TAU);
    }

    /// The phase runs off at the step's rate until the loop answers,
    /// sooner and less far the more sensitive the oscillator, then
    /// comes back to zero, the EFC having taken the step up.
    #[test]
    fn a_frequency_step_peaks_and_the_efc_takes_it_up() {
        let size = -1e-9;
        let mut previous: Option<Update> = None;
        for ratio in [0.5, 1.0, 2.0] {
            let updates =
                closed(ratio, 0.0).run(&[Disturbance::FrequencyStep { at: 0, size }], SETTLE);
            let peak = *updates
                .iter()
                .min_by(|a, b| a.interval.total_cmp(&b.interval))
                .expect("a peak");
            assert!(
                peak.interval < 0.0 && peak.interval > size * 4.0 * TAU,
                "S/G {ratio}: peak {:e}",
                peak.interval
            );
            assert!(f64::from(peak.second) < 4.0 * TAU, "S/G {ratio}");
            if let Some(previous) = previous {
                assert!(peak.interval > previous.interval && peak.second < previous.second);
            }
            previous = Some(peak);
            let final_update = last(&updates);
            assert!(
                final_update.interval.abs() < 1e-3 * peak.interval.abs(),
                "S/G {ratio}: {:e} left",
                final_update.interval
            );
            let taken = size / (ratio * GAIN);
            assert!(
                (final_update.efc / taken - 1.0).abs() < 1e-3,
                "S/G {ratio}: EFC {} for {taken}",
                final_update.efc
            );
        }
    }

    #[test]
    fn steps_add() {
        let first = Disturbance::FrequencyStep { at: 0, size: 1e-9 };
        let second = Disturbance::FrequencyStep {
            at: 3000,
            size: -3e-9,
        };
        let both = closed(1.0, 0.0).run(&[first, second], 6000);
        let alone = [first, second].map(|one| closed(1.0, 0.0).run(&[one], 6000));
        for (index, update) in both.iter().enumerate() {
            let sum = alone[0][index].interval + alone[1][index].interval;
            assert!((update.interval - sum).abs() < 1e-15);
        }
    }

    /// A drift D held by the integrator alone, k·f = D/S a second, stands
    /// the error at f = 4·G·τ²·D/S, less what an aging slope d takes:
    /// f = 4·G·τ²·(D/S − d/2700).
    #[test]
    fn a_drift_stands_the_error_where_the_law_puts_it() {
        let rate = 1e-9 / 86_400.0;
        let drift = [Disturbance::Drift { at: 0, rate }];
        for ratio in [0.5, 1.0, 2.0] {
            let standing = INTEGRAL_SCALE * GAIN * TAU * TAU * rate / (ratio * GAIN);
            let error = last(&closed(ratio, 0.0).run(&drift, SETTLE)).interval;
            assert!(
                (error / standing - 1.0).abs() < 1e-3,
                "S/G {ratio}: {error:e} for {standing:e}"
            );
            let slope = rate / (ratio * GAIN) * ENTRY_SECONDS;
            let error = last(&closed(ratio, slope).run(&drift, SETTLE)).interval;
            assert!(error.abs() < 1e-3 * standing, "S/G {ratio}: {error:e}");
        }
    }

    /// The 58503A's c, counts a unit of oscillator current.
    const COEFFICIENT: f64 = -33.65;

    /// The largest time interval over a run in which the current steps
    /// by `step` units at second 0, the oscillator's frequency stepping
    /// with it by what c says, S·c·step, for a loop applying `coefficient`.
    fn worst_interval(coefficient: f64, step: f64) -> f64 {
        let current = 110.0;
        let mut closed = closed(1.0, 0.0).with_current_term(coefficient, current);
        let size = GAIN * COEFFICIENT * step;
        let disturbances = [Disturbance::FrequencyStep { at: 0, size }];
        (0..SETTLE / BLOCK_SECONDS)
            .map(|_| {
                closed.read_current(current + step);
                closed.block(&disturbances).interval.abs()
            })
            .fold(0.0, f64::max)
    }

    /// With c right, c·s takes up a change of frequency that follows the
    /// current, lagging only by the monitor's average.
    #[test]
    fn the_current_term_takes_up_what_follows_the_current() {
        let without = worst_interval(0.0, 1.0);
        let with = worst_interval(COEFFICIENT, 1.0);
        assert!(with < 0.2 * without, "{with:e} against {without:e}");
    }

    /// A constant current moves nothing: B takes c·s up on entry.
    #[test]
    fn a_constant_current_is_taken_into_the_base() {
        let mut closed = closed(1.0, 0.0).with_current_term(COEFFICIENT, 110.0);
        for _ in 0..100 {
            closed.read_current(110.0);
            let update = closed.block(&[]);
            assert!(update.interval.abs() < f64::EPSILON);
            assert!(update.efc.abs() < 1e-9);
        }
    }

    /// A loop picked up running holds its EFC: the first update moves
    /// it only by the integrator's step, 10·k·f.
    #[test]
    fn a_resumed_loop_holds_its_efc() {
        let interval = 20e-9;
        let mut closed =
            ClosedLoop::new(GAIN, TAU, GAIN, 700_000.0, 0.0).with_current_term(COEFFICIENT, 110.0);
        closed.resume(interval, interval);
        let reference = Reference {
            efc: 700_000.0,
            interval: 0.0,
        };
        let update = closed.block_against(&[], reference);
        let step = f64::from(BLOCK_SECONDS) * closed.gains().integral * interval;
        assert!(
            (update.efc - 700_000.0 - step).abs() < 1e-6,
            "{}",
            update.efc
        );
    }

    #[test]
    fn the_fit_s_window_is_the_law_s() {
        assert!((f64::from(crate::aging::ENTRY_SECONDS) - ENTRY_SECONDS).abs() < f64::EPSILON);
    }

    /// The aging fit learns a drift and feeds it to the integrator,
    /// taking the standing error down from 4·G·τ²·D/S.
    #[test]
    fn the_aging_fit_takes_up_a_drift() {
        let rate = 1e-9 / 86_400.0;
        let drift = [Disturbance::Drift { at: 0, rate }];
        let standing = INTEGRAL_SCALE * GAIN * TAU * TAU * rate / GAIN;
        let days = 3 * 86_400;
        let mut closed = ClosedLoop::new(GAIN, TAU, GAIN, 524_288.0, 0.0)
            .with_aging(0.0, 0.0, 86_400)
            .with_aging_fit();
        let error = last(&closed.run(&drift, days)).interval;
        let curve = closed.curve().expect("a fit");
        let slope = rate / GAIN * ENTRY_SECONDS;
        assert!(
            (f64::from(curve.b) / slope - 1.0).abs() < 0.1,
            "{curve:?} for {slope}"
        );
        assert!(error.abs() < 0.1 * standing, "{error:e} for {standing:e}");
    }
}
