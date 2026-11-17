//! The timing messages, `UBX-TIM-*`.

use smartclock_gps::record::Raim;
use smartclock_gps::record::RaimAbility;
use smartclock_gps::record::RaimSolution;
use smartclock_gps::units::Milliseconds;

use crate::fields::Fields;
use crate::fields::bit_field;
use crate::units::Picoseconds;
use crate::units::SubMilliseconds;

/// `TIM-TP`'s `flags` bits (p. 433), and the values of its `raim`,
/// in bits 2 and 3: 0 is no information.
const TP_UTC_BASE: u8 = 1 << 0;
const TP_UTC_AVAILABLE: u8 = 1 << 1;
const TP_RAIM_SHIFT: u8 = 2;
const TP_RAIM: u8 = 0b11;
const TP_RAIM_NOT_ACTIVE: u8 = 1;
const TP_RAIM_ACTIVE: u8 = 2;
const TP_ERROR_INVALID: u8 = 1 << 4;
const TP_NOT_LOCKED: u8 = 1 << 5;
/// `TIM-TP`'s `refInfo` (p. 433): `timeRefGnss` in bits 0 to 3,
/// `utcStandard` in bits 4 to 7.
const TP_REFERENCE_SHIFT: u32 = 0;
const TP_STANDARD_SHIFT: u32 = 4;
const TP_REFERENCE_WIDTH: u32 = 4;

/// `UBX-TIM-TP` (section 32.20.8, pp. 432-433), every field: the next
/// time pulse.  The receiver sends it with the epoch before that pulse
/// (section 19.2, pp. 72-73).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct TimTp {
    /// `towMS`: the pulse's time of week, in the pulse's time base.
    pub time_of_week: Milliseconds<u32>,
    /// `towSubMS`: the fraction of a millisecond to add to it.
    pub sub_millisecond: SubMilliseconds,
    /// `week`: its week, in that time base.
    pub week: u16,
    /// `qErr`, the pulse's quantization error; `None` with
    /// `qErrInvalid` set.  The document does not give its sign.
    pub quantization_error: Option<Picoseconds>,
    /// `timeBase`: whether the time base is UTC, not a satellite
    /// system's time.
    pub utc_base: bool,
    /// `utc`: whether UTC is available.
    pub utc_available: bool,
    /// Whether the pulse is locked to the satellites: `TpNotLocked`
    /// clear.
    pub locked: bool,
    /// `raim`: whether the receiver's (T)RAIM is active; `None` when
    /// it has no information.
    pub raim: Option<bool>,
    /// `timeRefGnss`: the system whose time the pulse is in, when the
    /// time base is a system's: 0 GPS, 1 GLONASS, 2 `BeiDou`, 3 Galileo, 4
    /// `NavIC`, 15 unknown.
    pub time_reference: u8,
    /// `utcStandard`: the UTC standard, when the time base is UTC.
    pub utc_standard: u8,
}

impl TimTp {
    pub(crate) fn parse(fields: Fields<'_>) -> Self {
        let flags = fields.u8(14);
        let reference = u32::from(fields.u8(15));
        Self {
            time_of_week: Milliseconds(fields.u32(0)),
            sub_millisecond: SubMilliseconds(fields.u32(4)),
            week: fields.u16(12),
            quantization_error: (flags & TP_ERROR_INVALID == 0).then(|| Picoseconds(fields.i32(8))),
            utc_base: flags & TP_UTC_BASE != 0,
            utc_available: flags & TP_UTC_AVAILABLE != 0,
            locked: flags & TP_NOT_LOCKED == 0,
            raim: match (flags >> TP_RAIM_SHIFT) & TP_RAIM {
                TP_RAIM_NOT_ACTIVE => Some(false),
                TP_RAIM_ACTIVE => Some(true),
                _ => None,
            },
            time_reference: bit_field(reference, TP_REFERENCE_SHIFT, TP_REFERENCE_WIDTH),
            utc_standard: bit_field(reference, TP_STANDARD_SHIFT, TP_REFERENCE_WIDTH),
        }
    }

    /// What a record makes of `raim` ("From messages to a record" in
    /// the crate's documentation): the solution is never known, the
    /// message giving no RAIM result or alarm.
    pub(crate) fn time_raim(&self) -> Option<Raim> {
        self.raim.map(|active| {
            let ability = if active {
                RaimAbility::Detection
            } else {
                RaimAbility::Neither
            };
            Raim::new(RaimSolution::Unknown, ability)
        })
    }
}
