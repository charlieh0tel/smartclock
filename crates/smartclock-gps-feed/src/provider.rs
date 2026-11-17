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
use std::time::Duration;

use crate::cli::GpsdSpec;
use crate::cli::ProviderSpec;
use crate::event;
use crate::link::Backoff;
use crate::link::connect;
use crate::link::ready;
use crate::link::timed_out;

/// gpsd's port.
pub(crate) const GPSD_PORT: u16 = 2947;
/// How long gpsd may send nothing before it is taken to have gone, and
/// connected to again.  A device it reads sends every second.
pub(crate) const GPSD_SILENCE: Duration = Duration::from_secs(10);
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
    /// The UTC second of the newest record from gpsd, by its own time
    /// and offset, before any wrapper relabels it.
    pub(crate) delivered: Option<UnixSeconds>,
}

impl Shared {
    /// `nest`, at tick 0, nothing delivered.
    pub(crate) const fn new(nest: Nest) -> Self {
        Self {
            nest,
            tick: Tick(0),
            delivered: None,
        }
    }

    /// The live provider, if it is one.
    pub(crate) const fn latest(&self) -> Option<&Latest> {
        match self.nest.inner().inner() {
            Provider::Live(latest) => Some(&**latest),
            Provider::Scene(_) | Provider::Recording(_) => None,
        }
    }

    /// Delivers a record from gpsd, stamped with the driver's tick.
    pub(crate) fn deliver(&mut self, record: &Record) {
        let tick = self.tick;
        if let Provider::Live(latest) = self.nest.inner_mut().inner_mut() {
            *latest.slot() = *record;
            latest.commit(tick);
            self.delivered = record
                .time
                .zip(record.utc_offset)
                .and_then(|(time, offset)| time.seconds.to_utc(offset));
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

/// What `spec` names, of the kinds of provider.
pub(crate) const fn kind(spec: &ProviderSpec) -> Kind {
    match spec {
        ProviderSpec::Fixed | ProviderSpec::Leap(_) => Kind::Scene,
        ProviderSpec::Ubx(_) => Kind::Recording,
        ProviderSpec::Gpsd(_) => Kind::Live,
    }
}

/// The time `utc` as the made-up sky counts GPS time.
fn gps(utc: UnixSeconds) -> Result<GpsSeconds> {
    GpsSeconds::from_utc(utc, UTC_OFFSET).ok_or_else(|| anyhow!("a time past GPS time's range"))
}

/// Builds the provider `spec` names, `fake_start` relabeling its time,
/// `now` being the UTC second the first tick goes out in.  A recording is
/// read whole.  Refuses one the driver, paced to real time, cannot
/// serve.
///
/// A fake start is a UTC second.  The made-up sky's GPS time is the
/// one that UTC second is under its own offset; a recording's or
/// gpsd's, the one it is under theirs, frozen at the first record's
/// that has one and taken to be the sky's until then.
pub(crate) fn build(
    spec: &ProviderSpec,
    fake_start: Option<UnixSeconds>,
    now: UnixSeconds,
) -> Result<Built> {
    let (provider, relabel, leap) = match spec {
        ProviderSpec::Fixed => {
            let start = gps(fake_start.unwrap_or(now))?;
            (scene_from(start)?, Relabel::Keep, None)
        }
        ProviderSpec::Leap(event) => {
            let start = match fake_start.map(gps).transpose()? {
                Some(start) => start,
                None => event
                    .at
                    .checked_sub(LEAP_LEAD)
                    .ok_or_else(|| anyhow!("a leap second too near GPS time's start"))?,
            };
            (scene_from(start)?, Relabel::Keep, Some(*event))
        }
        ProviderSpec::Ubx(path) => {
            let bytes = fs::read(path).with_context(|| format!("reading {}", path.display()))?;
            (recording(bytes), relabel(fake_start), None)
        }
        ProviderSpec::Gpsd(_) => (
            Provider::Live(Box::new(Latest::new())),
            relabel(fake_start),
            None,
        ),
    };
    let name = spec.to_string();
    admit(&name, provider.clocking(), Driver::Paced).map_err(|refusal| anyhow!("{refusal}"))?;
    Ok(Built {
        nest: LeapSchedule::new(FakeTime::new(provider, relabel), leap),
        kind: kind(spec),
    })
}

/// What a fake start makes of a recording's or gpsd's time.
const fn relabel(fake_start: Option<UnixSeconds>) -> Relabel {
    match fake_start {
        Some(start) => Relabel::StartUtc {
            start,
            assumed: UTC_OFFSET,
        },
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

/// Completes a record of the made-up sky: acts on the engine's settings
/// ([`scene::act`]), and works out the Earth-centered position.
pub(crate) fn complete_scene(record: &mut Record, settings: &Settings) {
    scene::act(record, settings);
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

/// What asks gpsd for the raw output of `device`, or of every device it
/// has, byte for byte ("super-raw" in gpsd's `?WATCH`).  It is a request
/// to gpsd alone: nothing is sent to the receiver behind it.  The
/// device has no character JSON would have to escape (`cli`).
pub(crate) fn watch(device: Option<&str>) -> Vec<u8> {
    let device = device.map_or_else(String::new, |device| format!(",\"device\":\"{device}\""));
    format!("?WATCH={{\"enable\":true,\"raw\":2{device}}}\n").into_bytes()
}

/// Reads gpsd as `spec` names it for ever, delivering each epoch's
/// record to `shared`, and connects again, after a wait that grows,
/// whenever the connection fails or ends, or gpsd sends nothing for
/// `silence`.  A connection is tried for a time, and probed when idle,
/// as the link to the instrument is.
pub(crate) fn spawn_gpsd(
    spec: GpsdSpec,
    shared: Arc<Mutex<Shared>>,
    mut backoff: Backoff,
    silence: Duration,
) -> thread::JoinHandle<()> {
    thread::spawn(move || {
        let mut assembler = Ubx::new();
        let watch = watch(spec.device.as_deref());
        loop {
            let ended = connect((spec.host.as_str(), spec.port)).and_then(|stream| {
                event(format_args!("gpsd: connected to {spec}"));
                backoff.reset();
                ready(&stream)?;
                stream.set_read_timeout(Some(silence))?;
                read_gpsd(stream, &watch, &mut assembler, &shared)
            });
            match ended {
                Ok(()) => event(format_args!("gpsd: {spec} closed the connection")),
                Err(error) if timed_out(&error) => event(format_args!(
                    "gpsd: {spec} sent nothing for {} s",
                    silence.as_secs_f64()
                )),
                Err(error) => event(format_args!("gpsd: {spec}: {error}")),
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
fn read_gpsd(
    mut stream: TcpStream,
    watch: &[u8],
    assembler: &mut Ubx,
    shared: &Mutex<Shared>,
) -> io::Result<()> {
    stream.write_all(watch)?;
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
pub(crate) mod tests {
    use super::Built;
    use super::Kind;
    use super::Provider;
    use super::Shared;
    use super::UTC_OFFSET;
    use super::build;
    use super::complete_scene;
    use super::recording;
    use super::spawn_gpsd;
    use super::watch;
    use crate::cli::GpsdSpec;
    use crate::cli::ProviderSpec;
    use crate::link::Backoff;
    use smartclock_gps::engine::Engine;
    use smartclock_gps::engine::Options;
    use smartclock_gps::engine::Output;
    use smartclock_gps::engine::event::Event;
    use smartclock_gps::engine::identity::VP_EIGHT;
    use smartclock_gps::engine::settings::Setting;
    use smartclock_gps::engine::settings::SettingSet;
    use smartclock_gps::engine::settings::Settings;
    use smartclock_gps::oncore;
    use smartclock_gps::record::Fix;
    use smartclock_gps::record::Record;
    use smartclock_gps::record::field::Declaration;
    use smartclock_gps::record::field::IMAGE_SETS;
    use smartclock_gps::record::field::UNFILLED;
    use smartclock_gps::record::field::changed;
    use smartclock_gps::source::Clocking;
    use smartclock_gps::source::Presence;
    use smartclock_gps::source::Source;
    use smartclock_gps::source::Tick;
    use smartclock_gps::source::scene;
    use smartclock_gps::source::scene::DECLARATION as SCENE;
    use smartclock_gps::source::wrap::FakeTime;
    use smartclock_gps::source::wrap::LeapSchedule;
    use smartclock_gps::source::wrap::Relabel;
    use smartclock_gps::time::DateTime;
    use smartclock_gps::time::GpsSeconds;
    use smartclock_gps::time::GpsWeek;
    use smartclock_gps::time::LeapChange;
    use smartclock_gps::time::LeapEvent;
    use smartclock_gps::time::UnixSeconds;
    use smartclock_gps::units::MilliArcSeconds;
    use smartclock_gps::units::Milliseconds;
    use smartclock_gps::units::Nanoseconds;
    use smartclock_gps::units::Seconds;
    use smartclock_gps_ublox::assembler;
    use smartclock_gps_ublox::assembler::DECLARATION as UBLOX;
    use smartclock_gps_ublox::frame::MessageId;
    use smartclock_gps_ublox::frame::OVERHEAD;
    use smartclock_gps_ublox::frame::encode;
    use smartclock_gps_ublox::message::NAV_EOE;
    use smartclock_gps_ublox::message::NAV_PVT;
    use smartclock_gps_ublox::message::NAV_TIMEGPS;
    use std::io::Read as _;
    use std::io::Write as _;
    use std::net::TcpListener;
    use std::net::TcpStream;
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
    /// GPS less UTC in a recording made before the end of 2016.
    const OLD_LEAP_SECONDS: i8 = 17;
    /// The epochs in it.
    pub(crate) const EPOCHS: u32 = 3;
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
    /// week and leap seconds, `leap_seconds`, valid, and `NAV-EOE`
    /// (p. 363).
    fn epoch(time_of_week: u32, leap_seconds: i8) -> Vec<u8> {
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
            (10, &leap_seconds.to_le_bytes()),
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
    pub(crate) fn capture() -> Vec<u8> {
        capture_of(LEAP_SECONDS)
    }

    /// [`capture`], GPS less UTC in it `leap_seconds`.
    fn capture_of(leap_seconds: i8) -> Vec<u8> {
        let mut bytes = b"{\"class\":\"VERSION\",\"release\":\"3.25\"}\r\n".to_vec();
        for second in 0..EPOCHS {
            bytes.extend(epoch(NOON + second * MILLISECONDS_PER_SECOND, leap_seconds));
        }
        bytes
    }

    fn noon() -> GpsSeconds {
        GpsSeconds::nearest(GpsWeek::new(WEEK), Milliseconds(NOON), Nanoseconds(0)).unwrap()
    }

    fn nest(provider: Provider, relabel: Relabel) -> Shared {
        Shared::new(LeapSchedule::new(FakeTime::new(provider, relabel), None))
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

    /// Builds the provider of a UBX file holding `bytes`, as the command
    /// line names one, `name` telling the test's file apart.
    fn from_file(bytes: &[u8], name: &str, fake_start: Option<UnixSeconds>) -> Built {
        let path = std::env::temp_dir().join(format!(
            "smartclock-gps-feed-test-{name}-{}.ubx",
            std::process::id()
        ));
        std::fs::write(&path, bytes).unwrap();
        let built = build(
            &ProviderSpec::Ubx(path.clone()),
            fake_start,
            DateTime::new(2026, 1, 1, 0, 0, 0).unwrap().to_unix(),
        )
        .unwrap();
        std::fs::remove_file(&path).unwrap();
        built
    }

    /// A fake start is the UTC second a recording's first epoch reads,
    /// under the recording's own GPS less UTC, not the made-up sky's.
    #[test]
    fn a_fake_start_reads_as_given_under_the_sources_offset() {
        let fake = DateTime::new(2027, 12, 31, 23, 58, 0).unwrap();
        for leap_seconds in [LEAP_SECONDS, OLD_LEAP_SECONDS] {
            let Built { mut nest, .. } = from_file(
                &capture_of(leap_seconds),
                &format!("fake-{leap_seconds}"),
                Some(fake.to_unix()),
            );
            let mut record = Record::EMPTY;
            for tick in 0..EPOCHS {
                assert_eq!(
                    nest.record(Tick(u64::from(tick)), &mut record),
                    Presence::Fresh
                );
                assert_eq!(record.utc_offset, Some(Seconds(leap_seconds)));
                assert_eq!(
                    DateTime::of(record.time.unwrap().seconds, Seconds(leap_seconds), None)
                        .unwrap(),
                    DateTime::new(2027, 12, 31, 23, 58, u8::try_from(tick).unwrap()).unwrap(),
                    "{leap_seconds}"
                );
            }
        }
    }

    /// A recording from a file, as the command line names one.
    #[test]
    fn a_ubx_file_is_read_whole() {
        let Built { mut nest, kind } = from_file(&capture(), "whole", None);
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

    /// A fake gpsd on loopback, and the reader of it, `device` its
    /// device, taken to have gone after `silence`.
    fn fake_gpsd(device: Option<&str>, silence: Duration) -> (TcpListener, Arc<Mutex<Shared>>) {
        let listener = TcpListener::bind("127.0.0.1:0").unwrap();
        let port = listener.local_addr().unwrap().port();
        let shared = Arc::new(Mutex::new(nest(
            Provider::Live(Box::new(smartclock_gps::source::Latest::new())),
            Relabel::Keep,
        )));
        shared.lock().unwrap().tick = Tick(7);
        spawn_gpsd(
            GpsdSpec {
                host: "127.0.0.1".to_owned(),
                port,
                device: device.map(str::to_owned),
            },
            Arc::clone(&shared),
            Backoff::new(Duration::from_millis(10), Duration::from_millis(10)),
            silence,
        );
        (listener, shared)
    }

    /// Accepts the reader's next connection, within patience.
    fn accept(listener: &TcpListener) -> TcpStream {
        listener.set_nonblocking(true).unwrap();
        let started = Instant::now();
        loop {
            match listener.accept() {
                Ok((stream, _)) => {
                    stream.set_nonblocking(false).unwrap();
                    return stream;
                }
                Err(error) if error.kind() == std::io::ErrorKind::WouldBlock => {
                    assert!(started.elapsed() < PATIENCE, "never connected");
                    std::thread::sleep(Duration::from_millis(10));
                }
                Err(error) => panic!("{error}"),
            }
        }
    }

    /// What the reader asked gpsd for.
    fn asked(gpsd: &mut TcpStream) -> Vec<u8> {
        let mut asked = Vec::new();
        let mut byte = [0; 1];
        while asked.last() != Some(&b'\n') {
            gpsd.read_exact(&mut byte).unwrap();
            asked.push(byte[0]);
        }
        asked
    }

    #[test]
    fn watch_names_the_device_given() {
        assert_eq!(watch(None), b"?WATCH={\"enable\":true,\"raw\":2}\n");
        assert_eq!(
            watch(Some("/dev/ttyACM0")),
            b"?WATCH={\"enable\":true,\"raw\":2,\"device\":\"/dev/ttyACM0\"}\n"
        );
    }

    /// A gpsd that holds its connection open and sends nothing, as one
    /// whose host has gone without a word, is connected to again; the
    /// device named is asked for each time.
    #[test]
    fn a_silent_gpsd_is_connected_to_again() {
        const DEVICE: &str = "/dev/ttyACM0";
        let (listener, _shared) = fake_gpsd(Some(DEVICE), Duration::from_millis(200));
        let mut first = accept(&listener);
        assert_eq!(asked(&mut first), watch(Some(DEVICE)));
        let mut second = accept(&listener);
        assert_eq!(asked(&mut second), watch(Some(DEVICE)));
        drop(first);
    }

    /// gpsd's reader asks for raw output, makes records of what comes,
    /// stamps each with the driver's tick, and connects again when
    /// gpsd goes.
    #[test]
    fn gpsd_is_read_and_read_again() {
        let (listener, shared) = fake_gpsd(None, PATIENCE);
        for _ in 0..2 {
            let mut gpsd = accept(&listener);
            assert_eq!(asked(&mut gpsd), watch(None));
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

    /// What an engine sends, dropped.
    struct Dropped;

    impl Output for Dropped {
        fn send(&mut self, _frame: &[u8]) {}
    }

    /// Each setting the images set that a provider acts on, driven
    /// through an engine by its command, changes the provider's record
    /// as a receiver set so would have it (`docs/firmware/gps.md`,
    /// "Every path to a setting"); and each provider acts on no setting
    /// the images set that this does not drive.
    #[test]
    fn each_setting_the_images_set_reaches_the_providers() {
        /// The mask angle the engine is set to, in degrees.
        const MASK: i8 = 20;
        /// A satellite of the scene's sky to ignore, and `@@Am`'s data
        /// for it: the constant byte, then the mask, bit 0 satellite 1
        /// (VP p. 42).
        const IGNORED: u8 = 3;
        const IGNORE_DATA: [u8; 5] = [0, 0, 0, 0, 1 << (IGNORED - 1)];
        /// The latitude the position is held at.
        const HELD_LATITUDE: MilliArcSeconds = MilliArcSeconds(123_456_789);
        /// What a provider does to a record.
        type Act = fn(&mut Record, &Settings);
        /// A setting, the command that sets it, and whether a record
        /// shows it acted on.
        type Case = (Setting, oncore::MessageId, Vec<u8>, fn(&Record) -> bool);
        let hold_position = [
            &HELD_LATITUDE.0.to_be_bytes()[..],
            &(-98_765_432_i32).to_be_bytes()[..],
            &1234_i32.to_be_bytes()[..],
            &[1],
        ]
        .concat();
        let cases: [Case; 4] = [
            (
                Setting::MaskAngle,
                oncore::AG_MASK_ANGLE,
                vec![MASK.cast_unsigned()],
                |record| {
                    record
                        .satellites
                        .iter()
                        .all(|satellite| !satellite.used || satellite.elevation.0 >= MASK)
                },
            ),
            (
                Setting::IgnoreList,
                oncore::AM_IGNORE_LIST,
                IGNORE_DATA.to_vec(),
                |record| {
                    record
                        .satellites
                        .iter()
                        .any(|satellite| satellite.prn.get() == IGNORED && !satellite.used)
                },
            ),
            (
                Setting::HeldPosition,
                oncore::AS_HOLD_POSITION,
                hold_position,
                |record| {
                    record
                        .position
                        .is_some_and(|position| position.latitude == HELD_LATITUDE)
                },
            ),
            (
                Setting::PositionHold,
                oncore::AT_POSITION_HOLD,
                vec![1],
                |record| record.fix == Fix::TimeOnly,
            ),
        ];
        let driven = cases
            .iter()
            .fold(SettingSet::EMPTY, |set, case| set.with(case.0));
        let providers: [(Declaration, Act); 2] = [(SCENE, complete_scene), (UBLOX, assembler::act)];
        let sky = scene::sky(scene::PLACE, UTC_OFFSET, GpsWeek::new(WEEK));
        for (declaration, act) in providers {
            let undriven: Vec<Setting> = IMAGE_SETS
                .iter()
                .filter(|setting| declaration.acts.contains(*setting) && !driven.contains(*setting))
                .collect();
            assert!(
                undriven.is_empty(),
                "{}: undriven: {undriven:?}",
                declaration.name
            );
            let mut engine = Engine::new(&VP_EIGHT, Options::DEFAULT).unwrap();
            for (setting, id, data, shown) in &cases {
                let mut frame = vec![0; oncore::command_length(*id).unwrap()];
                oncore::encode(*id, data, &mut frame).unwrap();
                let changed = engine
                    .receive(&frame, &sky, Milliseconds(0), &mut Dropped)
                    .any(|event| {
                        matches!(event, Event::SettingsChanged(changed) if changed.contains(*setting))
                    });
                assert!(changed, "{setting:?}");
                if !declaration.acts.contains(*setting) {
                    continue;
                }
                let mut record = sky;
                assert!(!shown(&record), "{}: {setting:?} before", declaration.name);
                // The engine refuses `@@As` while the position is held,
                // and the held position shows only in position hold: it
                // is acted on as the hold that follows it will have it.
                let mut settings = engine.settings();
                settings.position_hold |= *setting == Setting::HeldPosition;
                act(&mut record, &settings);
                assert!(shown(&record), "{}: {setting:?}", declaration.name);
            }
        }
    }
}
