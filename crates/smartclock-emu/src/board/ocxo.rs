//! The OCXO: a 10 MHz oscillator whose frequency follows its EFC.  It
//! knows nothing of what it clocks; its input is the EFC's DAC word, as
//! the DAC's output holds it, and its output a clock signal
//! (clock.rs), which the board takes to the FPGA's clock input.
//!
//! Its fractional frequency offset, `y`, is
//!
//!     y = offset + gain x 16 x (word - 0x8000)
//!           + aging x days + warm_up x heating + noise
//!
//! - *The EFC.*  `efc_write_dithered` (dac.c) writes the loop's 20-bit
//!   EFC, dithered, as its top 16 bits (`EFC_DAC_SHIFT`, 4) to the
//!   AD569 on QSPI device 7, and `efc_write` then writes 0 to
//!   `efc_strobe`, `0x306000` (restart.md, "The chip selects", where the
//!   strobe is "not identified").  *Inferred:* the strobe loads the
//!   word into the DAC's output, since `qspi_efc_test` (selftest.c)
//!   writes its patterns without one; so a word reaches the oscillator
//!   only with its strobe.  The gain is the firmware's G, 6.25e-13 of
//!   frequency an EFC count (structures.md, "The EFC's gain";
//!   `efc_gain_set`), 1e-11 a step of the DAC, the frequency rising
//!   with the count (counter.md, "The sign, and the loop", inferred
//!   there; efc.md, "The pull, measured", has it rising on a 58503A).
//!   Real units differ from G: 3.94e-13 measured on a 58503A (efc.md),
//!   6.0e-13 on the bench Z3801A (ocxo.md), so the gain is an option.
//!   The middle, where the EFC adds nothing, is the 20-bit range's,
//!   524288 (structures.md, "The lifetime record": the EFC "as
//!   mid-scale"), the DAC's `0x8000`; before the first strobe the DAC
//!   holds that, a pick.
//! - *Aging,* fractional frequency a day, linear from the run's first
//!   power-up, a power cycle carrying it (`Crystal`); the 10811-60159's
//!   is under 2.5e-10 a day (ocxo.md, "Specification").
//! - *Warm-up,* the offset at power-up, falling as the oven settles, in
//!   the shape of the oven's heating (analog.rs, `heating`: full power four
//!   minutes, then a time constant of 150 s); the 10811 is within 5e-9
//!   of its final frequency ten minutes after turn-on (ocxo.md,
//!   "Power"), which a warm-up of 5.5e-8 or less meets.
//! - *Noise,* drawn once a second of the board's time from the board's
//!   seed: white frequency noise, each second's frequency off by a
//!   normal value of the spread given (so a random walk of phase), and
//!   a random walk of frequency, each second's step of the spread
//!   given.  A power cycle carries the generator's place and the walk's
//!   sum, so that a seeded run's draws go on rather than repeat.
//!
//! Every term defaults to 0 but the gain: an oscillator exactly on
//! frequency at the EFC's middle.
//!
//! The phase is integrated exactly: `y` is held constant between
//! changes (each strobe, and each slice of the board's time when aging,
//! warm-up or noise move it), quantized to 1e-18, and the phase is kept
//! as whole parts and a remainder, so that no rounding accumulates and
//! the edges over any span are the integral's, whatever the board's
//! cycles are.  A time after the last change is projected at the
//! present frequency: the board asks again once it has moved on.  The
//! history is kept for `RETAIN_SECONDS`, 64 s; a time older than that is
//! extrapolated from the oldest kept, not exact.

use std::collections::VecDeque;

use super::analog::Warmth;
use super::clock::Clock;
use super::clock::IDEAL_HZ;
use super::clock::Phase;
use super::clock::Tick;
use super::noise::Noise;
use super::time::CPU_HZ;
use super::time::Cycles;

/// The phase's parts: a board cycle is `PARTS_PER_CYCLE` of them at the
/// nominal frequency, an oscillator period `PARTS_PER_TICK`, both whole.
const PARTS_PER_CYCLE: i128 = IDEAL_HZ as i128;
const PARTS_PER_TICK: i128 = CPU_HZ as i128;
/// The unit of `y` in the phase's rate: 1e-18.
const RATE_SCALE: i128 = 1_000_000_000_000_000_000;

/// The firmware's G: fractional frequency an EFC count
/// (structures.md, "The EFC's gain").
pub const EFC_GAIN: f64 = 6.25e-13;
/// The EFC's counts a DAC step: the DAC takes the top 16 of 20 bits
/// (dac.c, `EFC_DAC_SHIFT`).
const COUNTS_PER_STEP: f64 = 16.0;
/// The DAC's word at the EFC's middle, 524288 counts.
const MIDDLE: u16 = 0x8000;
/// The EFC's middle, counts.
pub(crate) const MIDDLE_COUNTS: f64 = COUNTS_PER_STEP * MIDDLE as f64;
/// The offset at the EFC's middle that puts an oscillator of EFC gain
/// `efc_gain` on frequency at the EFC `efc`, counts: where `y`'s EFC
/// term cancels it.
#[cfg(feature = "mc683xx")]
pub(crate) const fn on_frequency_at(efc_gain: f64, efc: f64) -> f64 {
    -efc_gain * (efc - MIDDLE_COUNTS)
}
/// Seconds a day, for the aging.
const SECONDS_PER_DAY: f64 = 86_400.0;
/// The noise's interval: a draw each second of the board's time.
const NOISE_INTERVAL: Cycles = Cycles(CPU_HZ);
/// How long the history is kept, seconds of the board's time.
const RETAIN_SECONDS: i128 = 64;
/// What the noise's seed is mixed with, so that the oscillator's draws
/// are not the ADC's.
const NOISE_STREAM: u64 = 0x0c40_0c40_0c40_0c40;
/// The normals a draw takes: the white term's and the walk's step.
const NORMALS_PER_DRAW: u64 = 2;

/// The oscillator's parameters, fractional frequencies.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Config {
    /// The offset at the EFC's middle, past the warm-up and before any
    /// aging.
    pub offset: f64,
    /// The aging, a day.
    pub aging: f64,
    /// The warm-up's offset at power-up, falling to nothing.
    pub warm_up: f64,
    /// The EFC's gain, an EFC count.
    pub efc_gain: f64,
    /// The white frequency noise's spread, each second.
    pub white_fm: f64,
    /// The random walk of frequency's spread, each second's step.
    pub random_walk_fm: f64,
}

/// An oscillator's parameter it cannot run with.
#[derive(Debug, Clone, Copy, PartialEq, thiserror::Error)]
pub(crate) enum OcxoError {
    /// A term that is not a finite number, NaN or infinite: the phase's
    /// rate would be none.
    #[error("the oscillator's {term} is {value}, not a finite number")]
    NotFinite {
        /// The term, as `Config` names it.
        term: &'static str,
        /// Its value.
        value: f64,
    },
}

impl OcxoError {
    /// The term, as `Config` names it.
    pub(crate) fn term(self) -> &'static str {
        match self {
            Self::NotFinite { term, .. } => term,
        }
    }
}

impl Config {
    /// The parameters, if every term is a finite number.
    fn checked(self) -> Result<Self, OcxoError> {
        let terms = [
            ("offset", self.offset),
            ("aging", self.aging),
            ("warm_up", self.warm_up),
            ("efc_gain", self.efc_gain),
            ("white_fm", self.white_fm),
            ("random_walk_fm", self.random_walk_fm),
        ];
        match terms.into_iter().find(|(_, value)| !value.is_finite()) {
            Some((term, value)) => Err(OcxoError::NotFinite { term, value }),
            None => Ok(self),
        }
    }
}

impl Default for Config {
    fn default() -> Self {
        Self {
            offset: 0.0,
            aging: 0.0,
            warm_up: 0.0,
            efc_gain: EFC_GAIN,
            white_fm: 0.0,
            random_walk_fm: 0.0,
        }
    }
}

/// The oscillator's terms a run gives, fractional frequencies, each
/// `None` to keep the board file's or, on a board without one, the
/// default (`Config::default`): the command line's `--ocxo-*`.
#[derive(Debug, Clone, Copy, Default, PartialEq)]
pub struct Terms {
    /// `Config::offset`.
    pub offset: Option<f64>,
    /// `Config::aging`.
    pub aging: Option<f64>,
    /// `Config::warm_up`.
    pub warm_up: Option<f64>,
    /// `Config::efc_gain`.
    pub efc_gain: Option<f64>,
    /// `Config::white_fm`.
    pub white_fm: Option<f64>,
    /// `Config::random_walk_fm`.
    pub random_walk_fm: Option<f64>,
}

impl Terms {
    /// `config` with each term given here in place of its own.
    pub(crate) fn over(self, config: Config) -> Config {
        Config {
            offset: self.offset.unwrap_or(config.offset),
            aging: self.aging.unwrap_or(config.aging),
            warm_up: self.warm_up.unwrap_or(config.warm_up),
            efc_gain: self.efc_gain.unwrap_or(config.efc_gain),
            white_fm: self.white_fm.unwrap_or(config.white_fm),
            random_walk_fm: self.random_walk_fm.unwrap_or(config.random_walk_fm),
        }
    }
}

/// What the crystal keeps through a power cycle, which belongs to it and
/// not to the power: its aging, and its noise's place, so that neither
/// starts again (docs/emulator.md, "The controls").  The warm-up is the
/// oven's (`Warmth`), carried beside it.
#[derive(Debug, Clone, Copy, PartialEq, Default)]
pub(crate) struct Crystal {
    /// Seconds of aging behind it at power-up.
    pub(crate) aged: f64,
    /// The noise's draws taken, which the generator skips.
    pub(crate) draws: u64,
    /// The random walk of frequency's sum.
    pub(crate) walk: f64,
}

/// A span over which the frequency is constant: from `start`, a time in
/// the board's parts (`PARTS_PER_CYCLE` a cycle), the phase is
/// `phase + floor(((t - start) x rate + remainder) / RATE_SCALE)`.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
struct Segment {
    /// Where it starts, in the board's parts.
    start: i128,
    /// The phase there, whole parts, and the remainder below a part, in
    /// `RATE_SCALE`ths, from 0 up to `RATE_SCALE`.
    phase: i128,
    remainder: i128,
    /// Phase parts a board part, in `RATE_SCALE`ths: `RATE_SCALE` x
    /// (1 + y).
    rate: i128,
}

impl Segment {
    /// The phase at `at`, the board's parts, at this segment's rate.
    fn phase_at(&self, at: i128) -> (i128, i128) {
        let total = (at - self.start) * self.rate + self.remainder;
        (
            self.phase + total.div_euclid(RATE_SCALE),
            total.rem_euclid(RATE_SCALE),
        )
    }

    /// The first time, the board's parts, at which the phase reaches
    /// `phase`, at this segment's rate.
    fn reaching(&self, phase: i128) -> i128 {
        let wanted = (phase - self.phase) * RATE_SCALE - self.remainder;
        self.start + ceiling(wanted, self.rate)
    }
}

/// `value` over `divisor`, positive, rounded up.
fn ceiling(value: i128, divisor: i128) -> i128 {
    -(-value).div_euclid(divisor)
}

/// The oscillator.
#[derive(Debug)]
pub(crate) struct Ocxo {
    config: Config,
    /// How far its warm-up has come, which its oven's does (analog.rs).
    warmth: Warmth,
    /// The DAC's word, as its last strobe loaded it.
    word: u16,
    /// Seconds of aging behind it at power-up.
    aged: f64,
    /// The noise's generator, the draws taken, the white term drawn last
    /// and the random walk's sum, and the next draw's time, while there
    /// is noise.
    noise: Noise,
    draws: u64,
    white: f64,
    walk: f64,
    next_draw: Option<Cycles>,
    /// The frequency's history, oldest first, never empty.
    segments: VecDeque<Segment>,
}

impl Default for Ocxo {
    fn default() -> Self {
        Self::new(Config::default(), super::NOISE_SEED).expect("the defaults are finite")
    }
}

impl Ocxo {
    /// The oscillator at the run's first power-up, with `config` and its
    /// noise from `seed`; refused for a term not a finite number.
    pub(crate) fn new(config: Config, seed: u64) -> Result<Self, OcxoError> {
        Self::resumed(config, seed, Crystal::default())
    }

    /// The oscillator at a power-up, its crystal as `crystal` left it;
    /// refused for a term not a finite number.
    pub(crate) fn resumed(config: Config, seed: u64, crystal: Crystal) -> Result<Self, OcxoError> {
        let config = config.checked()?;
        let noisy = config.white_fm != 0.0 || config.random_walk_fm != 0.0;
        let mut noise = Noise::new(seed ^ NOISE_STREAM);
        noise.skip(crystal.draws.wrapping_mul(NORMALS_PER_DRAW));
        let mut ocxo = Self {
            config,
            warmth: Warmth::default(),
            word: MIDDLE,
            aged: crystal.aged,
            noise,
            draws: crystal.draws,
            white: 0.0,
            walk: crystal.walk,
            next_draw: noisy.then_some(Cycles(0)),
            segments: VecDeque::new(),
        };
        ocxo.draw_due(Cycles(0));
        let rate = ocxo.rate(Cycles(0));
        ocxo.segments.push_back(Segment {
            start: 0,
            phase: 0,
            remainder: 0,
            rate,
        });
        Ok(ocxo)
    }

    /// Its warm-up moved to `warmth` at `at`, as its oven's is.
    #[cfg(feature = "mc683xx")]
    pub(crate) fn set_warmth(&mut self, warmth: Warmth, at: Cycles) {
        self.warmth = warmth;
        self.retune(at);
    }

    /// The EFC's DAC loaded with `word` at `at`, by its strobe.
    pub(crate) fn set_efc(&mut self, word: u16, at: Cycles) {
        self.word = word;
        self.retune(at);
    }

    /// Time moves on to `now`: the noise's draws due, and the frequency
    /// as aging and warm-up have moved it.
    pub(crate) fn advance(&mut self, now: Cycles) {
        self.draw_due(now);
        self.retune(now);
    }

    /// Whether its frequency moves by itself between the noise's draws:
    /// aging or warm-up.
    #[cfg(feature = "mc683xx")]
    pub(crate) fn drifts(&self) -> bool {
        self.config.aging != 0.0 || self.config.warm_up != 0.0
    }

    /// Its crystal as it stands at `now`, for the next power-up.
    #[cfg(feature = "mc683xx")]
    pub(crate) fn crystal(&self, now: Cycles) -> Crystal {
        Crystal {
            aged: self.aged + seconds(now),
            draws: self.draws,
            walk: self.walk,
        }
    }

    /// When the noise is next drawn, while there is noise.
    pub(crate) fn next_event(&self) -> Option<Cycles> {
        self.next_draw
    }

    /// The fractional frequency offset at `at`, as it would be retuned
    /// there.
    pub(crate) fn frequency(&self, at: Cycles) -> f64 {
        let Config {
            offset,
            aging,
            warm_up,
            efc_gain,
            ..
        } = self.config;
        let efc = efc_gain * COUNTS_PER_STEP * (f64::from(self.word) - f64::from(MIDDLE));
        offset
            + efc
            + aging * (self.aged + seconds(at)) / SECONDS_PER_DAY
            + warm_up * self.warmth.heating(at)
            + self.white
            + self.walk
    }

    /// The noise's draws due by `now`.
    fn draw_due(&mut self, now: Cycles) {
        while let Some(at) = self.next_draw.filter(|at| *at <= now) {
            self.white = self.config.white_fm * self.noise.normal();
            self.walk += self.config.random_walk_fm * self.noise.normal();
            self.draws += 1;
            self.next_draw = Some(at + NOISE_INTERVAL);
        }
    }

    /// The phase's rate at `at`.
    fn rate(&self, at: Cycles) -> i128 {
        // y is far inside i128 at 1e-18, and well above -1
        let y = (self.frequency(at) * RATE_SCALE as f64).round() as i128;
        RATE_SCALE.saturating_add(y).max(1)
    }

    /// A new segment from `at` at the frequency there, if it changed.
    fn retune(&mut self, at: Cycles) {
        let rate = self.rate(at);
        let last = *self.segments.back().expect("never empty");
        if rate == last.rate {
            return;
        }
        let start = parts(at).max(last.start);
        if start == last.start {
            self.segments.back_mut().expect("never empty").rate = rate;
        } else {
            let (phase, remainder) = last.phase_at(start);
            self.segments.push_back(Segment {
                start,
                phase,
                remainder,
                rate,
            });
        }
        let oldest = start - RETAIN_SECONDS * i128::from(CPU_HZ) * PARTS_PER_CYCLE;
        while self
            .segments
            .get(1)
            .is_some_and(|next| next.start <= oldest)
        {
            self.segments.pop_front();
        }
    }

    /// The segment in force at `at`, the board's parts: the oldest if
    /// `at` is before them all.
    fn segment_at(&self, at: i128) -> &Segment {
        let after = self.segments.partition_point(|segment| segment.start <= at);
        &self.segments[after.saturating_sub(1)]
    }
}

/// A time on the board's clock in seconds.
fn seconds(at: Cycles) -> f64 {
    at.0 as f64 / CPU_HZ as f64
}

/// A time on the board's clock in its parts.
fn parts(at: Cycles) -> i128 {
    i128::from(at.0) * PARTS_PER_CYCLE
}

/// Its own time is the noise's draws (`next_event`) and the aging and
/// warm-up retuned as time moves (`advance`).  Its net-facing side is
/// `set_efc`, the EFC in, and `Clock`, the 10 MHz out.
#[cfg(feature = "mc683xx")]
impl mc683xx_modules::peripheral::Timed for Ocxo {
    fn next_event(&self) -> Option<mc683xx_cpu32::Cycle> {
        Self::next_event(self).map(|at| mc683xx_cpu32::Cycle::from_clocks(at.0))
    }

    fn advance(&mut self, now: mc683xx_cpu32::Cycle) {
        Self::advance(self, Cycles(now.clocks()));
    }
}

impl Ocxo {
    /// The edges in the span `from` to `to`: after `from`, at or before
    /// `to`; none if `to` is not after `from`.  Exact.
    #[cfg(test)]
    fn edges(&self, from: Cycles, to: Cycles) -> u64 {
        if to <= from {
            return 0;
        }
        self.phase(to)
            .tick
            .0
            .saturating_sub(self.phase(from).tick.0)
    }

    /// The `n`th edge after `from`, `n` at least 1, rounded up to the
    /// board's next cycle.  Exact.
    #[cfg(test)]
    fn edge_after(&self, from: Cycles, n: u64) -> Cycles {
        self.reaching(self.phase(from).edge(n))
    }

    /// The first board cycle at which the phase has reached `phase`,
    /// whole parts.
    fn reaching_parts(&self, phase: i128) -> Cycles {
        // the last segment starting below the phase, or the first
        let after = self
            .segments
            .partition_point(|segment| segment.phase < phase);
        let segment = &self.segments[after.saturating_sub(1)];
        let reached = segment.reaching(phase).max(segment.start);
        // board times are never negative, and far short of u64's cycles
        Cycles(ceiling(reached, PARTS_PER_CYCLE).max(0) as u64)
    }
}

impl Clock for Ocxo {
    fn phase(&self, at: Cycles) -> Phase {
        let parts = parts(at);
        let (phase, remainder) = self.segment_at(parts).phase_at(parts);
        let within = phase.rem_euclid(PARTS_PER_TICK) as f64 + remainder as f64 / RATE_SCALE as f64;
        Phase {
            // the phase from power-up is never negative, and a run's
            // ticks fit u64
            tick: Tick(phase.div_euclid(PARTS_PER_TICK).max(0) as u64),
            fraction: within / PARTS_PER_TICK as f64,
        }
    }

    fn reaching(&self, tick: Tick) -> Cycles {
        self.reaching_parts(i128::from(tick.0) * PARTS_PER_TICK)
    }
}

#[cfg(test)]
mod tests {
    use super::super::clock::Clock;
    use super::super::clock::Ideal;
    use super::super::clock::Tick;
    use super::super::time::CPU_HZ;
    use super::super::time::Cycles;
    use super::Config;
    use super::Ocxo;
    use super::OcxoError;

    fn seconds(seconds: u64) -> Cycles {
        Cycles(seconds * CPU_HZ)
    }

    fn with(config: Config) -> Ocxo {
        Ocxo::new(config, 1).expect("finite")
    }

    fn offset(offset: f64) -> Ocxo {
        with(Config {
            offset,
            ..Config::default()
        })
    }

    #[test]
    fn on_frequency_it_is_the_ideal_clock() {
        let ocxo = Ocxo::default();
        for at in [0, 1, CPU_HZ, 12_345, 9_876_543, 3600 * CPU_HZ + 7] {
            let phase = ocxo.phase(Cycles(at));
            let ideal = Ideal.phase(Cycles(at));
            assert_eq!(phase.tick, ideal.tick, "{at}");
            assert!((phase.fraction - ideal.fraction).abs() < 1e-9, "{at}");
            assert_eq!(
                ocxo.reaching(phase.tick),
                Ideal.reaching(ideal.tick),
                "{at}"
            );
        }
        for tick in [0, 1, 10_000_000, 4_000_003, 36_000_000_005] {
            assert_eq!(
                ocxo.reaching(Tick(tick)),
                Ideal.reaching(Tick(tick)),
                "{tick}"
            );
        }
        assert_eq!(ocxo.edges(Cycles(0), seconds(1)), 10_000_000);
        assert_eq!(ocxo.edge_after(Cycles(0), 10_000_000), seconds(1));
    }

    /// 1e-8 fast, the phase runs ahead by 1e-8 a second.
    #[test]
    fn the_phase_runs_at_the_frequency() {
        let ocxo = offset(1e-8);
        let phase = ocxo.phase(seconds(1000));
        assert!((phase.seconds() - 1000.00001).abs() < 1e-12, "{phase:?}");
        assert_eq!(ocxo.reaching(Tick(10_000_000_100)), seconds(1000));
    }

    /// 1e-8 fast, a thousand seconds hold 1e10 + 100 periods, to the
    /// period, however finely the history is cut.
    #[test]
    fn the_edges_are_the_integral_with_no_drift() {
        let mut ocxo = offset(1e-8);
        assert_eq!(ocxo.edges(Cycles(0), seconds(1000)), 10_000_000_100);
        // retuned a slice at a time to the same frequency, and to the
        // middle and back each slice: the same count
        for slice in 0..(10 * CPU_HZ / 4096) {
            let at = Cycles(slice * 4096 + 1);
            ocxo.set_efc(super::MIDDLE + 1, at);
            ocxo.set_efc(super::MIDDLE, at);
        }
        assert_eq!(ocxo.edges(Cycles(0), seconds(1000)), 10_000_000_100);
    }

    #[test]
    fn the_nth_edge_after_is_where_the_count_reaches_n() {
        let mut ocxo = offset(3.3e-7);
        ocxo.set_efc(0x1234, seconds(2));
        for (from, n) in [(0, 1), (CPU_HZ, 10_000_000), (CPU_HZ + 17, 4_000_003)] {
            let edge = ocxo.edge_after(Cycles(from), n);
            assert!(ocxo.edges(Cycles(from), edge) >= n);
            assert!(ocxo.edges(Cycles(from), Cycles(edge.0 - 1)) < n);
        }
    }

    /// 1e-8 fast at the middle; the EFC 1000 steps down, 16000 counts
    /// of 6.25e-13, takes it back to 10 MHz.
    #[test]
    fn the_efc_moves_the_frequency_by_the_firmwares_gain() {
        let mut ocxo = offset(1e-8);
        assert_eq!(ocxo.edges(seconds(0), seconds(30)), 300_000_003);
        ocxo.set_efc(super::MIDDLE - 1000, seconds(30));
        assert_eq!(ocxo.edges(seconds(30), seconds(60)), 300_000_000);
        assert!(ocxo.frequency(seconds(30)).abs() < 1e-20);
        ocxo.set_efc(super::MIDDLE + 1000, seconds(60));
        assert_eq!(ocxo.edges(seconds(60), seconds(90)), 300_000_006);
        // the history before the changes stays as it was
        assert_eq!(ocxo.edges(seconds(0), seconds(30)), 300_000_003);
    }

    #[test]
    fn aging_and_warm_up_move_the_frequency() {
        let mut aging = with(Config {
            aging: 1e-10,
            ..Config::default()
        });
        aging.advance(seconds(86_400));
        assert!((aging.frequency(seconds(86_400)) - 1e-10).abs() < 1e-15);
        let warm = with(Config {
            warm_up: 5e-8,
            ..Config::default()
        });
        assert!((warm.frequency(seconds(0)) - 5e-8).abs() < 1e-15);
        assert!(
            warm.frequency(seconds(600)) < 5e-9,
            "the 10811's ten minutes"
        );
    }

    /// A power cycle at 5.5 s: the aging goes on from where it had come
    /// to, and the noise's draws go on rather than repeat, the resumed
    /// oscillator's first draw, at its power-up, the one the first
    /// would have drawn at 6 s.
    #[cfg(feature = "mc683xx")]
    #[test]
    fn a_resumed_crystal_goes_on_aging_and_drawing() {
        let config = Config {
            aging: 1e-6,
            white_fm: 1e-11,
            random_walk_fm: 1e-12,
            ..Config::default()
        };
        let mut first = with(config);
        let cut = Cycles(11 * CPU_HZ / 2);
        first.advance(cut);
        let mut resumed = Ocxo::resumed(config, 1, first.crystal(cut)).expect("finite");
        let aging = |seconds: f64| 1e-6 * seconds / super::SECONDS_PER_DAY;
        for second in 0..5 {
            first.advance(seconds(6 + second));
            resumed.advance(seconds(second));
            let first_noise = first.frequency(seconds(6 + second)) - aging((6 + second) as f64);
            let resumed_noise = resumed.frequency(seconds(second)) - aging(5.5 + second as f64);
            assert!((first_noise - resumed_noise).abs() < 1e-24, "{second}");
        }
    }

    #[test]
    fn the_noise_is_the_seeds_and_off_by_default() {
        assert_eq!(Ocxo::default().next_event(), None);
        let noisy = |seed| {
            let mut ocxo = Ocxo::new(
                Config {
                    white_fm: 1e-11,
                    random_walk_fm: 1e-12,
                    ..Config::default()
                },
                seed,
            )
            .expect("finite");
            (1..=10)
                .map(|second| {
                    ocxo.advance(seconds(second));
                    ocxo.frequency(seconds(second))
                })
                .collect::<Vec<_>>()
        };
        assert_eq!(noisy(1), noisy(1));
        assert_ne!(noisy(1), noisy(2));
        assert!(noisy(1).iter().all(|y| y.abs() < 1e-10));
    }

    /// A NaN or an infinity in any term is refused, naming the term:
    /// the phase's rate would otherwise be the cast's 0 or saturate.
    #[test]
    fn a_term_not_a_finite_number_is_refused() {
        for value in [f64::NAN, f64::INFINITY, f64::NEG_INFINITY] {
            let refused = Ocxo::new(
                Config {
                    warm_up: value,
                    ..Config::default()
                },
                1,
            );
            assert_eq!(refused.map(|_| ()).map_err(OcxoError::term), Err("warm_up"));
        }
        let refused = Ocxo::new(
            Config {
                efc_gain: f64::NAN,
                ..Config::default()
            },
            1,
        );
        assert!(matches!(
            refused,
            Err(OcxoError::NotFinite { term: "efc_gain", value }) if value.is_nan()
        ));
    }
}
