//! The timing messages, `UBX-TIM-*`.

use smartclock_gps::record::Raim;
use smartclock_gps::record::RaimAbility;
use smartclock_gps::record::RaimSolution;
use smartclock_gps::units::Milliseconds;

use crate::fields::Fields;
/// `TIM-TP`'s `flags` bits (p. 433), and the values of its `raim`,
/// in bits 2 and 3: 0 is no information.
const TP_UTC_BASE: u8 = 1 << 0;
const TP_RAIM_SHIFT: u8 = 2;
const TP_RAIM: u8 = 0b11;
const TP_RAIM_NOT_ACTIVE: u8 = 1;
const TP_RAIM_ACTIVE: u8 = 2;
const TP_ERROR_INVALID: u8 = 1 << 4;
const TP_NOT_LOCKED: u8 = 1 << 5;

/// A count of picoseconds, `TIM-TP`'s unit for `qErr`.
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct Picoseconds(pub i32);

/// `UBX-TIM-TP` (section 32.20.8, pp. 432-433): the next time pulse.
/// The receiver sends it with the epoch before that pulse (section
/// 19.2, pp. 72-73).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct TimTp {
    /// `towMS`: the pulse's time of week, in the pulse's time base.
    pub time_of_week: Milliseconds<u32>,
    /// `week`: its week, in that time base.
    pub week: u16,
    /// `qErr`, the pulse's quantization error; `None` with
    /// `qErrInvalid` set.  The document does not give its sign.
    pub quantization_error: Option<Picoseconds>,
    /// `timeBase`: whether the time base is UTC, not a satellite
    /// system's time.
    pub utc_base: bool,
    /// Whether the pulse is locked to the satellites: `TpNotLocked`
    /// clear.
    pub locked: bool,
    /// `raim`: whether the receiver's (T)RAIM is active; `None` when
    /// it has no information.
    pub raim: Option<bool>,
}

impl TimTp {
    pub(crate) fn parse(fields: Fields<'_>) -> Self {
        let flags = fields.u8(14);
        Self {
            time_of_week: Milliseconds(fields.u32(0)),
            week: fields.u16(12),
            quantization_error: (flags & TP_ERROR_INVALID == 0).then(|| Picoseconds(fields.i32(8))),
            utc_base: flags & TP_UTC_BASE != 0,
            locked: flags & TP_NOT_LOCKED == 0,
            raim: match (flags >> TP_RAIM_SHIFT) & TP_RAIM {
                TP_RAIM_NOT_ACTIVE => Some(false),
                TP_RAIM_ACTIVE => Some(true),
                _ => None,
            },
        }
    }

    /// What a record makes of `raim` ("From messages to a record" in
    /// the crate's documentation).
    pub(crate) fn time_raim(&self) -> Option<Raim> {
        self.raim.map(|active| match (active, self.locked) {
            (true, true) => Raim::new(RaimSolution::Ok, RaimAbility::Detection),
            (true, false) => Raim::new(RaimSolution::Unknown, RaimAbility::Detection),
            (false, _) => Raim::new(RaimSolution::Unknown, RaimAbility::Neither),
        })
    }
}
