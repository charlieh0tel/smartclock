//! What an Oncore keeps of what it is told: a value for each command
//! that sets one, asked back by sending the command with its
//! out-of-range bytes.  The pages cited are the VP Oncore's command
//! reference, `third_party/motorola/VPCommands.pdf`, as printed.

use crate::engine::identity::Model;
use crate::engine::settings::Application;
use crate::engine::settings::AtmosphericCorrection;
use crate::engine::settings::CorrectionThresholds;
use crate::engine::settings::FixAlgorithm;
use crate::engine::settings::Height;
use crate::engine::settings::HeightKind;
use crate::engine::settings::Location;
use crate::engine::settings::SatelliteSelect;
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
use crate::record::DopKind;
use crate::units::Centimeters;
use crate::units::MilliArcSeconds;
use crate::units::Milliseconds;
use crate::units::Minutes;
use crate::units::Nanoseconds;
use crate::units::PrnSet;
use crate::units::Seconds;
use crate::units::Tenths;

/// The byte that asks for a value in place of setting it (as `@@Ag`'s,
/// p. 27).
pub(super) const QUERY: u8 = 0xff;

/// The height types: the ellipsoid and mean sea level (`@@Af`, p. 25).
const HEIGHT_ELLIPSOID: u8 = 0;
const HEIGHT_MEAN_SEA_LEVEL: u8 = 1;

/// The height a command gives as four bytes and a height type (`@@Af`,
/// p. 26), `None` if `data` is not those.  A type that is not mean sea
/// level is the ellipsoid.
pub(super) fn height_of(data: &[u8]) -> Option<Height> {
    let &[a, b, c, d, kind] = data else {
        return None;
    };
    Some(Height {
        height: Centimeters(i32::from_be_bytes([a, b, c, d])),
        kind: if kind == HEIGHT_MEAN_SEA_LEVEL {
            HeightKind::MeanSeaLevel
        } else {
            HeightKind::Ellipsoid
        },
    })
}

/// The satellite select modes that are not the best geometry (`@@Ah`,
/// p. 30).
const SELECT_MANUAL: u8 = 1;
const SELECT_HIGHEST_IN_SKY: u8 = 2;
/// The fix algorithm that is the best four (`@@Ar`, p. 53).
const ALGORITHM_BEST_FOUR: u8 = 0;
/// The almanac update mode that updates (`@@An`, p. 44).
const ALMANAC_UPDATE_ON: u8 = 1;
/// An atmospheric correction mode's bits (UT p. 6.28), of which a VP
/// has the first alone (`@@Aq`, p. 51).
const CORRECTION_IONOSPHERIC: u8 = 1;
const CORRECTION_TROPOSPHERIC: u8 = 2;

/// The first byte an ignore list's command must carry (`@@Am`, p. 41).
const IGNORE_LIST_CONSTANT: u8 = 0;
/// The heights a command takes, in centimeters (`@@Af`, p. 25, UT
/// p. 6.16; `@@As`, p. 54, UT p. 6.88; `@@Au`, p. 58, UT p. 6.50).
const HEIGHT_LEAST: i32 = -100_000;
const HEIGHT_MOST: i32 = 1_800_000;
/// The most a latitude and a longitude may be from 0, in
/// milliarcseconds (`@@Ad`, p. 17, UT p. 6.12; `@@Ae`, p. 21, UT
/// p. 6.14; `@@As`, p. 54, UT p. 6.88).
const LATITUDE_MOST: i32 = 324_000_000;
const LONGITUDE_MOST: i32 = 648_000_000;

/// Whether a latitude, in milliarcseconds, is in range.
pub(super) fn latitude_takes(latitude: i32) -> bool {
    (-LATITUDE_MOST..=LATITUDE_MOST).contains(&latitude)
}

/// Whether a longitude, in milliarcseconds, is in range.
pub(super) fn longitude_takes(longitude: i32) -> bool {
    (-LONGITUDE_MOST..=LONGITUDE_MOST).contains(&longitude)
}

/// Whether four bytes of height and a height type are in range on an
/// Oncore of `model`: a VP takes the ellipsoid or mean sea level, a
/// UT Plus the ellipsoid alone (UT pp. 6.16, 6.50 and 6.88).
pub(super) fn height_takes(data: &[u8], model: Model) -> bool {
    let &[a, b, c, d, kind] = data else {
        return false;
    };
    let kind_most = match model {
        Model::VpSix | Model::VpEight => HEIGHT_MEAN_SEA_LEVEL,
        Model::UtPlus => HEIGHT_ELLIPSOID,
    };
    (HEIGHT_LEAST..=HEIGHT_MOST).contains(&i32::from_be_bytes([a, b, c, d])) && kind <= kind_most
}
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
    /// A height within range and its type ([`height_takes`]).
    Height,
    /// A latitude and longitude within range, then a height as
    /// [`Self::Height`].
    Position,
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
            (Self::Height, _) => height_takes(data, model),
            (Self::Position, &[a, b, c, d, e, f, g, h, ref height @ ..]) => {
                latitude_takes(i32::from_be_bytes([a, b, c, d]))
                    && longitude_takes(i32::from_be_bytes([e, f, g, h]))
                    && height_takes(height, model)
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
    /// A UT Plus's, where it is not a VP's.
    default_ut_plus: &'static [u8],
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
        default_ut_plus: default,
        takes,
    }
}

impl Setting {
    /// The setting with a UT Plus's default in place of a VP's.
    const fn ut_plus(self, default: &'static [u8]) -> Self {
        assert!(self.default.len() == default.len());
        Self {
            default_ut_plus: default,
            ..self
        }
    }

    /// The value at power-up and after `@@Cf` on an Oncore of `model`.
    const fn default_of(&self, model: Model) -> &'static [u8] {
        match model {
            Model::VpSix | Model::VpEight => self.default,
            Model::UtPlus => self.default_ut_plus,
        }
    }
}

/// Every setting: pp. 9 to 69, and `@@Ta`, which is in no manual here
/// ([`TA_DCXO`]).
const SETTINGS: [Setting; 23] = [
    setting(AB_GMT_CORRECTION, &[QUERY; 3], &[0; 3], Takes::Anything),
    // A UT Plus's starts at the horizon (UT p. 6.21).
    setting(AG_MASK_ANGLE, &[QUERY], &[10], Takes::Bytes(89)).ut_plus(&[0]),
    setting(AH_SATELLITE_SELECT, &[QUERY], &[0], Takes::Bytes(2)),
    setting(
        AJ_XDOP_TYPE,
        &[QUERY],
        &[DOP_POSITION],
        Takes::Bytes(DOP_TIME),
    ),
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
        Takes::Position,
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
    setting(
        AB_APPLICATION_TYPE,
        &[QUERY],
        &[APPLICATION_LAND],
        Takes::Bytes(APPLICATION_STATIC),
    ),
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
/// `@@Av`'s mode that holds the altitude (p. 61).
const ALTITUDE_HOLD_ON: u8 = 1;
/// `@@AB`'s application types (p. 12).
const APPLICATION_AIR: u8 = 0;
const APPLICATION_HANDHELD: u8 = 1;
const APPLICATION_LAND: u8 = 2;
const APPLICATION_MARINE: u8 = 3;
const APPLICATION_STATIC: u8 = 4;
/// `@@Aj`'s DOP types (p. 34), which are `@@Bc`'s (p. 77).
const DOP_GEOMETRIC: u8 = 0;
const DOP_POSITION: u8 = 1;
const DOP_HORIZONTAL: u8 = 2;
const DOP_VERTICAL: u8 = 3;
const DOP_TIME: u8 = 4;

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

impl Place {
    /// A position a command gave, `separation` being the ellipsoid's
    /// height above mean sea level there.
    pub(super) const fn of(given: Location, separation: i32) -> Self {
        let height = given.height.height.0;
        let (ellipsoid, mean_sea_level) = match given.height.kind {
            HeightKind::MeanSeaLevel => (height.wrapping_add(separation), height),
            HeightKind::Ellipsoid => (height, height.wrapping_sub(separation)),
        };
        Self {
            latitude: given.latitude.0,
            longitude: given.longitude.0,
            ellipsoid,
            mean_sea_level,
        }
    }
}

/// Every setting's value, each as its command carries it, one after
/// another in the table's order.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(super) struct Store {
    /// The values.
    bytes: [u8; SETTINGS_BYTES],
    /// Bit `n` set if the table's setting `n` has been set by its
    /// command since power-up or `@@Cf`.
    told: u32,
}

const _: () = assert!(SETTINGS.len() <= u32::BITS as usize);

impl Store {
    /// A VP's values at power-up.
    pub(super) const DEFAULT: Self = Self::starting(Model::VpEight);

    /// The values at power-up and after `@@Cf` on an Oncore of `model`.
    pub(super) const fn starting(model: Model) -> Self {
        let mut bytes = [0; SETTINGS_BYTES];
        let mut index = 0;
        let mut at = 0;
        while index < SETTINGS.len() {
            let default = SETTINGS[index].default_of(model);
            let mut byte = 0;
            while byte < default.len() {
                bytes[at] = default[byte];
                at += 1;
                byte += 1;
            }
            index += 1;
        }
        Self { bytes, told: 0 }
    }

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
    /// value, is of the wrong length or is out of range, or is `@@As`
    /// while the position is held, which it may be set only while not
    /// (p. 53; UT p. 6.87).
    pub(super) fn set_or_query(&mut self, id: MessageId, data: &[u8], model: Model) {
        let Some(index) = Self::index(id) else {
            return;
        };
        let setting = &SETTINGS[index];
        if !(id == AS_HOLD_POSITION && self.holding())
            && setting.query != data
            && setting.default.len() == data.len()
            && setting.takes.takes(data, model)
            && let Some(held) = self.bytes.get_mut(start(index)..start(index) + data.len())
        {
            held.copy_from_slice(data);
            self.told |= 1 << index;
        }
    }

    /// Puts each setting no command has set to its default on an Oncore
    /// of `model`.
    pub(super) fn default_untold(&mut self, model: Model) {
        let starting = Self::starting(model);
        for index in (0..SETTINGS.len()).filter(|index| self.told & (1 << index) == 0) {
            let range = start(index)..start(index) + SETTINGS[index].default.len();
            self.bytes[range.clone()].copy_from_slice(&starting.bytes[range]);
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

    /// The satellites not to track (`@@Am`, p. 42).
    pub(super) fn ignore_list(&self) -> PrnSet {
        match self.get(AM_IGNORE_LIST) {
            Some(&[_, a, b, c, d]) => PrnSet::from_bits(u32::from_be_bytes([a, b, c, d])),
            _ => PrnSet::EMPTY,
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

    /// The kind of DOP asked for (`@@Aj`), as the command numbers it.
    pub(super) fn dop_type(&self) -> u8 {
        self.byte(AJ_XDOP_TYPE)
    }

    /// The kind of DOP asked for (`@@Aj`, p. 34).  The store takes no
    /// type but these five.
    pub(super) fn dop_kind(&self) -> DopKind {
        match self.dop_type() {
            DOP_GEOMETRIC => DopKind::Geometric,
            DOP_HORIZONTAL => DopKind::Horizontal,
            DOP_VERTICAL => DopKind::Vertical,
            DOP_TIME => DopKind::Time,
            _ => DopKind::Position,
        }
    }

    /// How the satellites to track are chosen (`@@Ah`, p. 30).
    pub(super) fn satellite_select(&self) -> SatelliteSelect {
        match self.byte(AH_SATELLITE_SELECT) {
            SELECT_MANUAL => SatelliteSelect::Manual,
            SELECT_HIGHEST_IN_SKY => SatelliteSelect::HighestInSky,
            _ => SatelliteSelect::BestGeometry,
        }
    }

    fn word(&self, id: MessageId) -> Option<u16> {
        match self.get(id) {
            Some(&[high, low]) => Some(u16::from_be_bytes([high, low])),
            _ => None,
        }
    }

    /// The DOP above which a 3D fix gives way to altitude hold, in
    /// tenths (`@@Al`, p. 39).
    pub(super) fn xdop_threshold(&self) -> u16 {
        self.word(AL_XDOP_THRESHOLD).unwrap_or(u16::MAX)
    }

    /// How much better another set's DOP must be (`@@Ak`, p. 38).
    pub(super) fn xdop_hysteresis(&self) -> Tenths {
        Tenths(self.word(AK_XDOP_HYSTERESIS).unwrap_or(0))
    }

    /// The HDOP above which a 2D fix is given up (`@@AC`, p. 16).
    pub(super) fn hdop_threshold(&self) -> Tenths {
        Tenths(self.word(AC_HDOP_THRESHOLD).unwrap_or(u16::MAX))
    }

    /// Where in the second the measurement epoch is put (`@@Ax`,
    /// p. 65).
    pub(super) fn epoch_offset(&self) -> Milliseconds<u16> {
        Milliseconds(self.word(AX_EPOCH_OFFSET).unwrap_or(0))
    }

    /// Whether a new almanac replaces the one kept (`@@An`, p. 44).
    pub(super) fn almanac_update(&self) -> bool {
        self.byte(AN_ALMANAC_UPDATE) == ALMANAC_UPDATE_ON
    }

    /// The models ranges are corrected by (`@@Aq`).
    pub(super) fn atmospheric_correction(&self) -> AtmosphericCorrection {
        let mode = self.byte(AQ_ATMOSPHERIC_CORRECTION);
        AtmosphericCorrection {
            ionospheric: mode & CORRECTION_IONOSPHERIC != 0,
            tropospheric: mode & CORRECTION_TROPOSPHERIC != 0,
        }
    }

    /// Which satellites the position is fixed by (`@@Ar`, p. 53).
    pub(super) fn fix_algorithm(&self) -> FixAlgorithm {
        if self.byte(AR_FIX_ALGORITHM) == ALGORITHM_BEST_FOUR {
            FixAlgorithm::BestFour
        } else {
            FixAlgorithm::AllInView
        }
    }

    /// How long a channel's code phase is filtered before use (`@@AD`,
    /// p. 20).
    pub(super) fn correction_thresholds(&self) -> CorrectionThresholds {
        match self.get(AD_CORRECTION_THRESHOLDS) {
            Some(&[fix, output]) => CorrectionThresholds {
                fix: Seconds(fix),
                output: Seconds(output),
            },
            _ => CorrectionThresholds {
                fix: Seconds(0),
                output: Seconds(0),
            },
        }
    }

    /// The datum (`@@Ao`).
    pub(super) fn datum(&self) -> u8 {
        self.byte(AO_DATUM_SELECT)
    }

    /// The position to hold, as `@@As` gave it.
    pub(super) fn hold_position(&self) -> Location {
        let Some(&[a, b, c, d, e, f, g, h, ref height @ ..]) = self.get(AS_HOLD_POSITION) else {
            return Location::default();
        };
        Location {
            latitude: MilliArcSeconds(i32::from_be_bytes([a, b, c, d])),
            longitude: MilliArcSeconds(i32::from_be_bytes([e, f, g, h])),
            height: height_of(height).unwrap_or_default(),
        }
    }

    /// The position to hold (`@@As`), `separation` being the ellipsoid's
    /// height above mean sea level there.
    pub(super) fn held_position(&self, separation: i32) -> Place {
        Place::of(self.hold_position(), separation)
    }

    /// Whether the altitude is held by hand (`@@Av`).
    pub(super) fn altitude_hold(&self) -> bool {
        self.byte(AV_ALTITUDE_HOLD) == ALTITUDE_HOLD_ON
    }

    /// The height to hold the altitude at (`@@Au`).
    pub(super) fn altitude_hold_height(&self) -> Height {
        self.get(AU_ALTITUDE_HOLD_HEIGHT)
            .and_then(height_of)
            .unwrap_or_default()
    }

    fn nanoseconds(&self, id: MessageId) -> Nanoseconds<u32> {
        match self.get(id) {
            Some(&[a, b, c, d]) => Nanoseconds(u32::from_be_bytes([a, b, c, d])),
            _ => Nanoseconds(0),
        }
    }

    /// How much earlier the 1 PPS is put out (`@@Az`).
    pub(super) fn cable_delay(&self) -> Nanoseconds<u32> {
        self.nanoseconds(AZ_CABLE_DELAY)
    }

    /// How far after the second the 1 PPS is put out (`@@Ay`).
    pub(super) fn pulse_offset(&self) -> Nanoseconds<u32> {
        self.nanoseconds(AY_PULSE_OFFSET)
    }

    /// What the engine is used for (`@@AB`).  The store takes no type
    /// but these five.
    pub(super) fn application(&self) -> Application {
        match self.byte(AB_APPLICATION_TYPE) {
            APPLICATION_AIR => Application::Air,
            APPLICATION_HANDHELD => Application::Handheld,
            APPLICATION_MARINE => Application::Marine,
            APPLICATION_STATIC => Application::Static,
            _ => Application::Land,
        }
    }
}

/// A kind of DOP as `@@Aj` and `@@Bc` number it (pp. 34 and 77).
pub(super) const fn dop_kind_code(kind: DopKind) -> u8 {
    match kind {
        DopKind::Geometric => DOP_GEOMETRIC,
        DopKind::Position => DOP_POSITION,
        DopKind::Horizontal => DOP_HORIZONTAL,
        DopKind::Vertical => DOP_VERTICAL,
        DopKind::Time => DOP_TIME,
    }
}

/// Whether `id` is a command that sets a value.
pub(super) fn is_setting(id: MessageId) -> bool {
    Store::index(id).is_some()
}
