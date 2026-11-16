//! The monitoring messages, `UBX-MON-*`.

use smartclock_gps::record::Antenna;

use crate::fields::Fields;
use crate::fields::bit_field;

/// `MON-HW`'s `aStatus` values (p. 342).
const ANTENNA_OK: u8 = 2;
const ANTENNA_SHORT: u8 = 3;
const ANTENNA_OPEN: u8 = 4;
/// `MON-HW`'s `aPower` values (p. 342).
const POWER_OFF: u8 = 0;
const POWER_ON: u8 = 1;
/// `MON-HW`'s `flags` (p. 342): `rtcCalib` bit 0, `safeBoot` bit 1,
/// `jammingState` bits 2 and 3, `xtalAbsent` bit 4.
const HW_RTC_CALIBRATED: u32 = 0;
const HW_SAFE_BOOT: u32 = 1;
const HW_JAMMING_SHIFT: u32 = 2;
const HW_JAMMING_WIDTH: u32 = 2;
const HW_CRYSTAL_ABSENT: u32 = 4;
/// `MON-HW`'s `VP`: the virtual pins' mapping to physical ones.
const VIRTUAL_PINS: usize = 17;

/// Whether the antenna is powered: `MON-HW`'s `aPower` (p. 342).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum AntennaPower {
    /// 0: off.
    Off,
    /// 1: on.
    On,
    /// 2, "don't know", or a value the document does not give.
    Unknown,
}

/// `UBX-MON-HW` (section 32.16.4, pp. 342-343), every field.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct MonHw {
    /// `pinSel`: the pins selected.
    pub pin_select: u32,
    /// `pinBank`: their banks.
    pub pin_bank: u32,
    /// `pinDir`: their directions.
    pub pin_direction: u32,
    /// `pinVal`: their values.
    pub pin_value: u32,
    /// `noisePerMS`: the noise level the CW interference detector
    /// measures.
    pub noise: u16,
    /// `agcCnt`: the AGC monitor's count, 0 to 8191.
    pub agc_count: u16,
    /// `aStatus`, the antenna supervisor's state: unknown while it
    /// starts up or does not know.
    pub antenna: Antenna,
    /// `aPower`.
    pub antenna_power: AntennaPower,
    /// `flags` bit 0, `rtcCalib`: the RTC is calibrated.
    pub rtc_calibrated: bool,
    /// `flags` bit 1, `safeBoot`: in safe boot mode.
    pub safe_boot: bool,
    /// `flags` bits 2 and 3, `jammingState`: 0 unknown or disabled, 1
    /// no jamming, 2 a warning, 3 critical.
    pub jamming_state: u8,
    /// `flags` bit 4, `xtalAbsent`: the RTC crystal is absent.
    pub crystal_absent: bool,
    /// `usedMask`: the pins used by a peripheral.
    pub used_mask: u32,
    /// `VP`: each virtual pin's physical pin.
    pub virtual_pins: [u8; VIRTUAL_PINS],
    /// `jamInd`: the CW jamming indicator, 0 none to 255 strong.
    pub jamming_indicator: u8,
    /// `pinIrq`: the pins with an interrupt.
    pub pin_irq: u32,
    /// `pullH`: the pins with a pull-up.
    pub pull_high: u32,
    /// `pullL`: the pins with a pull-down.
    pub pull_low: u32,
}

impl MonHw {
    pub(crate) fn parse(fields: Fields<'_>) -> Self {
        let flags = u32::from(fields.u8(22));
        Self {
            pin_select: fields.u32(0),
            pin_bank: fields.u32(4),
            pin_direction: fields.u32(8),
            pin_value: fields.u32(12),
            noise: fields.u16(16),
            agc_count: fields.u16(18),
            antenna: match fields.u8(20) {
                ANTENNA_OK => Antenna::Connected,
                ANTENNA_SHORT => Antenna::Shorted,
                ANTENNA_OPEN => Antenna::Open,
                _ => Antenna::Unknown,
            },
            antenna_power: match fields.u8(21) {
                POWER_OFF => AntennaPower::Off,
                POWER_ON => AntennaPower::On,
                _ => AntennaPower::Unknown,
            },
            rtc_calibrated: fields.bit(22, HW_RTC_CALIBRATED),
            safe_boot: fields.bit(22, HW_SAFE_BOOT),
            jamming_state: bit_field(flags, HW_JAMMING_SHIFT, HW_JAMMING_WIDTH),
            crystal_absent: fields.bit(22, HW_CRYSTAL_ABSENT),
            used_mask: fields.u32(24),
            virtual_pins: fields.bytes(28),
            jamming_indicator: fields.u8(45),
            pin_irq: fields.u32(48),
            pull_high: fields.u32(52),
            pull_low: fields.u32(56),
        }
    }
}
