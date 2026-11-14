//! What commands have set an engine to, as types: what a caller reads
//! to do as the receiver told the engine without decoding the commands
//! a second time.
//!
//! [`Engine::settings`](crate::engine::Engine::settings) gives the
//! [`Settings`] as they stand, and [`Engine::receive`] says which of
//! them the commands it was given changed
//! ([`Event`](crate::engine::event::Event)).  A setting is a value the
//! engine holds, no more: what the engine itself sends differs by a
//! setting only as the [module](crate::engine) says.  A setting whose
//! command an Oncore does not take, as a UT Plus takes no `@@AB`, stays
//! as it is at power-up.
//!
//! The pages cited are the VP Oncore's command reference,
//! `third_party/motorola/VPCommands.pdf`, as printed.
//!
//! [`Engine::receive`]: crate::engine::Engine::receive

use crate::units::Centimeters;
use crate::units::Degrees;
use crate::units::MilliArcSeconds;
use crate::units::Nanoseconds;

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
    /// was sent, in range or not.  The engine reports no position by
    /// it.
    pub initial_position: Location,
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
}

impl Setting {
    /// Its bit in a [`SettingSet`].
    const fn bit(self) -> u16 {
        1 << self as u16
    }
}

/// A set of [`Setting`]s.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Default)]
pub struct SettingSet(u16);

impl SettingSet {
    /// No setting.
    pub const EMPTY: Self = Self(0);

    /// The set with `setting` in it.
    #[must_use]
    pub const fn with(self, setting: Setting) -> Self {
        Self(self.0 | setting.bit())
    }

    /// The set with all of `other` in it too.
    pub(super) const fn union(self, other: Self) -> Self {
        Self(self.0 | other.0)
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
        ]
        .into_iter()
        .filter(|(differs, _)| *differs)
        .fold(SettingSet::EMPTY, |set, (_, setting)| set.with(setting))
    }
}
