//! The QSPI's front panel and the models off the bus as mc683xx-board
//! parts, registered from their parameter tables with the ports they
//! declare (docs/emulator.md, "The mc683xx device layer"): the 58503A's
//! front panel on the QSPI; the synthetic GPS engine, the OCXO and the
//! analog sources on nets alone.  The ADC and the DACs are
//! mc683xx-peripherals' stock chips (registry.rs), and a steady level
//! is mc683xx-board's built-in `dc-source`.  Each part wraps its
//! model unchanged, through the model's own small interface to its nets
//! (`gps::Port`, `Ocxo::set_efc`, `FrontPanel::control` and `press`), so
//! the Unicorn board and these parts run the same logic: the front
//! panel (`front_panel`), the engine (`oncore`), the oscillator
//! //! The Z3817A's time
//! references and their selector (`timeref`), its EFC's shift register
//! (`efc_shift`) and its oscillator's report of its EFC, and the Z3815A's
//! puck's, the same report clocked (`efc_monitor`),
//! have no Unicorn model.
//!
//! The front panel, which the chip's QSPI clocks, keeps its time in the
//! chip's system clocks, converted from board time through each call's
//! system clock (`system_clock`); the models off the bus, the GPS
//! engine, the OCXO and its ovens and the temperature, in the board's
//! own cycles (`board_cycles`), the same rate from power-on whatever
//! the chip's clock does.  Analog values are volts, as the stock chips
//! take and give them: a source's level, in the ADC's counts on
//! the Unicorn board (analog.rs), is the voltage the stock ADC0838
//! converts to that count against its reference (`adc_volts`), which
//! the board file gives the sources as the ADC's; the EFC, the AD569's
//! output, is taken back to the DAC's word over the DAC's span, which
//! the board file gives the oscillator as the DAC's (`efc_word`), as
//! the Unicorn board's oscillator takes it.

use mc683xx_board::peripheral::Peripheral;
use mc683xx_board::registry::Context;
use mc683xx_board::registry::ParameterError;
use mc683xx_board::registry::PortSpec;
use mc683xx_board::registry::Registry;
use mc683xx_board::registry::RegistryError;

use super::analog::Supplies;
use super::analog::Temperature;
use super::analog::Warmth;
use super::ocxo::Crystal;
use super::parameters::ocxo as keys;
use super::parameters::warm_up;
use crate::gps::B1121;
use crate::gps::GT_74;
use efc_monitor::ClockedEfcMonitorPart;
use efc_monitor::EfcMonitorPart;
use efc_shift::EfcShiftPart;
use front_panel::FrontPanelPart;
use gps::GpsPart;
use ocxo::OcxoPart;
use sources::SuppliesPart;
use sources::TemperaturePart;
use timeref::TimeReferencesPart;

/// The types' names in a board file.
pub(crate) const FRONT_PANEL: &str = "hp58503a-front-panel";
pub(crate) const GPS: &str = "oncore";
pub(crate) const FURUNO: &str = "furuno-gt74";
/// The types that are the synthetic GPS engine, an Oncore and a Furuno.
pub(crate) const GPS_TYPES: [&str; 2] = [GPS, FURUNO];
pub(crate) const OCXO: &str = "ocxo";
pub(crate) const TEMPERATURE: &str = "temperature-sensor";
pub(crate) const SUPPLIES: &str = "supply-monitor";
pub(crate) const TIME_REFERENCES: &str = "timeref-selector";
pub(crate) const EFC_MONITOR: &str = "efc-monitor";
pub(crate) const CLOCKED_EFC_MONITOR: &str = "clocked-efc-monitor";
pub(crate) const EFC_SHIFT: &str = "efc-shift-register";

/// The ADC0838's reference, volts, as the parts that feed it take it:
/// their `adc_vref_volts`, which the board files give as the ADC's own
/// `vref_volts`, the stock part's default unless given; and its codes
/// over it: 1 LSB is VREF / 256 (SNAS531B, Reference Considerations).
/// A source's count is the voltage the ADC converts to it, the code the
/// nearest whole number of LSBs (SNAS531B; mc683xx's
/// docs/peripherals.md, ADC0838).
const ADC_VREF: (&str, f64) = ("adc_vref_volts", 5.0);
const ADC_CODES: f64 = 256.0;
/// The ADC's span unless the parameters give another.
const ADC_SPAN: Span = Span {
    low: 0.0,
    high: ADC_VREF.1,
};

/// The EFC DAC's span, volts, as the oscillator takes it: its
/// `efc_vref_negative` and `efc_vref_positive`, which the board files
/// give as the AD569's own, -VREF at code 0 and +VREF a code past
/// 0xffff would give (AD569 datasheet, Analog Circuit Connections), the
/// stock part's defaults unless given: the 10811's EFC spans -5 V to
/// +5 V (efc.md).
const EFC_VREF_NEGATIVE: (&str, f64) = ("efc_vref_negative", -5.0);
const EFC_VREF_POSITIVE: (&str, f64) = ("efc_vref_positive", 5.0);
/// The AD569's codes over its span.
const EFC_CODES: f64 = 65536.0;

/// A converter's reference span, volts: code 0 at `low`, the full
/// scale, a code past the last, at `high`.
#[derive(Debug, Clone, Copy, PartialEq)]
pub(super) struct Span {
    /// Code 0's voltage.
    low: f64,
    /// The full scale's.
    high: f64,
}

impl Span {
    /// `volts` as a code on the span's `codes` codes, unrounded.
    pub(super) fn code(self, volts: f64, codes: f64) -> f64 {
        (volts - self.low) / (self.high - self.low) * codes
    }

    /// Code `code` of the span's `codes` codes as volts.
    fn volts(self, code: f64, codes: f64) -> f64 {
        self.low + code * (self.high - self.low) / codes
    }
}

/// The span `context`'s parameters give from `low` and `high`, each a
/// key and its default, the stock part's.
pub(super) fn span(
    context: &Context<'_>,
    (low_key, low): (&str, f64),
    (high_key, high): (&str, f64),
) -> Result<Span, ParameterError> {
    let low = context.parameters.float(low_key)?.unwrap_or(low);
    let high = context.parameters.float(high_key)?.unwrap_or(high);
    checked(low, high, high_key)
}

/// The ADC's span `context`'s parameters give: from 0 V to their
/// `adc_vref_volts`.
fn adc_span(context: &Context<'_>) -> Result<Span, ParameterError> {
    let (key, vref) = ADC_VREF;
    let vref = context.parameters.float(key)?.unwrap_or(vref);
    checked(ADC_SPAN.low, vref, key)
}

/// The span from `low` to `high`, which must be above it, `key` the
/// parameter that says so.
fn checked(low: f64, high: f64, key: &str) -> Result<Span, ParameterError> {
    if low.is_finite() && high.is_finite() && high > low {
        Ok(Span { low, high })
    } else {
        Err(invalid(key, format!("not a voltage above {low}")))
    }
}

/// A type's constructor.
type Builds = fn(&Context<'_>) -> Result<Box<dyn Peripheral>, ParameterError>;

/// Registers this module's types in `registry`.
pub(crate) fn register(registry: &mut Registry) -> Result<(), RegistryError> {
    let types: [(&str, Vec<PortSpec>, Builds); 10] = [
        (FRONT_PANEL, FrontPanelPart::ports(), |context| {
            Ok(Box::new(FrontPanelPart::from_parameters(context)?))
        }),
        (GPS, GpsPart::ports(), |context| {
            Ok(Box::new(GpsPart::from_parameters(context, &B1121)?))
        }),
        (FURUNO, GpsPart::ports(), |context| {
            Ok(Box::new(GpsPart::from_parameters(context, &GT_74)?))
        }),
        (OCXO, OcxoPart::ports(), |context| {
            Ok(Box::new(OcxoPart::from_parameters(context)?))
        }),
        (TEMPERATURE, TemperaturePart::ports(), |context| {
            Ok(Box::new(TemperaturePart {
                temperature: Temperature {
                    warmth: warmth(context)?,
                },
                adc: adc_span(context)?,
            }))
        }),
        (SUPPLIES, SuppliesPart::ports(), |context| {
            Ok(Box::new(SuppliesPart {
                supplies: Supplies,
                adc: adc_span(context)?,
            }))
        }),
        (TIME_REFERENCES, TimeReferencesPart::ports(), |context| {
            Ok(Box::new(TimeReferencesPart::from_parameters(context)?))
        }),
        (EFC_MONITOR, EfcMonitorPart::ports(), |context| {
            Ok(Box::new(EfcMonitorPart::from_parameters(context)?))
        }),
        (
            CLOCKED_EFC_MONITOR,
            ClockedEfcMonitorPart::ports(),
            |context| Ok(Box::new(ClockedEfcMonitorPart::from_parameters(context)?)),
        ),
        (EFC_SHIFT, EfcShiftPart::ports(), |context| {
            Ok(Box::new(EfcShiftPart::from_parameters(context)?))
        }),
    ];
    for (name, ports, constructor) in types {
        registry.register(name, ports, constructor)?;
    }
    Ok(())
}

/// The warm-up at power-up `context`'s parameters give: settled with
/// `warm_up_settled` (false), else `warm_up_seconds` (0) of it behind
/// it, as a power cycle carries it (docs/emulator.md, "The controls").
fn warmth(context: &Context<'_>) -> Result<Warmth, ParameterError> {
    if context
        .parameters
        .boolean(warm_up::SETTLED)?
        .unwrap_or(false)
    {
        return Ok(Warmth::Settled);
    }
    let warmth = Warmth::Warming {
        head_start: context.parameters.float(warm_up::SECONDS)?.unwrap_or(0.0),
    };
    if warmth.possible() {
        Ok(warmth)
    } else {
        Err(invalid(warm_up::SECONDS, "not a number of seconds"))
    }
}

/// The crystal at power-up `context`'s parameters give: `aging_seconds`
/// (0) of aging behind it, `noise_draws` (0) of its noise's draws taken
/// and `random_walk` (0) its walk's sum, as a power cycle carries them
/// (docs/emulator.md, "The controls").
fn crystal(context: &Context<'_>) -> Result<Crystal, ParameterError> {
    let aged = context
        .parameters
        .float(keys::AGING_SECONDS)?
        .unwrap_or(0.0);
    if !(aged.is_finite() && aged >= 0.0) {
        return Err(invalid(keys::AGING_SECONDS, "not a number of seconds"));
    }
    let walk = context.parameters.float(keys::RANDOM_WALK)?.unwrap_or(0.0);
    if !walk.is_finite() {
        return Err(invalid(keys::RANDOM_WALK, "not a number"));
    }
    Ok(Crystal {
        aged,
        draws: context.parameters.integer(keys::NOISE_DRAWS)?.unwrap_or(0),
        walk,
    })
}

/// A source's level, `count` in the ADC's counts (analog.rs), as the
/// voltage the ADC converts to it on `adc`.
fn adc_volts(adc: Span, count: f64) -> f64 {
    adc.volts(count, ADC_CODES)
}

/// A parameter `key` the part cannot take, and why.
fn invalid(key: &str, reason: impl Into<String>) -> ParameterError {
    ParameterError::Invalid {
        key: key.to_owned(),
        reason: reason.into(),
    }
}

pub(crate) mod efc_monitor;
mod efc_shift;
pub(crate) mod front_panel;
pub(crate) mod gps;
pub(crate) mod ocxo;
pub(crate) mod sources;
#[cfg(test)]
mod tests;
mod timeref;
