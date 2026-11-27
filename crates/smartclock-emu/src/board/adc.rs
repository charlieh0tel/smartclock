//! The ADC on the QSPI's chip select 1, an ADC0838 (ovens.md), as
//! `adc_read` (adc.c) uses it: a command word of `0xc000` with the
//! channel in bits 13 to 11, and a reply with the reading in bits 9 to
//! 2.
//!
//! Each channel converts the level on its input (analog.rs, the board's
//! sources, in the ADC's counts) with a little noise, about a third of
//! a count, never more than one: a fixed sequence from a seed,
//! `--seed`, so a run with the same seed is the same each time.  The
//! noise's spread is a pick.  A channel wired to nothing reads 0.
//!
//! The inputs are behind `Inputs`, so the ADC knows nothing of what
//! drives them: on the Unicorn board, `analog::Wiring`; on an
//! mc683xx-board board, its analog input ports.

use super::NOISE_SEED;
use super::noise::Noise;
use super::time::Cycles;

/// The command word's channel field.
const CHANNEL_SHIFT: u16 = 11;
const CHANNEL_MASK: u16 = 0x07;
/// The reading's place in the reply.
const REPLY_SHIFT: u16 = 2;

/// The noise's spread and its limit, in counts.
const NOISE_SIGMA: f64 = 0.35;
const NOISE_LIMIT: f64 = 1.0;

/// What drives the ADC's inputs.
pub(crate) trait Inputs {
    /// The level on channel `channel`'s input at `now`, in counts; `None`
    /// for one wired to nothing.
    fn level(&mut self, channel: u16, now: Cycles) -> Option<f64>;
}

/// The ADC: its noise's generator.
#[derive(Debug)]
pub(crate) struct Adc {
    /// The noise added to each reading.
    noise: Noise,
}

impl Default for Adc {
    fn default() -> Self {
        Self::new(NOISE_SEED)
    }
}

impl Adc {
    /// The ADC with its noise from `seed`.
    pub(crate) fn new(seed: u64) -> Self {
        Self {
            noise: Noise::new(seed),
        }
    }

    /// A conversion at `now` of the input `inputs` give: the reply to
    /// `command`.  After the reading, MSB first, the part shifts it out
    /// again LSB first, so bits 1 and 0 repeat the reading's bits 1 and
    /// 2, as `qspi_adc_test` (selftest.c) checks bits 4 and 3 against
    /// them.
    pub(crate) fn convert(&mut self, command: u16, now: Cycles, inputs: &mut impl Inputs) -> u16 {
        let channel = command >> CHANNEL_SHIFT & CHANNEL_MASK;
        let reading = self.reading(channel, now, inputs);
        reading << REPLY_SHIFT | (reading >> 1 & 1) << 1 | reading >> 2 & 1
    }

    /// Channel `channel`'s count at `now`.
    fn reading(&mut self, channel: u16, now: Cycles, inputs: &mut impl Inputs) -> u16 {
        let Some(level) = inputs.level(channel, now) else {
            return 0;
        };
        let count = (level + self.noise()).round();
        // the ADC's eight bits
        count.clamp(0.0, f64::from(u8::MAX)) as u16
    }

    /// The next of the noise's values, in counts.
    fn noise(&mut self) -> f64 {
        (NOISE_SIGMA * self.noise.normal()).clamp(-NOISE_LIMIT, NOISE_LIMIT)
    }
}

/// The ADC wired to its inputs: the part on the QSPI.
#[derive(Debug, Default)]
pub(crate) struct Wired<I> {
    /// The converter.
    pub(crate) adc: Adc,
    /// What drives its inputs.
    pub(crate) inputs: I,
}

impl<I: Inputs> Wired<I> {
    /// A conversion at `now`: the reply to `command`.
    pub(crate) fn exchange(&mut self, command: u16, now: Cycles) -> u16 {
        self.adc.convert(command, now, &mut self.inputs)
    }
}

#[cfg(test)]
mod tests {
    use super::super::analog::Wiring;
    use super::super::time::CPU_HZ;
    use super::super::time::Cycles;
    use super::Adc;
    use super::Wired;

    /// A time `seconds` after power-up.
    fn at(seconds: u64) -> Cycles {
        Cycles(seconds * CPU_HZ)
    }

    /// A channel's count.
    fn count(adc: &mut Wired<Wiring>, channel: u16, now: Cycles) -> f32 {
        f32::from(adc.exchange(0xc000 | channel << 11, now) >> 2)
    }

    /// What the firmware makes of a channel: its count times a scale,
    /// plus the 5 V supply's times another (adc.c).
    fn value(adc: &mut Wired<Wiring>, channel: u16, scale: f32, scale_5v: f32, now: Cycles) -> f32 {
        count(adc, channel, now) * scale + count(adc, 4, now) * scale_5v
    }

    #[test]
    fn the_reply_passes_the_self_tests_check() {
        let mut adc = Wired::<Wiring>::default();
        for _ in 0..100 {
            let reply = adc.exchange(0xe000, at(1));
            let byte = reply >> 2 & 0xff;
            assert_eq!(reply & 0x400, 0);
            assert_eq!(reply >> 4 & 1, reply & 1);
            assert_eq!(reply >> 3 & 1, reply >> 1 & 1);
            assert!(byte != 0 && byte != 0xff);
        }
    }

    #[test]
    fn the_supplies_read_inside_the_health_monitors_bounds() {
        let mut adc = Wired::<Wiring>::default();
        for second in (0..3600).step_by(7) {
            let now = at(second);
            assert!((4.5..=5.5).contains(&value(&mut adc, 4, 0.0242, 0.0, now)));
            assert!((14.0..=16.0).contains(&value(&mut adc, 2, 0.0763, 0.0, now)));
            assert!((-16.0..=-14.0).contains(&value(&mut adc, 6, 0.0438, -0.084, now)));
            assert!(value(&mut adc, 1, 0.0743, -0.0472, now) <= 6.8);
        }
    }

    #[test]
    fn the_supplies_are_noisy() {
        let mut adc = Wired::<Wiring>::default();
        let counts: std::collections::BTreeSet<u16> =
            (0..100).map(|_| adc.exchange(0xe000, at(1)) >> 2).collect();
        assert!(counts.len() > 1, "{counts:?}");
        assert!(
            counts.iter().all(|count| count.abs_diff(207) <= 1),
            "{counts:?}"
        );
    }

    #[test]
    fn the_noise_is_the_seeds() {
        let readings = |seed| {
            let mut adc = Wired {
                adc: Adc::new(seed),
                inputs: Wiring::default(),
            };
            (0..32)
                .map(|_| adc.exchange(0xe000, at(1)))
                .collect::<Vec<_>>()
        };
        assert_eq!(readings(1), readings(1));
        assert_ne!(readings(1), readings(2));
    }

    /// The oven channel's value, averaged over ten readings as the
    /// health monitor averages it (monitors.c, `monitor_average_add`).
    fn oven(adc: &mut Wired<Wiring>, now: Cycles) -> f32 {
        (0..10)
            .map(|_| value(adc, 5, 0.0743, -0.0472, now))
            .sum::<f32>()
            / 10.0
    }

    #[test]
    fn the_oven_warms_up_in_minutes() {
        let mut adc = Wired::<Wiring>::default();
        // oven_warmed_up's test: below -2.0
        assert!(oven(&mut adc, at(1)) > 4.0);
        assert!(
            oven(&mut adc, at(300)) > -2.0,
            "still warming at five minutes"
        );
        assert!(oven(&mut adc, at(900)) < -2.0, "warm at fifteen");
        let current = |adc: &mut Wired<Wiring>, seconds| count(adc, 3, at(seconds)) * 250.0 / 255.0;
        assert!(current(&mut adc, 1) > 220.0);
        assert!(current(&mut adc, 400) < current(&mut adc, 200));
        assert!((current(&mut adc, 3600) - 100.0).abs() < 2.0);
    }

    #[test]
    fn the_temperature_rises_some_degrees() {
        let mut adc = Wired::<Wiring>::default();
        let degrees = |adc: &mut Wired<Wiring>, seconds| count(adc, 0, at(seconds)) * 0.273;
        assert!((degrees(&mut adc, 0) - 25.0).abs() < 1.0);
        assert!((degrees(&mut adc, 7200) - 35.0).abs() < 1.5);
    }
}
