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
use crate::engine::ReceiverControl;
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
use crate::record::Fix;
use crate::record::GpsTime;
use crate::record::Raim;
use crate::record::RaimAbility;
use crate::record::RaimSolution;
use crate::record::ReceiverMode;
use crate::record::Record;
use crate::record::Timing;
use crate::record::URA_MAX;
use crate::record::Ura;
use crate::record::Velocity;
use crate::source::fake_receiver;
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

/// The images that speak to a Furuno, from the crate's directory, each
/// with its table of the sentences it decodes, `nmea_sentences`: the
/// Z3815A 4010 and the Z3805A 1.01.03.  An entry of the table,
/// `nmea_sentence_entry`, is a pointer to the name, the length and the
/// count of field ends, and a pointer to the fields' entries, each
/// `nmea_field_entry`: the field's index and length, and two pointers
/// (`third_party/firmware/rewritten/IMAGE/types/smartclock.h`).
const IMAGES: [(&str, usize); 2] = [
    ("../../third_party/firmware/images/z3815a-4010.bin", 0x5e32a),
    (
        "../../third_party/firmware/images/z3805a-1.01.03.bin",
        0x54dee,
    ),
];
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

/// An image's table, by name, from its file and the table's offset.
fn table((path, decoded): (&str, usize)) -> BTreeMap<String, Taken> {
    let image = fs::read(Path::new(env!("CARGO_MANIFEST_DIR")).join(path)).expect("the image");
    let pointer = |at: usize| {
        let bytes = image[at..at + 4].try_into().expect("four bytes");
        usize::try_from(u32::from_be_bytes(bytes)).expect("an offset")
    };
    let byte = |at: usize| i8::from_be_bytes([image[at]]);
    let mut table = BTreeMap::new();
    let mut at = decoded;
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

/// The fake receiver's record at `time`.
fn record(time: GpsSeconds) -> Record {
    let mut record = fake_receiver::sky(fake_receiver::PLACE, OFFSET, time.week().expect("a week"));
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
        b"$PFEC,GPint,GGA00,GSA00,GSV00,RMC00,anc00,acc00,tst00,tps00,rrm00\r\n$PFEC,GPsrq\r\n$PFEC,GPrrq\r\n",
        record,
        &mut sent,
    );
    sent
}

/// Every sentence the engine sends is one each image whose table has
/// it takes, by that table: before the record knows anything, fixing,
/// with each URA, in fixed observation point mode with Time RAIM, with
/// the antenna off and over a leap second, each of `set`'s parameters
/// at its longest.
#[test]
fn the_images_take_every_sentence() {
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
    for (index, satellite) in known.satellites.iter_mut().enumerate() {
        satellite.ura = Ura::new(u8::try_from(index).expect("a few satellites") % (URA_MAX + 1));
    }
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
    let names: Vec<String> = sent
        .text()
        .iter()
        .map(|sentence| {
            let address = sentence[1..]
                .split(',')
                .find(|field| field.starts_with("GP"))
                .expect("an address");
            address[2..].to_string()
        })
        .collect();
    for image in IMAGES {
        let table = table(image);
        for (sentence, name) in sent.0.iter().zip(&names) {
            if table.contains_key(name) {
                assert_eq!(
                    refused(&table, sentence),
                    None,
                    "{}: {}",
                    image.0,
                    sentence.escape_ascii()
                );
            }
        }
        for name in table.keys() {
            assert!(names.contains(name), "{name} never sent");
        }
    }
}

/// `acc` gives each satellite's URA index as a hex digit, `X` where the
/// record has none (GT p. 28), and is sent only when `int` asks for it.
#[test]
fn acc_gives_each_satellites_ura() {
    let mut engine = Engine::new(&GT_74);
    let mut known = record(at(2026, 11, 17, 12, 0, 0));
    known
        .satellites
        .get_mut(Prn::new(3).expect("a PRN"))
        .expect("satellite 3")
        .ura = Ura::new(2);
    known
        .satellites
        .get_mut(Prn::new(4).expect("a PRN"))
        .expect("satellite 4")
        .ura = Ura::new(URA_MAX);
    let mut sent = Sent::default();
    engine.second(&known, &mut sent);
    assert!(
        !sent
            .text()
            .iter()
            .any(|sentence| sentence.starts_with("$PFEC,GPacc"))
    );
    let mut sent = Sent::default();
    engine.receive(b"$PFEC,GPint,acc00\r\n", &known, &mut sent);
    let expected = format!("$PFEC,GPacc,XX2F{}", "X".repeat(28));
    assert_eq!(sent.only("$PFEC,GPacc"), expected);
    let mut sent = Sent::default();
    engine.second(&known, &mut sent);
    assert!(
        !sent
            .text()
            .iter()
            .any(|sentence| sentence.starts_with("$PFEC,GPacc"))
    );
    let mut sent = Sent::default();
    engine.receive(b"$PFEC,GPint,acc01\r\n", &known, &mut sent);
    engine.second(&Record::EMPTY, &mut sent);
    assert_eq!(
        sent.only("$PFEC,GPacc"),
        format!("$PFEC,GPacc,{}", "X".repeat(32))
    );
}

/// The field table's rows of the Z3815A's sentences, its manual `GT`,
/// name only sentences of [`SENTENCES`](crate::nmea::SENTENCES), each
/// in the direction the image uses it, and each response sentence the
/// engine sends has rows (`tools/firmware/gps_fields.py` holds them to
/// the image).
#[test]
fn the_field_table_has_the_sentences() {
    const FIELDS: &str = "../../docs/firmware/gps-fields.tsv";
    const MANUAL: &str = "GT";
    let text = fs::read_to_string(Path::new(env!("CARGO_MANIFEST_DIR")).join(FIELDS))
        .expect("the field table");
    let mut named = std::collections::BTreeSet::new();
    for line in text.lines().skip(1) {
        let cells: Vec<&str> = line.split('\t').collect();
        if cells.get(8) != Some(&MANUAL) {
            continue;
        }
        let row = crate::nmea::sentence_type(cells[0].as_bytes()).expect("a sentence of the table");
        let direction = match row.direction {
            crate::Direction::ToEngine => "command",
            crate::Direction::FromEngine => "response",
        };
        assert_eq!(cells[1], direction, "{line}");
        named.insert(cells[0]);
    }
    for row in crate::nmea::SENTENCES {
        let has_data = row.fields.most > 0;
        assert_eq!(named.contains(row.name), has_data, "{}", row.name);
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

/// The software the image corrects the cable delay of: told the
/// `t` the image sends for a delay of 300 ns, `+000100`, it reports
/// software `4850102008`, which the image keys the correction on, echoes
/// the `t` as sent, which the image compares, and puts the pulse 300 ns
/// early, where the same `t` puts another GT-74's 100 ns late.
#[test]
fn the_image_corrects_one_softwares_cable_delay() {
    let now = Record::EMPTY;
    let mut corrected = Engine::new(&super::GT_74_PULSE_EARLY);
    let mut sent = Sent::default();
    corrected.receive(
        b"$PFEC,GPset,t+000100\r\n$PFEC,GPint,tst00\r\n$PFEC,GPsrq\r\n",
        &now,
        &mut sent,
    );
    assert_eq!(sent.only("$PFEC,GPtst"), "$PFEC,GPtst,0,4850102008,0,0");
    assert!(sent.only("$PFEC,GPssd").ends_with(",t+000100"));
    assert_eq!(corrected.settings().cable_delay, Nanoseconds(300));
    assert_eq!(corrected.settings().pulse_offset, Nanoseconds(0));
    let mut plain = Engine::new(&GT_74);
    plain.receive(b"$PFEC,GPset,t+000100\r\n", &now, &mut sent);
    assert_eq!(plain.settings().cable_delay, Nanoseconds(0));
    assert_eq!(plain.settings().pulse_offset, Nanoseconds(100));
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

/// `tps`'s mode, `GSA`'s, `GGA`'s quality and the latitude it gives, of
/// what `record` makes `engine` send.
fn modes(engine: &mut Engine, record: &Record) -> (String, String, String, String) {
    let mut sent = Sent::default();
    engine.receive(b"$PFEC,GPint,tps00,GSA00,GGA00\r\n", record, &mut sent);
    let field = |start: &str, index: usize| {
        sent.only(start)
            .split(',')
            .nth(index)
            .expect("the field")
            .to_string()
    };
    (
        field("$PFEC,GPtps", 5),
        field("$GPGSA", 2),
        field("$GPGGA", 6),
        field("$GPGGA", 2),
    )
}

/// The observation point mode `ssd` gives.
fn mode_set(engine: &mut Engine, record: &Record) -> String {
    let mut sent = Sent::default();
    engine.receive(b"$PFEC,GPsrq\r\n", record, &mut sent);
    sent.only("$PFEC,GPssd")
        .split(',')
        .find(|field| field.starts_with('Z'))
        .expect("the mode")
        .to_string()
}

/// The fake receiver's latitude, and the one [`ENTER`] enters.
const FOUND: &str = "3719.5350";
const ENTERED: &str = "1000.0000";
const ENTER: &[u8] = b"$GPGLL,1000.0000,N,02000.0000,E,,\r\n";

/// The record of [`record`] with its receiver in `mode`: in position
/// hold, with a fix of time alone.
fn in_mode(mode: ReceiverMode) -> Record {
    let mut record = record(at(2026, 10, 22, 12, 0, 0));
    record.receiver_mode = mode;
    if mode == ReceiverMode::Holding {
        record.fix = Fix::TimeOnly;
    }
    record
}

/// `modes` held at `latitude`, and fixing in 3D there.
fn held(latitude: &str) -> (String, String, String, String) {
    ("2".into(), "1".into(), "0".into(), latitude.into())
}
fn fixed(latitude: &str) -> (String, String, String, String) {
    ("1".into(), "3".into(), "1".into(), latitude.into())
}

/// From a source whose receiver is commanded, `tps`'s mode, the fix and
/// `GGA` hold the position only while the record's receiver does, one
/// whose mode is not known navigating, at the record's position,
/// whatever `set,Z`, `clr` 1 or `GLL` set; `ssd` gives the command
/// alone.
#[test]
fn a_commanded_receiver_tps_holds_as_the_receiver_does() {
    let holding = in_mode(ReceiverMode::Holding);
    let navigating = in_mode(ReceiverMode::Navigating);
    let unknown = in_mode(ReceiverMode::Unknown);
    let commanded = || Engine::new(&GT_74).with_receiver(ReceiverControl::Commanded);
    let mut sent = Sent::default();

    // Never commanded, its receiver found holding.
    let mut engine = commanded();
    assert_eq!(modes(&mut engine, &holding), held(FOUND));
    assert_eq!(mode_set(&mut engine, &holding), "Z1");

    let mut engine = commanded();
    engine.receive(ENTER, &navigating, &mut sent);
    engine.receive(b"$PFEC,GPset,Z2\r\n", &navigating, &mut sent);
    assert_eq!(mode_set(&mut engine, &navigating), "Z2");
    assert!(engine.settings().position_hold);
    assert_eq!(modes(&mut engine, &navigating), fixed(FOUND));
    assert_eq!(modes(&mut engine, &holding), held(FOUND));
    assert_eq!(modes(&mut engine, &unknown), fixed(FOUND));

    engine.receive(b"$PFEC,GPset,Z1\r\n", &holding, &mut sent);
    assert_eq!(mode_set(&mut engine, &holding), "Z1");
    assert_eq!(modes(&mut engine, &holding), held(FOUND));
    assert_eq!(modes(&mut engine, &navigating), fixed(FOUND));

    engine.receive(b"$PFEC,GPset,Z2\r\n", &holding, &mut sent);
    // `clr` 1 puts the mode back to estimated (GT p. 10).
    engine.receive(b"$PFEC,GPclr,1\r\n", &holding, &mut sent);
    assert_eq!(mode_set(&mut engine, &holding), "Z1");
    assert!(!engine.settings().position_hold);
    assert_eq!(modes(&mut engine, &holding), held(FOUND));
    assert_eq!(modes(&mut engine, &navigating), fixed(FOUND));
    assert_eq!(modes(&mut engine, &unknown), fixed(FOUND));
}

/// From a source whose receiver is not commanded, `tps`'s mode, the fix
/// and `GGA` hold the position as `set,Z` set it, at the position `GLL`
/// entered, whatever the record's receiver does.
#[test]
fn an_uncommanded_receiver_tps_holds_as_set() {
    let holding = in_mode(ReceiverMode::Holding);
    let navigating = in_mode(ReceiverMode::Navigating);
    let mut engine = Engine::new(&GT_74);
    let mut sent = Sent::default();
    assert_eq!(modes(&mut engine, &holding), fixed(FOUND));
    engine.receive(ENTER, &holding, &mut sent);
    engine.receive(b"$PFEC,GPset,Z2\r\n", &holding, &mut sent);
    assert_eq!(modes(&mut engine, &navigating), held(ENTERED));
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

/// `rrm` (GT p. 40) with no Time RAIM result in the record: the 1PPS
/// output result unknown and neither detection nor isolation, in fixed
/// observation point mode with the algorithm on as with it off.
#[test]
fn rrm_without_a_result_is_unknown() {
    const UNKNOWN: &str = "$PFEC,GPrrm,2,2,00,00,00,00,+000,";
    let mut engine = Engine::new(&GT_74);
    let mut now = record(at(2026, 10, 22, 12, 0, 0));
    now.derived.raim = None;
    let mut sent = Sent::default();
    engine.receive(b"$PFEC,GPint,rrm00\r\n", &now, &mut sent);
    assert!(sent.only("$PFEC,GPrrm").starts_with(UNKNOWN));
    let mut sent = Sent::default();
    engine.receive(
        b"$PFEC,GPset,Z2\r\n$PFEC,GPrrs,1,100,3\r\n$PFEC,GPint,rrm00\r\n",
        &now,
        &mut sent,
    );
    assert!(sent.only("$PFEC,GPrrm").starts_with(UNKNOWN));
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

/// `ZDA`'s zone keeps its hours' sign with no hours, `-00,30` a zone of
/// -30 minutes, local time being UTC less it (GT p. 8): `anc`'s local
/// almanac date is 30 minutes ahead of UTC, where `+00,30` puts it 30
/// minutes behind.
#[test]
fn zda_keeps_the_sign_of_no_hours() {
    let now = record(at(2026, 10, 22, 12, 0, 0));
    let almanac_date = |zone: &[u8]| {
        let mut engine = Engine::new(&GT_74);
        let mut sent = Sent::default();
        let sentence = [
            b"$GPZDA,120000,22,10,2026,",
            zone,
            b"\r\n$PFEC,GPint,anc00\r\n",
        ]
        .concat();
        engine.receive(&sentence, &now, &mut sent);
        let anc = sent.only("$PFEC,GPanc");
        let stamp = anc.split(',').nth(2).expect("a date").as_bytes();
        let two = |at: usize| {
            std::str::from_utf8(&stamp[at..at + 2])
                .expect("ASCII")
                .parse::<u8>()
                .expect("digits")
        };
        DateTime::new(
            2000 + u16::from(two(0)),
            two(2),
            two(4),
            two(6),
            two(8),
            two(10),
        )
        .expect("a date")
        .to_unix()
    };
    let utc = almanac_date(b"+00,00");
    assert_eq!(almanac_date(b"-00,30") - utc, Seconds(30 * 60));
    assert_eq!(almanac_date(b"+00,30") - utc, Seconds(-30 * 60));
    assert_eq!(almanac_date(b"-01,30") - utc, Seconds(90 * 60));
}

/// In estimated observation point mode the 1 PPS waits for a first fix
/// of four satellites or more with a PDOP under 6, and then goes out
/// with one satellite or more (GT p. 47, (2)-1): a 2D fix of three
/// satellites, or a fix of four at a PDOP of 6.0, does not make it.
/// [`Engine::pulsing`] says what `tps` does.
#[test]
fn the_pulse_waits_for_a_first_fix_of_four_satellites() {
    let base = record(at(2026, 10, 22, 12, 0, 0));
    let using = |count: usize, pdop: u16| {
        let mut now = base;
        for (index, satellite) in now
            .satellites
            .iter_mut()
            .filter(|satellite| satellite.used)
            .enumerate()
        {
            satellite.used = index < count;
        }
        let mut dops = now.dops.expect("DOPs");
        dops.position = crate::units::Tenths(pdop);
        now.dops = Some(dops);
        now.fix = crate::record::Fix::TwoD;
        now
    };
    let pulse = |engine: &mut Engine, now: &Record| {
        let mut sent = Sent::default();
        engine.second(now, &mut sent);
        let tps = sent.only("$PFEC,GPtps");
        let flag = tps.split(',').nth(4).expect("the pulse").to_string();
        assert_eq!(engine.pulsing(now), flag == "1");
        flag
    };
    let mut engine = Engine::new(&GT_74);
    assert_eq!(pulse(&mut engine, &using(3, 20)), "0");
    assert_eq!(pulse(&mut engine, &using(4, 60)), "0");
    assert_eq!(pulse(&mut engine, &using(4, 59)), "1");
    assert_eq!(pulse(&mut engine, &using(1, 20)), "1");
}

/// `GGA`'s geoid separation stays six characters over GT p. 19's range,
/// -999.9 to 9999.9, and at its ends past it, so the image takes the
/// sentence.
#[test]
fn gga_carries_the_geoid_separation_in_six_characters() {
    let tables = IMAGES.map(table);
    for (separation, field) in [
        (160_000, "1600.0"),
        (2_000_000, "9999.9"),
        (3_600, "0036.0"),
        (-50_000, "-500.0"),
        (-200_000, "-999.9"),
    ] {
        let mut now = record(at(2026, 10, 22, 12, 0, 0));
        let mut position = now.position.expect("a position");
        position.ellipsoid = Centimeters(position.mean_sea_level.0 + separation);
        now.position = Some(position);
        let mut engine = Engine::new(&GT_74);
        let mut sent = Sent::default();
        engine.receive(b"$PFEC,GPint,GGA00\r\n", &now, &mut sent);
        let gga = sent.only("$GPGGA");
        assert_eq!(gga.split(',').nth(11), Some(field), "{gga}");
        for table in &tables {
            assert_eq!(refused(table, &sent.0[0]), None);
        }
    }
}

/// A height of more digits than `set,H` has (GT p. 11), one a `u32`
/// cannot hold among them, is ignored, and so is any number too long
/// for one.
#[test]
fn an_overlong_height_is_ignored() {
    let mut engine = Engine::new(&GT_74);
    let mut sent = Sent::default();
    engine.receive(b"$PFEC,GPset,H000100.5\r\n", &Record::EMPTY, &mut sent);
    for sentence in [
        &b"$PFEC,GPset,H+4294967296.0\r\n"[..],
        b"$PFEC,GPset,H0000100.5\r\n",
    ] {
        engine.receive(sentence, &Record::EMPTY, &mut sent);
        assert_eq!(
            engine.settings().held_position.height.height,
            Centimeters(10_050),
            "{}",
            sentence.escape_ascii()
        );
    }
    assert_eq!(super::number(b"4294967295", 10), Some(u32::MAX));
    assert_eq!(super::number(b"4294967296", 10), None);
}
