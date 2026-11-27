//! The levels the ADC reads (adc.rs): the oven, the temperature inside
//! and the supplies, each a model with analog outputs of its own, and
//! the board's wiring of them to the ADC's channels (smartclock.h's
//! `adc_channel`).  A level is in the ADC's counts, 1/255 of its full
//! scale, the unit both ends of each net agree on.
//!
//! Each level is chosen so that the firmware's own scalings (adc.c)
//! give a healthy receiver's values, inside the bounds the health
//! monitor holds them to (monitors.c, `hmon_check_supplies`: 5 V within
//! 4.5 to 5.5, +15 V within 14 to 16, -15 V within -16 to -14, the
//! double oven at most 6.8 V).  The board warms up from its power-up:
//! the oven's heater runs at full power for its first minutes, then
//! backs off to its steady draw (ocxo.md, "Power": 11 W at turn-on
//! dropping to about 2 W), so the oscillator's current and the oven
//! channel fall over minutes, and the oven channel crosses the -2.0
//! that `oven_warmed_up` (monitors.c) waits for after about eight
//! minutes, when `powerup` leaves the inner oven's warm-up
//! (`pll_stages.c`); the temperature inside rises some degrees over half
//! an hour.  The levels and the warm-up's times and end points are
//! picks, not measurements of a receiver: chosen to fall inside the
//! firmware's bounds and to cross its thresholds at plausible times,
//! the shape alone taken from the oven's power figures.

use super::adc::Inputs;
use super::time::CPU_HZ;
use super::time::Cycles;

/// The channels, by their numbers (smartclock.h's `adc_channel`).
const TEMPERATURE: u16 = 0;
const DOUBLE_OVEN: u16 = 1;
const PLUS_15V: u16 = 2;
const CURRENT: u16 = 3;
const PLUS_5V: u16 = 4;
const OVEN: u16 = 5;
const MINUS_15V: u16 = 6;

/// The steady levels, and what the firmware makes of them.
/// `ADC_DOUBLE_OVEN`: 199 x 0.0743 - 207 x 0.0472 = 5.0 V.
const DOUBLE_OVEN_COUNT: f64 = 199.0;
/// `ADC_PLUS_15V`: 197 x 0.0763 = 15.03 V.
const PLUS_15V_COUNT: f64 = 197.0;
/// `ADC_5V`: 207 x 0.0242 = 5.01 V.
const PLUS_5V_COUNT: f64 = 207.0;
/// `ADC_MINUS_15V`: 55 x 0.0438 - 207 x 0.084 = -14.98 V.
const MINUS_15V_COUNT: f64 = 55.0;
/// The Z3817A's 12B and -12B, its channels 4 and 5, within 11 to 13 V
/// and -13 to -11 V (its table at 0x32498): 157 x 0.0763 = 11.98 V,
/// 123 x 0.0438 - 207 x 0.084 = -12.0 V, the scalings its +15 V and
/// -15 V take (0x31e70, 0x31e8a; docs/firmware/z3817a-3752.md).
const PLUS_12V_B_COUNT: f64 = 157.0;
const MINUS_12V_B_COUNT: f64 = 123.0;

/// The temperature, 0.273 degrees a count: 92 is 25.1 degrees at
/// power-up, rising 37, 10 degrees, with a time constant of 20 minutes.
const TEMPERATURE_COLD: f64 = 92.0;
const TEMPERATURE_RISE: f64 = 37.0;
const TEMPERATURE_SECONDS: f64 = 1200.0;

/// The oven's warm-up: full power for four minutes, then falling to
/// its steady draw with a time constant of two and a half.
const FULL_POWER_SECONDS: f64 = 240.0;
const SETTLING_SECONDS: f64 = 150.0;
/// `ADC_CURRENT`, 250/255 a count: 235 is 230 at turn-on, 102 is 100
/// steady, below the 250 nominal (README.md).
const CURRENT_COLD: f64 = 235.0;
const CURRENT_WARM: f64 = 102.0;
/// `ADC_OVEN`, 0.0743 a count less 0.0472 the 5 V's: 199 is 5.0 V cold,
/// 80 is -3.8 V warm, past `oven_warmed_up`'s -2.0.
const OVEN_COLD: f64 = 199.0;
const OVEN_WARM: f64 = 80.0;

/// Half an ADC count, the least change of a level its code can show.
#[cfg(feature = "mc683xx")]
const HALF_COUNT: f64 = 0.5;

/// Seconds of board time at `now`.
fn seconds(now: Cycles) -> f64 {
    // seconds of board time fit a double's mantissa for ages
    now.0 as f64 / CPU_HZ as f64
}

/// How far the board's warm-up has come: the oven's, the oscillator's
/// that follows it (ocxo.rs) and the temperature inside's, one curve
/// from power-up.
#[derive(Debug, Clone, Copy, PartialEq)]
#[cfg_attr(feature = "mc683xx", derive(serde::Serialize, serde::Deserialize))]
pub(crate) enum Warmth {
    /// Warming, `head_start` seconds of the curve behind it at the
    /// board's power-up.
    Warming {
        /// Seconds.
        head_start: f64,
    },
    /// At the curve's end, as a receiver long powered: where the skip
    /// warm-up control puts it (docs/emulator.md, "The controls").
    Settled,
}

impl Default for Warmth {
    /// Cold at power-up.
    fn default() -> Self {
        Self::Warming { head_start: 0.0 }
    }
}

impl Warmth {
    /// Whether a run could come to it: a head start of seconds, finite
    /// and not negative, or the curve's end.
    pub(crate) fn possible(self) -> bool {
        match self {
            Self::Warming { head_start } => head_start.is_finite() && head_start >= 0.0,
            Self::Settled => true,
        }
    }

    /// Seconds along the curve at `now`, board time since power-up.
    pub(crate) fn age(self, now: Cycles) -> f64 {
        match self {
            Self::Warming { head_start } => head_start + seconds(now),
            // every term of the curve decays to its end, exactly
            Self::Settled => f64::INFINITY,
        }
    }

    /// The warm-up as it stands at `now`, as the head start of the next
    /// power-up's: its thermal state kept through a power cycle.
    #[cfg(feature = "mc683xx")]
    pub(crate) fn at(self, now: Cycles) -> Self {
        match self {
            Self::Warming { .. } => Self::Warming {
                head_start: self.age(now),
            },
            Self::Settled => Self::Settled,
        }
    }

    /// Whether any level of the curve is still half an ADC count or
    /// more from its end at `now`: past that, the ADC reads the end.
    #[cfg(feature = "mc683xx")]
    pub(crate) fn warming(self, now: Cycles) -> bool {
        let age = self.age(now);
        let heating = heating(age);
        [
            (OVEN_COLD - OVEN_WARM) * heating,
            (CURRENT_COLD - CURRENT_WARM) * heating,
            TEMPERATURE_RISE * (-age / TEMPERATURE_SECONDS).exp(),
        ]
        .into_iter()
        .any(|left| left >= HALF_COUNT)
    }

    /// How hard the oven heats at `now`: 1 at full power, falling to 0
    /// at its steady draw.  The oscillator's warm-up follows it
    /// (ocxo.rs).
    pub(crate) fn heating(self, now: Cycles) -> f64 {
        heating(self.age(now))
    }
}

/// How hard the oven heats `age` seconds along the curve.
fn heating(age: f64) -> f64 {
    (-(age - FULL_POWER_SECONDS).max(0.0) / SETTLING_SECONDS).exp()
}

/// The temperature inside the receiver: one analog output.
#[derive(Debug, Clone, Copy, Default)]
pub(crate) struct Temperature {
    /// Its warm-up.
    pub(crate) warmth: Warmth,
}

impl Temperature {
    /// Its level at `now`.
    pub(crate) fn level(self, now: Cycles) -> f64 {
        TEMPERATURE_COLD
            + TEMPERATURE_RISE * (1.0 - (-self.warmth.age(now) / TEMPERATURE_SECONDS).exp())
    }
}

/// The oven's monitor outputs.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) enum OvenOutput {
    /// The oscillator's current, falling as the oven warms.
    Current,
    /// The oven's own monitor, falling as it warms.
    Oven,
    /// The double oven's monitor, steady.
    DoubleOven,
}

/// The oven, warming from power-up: three analog outputs.
#[derive(Debug, Clone, Copy, Default)]
pub(crate) struct Oven {
    /// Its warm-up.
    pub(crate) warmth: Warmth,
}

impl Oven {
    /// Its `output`'s level at `now`.
    pub(crate) fn level(self, output: OvenOutput, now: Cycles) -> f64 {
        let heating = self.warmth.heating(now);
        match output {
            OvenOutput::Current => CURRENT_WARM + (CURRENT_COLD - CURRENT_WARM) * heating,
            OvenOutput::Oven => OVEN_WARM + (OVEN_COLD - OVEN_WARM) * heating,
            OvenOutput::DoubleOven => DOUBLE_OVEN_COUNT,
        }
    }
}

/// The supplies' monitor outputs.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) enum Supply {
    /// +15 V.
    Plus15V,
    /// +5 V.
    Plus5V,
    /// -15 V.
    Minus15V,
    /// The Z3817A's 12B.
    Plus12VB,
    /// The Z3817A's -12B.
    Minus12VB,
}

/// The supplies, steady: five analog outputs, the last two the
/// Z3817A's alone.
#[derive(Debug, Clone, Copy, Default)]
pub(crate) struct Supplies;

impl Supplies {
    /// `supply`'s level.
    pub(crate) fn level(self, supply: Supply) -> f64 {
        match supply {
            Supply::Plus15V => PLUS_15V_COUNT,
            Supply::Plus5V => PLUS_5V_COUNT,
            Supply::Minus15V => MINUS_15V_COUNT,
            Supply::Plus12VB => PLUS_12V_B_COUNT,
            Supply::Minus12VB => MINUS_12V_B_COUNT,
        }
    }
}

/// The sources as the board wires them to the ADC's channels; channel
/// 7, which no routine reads, to nothing.
#[derive(Debug, Clone, Copy, Default)]
pub(crate) struct Wiring {
    temperature: Temperature,
    oven: Oven,
    supplies: Supplies,
}

impl Inputs for Wiring {
    fn level(&mut self, channel: u16, now: Cycles) -> Option<f64> {
        Some(match channel {
            TEMPERATURE => self.temperature.level(now),
            DOUBLE_OVEN => self.oven.level(OvenOutput::DoubleOven, now),
            PLUS_15V => self.supplies.level(Supply::Plus15V),
            CURRENT => self.oven.level(OvenOutput::Current, now),
            PLUS_5V => self.supplies.level(Supply::Plus5V),
            OVEN => self.oven.level(OvenOutput::Oven, now),
            MINUS_15V => self.supplies.level(Supply::Minus15V),
            _ => return None,
        })
    }
}

#[cfg(feature = "mc683xx")]
mod timed {
    use mc683xx_modules::peripheral::Timed;

    use super::Oven;
    use super::Supplies;
    use super::Temperature;

    /// The sources change continuously, never by an event: a sink
    /// samples them when it converts.
    impl Timed for Temperature {}
    impl Timed for Oven {}
    impl Timed for Supplies {}
}

#[cfg(test)]
mod tests {
    use super::super::adc::Inputs;
    use super::super::time::CPU_HZ;
    use super::super::time::Cycles;
    use super::Oven;
    use super::OvenOutput;
    use super::Temperature;
    use super::Warmth;
    use super::Wiring;

    #[test]
    fn channel_7_is_wired_to_nothing() {
        assert_eq!(Wiring::default().level(7, Cycles(0)), None);
        assert_eq!(Wiring::default().level(4, Cycles(0)), Some(207.0));
    }

    #[test]
    fn a_settled_warm_up_is_at_its_end_and_stays_there() {
        let settled = Warmth::Settled;
        let oven = Oven { warmth: settled };
        assert_eq!(oven.level(OvenOutput::Oven, Cycles(0)), 80.0);
        assert_eq!(oven.level(OvenOutput::Current, Cycles(0)), 102.0);
        let temperature = Temperature { warmth: settled };
        assert_eq!(temperature.level(Cycles(0)), 92.0 + 37.0);
    }

    #[cfg(feature = "mc683xx")]
    #[test]
    fn a_warm_up_carried_through_a_power_cycle_goes_on_where_it_was() {
        let cold = Warmth::default();
        let carried = cold.at(Cycles(600 * CPU_HZ));
        assert_eq!(carried, Warmth::Warming { head_start: 600.0 });
        let oven = |warmth| Oven { warmth }.level(OvenOutput::Oven, Cycles(0));
        assert_eq!(
            oven(carried),
            Oven { warmth: cold }.level(OvenOutput::Oven, Cycles(600 * CPU_HZ))
        );
        assert!(cold.warming(Cycles(0)));
        // the temperature's time constant is the longest: half a count
        // from its end at 1200 s x ln(74), about 86 minutes
        assert!(cold.warming(Cycles(5000 * CPU_HZ)));
        assert!(!cold.warming(Cycles(5200 * CPU_HZ)));
        assert_eq!(Warmth::Settled.at(Cycles(CPU_HZ)), Warmth::Settled);
        assert!(!Warmth::Settled.warming(Cycles(0)));
    }

    #[test]
    fn the_oven_falls_as_it_warms() {
        let mut wiring = Wiring::default();
        let oven = |wiring: &mut Wiring, seconds| wiring.level(5, Cycles(seconds * CPU_HZ));
        assert_eq!(oven(&mut wiring, 0), Some(199.0));
        assert!(oven(&mut wiring, 900) < Some(100.0));
    }
}
