//! The engine, paced to real time: each second it takes the provider's
//! record and steps, and between seconds it answers what the instrument
//! sends.

use std::io;
use std::io::Write;
use std::sync::Arc;
use std::sync::Mutex;
use std::sync::PoisonError;
use std::time::Duration;
use std::time::SystemTime;

use jiff::Timestamp;
use smartclock_gps::engine::Output;
use smartclock_gps::engine::event::Event;
use smartclock_gps::record::Record;
use smartclock_gps::source::Presence;
use smartclock_gps::source::Source;
use smartclock_gps::source::Tick;
use smartclock_gps::units::Milliseconds;
use smartclock_gps::units::Seconds;

use crate::engine::Engine;
use crate::event;
use crate::link::Transport;
use crate::provider::Kind;
use crate::provider::Shared;
use crate::provider::complete_scene;

/// How far into each second a live provider's record is taken: a
/// receiver's epoch arrives some time after the second it is of, and
/// is the next second's to take if taken at its start.
pub(crate) const LIVE_PHASE: Duration = Duration::from_millis(500);
/// How late a second may be begun before the clock is taken to have
/// jumped, and the seconds are counted again from now.
const LATEST_START: Duration = Duration::from_secs(2);
/// How long a live provider may give nothing before it is reported.
const SILENCE: u64 = 3;
const NANOSECONDS_PER_MILLISECOND: u128 = 1_000_000;
const ONE_SECOND: Duration = Duration::from_secs(1);

/// The time, as the feed reads it.
pub(crate) trait Clock {
    /// Since the Unix epoch.
    fn now(&self) -> Duration;
}

/// The host's clock.
#[derive(Debug)]
pub(crate) struct SystemClock;

impl Clock for SystemClock {
    fn now(&self) -> Duration {
        SystemTime::now()
            .duration_since(SystemTime::UNIX_EPOCH)
            .unwrap_or_default()
    }
}

/// The frames the engine hands over in one call.
#[derive(Debug, Default)]
struct Frames(Vec<Vec<u8>>);

impl Output for Frames {
    fn send(&mut self, frame: &[u8]) {
        self.0.push(frame.to_vec());
    }
}

/// Every byte each way, a line a frame or a read: its UTC time to the
/// millisecond, `to` (the engine) or `from` (it), and the bytes in hex.
#[derive(Debug)]
pub(crate) struct WireLog<W: Write> {
    out: W,
}

impl<W: Write> WireLog<W> {
    pub(crate) const fn new(out: W) -> Self {
        Self { out }
    }

    fn line(&mut self, at: Duration, direction: &str, bytes: &[u8]) -> io::Result<()> {
        let at = i64::try_from(at.as_millis())
            .ok()
            .and_then(|milliseconds| Timestamp::from_millisecond(milliseconds).ok())
            .unwrap_or(Timestamp::UNIX_EPOCH);
        write!(self.out, "{at:.3} {direction} ")?;
        for byte in bytes {
            write!(self.out, "{byte:02x}")?;
        }
        writeln!(self.out)?;
        self.out.flush()
    }
}

/// What ends a run.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) enum End {
    /// A recording's end.
    Recording,
    /// The seconds a test asked for.
    Seconds,
}

/// The engine and where its records come from.
#[derive(Debug)]
pub(crate) struct Feed {
    engine: Engine,
    shared: Arc<Mutex<Shared>>,
    kind: Kind,
    /// The current second's record, which commands between seconds are
    /// answered with.
    record: Record,
    /// The next tick to take.
    tick: u64,
    /// The tick a live provider fell silent at.
    silent_since: Option<u64>,
    /// The leap seconds the wrappers withheld or dropped, last seen.
    withheld: u32,
    dropped: u32,
}

impl Feed {
    pub(crate) const fn new(engine: Engine, shared: Arc<Mutex<Shared>>, kind: Kind) -> Self {
        Self {
            engine,
            shared,
            kind,
            record: Record::EMPTY,
            tick: 0,
            silent_since: None,
            withheld: 0,
            dropped: 0,
        }
    }

    /// Takes the next tick's record and steps the engine with it, or
    /// leaves it silent for the second.
    fn second(&mut self, now: Milliseconds<u64>, out: &mut Frames) -> Presence {
        let tick = Tick(self.tick);
        self.tick += 1;
        let mut record = Record::EMPTY;
        let presence = {
            let held = Arc::clone(&self.shared);
            let mut shared = held.lock().unwrap_or_else(PoisonError::into_inner);
            shared.tick = tick;
            let presence = shared.nest.record(tick, &mut record);
            self.watch(&shared, tick, presence);
            presence
        };
        if presence == Presence::Fresh {
            if self.kind == Kind::Scene {
                complete_scene(&mut record, &self.engine.settings());
            }
            self.record = record;
            self.engine.second(&self.record, now, out);
        } else {
            self.engine.poll(now, out);
        }
        presence
    }

    /// Reports what the provider and its wrappers did that changes what
    /// the engine is given.
    fn watch(&mut self, shared: &Shared, tick: Tick, presence: Presence) {
        let withheld = shared.nest.withheld();
        if withheld > 0 && self.withheld == 0 {
            event("the provider has a leap second of its own; the one asked for is withheld");
        }
        self.withheld = withheld;
        let dropped = shared.nest.inner().dropped();
        if dropped > 0 && self.dropped == 0 {
            event("the provider's own leap second is dropped under the fake start");
        }
        self.dropped = dropped;

        let Some(latest) = shared.latest() else {
            return;
        };
        match (presence, self.silent_since) {
            (Presence::Silent, None) => self.silent_since = Some(tick.0),
            (Presence::Silent, Some(since)) if tick.0 - since == SILENCE => {
                event(format_args!(
                    "gps provider silent for {SILENCE} s; the engine is silent with it"
                ));
            }
            (Presence::Fresh, Some(since)) => {
                if tick.0 - since >= SILENCE {
                    event(format_args!("gps provider back after {} s", tick.0 - since));
                }
                self.silent_since = None;
            }
            _ => {}
        }
        if presence == Presence::Fresh
            && let Some(slip) = latest.slip()
            && slip != Seconds(0)
        {
            event(format_args!(
                "gps provider's time moved {} s against the clock",
                slip.0
            ));
        }
    }

    /// Answers the commands among `bytes`, with the current second's
    /// record.
    fn receive(&mut self, bytes: &[u8], now: Milliseconds<u64>, out: &mut Frames) {
        let events = self.engine.receive(bytes, &self.record, now, out);
        for happened in events {
            match happened {
                Event::Reset => event(self.engine.reset()),
                Event::SettingsChanged(settings) => {
                    event(format_args!("engine settings changed: {settings:?}"));
                }
                other => event(format_args!("engine: {other:?}")),
            }
        }
    }

    fn poll(&mut self, now: Milliseconds<u64>, out: &mut Frames) {
        self.engine.poll(now, out);
    }
}

/// Milliseconds from `origin` to `now`, none before it.
fn since(origin: Duration, now: Duration) -> Milliseconds<u64> {
    let nanoseconds = now.saturating_sub(origin).as_nanos();
    Milliseconds(u64::try_from(nanoseconds / NANOSECONDS_PER_MILLISECOND).unwrap_or(u64::MAX))
}

/// The first whole second after `now`, `phase` into it.
fn next_second(now: Duration, phase: Duration) -> Duration {
    Duration::from_secs(now.as_secs() + 1) + phase
}

/// Writes the engine's frames to the instrument and the log.
fn send<W: Write>(
    frames: &mut Frames,
    transport: &mut dyn Transport,
    log: &mut Option<WireLog<W>>,
    at: Duration,
) {
    for frame in frames.0.drain(..) {
        transport.write(&frame);
        log_line(log, at, "from", &frame);
    }
}

fn log_line<W: Write>(log: &mut Option<WireLog<W>>, at: Duration, direction: &str, bytes: &[u8]) {
    if let Some(wire) = log
        && let Err(error) = wire.line(at, direction, bytes)
    {
        event(format_args!("the log: {error}; logging stops"));
        *log = None;
    }
}

/// Runs `feed` against `transport` on `clock`: a second each whole
/// second of it, `phase` in, until a recording ends or, if given,
/// `seconds` have been stepped.
pub(crate) fn run<W: Write>(
    feed: &mut Feed,
    transport: &mut dyn Transport,
    clock: &dyn Clock,
    log: &mut Option<WireLog<W>>,
    phase: Duration,
    seconds: Option<u64>,
) -> End {
    let mut origin = next_second(clock.now(), phase);
    let mut frames = Frames::default();
    let mut inbound = Vec::new();
    loop {
        if seconds.is_some_and(|seconds| feed.tick >= seconds) {
            return End::Seconds;
        }
        let elapsed = u32::try_from(feed.tick).unwrap_or(u32::MAX);
        let mut due = origin + ONE_SECOND * elapsed;
        loop {
            let now = clock.now();
            if now >= due {
                if now - due > LATEST_START {
                    event("the clock jumped; counting seconds again from now");
                    origin = next_second(now, phase) - ONE_SECOND * elapsed;
                    due = origin + ONE_SECOND * elapsed;
                    continue;
                }
                break;
            }
            inbound.clear();
            transport.read(&mut inbound, due - now);
            let now = clock.now();
            if inbound.is_empty() {
                feed.poll(since(origin, now), &mut frames);
            } else {
                log_line(log, now, "to", &inbound);
                feed.receive(&inbound, since(origin, now), &mut frames);
            }
            send(&mut frames, transport, log, now);
        }
        let now = clock.now();
        let presence = feed.second(since(origin, now), &mut frames);
        send(&mut frames, transport, log, now);
        if presence == Presence::Silent && feed.kind == Kind::Recording {
            event(format_args!(
                "the recording ended after {} seconds",
                feed.tick - 1
            ));
            return End::Recording;
        }
    }
}

#[cfg(test)]
mod tests {
    use super::Clock;
    use super::End;
    use super::Feed;
    use super::WireLog;
    use super::run;
    use crate::cli::EngineSpec;
    use crate::cli::ProviderSpec;
    use crate::engine::Engine;
    use crate::link::Transport;
    use crate::provider::Built;
    use crate::provider::Kind;
    use crate::provider::Shared;
    use crate::provider::build;
    use crate::provider::recording;
    use crate::provider::tests::EPOCHS;
    use crate::provider::tests::capture;
    use smartclock_gps::engine::Options;
    use smartclock_gps::engine::furuno::GT_74;
    use smartclock_gps::engine::identity::Identity;
    use smartclock_gps::engine::identity::VP_EIGHT;
    use smartclock_gps::engine::identity::VP_SIX;
    use smartclock_gps::engine::saved::CONFIGURATION_58503A;
    use smartclock_gps::oncore::CJ_RECEIVER_ID;
    use smartclock_gps::oncore::EA_POSITION_STATUS;
    use smartclock_gps::oncore::encode;
    use smartclock_gps::source::Tick;
    use smartclock_gps::source::wrap::FakeTime;
    use smartclock_gps::source::wrap::LeapSchedule;
    use smartclock_gps::source::wrap::Relabel;
    use smartclock_gps::time::DateTime;
    use std::cell::Cell;
    use std::collections::BTreeMap;
    use std::rc::Rc;
    use std::sync::Arc;
    use std::sync::Mutex;
    use std::time::Duration;

    /// The test's clock: it moves only as the transport waits.
    #[derive(Clone)]
    struct TestClock(Rc<Cell<Duration>>);

    impl Clock for TestClock {
        fn now(&self) -> Duration {
            self.0.get()
        }
    }

    /// An instrument in memory: what it sends at each whole second, and
    /// what it was sent, by the second it came in.
    struct Instrument {
        clock: TestClock,
        /// Bytes to send, by the whole second from which they are sent.
        script: BTreeMap<u64, Vec<u8>>,
        /// What it was sent, by the whole second.
        received: BTreeMap<u64, Vec<Vec<u8>>>,
    }

    impl Transport for Instrument {
        fn read(&mut self, into: &mut Vec<u8>, timeout: Duration) {
            let now = self.clock.now();
            if let Some(bytes) = self
                .script
                .first_key_value()
                .filter(|(second, _)| **second <= now.as_secs())
                .map(|(second, _)| *second)
                .and_then(|second| self.script.remove(&second))
            {
                into.extend(bytes);
                return;
            }
            self.clock.0.set(now + timeout);
        }

        fn write(&mut self, bytes: &[u8]) {
            self.received
                .entry(self.clock.now().as_secs())
                .or_default()
                .push(bytes.to_vec());
        }
    }

    /// When the test starts: a little into a second.
    const START: Duration = Duration::from_millis(1_800_000_000_250);

    fn instrument(script: BTreeMap<u64, Vec<u8>>) -> Instrument {
        Instrument {
            clock: TestClock(Rc::new(Cell::new(START))),
            script,
            received: BTreeMap::new(),
        }
    }

    fn command(id: smartclock_gps::oncore::MessageId) -> Vec<u8> {
        let mut out = [0; 16];
        let length = encode(id, &[], &mut out).unwrap();
        out[..length].to_vec()
    }

    fn oncore(identity: &'static Identity, options: Options) -> Engine {
        Engine::new(&EngineSpec::Oncore { identity, options }).unwrap()
    }

    fn fixed(engine: Engine) -> Feed {
        let now = DateTime::new(2027, 1, 1, 0, 0, 0).unwrap().to_unix();
        let Built { nest, kind } = build(&ProviderSpec::Fixed, None, now).unwrap();
        Feed::new(
            engine,
            Arc::new(Mutex::new(Shared {
                nest,
                tick: Tick(0),
            })),
            kind,
        )
    }

    /// Whether a frame is of `id`.
    fn is(frame: &[u8], id: smartclock_gps::oncore::MessageId) -> bool {
        frame.starts_with(b"@@") && frame[2..4] == id.bytes()
    }

    /// An engine saved as a 58503A leaves it, under the made-up sky,
    /// sends its position status every second from the first, unasked,
    /// each a second of GPS time after the one before; and every line
    /// of the log is a time, a direction and hex.
    #[test]
    fn a_saved_engine_sends_each_second() {
        let mut options = Options::DEFAULT;
        options.saved = Some(&CONFIGURATION_58503A);
        let mut feed = fixed(oncore(&VP_EIGHT, options));
        let mut instrument = instrument(BTreeMap::new());
        let clock = instrument.clock.clone();
        let mut log = Some(WireLog::new(Vec::new()));
        let end = run(
            &mut feed,
            &mut instrument,
            &clock,
            &mut log,
            Duration::ZERO,
            Some(5),
        );
        assert_eq!(end, End::Seconds);

        let first = START.as_secs() + 1;
        let seconds: Vec<u64> = instrument.received.keys().copied().collect();
        assert_eq!(seconds, (first..first + 5).collect::<Vec<_>>());
        let statuses: Vec<&Vec<u8>> = instrument
            .received
            .values()
            .map(|frames| {
                let status: Vec<&Vec<u8>> = frames
                    .iter()
                    .filter(|frame| is(frame, EA_POSITION_STATUS))
                    .collect();
                assert_eq!(status.len(), 1, "one @@Ea a second");
                status[0]
            })
            .collect();
        // @@Ea's seconds field, data byte 6 (VP p. 125), counts on.
        let seconds_field: Vec<u8> = statuses.iter().map(|frame| frame[4 + 6]).collect();
        for pair in seconds_field.windows(2) {
            assert_eq!((pair[0] + 1) % 60, pair[1], "{seconds_field:?}");
        }

        let text = String::from_utf8(log.unwrap().out).unwrap();
        assert!(!text.is_empty());
        for line in text.lines() {
            let words: Vec<&str> = line.split(' ').collect();
            assert_eq!(words.len(), 3, "{line}");
            assert!(words[0].ends_with('Z') && words[0].contains('.'), "{line}");
            assert!(words[1] == "to" || words[1] == "from", "{line}");
            assert!(
                words[2].chars().all(|digit| digit.is_ascii_hexdigit()),
                "{line}"
            );
        }
    }

    /// An engine as it left the factory sends nothing until asked, and
    /// answers a command between seconds at once; a deaf one does not.
    #[test]
    fn commands_are_answered_between_seconds() {
        for deaf in [false, true] {
            let mut options = Options::DEFAULT;
            options.deaf = deaf;
            let mut feed = fixed(oncore(&VP_SIX, options));
            let asked = START.as_secs() + 2;
            let mut instrument = instrument(BTreeMap::from([(asked, command(CJ_RECEIVER_ID))]));
            let clock = instrument.clock.clone();
            run(
                &mut feed,
                &mut instrument,
                &clock,
                &mut None::<WireLog<Vec<u8>>>,
                Duration::ZERO,
                Some(4),
            );
            let answered: Vec<u64> = instrument
                .received
                .iter()
                .filter(|(_, frames)| frames.iter().any(|frame| is(frame, CJ_RECEIVER_ID)))
                .map(|(second, _)| *second)
                .collect();
            if deaf {
                assert!(instrument.received.is_empty(), "a deaf engine answered");
            } else {
                assert_eq!(answered, [asked]);
                assert_eq!(instrument.received.len(), 1, "sent unasked");
            }
        }
    }

    /// A recording runs to its end, and the run ends with it.
    #[test]
    fn a_run_ends_with_its_recording() {
        let shared = Shared {
            nest: LeapSchedule::new(FakeTime::new(recording(Vec::new()), Relabel::Keep), None),
            tick: Tick(0),
        };
        let mut feed = Feed::new(
            oncore(&VP_SIX, Options::DEFAULT),
            Arc::new(Mutex::new(shared)),
            Kind::Recording,
        );
        let mut instrument = instrument(BTreeMap::new());
        let clock = instrument.clock.clone();
        let end = run(
            &mut feed,
            &mut instrument,
            &clock,
            &mut None::<WireLog<Vec<u8>>>,
            Duration::ZERO,
            Some(10),
        );
        assert_eq!(end, End::Recording);
    }

    /// Whether a frame is the NMEA sentence of address `address`.
    fn sentence(frame: &[u8], address: &str) -> bool {
        frame.starts_with(format!("${address},").as_bytes())
    }

    /// The seconds in which `address` was sent, and how often in each.
    fn seconds_of(instrument: &Instrument, address: &str) -> Vec<(u64, usize)> {
        instrument
            .received
            .iter()
            .map(|(second, frames)| {
                let count = frames
                    .iter()
                    .filter(|frame| sentence(frame, address))
                    .count();
                (*second, count)
            })
            .filter(|(_, count)| *count > 0)
            .collect()
    }

    /// A GT-74 under the made-up sky sends what it sends at power-up
    /// each second from the first, unasked; takes an interval for `GSA`
    /// and sends it at that interval from then on; and answers `srq`
    /// with `ssd` in the second it was asked.
    #[test]
    fn a_furuno_engine_sends_at_its_intervals_and_answers() {
        let mut feed = fixed(Engine::new(&EngineSpec::Furuno(&GT_74)).unwrap());
        let asked = START.as_secs() + 2;
        let mut instrument = instrument(BTreeMap::from([(
            asked,
            b"$PFEC,GPint,GSA02\r\n$PFEC,GPsrq\r\n".to_vec(),
        )]));
        let clock = instrument.clock.clone();
        let mut log = Some(WireLog::new(Vec::new()));
        let end = run(
            &mut feed,
            &mut instrument,
            &clock,
            &mut log,
            Duration::ZERO,
            Some(8),
        );
        assert_eq!(end, End::Seconds);

        let first = START.as_secs() + 1;
        let every: Vec<u64> = (first..first + 8).collect();
        // GSV takes as many sentences as the sky's satellites need.
        for address in ["GPGGA", "GPGSV", "PFEC,GPtps"] {
            let seconds: Vec<u64> = seconds_of(&instrument, address)
                .iter()
                .map(|(second, _)| *second)
                .collect();
            assert_eq!(seconds, every, "{address}");
        }
        for address in ["GPGGA", "PFEC,GPtps"] {
            assert!(
                seconds_of(&instrument, address)
                    .iter()
                    .all(|(_, count)| *count == 1),
                "{address}"
            );
        }
        assert_eq!(seconds_of(&instrument, "PFEC,GPssd"), [(asked, 1)]);
        let gsa: Vec<u64> = seconds_of(&instrument, "GPGSA")
            .iter()
            .map(|(second, count)| {
                assert_eq!(*count, 1);
                *second
            })
            .collect();
        assert!(gsa.len() >= 2 && gsa[0] > asked, "{gsa:?}");
        for pair in gsa.windows(2) {
            assert_eq!(pair[1] - pair[0], 2, "{gsa:?}");
        }
        for frame in instrument.received.values().flatten() {
            assert!(frame.starts_with(b"$") && frame.ends_with(b"\r\n"));
        }

        let text = String::from_utf8(log.unwrap().out).unwrap();
        assert_eq!(text.lines().filter(|line| line.contains(" to ")).count(), 1);
    }

    /// A UBX recording drives a GT-74 to its end: a `GGA` a second, each
    /// of the recording's UTC second.
    #[test]
    fn a_recording_drives_a_furuno_engine() {
        let shared = Shared {
            nest: LeapSchedule::new(FakeTime::new(recording(capture()), Relabel::Keep), None),
            tick: Tick(0),
        };
        let mut feed = Feed::new(
            Engine::new(&EngineSpec::Furuno(&GT_74)).unwrap(),
            Arc::new(Mutex::new(shared)),
            Kind::Recording,
        );
        let mut instrument = instrument(BTreeMap::new());
        let clock = instrument.clock.clone();
        let end = run(
            &mut feed,
            &mut instrument,
            &clock,
            &mut None::<WireLog<Vec<u8>>>,
            Duration::ZERO,
            Some(10),
        );
        assert_eq!(end, End::Recording);
        // Noon GPS time less the recording's 18 leap seconds.
        let times: Vec<String> = instrument
            .received
            .values()
            .flatten()
            .filter(|frame| sentence(frame, "GPGGA"))
            .map(|frame| String::from_utf8_lossy(&frame[7..13]).into_owned())
            .collect();
        assert_eq!(times.len(), usize::try_from(EPOCHS).unwrap());
        assert_eq!(times, ["115942", "115943", "115944"]);
    }

    /// A clock that jumps ahead does not make the feed catch up: the
    /// seconds are counted again from the new time.
    #[test]
    fn a_clock_jump_steps_once() {
        struct Jumping(Instrument);
        impl Transport for Jumping {
            fn read(&mut self, into: &mut Vec<u8>, timeout: Duration) {
                if self.0.clock.now().as_secs() == START.as_secs() + 2 {
                    self.0
                        .clock
                        .0
                        .set(self.0.clock.now() + Duration::from_secs(3600));
                }
                self.0.read(into, timeout);
            }
            fn write(&mut self, bytes: &[u8]) {
                self.0.write(bytes);
            }
        }
        let mut options = Options::DEFAULT;
        options.saved = Some(&CONFIGURATION_58503A);
        let mut feed = fixed(oncore(&VP_EIGHT, options));
        let mut jumping = Jumping(instrument(BTreeMap::new()));
        let clock = jumping.0.clock.clone();
        run(
            &mut feed,
            &mut jumping,
            &clock,
            &mut None::<WireLog<Vec<u8>>>,
            Duration::ZERO,
            Some(6),
        );
        // A second's frames each in a whole second of its own.
        assert_eq!(jumping.0.received.len(), 6);
    }
}
