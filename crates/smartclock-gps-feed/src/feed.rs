//! The engine, paced to real time: each second it takes the provider's
//! record and steps, and between seconds it answers what the instrument
//! sends.

use std::io;
use std::io::Write;
use std::sync::Arc;
use std::sync::Mutex;
use std::sync::PoisonError;
use std::time::Duration;
use std::time::Instant;
use std::time::SystemTime;

use jiff::Timestamp;
use smartclock_gps::engine::Output;
use smartclock_gps::engine::event::Event;
use smartclock_gps::record::Record;
use smartclock_gps::source::Presence;
use smartclock_gps::source::Source;
use smartclock_gps::source::Tick;
use smartclock_gps::time::UnixSeconds;
use smartclock_gps::units::Milliseconds;
use smartclock_gps::units::Seconds;
use smartclock_gps_ublox::assembler;

use crate::engine::Engine;
use crate::event;
use crate::link::Transport;
use crate::provider::Kind;
use crate::provider::Shared;
use crate::provider::complete_scene;

/// How far into each second a live provider's record is taken: a
/// receiver's epoch arrives some time after the second it is of, and
/// is the next second's to take if taken at its start.
const LIVE_PHASE: Duration = Duration::from_millis(500);
/// How late a second may be begun before the loop is taken to have
/// fallen behind, and the seconds it missed are skipped.
const LATEST_START: Duration = Duration::from_secs(2);
/// How far the host's clock may move against the monotonic one, between
/// two readings, before it is taken to have been stepped, and the
/// seconds are counted again from now.
const LARGEST_STEP: Duration = Duration::from_secs(1);
/// How long a live provider may give nothing before it is reported.
const SILENCE: u64 = 3;
const NANOSECONDS_PER_MILLISECOND: u128 = 1_000_000;
const ONE_SECOND: Duration = Duration::from_secs(1);

/// The time, as the feed reads it.
pub(crate) trait Clock {
    /// Since the Unix epoch, by the host's clock, which can be stepped
    /// either way.
    fn now(&self) -> Duration;

    /// Since some fixed instant, by a clock that is never stepped.
    fn monotonic(&self) -> Duration;
}

/// The host's clocks.
#[derive(Debug)]
pub(crate) struct SystemClock {
    /// What [`Clock::monotonic`] counts from.
    started: Instant,
}

impl SystemClock {
    pub(crate) fn new() -> Self {
        Self {
            started: Instant::now(),
        }
    }
}

impl Clock for SystemClock {
    fn now(&self) -> Duration {
        SystemTime::now()
            .duration_since(SystemTime::UNIX_EPOCH)
            .unwrap_or_default()
    }

    fn monotonic(&self) -> Duration {
        self.started.elapsed()
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
/// A frame is logged only once it has gone: one dropped, with no
/// instrument connected or a write that failed, never reached the wire.
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
    /// How many seconds a live provider's record was behind the host
    /// second it was taken in, last seen, and how many records taken
    /// were behind it at all.
    late: Seconds<i64>,
    late_records: u64,
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
            late: Seconds(0),
            late_records: 0,
        }
    }

    /// Takes the next tick's record and steps the engine with it, or
    /// leaves it silent for the second.  `at` is the host's clock.
    fn second(&mut self, now: Milliseconds<u64>, at: Duration, out: &mut Frames) -> Presence {
        let tick = Tick(self.tick);
        self.tick += 1;
        let mut record = Record::EMPTY;
        let presence = {
            let held = Arc::clone(&self.shared);
            let mut shared = held.lock().unwrap_or_else(PoisonError::into_inner);
            shared.tick = tick;
            let presence = shared.nest.record(tick, &mut record);
            self.watch(&shared, tick, presence, utc_second(at));
            presence
        };
        if presence == Presence::Fresh {
            if self.kind == Kind::Scene {
                complete_scene(&mut record, &self.engine.settings());
            } else {
                assembler::act(&mut record, &self.engine.settings());
            }
            self.record = record;
            self.engine.second(&self.record, now, out);
        } else {
            self.engine.poll(now, out);
        }
        presence
    }

    /// Reports what the provider and its wrappers did that changes what
    /// the engine is given, the record taken in host second `taken_in`.
    fn watch(&mut self, shared: &Shared, tick: Tick, presence: Presence, taken_in: UnixSeconds) {
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
        if presence == Presence::Fresh
            && let Some(of) = shared.delivered
        {
            self.lateness(of, taken_in);
        }
    }

    /// Holds the record of UTC second `of`, by the provider's own time,
    /// to the host second it was taken in, which is to be the same: one
    /// a second or more behind arrived later than the phase it is taken
    /// at.  A slip [`Latest::slip`] cannot see, being the same each
    /// second.  Reported when it changes, with the count of records late
    /// so far.
    ///
    /// [`Latest::slip`]: smartclock_gps::source::Latest::slip
    fn lateness(&mut self, of: UnixSeconds, taken_in: UnixSeconds) {
        let Some(late) = taken_in.checked_since(of) else {
            return;
        };
        if late != Seconds(0) {
            self.late_records = self.late_records.saturating_add(1);
        }
        if late != self.late {
            if late == Seconds(0) {
                event("gps provider's records are of the second they are taken in again");
            } else {
                event(format_args!(
                    "gps provider's record is {} s behind the second it is taken in, {} ms into it: its epochs arrive late ({} records so far)",
                    late.0,
                    LIVE_PHASE.as_millis(),
                    self.late_records
                ));
            }
        }
        self.late = late;
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

    /// Passes over `seconds` ticks unstepped.
    const fn skip(&mut self, seconds: u64) {
        self.tick = self.tick.saturating_add(seconds);
    }
}

/// Where the feed counts its seconds from, on the host's clock, and
/// how that clock stood against the monotonic one when last read.
#[derive(Debug)]
struct Pace {
    /// When tick 0 was, or would have been had the count always run as
    /// it runs now.
    origin: Duration,
    /// How far into each second a tick is.
    phase: Duration,
    /// The host's clock less the monotonic one, in nanoseconds.
    skew: i128,
}

/// The host's clock, and it less the monotonic one in nanoseconds.
fn skew(clock: &dyn Clock) -> (Duration, i128) {
    let now = clock.now();
    (now, nanoseconds(now) - nanoseconds(clock.monotonic()))
}

fn nanoseconds(duration: Duration) -> i128 {
    i128::try_from(duration.as_nanos()).unwrap_or(i128::MAX)
}

impl Pace {
    fn new(clock: &dyn Clock, origin: Duration, phase: Duration) -> Self {
        Self {
            origin,
            phase,
            skew: skew(clock).1,
        }
    }

    /// When tick `elapsed` is due.
    fn due(&self, elapsed: u32) -> Duration {
        self.origin + ONE_SECOND * elapsed
    }

    /// Reads the host's clock.  If it was stepped since it was last
    /// read, either way, the seconds are counted again from the next
    /// whole second, `elapsed` ticks having been counted already: a
    /// step is neither caught up nor waited out.
    fn now(&mut self, clock: &dyn Clock, elapsed: u32) -> Duration {
        let (now, skew) = skew(clock);
        let moved = skew - self.skew;
        self.skew = skew;
        if moved.unsigned_abs() > LARGEST_STEP.as_nanos() {
            let way = if moved < 0 { "back" } else { "ahead" };
            event(format_args!(
                "the clock stepped {way} {} ms; counting seconds again from now",
                moved.unsigned_abs() / NANOSECONDS_PER_MILLISECOND
            ));
            self.origin = next_second(now, self.phase).saturating_sub(ONE_SECOND * elapsed);
        }
        now
    }
}

/// How far into each second a provider of `kind` is taken.
pub(crate) const fn phase(kind: Kind) -> Duration {
    match kind {
        Kind::Live => LIVE_PHASE,
        Kind::Scene | Kind::Recording => Duration::ZERO,
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

/// When the first tick is due, `phase` into the next whole second of
/// `clock`: read once, both for the provider's start
/// ([`utc_second`]) and for [`run`], so a made-up sky's time is that of
/// the second its frames go out in.
pub(crate) fn first_second(clock: &dyn Clock, phase: Duration) -> Duration {
    next_second(clock.now(), phase)
}

/// The UTC second `at` is in.
pub(crate) fn utc_second(at: Duration) -> UnixSeconds {
    UnixSeconds::from_epoch(Seconds(i64::try_from(at.as_secs()).unwrap_or(i64::MAX)))
}

/// Writes the engine's frames to the instrument and the log.
fn send<W: Write>(
    frames: &mut Frames,
    transport: &mut dyn Transport,
    log: &mut Option<WireLog<W>>,
    at: Duration,
) {
    for frame in frames.0.drain(..) {
        if transport.write(&frame) {
            log_line(log, at, "from", &frame);
        }
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
/// second of it from `first` ([`first_second`]), as far into each,
/// until a recording ends or, if given, `seconds` have been stepped.
///
/// A step of the clock, either way, counts the seconds again from now.
/// A loop that fell behind with the clock unstepped skips the seconds
/// it missed, so a made-up sky's time stays the clock's.
pub(crate) fn run<W: Write>(
    feed: &mut Feed,
    transport: &mut dyn Transport,
    clock: &dyn Clock,
    log: &mut Option<WireLog<W>>,
    first: Duration,
    seconds: Option<u64>,
) -> End {
    let phase = Duration::from_nanos(u64::from(first.subsec_nanos()));
    let mut pace = Pace::new(clock, first, phase);
    let mut frames = Frames::default();
    let mut inbound = Vec::new();
    'seconds: loop {
        if seconds.is_some_and(|seconds| feed.tick >= seconds) {
            return End::Seconds;
        }
        let elapsed = u32::try_from(feed.tick).unwrap_or(u32::MAX);
        loop {
            let now = pace.now(clock, elapsed);
            let due = pace.due(elapsed);
            if now >= due {
                let late = now - due;
                if late > LATEST_START {
                    event(format_args!(
                        "the feed fell {} ms behind; the seconds missed are skipped",
                        late.as_millis()
                    ));
                    feed.skip(late.as_secs());
                    continue 'seconds;
                }
                break;
            }
            inbound.clear();
            transport.read(&mut inbound, due - now);
            let now = pace.now(clock, elapsed);
            if inbound.is_empty() {
                feed.poll(since(pace.origin, now), &mut frames);
            } else {
                log_line(log, now, "to", &inbound);
                feed.receive(&inbound, since(pace.origin, now), &mut frames);
            }
            send(&mut frames, transport, log, now);
        }
        let now = pace.now(clock, elapsed);
        let presence = feed.second(since(pace.origin, now), now, &mut frames);
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
    use super::ONE_SECOND;
    use super::WireLog;
    use super::first_second;
    use super::phase;
    use super::run;
    use super::utc_second;
    use crate::cli::EngineSpec;
    use crate::cli::ProviderSpec;
    use crate::engine::Engine;
    use crate::link::Transport;
    use crate::provider::Built;
    use crate::provider::Kind;
    use crate::provider::Provider;
    use crate::provider::Shared;
    use crate::provider::UTC_OFFSET;
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
    use smartclock_gps::record::GpsTime;
    use smartclock_gps::record::Record;
    use smartclock_gps::source::Latest;
    use smartclock_gps::source::wrap::FakeTime;
    use smartclock_gps::source::wrap::LeapSchedule;
    use smartclock_gps::source::wrap::Relabel;
    use smartclock_gps::time::GpsSeconds;
    use smartclock_gps::time::UnixSeconds;
    use smartclock_gps::units::Nanoseconds;
    use smartclock_gps::units::Seconds;
    use std::cell::Cell;
    use std::collections::BTreeMap;
    use std::rc::Rc;
    use std::sync::Arc;
    use std::sync::Mutex;
    use std::time::Duration;

    /// The test's clocks: they move only as the transport waits, or the
    /// host's clock is stepped.
    #[derive(Clone)]
    struct TestClock {
        now: Rc<Cell<Duration>>,
        monotonic: Rc<Cell<Duration>>,
    }

    impl TestClock {
        /// Time passes.
        fn wait(&self, wait: Duration) {
            self.now.set(self.now.get() + wait);
            self.monotonic.set(self.monotonic.get() + wait);
        }

        /// The host's clock is stepped, ahead or back.
        fn step(&self, to: Duration) {
            self.now.set(to);
        }
    }

    impl Clock for TestClock {
        fn now(&self) -> Duration {
            self.now.get()
        }

        fn monotonic(&self) -> Duration {
            self.monotonic.get()
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
        /// Whether it is connected: if not, what is sent is dropped.
        connected: bool,
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
            self.clock.wait(timeout);
        }

        fn write(&mut self, bytes: &[u8]) -> bool {
            if self.connected {
                self.received
                    .entry(self.clock.now().as_secs())
                    .or_default()
                    .push(bytes.to_vec());
            }
            self.connected
        }
    }

    /// When the test starts: a little into a second.
    const START: Duration = Duration::from_millis(1_800_000_000_250);

    fn instrument(script: BTreeMap<u64, Vec<u8>>) -> Instrument {
        Instrument {
            clock: TestClock {
                now: Rc::new(Cell::new(START)),
                monotonic: Rc::new(Cell::new(Duration::ZERO)),
            },
            script,
            received: BTreeMap::new(),
            connected: true,
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

    /// The made-up sky from the second the test's first tick is in, as
    /// the feed starts it.
    fn fixed(engine: Engine) -> Feed {
        let clock = instrument(BTreeMap::new()).clock;
        let now = utc_second(first_second(&clock, Duration::ZERO));
        let Built { nest, kind } = build(&ProviderSpec::Fixed, None, now).unwrap();
        Feed::new(engine, Arc::new(Mutex::new(Shared::new(nest))), kind)
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
        options.saved = Some(CONFIGURATION_58503A);
        let mut feed = fixed(oncore(&VP_EIGHT, options));
        let mut instrument = instrument(BTreeMap::new());
        let clock = instrument.clock.clone();
        let mut log = Some(WireLog::new(Vec::new()));
        let end = run(
            &mut feed,
            &mut instrument,
            &clock,
            &mut log,
            first_second(&clock, Duration::ZERO),
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

    /// With no instrument connected the engine runs on, and what it
    /// sends, dropped, is not logged as sent; what it is sent is.
    #[test]
    fn the_log_has_only_what_went() {
        let mut feed = fixed(saved());
        let mut instrument = instrument(BTreeMap::from([(
            START.as_secs() + 2,
            command(CJ_RECEIVER_ID),
        )]));
        instrument.connected = false;
        let clock = instrument.clock.clone();
        let mut log = Some(WireLog::new(Vec::new()));
        run(
            &mut feed,
            &mut instrument,
            &clock,
            &mut log,
            first_second(&clock, Duration::ZERO),
            Some(4),
        );
        let text = String::from_utf8(log.unwrap().out).unwrap();
        let directions: Vec<&str> = text
            .lines()
            .map(|line| line.split(' ').nth(1).unwrap())
            .collect();
        assert_eq!(directions, ["to"], "{text}");
    }

    /// gpsd's stand-in: an instrument that, before each second is
    /// taken, delivers a record of the second `behind` before it.
    struct Delivering {
        instrument: Instrument,
        shared: Arc<Mutex<Shared>>,
        behind: u64,
    }

    impl Transport for Delivering {
        fn read(&mut self, into: &mut Vec<u8>, timeout: Duration) {
            let second = (self.instrument.clock.now() + timeout).as_secs() - self.behind;
            let utc = UnixSeconds::from_epoch(Seconds(i64::try_from(second).unwrap()));
            let mut record = Record::EMPTY;
            record.utc_offset = Some(UTC_OFFSET);
            record.time = Some(GpsTime {
                seconds: GpsSeconds::from_utc(utc, UTC_OFFSET).unwrap(),
                subsecond: Nanoseconds(0),
            });
            self.shared.lock().unwrap().deliver(&record);
            self.instrument.read(into, timeout);
        }
        fn write(&mut self, bytes: &[u8]) -> bool {
            self.instrument.write(bytes)
        }
    }

    /// Each record gpsd gives is held to the host second it is taken
    /// in: one of that second is in time, one of the second before is
    /// late, and each such is counted.
    #[test]
    fn late_records_from_gpsd_are_counted() {
        const SECONDS: u64 = 4;
        for behind in [0, 1] {
            let shared = Arc::new(Mutex::new(Shared::new(LeapSchedule::new(
                FakeTime::new(Provider::Live(Box::new(Latest::new())), Relabel::Keep),
                None,
            ))));
            let mut feed = Feed::new(
                Engine::new(&EngineSpec::Furuno(&GT_74)).unwrap(),
                Arc::clone(&shared),
                Kind::Live,
            );
            let mut delivering = Delivering {
                instrument: instrument(BTreeMap::new()),
                shared,
                behind,
            };
            let clock = delivering.instrument.clock.clone();
            run(
                &mut feed,
                &mut delivering,
                &clock,
                &mut None::<WireLog<Vec<u8>>>,
                first_second(&clock, phase(Kind::Live)),
                Some(SECONDS),
            );
            assert_eq!(feed.late, Seconds(i64::try_from(behind).unwrap()));
            assert_eq!(feed.late_records, behind * SECONDS);
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
                first_second(&clock, Duration::ZERO),
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
        let shared = Shared::new(LeapSchedule::new(
            FakeTime::new(recording(Vec::new()), Relabel::Keep),
            None,
        ));
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
            first_second(&clock, Duration::ZERO),
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
            first_second(&clock, Duration::ZERO),
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
        let shared = Shared::new(LeapSchedule::new(
            FakeTime::new(recording(capture()), Relabel::Keep),
            None,
        ));
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
            first_second(&clock, Duration::ZERO),
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

    /// An instrument whose host's clock is stepped by `step`, or time
    /// passes by it with the loop held up, once, two seconds in; and
    /// the longest wait it was asked for.
    struct Interrupted {
        instrument: Instrument,
        step: i64,
        /// Whether the clock is stepped, not time passed.
        stepped: bool,
        longest: Duration,
    }

    impl Transport for Interrupted {
        fn read(&mut self, into: &mut Vec<u8>, timeout: Duration) {
            self.longest = self.longest.max(timeout);
            let clock = &self.instrument.clock;
            if self.step != 0 && clock.now().as_secs() == START.as_secs() + 2 {
                let by = Duration::from_secs(self.step.unsigned_abs());
                match (self.stepped, self.step > 0) {
                    (true, true) => clock.step(clock.now() + by),
                    (true, false) => clock.step(clock.now() - by),
                    (false, _) => clock.wait(by),
                }
                self.step = 0;
            }
            self.instrument.read(into, timeout);
        }
        fn write(&mut self, bytes: &[u8]) -> bool {
            self.instrument.write(bytes)
        }
    }

    /// Runs a saved engine under the made-up sky for `seconds`, its
    /// clock interrupted as `step` and `stepped` say.
    fn interrupted(engine: Engine, step: i64, stepped: bool, seconds: u64) -> Interrupted {
        let mut feed = fixed(engine);
        let mut interrupted = Interrupted {
            instrument: instrument(BTreeMap::new()),
            step,
            stepped,
            longest: Duration::ZERO,
        };
        let clock = interrupted.instrument.clock.clone();
        run(
            &mut feed,
            &mut interrupted,
            &clock,
            &mut None::<WireLog<Vec<u8>>>,
            first_second(&clock, Duration::ZERO),
            Some(seconds),
        );
        interrupted
    }

    fn saved() -> Engine {
        let mut options = Options::DEFAULT;
        options.saved = Some(CONFIGURATION_58503A);
        oncore(&VP_EIGHT, options)
    }

    /// A clock stepped either way does not make the feed catch up or
    /// wait it out: the seconds are counted again from the new time,
    /// and no wait is longer than a second.
    #[test]
    fn a_clock_step_either_way_steps_once() {
        const HOUR: i64 = 3600;
        for step in [HOUR, -HOUR] {
            let interrupted = interrupted(saved(), step, true, 6);
            // A second's frames each in a whole second of its own.
            assert_eq!(interrupted.instrument.received.len(), 6, "{step}");
            assert!(interrupted.longest <= ONE_SECOND, "{step}");
        }
    }

    /// The UTC second of day a `GGA` gives.
    fn gga_second(frame: &[u8]) -> u64 {
        let field = |range: std::ops::Range<usize>| -> u64 {
            std::str::from_utf8(&frame[range]).unwrap().parse().unwrap()
        };
        field(7..9) * 3600 + field(9..11) * 60 + field(11..13)
    }

    /// How far the made-up sky's UTC second is from the host's second
    /// each `GGA` went out in.
    fn sky_less_host(instrument: &Instrument) -> Vec<i64> {
        const SECONDS_PER_DAY: u64 = 86_400;
        instrument
            .received
            .iter()
            .flat_map(|(second, frames)| {
                frames
                    .iter()
                    .filter(|frame| sentence(frame, "GPGGA"))
                    .map(move |frame| {
                        i64::try_from(gga_second(frame)).unwrap()
                            - i64::try_from(second % SECONDS_PER_DAY).unwrap()
                    })
            })
            .collect()
    }

    /// The made-up sky's time is the second each of its frames goes out
    /// in.
    #[test]
    fn the_sky_reads_the_second_it_goes_out_in() {
        let interrupted = interrupted(
            Engine::new(&EngineSpec::Furuno(&GT_74)).unwrap(),
            0,
            false,
            4,
        );
        let offsets = sky_less_host(&interrupted.instrument);
        assert_eq!(offsets, [0; 4]);
    }

    /// A loop held up five seconds, the clock unstepped, as by a link
    /// slow to connect, skips the seconds it missed: the made-up sky's
    /// time does not fall behind the clock's.
    #[test]
    fn a_held_up_loop_keeps_the_sky_on_time() {
        let interrupted = interrupted(
            Engine::new(&EngineSpec::Furuno(&GT_74)).unwrap(),
            5,
            false,
            10,
        );
        let offsets = sky_less_host(&interrupted.instrument);
        assert!(offsets.len() >= 5, "{offsets:?}");
        assert!(
            offsets.windows(2).all(|pair| pair[0] == pair[1]),
            "{offsets:?}"
        );
    }
}
