//! The monitoring messages, `UBX-MON-*`.

use smartclock_gps::record::Antenna;

use crate::fields::Fields;
/// `MON-HW`'s `aStatus` values (p. 342).
const ANTENNA_OK: u8 = 2;
const ANTENNA_SHORT: u8 = 3;
const ANTENNA_OPEN: u8 = 4;

/// `UBX-MON-HW` (section 32.16.4, pp. 342-343): what of it a record
/// holds.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct MonHw {
    /// `aStatus`, the antenna supervisor's state: unknown while it
    /// starts up or does not know.
    pub antenna: Antenna,
}

impl MonHw {
    pub(crate) fn parse(fields: Fields<'_>) -> Self {
        Self {
            antenna: match fields.u8(20) {
                ANTENNA_OK => Antenna::Connected,
                ANTENNA_SHORT => Antenna::Shorted,
                ANTENNA_OPEN => Antenna::Open,
                _ => Antenna::Unknown,
            },
        }
    }
}
