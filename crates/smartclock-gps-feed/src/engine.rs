//! The engine the feed runs, of whichever family the command line
//! names, behind the one face the per-second loop steps.

use std::time::Duration;

use anyhow::Result;
use smartclock_gps::engine::Engine as OncoreEngine;
use smartclock_gps::engine::Output;
use smartclock_gps::engine::event::Events;
use smartclock_gps::engine::furuno::Engine as FurunoEngine;
use smartclock_gps::engine::settings::Settings;
use smartclock_gps::record::Record;
use smartclock_gps::units::Milliseconds;

use crate::cli::EngineSpec;

/// An engine of one family or the other.
#[derive(Debug)]
pub(crate) enum Engine {
    /// A Motorola Oncore, on its binary `@@` messages.  Each family is
    /// boxed for its size.
    Oncore(Box<OncoreEngine>),
    /// A Furuno GT-74, on NMEA sentences and Furuno's `$PFEC` ones.
    Furuno(Box<FurunoEngine>),
}

impl Engine {
    /// Makes the engine `spec` names, its options checked.
    pub(crate) fn new(spec: &EngineSpec) -> Result<Self> {
        Ok(match spec {
            EngineSpec::Oncore { identity, options } => {
                Self::Oncore(Box::new(OncoreEngine::new(identity, *options)?))
            }
            EngineSpec::Furuno(identity) => Self::Furuno(Box::new(FurunoEngine::new(identity))),
        })
    }

    /// How long after a pulse it sends a second's frames, where that is
    /// not when its record is taken: a GT-74 sends the sentences of the
    /// second the next pulse marks
    /// [`SENTENCES_AFTER_PULSE`](smartclock_gps::engine::furuno::SENTENCES_AFTER_PULSE)
    /// after a pulse (`engine::furuno`, "Timing").  An Oncore's are of
    /// the record's own second, sent when it is taken.
    pub(crate) fn sentences_after_pulse(&self) -> Option<Duration> {
        match self {
            Self::Oncore(_) => None,
            Self::Furuno(engine) => Some(Duration::from_millis(
                engine.sentences_after_pulse().0.into(),
            )),
        }
    }

    /// What commands have set it to.
    pub(crate) fn settings(&self) -> Settings {
        match self {
            Self::Oncore(engine) => engine.settings(),
            Self::Furuno(engine) => engine.settings(),
        }
    }

    /// A second starts, `record` being what is known in it, of the
    /// second the next pulse marks for a GT-74
    /// ([`Engine::sentences_after_pulse`]).  A GT-74 counts seconds, not
    /// milliseconds, and takes no `now`.
    pub(crate) fn second<O: Output + ?Sized>(
        &mut self,
        record: &Record,
        now: Milliseconds<u64>,
        out: &mut O,
    ) {
        match self {
            Self::Oncore(engine) => engine.second(record, now, out),
            Self::Furuno(engine) => engine.second(record, out),
        }
    }

    /// Time passes with no record: sends what is due by `now`.  A GT-74
    /// has nothing that waits (`engine::furuno`, "Timing").
    pub(crate) fn poll<O: Output + ?Sized>(&mut self, now: Milliseconds<u64>, out: &mut O) {
        match self {
            Self::Oncore(engine) => engine.poll(now, out),
            Self::Furuno(_) => {}
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
            Self::Furuno(engine) => engine.receive(bytes, record, out),
        }
    }

    /// What an [`Event::Reset`] is, for the log.
    ///
    /// [`Event::Reset`]: smartclock_gps::engine::event::Event::Reset
    pub(crate) const fn reset(&self) -> &'static str {
        match self {
            Self::Oncore(_) => "engine set to its defaults (@@Cf)",
            Self::Furuno(_) => "engine restarted ($PFEC,GPclr)",
        }
    }
}
