use std::collections::BTreeMap;
use std::format;
use std::fs;
use std::path::Path;
use std::string::String;
use std::string::ToString;
use std::vec::Vec;

use super::Engine;
use super::GT_74;
use super::GT_74_WITHOUT_TIME_RAIM;
use super::Identity;
use crate::engine::Output;
use crate::engine::event::Event;
use crate::engine::settings::Height;
use crate::engine::settings::HeightKind;
use crate::engine::settings::Location;
use crate::engine::settings::PulseMode;
use crate::engine::settings::Setting;
use crate::engine::settings::SettingSet;
use crate::record::Antenna;
use crate::record::Fault;
use crate::record::FaultSet;
use crate::record::GpsTime;
use crate::record::Raim;
use crate::record::RaimAbility;
use crate::record::RaimSolution;
use crate::record::Record;
use crate::record::Timing;
use crate::record::Velocity;
use crate::source::scene;
use crate::time::DateTime;
use crate::time::GpsSeconds;
use crate::time::LeapChange;
use crate::time::LeapEvent;
use crate::units::Centimeters;
use crate::units::CentimetersPerSecond;
use crate::units::HundredNanoseconds;
use crate::units::MilliArcSeconds;
use crate::units::Nanoseconds;
use crate::units::Prn;
use crate::units::PrnSet;
use crate::units::Seconds;
use crate::units::TenthDegrees;
use crate::units::TenthDegreesEast;

/// The Z3815A's image, from the crate's directory.
const IMAGE: &str = "../../third_party/firmware/images/z3815a-4010.bin";
/// Its table of the sentences it decodes, `nmea_sentences`, and its
/// entry, `nmea_sentence_entry`: a pointer to the name, the length and
/// the count of field ends, and a pointer to the fields' entries, each
/// `nmea_field_entry`: the field's index and length, and two pointers
/// (`third_party/firmware/rewritten/z3815a-4010/types/smartclock.h`).
const DECODED: usize = 0x5e32a;
const ENTRY: usize = 10;
const FIELD_ENTRY: usize = 10;
/// `GSV`'s most field ends, and their multiple (`nmea_sentence_fields_allowed`).
const GSV_FIELDS: usize = 20;
const GSV_MULTIPLE: usize = 4;
/// GPS less UTC in the records here.
const OFFSET: Seconds<i8> = Seconds(18);

/// One sentence of the image's table.
struct Taken {
    /// Its bytes, `$` to LF, where positive.
    length: i8,
    /// Its field ends where positive.
    fields: i8,
    /// Each field it decodes, by index: its length, where positive.
    lengths: Vec<(usize, i8)>,
}

/// The image's table, by name.
fn table() -> BTreeMap<String, Taken> {
    let image = fs::read(Path::new(env!("CARGO_MANIFEST_DIR")).join(IMAGE)).expect("the image");
    let pointer = |at: usize| {
        let bytes = image[at..at + 4].try_into().expect("four bytes");
        usize::try_from(u32::from_be_bytes(bytes)).expect("an offset")
    };
    let byte = |at: usize| i8::from_be_bytes([image[at]]);
    let mut table = BTreeMap::new();
    let mut at = DECODED;
    while pointer(at) != 0 {
        let name = image[pointer(at)..]
            .split(|&byte| byte == 0)
            .next()
            .expect("a name");
        let mut lengths = Vec::new();
        let mut field = pointer(at + 6);
        while byte(field) != 0 {
            lengths.push((
                usize::try_from(byte(field)).expect("an index"),
                byte(field + 1),
            ));
            field += FIELD_ENTRY;
        }
        table.insert(
            String::from_utf8(name.to_vec()).expect("ASCII"),
            Taken {
                length: byte(at + 4),
                fields: byte(at + 5),
                lengths,
            },
        );
        at += ENTRY;
    }
    table
}

/// Why the image would not take a sentence, as `nmea_sentence_decode`
/// judges one; `None` when it would.
fn refused(table: &BTreeMap<String, Taken>, sentence: &[u8]) -> Option<String> {
    let text = std::str::from_utf8(sentence).expect("ASCII");
    let body = text.strip_prefix('$')?.strip_suffix("\r\n")?;
    let (fields, checked) = match body.split_once('*') {
        Some((fields, _)) => (fields, true),
        None => (body, false),
    };
    let fields: Vec<&str> = fields.split(',').collect();
    let address = if fields[0] == "PFEC" {
        fields[1]
    } else {
        fields[0]
    };
    let name = address.strip_prefix("GP")?;
    let Some(taken) = table.get(name) else {
        return Some(format!("{name} is not in the table"));
    };
    let ends = fields.len() + usize::from(checked);
    if taken.length > 0 && sentence.len() != taken.length.unsigned_abs().into() {
        return Some(format!(
            "{name}: {} bytes, not {}",
            sentence.len(),
            taken.length
        ));
    }
    let allowed = if taken.fields > 0 {
        ends == taken.fields.unsigned_abs().into()
    } else if name == "GSV" {
        ends <= GSV_FIELDS && ends.is_multiple_of(GSV_MULTIPLE)
    } else {
        true
    };
    if !allowed {
        return Some(format!("{name}: {ends} field ends, not {}", taken.fields));
    }
    for &(index, length) in &taken.lengths {
        if let Some(field) = fields.get(index)
            && length > 0
            && field.len() != length.unsigned_abs().into()
        {
            return Some(format!(
                "{name}: field {index} is {field:?}, not {length} long"
            ));
        }
    }
    None
}

/// The sentences an engine sent.
#[derive(Default)]
struct Sent(Vec<Vec<u8>>);

impl Output for Sent {
    fn send(&mut self, frame: &[u8]) {
        self.0.push(frame.to_vec());
    }
}

impl Sent {
    /// The sentences as text, without their CR LF.
    fn text(&self) -> Vec<String> {
        self.0
            .iter()
            .map(|sentence| {
                String::from_utf8(sentence.strip_suffix(b"\r\n").expect("CR LF").to_vec())
                    .expect("ASCII")
            })
            .collect()
    }

    /// The one sentence sent that starts with `start`.
    fn only(&self, start: &str) -> String {
        let found: Vec<String> = self
            .text()
            .into_iter()
            .filter(|sentence| sentence.starts_with(start))
            .collect();
        let [one] = found.try_into().expect("one such sentence");
        one
    }
}

/// The GPS second of a UTC date and time, by [`OFFSET`].
fn at(year: u16, month: u8, day: u8, hour: u8, minute: u8, second: u8) -> GpsSeconds {
    let date = DateTime::new(year, month, day, hour, minute, second).expect("a date");
    GpsSeconds::from_utc(date.to_unix(), OFFSET).expect("a GPS second")
}

/// The scene's record at `time`.
fn record(time: GpsSeconds) -> Record {
    let mut record = scene::sky(scene::PLACE, OFFSET, time.week().expect("a week"));
    record.time = Some(GpsTime {
        seconds: time,
        subsecond: Nanoseconds(0),
    });
    record.derived.raim = Some(Raim::new(RaimSolution::Ok, RaimAbility::Isolation));
    record
}

/// Every sentence the engine sends, under each of them.
fn everything(engine: &mut Engine, record: &Record) -> Sent {
    let mut sent = Sent::default();
    engine.receive(
        b"$PFEC,GPint,GGA00,GSA00,GSV00,RMC00,anc00,tst00,tps00,rrm00\r\n$PFEC,GPsrq\r\n$PFEC,GPrrq\r\n",
        record,
        &mut sent,
    );
    sent
}

/// Every sentence the engine sends is one the image takes, by its own
/// table: before the record knows anything, fixing, in fixed observation
/// point mode with Time RAIM, with the antenna off and over a leap
/// second, each of `set`'s parameters at its longest.
#[test]
fn the_image_takes_every_sentence() {
    let table = table();
    let mut engine = Engine::new(&GT_74);
    let mut sent = everything(&mut engine, &Record::EMPTY);
    let time = at(2027, 12, 31, 23, 59, 59);
    let mut known = record(time);
    known.leap = Some(LeapEvent {
        at: time + Seconds(1),
        change: LeapChange::Insert,
    });
    known.velocity = Some(Velocity {
        speed: CentimetersPerSecond(u16::MAX),
        heading: TenthDegrees(3599),
    });
    known.derived.magnetic_variation = Some(TenthDegreesEast(-1800));
    engine.second(&known, &mut sent);
    sent.0.extend(everything(&mut engine, &known).0);
    engine.receive(
        b"$PFEC,GPset,Z2,H-00999.9,M90,U0000FFFF,t-999999\r\n$PFEC,GPrrs,1,999,3\r\n",
        &known,
        &mut sent,
    );
    sent.0.extend(everything(&mut engine, &known).0);
    known.antenna = Antenna::Shorted;
    known.self_test.hardware = FaultSet::EMPTY
        .with(Fault::Rom)
        .with(Fault::Ram)
        .with(Fault::Eeprom)
        .with(Fault::RealTimeClock);
    sent.0.extend(everything(&mut engine, &known).0);
    for sentence in &sent.0 {
        assert_eq!(
            refused(&table, sentence),
            None,
            "{}",
            sentence.escape_ascii()
        );
    }
    let names: Vec<String> = sent
        .text()
        .iter()
        .filter_map(|sentence| {
            let address = sentence[1..]
                .split(',')
                .find(|field| field.starts_with("GP"))?;
            Some(address[2..].to_string())
        })
        .collect();
    for name in table.keys() {
        assert!(names.contains(name), "{name} never sent");
    }
}

#[test]
fn a_set_parameter_changes_its_setting() {
    let mut engine = Engine::new(&GT_74);
    let mut sent = Sent::default();
    let now = Record::EMPTY;
    let changed = |events: crate::engine::event::Events| {
        events.fold(SettingSet::EMPTY, |set, event| match event {
            Event::SettingsChanged(changed) => set.union(changed),
            Event::Reset => set,
        })
    };
    let events = engine.receive(b"$PFEC,GPset,M15,U00000003\r\n", &now, &mut sent);
    assert_eq!(
        changed(events),
        SettingSet::of(&[Setting::MaskAngle, Setting::IgnoreList])
    );
    let settings = engine.settings();
    assert_eq!(settings.mask_angle.0, 15);
    assert_eq!(
        settings.ignore_list,
        PrnSet::EMPTY
            .with(Prn::new(1).expect("a satellite"))
            .with(Prn::new(2).expect("a satellite"))
    );

    let events = engine.receive(b"$PFEC,GPset,t-000300\r\n", &now, &mut sent);
    assert_eq!(changed(events), SettingSet::of(&[Setting::CableDelay]));
    assert_eq!(engine.settings().cable_delay, Nanoseconds(300));
    let events = engine.receive(b"$PFEC,GPset,t+000300\r\n", &now, &mut sent);
    assert_eq!(
        changed(events),
        SettingSet::of(&[Setting::CableDelay, Setting::PulseOffset])
    );
    assert_eq!(engine.settings().cable_delay, Nanoseconds(0));
    assert_eq!(engine.settings().pulse_offset, Nanoseconds(300));

    engine.receive(
        b"$PFEC,GPset,Z2\r\n$GPGLL,1000.5000,S,02000.25,W,,\r\n$PFEC,GPset,H000100.5\r\n",
        &now,
        &mut sent,
    );
    let entered = Location {
        latitude: MilliArcSeconds(-(10 * 3_600_000 + 30_000)),
        longitude: MilliArcSeconds(-(20 * 3_600_000 + 15_000)),
        height: Height {
            height: Centimeters(10_050),
            kind: HeightKind::MeanSeaLevel,
        },
    };
    let settings = engine.settings();
    assert!(settings.position_hold);
    assert_eq!(settings.held_position, entered);
    assert_eq!(settings.initial_position, entered);

    engine.receive(b"$PFEC,GPrrs,1,250,3\r\n", &now, &mut sent);
    let settings = engine.settings();
    assert!(settings.raim_algorithm);
    assert_eq!(settings.raim_alarm_limit, HundredNanoseconds(25));
    assert_eq!(settings.pulse_mode, PulseMode::Confirmed);
    assert!(sent.0.is_empty(), "set, GLL and rrs have no answer");

    let events: Vec<Event> = engine
        .receive(b"$PFEC,GPclr,1\r\n", &now, &mut sent)
        .collect();
    assert_eq!(events.first(), Some(&Event::Reset));
    let settings = engine.settings();
    assert!(!settings.position_hold);
    assert!(!settings.raim_algorithm);
    assert_eq!(settings.mask_angle.0, 5);
}

/// A GT-74 without Time RAIM takes no `rrs`, `rrq` or `int,rrm`, and
/// sends neither `rrm` nor `rsd`.
#[test]
fn without_time_raim_there_is_no_rrm() {
    let mut engine = Engine::new(&GT_74_WITHOUT_TIME_RAIM);
    let now = record(at(2026, 10, 22, 12, 0, 0));
    let sent = everything(&mut engine, &now);
    assert!(
        sent.text()
            .iter()
            .all(|sentence| !sentence.contains("GPrrm") && !sentence.contains("GPrsd"))
    );
    assert_eq!(sent.only("$PFEC,GPtst"), "$PFEC,GPtst,0,4850113002,0,0");
    let mut sent = Sent::default();
    engine.receive(b"$PFEC,GPrrs,1,100,3\r\n", &now, &mut sent);
    assert!(!engine.settings().raim_algorithm);
    assert!(sent.0.is_empty());
}

/// `tst` gives the identity's software and the record's faults (GT
/// p. 31).
#[test]
fn tst_gives_the_software_and_the_faults() {
    static ODD: Identity = match Identity::new(1_234_567, 89, true) {
        Some(identity) => identity,
        None => panic!("an identity"),
    };
    assert_eq!(Identity::new(10_000_000, 1, true), None);
    assert_eq!(Identity::new(1, 1000, true), None);
    let mut engine = Engine::new(&ODD);
    let mut now = Record::EMPTY;
    now.self_test.hardware = FaultSet::EMPTY.with(Fault::Rom).with(Fault::RealTimeClock);
    now.antenna = Antenna::Open;
    let mut sent = Sent::default();
    engine.receive(b"$PFEC,GPint,tst00\r\n", &now, &mut sent);
    assert_eq!(sent.only("$PFEC,GPtst"), "$PFEC,GPtst,0,1234567089,1,5");
}

/// Before any satellite, the time is the RTC's from GT p. 5's date, and
/// with a time but no offset to UTC it is GPS time, with no leap second
/// or offset to give (GT pp. 36 and 37).
#[test]
fn tps_gives_the_time_standard() {
    let mut engine = Engine::new(&GT_74);
    let mut sent = Sent::default();
    engine.second(&Record::EMPTY, &mut sent);
    assert_eq!(
        sent.only("$PFEC,GPtps"),
        "$PFEC,GPtps,020101000000,1,0,1,000000000000,00,00,000000000000,1147,172800"
    );
    let mut gps = record(at(2026, 10, 22, 12, 0, 0));
    gps.utc_offset = None;
    let mut sent = Sent::default();
    engine.second(&gps, &mut sent);
    assert_eq!(
        sent.only("$PFEC,GPtps"),
        "$PFEC,GPtps,261022120018,2,0,1,000000000000,00,00,000000000000,2441,388818"
    );
}

/// `rrm` gives up to four isolated satellites and the sawtooth within
/// its range, in fixed observation point mode with the algorithm on.
#[test]
fn rrm_gives_the_satellites_isolated() {
    let mut engine = Engine::new(&GT_74);
    let mut now = record(at(2026, 10, 22, 12, 0, 0));
    let isolated = [3, 4, 16, 26, 28]
        .into_iter()
        .fold(PrnSet::EMPTY, |set, id| {
            set.with(Prn::new(id).expect("a satellite"))
        });
    let mut raim = Raim::new(RaimSolution::Alarm, RaimAbility::Isolation);
    raim.isolated = isolated;
    now.derived.raim = Some(raim);
    let mut timing = Timing::ZERO;
    timing.sawtooth = Nanoseconds(-100);
    now.timing = Some(timing);
    let mut sent = Sent::default();
    engine.receive(b"$PFEC,GPint,rrm00\r\n", &now, &mut sent);
    assert_eq!(
        sent.only("$PFEC,GPrrm"),
        "$PFEC,GPrrm,2,2,00,00,00,00,+000,-15"
    );
    let mut sent = Sent::default();
    engine.receive(
        b"$PFEC,GPset,Z2\r\n$PFEC,GPrrs,1,100,3\r\n$PFEC,GPint,rrm00,tps00\r\n",
        &now,
        &mut sent,
    );
    assert_eq!(
        sent.only("$PFEC,GPrrm"),
        "$PFEC,GPrrm,1,0,03,04,16,26,+000,-15"
    );
    // An alarm in control mode 3 holds the pulse back (GT p. 47).
    assert!(
        sent.only("$PFEC,GPtps")
            .starts_with("$PFEC,GPtps,261022120000,3,0,2,")
    );
}

/// `RMC` gives the speed in knots, the course and the magnetic
/// variation (GT p. 25), and its checksum.
#[test]
fn rmc_gives_the_motion() {
    let mut engine = Engine::new(&GT_74);
    let mut now = record(at(2026, 10, 22, 12, 0, 0));
    now.velocity = Some(Velocity {
        speed: CentimetersPerSecond(1029),
        heading: TenthDegrees(1235),
    });
    now.derived.magnetic_variation = Some(TenthDegreesEast(-10));
    let mut sent = Sent::default();
    engine.second(&now, &mut sent);
    engine.receive(b"$PFEC,GPint,RMC00\r\n", &now, &mut sent);
    assert_eq!(
        sent.only("$GPRMC"),
        "$GPRMC,120000,A,3719.5350,N,12159.8700,W,020.0,123.5,221026,001.0,W*78"
    );
}

/// `ZDA` sets the clock while the record tells no time, and not out of
/// its range (GT p. 8).
#[test]
fn zda_sets_the_clock_without_a_time() {
    let mut engine = Engine::new(&GT_74);
    let mut sent = Sent::default();
    engine.receive(
        b"$GPZDA,235959,31,12,2001,+00,00\r\n",
        &Record::EMPTY,
        &mut sent,
    );
    engine.receive(
        b"$GPZDA,101112,13,03,2026,+05,30\r\n",
        &Record::EMPTY,
        &mut sent,
    );
    engine.second(&Record::EMPTY, &mut sent);
    assert!(
        sent.only("$PFEC,GPtps")
            .starts_with("$PFEC,GPtps,260313101112,1,")
    );
}
