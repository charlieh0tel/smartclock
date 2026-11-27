//! The simulation's controls (docs/emulator.md, "The controls"): what
//! happens to the receiver from outside it, not on its panel: its
//! antenna pulled and put back, its warm-up skipped, its power cycled,
//! its CPU reset.  A script's `event` step or the panel page's control
//! strip gives one; the run loop applies it at the end of a slice, as a
//! key, and traces it, so that it replays.

use crate::names::Named;

/// A control, as a script's `event` step and its record name it.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) enum Control {
    /// The antenna disconnected: the synthetic engine tracks nothing.
    AntennaOff,
    /// The antenna connected again: the fixed sky back.
    AntennaOn,
    /// The board's warm-up curve jumped to its settled end.
    SkipWarmUp,
    /// Power off and on: the board built again from its file as read at
    /// the start, its EEPROM, its warm-up and its oscillator's crystal
    /// kept.
    PowerCycle,
    /// A warm restart, as the SIM's software watchdog's time-out gives
    /// one.
    Reset,
}

impl Named for Control {
    /// The controls in the order the page shows them, each with the name
    /// a script gives it.
    const NAMES: &'static [(Self, &'static str)] = &[
        (Self::AntennaOff, "antenna-off"),
        (Self::AntennaOn, "antenna-on"),
        (Self::SkipWarmUp, "skip-warm-up"),
        (Self::PowerCycle, "power-cycle"),
        (Self::Reset, "reset"),
    ];
}

#[cfg(test)]
mod tests {
    use super::Control;
    use crate::names::Named;

    #[test]
    fn each_control_reads_back_from_its_name() {
        for &(control, name) in Control::NAMES {
            assert_eq!(control.name(), name);
            assert_eq!(Control::from_name(name), Some(control));
        }
        assert_eq!(Control::from_name("antenna"), None);
    }
}
