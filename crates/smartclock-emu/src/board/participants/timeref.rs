//! The Z3817A's two time references off the board, TIMEREF1 and
//! TIMEREF2, each a pulse and a valid line, and the selector port E's
//! bit 7 drives between them (docs/firmware/z3817a-3752.md, "The time
//! references").

use mc683xx_board::peripheral::Drive;
use mc683xx_board::peripheral::Peripheral;
use mc683xx_board::registry::Context;
use mc683xx_board::registry::ParameterError;
use mc683xx_board::registry::PortSpec;
use mc683xx_board::signal::Kind;
use mc683xx_board::signal::Signal;
use mc683xx_board::time::Moment;
use mc683xx_board::time::Span;
use mc683xx_board::time::Time;

use super::gps::PPS_HIGH;
use super::invalid;

/// A reference's pulse, by the name a board file gives it.
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
enum Pulse {
    /// No pulse.
    #[default]
    None,
    /// A pulse each second.
    OnePps,
    /// A pulse each even second, a CDMA site's PP2S.
    Pp2s,
}

impl Pulse {
    /// Each pulse by its name.
    const NAMES: [(&str, Self); 3] = [
        ("none", Self::None),
        ("1pps", Self::OnePps),
        ("pp2s", Self::Pp2s),
    ];

    /// The pulse a board file calls `name`, if any.
    fn named(name: &str) -> Option<Self> {
        Self::NAMES
            .iter()
            .find(|(known, _)| *known == name)
            .map(|&(_, pulse)| pulse)
    }

    /// Whether second `n` from power-up brings a pulse.
    fn due(self, n: u64) -> bool {
        match self {
            Self::None => false,
            Self::OnePps => true,
            Self::Pp2s => n.is_multiple_of(2),
        }
    }
}

/// A change of a reference's pulse during the run.
#[derive(Debug, Clone, Copy)]
struct PulseChange {
    /// The second from power-up from which the new pulse runs.
    second: u64,
    /// The new pulse.
    pulse: Pulse,
}

/// One reference as it reaches the selector.
#[derive(Debug, Clone, Copy, Default)]
struct Reference {
    /// Its pulse from power-up.
    pulse: Pulse,
    /// The change of its pulse, if any.
    change: Option<PulseChange>,
    /// Its valid line, high asserted.
    valid: bool,
}

impl Reference {
    /// Whether second `n` from power-up brings its pulse.
    fn due(&self, n: u64) -> bool {
        match self.change {
            Some(change) if n >= change.second => change.pulse.due(n),
            _ => self.pulse.due(n),
        }
    }
}

/// `timeref-selector`: TIMEREF1 and TIMEREF2 and the selector between
/// them, the selector's input `select` a logic input, the selected
/// reference's pulse `pulse` an edge output and its valid line `valid`
/// a logic output.  Its parameters, each reference's: `timeref1` and
/// `timeref2`, the pulse, `none`, `1pps` or `pp2s` (`none`), and
/// `timeref1_valid` and `timeref2_valid` (false); `timeref1_change` and
/// `timeref2_change`, a pulse the reference changes to during the run
/// (none), from the second from power-up `timeref1_change_second` or
/// `timeref2_change_second` gives; and `select_high`, the reference the
/// selector's input high picks, 1 or 2 (1).  A pulse
/// rises at a whole second of board time from power-up, as the
/// synthetic engine's seconds begin, every second for `1pps` and every
/// even one for `pp2s`, and is high for the VP Oncore's 200 ms.  The
/// selector's input is high until driven.
#[derive(Debug)]
pub(super) struct TimeReferencesPart {
    /// TIMEREF1 and TIMEREF2.
    references: [Reference; 2],
    /// The index in `references` the selector's input high picks.
    high_selects: usize,
    /// The selector's input.
    select: bool,
    /// Power-up, from which the seconds count.
    origin: Time,
    /// The next second whose pulse is to be driven, counted from
    /// power-up.
    next_second: u64,
}

impl TimeReferencesPart {
    /// Its ports.
    const SELECT: usize = 0;
    const PULSE: usize = 1;
    const VALID: usize = 2;

    /// Its parameters' keys, a reference's by its number.
    const PULSE_KEYS: [&str; 2] = ["timeref1", "timeref2"];
    const VALID_KEYS: [&str; 2] = ["timeref1_valid", "timeref2_valid"];
    const CHANGE_KEYS: [&str; 2] = ["timeref1_change", "timeref2_change"];
    const CHANGE_SECOND_KEYS: [&str; 2] = ["timeref1_change_second", "timeref2_change_second"];
    const SELECT_HIGH: &str = "select_high";

    /// Its ports.
    pub(super) fn ports() -> Vec<PortSpec> {
        vec![
            PortSpec::input("select", Kind::Logic),
            PortSpec::output("pulse", Kind::Edge),
            PortSpec::output("valid", Kind::Logic),
        ]
    }

    /// From its parameters (`TimeReferencesPart`).
    pub(super) fn from_parameters(context: &Context<'_>) -> Result<Self, ParameterError> {
        let parameters = context.parameters;
        let pulse = |key: &str| {
            parameters
                .string(key)?
                .map(|name| {
                    Pulse::named(name).ok_or_else(|| invalid(key, "not none, 1pps or pp2s"))
                })
                .transpose()
        };
        let reference = |index: usize| {
            let change = match (
                pulse(Self::CHANGE_KEYS[index])?,
                parameters.integer::<u64>(Self::CHANGE_SECOND_KEYS[index])?,
            ) {
                (None, None) => None,
                (Some(pulse), Some(second)) => Some(PulseChange { second, pulse }),
                _ => {
                    return Err(invalid(
                        Self::CHANGE_KEYS[index],
                        "a change wants its pulse and its second",
                    ));
                }
            };
            Ok::<_, ParameterError>(Reference {
                pulse: pulse(Self::PULSE_KEYS[index])?.unwrap_or_default(),
                change,
                valid: parameters
                    .boolean(Self::VALID_KEYS[index])?
                    .unwrap_or(false),
            })
        };
        let high_selects = match parameters.integer::<u8>(Self::SELECT_HIGH)? {
            None | Some(1) => 0,
            Some(2) => 1,
            Some(_) => return Err(invalid(Self::SELECT_HIGH, "not 1 or 2")),
        };
        Ok(Self {
            references: [reference(0)?, reference(1)?],
            high_selects,
            select: true,
            origin: Time::ZERO,
            next_second: 1,
        })
    }

    /// The reference the selector passes.
    fn selected(&self) -> Reference {
        let index = if self.select {
            self.high_selects
        } else {
            1 - self.high_selects
        };
        self.references[index]
    }

    /// When second `n` from power-up begins.
    fn second_at(&self, n: u64) -> Time {
        self.origin + Span::from_secs(n)
    }
}

impl Peripheral for TimeReferencesPart {
    fn power_on(&mut self, drive: &mut Drive<'_>) {
        self.origin = drive.now();
        drive.drive(Self::VALID, Signal::Level(self.selected().valid));
    }

    fn input(&mut self, port: usize, signal: Signal, drive: &mut Drive<'_>) {
        if let (Self::SELECT, Signal::Level(high)) = (port, signal)
            && high != self.select
        {
            self.select = high;
            drive.drive(Self::VALID, Signal::Level(self.selected().valid));
        }
    }

    fn next_event(&self, _: Moment) -> Option<Time> {
        Some(self.second_at(self.next_second))
    }

    fn advance(&mut self, drive: &mut Drive<'_>) {
        let now = drive.now();
        while self.second_at(self.next_second) <= now {
            let n = self.next_second;
            if self.selected().due(n) {
                let at = self.second_at(n);
                drive.drive_at(at, Self::PULSE, Signal::Level(true));
                drive.drive_at(at + PPS_HIGH, Self::PULSE, Signal::Level(false));
            }
            self.next_second += 1;
        }
    }
}
