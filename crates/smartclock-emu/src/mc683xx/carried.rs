//! What a power cycle carries from one board to the next: what is not
//! the board's own to lose (docs/emulator.md, "The controls").

use mc683xx_board::Board;
use mc683xx_board::time::Span;
use mc683xx_board::time::Time;
use mc683xx_mc68331::Mc68331;

use super::Error;
use super::KeptEeprom;
use super::eeprom;
use super::overrides::Parts;
use crate::board::analog::Warmth;
use crate::board::ocxo::Crystal;
use crate::board::participants::ocxo;
use crate::board::system_clock::board_cycles;
use crate::board::time::Cycles;

/// The run's clock at a power-up: zero at the run's start, and at each
/// power cycle where the clock had come to.  The board's own clock, and
/// the chip's, start again at each power-up; the run's goes on, and a
/// script's steps and the records' `at` count it.
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub(super) struct Epoch {
    /// The chip's system clocks.
    pub(super) cycles: Cycles,
    /// Board time.
    pub(super) time: Time,
}

/// What a power cycle carries to the board it builds.
#[derive(Debug, Clone, PartialEq)]
pub(super) struct Carried {
    /// The run's clock at this power-up.
    pub(super) epoch: Epoch,
    /// Whether the antenna is connected.
    pub(super) antenna: bool,
    /// The warm-up at this power-up: a short power cycle does not cool
    /// the ovens.
    pub(super) warmth: Warmth,
    /// The oscillator's crystal at this power-up: its aging and its
    /// noise are the crystal's, not the power's.
    pub(super) crystal: Crystal,
    /// The EEPROM at this power-up, when it is in memory and a power
    /// cycle has kept it: its cells and its protection, as the chip
    /// keeps them.
    pub(super) eeprom: Option<KeptEeprom>,
}

impl Default for Carried {
    /// At the run's start.
    fn default() -> Self {
        Self {
            epoch: Epoch::default(),
            antenna: true,
            warmth: Warmth::default(),
            crystal: Crystal::default(),
            eeprom: None,
        }
    }
}

impl Carried {
    /// What a power cycle of `board`, whose parts are `parts`, carries to
    /// the board it builds, now: the run's clock where it has come to,
    /// the warm-up's place, on the board's own clock as the oscillator
    /// and the temperature count it, the oscillator's crystal, and the
    /// EEPROM's cells and protection as they stand, when it is in
    /// memory.
    pub(super) fn through_power_cycle(
        self,
        board: &Board<Mc68331>,
        parts: &Parts,
    ) -> Result<Self, Error> {
        let now = board.now();
        let cycles = Cycles(board.system_clock().cycle(now).clocks());
        let eeprom = match &parts.eeprom {
            Some(memory) => Some(eeprom::kept(board, &memory.name)?),
            None => self.eeprom.clone(),
        };
        Ok(Self {
            epoch: Epoch {
                cycles: self.epoch.cycles + cycles,
                time: self.epoch.time + now.since_power_on(),
            },
            warmth: self.warmth.at(board_cycles(now)),
            crystal: parts
                .ocxo
                .as_deref()
                .and_then(|name| ocxo::crystal_now(board, name))
                .unwrap_or(self.crystal),
            eeprom,
            ..self
        })
    }

    /// The synthetic engine's UTC at this power-up, the run's start at
    /// `start`: the whole seconds of board time the run has come to,
    /// since the engine's seconds start at its power-up.  Board time,
    /// not the chip's clocks, which run slow from each power-up and
    /// reset until the reset code sets SYNCR.
    pub(super) fn gps_start(&self, start: jiff::Timestamp) -> Result<jiff::Timestamp, Error> {
        let seconds = self.epoch.time / Span::from_secs(1);
        Ok(start.checked_add(jiff::SignedDuration::from_secs(i64::try_from(seconds)?))?)
    }
}
