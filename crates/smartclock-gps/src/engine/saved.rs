//! What an engine keeps through a power cycle, and so does again at
//! power-up without being told.
//!
//! A VP keeps the rate of its position status in nonvolatile memory:
//! one sending it continuously when turned off sends it again when
//! power comes back (`@@Ba`, p. 70; `@@Ea`, p. 124).  Its Time RAIM
//! setup and rate it keeps so while it has backup power (`@@Bn`,
//! p. 102; `@@En`, p. 138).  So a receiver that configured its engine
//! once can find it configured at the next power-up.  A
//! [`SavedConfiguration`] is that configuration, given to an
//! [`Engine`](crate::engine::Engine) through
//! [`Options::saved`](crate::engine::Options::saved).
//!
//! `@@Cf` "sets all of the GPS receiver parameters to their default
//! values", every continuous message to polled only (p. 116), and a
//! VP to idle (p. 118), whatever it was saved as: so the engine's
//! `@@Cf` puts it back to its defaults, not to this.  Only a new
//! engine, a power-up, starts from it again.  Whether a real Oncore
//! told `@@Cf` would still come up configured at its next power-up is
//! its holder's to model, by the options it makes the next engine
//! with.
//!
//! One configuration is built in, [`CONFIGURATION_58503A`]: how a
//! 58503A leaves its engine, for a receiver that never configures its
//! own.
//!
//! The pages cited are the VP Oncore's command reference,
//! `third_party/motorola/VPCommands.pdf`, as printed.

use thiserror::Error;

use crate::engine::ALARM_LIMIT_LEAST;
use crate::engine::identity::Model;
use crate::engine::settings::PulseMode;
use crate::units::HundredNanoseconds;
use crate::units::Seconds;

/// How many receiver channels an engine has, which picks the form of
/// the messages that come in a six-channel and an eight-channel form:
/// `@@Ba` or `@@Ea`, `@@Bn` or `@@En` (pp. 70, 102, 124 and 138).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum ChannelCount {
    /// Six, as a six-channel VP.
    Six,
    /// Eight, as an eight-channel VP or a UT Plus.
    Eight,
}

impl ChannelCount {
    /// The channels a model has.
    #[must_use]
    pub const fn of(model: Model) -> Self {
        match model {
            Model::VpSix => Self::Six,
            Model::VpEight | Model::UtPlus => Self::Eight,
        }
    }

    /// The count.
    #[must_use]
    pub const fn count(self) -> usize {
        match self {
            Self::Six => 6,
            Self::Eight => 8,
        }
    }
}

/// What an engine powers up configured to.  See the [module](self).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct SavedConfiguration {
    /// The channels it has from power-up, which must be those of the
    /// identity the engine is made with: with eight, an eight-channel
    /// identity.  The rates below are of the messages of this form.
    pub channels: ChannelCount,
    /// Whether it fixes from power-up, not idle (`@@Cg`, p. 118).  A UT
    /// Plus has no idle mode, so a configuration for one fixes.
    pub fixing: bool,
    /// How often it sends its position status, `@@Ba` or `@@Ea` by
    /// [`SavedConfiguration::channels`], unasked (pp. 70 and 124): zero
    /// for only when polled.
    pub position_status_rate: Seconds<u8>,
    /// Whether it sends `@@Bb` each time its satellite alert
    /// computation finds the visibility changed, `@@Bb`'s mode 1, not
    /// only when polled (pp. 74 and 75).  The manual does not say
    /// whether a VP keeps this mode through a power cycle.
    pub visibility_updates: bool,
    /// How often it sends Time RAIM's setup and status, `@@Bn` or
    /// `@@En` by [`SavedConfiguration::channels`], unasked (pp. 102,
    /// 104, 138 and 140): zero for only when polled.
    pub time_raim_rate: Seconds<u8>,
    /// Whether the Time RAIM algorithm is on (pp. 104 and 140), as
    /// [`Settings::raim_algorithm`](crate::engine::settings::Settings::raim_algorithm).
    pub raim_algorithm: bool,
    /// Time RAIM's alarm limit, 3 to 65,535 hundreds of nanoseconds
    /// (pp. 104 and 140), as
    /// [`Settings::raim_alarm_limit`](crate::engine::settings::Settings::raim_alarm_limit).
    pub raim_alarm_limit: HundredNanoseconds,
    /// When the 1 PPS is put out, the 1PPS control mode (pp. 104 and
    /// 140), as
    /// [`Settings::pulse_mode`](crate::engine::settings::Settings::pulse_mode).
    pub pulse_mode: PulseMode,
}

/// The configuration a 58503A leaves its eight-channel engine in.  Its
/// power-up script (3704's `0x53`, `docs/firmware/oncore/58503a-3704.txt`)
/// asks for `@@Ea` every second (p. 124) and `@@Bb` at each change
/// (p. 75) and ends fixing (`@@Cg` 1, p. 118); its Time RAIM request
/// asks for `@@En` every second with the 1 PPS on all the time (p. 140)
/// and Time RAIM as its settings have it (`docs/firmware/gps.md`, "Time
/// RAIM"), here at their defaults in position hold: on, at an alarm
/// limit of 1000 ns (`docs/firmware/structures.md`, `raim_state` and
/// `raim_threshold`).  So it is how an engine a 58503A configured
/// comes up, and it is offered for a receiver that never configures
/// its engine, as the Z3817A, which sends its engine nothing.  An
/// engine made with it needs an eight-channel identity.
pub const CONFIGURATION_58503A: SavedConfiguration = SavedConfiguration {
    channels: ChannelCount::Eight,
    fixing: true,
    position_status_rate: Seconds(1),
    visibility_updates: true,
    time_raim_rate: Seconds(1),
    raim_algorithm: true,
    raim_alarm_limit: HundredNanoseconds(10),
    pulse_mode: PulseMode::Always,
};

/// Why an engine cannot power up with a [`SavedConfiguration`].
#[derive(Debug, Clone, Copy, PartialEq, Eq, Error)]
pub enum ConfigurationError {
    /// The configuration's channels are not the identity's.
    #[error(
        "the saved configuration has {} channels, the identity's Oncore {}",
        .saved.count(),
        .identity.count()
    )]
    Channels {
        /// The configuration's.
        saved: ChannelCount,
        /// The identity's.
        identity: ChannelCount,
    },
    /// An idle configuration for a UT Plus, which has no idle mode
    /// (p. 118).
    #[error("a UT Plus has no idle mode to power up in")]
    Idle,
    /// A Time RAIM alarm limit below the least, 3 (pp. 104 and 140).
    #[error("a Time RAIM alarm limit of {} hundred ns is below the least, {}", .limit.0, ALARM_LIMIT_LEAST.0)]
    AlarmLimit {
        /// The configuration's.
        limit: HundredNanoseconds,
    },
}

impl SavedConfiguration {
    /// Whether an engine of `model` can power up with it.
    pub(super) const fn check(&self, model: Model) -> Result<(), ConfigurationError> {
        let identity = ChannelCount::of(model);
        if !matches!(
            (self.channels, identity),
            (ChannelCount::Six, ChannelCount::Six) | (ChannelCount::Eight, ChannelCount::Eight)
        ) {
            return Err(ConfigurationError::Channels {
                saved: self.channels,
                identity,
            });
        }
        if !self.fixing && matches!(model, Model::UtPlus) {
            return Err(ConfigurationError::Idle);
        }
        if self.raim_alarm_limit.0 < ALARM_LIMIT_LEAST.0 {
            return Err(ConfigurationError::AlarmLimit {
                limit: self.raim_alarm_limit,
            });
        }
        Ok(())
    }
}
