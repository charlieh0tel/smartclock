//! What an engine keeps through a power cycle, and so does again at
//! power-up without being told.
//!
//! A VP keeps the rate of its position status in nonvolatile memory:
//! one sending it continuously when turned off sends it again when
//! power comes back (`@@Ba`, p. 70; `@@Ea`, p. 124).  Its Time RAIM
//! setup and rate the manual has it keep while it has backup power
//! (`@@Bn`, p. 102; `@@En`, p. 138).  Tests of a real VP showed more:
//! one configured (its settings, a held position, `@@Ea` and `@@En`
//! each second with Time RAIM on, `@@Bb` at each change, `@@Cg` 1) and
//! power-cycled with no backup power came back up exactly as left,
//! fixing, its outputs running unasked, Time RAIM on at a limit of 10,
//! the pulse mode as set and all 20 `@@A` settings queried as saved;
//! only its clock was lost.  So a receiver that configured its engine
//! once finds it configured at the next power-up, whether it fixes,
//! its rates and Time RAIM included.  A [`SavedConfiguration`] is that
//! configuration, given to an [`Engine`](crate::engine::Engine) through
//! [`Options::saved`](crate::engine::Options::saved).
//!
//! `@@Cf` "sets all of the GPS receiver parameters to their default
//! values", every continuous message to polled only (p. 116), and a
//! VP to idle (p. 118), whatever it was saved as: so the engine's
//! `@@Cf` puts it back to its defaults, not to this.  It is what the
//! VP saves, too: the same tests found one told `@@Cf` and
//! power-cycled with no backup power idle and silent, its position
//! status's time at 1997-01-01 12:00:00 and standing, its almanac
//! dropped and its settings at `@@Cf`'s values.  So a holder modeling that power cycle
//! makes the next engine with no saved configuration.
//!
//! A caller makes its own from
//! [`SavedConfiguration::starting`], which is how an engine powers up
//! with nothing saved, changed by its `with_` methods, and checks it
//! for an Oncore with [`SavedConfiguration::check`], as
//! [`Engine::new`](crate::engine::Engine::new) does.  One configuration
//! is built in, [`CONFIGURATION_58503A`]: how a 58503A leaves its
//! engine, for a receiver that cannot command its own.
//!
//! The pages cited are the VP Oncore's command reference,
//! `third_party/motorola/VPCommands.pdf`, as printed.

use thiserror::Error;

use crate::engine::ALARM_LIMIT_LEAST;
use crate::engine::TraimSetup;
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
    /// whether a VP keeps this mode through a power cycle; tests of a
    /// real VP showed it does, with no backup power (the [module](self)).
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
/// comes up, and it is offered for a receiver that cannot command its
/// engine, as the Z3817A, whose firmware (3752) refuses every engine
/// request internally and so sends its engine nothing
/// (`docs/firmware/gps.md`, "Which engines each image accepts").  An
/// engine made with it needs an eight-channel identity.
pub const CONFIGURATION_58503A: SavedConfiguration = SavedConfiguration::starting(Model::VpEight)
    .with_fixing(true)
    .with_position_status_rate(Seconds(1))
    .with_visibility_updates(true)
    .with_time_raim_rate(Seconds(1))
    .with_raim_algorithm(true)
    .with_raim_alarm_limit(HundredNanoseconds(10))
    .with_pulse_mode(PulseMode::Always);

const _: () = assert!(CONFIGURATION_58503A.check(Model::VpEight).is_ok());

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
    /// How an engine of `model` powers up with nothing saved: with its
    /// channels, idle if a VP and fixing if a UT Plus (p. 118), every
    /// message polled only, Time RAIM off at an alarm limit of 1000 ns
    /// and the 1 PPS on all the time (pp. 103 and 139).  An engine made
    /// with it starts as one made with none; the `with_` methods make
    /// a configuration of it.
    #[must_use]
    pub const fn starting(model: Model) -> Self {
        let traim = TraimSetup::DEFAULT;
        Self {
            channels: ChannelCount::of(model),
            fixing: matches!(model, Model::UtPlus),
            position_status_rate: Seconds(0),
            visibility_updates: false,
            time_raim_rate: Seconds(0),
            raim_algorithm: traim.algorithm,
            raim_alarm_limit: traim.alarm_limit,
            pulse_mode: traim.pulse_mode,
        }
    }

    /// The same, with [`SavedConfiguration::fixing`] as given.
    #[must_use]
    pub const fn with_fixing(self, fixing: bool) -> Self {
        Self { fixing, ..self }
    }

    /// The same, with [`SavedConfiguration::position_status_rate`] as
    /// given.
    #[must_use]
    pub const fn with_position_status_rate(self, rate: Seconds<u8>) -> Self {
        Self {
            position_status_rate: rate,
            ..self
        }
    }

    /// The same, with [`SavedConfiguration::visibility_updates`] as
    /// given.
    #[must_use]
    pub const fn with_visibility_updates(self, updates: bool) -> Self {
        Self {
            visibility_updates: updates,
            ..self
        }
    }

    /// The same, with [`SavedConfiguration::time_raim_rate`] as given.
    #[must_use]
    pub const fn with_time_raim_rate(self, rate: Seconds<u8>) -> Self {
        Self {
            time_raim_rate: rate,
            ..self
        }
    }

    /// The same, with [`SavedConfiguration::raim_algorithm`] as given.
    #[must_use]
    pub const fn with_raim_algorithm(self, on: bool) -> Self {
        Self {
            raim_algorithm: on,
            ..self
        }
    }

    /// The same, with [`SavedConfiguration::raim_alarm_limit`] as
    /// given.
    #[must_use]
    pub const fn with_raim_alarm_limit(self, limit: HundredNanoseconds) -> Self {
        Self {
            raim_alarm_limit: limit,
            ..self
        }
    }

    /// The same, with [`SavedConfiguration::pulse_mode`] as given.
    #[must_use]
    pub const fn with_pulse_mode(self, mode: PulseMode) -> Self {
        Self {
            pulse_mode: mode,
            ..self
        }
    }

    /// Whether an engine of `model` can power up with it, as
    /// [`Engine::new`](crate::engine::Engine::new) checks: its channels
    /// must be the model's, a UT Plus's must fix, and its Time RAIM
    /// alarm limit must be no less than 3 (pp. 104 and 140).
    ///
    /// # Errors
    ///
    /// The [`ConfigurationError`] [`Engine::new`](crate::engine::Engine::new)
    /// would return.
    pub const fn check(&self, model: Model) -> Result<(), ConfigurationError> {
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
