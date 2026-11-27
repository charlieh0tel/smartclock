//! What the panel shows at a moment of the run, as the run publishes it
//! and `/state` answers it (docs/emulator.md, "The panel page").

use serde::Serialize;

use crate::board::front_panel::Display;
use crate::board::front_panel::Key;
use crate::board::profile::Model;
use crate::board::time::Cycles;
use crate::control::Control;
use crate::indicators::Change;
use crate::names::Named;

use super::json;
use super::json::Bytes;

/// Which drawing the page shows, by the board's model: the 58503A's
/// with Option 001's display and keys when the board has the front
/// panel, its standard panel's four lamps when not, the 58503B's the
/// same faces with its name, the Z3805A's black band, the Z3801A's
/// plate, the Z3816A's band with its power LED alone.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) enum Drawing {
    /// The 58503A with Option 001.
    Hp58503AOption001,
    /// The 58503A's standard panel, the lamps alone.
    Hp58503A,
    /// The Z3805A.
    Z3805A,
    /// The Z3801A.
    Z3801A,
    /// The Z3816A.
    Z3816A,
    /// The 58503B with Option 001.
    Hp58503BOption001,
    /// The 58503B's standard panel.
    Hp58503B,
}

impl Named for Drawing {
    /// Each drawing with its name, as the page's drawings are keyed.
    const NAMES: &'static [(Self, &'static str)] = &[
        (Self::Hp58503AOption001, "58503a-option-001"),
        (Self::Hp58503A, "58503a"),
        (Self::Z3805A, "z3805a"),
        (Self::Z3801A, "z3801a"),
        (Self::Z3816A, "z3816a"),
        (Self::Hp58503BOption001, "58503b-option-001"),
        (Self::Hp58503B, "58503b"),
    ];
}

impl Drawing {
    /// `model`'s drawing, with or without the front panel.
    pub(crate) fn of(model: Model, front_panel: bool) -> Self {
        match model {
            Model::Hp58503A if front_panel => Self::Hp58503AOption001,
            Model::Hp58503A => Self::Hp58503A,
            Model::Z3805A => Self::Z3805A,
            Model::Z3801A => Self::Z3801A,
            Model::Z3816A => Self::Z3816A,
            Model::Hp58503B if front_panel => Self::Hp58503BOption001,
            Model::Hp58503B => Self::Hp58503B,
        }
    }
}

/// What the panel shows at a moment of the run.
#[derive(Debug, Clone, PartialEq)]
pub(crate) struct Snapshot {
    /// Which drawing.
    pub(crate) drawing: Drawing,
    /// When, on the chip's cycles, as the trace counts them.
    pub(crate) cycle: Cycles,
    /// When, seconds of board time since power-up.
    pub(crate) seconds: f64,
    /// Every lamp and LED, lit or not.
    pub(crate) indicators: Vec<Change>,
    /// The front panel's display, if the board has one.
    pub(crate) display: Option<Display>,
    /// The simulation's controls' state.
    pub(crate) controls: Controls,
}

/// What the page's control strip shows of the simulation's controls
/// (docs/emulator.md, "The controls").
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub(crate) struct Controls {
    /// Whether the antenna is connected; none without the synthetic
    /// engine, whose antenna it is.
    pub(crate) antenna: Option<bool>,
    /// Whether the board's warm-up curve is still short of its end;
    /// none without the oscillator, whose oven it is.
    pub(crate) warming: Option<bool>,
}

impl Controls {
    /// The controls that apply: the antenna's with the engine, the
    /// warm-up's with the oscillator, and the power and the reset always.
    fn applying(self) -> impl Iterator<Item = Control> {
        Control::all().filter(move |control| match control {
            Control::AntennaOff | Control::AntennaOn => self.antenna.is_some(),
            Control::SkipWarmUp => self.warming.is_some(),
            Control::PowerCycle | Control::Reset => true,
        })
    }
}

/// `/state`'s object, as docs/emulator.md gives it.
#[derive(Debug, Serialize)]
struct StateJson<'a> {
    /// The drawing's name.
    drawing: &'static str,
    /// The chip's cycles.
    cycle: u64,
    /// Board seconds since the run's start.
    seconds: f64,
    /// Every lamp and LED.
    indicators: Vec<IndicatorJson>,
    /// The front panel's display, null without one.
    display: Option<DisplayJson<'a>>,
    /// The keys' names in the lamp byte's order, none without a panel.
    keys: Vec<&'static str>,
    /// The simulation's controls.
    controls: ControlsJson,
}

/// A lamp or an LED in `/state`.
#[derive(Debug, Serialize)]
struct IndicatorJson {
    /// Its bank, `lamp` or `led`.
    bank: &'static str,
    /// Its name in the bank.
    name: &'static str,
    /// Whether it is lit.
    lit: bool,
}

/// The display in `/state`.
#[derive(Debug, Serialize)]
struct DisplayJson<'a> {
    /// The key lamps' byte.
    lamps: u8,
    /// The text, each byte as the character of its code.
    text: Bytes<'a>,
}

/// The controls in `/state`.
#[derive(Debug, Serialize)]
struct ControlsJson {
    /// The names of those that apply.
    names: Vec<&'static str>,
    /// Whether the antenna is connected, null without the engine.
    antenna: Option<bool>,
    /// Whether the warm-up is short of its end, null without the oven.
    warming: Option<bool>,
}

impl Snapshot {
    /// As the state endpoint sends it: an object of `drawing`, `cycle`,
    /// `seconds`, `indicators` (each `bank`, `name` and `lit`),
    /// `display` (`lamps`, the key lamps' byte, and `text`), null
    /// without a panel, and `keys`, the keys' names in the order of the
    /// lamp byte's bits, empty without a panel; and `controls`, the
    /// simulation's: `names`, those that apply, `antenna`, whether it is
    /// connected, and `warming`, whether the warm-up is short of its end,
    /// each null where it does not apply.
    pub(super) fn json(&self) -> String {
        json::to_string(&StateJson {
            drawing: self.drawing.name(),
            cycle: self.cycle.0,
            seconds: self.seconds,
            indicators: self
                .indicators
                .iter()
                .map(|change| IndicatorJson {
                    bank: change.bank,
                    name: change.name,
                    lit: change.lit,
                })
                .collect(),
            display: self.display.as_ref().map(|display| DisplayJson {
                lamps: display.lamps,
                text: Bytes(&display.text),
            }),
            keys: if self.display.is_some() {
                Key::all_names().collect()
            } else {
                Vec::new()
            },
            controls: ControlsJson {
                names: self.controls.applying().map(Control::name).collect(),
                antenna: self.controls.antenna,
                warming: self.controls.warming,
            },
        })
    }
}
