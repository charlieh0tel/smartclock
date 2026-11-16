//! What an engine keeps through a power cycle, and so does again at
//! power-up without being told.
//!
//! A VP keeps the rate of its position status in nonvolatile memory:
//! one sending it continuously when turned off sends it again when
//! power comes back (`@@Ba`, VP p. 70; `@@Ea`, VP p. 124).  So a
//! receiver that configured its engine once can find it configured at
//! the next power-up.  A [`SavedConfiguration`] is that configuration,
//! given to an [`Engine`](crate::engine::Engine) through
//! [`Options::saved`](crate::engine::Options::saved).  `@@Cf` puts the
//! engine back to its defaults, not to this (VP p. 116); only a new
//! engine, a power-up, starts from it again.
//!
//! The pages cited are the VP Oncore's command reference,
//! `third_party/motorola/VPCommands.pdf`, as printed.

use thiserror::Error;

use crate::engine::identity::Model;
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
}

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
        Ok(())
    }
}
