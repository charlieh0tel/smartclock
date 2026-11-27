//! The analog sources off the bus: the temperature inside and the
//! supplies' monitors.

use mc683xx_board::Board;
use mc683xx_board::peripheral::Peripheral;
use mc683xx_board::registry::PortSpec;
use mc683xx_board::signal::Kind;
use mc683xx_board::time::Moment;
use mc683xx_cpu32::state::StateError;
use mc683xx_mc68331::Mc68331;

use super::Span;
use super::adc_volts;
use crate::board::analog::Supplies;
use crate::board::analog::Supply;
use crate::board::analog::Temperature;
use crate::board::analog::Warmth;
use crate::board::snapshot;
use crate::board::system_clock::board_cycles;

/// `board`'s temperature inside, the part `name`, settled at the end of
/// its warm-up: whether the board has the part.
pub(crate) fn settle_temperature(board: &mut Board<Mc68331>, name: &str) -> bool {
    board
        .peripheral_mut::<TemperaturePart>(name)
        .ok()
        .map(|mut part| part.temperature.warmth = Warmth::Settled)
        .is_some()
}

/// `temperature-sensor`: the temperature inside, `out`, an analog output
/// in volts, over the ADC's span, `adc_vref_volts`; its warm-up's place
/// at power-up from `warm_up_seconds` and `warm_up_settled` (`warmth`).
#[derive(Debug)]
pub(super) struct TemperaturePart {
    /// The model.
    pub(super) temperature: Temperature,
    /// The ADC's span.
    pub(super) adc: Span,
}

impl TemperaturePart {
    /// Its ports.
    pub(super) fn ports() -> Vec<PortSpec> {
        vec![PortSpec::output("out", Kind::Analog)]
    }
}

impl Peripheral for TemperaturePart {
    /// Its warm-up's place, which the skip warm-up control moves.
    fn save(&self) -> Result<Vec<u8>, StateError> {
        Ok(snapshot::save(&self.temperature.warmth))
    }

    fn restore(&mut self, snapshot: &[u8]) -> Result<(), StateError> {
        let warmth: Warmth = snapshot::load(snapshot)?;
        if !warmth.possible() {
            return Err(StateError::invalid(format!("a warm-up of {warmth:?}")));
        }
        self.temperature.warmth = warmth;
        Ok(())
    }

    fn analog_output(&mut self, _: usize, moment: Moment) -> Option<f64> {
        Some(adc_volts(
            self.adc,
            self.temperature.level(board_cycles(moment.now())),
        ))
    }
}

/// `supply-monitor`: the supplies' monitors, analog outputs in volts
/// over the ADC's span, `adc_vref_volts`: `plus_15v`, `plus_5v` and
/// `minus_15v`, and the Z3817A's `plus_12v_b` and `minus_12v_b`.
#[derive(Debug)]
pub(super) struct SuppliesPart {
    /// The model.
    pub(super) supplies: Supplies,
    /// The ADC's span.
    pub(super) adc: Span,
}

impl SuppliesPart {
    /// Its outputs, in port order.
    const OUTPUTS: [(&str, Supply); 5] = [
        ("plus_15v", Supply::Plus15V),
        ("plus_5v", Supply::Plus5V),
        ("minus_15v", Supply::Minus15V),
        ("plus_12v_b", Supply::Plus12VB),
        ("minus_12v_b", Supply::Minus12VB),
    ];

    /// Its ports.
    pub(super) fn ports() -> Vec<PortSpec> {
        Self::OUTPUTS
            .iter()
            .map(|(name, _)| PortSpec::output(*name, Kind::Analog))
            .collect()
    }
}

impl Peripheral for SuppliesPart {
    fn save(&self) -> Result<Vec<u8>, StateError> {
        Ok(snapshot::save(&()))
    }

    fn restore(&mut self, snapshot: &[u8]) -> Result<(), StateError> {
        snapshot::load::<()>(snapshot)
    }

    fn analog_output(&mut self, port: usize, _: Moment) -> Option<f64> {
        let (_, supply) = Self::OUTPUTS.get(port)?;
        Some(adc_volts(self.adc, self.supplies.level(*supply)))
    }
}
