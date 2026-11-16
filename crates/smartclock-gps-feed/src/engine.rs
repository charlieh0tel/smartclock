//! The engine the feed runs, of whichever family the command line
//! names, behind the one face the per-second loop steps.

use anyhow::Result;
use smartclock_gps::engine::Engine as OncoreEngine;
use smartclock_gps::engine::Output;
use smartclock_gps::engine::event::Events;
use smartclock_gps::engine::settings::Settings;
use smartclock_gps::record::Record;
use smartclock_gps::units::Milliseconds;

use crate::cli::EngineSpec;

/// An engine of one family or the other.
#[derive(Debug)]
pub(crate) enum Engine {
    /// A Motorola Oncore, on its binary `@@` messages.
    Oncore(OncoreEngine),
}

impl Engine {
    /// Makes the engine `spec` names, its options checked.
    pub(crate) fn new(spec: &EngineSpec) -> Result<Self> {
        Ok(match spec {
            EngineSpec::Oncore { identity, options } => {
                Self::Oncore(OncoreEngine::new(identity, *options)?)
            }
        })
    }

    /// What commands have set it to.
    pub(crate) fn settings(&self) -> Settings {
        match self {
            Self::Oncore(engine) => engine.settings(),
        }
    }

    /// A second starts, `record` being what is known in it.
    pub(crate) fn second<O: Output + ?Sized>(
        &mut self,
        record: &Record,
        now: Milliseconds<u64>,
        out: &mut O,
    ) {
        match self {
            Self::Oncore(engine) => engine.second(record, now, out),
        }
    }

    /// Time passes with no record: sends what is due by `now`.
    pub(crate) fn poll<O: Output + ?Sized>(&mut self, now: Milliseconds<u64>, out: &mut O) {
        match self {
            Self::Oncore(engine) => engine.poll(now, out),
        }
    }

    /// Answers the commands among `bytes`, `record` being the current
    /// second's, and returns what they did.
    pub(crate) fn receive<O: Output + ?Sized>(
        &mut self,
        bytes: &[u8],
        record: &Record,
        now: Milliseconds<u64>,
        out: &mut O,
    ) -> Events {
        match self {
            Self::Oncore(engine) => engine.receive(bytes, record, now, out),
        }
    }

    /// What an [`Event::Reset`] is, for the log.
    ///
    /// [`Event::Reset`]: smartclock_gps::engine::event::Event::Reset
    pub(crate) const fn reset(&self) -> &'static str {
        match self {
            Self::Oncore(_) => "engine set to its defaults (@@Cf)",
        }
    }
}
