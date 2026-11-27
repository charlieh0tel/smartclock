//! The oscillator, its EFC and its ovens' monitors on the board's nets.

use mc683xx_board::Board;
use mc683xx_board::peripheral::Drive;
use mc683xx_board::peripheral::Peripheral;
use mc683xx_board::registry::Context;
use mc683xx_board::registry::ParameterError;
use mc683xx_board::registry::PortSpec;
use mc683xx_board::signal::Kind;
use mc683xx_board::signal::Signal;
use mc683xx_board::time::Clock as NetClock;
use mc683xx_board::time::Moment;
use mc683xx_board::time::Span as BoardSpan;
use mc683xx_board::time::Time;
use mc683xx_cpu32::Frequency;
use mc683xx_cpu32::NANOHERTZ_PER_HERTZ;
use mc683xx_mc68331::Mc68331;

use super::EFC_CODES;
use super::EFC_VREF_NEGATIVE;
use super::EFC_VREF_POSITIVE;
use super::Span;
use super::adc_span;
use super::adc_volts;
use super::crystal;
use super::invalid;
use super::span;
use super::warmth;
use crate::board::analog::Oven;
use crate::board::analog::OvenOutput;
use crate::board::analog::Warmth;
use crate::board::clock::IDEAL_HZ;
use crate::board::ocxo::Config as OcxoConfig;
use crate::board::ocxo::Crystal;
use crate::board::ocxo::Ocxo;
use crate::board::ocxo::Terms as OcxoTerms;
use crate::board::parameters::ocxo as keys;
use crate::board::system_clock::board_cycles;
use crate::board::system_clock::board_time;
use crate::board::time::Cycles;

/// The EFC DAC's word whose output is `volts` on `efc`, the nearest,
/// within its 16 bits: the AD569's VOUT taken back to its code, so that
/// the oscillator sees the word the firmware wrote, as on the Unicorn
/// board (ocxo.rs).
pub(super) fn efc_word(efc: Span, volts: f64) -> u16 {
    let code = efc.code(volts, EFC_CODES);
    // a whole number within the DAC's 16 bits
    code.round().clamp(0.0, f64::from(u16::MAX)) as u16
}

/// `ocxo`: the oscillator, its EFC, `efc`, an analog input in volts,
/// the AD569's output, which it takes back to the DAC's word
/// (`efc_word`); its 10 MHz, `out`, a clock output; the outer oven's
/// control, `outer_oven`, a logic input the model does not act on; and
/// its ovens' monitors, analog outputs in volts, sampled as they warm:
/// `oven_current`, `oven_monitor` and `outer_oven_monitor` (ovens.md).  Its parameters are `Config`'s, fractional frequencies:
/// `offset`, `aging` (a day), `warm_up`, `efc_gain` (the firmware's G),
/// `white_fm`, `random_walk_fm`, each 0 unless given but the gain;
/// `warm_up_seconds` and `warm_up_settled`, its warm-up's place at
/// power-up (`warmth`); `aging_seconds`, `noise_draws` and
/// `random_walk`, its crystal's (`crystal`); `hz`,
/// which must be the model's 10 MHz; `efc_vref_negative` and
/// `efc_vref_positive`, the EFC DAC's span, and `adc_vref_volts`, the
/// ADC's, volts; and `part`.  Its noise is from the
/// part's own seed.
///
/// `out` is mc683xx-board's clock net: the frequency in nanohertz from
/// a time on, its phase carried across each change.  The model's
/// frequency is constant between its changes (ocxo.rs), and the part
/// drives each: the EFC's strobe, a draw of the noise, and, while aging
/// or warm-up move it, a retune every `RETUNE` of board time, the
/// event-level approximation of a frequency that drifts between.
#[derive(Debug)]
pub(super) struct OcxoPart {
    /// The model.
    pub(super) ocxo: Ocxo,
    /// The ovens' monitors.
    oven: Oven,
    /// The clock last driven on `out`, none before power-on.
    driven: Option<NetClock>,
    /// When it next retunes for its drift, while it drifts.
    retune: Option<Time>,
    /// The EFC DAC's span, which its input is taken back over.
    efc: Span,
    /// The ADC's span, which its monitors are given over.
    adc: Span,
}

/// A drifting oscillator's retune interval: a second of board time, the
/// noise's interval too (ocxo.rs).
const RETUNE: BoardSpan = BoardSpan::from_secs(1);

/// Its frequency's parameter, which must be the model's.
const HZ: &str = "hz";

impl OcxoPart {
    /// Its EFC input.
    const EFC: usize = 0;
    /// Its clock output.
    const OUT: usize = 1;
    /// Its first monitor; the rest follow in `MONITORS`' order.
    const FIRST_MONITOR: usize = 3;
    /// Its ovens' monitors, in port order.
    const MONITORS: [(&str, OvenOutput); 3] = [
        ("oven_current", OvenOutput::Current),
        ("oven_monitor", OvenOutput::Oven),
        ("outer_oven_monitor", OvenOutput::DoubleOven),
    ];

    /// Its ports.
    pub(super) fn ports() -> Vec<PortSpec> {
        let monitors = Self::MONITORS
            .iter()
            .map(|(name, _)| PortSpec::output(*name, Kind::Analog));
        [
            PortSpec::input("efc", Kind::Analog),
            PortSpec::output("out", Kind::Clock),
            PortSpec::input("outer_oven", Kind::Logic),
        ]
        .into_iter()
        .chain(monitors)
        .collect()
    }

    /// From its parameters (`OcxoPart`).
    pub(super) fn from_parameters(context: &Context<'_>) -> Result<Self, ParameterError> {
        if let Some(hz) = context.parameters.integer::<u64>(HZ)?
            && hz != IDEAL_HZ
        {
            return Err(invalid(
                HZ,
                format!("the model's oscillator is {IDEAL_HZ} Hz"),
            ));
        }
        let term = |key| context.parameters.float(key);
        let config = OcxoTerms {
            offset: term(keys::OFFSET)?,
            aging: term(keys::AGING)?,
            warm_up: term(keys::WARM_UP)?,
            efc_gain: term(keys::EFC_GAIN)?,
            white_fm: term(keys::WHITE_FM)?,
            random_walk_fm: term(keys::RANDOM_WALK_FM)?,
        }
        .over(OcxoConfig::default());
        let warmth = warmth(context)?;
        let mut ocxo = Ocxo::resumed(
            config,
            context.environment.seed_for(context.name),
            crystal(context)?,
        )
        .map_err(|error| invalid(error.term(), error.to_string()))?;
        ocxo.set_warmth(warmth, Cycles(0));
        let retune = ocxo.drifts().then_some(Time::ZERO + RETUNE);
        Ok(Self {
            ocxo,
            oven: Oven { warmth },
            driven: None,
            retune,
            efc: span(context, EFC_VREF_NEGATIVE, EFC_VREF_POSITIVE)?,
            adc: adc_span(context)?,
        })
    }

    /// Its frequency at `now` onto `out`, if it changed.
    fn drive_clock(&mut self, now: Time, drive: &mut Drive<'_>) {
        let offset = self.ocxo.frequency(board_cycles(now));
        // nanohertz of 10 MHz, within u64 for any offset an oven holds
        let nanohertz = (IDEAL_HZ as f64 * NANOHERTZ_PER_HERTZ as f64 * (1.0 + offset)).round();
        let frequency = Frequency::from_nanohertz(nanohertz.max(0.0) as u64);
        let clock = match self.driven {
            Some(driven) if driven.frequency == frequency => return,
            Some(driven) => driven.retuned(frequency, now),
            None => NetClock::new(frequency, now),
        };
        self.driven = Some(clock);
        drive.drive(Self::OUT, Signal::Clock(clock));
    }
}

impl Peripheral for OcxoPart {
    fn power_on(&mut self, drive: &mut Drive<'_>) {
        self.drive_clock(drive.now(), drive);
    }

    fn input(&mut self, port: usize, signal: Signal, drive: &mut Drive<'_>) {
        let now = drive.now();
        if let (Self::EFC, Signal::Analog(volts)) = (port, signal) {
            self.ocxo
                .set_efc(efc_word(self.efc, volts), board_cycles(now));
            self.drive_clock(now, drive);
        }
    }

    fn analog_output(&mut self, port: usize, moment: Moment) -> Option<f64> {
        let (_, output) = Self::MONITORS.get(port.checked_sub(Self::FIRST_MONITOR)?)?;
        Some(adc_volts(
            self.adc,
            self.oven.level(*output, board_cycles(moment.now())),
        ))
    }

    fn next_event(&self, _: Moment) -> Option<Time> {
        let noise = self.ocxo.next_event().map(board_time);
        noise.into_iter().chain(self.retune).min()
    }

    fn advance(&mut self, drive: &mut Drive<'_>) {
        let now = drive.now();
        self.ocxo.advance(board_cycles(now));
        if let Some(retune) = &mut self.retune
            && *retune <= now
        {
            *retune = now + RETUNE;
        }
        self.drive_clock(now, drive);
    }
}

/// `board`'s oscillator, the part `name`, and its oven settled at the
/// end of their warm-up now: whether the board has the part.
pub(crate) fn settle(board: &mut Board<Mc68331>, name: &str) -> bool {
    let now = board_cycles(board.now());
    board
        .peripheral_mut::<OcxoPart>(name)
        .ok()
        .map(|mut part| {
            part.oven.warmth = Warmth::Settled;
            part.ocxo.set_warmth(Warmth::Settled, now);
        })
        .is_some()
}

/// `board`'s oscillator's crystal, the part `name`, as it stands now,
/// if the board has the part.
pub(crate) fn crystal_now(board: &Board<Mc68331>, name: &str) -> Option<Crystal> {
    let now = board_cycles(board.now());
    board
        .peripheral::<OcxoPart>(name)
        .ok()
        .map(|part| part.ocxo.crystal(now))
}

/// `board`'s oscillator's fractional frequency offset now, the part
/// `name`, if the board has the part.
#[cfg(test)]
pub(crate) fn frequency_now(board: &Board<Mc68331>, name: &str) -> Option<f64> {
    let now = board_cycles(board.now());
    board
        .peripheral::<OcxoPart>(name)
        .ok()
        .map(|part| part.ocxo.frequency(now))
}

/// `board`'s oscillator's oven's `output` level now, ADC counts, the
/// part `name`, if the board has the part.
#[cfg(test)]
pub(crate) fn oven_level_now(
    board: &Board<Mc68331>,
    name: &str,
    output: OvenOutput,
) -> Option<f64> {
    let now = board_cycles(board.now());
    board
        .peripheral::<OcxoPart>(name)
        .ok()
        .map(|part| part.oven.level(output, now))
}
