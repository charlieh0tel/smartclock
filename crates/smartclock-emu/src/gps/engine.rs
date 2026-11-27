//! The synthetic engine: smartclock-gps's engine held on the board's
//! clock.  It gives the engine what the firmware sends as it is read,
//! a second's record at each second's start (`Engine::second`), and
//! sends what the engine writes; a self-test's answer, which the engine
//! holds back, goes out when due (`Engine::poll`).  It makes the 1 PPS,
//! and so acts on the engine's pulse settings (`Engine::settings`): the
//! cable delay puts the pulse earlier, the pulse offset later, and the
//! pulse mode says whether it is put out at all.

use smartclock_gps::engine::Engine as OncoreEngine;
use smartclock_gps::engine::Options;
use smartclock_gps::engine::Output;
use smartclock_gps::engine::event::Events;
use smartclock_gps::engine::furuno::Engine as FurunoEngine;
use smartclock_gps::engine::saved::ConfigurationError;
use smartclock_gps::engine::settings::PulseMode;
use smartclock_gps::engine::settings::Settings;
use smartclock_gps::record::Antenna;
use smartclock_gps::record::RaimSolution;
use smartclock_gps::record::Record;
use smartclock_gps::units::Milliseconds;

use super::Config;
use super::Family;
use super::HeardHook;
use super::PICOSECONDS_PER_NANOSECOND;
use super::PICOSECONDS_PER_SECOND;
use super::Port;
use super::Pulse;
use super::PulseHook;
use super::sky::Sky;
use super::sky::StartError;
use crate::board::time::CPU_HZ;
use crate::board::time::Cycles;
use crate::line::FarEnd;
use crate::run::Alongside;
use crate::run::Flow;
use crate::script::Injected;
use crate::trace;

/// Milliseconds a second, the engine's clock's unit.
const MILLISECONDS_PER_SECOND: u128 = 1000;
/// The CR LF each frame ends with, which the tap leaves out.
const LINE_END: usize = 2;

/// Why the synthetic engine cannot be made as configured.
#[derive(Debug, thiserror::Error)]
pub enum EngineError {
    /// The saved configuration does not suit the engine's model.
    #[error("{0}")]
    Saved(#[from] ConfigurationError),
    /// The power-up time is past what GPS time counts.
    #[error(transparent)]
    Start(#[from] StartError),
    /// Options for an engine that has none: a GT-74 is neither deaf nor
    /// powered up configured.
    #[error(
        "a Furuno GT-74 takes no options: it answers, and powers up with no saved configuration"
    )]
    FurunoOptions,
}

/// What the engine writes in one call: its frames, in order.
#[derive(Debug, Default)]
struct Frames(Vec<Vec<u8>>);

impl Output for Frames {
    fn send(&mut self, frame: &[u8]) {
        self.0.push(frame.to_vec());
    }
}

/// The shared engine of the family the run names.
#[derive(Debug)]
enum Held {
    /// An Oncore, boxed for its size.
    Oncore(Box<OncoreEngine>),
    /// A Furuno GT-74, boxed for its size.
    Furuno(Box<FurunoEngine>),
}

impl Held {
    /// The engine `config` names, set up as it says.
    fn new(config: &Config) -> Result<Self, EngineError> {
        Ok(match config.engine.family {
            Family::Oncore(identity) => {
                Self::Oncore(Box::new(OncoreEngine::new(identity, config.options)?))
            }
            Family::Furuno(identity) if config.options == Options::DEFAULT => {
                Self::Furuno(Box::new(FurunoEngine::new(identity)))
            }
            Family::Furuno(_) => return Err(EngineError::FurunoOptions),
        })
    }

    /// What commands have set it to.
    fn settings(&self) -> Settings {
        match self {
            Self::Oncore(engine) => engine.settings(),
            Self::Furuno(engine) => engine.settings(),
        }
    }

    /// How long after a pulse its seconds begin, where that is not at
    /// the pulse: a GT-74's (`engine::furuno::SENTENCES_AFTER_PULSE`).
    fn sentences_after_pulse(&self) -> Option<Milliseconds<u16>> {
        match self {
            Self::Oncore(_) => None,
            Self::Furuno(engine) => Some(engine.sentences_after_pulse()),
        }
    }

    /// A second starts at `now`, `record` being what is known in it.  A
    /// GT-74 counts seconds, not milliseconds, and takes no `now`.
    fn second(&mut self, record: &Record, now: Milliseconds<u64>, out: &mut Frames) {
        match self {
            Self::Oncore(engine) => engine.second(record, now, out),
            Self::Furuno(engine) => engine.second(record, out),
        }
    }

    /// Sends what is due by `now`.  A GT-74 holds nothing back
    /// (`engine::furuno`, "Timing").
    fn poll(&mut self, now: Milliseconds<u64>, out: &mut Frames) {
        match self {
            Self::Oncore(engine) => engine.poll(now, out),
            Self::Furuno(_) => {}
        }
    }

    /// Answers the commands among `bytes`, and returns what they did.
    fn receive(
        &mut self,
        bytes: &[u8],
        record: &Record,
        now: Milliseconds<u64>,
        out: &mut Frames,
    ) -> Events {
        match self {
            Self::Oncore(engine) => engine.receive(bytes, record, now, out),
            Self::Furuno(engine) => engine.receive(bytes, record, out),
        }
    }

    /// When the next held-back answer is due, if one is.
    fn deadline(&self) -> Option<Milliseconds<u64>> {
        match self {
            Self::Oncore(engine) => engine.deadline(),
            Self::Furuno(_) => None,
        }
    }

    /// Whether the 1 PPS goes out under `record`, as the pulse mode says:
    /// an Oncore's (VP p. 104) never, always, while a satellite is
    /// tracked, or while Time RAIM also finds the time solution within
    /// its alarm limit; a GT-74's as its own `tps` reports it, which
    /// waits in modes 2 and 3 for the almanac, the offset to UTC and a
    /// first fix of four satellites under a PDOP of 6 (GT p. 47;
    /// `furuno::Engine::pulsing`).
    fn pulses(&self, record: &Record) -> bool {
        match self {
            Self::Furuno(engine) => engine.pulsing(record),
            Self::Oncore(engine) => match engine.settings().pulse_mode {
                PulseMode::Off => false,
                PulseMode::Always => true,
                PulseMode::Tracking => tracking(engine, record),
                PulseMode::Confirmed => {
                    tracking(engine, record)
                        && record
                            .derived
                            .raim
                            .is_some_and(|raim| raim.solution == RaimSolution::Ok)
                }
            },
        }
    }
}

/// Whether an Oncore tracks a satellite: fixing, its antenna heard, and
/// one heard at or above the mask and not ignored, as the engine's own
/// tracking has it.
fn tracking(engine: &OncoreEngine, record: &Record) -> bool {
    let settings = engine.settings();
    engine.fixing()
        && !matches!(record.antenna, Antenna::Open | Antenna::Shorted)
        && record.satellites.iter().any(|satellite| {
            satellite.signal.is_some()
                && i16::from(satellite.elevation.0) >= i16::from(settings.mask_angle.0)
                && !settings.ignore_list.contains(satellite.prn)
        })
}

/// The synthetic engine.
pub(crate) struct Synthetic<P = FarEnd> {
    /// Its end of DUART channel A.
    far: P,
    /// The engine.
    engine: Held,
    /// What it is told each second.
    sky: Sky,
    /// The current second's record.
    record: Record,
    /// The next second to begin, counted from power-up.
    next_second: u64,
    /// The second whose 1 PPS is next, counted from power-up.
    next_pulse: u64,
    /// What each pulse is given to.
    hooks: Vec<PulseHook>,
    /// What each byte read from the firmware is given to.
    heard: Vec<HeardHook>,
    /// Copy what it sends to standard error.
    tap: bool,
    /// Bytes to put on the line as they come, past the engine's own
    /// messages: a script's write-gps steps.
    injected: Option<Injected>,
}

impl<P> std::fmt::Debug for Synthetic<P> {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        formatter
            .debug_struct("Synthetic")
            .field("engine", &self.engine)
            .field("next_second", &self.next_second)
            .finish_non_exhaustive()
    }
}

impl<P: Port> Synthetic<P> {
    /// The engine on `far`, set up as `config` says, copying what it
    /// sends to standard error with `tap`.
    pub(crate) fn new(far: P, config: Config, tap: bool) -> Result<Self, EngineError> {
        let engine = Held::new(&config)?;
        let sky = Sky::new(config.position, config.start, config.leap_seconds)?;
        let record = sky.record(0, &engine.settings());
        Ok(Self {
            far,
            engine,
            sky,
            record,
            next_second: 1,
            next_pulse: 1,
            hooks: Vec::new(),
            heard: Vec::new(),
            tap,
            injected: None,
        })
    }

    /// Its antenna connected, or not: disconnected, it hears nothing
    /// from its next second on (docs/emulator.md, "The controls").
    #[cfg(feature = "mc683xx")]
    pub(crate) fn set_antenna(&mut self, connected: bool) {
        self.sky.set_antenna(connected);
    }

    /// Gives each pulse to `hook`, as it passes.
    pub(crate) fn on_pulse(&mut self, hook: PulseHook) {
        self.hooks.push(hook);
    }

    /// Gives what the firmware sends to `hook`, as it is read.
    pub(crate) fn on_heard(&mut self, hook: HeardHook) {
        self.heard.push(hook);
    }

    /// Sends what `injected` is given, as it comes, after what is due.
    pub(crate) fn on_injected(&mut self, injected: Injected) {
        self.injected = Some(injected);
    }

    /// Puts `bytes` on the line to the firmware as they are, after what
    /// it has sent.
    pub(crate) fn inject(&mut self, bytes: &[u8]) {
        if self.tap {
            trace::note(format_args!("gps > {} (injected)", bytes.escape_ascii()));
        }
        self.far.send(bytes);
    }

    /// When second `n` begins: at its start for an Oncore, whose
    /// messages follow the pulse they are of; for a GT-74
    /// `SENTENCES_AFTER_PULSE` after the pulse before it, its sentences
    /// being of the second the next pulse marks (`engine::furuno`,
    /// "Timing").
    fn second_at(&self, n: u64) -> Cycles {
        match self.engine.sentences_after_pulse() {
            None => Cycles(n * CPU_HZ),
            Some(after) => Cycles(
                n.saturating_sub(1) * CPU_HZ
                    + u64::try_from(
                        u128::from(after.0) * u128::from(CPU_HZ) / MILLISECONDS_PER_SECOND,
                    )
                    .unwrap_or(u64::MAX),
            ),
        }
    }

    /// `at` on the engine's clock, milliseconds from power-up.
    fn milliseconds(at: Cycles) -> Milliseconds<u64> {
        let milliseconds = u128::from(at.0) * MILLISECONDS_PER_SECOND / u128::from(CPU_HZ);
        Milliseconds(u64::try_from(milliseconds).unwrap_or(u64::MAX))
    }

    /// The first cycle at or after `at` on the engine's clock.
    fn cycles(at: Milliseconds<u64>) -> Cycles {
        let cycles = (u128::from(at.0) * u128::from(CPU_HZ)).div_ceil(MILLISECONDS_PER_SECOND);
        Cycles(u64::try_from(cycles).unwrap_or(u64::MAX))
    }

    /// Time moves on to `now`: what the firmware sent is given to the
    /// engine, the seconds passed begin, and what is due goes out.
    pub(crate) fn advance(&mut self, now: Cycles) {
        let at = Self::milliseconds(now);
        let mut frames = Frames::default();
        let mut received = [0; 64];
        while let count @ 1.. = self.far.receive(&mut received) {
            let bytes = &received[..count];
            for hook in &mut self.heard {
                hook(bytes);
            }
            let events = self.engine.receive(bytes, &self.record, at, &mut frames);
            if !events.is_empty() {
                // a setting the sky acts on, from this second on
                self.record = self
                    .sky
                    .record(self.next_second - 1, &self.engine.settings());
            }
        }
        // the seconds and the pulses passed, in their order, so that a
        // pulse is put out under its second's record
        loop {
            let second = self.second_at(self.next_second);
            let pulse = self.pulse_at(self.next_pulse);
            match pulse {
                Some(pulse) if pulse.cycles() <= now && pulse.cycles() < second => {
                    if self.engine.pulses(&self.record) {
                        for hook in &mut self.hooks {
                            hook(&pulse);
                        }
                    }
                    self.next_pulse += 1;
                }
                // a pulse moved before power-up is none
                None if self.next_pulse < self.next_second => self.next_pulse += 1,
                _ if second <= now => {
                    let n = self.next_second;
                    self.record = self.sky.record(n, &self.engine.settings());
                    self.engine
                        .second(&self.record, Self::milliseconds(second), &mut frames);
                    self.next_second += 1;
                }
                _ => break,
            }
        }
        self.engine.poll(at, &mut frames);
        for frame in frames.0 {
            self.send(&frame);
        }
        let injected = self
            .injected
            .as_ref()
            .map(|injected| std::mem::take(&mut *injected.borrow_mut()))
            .unwrap_or_default();
        if !injected.is_empty() {
            self.inject(&injected);
        }
    }

    /// Sends a frame to the firmware.
    fn send(&mut self, frame: &[u8]) {
        if self.tap {
            trace::note(format_args!(
                "gps > {}",
                frame[..frame.len().saturating_sub(LINE_END)].escape_ascii()
            ));
        }
        self.far.send(frame);
    }

    /// Its port.
    #[cfg(feature = "mc683xx")]
    pub(crate) fn port(&mut self) -> &mut P {
        &mut self.far
    }

    /// Second `n`'s 1 PPS as the pulse settings place it: its start,
    /// less the cable delay (`@@Az`, VP p. 68), plus the pulse offset
    /// (`@@Ay`, VP p. 66).  `None` for one before power-up.
    fn pulse_at(&self, n: u64) -> Option<Pulse> {
        let settings = self.engine.settings();
        let picoseconds = |nanoseconds: u32| u64::from(nanoseconds) * PICOSECONDS_PER_NANOSECOND;
        (n * PICOSECONDS_PER_SECOND + picoseconds(settings.pulse_offset.0))
            .checked_sub(picoseconds(settings.cable_delay.0))
            .map(|picoseconds| Pulse { picoseconds })
    }

    /// The next pulse, wherever it falls, put out or not: an event of
    /// its own on mc683xx's board time.
    #[cfg(feature = "mc683xx")]
    pub(crate) fn next_pulse(&self) -> Option<Pulse> {
        self.pulse_at(self.next_pulse)
    }

    /// When it next acts by itself: the next second's start, or a pulse
    /// or a held-back answer falling due before it.
    pub(crate) fn next_due(&self) -> Cycles {
        let second = self.second_at(self.next_second);
        let pulse = self.pulse_at(self.next_pulse).map(Pulse::cycles);
        self.engine
            .deadline()
            .map(Self::cycles)
            .into_iter()
            .chain(pulse)
            .fold(second, Cycles::min)
    }
}

/// The engine runs alongside the board, its seconds and its pulses
/// ending slices, so that each 1 PPS reaches the board at its own
/// cycle, before the board's devices move past it.
impl<P: Port> Alongside for Synthetic<P> {
    fn advance(&mut self, now: Cycles) -> Flow {
        Self::advance(self, now);
        Flow::Continue
    }

    fn next_event(&self) -> Option<Cycles> {
        let second = self.second_at(self.next_second);
        Some(
            self.pulse_at(self.next_pulse)
                .map_or(second, |pulse| pulse.cycles().min(second)),
        )
    }
}

/// On mc683xx's time a held-back answer due before the next second is
/// an event of its own: the machine above advances what runs alongside
/// after every slice, mc683xx only at its events.
#[cfg(feature = "mc683xx")]
impl<P: Port> mc683xx_modules::peripheral::Timed for Synthetic<P> {
    fn next_event(&self) -> Option<mc683xx_cpu32::Cycle> {
        Some(mc683xx_cpu32::Cycle::from_clocks(self.next_due().0))
    }

    fn advance(&mut self, now: mc683xx_cpu32::Cycle) {
        Self::advance(self, Cycles(now.clocks()));
    }
}
