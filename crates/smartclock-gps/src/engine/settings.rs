//! What commands have set an engine to, as types: what a caller reads
//! to do as the receiver told the engine without decoding the commands
//! a second time.
//!
//! [`Engine::settings`](crate::engine::Engine::settings) gives the
//! [`Settings`] as they stand, and [`Engine::receive`] says which of
//! them the commands it was given changed
//! ([`Event`](crate::engine::event::Event)).  What a command changes in
//! the bytes the engine builds, the engine does; what it would change
//! in a receiver's tracking, fix or pulse, the engine can only hold
//! here, for its holder to do.  A setting whose
//! command an Oncore does not take, as a UT Plus takes no `@@AB`, stays
//! as it is at power-up.
//!
//! The pages cited are the VP Oncore's command reference,
//! `third_party/motorola/VPCommands.pdf`, as printed.
//!
//! [`Engine::receive`]: crate::engine::Engine::receive

use core::fmt::Debug;
use core::fmt::Formatter;
use core::fmt::Result as FmtResult;

use crate::units::Centimeters;
use crate::units::Degrees;
use crate::units::MilliArcSeconds;
use crate::units::Milliseconds;
use crate::units::Nanoseconds;
use crate::units::Seconds;
use crate::units::Tenths;

/// The channels the most an engine has, each of which `@@Ai` can put a
/// satellite on (p. 32).
pub const CHANNELS_MOST: usize = 8;

/// What a height is measured from (`@@Af`'s height type, p. 26).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Default)]
pub enum HeightKind {
    /// The GPS reference ellipsoid.
    #[default]
    Ellipsoid,
    /// Mean sea level.
    MeanSeaLevel,
}

/// A height as a command gives one: one figure and what it is measured
/// from.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Default)]
pub struct Height {
    /// The height.
    pub height: Centimeters,
    /// What it is above.
    pub kind: HeightKind,
}

/// A position as commands give one (`@@As`, p. 55).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Default)]
pub struct Location {
    /// North positive.
    pub latitude: MilliArcSeconds,
    /// East positive.
    pub longitude: MilliArcSeconds,
    /// The height.
    pub height: Height,
}

/// The time an engine reports and puts its 1 PPS on (`@@Aw`, pp. 62 and
/// 63).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum TimeBase {
    /// GPS time.
    Gps,
    /// UTC.
    Utc,
}

/// When an engine puts out its 1 PPS: the 1PPS control mode of `@@Bn`
/// and `@@En` (pp. 102, 104 and 140).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum PulseMode {
    /// Never.
    Off,
    /// All the time.
    Always,
    /// Only while it tracks at least one satellite.
    Tracking,
    /// Only while Time RAIM confirms the time solution's error is
    /// within the alarm limit.
    Confirmed,
}

/// What an engine is used for, which sets how it searches for
/// satellites (`@@AB`, pp. 11 and 12).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum Application {
    /// In an aircraft.
    Air,
    /// In the hand.
    Handheld,
    /// On land.
    Land,
    /// On the water.
    Marine,
    /// Not moving.
    Static,
}

/// How an engine chooses the satellites it tracks (`@@Ah`, pp. 29 and
/// 30).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum SatelliteSelect {
    /// Those of the best geometry.
    BestGeometry,
    /// Those `@@Ai` puts on its channels
    /// ([`Settings::manual_satellites`]).
    Manual,
    /// Those highest above the horizon.
    HighestInSky,
}

/// Which satellites an engine fixes its position by (`@@Ar`, pp. 52 and
/// 53).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum FixAlgorithm {
    /// The best four of those it tracks.
    BestFour,
    /// All it tracks.
    AllInView,
}

/// The models an engine corrects ranges by (`@@Aq`, pp. 50 and 51; UT
/// p. 6.28).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct AtmosphericCorrection {
    /// The ionospheric model.
    pub ionospheric: bool,
    /// The tropospheric model, a UT Plus's alone to set: a VP's command
    /// has no value for it, and this is false on one.
    pub tropospheric: bool,
}

/// How long an engine filters a channel's code phase before it uses it
/// (`@@AD`, pp. 19 and 20).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct CorrectionThresholds {
    /// Before the channel is used in a fix, `FIX_THR`.
    pub fix: Seconds<u8>,
    /// Before its differential correction is put out, `OUT_THR`.
    pub output: Seconds<u8>,
}

/// What an engine's port speaks (`@@Ci`, pp. 120 and 121).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum OutputFormat {
    /// Motorola binary, which is all an [`Engine`](crate::engine::Engine)
    /// speaks.
    Binary,
    /// NMEA 0183, at 4800 baud.
    Nmea,
    /// Loran emulation, at 1200 baud.
    Loran,
}

/// What commands have set an engine to.  See the [module](self).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[non_exhaustive]
pub struct Settings {
    /// The elevation a satellite must be above to be tracked (`@@Ag`,
    /// pp. 27 and 28).
    pub mask_angle: Degrees<u8>,
    /// Whether the position is held (`@@At`, pp. 56 and 57).
    pub position_hold: bool,
    /// The position to hold, held or not (`@@As`, pp. 54 and 55).
    pub held_position: Location,
    /// Whether the altitude is held by hand (`@@Av`, pp. 60 and 61).
    pub altitude_hold: bool,
    /// The height to hold it at, held or not (`@@Au`, pp. 58 and 59).
    pub altitude_hold_height: Height,
    /// Which time is reported (`@@Aw`).
    pub time_base: TimeBase,
    /// How much earlier the 1 PPS is put out, to make up for the
    /// antenna cable (`@@Az`, pp. 68 and 69).  The manual's most is
    /// 999,999 ns; the engine keeps whatever it is given.
    pub cable_delay: Nanoseconds<u32>,
    /// How far after the second the 1 PPS is put out (`@@Ay`, pp. 66
    /// and 67): 0 to 999,999,999 ns, never negative.
    pub pulse_offset: Nanoseconds<u32>,
    /// When the 1 PPS is put out (`@@Bn` and `@@En`).
    pub pulse_mode: PulseMode,
    /// What the engine is used for (`@@AB`).
    pub application: Application,
    /// Where to start looking from: the latitude, longitude and height
    /// last given (`@@Ad`, `@@Ae` and `@@Af`, pp. 17 to 26), each as it
    /// was sent, in range or not.  The engine reports it while the
    /// record has no position.
    pub initial_position: Location,
    /// How the satellites to track are chosen (`@@Ah`).
    pub satellite_select: SatelliteSelect,
    /// The satellite `@@Ai` put on each channel, the first channel
    /// first (pp. 31 and 32): its ID as it was sent, in range or not,
    /// and zero for none or a channel turned off.  They count only
    /// while [`Settings::satellite_select`] is
    /// [`Manual`](SatelliteSelect::Manual).
    pub manual_satellites: [u8; CHANNELS_MOST],
    /// How much better another set of satellites' DOP must be before
    /// the engine changes to it (`@@Ak`, pp. 37 and 38).
    pub xdop_hysteresis: Tenths,
    /// Whether a new almanac the satellites send replaces the one kept
    /// (`@@An`, pp. 43 and 44).
    pub almanac_update: bool,
    /// The models ranges are corrected by (`@@Aq`).
    pub atmospheric_correction: AtmosphericCorrection,
    /// Which satellites the position is fixed by (`@@Ar`).
    pub fix_algorithm: FixAlgorithm,
    /// Where in the second the measurement epoch is put (`@@Ax`, pp. 64
    /// and 65): 0 to 999 ms.
    pub epoch_offset: Milliseconds<u16>,
    /// The HDOP above which a 2D fix is given up (`@@AC`, pp. 15 and
    /// 16).
    pub hdop_threshold: Tenths,
    /// How long a channel's code phase is filtered before use (`@@AD`).
    pub correction_thresholds: CorrectionThresholds,
    /// What the port was last told to speak (`@@Ci`).  The engine
    /// speaks binary whatever this is: another format is its holder's
    /// to speak, or to pass over.
    pub output_format: OutputFormat,
}

/// One of the [`Settings`].
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
#[non_exhaustive]
pub enum Setting {
    /// [`Settings::mask_angle`].
    MaskAngle,
    /// [`Settings::position_hold`].
    PositionHold,
    /// [`Settings::held_position`].
    HeldPosition,
    /// [`Settings::altitude_hold`].
    AltitudeHold,
    /// [`Settings::altitude_hold_height`].
    AltitudeHoldHeight,
    /// [`Settings::time_base`].
    TimeBase,
    /// [`Settings::cable_delay`].
    CableDelay,
    /// [`Settings::pulse_offset`].
    PulseOffset,
    /// [`Settings::pulse_mode`].
    PulseMode,
    /// [`Settings::application`].
    Application,
    /// [`Settings::initial_position`].
    InitialPosition,
    /// [`Settings::satellite_select`].
    SatelliteSelect,
    /// [`Settings::manual_satellites`].
    ManualSatellites,
    /// [`Settings::xdop_hysteresis`].
    XdopHysteresis,
    /// [`Settings::almanac_update`].
    AlmanacUpdate,
    /// [`Settings::atmospheric_correction`].
    AtmosphericCorrection,
    /// [`Settings::fix_algorithm`].
    FixAlgorithm,
    /// [`Settings::epoch_offset`].
    EpochOffset,
    /// [`Settings::hdop_threshold`].
    HdopThreshold,
    /// [`Settings::correction_thresholds`].
    CorrectionThresholds,
    /// [`Settings::output_format`].
    OutputFormat,
}

impl Setting {
    /// Every setting, in the order of [`Settings`]' fields.
    pub const ALL: [Self; 21] = [
        Self::MaskAngle,
        Self::PositionHold,
        Self::HeldPosition,
        Self::AltitudeHold,
        Self::AltitudeHoldHeight,
        Self::TimeBase,
        Self::CableDelay,
        Self::PulseOffset,
        Self::PulseMode,
        Self::Application,
        Self::InitialPosition,
        Self::SatelliteSelect,
        Self::ManualSatellites,
        Self::XdopHysteresis,
        Self::AlmanacUpdate,
        Self::AtmosphericCorrection,
        Self::FixAlgorithm,
        Self::EpochOffset,
        Self::HdopThreshold,
        Self::CorrectionThresholds,
        Self::OutputFormat,
    ];

    /// The name of its field of [`Settings`], which is what the tables
    /// under `docs/firmware` call it.
    #[must_use]
    pub const fn name(self) -> &'static str {
        match self {
            Self::MaskAngle => "mask_angle",
            Self::PositionHold => "position_hold",
            Self::HeldPosition => "held_position",
            Self::AltitudeHold => "altitude_hold",
            Self::AltitudeHoldHeight => "altitude_hold_height",
            Self::TimeBase => "time_base",
            Self::CableDelay => "cable_delay",
            Self::PulseOffset => "pulse_offset",
            Self::PulseMode => "pulse_mode",
            Self::Application => "application",
            Self::InitialPosition => "initial_position",
            Self::SatelliteSelect => "satellite_select",
            Self::ManualSatellites => "manual_satellites",
            Self::XdopHysteresis => "xdop_hysteresis",
            Self::AlmanacUpdate => "almanac_update",
            Self::AtmosphericCorrection => "atmospheric_correction",
            Self::FixAlgorithm => "fix_algorithm",
            Self::EpochOffset => "epoch_offset",
            Self::HdopThreshold => "hdop_threshold",
            Self::CorrectionThresholds => "correction_thresholds",
            Self::OutputFormat => "output_format",
        }
    }

    /// The setting `name` names.
    #[must_use]
    pub fn of_name(name: &str) -> Option<Self> {
        Self::ALL.into_iter().find(|setting| setting.name() == name)
    }

    /// Its bit in a [`SettingSet`].
    const fn bit(self) -> u32 {
        1 << self as u32
    }
}

/// A set of [`Setting`]s.
#[derive(Clone, Copy, PartialEq, Eq, Hash, Default)]
pub struct SettingSet(u32);

impl Debug for SettingSet {
    fn fmt(&self, formatter: &mut Formatter<'_>) -> FmtResult {
        formatter.debug_set().entries(self.iter()).finish()
    }
}

impl SettingSet {
    /// No setting.
    pub const EMPTY: Self = Self(0);

    /// The set of `settings`.
    #[must_use]
    pub const fn of(settings: &[Setting]) -> Self {
        let mut set = Self::EMPTY;
        let mut index = 0;
        while index < settings.len() {
            set = set.with(settings[index]);
            index += 1;
        }
        set
    }

    /// The set with `setting` in it.
    #[must_use]
    pub const fn with(self, setting: Setting) -> Self {
        Self(self.0 | setting.bit())
    }

    /// The set with all of `other` in it too.
    pub(crate) const fn union(self, other: Self) -> Self {
        Self(self.0 | other.0)
    }

    /// The set with none of `other` in it.
    #[must_use]
    pub const fn without(self, other: Self) -> Self {
        Self(self.0 & !other.0)
    }

    /// Those of it that are in `other` too.
    pub(crate) const fn common(self, other: Self) -> Self {
        Self(self.0 & other.0)
    }

    /// The settings in it, in the order of [`Setting::ALL`].
    pub fn iter(self) -> impl Iterator<Item = Setting> {
        Setting::ALL
            .into_iter()
            .filter(move |setting| self.contains(*setting))
    }

    /// Whether `setting` is in it.
    #[must_use]
    pub const fn contains(self, setting: Setting) -> bool {
        self.0 & setting.bit() != 0
    }

    /// Whether it holds none.
    #[must_use]
    pub const fn is_empty(self) -> bool {
        self.0 == 0
    }
}

impl Settings {
    /// The settings that are not in `after` what they are here.
    pub(super) fn changed(&self, after: &Self) -> SettingSet {
        [
            (self.mask_angle != after.mask_angle, Setting::MaskAngle),
            (
                self.position_hold != after.position_hold,
                Setting::PositionHold,
            ),
            (
                self.held_position != after.held_position,
                Setting::HeldPosition,
            ),
            (
                self.altitude_hold != after.altitude_hold,
                Setting::AltitudeHold,
            ),
            (
                self.altitude_hold_height != after.altitude_hold_height,
                Setting::AltitudeHoldHeight,
            ),
            (self.time_base != after.time_base, Setting::TimeBase),
            (self.cable_delay != after.cable_delay, Setting::CableDelay),
            (
                self.pulse_offset != after.pulse_offset,
                Setting::PulseOffset,
            ),
            (self.pulse_mode != after.pulse_mode, Setting::PulseMode),
            (self.application != after.application, Setting::Application),
            (
                self.initial_position != after.initial_position,
                Setting::InitialPosition,
            ),
            (
                self.satellite_select != after.satellite_select,
                Setting::SatelliteSelect,
            ),
            (
                self.manual_satellites != after.manual_satellites,
                Setting::ManualSatellites,
            ),
            (
                self.xdop_hysteresis != after.xdop_hysteresis,
                Setting::XdopHysteresis,
            ),
            (
                self.almanac_update != after.almanac_update,
                Setting::AlmanacUpdate,
            ),
            (
                self.atmospheric_correction != after.atmospheric_correction,
                Setting::AtmosphericCorrection,
            ),
            (
                self.fix_algorithm != after.fix_algorithm,
                Setting::FixAlgorithm,
            ),
            (
                self.epoch_offset != after.epoch_offset,
                Setting::EpochOffset,
            ),
            (
                self.hdop_threshold != after.hdop_threshold,
                Setting::HdopThreshold,
            ),
            (
                self.correction_thresholds != after.correction_thresholds,
                Setting::CorrectionThresholds,
            ),
            (
                self.output_format != after.output_format,
                Setting::OutputFormat,
            ),
        ]
        .into_iter()
        .filter(|(differs, _)| *differs)
        .fold(SettingSet::EMPTY, |set, (_, setting)| set.with(setting))
    }
}
