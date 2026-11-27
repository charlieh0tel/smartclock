//! The external engine's 1 PPS on board time (`gps::external`).

use mc683xx_board::time::PICOSECONDS_PER_SECOND;
use mc683xx_board::time::Time;
use smartclock_gps::engine::Engine as OncoreEngine;
use smartclock_gps::engine::Options;
use smartclock_gps::engine::Output;
use smartclock_gps::engine::furuno;
use smartclock_gps::engine::furuno::Engine as FurunoEngine;
use smartclock_gps::engine::identity::VP_EIGHT;
use smartclock_gps::engine::settings::Settings;
use smartclock_gps::record::Record;
use smartclock_gps::units::Milliseconds;

use crate::board::participants::gps::PPS_HIGH;
use crate::gps::external::PulseClock;
use crate::gps::external::Rises;

/// Picoseconds a nanosecond.
const PICOSECONDS_PER_NANOSECOND: i64 = 1000;

/// What the firmware has set the external engine's pulse to, as it sends
/// it: its commands read by a model of each family, an Oncore's `@@Az`
/// and `@@Ay` (VP pp. 66, 68) and a GT-74's `set,t` (GT p. 11), the one
/// of the family whose commands last changed a setting giving the
/// cable delay and the pulse offset.  What the models answer goes
/// nowhere: the external engine answers for itself.
#[derive(Debug)]
struct Commanded {
    /// An Oncore's model.
    oncore: Box<OncoreEngine>,
    /// A GT-74's model.
    furuno: Box<FurunoEngine>,
    /// The settings of the family last commanded, if any has been.
    settings: Option<Settings>,
}

/// Where the models' answers go: nowhere.
struct Nowhere;

impl Output for Nowhere {
    fn send(&mut self, _frame: &[u8]) {}
}

impl Commanded {
    fn new() -> Self {
        Self {
            oncore: Box::new(
                OncoreEngine::new(&VP_EIGHT, Options::DEFAULT).expect("no saved configuration"),
            ),
            furuno: Box::new(FurunoEngine::new(&furuno::GT_74)),
            settings: None,
        }
    }

    /// The firmware sent `bytes`: the pulse's offset from its whole
    /// second, picoseconds, later for a positive one, if a command
    /// changed the settings.
    fn heard(&mut self, bytes: &[u8]) -> Option<i64> {
        let oncore = self
            .oncore
            .receive(bytes, &Record::EMPTY, Milliseconds(0), &mut Nowhere);
        let furuno = self.furuno.receive(bytes, &Record::EMPTY, &mut Nowhere);
        let settings = if !oncore.is_empty() {
            self.oncore.settings()
        } else if !furuno.is_empty() {
            self.furuno.settings()
        } else {
            return None;
        };
        let before = self.settings.replace(settings);
        let offset = |settings: &Settings| {
            (i64::from(settings.pulse_offset.0) - i64::from(settings.cable_delay.0))
                * PICOSECONDS_PER_NANOSECOND
        };
        let now = offset(&settings);
        (before.as_ref().map_or(0, offset) != now).then_some(now)
    }
}

/// The pulse: it rises as `Rises` schedules it, on the whole seconds of
/// `PulseClock`, moved by the cable delay and pulse offset the firmware
/// sends the engine, high for `PPS_HIGH`, each edge ending a
/// slice.
#[derive(Debug)]
pub(super) struct PpsTrain {
    /// The rises, picoseconds.
    pub(super) rises: Rises,
    /// The next fall, while high.
    falls: Option<Time>,
    /// The pulse's settings, as the firmware sends them.
    commanded: Commanded,
}

impl PpsTrain {
    /// The pulse on `seconds`' whole seconds, board time `now` and the
    /// run's time `run`.
    pub(super) fn new(seconds: PulseClock, now: Time, run: Time) -> Self {
        let into_second = seconds.into_second(run.picos(), PICOSECONDS_PER_SECOND);
        Self {
            rises: Rises::from(now.picos(), into_second, PICOSECONDS_PER_SECOND),
            falls: None,
            commanded: Commanded::new(),
        }
    }

    /// Bytes the firmware sent the engine: a change of the cable delay or
    /// the pulse offset moves each rise from the next on by the change,
    /// the seconds staying where they are.
    pub(super) fn heard(&mut self, bytes: &[u8]) {
        if let Some(offset) = self.commanded.heard(bytes) {
            self.rises.shift(offset);
        }
    }

    /// When the next edge is due.
    pub(super) fn next(&self) -> Option<Time> {
        self.rises
            .next()
            .map(Time::from_picos)
            .into_iter()
            .chain(self.falls)
            .min()
    }

    /// The edges due by `now`, as levels in order.
    pub(super) fn edges(&mut self, now: Time) -> Vec<bool> {
        let mut edges = Vec::new();
        if self.falls.is_some_and(|at| at <= now) {
            self.falls = None;
            edges.push(false);
        }
        if let Some(at) = self.rises.take(now.picos(), PICOSECONDS_PER_SECOND) {
            self.falls = Some(Time::from_picos(at) + PPS_HIGH);
            edges.push(true);
        }
        edges
    }
}
