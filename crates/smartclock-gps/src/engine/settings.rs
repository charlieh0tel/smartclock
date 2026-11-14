//! What an Oncore keeps of what it is told: a value for each command
//! that sets one, asked back by sending the command with its
//! out-of-range bytes.  The pages cited are the VP Oncore's command
//! reference, `third_party/motorola/VPCommands.pdf`, as printed.

use crate::engine::identity::Model;
use crate::oncore::AB_APPLICATION_TYPE;
use crate::oncore::AB_GMT_CORRECTION;
use crate::oncore::AC_HDOP_THRESHOLD;
use crate::oncore::AD_CORRECTION_THRESHOLDS;
use crate::oncore::AG_MASK_ANGLE;
use crate::oncore::AH_SATELLITE_SELECT;
use crate::oncore::AJ_XDOP_TYPE;
use crate::oncore::AK_XDOP_HYSTERESIS;
use crate::oncore::AL_XDOP_THRESHOLD;
use crate::oncore::AM_IGNORE_LIST;
use crate::oncore::AN_ALMANAC_UPDATE;
use crate::oncore::AO_DATUM_SELECT;
use crate::oncore::AQ_ATMOSPHERIC_CORRECTION;
use crate::oncore::AR_FIX_ALGORITHM;
use crate::oncore::AS_HOLD_POSITION;
use crate::oncore::AT_POSITION_HOLD;
use crate::oncore::AU_ALTITUDE_HOLD_HEIGHT;
use crate::oncore::AV_ALTITUDE_HOLD;
use crate::oncore::AW_TIME_MODE;
use crate::oncore::AX_EPOCH_OFFSET;
use crate::oncore::AY_PULSE_OFFSET;
use crate::oncore::AZ_CABLE_DELAY;
use crate::oncore::MessageId;
use crate::oncore::TA_DCXO;
use crate::units::Minutes;
use crate::units::Seconds;

/// The byte that asks for a value in place of setting it (as `@@Ag`'s,
/// p. 27).
pub(super) const QUERY: u8 = 0xff;

/// The height type that says mean sea level (`@@Af`, p. 25).
pub(super) const HEIGHT_MEAN_SEA_LEVEL: u8 = 1;

/// The first byte an ignore list's command must carry (`@@Am`, p. 41).
const IGNORE_LIST_CONSTANT: u8 = 0;
/// The heights an altitude hold takes, in centimeters (`@@Au`, p. 58).
const HEIGHT_LEAST: i32 = -100_000;
const HEIGHT_MOST: i32 = 1_800_000;
/// The most a VP's atmospheric correction mode may be (`@@Aq`, p. 50),
/// and a UT Plus's, which has two modes more.
const CORRECTION_MOST_VP: u8 = 1;
const CORRECTION_MOST_UT_PLUS: u8 = 3;

/// What values a setting takes.
#[derive(Debug, Clone, Copy)]
enum Takes {
    /// Any.
    Anything,
    /// Bytes each no more than this.
    Bytes(u8),
    /// One word within these bounds.
    Word(u16, u16),
    /// One long no more than this.
    Long(u32),
    /// An ignore list: its constant byte, then any mask.
    IgnoreList,
    /// An atmospheric correction mode, whose range is the model's.
    Correction,
    /// A height within range and its type.
    Height,
}

impl Takes {
    fn takes(self, data: &[u8], model: Model) -> bool {
        match (self, data) {
            (Self::Anything, _) => true,
            (Self::Bytes(most), _) => data.iter().all(|byte| *byte <= most),
            (Self::Word(least, most), &[high, low]) => {
                (least..=most).contains(&u16::from_be_bytes([high, low]))
            }
            (Self::Long(most), &[a, b, c, d]) => u32::from_be_bytes([a, b, c, d]) <= most,
            (Self::IgnoreList, [constant, ..]) => *constant == IGNORE_LIST_CONSTANT,
            (Self::Correction, [mode]) => {
                *mode
                    <= match model {
                        Model::VpSix | Model::VpEight => CORRECTION_MOST_VP,
                        Model::UtPlus => CORRECTION_MOST_UT_PLUS,
                    }
            }
            (Self::Height, &[a, b, c, d, kind]) => {
                (HEIGHT_LEAST..=HEIGHT_MOST).contains(&i32::from_be_bytes([a, b, c, d]))
                    && kind <= HEIGHT_MEAN_SEA_LEVEL
            }
            _ => false,
        }
    }
}

/// One command that sets a value and is answered with it.
#[derive(Debug)]
struct Setting {
    /// The command, and its answer.
    id: MessageId,
    /// The data that asks for the value.
    query: &'static [u8],
    /// The value at power-up and after `@@Cf`.
    default: &'static [u8],
    /// What it may be set to.
    takes: Takes,
}

const fn setting(
    id: MessageId,
    query: &'static [u8],
    default: &'static [u8],
    takes: Takes,
) -> Setting {
    assert!(query.len() == default.len());
    Setting {
        id,
        query,
        default,
        takes,
    }
}

/// Every setting: pp. 9 to 69, and `@@Ta`, which is in no manual here
/// ([`TA_DCXO`]).
const SETTINGS: [Setting; 23] = [
    setting(AB_GMT_CORRECTION, &[QUERY; 3], &[0; 3], Takes::Anything),
    setting(AG_MASK_ANGLE, &[QUERY], &[10], Takes::Bytes(89)),
    setting(AH_SATELLITE_SELECT, &[QUERY], &[0], Takes::Bytes(2)),
    setting(AJ_XDOP_TYPE, &[QUERY], &[1], Takes::Bytes(4)),
    setting(
        AK_XDOP_HYSTERESIS,
        &[QUERY; 2],
        &[0, 10],
        Takes::Word(0, 999),
    ),
    setting(
        AL_XDOP_THRESHOLD,
        &[QUERY; 2],
        &[0, 60],
        Takes::Word(10, 999),
    ),
    setting(
        AM_IGNORE_LIST,
        &[QUERY, 0, 0, 0, 0],
        &[0; 5],
        Takes::IgnoreList,
    ),
    setting(AN_ALMANAC_UPDATE, &[QUERY], &[1], Takes::Bytes(1)),
    setting(AO_DATUM_SELECT, &[QUERY], &[WGS_84], Takes::Anything),
    setting(AQ_ATMOSPHERIC_CORRECTION, &[QUERY], &[1], Takes::Correction),
    setting(AR_FIX_ALGORITHM, &[QUERY], &[1], Takes::Bytes(1)),
    setting(
        AS_HOLD_POSITION,
        &[
            0x7f, 0xff, 0xff, 0xff, 0x7f, 0xff, 0xff, 0xff, 0x7f, 0xff, 0xff, 0xff, QUERY,
        ],
        &[0; 13],
        Takes::Anything,
    ),
    setting(AT_POSITION_HOLD, &[QUERY], &[0], Takes::Bytes(1)),
    setting(AU_ALTITUDE_HOLD_HEIGHT, &[QUERY; 5], &[0; 5], Takes::Height),
    setting(AV_ALTITUDE_HOLD, &[QUERY], &[0], Takes::Bytes(1)),
    setting(AW_TIME_MODE, &[QUERY], &[TIME_MODE_UTC], Takes::Bytes(1)),
    setting(AX_EPOCH_OFFSET, &[QUERY; 2], &[0; 2], Takes::Word(0, 999)),
    setting(
        AY_PULSE_OFFSET,
        &[QUERY; 4],
        &[0; 4],
        Takes::Long(999_999_999),
    ),
    setting(AZ_CABLE_DELAY, &[QUERY; 4], &[0; 4], Takes::Anything),
    setting(AB_APPLICATION_TYPE, &[QUERY], &[2], Takes::Bytes(4)),
    setting(
        AC_HDOP_THRESHOLD,
        &[QUERY; 2],
        &[0, 120],
        Takes::Word(10, 999),
    ),
    setting(
        AD_CORRECTION_THRESHOLDS,
        &[QUERY; 2],
        &[0, 32],
        Takes::Bytes(127),
    ),
    setting(TA_DCXO, &[QUERY; 5], &[0; 5], Takes::Anything),
];

/// The datum an Oncore starts on, WGS-84 (`@@Ao`, p. 45).
const WGS_84: u8 = 49;
/// `@@Aw`'s modes (p. 62).
const TIME_MODE_GPS: u8 = 0;
const TIME_MODE_UTC: u8 = 1;
/// `@@At`'s mode that holds the position (p. 56).
const POSITION_HOLD_ON: u8 = 1;

const MINUTES_PER_HOUR: i16 = 60;

/// The bytes every setting's value takes together.
const SETTINGS_BYTES: usize = {
    let mut total = 0;
    let mut index = 0;
    while index < SETTINGS.len() {
        total += SETTINGS[index].default.len();
        index += 1;
    }
    total
};

/// Where in the store a setting's value starts.
const fn start(index: usize) -> usize {
    let mut total = 0;
    let mut before = 0;
    while before < index {
        total += SETTINGS[before].default.len();
        before += 1;
    }
    total
}

/// A position as the messages carry one: milliarcseconds and
/// centimeters.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub(super) struct Place {
    /// North positive.
    pub(super) latitude: i32,
    /// East positive.
    pub(super) longitude: i32,
    /// Height above the ellipsoid.
    pub(super) ellipsoid: i32,
    /// Height above mean sea level.
    pub(super) mean_sea_level: i32,
}

/// Every setting's value, each as its command carries it, one after
/// another in the table's order.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(super) struct Settings {
    /// The values.
    bytes: [u8; SETTINGS_BYTES],
}

impl Settings {
    /// The values at power-up.
    pub(super) const DEFAULT: Self = {
        let mut bytes = [0; SETTINGS_BYTES];
        let mut index = 0;
        let mut at = 0;
        while index < SETTINGS.len() {
            let default = SETTINGS[index].default;
            let mut byte = 0;
            while byte < default.len() {
                bytes[at] = default[byte];
                at += 1;
                byte += 1;
            }
            index += 1;
        }
        Self { bytes }
    };

    fn index(id: MessageId) -> Option<usize> {
        SETTINGS.iter().position(|setting| setting.id == id)
    }

    /// A setting's value, `None` if `id` is not one.
    pub(super) fn get(&self, id: MessageId) -> Option<&[u8]> {
        let index = Self::index(id)?;
        let from = start(index);
        self.bytes.get(from..from + SETTINGS[index].default.len())
    }

    /// Takes a setting's command: stores `data` unless it asks for the
    /// value, is of the wrong length or is out of range.
    pub(super) fn set_or_query(&mut self, id: MessageId, data: &[u8], model: Model) {
        let Some(index) = Self::index(id) else {
            return;
        };
        let setting = &SETTINGS[index];
        if setting.query != data
            && setting.default.len() == data.len()
            && setting.takes.takes(data, model)
            && let Some(held) = self.bytes.get_mut(start(index)..start(index) + data.len())
        {
            held.copy_from_slice(data);
        }
    }

    fn byte(&self, id: MessageId) -> u8 {
        self.get(id)
            .and_then(|value| value.first().copied())
            .unwrap_or(0)
    }

    /// Whether the position is held (`@@At`).
    pub(super) fn holding(&self) -> bool {
        self.byte(AT_POSITION_HOLD) == POSITION_HOLD_ON
    }

    /// The elevation a satellite must be above to be tracked (`@@Ag`).
    pub(super) fn mask_angle(&self) -> u8 {
        self.byte(AG_MASK_ANGLE)
    }

    /// The satellites not to track, bit 0 satellite 1 (`@@Am`).
    pub(super) fn ignore_list(&self) -> u32 {
        match self.get(AM_IGNORE_LIST) {
            Some(&[_, a, b, c, d]) => u32::from_be_bytes([a, b, c, d]),
            _ => 0,
        }
    }

    /// Whether times are given in GPS time, not UTC (`@@Aw`).
    pub(super) fn gps_time(&self) -> bool {
        self.byte(AW_TIME_MODE) == TIME_MODE_GPS
    }

    /// How far ahead of UTC the times given are (`@@Ab`).
    pub(super) fn gmt_offset(&self) -> Seconds<i64> {
        let Some(&[sign, hours, minutes]) = self.get(AB_GMT_CORRECTION) else {
            return Seconds(0);
        };
        let ahead = Minutes(i16::from(hours) * MINUTES_PER_HOUR + i16::from(minutes)).seconds();
        if sign == 0 { ahead } else { -ahead }
    }

    /// The kind of DOP asked for (`@@Aj`).
    pub(super) fn dop_type(&self) -> u8 {
        self.byte(AJ_XDOP_TYPE)
    }

    /// The datum (`@@Ao`).
    pub(super) fn datum(&self) -> u8 {
        self.byte(AO_DATUM_SELECT)
    }

    /// The position to hold (`@@As`), `separation` being the ellipsoid's
    /// height above mean sea level there.
    pub(super) fn held_position(&self, separation: i32) -> Place {
        let Some(&[a, b, c, d, e, f, g, h, i, j, k, l, kind]) = self.get(AS_HOLD_POSITION) else {
            return Place::default();
        };
        let height = i32::from_be_bytes([i, j, k, l]);
        let (ellipsoid, mean_sea_level) = if kind == HEIGHT_MEAN_SEA_LEVEL {
            (height.wrapping_add(separation), height)
        } else {
            (height, height.wrapping_sub(separation))
        };
        Place {
            latitude: i32::from_be_bytes([a, b, c, d]),
            longitude: i32::from_be_bytes([e, f, g, h]),
            ellipsoid,
            mean_sea_level,
        }
    }
}

/// Whether `id` is a command that sets a value.
pub(super) fn is_setting(id: MessageId) -> bool {
    Settings::index(id).is_some()
}
