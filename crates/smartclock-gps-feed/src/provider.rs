//! Where each second's record comes from: the provider the command line
//! names, inside the wrappers that relabel its time and announce a
//! leap second, and the thread that reads gpsd for a live one.

use anyhow::Context as _;
use anyhow::Result;
use anyhow::anyhow;
use smartclock_gps::engine::settings::Settings;
use smartclock_gps::record::Ecef;
use smartclock_gps::record::Position;
use smartclock_gps::record::Record;
use smartclock_gps::source::Clocking;
use smartclock_gps::source::Driver;
use smartclock_gps::source::Latest;
use smartclock_gps::source::Presence;
use smartclock_gps::source::Source;
use smartclock_gps::source::Tick;
use smartclock_gps::source::admit;
use smartclock_gps::source::replay::Assemble;
use smartclock_gps::source::replay::Replay;
use smartclock_gps::source::scenario::Fixed;
use smartclock_gps::source::scene;
use smartclock_gps::source::wrap::FakeTime;
use smartclock_gps::source::wrap::LeapSchedule;
use smartclock_gps::source::wrap::Relabel;
use smartclock_gps::time::GpsSeconds;
use smartclock_gps::time::UnixSeconds;
use smartclock_gps::units::Centimeters;
use smartclock_gps::units::Seconds;
use smartclock_gps_ublox::assembler::Assembler;
use smartclock_gps_ublox::nav::nav_sat_length;
use std::fs;
use std::io;
use std::io::Read as _;
use std::io::Write as _;
use std::net::TcpStream;
use std::sync::Arc;
use std::sync::Mutex;
use std::sync::PoisonError;
use std::thread;

use crate::cli::ProviderSpec;
use crate::event;
use crate::link::Backoff;

/// gpsd's port.
pub(crate) const GPSD_PORT: u16 = 2947;
/// What asks gpsd for its devices' raw output, byte for byte ("super-raw"
/// in gpsd's `?WATCH`).  It is a request to gpsd alone: nothing is sent
/// to the receiver behind it.
pub(crate) const WATCH: &[u8] = b"?WATCH={\"enable\":true,\"raw\":2}\n";
/// GPS less UTC since the end of 2016, which the made-up sky carries.
pub(crate) const UTC_OFFSET: Seconds<i8> = Seconds(18);
/// How long before its leap second a `leap:` sky starts, unless a fake
/// start says otherwise.
const LEAP_LEAD: Seconds<i64> = Seconds(60);
/// How much of gpsd's stream is read at once.
const GPSD_CHUNK: usize = 4096;

/// An assembler with room for a `NAV-SAT` of every satellite it can
/// list, of whatever system: the count is a byte.
type Ubx = Assembler<{ nav_sat_length(u8::MAX as usize) }>;

/// One of the providers the command line can name.
#[derive(Debug, Clone)]
pub(crate) enum Provider {
    /// The made-up sky, with or without a leap second.
    Scene(Fixed<'static>),
    /// A UBX recording, an epoch a tick, boxed for its assembler's
    /// buffer.
    Recording(Box<Replay<&'static [u8], Ubx>>),
    /// gpsd, whatever it last gave, boxed for its two records.
    Live(Box<Latest>),
}

impl Source for Provider {
    fn clocking(&self) -> Clocking {
        match self {
            Self::Scene(fixed) => fixed.clocking(),
            Self::Recording(replay) => replay.clocking(),
            Self::Live(latest) => latest.clocking(),
        }
    }

    fn record(&mut self, tick: Tick, record: &mut Record) -> Presence {
        match self {
            Self::Scene(fixed) => fixed.record(tick, record),
            Self::Recording(replay) => replay.record(tick, record),
            Self::Live(latest) => latest.record(tick, record),
        }
    }
}

/// The provider in its wrappers, each of which may change nothing.
pub(crate) type Nest = LeapSchedule<FakeTime<Provider>>;

/// What the driver and gpsd's reader share.
#[derive(Debug)]
pub(crate) struct Shared {
    /// The provider in its wrappers.
    pub(crate) nest: Nest,
    /// The tick the driver's clock is at, which a record from gpsd is
    /// stamped with.
    pub(crate) tick: Tick,
}

impl Shared {
    /// The live provider, if it is one.
    pub(crate) const fn latest(&self) -> Option<&Latest> {
        match self.nest.inner().inner() {
            Provider::Live(latest) => Some(&**latest),
            Provider::Scene(_) | Provider::Recording(_) => None,
        }
    }

    /// Delivers a record from gpsd, stamped with the driver's tick.
    fn deliver(&mut self, record: &Record) {
        let tick = self.tick;
        if let Provider::Live(latest) = self.nest.inner_mut().inner_mut() {
            *latest.slot() = *record;
            latest.commit(tick);
        }
    }
}

/// What the providers need beyond their records.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) enum Kind {
    /// The made-up sky, which acts on position hold and leaves the
    /// Earth-centered position to its holder.
    Scene,
    /// A recording, which ends.
    Recording,
    /// gpsd, read by a thread of its own.
    Live,
}

/// A provider built from the command line.
#[derive(Debug)]
pub(crate) struct Built {
    pub(crate) nest: Nest,
    pub(crate) kind: Kind,
}

/// The time `utc` as the made-up sky counts GPS time.
fn gps(utc: UnixSeconds) -> Result<GpsSeconds> {
    GpsSeconds::from_utc(utc, UTC_OFFSET).ok_or_else(|| anyhow!("a time past GPS time's range"))
}

/// Builds the provider `spec` names, `fake_start` relabeling its time,
/// `now` being the UTC second the driver starts in.  A recording is
/// read whole.  Refuses one the driver, paced to real time, cannot
/// serve.
pub(crate) fn build(
    spec: &ProviderSpec,
    fake_start: Option<UnixSeconds>,
    now: UnixSeconds,
) -> Result<Built> {
    let fake_start = fake_start.map(gps).transpose()?;
    let (provider, kind, relabel, leap) = match spec {
        ProviderSpec::Fixed => {
            let start = fake_start.map_or_else(|| gps(now), Ok)?;
            (scene_from(start)?, Kind::Scene, Relabel::Keep, None)
        }
        ProviderSpec::Leap(event) => {
            let start = match fake_start {
                Some(start) => start,
                None => event
                    .at
                    .checked_sub(LEAP_LEAD)
                    .ok_or_else(|| anyhow!("a leap second too near GPS time's start"))?,
            };
            (scene_from(start)?, Kind::Scene, Relabel::Keep, Some(*event))
        }
        ProviderSpec::Ubx(path) => {
            let bytes = fs::read(path).with_context(|| format!("reading {}", path.display()))?;
            (recording(bytes), Kind::Recording, relabel(fake_start), None)
        }
        ProviderSpec::Gpsd { .. } => (
            Provider::Live(Box::new(Latest::new())),
            Kind::Live,
            relabel(fake_start),
            None,
        ),
    };
    let name = spec.to_string();
    admit(&name, provider.clocking(), Driver::Paced).map_err(|refusal| anyhow!("{refusal}"))?;
    Ok(Built {
        nest: LeapSchedule::new(FakeTime::new(provider, relabel), leap),
        kind,
    })
}

const fn relabel(fake_start: Option<GpsSeconds>) -> Relabel {
    match fake_start {
        Some(start) => Relabel::Start(start),
        None => Relabel::Keep,
    }
}

/// The made-up sky, its first second `start`.  The sky is built once
/// and lives as long as the program.
fn scene_from(start: GpsSeconds) -> Result<Provider> {
    let week = start
        .week()
        .ok_or_else(|| anyhow!("a start before GPS time's"))?;
    let sky: &'static Record = Box::leak(Box::new(scene::sky(scene::PLACE, UTC_OFFSET, week)));
    Ok(Provider::Scene(Fixed::new(start, sky)))
}

/// A UBX recording, which lives as long as the program.
pub(crate) fn recording(bytes: Vec<u8>) -> Provider {
    Provider::Recording(Box::new(Replay::new(bytes.leak(), Ubx::new())))
}

/// Completes a record of the made-up sky: acts on position hold, and
/// works out the Earth-centered position.
pub(crate) fn complete_scene(record: &mut Record, settings: &Settings) {
    scene::hold(record, settings);
    record.derived.ecef = record.position.map(ecef);
}

/// WGS-84's semi-major axis in meters and its inverse flattening.
const AXIS: f64 = 6_378_137.0;
const INVERSE_FLATTENING: f64 = 298.257_223_563;
const MILLI_ARC_SECONDS_PER_DEGREE: f64 = 3_600_000.0;
const CENTIMETERS_PER_METER: f64 = 100.0;

/// A place's Earth-centered coordinates.
fn ecef(position: Position) -> Ecef {
    let flattening = 1.0 / INVERSE_FLATTENING;
    let eccentricity_squared = flattening * (2.0 - flattening);
    let latitude = (f64::from(position.latitude.0) / MILLI_ARC_SECONDS_PER_DEGREE).to_radians();
    let longitude = (f64::from(position.longitude.0) / MILLI_ARC_SECONDS_PER_DEGREE).to_radians();
    let height = f64::from(position.ellipsoid.0) / CENTIMETERS_PER_METER;
    let normal = AXIS / (1.0 - eccentricity_squared * latitude.sin().powi(2)).sqrt();
    #[expect(
        clippy::cast_possible_truncation,
        reason = "a place on Earth is within an i32 of centimeters"
    )]
    let centimeters = |meters: f64| Centimeters((meters * CENTIMETERS_PER_METER).round() as i32);
    Ecef {
        x: centimeters((normal + height) * latitude.cos() * longitude.cos()),
        y: centimeters((normal + height) * latitude.cos() * longitude.sin()),
        z: centimeters((normal * (1.0 - eccentricity_squared) + height) * latitude.sin()),
    }
}

/// Reads gpsd at `host` and `port` for ever, delivering each epoch's
/// record to `shared`, and connects again, after a wait that grows,
/// whenever the connection fails or ends.
pub(crate) fn spawn_gpsd(
    host: String,
    port: u16,
    shared: Arc<Mutex<Shared>>,
    mut backoff: Backoff,
) -> thread::JoinHandle<()> {
    thread::spawn(move || {
        let mut assembler = Ubx::new();
        loop {
            let ended = TcpStream::connect((host.as_str(), port)).and_then(|stream| {
                event(format_args!("gpsd: connected to {host}:{port}"));
                backoff.reset();
                read_gpsd(stream, &mut assembler, &shared)
            });
            match ended {
                Ok(()) => event(format_args!("gpsd: {host}:{port} closed the connection")),
                Err(error) => event(format_args!("gpsd: {host}:{port}: {error}")),
            }
            assembler.reset();
            let wait = backoff.next_wait();
            event(format_args!(
                "gpsd: connecting again in {} s",
                wait.as_secs()
            ));
            thread::sleep(wait);
        }
    })
}

/// Asks gpsd for its raw output and delivers each epoch's record, until
/// the connection ends.  What gpsd says of itself in JSON is not UBX,
/// and the deframer passes over it.
fn read_gpsd(mut stream: TcpStream, assembler: &mut Ubx, shared: &Mutex<Shared>) -> io::Result<()> {
    stream.write_all(WATCH)?;
    let mut chunk = [0; GPSD_CHUNK];
    let mut record = Record::EMPTY;
    loop {
        let count = stream.read(&mut chunk)?;
        if count == 0 {
            return Ok(());
        }
        for &byte in &chunk[..count] {
            if assembler.push(byte, &mut record) {
                shared
                    .lock()
                    .unwrap_or_else(PoisonError::into_inner)
                    .deliver(&record);
                record = Record::EMPTY;
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::Built;
    use super::Kind;
    use super::Provider;
    use super::Shared;
    use super::WATCH;
    use super::build;
    use super::complete_scene;
    use super::recording;
    use super::spawn_gpsd;
    use crate::cli::ProviderSpec;
    use crate::link::Backoff;
    use smartclock_gps::engine::Engine;
    use smartclock_gps::engine::Options;
    use smartclock_gps::engine::identity::VP_EIGHT;
    use smartclock_gps::record::Fix;
    use smartclock_gps::record::Record;
    use smartclock_gps::record::field::UNFILLED;
    use smartclock_gps::record::field::changed;
    use smartclock_gps::source::Clocking;
    use smartclock_gps::source::Presence;
    use smartclock_gps::source::Source;
    use smartclock_gps::source::Tick;
    use smartclock_gps::source::scene::DECLARATION as SCENE;
    use smartclock_gps::source::wrap::FakeTime;
    use smartclock_gps::source::wrap::LeapSchedule;
    use smartclock_gps::source::wrap::Relabel;
    use smartclock_gps::time::DateTime;
    use smartclock_gps::time::GpsSeconds;
    use smartclock_gps::time::GpsWeek;
    use smartclock_gps::time::LeapChange;
    use smartclock_gps::time::LeapEvent;
    use smartclock_gps::units::Milliseconds;
    use smartclock_gps::units::Nanoseconds;
    use smartclock_gps::units::Seconds;
    use smartclock_gps_ublox::frame::MessageId;
    use smartclock_gps_ublox::frame::OVERHEAD;
    use smartclock_gps_ublox::frame::encode;
    use smartclock_gps_ublox::message::NAV_EOE;
    use smartclock_gps_ublox::message::NAV_PVT;
    use smartclock_gps_ublox::message::NAV_TIMEGPS;
    use std::io::Read as _;
    use std::io::Write as _;
    use std::net::TcpListener;
    use std::path::Path;
    use std::sync::Arc;
    use std::sync::Mutex;
    use std::time::Duration;
    use std::time::Instant;

    /// The week of the recording made here, and the time of week of its
    /// first epoch, noon of its Saturday, in milliseconds.
    const WEEK: u16 = 2400;
    const NOON: u32 = (6 * 24 + 12) * 3600 * 1000;
    const MILLISECONDS_PER_SECOND: u32 = 1000;
    /// GPS less UTC in it.
    const LEAP_SECONDS: i8 = 18;
    /// The epochs in it.
    const EPOCHS: u32 = 3;
    /// How long a test waits for gpsd's reader.
    const PATIENCE: Duration = Duration::from_secs(5);

    fn frame(id: MessageId, payload: &[u8]) -> Vec<u8> {
        let mut out = vec![0; OVERHEAD + payload.len()];
        assert_eq!(encode(id, payload, &mut out), Ok(out.len()));
        out
    }

    /// A payload of `N` bytes with `fields` at their offsets.
    fn payload<const N: usize>(fields: &[(usize, &[u8])]) -> [u8; N] {
        let mut out = [0; N];
        for &(offset, field) in fields {
            out[offset..offset + field.len()].copy_from_slice(field);
        }
        out
    }

    /// One epoch of a u-blox receiver with a 3D fix: `NAV-PVT` (UBX-13003221
    /// R28 pp. 375-378), `NAV-TIMEGPS` (p. 397) with its time of week,
    /// week and leap seconds valid, and `NAV-EOE` (p. 363).
    fn epoch(time_of_week: u32) -> Vec<u8> {
        let tow = time_of_week.to_le_bytes();
        let pvt: [u8; 92] = payload(&[
            (0, &tow),
            (20, &[3, 0x01]),
            (24, &(-1_219_978_333_i32).to_le_bytes()),
            (28, &373_255_833_i32.to_le_bytes()),
            (32, &22_995_i32.to_le_bytes()),
            (36, &55_000_i32.to_le_bytes()),
        ]);
        let time: [u8; 16] = payload(&[
            (0, &tow),
            (8, &WEEK.to_le_bytes()),
            (10, &LEAP_SECONDS.to_le_bytes()),
            (11, &[0x07]),
        ]);
        [
            frame(NAV_PVT, &pvt),
            frame(NAV_TIMEGPS, &time),
            frame(NAV_EOE, &tow),
        ]
        .concat()
    }

    /// A recording of [`EPOCHS`] epochs a second apart, with gpsd's JSON
    /// greeting in front, as `gpspipe -R` can leave it.
    fn capture() -> Vec<u8> {
        let mut bytes = b"{\"class\":\"VERSION\",\"release\":\"3.25\"}\r\n".to_vec();
        for second in 0..EPOCHS {
            bytes.extend(epoch(NOON + second * MILLISECONDS_PER_SECOND));
        }
        bytes
    }

    fn noon() -> GpsSeconds {
        GpsSeconds::nearest(GpsWeek::new(WEEK), Milliseconds(NOON), Nanoseconds(0)).unwrap()
    }

    fn nest(provider: Provider, relabel: Relabel) -> Shared {
        Shared {
            nest: LeapSchedule::new(FakeTime::new(provider, relabel), None),
            tick: Tick(0),
        }
    }

    /// A UBX recording gives a record an epoch a tick, each a second on,
    /// and nothing past its end.
    #[test]
    fn a_ubx_recording_drives_records() {
        let mut provider = recording(capture());
        assert_eq!(provider.clocking(), Clocking::Driven);
        let mut record = Record::EMPTY;
        for tick in 0..EPOCHS {
            assert_eq!(
                provider.record(Tick(u64::from(tick)), &mut record),
                Presence::Fresh
            );
            assert_eq!(record.fix, Fix::ThreeD);
            assert_eq!(
                record.time.map(|time| time.seconds),
                Some(noon() + Seconds(i64::from(tick)))
            );
            assert_eq!(record.utc_offset, Some(Seconds(LEAP_SECONDS)));
        }
        assert_eq!(
            provider.record(Tick(u64::from(EPOCHS)), &mut record),
            Presence::Silent
        );

        // Relabeled: the first epoch is the fake start.
        let start = noon() + Seconds(86_400);
        let mut shared = nest(recording(capture()), Relabel::Start(start));
        assert_eq!(shared.nest.record(Tick(1), &mut record), Presence::Fresh);
        assert_eq!(
            record.time.map(|time| time.seconds),
            Some(start + Seconds(1))
        );
    }

    /// A recording from a file, as the command line names one.
    #[test]
    fn a_ubx_file_is_read_whole() {
        let path = std::env::temp_dir().join(format!(
            "smartclock-gps-feed-test-{}.ubx",
            std::process::id()
        ));
        std::fs::write(&path, capture()).unwrap();
        let Built { mut nest, kind } = build(
            &ProviderSpec::Ubx(path.clone()),
            None,
            DateTime::new(2026, 1, 1, 0, 0, 0).unwrap().to_unix(),
        )
        .unwrap();
        std::fs::remove_file(&path).unwrap();
        assert_eq!(kind, Kind::Recording);
        let mut record = Record::EMPTY;
        assert_eq!(nest.record(Tick(2), &mut record), Presence::Fresh);
        assert_eq!(
            record.time.map(|time| time.seconds),
            Some(noon() + Seconds(2))
        );
    }

    /// The made-up sky starts at the driver's second, or at the fake
    /// start; a `leap:` sky a minute before its leap second, which it
    /// announces.
    #[test]
    fn the_scene_starts_where_it_is_told() {
        let now = DateTime::new(2026, 11, 15, 12, 0, 0).unwrap().to_unix();
        let at_now = GpsSeconds::from_utc(now, Seconds(18)).unwrap();
        let mut record = Record::EMPTY;

        let Built { mut nest, kind } = build(&ProviderSpec::Fixed, None, now).unwrap();
        assert_eq!(kind, Kind::Scene);
        assert_eq!(nest.record(Tick(5), &mut record), Presence::Fresh);
        assert_eq!(record.time.unwrap().seconds, at_now + Seconds(5));

        let fake = DateTime::new(2030, 6, 1, 0, 0, 0).unwrap().to_unix();
        let Built { mut nest, .. } = build(&ProviderSpec::Fixed, Some(fake), now).unwrap();
        assert_eq!(nest.record(Tick(0), &mut record), Presence::Fresh);
        assert_eq!(
            record.time.unwrap().seconds,
            GpsSeconds::from_utc(fake, Seconds(18)).unwrap()
        );

        let midnight = DateTime::new(2028, 1, 1, 0, 0, 0).unwrap().to_unix();
        let leap = LeapEvent::at_day_end(midnight, Seconds(18), LeapChange::Insert).unwrap();
        let Built { mut nest, .. } = build(&ProviderSpec::Leap(leap), None, now).unwrap();
        assert_eq!(nest.record(Tick(59), &mut record), Presence::Fresh);
        assert_eq!(record.time.unwrap().seconds, leap.at - Seconds(1));
        assert_eq!(record.leap, Some(leap));
        assert_eq!(record.utc_offset, Some(Seconds(18)));
        assert_eq!(nest.record(Tick(60), &mut record), Presence::Fresh);
        assert_eq!(record.utc_offset, Some(Seconds(19)));
    }

    /// The made-up sky, as the feed completes it, fills what it
    /// declares and nothing else, all the images use; it acts on the
    /// settings it declares and is excused the rest, none for nothing.
    #[test]
    fn the_scene_fills_what_it_declares() {
        let root = Path::new(env!("CARGO_MANIFEST_DIR"));
        let exceptions =
            std::fs::read_to_string(root.join("../../docs/firmware/gps-field-exceptions.tsv"))
                .unwrap();
        let shortfall = SCENE.shortfall(&exceptions);
        assert!(shortfall.is_empty(), "shortfall: {shortfall:?}");
        let exceptions =
            std::fs::read_to_string(root.join("../../docs/firmware/gps-setting-exceptions.tsv"))
                .unwrap();
        let unacted = SCENE.unacted(&exceptions);
        assert!(unacted.is_empty(), "unacted: {unacted:?}");
        let needless = SCENE.needless(&exceptions);
        assert!(needless.is_empty(), "needless: {needless:?}");

        let now = DateTime::new(2027, 12, 31, 23, 0, 0).unwrap().to_unix();
        let midnight = DateTime::new(2028, 1, 1, 0, 0, 0).unwrap().to_unix();
        let leap = LeapEvent::at_day_end(midnight, Seconds(18), LeapChange::Insert).unwrap();
        let Built { mut nest, .. } = build(&ProviderSpec::Leap(leap), None, now).unwrap();
        let engine = Engine::new(&VP_EIGHT, Options::DEFAULT).unwrap();
        let mut record = UNFILLED;
        assert_eq!(nest.record(Tick(0), &mut record), Presence::Fresh);
        complete_scene(&mut record, &engine.settings());
        let undeclared = SCENE.undeclared(&UNFILLED, &record);
        assert!(undeclared.is_empty(), "undeclared: {undeclared:?}");
        let unshown = SCENE.unshown(changed(&UNFILLED, &record));
        assert!(unshown.is_empty(), "unshown: {unshown:?}");
    }

    /// gpsd's reader asks for raw output, makes records of what comes,
    /// stamps each with the driver's tick, and connects again when
    /// gpsd goes.
    #[test]
    fn gpsd_is_read_and_read_again() {
        let listener = TcpListener::bind("127.0.0.1:0").unwrap();
        let port = listener.local_addr().unwrap().port();
        let shared = Arc::new(Mutex::new(nest(
            Provider::Live(Box::new(smartclock_gps::source::Latest::new())),
            Relabel::Keep,
        )));
        shared.lock().unwrap().tick = Tick(7);
        spawn_gpsd(
            "127.0.0.1".to_owned(),
            port,
            Arc::clone(&shared),
            Backoff::new(Duration::from_millis(10), Duration::from_millis(10)),
        );

        for _ in 0..2 {
            let (mut gpsd, _) = listener.accept().unwrap();
            let mut asked = vec![0; WATCH.len()];
            gpsd.read_exact(&mut asked).unwrap();
            assert_eq!(asked, WATCH);
            gpsd.write_all(&capture()).unwrap();
            let started = Instant::now();
            loop {
                let arrival = shared.lock().unwrap().latest().unwrap().arrival();
                if arrival == Some(Tick(7)) {
                    break;
                }
                assert!(started.elapsed() < PATIENCE, "no record from gpsd");
                std::thread::sleep(Duration::from_millis(10));
            }
            let mut record = Record::EMPTY;
            let mut guard = shared.lock().unwrap();
            assert_eq!(guard.nest.record(Tick(8), &mut record), Presence::Fresh);
            assert_eq!(record.fix, Fix::ThreeD);
            // Forget it, to see the next connection's.
            *guard = nest(
                Provider::Live(Box::new(smartclock_gps::source::Latest::new())),
                Relabel::Keep,
            );
            guard.tick = Tick(7);
            drop(guard);
            drop(gpsd);
        }
    }
}
