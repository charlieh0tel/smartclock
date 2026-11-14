//! What the commands an engine was given did that its holder may have
//! to act on: [`Engine::receive`](crate::engine::Engine::receive)
//! returns them, as [`Events`].
//!
//! The page cited is the VP Oncore's command reference's,
//! `third_party/motorola/VPCommands.pdf`, as printed.

use crate::engine::settings::SettingSet;

/// One thing commands did.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[non_exhaustive]
pub enum Event {
    /// The engine was set to its defaults: it took `@@Cf` (pp. 116 and
    /// 117), which "sets all of the GPS receiver parameters to their
    /// default values", makes every continuous message polled only and
    /// leaves the time and date.  An Oncore clears its almanac and
    /// ephemeris for it too; the engine holds neither.
    Reset,
    /// Commands changed these settings.  A command that sets a setting
    /// to what it is, or asks for it, changes none.
    SettingsChanged(SettingSet),
}

/// What the commands in the bytes of one
/// [`Engine::receive`](crate::engine::Engine::receive) did, as an
/// iterator of each [`Event`]: a [`Reset`](Event::Reset) if any command
/// was one, then a [`SettingsChanged`](Event::SettingsChanged) if any
/// changed a setting.
///
/// The settings named are every one a command changed, a reset among
/// them, in whatever order the commands came.  So a caller that keeps
/// something else as the engine is set takes the events in the order
/// given: it sets that to its defaults for a reset, then sets it to
/// each setting named, read from
/// [`Engine::settings`](crate::engine::Engine::settings) after the
/// call.  A setting changed and changed back within the bytes is named
/// though it reads as it did.
#[derive(Debug, Clone, PartialEq, Eq, Default)]
pub struct Events {
    /// Whether a command set the engine to its defaults, not yet given
    /// out.
    reset: bool,
    /// The settings commands changed, not yet given out.
    changed: SettingSet,
}

impl Events {
    /// Nothing.
    pub const NONE: Self = Self {
        reset: false,
        changed: SettingSet::EMPTY,
    };

    /// What one command did.
    pub(super) const fn of(reset: bool, changed: SettingSet) -> Self {
        Self { reset, changed }
    }

    /// These and `later` together.
    pub(super) const fn and(self, later: Self) -> Self {
        Self {
            reset: self.reset || later.reset,
            changed: self.changed.union(later.changed),
        }
    }

    /// Whether there are none left.
    #[must_use]
    pub const fn is_empty(&self) -> bool {
        !self.reset && self.changed.is_empty()
    }
}

impl Iterator for Events {
    type Item = Event;

    fn next(&mut self) -> Option<Event> {
        if self.reset {
            self.reset = false;
            Some(Event::Reset)
        } else if self.changed.is_empty() {
            None
        } else {
            let changed = self.changed;
            self.changed = SettingSet::EMPTY;
            Some(Event::SettingsChanged(changed))
        }
    }
}
