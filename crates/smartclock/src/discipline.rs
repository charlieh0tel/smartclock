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
//! immediate.

use std::ops::Add;
use std::ops::Div;
use std::ops::Mul;
use std::ops::Neg;
use std::ops::Sub;

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
}

/// The normal loop, `pll_normal`, closed around an oscillator.
#[derive(Debug, Clone, PartialEq)]
pub struct ClosedLoop {
    /// The time interval now, seconds.
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
    /// K, k and a.
    gains: Gains<f64>,
    /// d, the aging slope fed to the integrator, counts a window,
    /// already within ±M.
    slope: f64,
    /// The seconds run.
    second: u32,
}

impl ClosedLoop {
    /// At rest, with time interval 0 and EFC `efc`, for the firmware's
    /// gain G and time constant τ, an oscillator of sensitivity S and an
    /// aging slope fed to the integrator (counts a window, clamped to
    /// ±M as the firmware does).
    pub fn new(gain: f64, tau: f64, sensitivity: f64, efc: f64, slope: f64) -> Self {
        Self {
            phase: 0.0,
            steering: Steering::new(efc),
            efc,
            sensitivity,
            rest: efc,
            gains: Gains::new(gain, tau),
            slope: aging_slope(slope, 0.0, 1.0, gain),
            second: 0,
        }
    }

    /// Ten seconds of readings and the update at their end.
    pub fn block(&mut self, disturbances: &[Disturbance]) -> Update {
        let mut sum = 0.0;
        for _ in 0..BLOCK_SECONDS {
            self.second += 1;
            let free: f64 = disturbances
                .iter()
                .map(|disturbance| disturbance.phase(self.second))
                .sum();
            self.phase += free - self.sensitivity * (self.efc - self.rest);
            sum += self.phase;
        }
        let interval = sum / f64::from(BLOCK_SECONDS);
        // Neither c·s nor the clamp to the DAC's range: the oscillator
        // model is linear, and the current is not modeled.
        self.efc = self.steering.steer(self.gains, interval, self.slope);
        Update {
            second: self.second,
            interval,
            efc: self.efc,
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
    use super::ClosedLoop;
    use super::Disturbance;
    use super::ENTRY_SECONDS;
    use super::INTEGRAL_SCALE;
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
}
