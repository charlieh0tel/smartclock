//! The board's indicators, the lamps and LEDs: each is one bit of a
//! register the firmware writes whole, the front-panel latch at
//! `0x300000` or the DUART's output port.  With `--indicators`, what
//! a bank lights at power-up and each change after are reported as
//! lines on standard error, in the trace's form (`trace`):
//!
//!     smartclock-emu: indicator cycle=16777216 bank=lamp name=gps_lock lit=1

use std::fmt;

use crate::board::time::Cycles;
use crate::trace;

/// Which level of its bit lights an indicator.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) enum Polarity {
    /// A one lights it.
    High,
    /// A zero lights it, as on an open-collector output driving the
    /// lamp's cathode, or the DUART's output port, whose register holds
    /// its pins' complement.
    Low,
}

/// One indicator: its bit in its bank's register, and its name as the
/// reports give it, lower case and ASCII.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) struct Indicator {
    /// Its bit, 0 to 7.
    pub(crate) bit: u8,
    /// Its name.
    pub(crate) name: &'static str,
}

/// A bank of indicators, the bits of one register.
#[derive(Debug, Clone)]
pub(crate) struct Bank {
    /// Its name in reports: `lamp`, `led`.
    name: &'static str,
    /// Which bits drive an indicator; the others drive nothing.
    indicators: &'static [Indicator],
    /// Which level of a bit lights its indicator.
    polarity: Polarity,
    /// The register, as last written.
    value: u8,
    /// Whether its changes are reported.
    reporting: bool,
}

/// An indicator lit or put out.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) struct Change {
    /// When, on the board's clock.
    pub(crate) at: Cycles,
    /// Its bank's name.
    pub(crate) bank: &'static str,
    /// Its name.
    pub(crate) name: &'static str,
    /// Lit, or put out.
    pub(crate) lit: bool,
}

impl fmt::Display for Change {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            formatter,
            "indicator cycle={} bank={} name={} lit={}",
            self.at.0,
            self.bank,
            self.name,
            u8::from(self.lit)
        )
    }
}

impl Bank {
    /// A bank named `name`, its `indicators` lit as `polarity` says,
    /// its register `value` at power-up, not reported.
    pub(crate) const fn new(
        name: &'static str,
        indicators: &'static [Indicator],
        polarity: Polarity,
        value: u8,
    ) -> Self {
        Self {
            name,
            indicators,
            polarity,
            value,
            reporting: false,
        }
    }

    /// The indicators lit at `at`, in the order of their bits.
    pub(crate) fn lit(&self, at: Cycles) -> Vec<Change> {
        self.indicators
            .iter()
            .filter(|indicator| self.lights(self.value, indicator.bit))
            .map(|indicator| Change {
                at,
                bank: self.name,
                name: indicator.name,
                lit: true,
            })
            .collect()
    }

    /// Reports the bank from `at` on: what is lit then, and each change
    /// after.
    pub(crate) fn report_from(&mut self, at: Cycles) {
        self.reporting = true;
        for change in self.lit(at) {
            trace::emit(&change);
        }
    }

    /// The register, as last written.
    pub(crate) fn value(&self) -> u8 {
        self.value
    }

    /// Whether `value` lights the indicator on `bit`.
    fn lights(&self, value: u8, bit: u8) -> bool {
        let set = value & 1 << bit != 0;
        set == (self.polarity == Polarity::High)
    }

    /// The register written `value` at `at`: the indicators it changes,
    /// in the order of their bits.
    pub(crate) fn write(&mut self, at: Cycles, value: u8) -> Vec<Change> {
        let was = std::mem::replace(&mut self.value, value);
        self.indicators
            .iter()
            .filter(|indicator| {
                self.lights(was, indicator.bit) != self.lights(value, indicator.bit)
            })
            .map(|indicator| Change {
                at,
                bank: self.name,
                name: indicator.name,
                lit: self.lights(value, indicator.bit),
            })
            .collect()
    }

    /// The register written `value` at `at`, each change reported on
    /// standard error while the bank is reported.
    pub(crate) fn write_reporting(&mut self, at: Cycles, value: u8) {
        let changes = self.write(at, value);
        if self.reporting {
            for change in changes {
                trace::emit(&change);
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::Bank;
    use super::Change;
    use super::Indicator;
    use super::Polarity;
    use crate::board::time::Cycles;

    /// Two indicators, on bits 0 and 2.
    const PAIR: [Indicator; 2] = [
        Indicator { bit: 0, name: "a" },
        Indicator { bit: 2, name: "b" },
    ];

    fn names(changes: &[Change]) -> Vec<(&str, bool)> {
        changes
            .iter()
            .map(|change| (change.name, change.lit))
            .collect()
    }

    #[test]
    fn only_a_change_is_reported_and_bits_driving_nothing_are_not() {
        let mut bank = Bank::new("lamp", &PAIR, Polarity::High, 0);
        assert_eq!(
            names(&bank.write(Cycles(1), 0x05)),
            [("a", true), ("b", true)]
        );
        assert!(bank.write(Cycles(2), 0x05).is_empty(), "unchanged");
        assert!(
            bank.write(Cycles(3), 0x07).is_empty(),
            "bit 1 drives nothing"
        );
        assert_eq!(names(&bank.write(Cycles(4), 0x03)), [("b", false)]);
        assert_eq!(bank.value(), 0x03);
    }

    #[test]
    fn an_active_low_bank_lights_on_a_zero() {
        let mut bank = Bank::new("led", &PAIR, Polarity::Low, 0xff);
        assert!(bank.lit(Cycles(0)).is_empty(), "dark");
        assert_eq!(names(&bank.write(Cycles(0), 0xfe)), [("a", true)]);
    }

    #[test]
    fn what_a_bank_lights_at_power_up_is_lit() {
        let bank = Bank::new("led", &PAIR, Polarity::Low, 0xfb);
        assert_eq!(names(&bank.lit(Cycles(0))), [("b", true)]);
    }

    #[test]
    fn a_change_reads_as_a_trace_line() {
        let change = Change {
            at: Cycles(16_777_216),
            bank: "lamp",
            name: "gps_lock",
            lit: true,
        };
        assert_eq!(
            change.to_string(),
            "indicator cycle=16777216 bank=lamp name=gps_lock lit=1"
        );
    }
}
