//! Each message against its layout in the manuals, and what the test
//! vectors do not reach: a record with gaps, a shorted antenna, a leap
//! second removed, `@@Gj`, the options, and the settings and events a
//! holder reads.

use std::slice;
use std::vec;
use std::vec::Vec;

use crate::engine::Engine;
use crate::engine::Options;
use crate::engine::Output;
use crate::engine::event::Event;
use crate::engine::event::Events;
use crate::engine::identity::Identity;
use crate::engine::identity::UT_PLUS_2_2;
use crate::engine::identity::UT_PLUS_3_1;
use crate::engine::identity::VP_EIGHT;
use crate::engine::identity::VP_EIGHT_WITHOUT_TIME_RAIM;
use crate::engine::identity::VP_SIX;
use crate::engine::settings::Application;
use crate::engine::settings::AtmosphericCorrection;
use crate::engine::settings::CHANNELS_MOST;
use crate::engine::settings::CorrectionThresholds;
use crate::engine::settings::FixAlgorithm;
use crate::engine::settings::Height;
use crate::engine::settings::HeightKind;
use crate::engine::settings::Location;
use crate::engine::settings::OutputFormat;
use crate::engine::settings::PulseMode;
use crate::engine::settings::SatelliteSelect;
use crate::engine::settings::Setting;
use crate::engine::settings::SettingSet;
use crate::engine::settings::Settings;
use crate::engine::settings::TimeBase;
use crate::oncore::AA_TIME_OF_DAY;
use crate::oncore::AB_APPLICATION_TYPE;
use crate::oncore::AB_GMT_CORRECTION;
use crate::oncore::AC_DATE;
use crate::oncore::AC_HDOP_THRESHOLD;
use crate::oncore::AD_CORRECTION_THRESHOLDS;
use crate::oncore::AD_LATITUDE;
use crate::oncore::AE_LONGITUDE;
use crate::oncore::AF_HEIGHT;
use crate::oncore::AG_MASK_ANGLE;
use crate::oncore::AH_SATELLITE_SELECT;
use crate::oncore::AI_MANUAL_SATELLITE;
use crate::oncore::AJ_XDOP_TYPE;
use crate::oncore::AK_XDOP_HYSTERESIS;
use crate::oncore::AM_IGNORE_LIST;
use crate::oncore::AN_ALMANAC_UPDATE;
use crate::oncore::AQ_ATMOSPHERIC_CORRECTION;
use crate::oncore::AR_FIX_ALGORITHM;
use crate::oncore::AS_HOLD_POSITION;
use crate::oncore::AT_POSITION_HOLD;
use crate::oncore::AU_ALTITUDE_HOLD_HEIGHT;
use crate::oncore::AV_ALTITUDE_HOLD;
use crate::oncore::AW_TIME_MODE;
use crate::oncore::AX_EPOCH_OFFSET;
use crate::oncore::AY_PULSE_OFFSET;
use crate::oncore::AZ_CABLE_DELAY;
use crate::oncore::BA_POSITION_STATUS;
use crate::oncore::BB_VISIBLE_SATELLITES;
use crate::oncore::BC_XDOP_TABLE;
use crate::oncore::BD_ALMANAC_STATUS;
use crate::oncore::BH_CORRECTION_OUTPUT;
use crate::oncore::BJ_LEAP_SECOND_PENDING;
use crate::oncore::BK_EXTENSION;
use crate::oncore::BN_TIME_RAIM;
use crate::oncore::BO_UTC_OFFSET;
use crate::oncore::CA_SELF_TEST;
use crate::oncore::CE_PSEUDORANGE_CORRECTION;
use crate::oncore::CF_SET_TO_DEFAULTS;
use crate::oncore::CG_FIX_MODE;
use crate::oncore::CI_IO_FORMAT;
use crate::oncore::CJ_RECEIVER_ID;
use crate::oncore::Deframer;
use crate::oncore::EA_POSITION_STATUS;
use crate::oncore::EK_EXTENSION;
use crate::oncore::EN_TIME_RAIM;
use crate::oncore::FA_SELF_TEST;
use crate::oncore::GJ_LEAP_SECOND_PENDING;
use crate::oncore::HEADER;
use crate::oncore::MAX_COMMAND;
use crate::oncore::MAX_RESPONSE;
use crate::oncore::MESSAGES;
use crate::oncore::MessageId;
use crate::oncore::OVERHEAD;
use crate::oncore::TRAILER;
use crate::oncore::encode;
use crate::oncore::response_length;
use crate::record::Almanac;
use crate::record::Antenna;
use crate::record::Correction;
use crate::record::Corrections;
use crate::record::DopKind;
use crate::record::Dops;
use crate::record::Ecef;
use crate::record::Fault;
use crate::record::FaultSet;
use crate::record::Fix;
use crate::record::GpsTime;
use crate::record::Health;
use crate::record::Position;
use crate::record::Raim;
use crate::record::RaimAbility;
use crate::record::RaimSolution;
use crate::record::Record;
use crate::record::Satellite;
use crate::record::SatelliteFlag;
use crate::record::SatelliteFlags;
use crate::record::Solver;
use crate::record::Timing;
use crate::record::Tracking;
use crate::record::URA_MAX;
use crate::record::Ura;
use crate::record::Velocity;
use crate::record::VelocityAxes;
use crate::record::XdopCombination;
use crate::record::XdopTable;
use crate::time::DateTime;
use crate::time::GpsSeconds;
use crate::time::LeapChange;
use crate::time::LeapEvent;
use crate::units::Centimeters;
use crate::units::CentimetersPerSecond;
use crate::units::DecibelHertz;
use crate::units::Decimeters;
use crate::units::DecimetersPerSecond;
use crate::units::Deciseconds;
use crate::units::Degrees;
use crate::units::HundredNanoseconds;
use crate::units::MilliArcSeconds;
use crate::units::MillimetersPerSecond;
use crate::units::Milliseconds;
use crate::units::Nanoseconds;
use crate::units::Prn;
use crate::units::PrnSet;
use crate::units::Seconds;
use crate::units::TenthDegrees;
use crate::units::TenthDegreesEast;
use crate::units::Tenths;

/// The frames an engine sent.
#[derive(Default)]
struct Sent(Vec<Vec<u8>>);

impl Output for Sent {
    fn send(&mut self, frame: &[u8]) {
        self.0.push(frame.to_vec());
    }
}

const PRESETS: [&Identity; 5] = [
    &VP_SIX,
    &VP_EIGHT,
    &VP_EIGHT_WITHOUT_TIME_RAIM,
    &UT_PLUS_2_2,
    &UT_PLUS_3_1,
];

const NOW: Milliseconds<u64> = Milliseconds(0);
const OFFSET: Seconds<i8> = Seconds(18);
const FIX: [u8; 1] = [1];
/// Time RAIM on, each second, its alarm limit 1 us, the pulse always.
const TRAIM_EACH_SECOND: [u8; 15] = [1, 1, 0, 10, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0];

fn gps(year: u16, month: u8, day: u8, hour: u8, minute: u8, second: u8) -> GpsSeconds {
    let utc = DateTime::new(year, month, day, hour, minute, second).expect("a date");
    GpsSeconds::from_utc(utc.to_unix(), OFFSET).expect("a GPS time")
}

/// 2026-10-22 12:00:00 UTC.
fn noon() -> GpsSeconds {
    gps(2026, 10, 22, 12, 0, 0)
}

fn prn(id: u8) -> Prn {
    Prn::new(id).expect("a satellite")
}

/// A record with everything in it, five satellites tracked.
fn full() -> Record {
    let mut record = Record::EMPTY;
    record.time = Some(GpsTime {
        seconds: noon(),
        subsecond: Nanoseconds(7),
    });
    record.utc_offset = Some(OFFSET);
    record.fix = Fix::ThreeD;
    record.position = Some(Position {
        latitude: MilliArcSeconds(134_372_100),
        longitude: MilliArcSeconds(-439_192_200),
        ellipsoid: Centimeters(2659),
        mean_sea_level: Centimeters(5500),
    });
    record.velocity = Some(Velocity {
        speed: CentimetersPerSecond(3),
        heading: TenthDegrees(900),
    });
    record.dops = Some(Dops {
        geometric: Tenths(25),
        position: Tenths(20),
        horizontal: Tenths(12),
        vertical: Tenths(16),
        time: Tenths(10),
    });
    let mut timing = Timing::ZERO;
    timing.sigma = Nanoseconds(12);
    timing.sawtooth = Nanoseconds(-3);
    record.timing = Some(timing);
    record.derived.ecef = Some(Ecef {
        x: Centimeters(1),
        y: Centimeters(-2),
        z: Centimeters(3),
    });
    for (id, elevation, signal) in [
        (3, 78, 44),
        (4, 51, 42),
        (31, 41, 43),
        (26, 40, 40),
        (16, 29, 38),
    ] {
        let satellite =
            Satellite::new(prn(id), Degrees(elevation), Degrees(100)).tracked(DecibelHertz(signal));
        record.satellites.push(satellite);
    }
    let held = record
        .satellites
        .iter()
        .fold(PrnSet::EMPTY, |held, satellite| held.with(satellite.prn));
    let week = noon().week().expect("a week of GPS time");
    record.almanac = Some(Almanac::new(week, 0, held));
    record.antenna = Antenna::Connected;
    record
}

fn command(id: MessageId, data: &[u8]) -> Vec<u8> {
    let mut frame = [0; MAX_COMMAND];
    let length = encode(id, data, &mut frame).expect("a command fits");
    frame[..length].to_vec()
}

/// Sends a command and gives what came back at once.
fn ask(engine: &mut Engine, record: &Record, id: MessageId, data: &[u8]) -> Vec<Vec<u8>> {
    let mut sent = Sent::default();
    engine.receive(&command(id, data), record, NOW, &mut sent);
    sent.0
}

/// The data of the one frame `frames` holds, which must be `id`'s.
fn only(frames: &[Vec<u8>], id: MessageId) -> &[u8] {
    let [frame] = frames else {
        panic!("{} frames, not one", frames.len());
    };
    assert_eq!(frame[2..HEADER], id.bytes());
    &frame[HEADER..frame.len() - TRAILER]
}

fn fixing(identity: &'static Identity) -> Engine {
    let mut engine = Engine::new(identity, Options::DEFAULT);
    ask(&mut engine, &Record::EMPTY, CG_FIX_MODE, &FIX);
    engine
}

#[test]
fn every_frame_sent_is_whole_and_of_its_length() {
    for identity in PRESETS {
        for record in [Record::EMPTY, full()] {
            let mut engine = fixing(identity);
            let mut sent = Sent::default();
            for fill in [0x00, 0x01, 0xff] {
                for message in MESSAGES {
                    let Some(length) = message.command else {
                        continue;
                    };
                    let data = vec![fill; length - OVERHEAD];
                    engine.receive(&command(message.id, &data), &record, NOW, &mut sent);
                }
                ask(&mut engine, &record, CG_FIX_MODE, &FIX);
                for second in 1..=4 {
                    engine.second(&record, Milliseconds(second * 1000), &mut sent);
                }
            }
            engine.poll(Milliseconds(u64::MAX), &mut sent);
            assert!(sent.0.len() > 30, "{} frames", sent.0.len());
            for frame in &sent.0 {
                let mut deframer = Deframer::<MAX_RESPONSE>::new(response_length);
                let (taken, event) = deframer.feed(frame);
                assert_eq!(taken, frame.len());
                assert!(matches!(event, Some(Ok(_))), "{frame:02x?}");
            }
        }
    }
}

#[test]
fn a_vp_powers_up_idle_and_a_ut_plus_fixing() {
    assert!(!Engine::new(&VP_SIX, Options::DEFAULT).fixing());
    assert!(!Engine::new(&VP_EIGHT, Options::DEFAULT).fixing());
    assert!(Engine::new(&UT_PLUS_2_2, Options::DEFAULT).fixing());

    let mut engine = Engine::new(&VP_EIGHT, Options::DEFAULT);
    let mut sent = Sent::default();
    engine.second(&full(), NOW, &mut sent);
    assert!(sent.0.is_empty());
}

/// VP pp. 124 to 126.
#[test]
fn a_position_status_is_laid_out_as_the_manual_has_it() {
    let mut engine = fixing(&VP_EIGHT);
    let mut record = full();
    record.satellites.get_mut(prn(4)).expect("in view").tracking = Tracking::BitSync;
    record.satellites.get_mut(prn(4)).expect("in view").used = false;
    let frames = ask(&mut engine, &record, EA_POSITION_STATUS, &[0]);
    let data = only(&frames, EA_POSITION_STATUS);
    assert_eq!(data.len(), 69);
    assert_eq!(data[..4], [10, 22, 0x07, 0xea]);
    assert_eq!(data[4..7], [12, 0, 0]);
    assert_eq!(data[7..11], 7_u32.to_be_bytes());
    assert_eq!(data[11..15], 134_372_100_i32.to_be_bytes());
    assert_eq!(data[15..19], (-439_192_200_i32).to_be_bytes());
    assert_eq!(data[19..23], 2659_i32.to_be_bytes());
    assert_eq!(data[23..27], 5500_i32.to_be_bytes());
    assert_eq!(data[27..29], 3_u16.to_be_bytes());
    assert_eq!(data[29..31], 900_u16.to_be_bytes());
    // PDOP, its type, eight visible... five, and five tracked.
    assert_eq!(data[31..36], [0, 20, 0, 5, 5]);
    // Satellite, mode 8, the signal strength of 44 dB-Hz, used.
    assert_eq!(data[36..40], [3, 8, 112, 0x80]);
    assert_eq!(data[40..44], [4, 4, 89, 0x00]);
    assert_eq!(data[56..68], [0; 12]);
    assert_eq!(data[68], 0x20);
}

#[test]
fn a_fix_is_no_better_than_the_records() {
    let status = |fix, satellites: usize| {
        let mut engine = fixing(&VP_EIGHT);
        let mut record = full();
        record.fix = fix;
        let all = record.satellites;
        record.satellites = crate::record::Satellites::EMPTY;
        for satellite in all.iter().take(satellites) {
            record.satellites.push(*satellite);
        }
        let frames = ask(&mut engine, &record, EA_POSITION_STATUS, &[0]);
        let data = only(&frames, EA_POSITION_STATUS);
        (data[32], data[33], data[68])
    };
    assert_eq!(status(Fix::ThreeD, 5), (20, 0, 0x20));
    assert_eq!(status(Fix::TwoD, 5), (12, 1, 0x10));
    assert_eq!(status(Fix::ThreeD, 3), (12, 1, 0x10));
    assert_eq!(status(Fix::None, 5), (0, 0, 0x08));
    // VP pp. 70 and 72: propagated, the DOP is zero.
    assert_eq!(status(Fix::Propagated, 5), (0, 0, 0x80));
    assert_eq!(status(Fix::ThreeD, 2), (0, 0, 0x08 | 0x02));
}

/// UT pp. 6.18 to 6.20 and 6.43.
#[test]
fn a_ut_plus_tells_what_its_antenna_draws() {
    for (antenna, sense, result) in [
        (Antenna::Connected, 0x00, 0x0000_u16),
        (Antenna::Open, 0x80, 0x8000),
        (Antenna::Shorted, 0x40, 0x4000),
    ] {
        let mut engine = Engine::new(&UT_PLUS_2_2, Options::DEFAULT);
        let mut record = full();
        record.antenna = antenna;
        let frames = ask(&mut engine, &record, EA_POSITION_STATUS, &[0]);
        let data = only(&frames, EA_POSITION_STATUS);
        assert_eq!(data[23..27], [0; 4], "no height above mean sea level");
        assert_eq!(data[33] & 0xc0, sense);
        if antenna == Antenna::Connected {
            assert_eq!(data[36..40], [3, 8, 44, 0x82]);
        } else {
            assert_eq!(data[35..68], [0; 33], "nothing tracked");
        }

        assert!(ask(&mut engine, &record, FA_SELF_TEST, &[]).is_empty());
        assert_eq!(engine.deadline(), Some(Milliseconds(2000)));
        let mut sent = Sent::default();
        engine.poll(Milliseconds(1999), &mut sent);
        assert!(sent.0.is_empty());
        engine.poll(Milliseconds(2000), &mut sent);
        assert_eq!(only(&sent.0, FA_SELF_TEST), result.to_be_bytes());
        assert_eq!(engine.deadline(), None);
    }
}

/// VP pp. 108 and 143; UT p. 6.43.
#[test]
fn a_self_test_tells_the_records_faults_in_its_own_bits() {
    let mut record = full();
    record.self_test.hardware = FaultSet::EMPTY
        .with(Fault::RealTimeClock)
        .with(Fault::TemperatureSensor)
        .with(Fault::Eeprom)
        .with(Fault::Ram);
    record.self_test.channels = 0x0181;
    for (identity, id, result) in [
        (&VP_SIX, CA_SELF_TEST, 0x2d01_u16),
        (&VP_EIGHT, FA_SELF_TEST, 0xb881),
        (&UT_PLUS_2_2, FA_SELF_TEST, 0x3481),
    ] {
        let mut engine = fixing(identity);
        assert!(ask(&mut engine, &record, id, &[]).is_empty());
        let mut sent = Sent::default();
        engine.poll(Milliseconds(2000), &mut sent);
        assert_eq!(only(&sent.0, id), result.to_be_bytes());
    }
}

/// VP pp. 41 and 75.
#[test]
fn a_satellite_on_the_ignore_list_is_listed_as_removed() {
    let mut engine = fixing(&VP_EIGHT);
    let mut record = full();
    record.satellites.get_mut(prn(4)).unwrap().health = Health::Unhealthy;
    // Satellites 3 and 4.
    ask(&mut engine, &record, AM_IGNORE_LIST, &[0, 0, 0, 0, 0x0c]);
    assert_eq!(
        engine.settings().ignore_list,
        PrnSet::EMPTY.with(prn(3)).with(prn(4))
    );
    let frames = ask(&mut engine, &record, BB_VISIBLE_SATELLITES, &[0]);
    let data = only(&frames, BB_VISIBLE_SATELLITES);
    let health: Vec<u8> = (0..5).map(|slot| data[1 + slot * 7 + 6]).collect();
    assert_eq!(health, [1, 3, 0, 0, 0]);
}

#[test]
fn a_vp_goes_idle_for_a_self_test_and_ignores_its_antenna() {
    let mut engine = fixing(&VP_SIX);
    let mut record = full();
    record.antenna = Antenna::Shorted;
    assert!(ask(&mut engine, &record, CA_SELF_TEST, &[]).is_empty());
    assert!(!engine.fixing());
    let mut sent = Sent::default();
    engine.second(&record, Milliseconds(2000), &mut sent);
    assert_eq!(only(&sent.0, CA_SELF_TEST), [0, 0]);
}

/// VP pp. 138 to 142; UT pp. 6.92 to 6.95.
#[test]
fn a_time_raim_status_carries_the_records_timing() {
    let mut record = full();
    record
        .satellites
        .get_mut(prn(3))
        .expect("in view")
        .time_error = Some(Nanoseconds(-5));
    record
        .satellites
        .get_mut(prn(4))
        .expect("in view")
        .time_error = Some(Nanoseconds(40));

    let mut engine = fixing(&VP_EIGHT);
    let frames = ask(&mut engine, &record, EN_TIME_RAIM, &TRAIM_EACH_SECOND);
    let data = only(&frames, EN_TIME_RAIM);
    assert_eq!(data.len(), 62);
    assert_eq!(data[..5], [1, 1, 0, 10, 1]);
    // A pulse a second, and the next one's date and time.
    assert_eq!(data[5..15], [0, 0, 1, 10, 22, 0x07, 0xea, 12, 0, 1]);
    // The pulse on, UTC, the solution good, a bad satellite detectable.
    assert_eq!(data[15..19], [1, 0, 0, 1]);
    assert_eq!(data[19..21], 12_u16.to_be_bytes());
    assert_eq!(data[21], (-3_i8).to_be_bytes()[0]);
    assert_eq!(data[22], 3);
    assert_eq!(data[23..27], 999_999_995_u32.to_be_bytes());
    assert_eq!(data[27], 4);
    assert_eq!(data[28..32], 40_u32.to_be_bytes());
    assert_eq!(data[47..62], [0; 15]);

    let mut engine = Engine::new(&UT_PLUS_2_2, Options::DEFAULT);
    let frames = ask(&mut engine, &record, EN_TIME_RAIM, &TRAIM_EACH_SECOND);
    let data = only(&frames, EN_TIME_RAIM);
    assert_eq!(data[5..15], [0; 10]);
    assert_eq!(data[22..27], [3, 0x3b, 0x9a, 0xc9, 0xfb]);
}

/// VP p. 5: "initialized at 12:00:00 at power up"; the date is the
/// model's, the day GPS time starts.  The clock runs on from there, and
/// one `@@Ab` sets behind UTC (VP p. 9) reads a time before the start
/// of GPS time.
#[test]
fn an_engine_given_no_time_starts_at_noon() {
    const DATE: [u8; 4] = [1, 6, 0x07, 0xbc];
    const EVE: [u8; 4] = [1, 5, 0x07, 0xbc];
    let record = Record::EMPTY;
    let clock = |engine: &mut Engine| {
        let date = ask(engine, &record, AC_DATE, &[0xff; 4]);
        let time = ask(engine, &record, AA_TIME_OF_DAY, &[0xff; 3]);
        [only(&date, AC_DATE), only(&time, AA_TIME_OF_DAY)].concat()
    };
    for identity in [&VP_SIX, &VP_EIGHT, &UT_PLUS_2_2] {
        let mut engine = Engine::new(identity, Options::DEFAULT);
        assert_eq!(clock(&mut engine), [&DATE[..], &[12, 0, 0]].concat());
        for _ in 0..3 {
            engine.second(&record, NOW, &mut Sent::default());
        }
        // The first second given is the one the clock is at.
        assert_eq!(clock(&mut engine), [&DATE[..], &[12, 0, 2]].concat());

        // Thirteen hours behind, and thirteen hours and a half.
        ask(&mut engine, &record, AB_GMT_CORRECTION, &[0xff, 13, 0]);
        assert_eq!(clock(&mut engine), [&EVE[..], &[23, 0, 2]].concat());
        ask(&mut engine, &record, AB_GMT_CORRECTION, &[0xff, 13, 30]);
        assert_eq!(clock(&mut engine), [&EVE[..], &[22, 30, 2]].concat());
        ask(&mut engine, &record, AB_GMT_CORRECTION, &[0, 13, 0]);
        assert_eq!(
            clock(&mut engine),
            [&[1, 7, 0x07, 0xbc][..], &[1, 0, 2]].concat()
        );
    }
}

#[test]
fn a_record_with_gaps_is_sent_as_zeros() {
    let mut engine = fixing(&VP_EIGHT);
    let frames = ask(&mut engine, &Record::EMPTY, EA_POSITION_STATUS, &[0]);
    let data = only(&frames, EA_POSITION_STATUS);
    // The power-up clock, and nothing else but the status.
    assert_eq!(data[..7], [1, 6, 0x07, 0xbc, 12, 0, 0]);
    assert_eq!(data[7..68], [0; 61]);
    // Acquiring, too few visible, no almanac (VP p. 73).
    assert_eq!(data[68], 0x08 | 0x02 | 0x01);

    let mut record = full();
    let frames = ask(&mut engine, &record, EK_EXTENSION, &[0]);
    let data = only(&frames, EK_EXTENSION);
    assert_eq!(data[..10], [0, 25, 0, 20, 0, 12, 0, 16, 0, 10]);
    assert_eq!(
        data[20..32],
        [0, 0, 0, 1, 0xff, 0xff, 0xff, 0xfe, 0, 0, 0, 3]
    );
    record.dops = None;
    record.derived.ecef = None;
    let frames = ask(&mut engine, &record, EK_EXTENSION, &[0]);
    let data = only(&frames, EK_EXTENSION);
    // VP p. 137: no URA is decoded, which is 63.
    assert_eq!(data[..32], [0; 32]);
    assert_eq!(data[32..40], [63; 8]);
    assert_eq!(data[40..], [0; 24]);
}

/// VP pp. 97 and 98, and 136 and 137: the rest of an extension is the
/// record's, in position hold as out of it.
#[test]
fn an_extension_tells_all_the_record_has_for_it() {
    let mut record = full();
    record.derived.magnetic_variation = Some(TenthDegreesEast(-123));
    record.derived.velocity = Some(VelocityAxes {
        north: DecimetersPerSecond(5),
        east: DecimetersPerSecond(-6),
        up: DecimetersPerSecond(7),
    });
    record.differential_age = Some(Deciseconds(70_000));
    record.satellites.get_mut(prn(3)).unwrap().ura = Ura::new(2);
    assert_eq!(Ura::new(URA_MAX).map(Ura::get), Some(15));
    assert_eq!(Ura::new(URA_MAX + 1), None);
    let mut solver = Solver::ZERO;
    solver.matrix = [1, -2, 3, 4, 5, 6, 7, 8, 9, 10];
    solver.clock_bias = Decimeters(-300);
    solver.oscillator_offset = DecimetersPerSecond(40_000);
    record.derived.solver = Some(solver);

    let matrix = [
        0, 1, 0xff, 0xfe, 0, 3, 0, 4, 0, 5, 0, 6, 0, 7, 0, 8, 0, 9, 0, 10,
    ];
    for (identity, id, channels) in [(&VP_SIX, BK_EXTENSION, 6), (&VP_EIGHT, EK_EXTENSION, 8)] {
        let mut engine = fixing(identity);
        ask(&mut engine, &record, AT_POSITION_HOLD, &[1]);
        let frames = ask(&mut engine, &record, id, &[0]);
        let data = only(&frames, id);
        assert_eq!(data[10..12], [0xff, 0x85]);
        assert_eq!(data[12..18], [0, 5, 0xff, 0xfa, 0, 7]);
        // An age past what the field holds is its most.
        assert_eq!(data[18..20], [0xff, 0xff]);
        assert_eq!(data[20..24], [0, 0, 0, 1]);
        let mut ura = [63; 8];
        ura[0] = 2;
        assert_eq!(data[32..32 + channels], ura[..channels]);
        let rest = &data[32 + channels..];
        assert_eq!(rest[..20], matrix);
        assert_eq!(rest[20..], [0xfe, 0xd4, 0x9c, 0x40]);
    }
}

/// VP pp. 72 and 73.
#[test]
fn a_channel_tells_every_mode_and_flag() {
    let mut record = full();
    let first = record.satellites.get_mut(prn(3)).unwrap();
    first.tracking = Tracking::AgcSet;
    first.flags = SatelliteFlags::EMPTY
        .with(SatelliteFlag::MomentumAlert)
        .with(SatelliteFlag::Inaccurate);
    let second = record.satellites.get_mut(prn(4)).unwrap();
    second.tracking = Tracking::MessageSync;
    second.used = false;
    second.health = Health::Unhealthy;
    second.flags = SatelliteFlags::EMPTY
        .with(SatelliteFlag::AntiSpoof)
        .with(SatelliteFlag::ParityError);
    let mut engine = fixing(&VP_EIGHT);
    let frames = ask(&mut engine, &record, EA_POSITION_STATUS, &[0]);
    let data = only(&frames, EA_POSITION_STATUS);
    assert_eq!((data[37], data[39]), (2, 0x80 | 0x40 | 0x08));
    assert_eq!((data[41], data[43]), (5, 0x20 | 0x10 | 0x01));
}

#[test]
fn the_rates_count_seconds_given_not_the_records_time() {
    let mut engine = fixing(&VP_EIGHT);
    let mut record = full();
    ask(&mut engine, &record, EA_POSITION_STATUS, &[2]);
    let mut seconds = Vec::new();
    for step in [0, 1, 500, 501, 502, 502, 502] {
        record.time = Some(GpsTime {
            seconds: noon() + Seconds(step),
            subsecond: Nanoseconds(0),
        });
        let mut sent = Sent::default();
        engine.second(&record, NOW, &mut sent);
        seconds.extend(
            sent.0
                .iter()
                .map(|frame| (frame[HEADER + 5], frame[HEADER + 6])),
        );
    }
    // Every other second given, whatever the record's time jumps by,
    // and a record that stands still does not hold the clock: the
    // sixth second is 12:08:23, though its record still says 12:08:22.
    assert_eq!(seconds, [(0, 1), (8, 21), (8, 23)]);
}

#[test]
fn a_six_channel_engine_takes_the_eight_channel_messages_only_by_option() {
    let record = full();
    let mut engine = fixing(&VP_SIX);
    assert!(ask(&mut engine, &record, EA_POSITION_STATUS, &[0]).is_empty());
    assert!(ask(&mut engine, &record, EN_TIME_RAIM, &TRAIM_EACH_SECOND).is_empty());

    let mut options = Options::DEFAULT;
    options.eight_on_ea = true;
    let mut engine = Engine::new(&VP_SIX, options);
    ask(&mut engine, &record, CG_FIX_MODE, &FIX);
    assert!(ask(&mut engine, &record, EN_TIME_RAIM, &TRAIM_EACH_SECOND).is_empty());
    let frames = ask(&mut engine, &record, BA_POSITION_STATUS, &[0]);
    assert_eq!(only(&frames, BA_POSITION_STATUS)[34], 5);
    let frames = ask(&mut engine, &record, EA_POSITION_STATUS, &[0]);
    assert_eq!(only(&frames, EA_POSITION_STATUS)[35], 5);
    let frames = ask(&mut engine, &record, EN_TIME_RAIM, &TRAIM_EACH_SECOND);
    assert_eq!(only(&frames, EN_TIME_RAIM).len(), 62);
    let frames = ask(&mut engine, &record, BN_TIME_RAIM, &TRAIM_EACH_SECOND);
    assert_eq!(only(&frames, BN_TIME_RAIM).len(), 52);
}

#[test]
fn an_engine_set_to_send_its_status_unasked_does() {
    let mut options = Options::DEFAULT;
    options.deaf = true;
    options.status_rate = Seconds(2);
    let mut engine = Engine::new(&VP_SIX, options);
    let record = full();
    assert!(ask(&mut engine, &record, AA_TIME_OF_DAY, &[0xff; 3]).is_empty());
    let mut sent = Sent::default();
    for _ in 0..4 {
        engine.second(&record, NOW, &mut sent);
    }
    assert_eq!(sent.0.len(), 2);
    assert_eq!(sent.0[0][2..HEADER], BA_POSITION_STATUS.bytes());
}

/// The leap second at the end of 2027, in a record two seconds before
/// it.
fn before_a_leap(change: LeapChange) -> (Record, LeapEvent) {
    let midnight = gps(2028, 1, 1, 0, 0, 0);
    let (at, now) = match change {
        // 23:59:60 is the second the old offset would call midnight.
        LeapChange::Insert => (midnight, midnight - Seconds(2)),
        // 23:59:59 is left out: the new day starts a second early.
        LeapChange::Remove => (midnight - Seconds(1), midnight - Seconds(3)),
    };
    let leap = LeapEvent { at, change };
    let mut record = full();
    record.time = Some(GpsTime {
        seconds: now,
        subsecond: Nanoseconds(0),
    });
    record.leap = Some(leap);
    (record, leap)
}

/// VP p. 94.
#[test]
fn a_leap_second_to_be_removed_is_pending_as_a_subtraction() {
    let (mut record, leap) = before_a_leap(LeapChange::Remove);
    let mut engine = fixing(&VP_EIGHT);
    let frames = ask(&mut engine, &record, BJ_LEAP_SECOND_PENDING, &[0]);
    assert_eq!(only(&frames, BJ_LEAP_SECOND_PENDING), [2]);
    let frames = ask(&mut engine, &record, AA_TIME_OF_DAY, &[0xff; 3]);
    assert_eq!(only(&frames, AA_TIME_OF_DAY), [23, 59, 57]);

    let mut times = Vec::new();
    for step in 1..=2 {
        let seconds = leap.at - Seconds(2) + Seconds(step);
        record.time = Some(GpsTime {
            seconds,
            subsecond: Nanoseconds(0),
        });
        record.utc_offset = Some(leap.offset(seconds, OFFSET));
        engine.second(&record, NOW, &mut Sent::default());
        let frames = ask(&mut engine, &record, AA_TIME_OF_DAY, &[0xff; 3]);
        times.push(only(&frames, AA_TIME_OF_DAY).to_vec());
    }
    assert_eq!(times, [[23, 59, 58], [0, 0, 0]]);
    let frames = ask(&mut engine, &record, BJ_LEAP_SECOND_PENDING, &[0]);
    assert_eq!(only(&frames, BJ_LEAP_SECOND_PENDING), [0]);
}

/// UT p. 6.81.
#[test]
fn the_offset_is_sent_when_it_changes_and_not_through_the_leap_second() {
    let (mut record, leap) = before_a_leap(LeapChange::Insert);
    let mut engine = Engine::new(&UT_PLUS_2_2, Options::DEFAULT);
    let frames = ask(&mut engine, &record, BO_UTC_OFFSET, &[1]);
    assert_eq!(only(&frames, BO_UTC_OFFSET), [18]);
    let mut offsets = Vec::new();
    for step in 0..4 {
        let seconds = leap.at - Seconds(2) + Seconds(step);
        record.time = Some(GpsTime {
            seconds,
            subsecond: Nanoseconds(0),
        });
        record.utc_offset = Some(leap.offset(seconds, OFFSET));
        let mut sent = Sent::default();
        engine.second(&record, NOW, &mut sent);
        offsets.push(sent.0.iter().map(|frame| frame[HEADER]).collect::<Vec<_>>());
    }
    assert_eq!(offsets, [vec![], vec![], vec![], vec![19]]);
}

/// M12+ pp. 152 to 153.
#[test]
fn leap_details_give_the_leap_second_and_when() {
    let (record, _) = before_a_leap(LeapChange::Insert);
    let mut engine = Engine::new(&UT_PLUS_3_1, Options::DEFAULT);
    let frames = ask(&mut engine, &record, GJ_LEAP_SECOND_PENDING, &[]);
    assert_eq!(
        only(&frames, GJ_LEAP_SECOND_PENDING),
        [18, 19, 0x07, 0xeb, 12, 31, 18, 0, 0, 0, 0, 23, 59, 60]
    );

    let (record, _) = before_a_leap(LeapChange::Remove);
    let frames = ask(&mut engine, &record, GJ_LEAP_SECOND_PENDING, &[]);
    assert_eq!(
        only(&frames, GJ_LEAP_SECOND_PENDING),
        [18, 17, 0x07, 0xeb, 12, 31, 18, 0, 0, 0, 0, 23, 59, 58]
    );

    let frames = ask(&mut engine, &full(), GJ_LEAP_SECOND_PENDING, &[]);
    assert_eq!(
        only(&frames, GJ_LEAP_SECOND_PENDING),
        [18, 18, 0, 0, 0, 0, 18, 0, 0, 0, 0, 0, 0, 0]
    );

    let mut engine = Engine::new(&UT_PLUS_2_2, Options::DEFAULT);
    assert!(ask(&mut engine, &record, GJ_LEAP_SECOND_PENDING, &[]).is_empty());
    let mut engine = fixing(&VP_EIGHT);
    assert!(ask(&mut engine, &record, GJ_LEAP_SECOND_PENDING, &[]).is_empty());
}

#[test]
fn a_command_straight_after_a_broken_one_is_taken() {
    let mut engine = fixing(&VP_EIGHT);
    let mut bytes = command(AA_TIME_OF_DAY, &[0xff; 3]);
    bytes.truncate(HEADER + 1);
    bytes.extend(command(CG_FIX_MODE, &[0xff]));
    let mut sent = Sent::default();
    engine.receive(&bytes, &full(), NOW, &mut sent);
    assert_eq!(only(&sent.0, CG_FIX_MODE), [1]);
}

/// VP pp. 76 and 77: the record's table when it is of the kind `@@Aj`
/// asks for (VP p. 34), else none.
#[test]
fn an_xdop_table_is_sent_when_it_is_of_the_kind_asked() {
    let mut record = full();
    let mut table = XdopTable::new(DopKind::Position);
    for (dop, ids) in [(15, [3, 4, 31, 26]), (21, [3, 4, 31, 16])] {
        let mut combination = XdopCombination::EMPTY;
        for (slot, id) in combination.satellites.iter_mut().zip(ids) {
            *slot = Some(prn(id));
        }
        combination.dop = Tenths(dop);
        assert!(table.push(combination));
    }
    record.derived.xdops = Some(table);

    let mut engine = fixing(&VP_EIGHT);
    let frames = ask(&mut engine, &record, BC_XDOP_TABLE, &[0]);
    let data = only(&frames, BC_XDOP_TABLE);
    assert_eq!(data.len(), 75);
    // PDOP, two combinations, Best-4.
    assert_eq!(data[..3], [1, 2, 4]);
    assert_eq!(data[3..11], [0, 15, 3, 4, 31, 26, 0, 0]);
    assert_eq!(data[11..19], [0, 21, 3, 4, 31, 16, 0, 0]);
    assert_eq!(data[19..], [0; 56]);

    ask(&mut engine, &record, AJ_XDOP_TYPE, &[0]);
    let frames = ask(&mut engine, &record, BC_XDOP_TABLE, &[0]);
    let data = only(&frames, BC_XDOP_TABLE);
    assert_eq!(data[..3], [0, 0, 6]);
    assert_eq!(data[3..], [0; 72]);
}

/// VP pp. 89, 114 and 115: the record's corrections at their own time,
/// each within the range the message carries.
#[test]
fn pseudorange_corrections_are_sent_as_the_record_has_them() {
    let mut record = full();
    let mut set = Corrections::new(Deciseconds(123_456));
    set.satellites[0] = Some(Correction {
        prn: prn(3),
        range: Centimeters(-150),
        rate: MillimetersPerSecond(25),
        ephemeris_issue: 7,
    });
    set.satellites[2] = Some(Correction {
        prn: prn(4),
        range: Centimeters(2_000_000),
        rate: MillimetersPerSecond(-5000),
        ephemeris_issue: 9,
    });
    record.derived.corrections = Some(set);
    let mut engine = fixing(&VP_EIGHT);
    let frames = ask(&mut engine, &record, BH_CORRECTION_OUTPUT, &[0]);
    let data = only(&frames, CE_PSEUDORANGE_CORRECTION);
    assert_eq!(data.len(), 45);
    assert_eq!(data[..3], [0x01, 0xe2, 0x40]);
    assert_eq!(data[3..10], [3, 0xff, 0xff, 0x6a, 0, 25, 7]);
    assert_eq!(data[10..17], [0; 7]);
    // Out of range either way, sent as the nearest end.
    assert_eq!(data[17..24], [4, 0x10, 0, 0, 0xf0, 0x00, 9]);
    assert_eq!(data[24..], [0; 21]);
}

/// VP pp. 89 and 90: a message carries six corrections, and an
/// eight-channel engine with more sends a second straight after, the
/// fields it does not need zeros.  A six-channel one sends its six.
#[test]
fn an_eight_channel_engine_sends_corrections_past_the_sixth_in_a_second_message() {
    let correction = |id: u8| {
        Some(Correction {
            prn: prn(id),
            range: Centimeters(i32::from(id)),
            rate: MillimetersPerSecond(i16::from(id)),
            ephemeris_issue: id,
        })
    };
    let field = |id: u8| [id, 0, 0, id, 0, id, id];
    let time = [0x01, 0xe2, 0x40];
    let mut set = Corrections::new(Deciseconds(123_456));
    set.satellites[5] = correction(6);
    let mut record = full();
    record.derived.corrections = Some(set);
    let sent = |identity: &'static Identity, record: &Record| {
        let mut engine = fixing(identity);
        ask(&mut engine, record, BH_CORRECTION_OUTPUT, &[0])
            .iter()
            .map(|frame| {
                assert_eq!(frame[2..HEADER], CE_PSEUDORANGE_CORRECTION.bytes());
                frame[HEADER..frame.len() - TRAILER].to_vec()
            })
            .collect::<Vec<_>>()
    };
    let first = [&time[..], &[0; 35], &field(6)].concat();

    // Nothing past the sixth: one message, from either.
    assert_eq!(sent(&VP_EIGHT, &record), slice::from_ref(&first));
    assert_eq!(sent(&VP_SIX, &record), slice::from_ref(&first));

    // The eighth alone: a second message, the seventh's field unused.
    set.satellites[7] = correction(8);
    record.derived.corrections = Some(set);
    let second = [&time[..], &[0; 7], &field(8), &[0; 28]].concat();
    assert_eq!(second.len(), first.len());
    assert_eq!(sent(&VP_EIGHT, &record), [first.clone(), second]);
    assert_eq!(sent(&VP_SIX, &record), slice::from_ref(&first));

    // Both.
    set.satellites[6] = correction(7);
    record.derived.corrections = Some(set);
    let second = [&time[..], &field(7), &field(8), &[0; 28]].concat();
    assert_eq!(sent(&VP_EIGHT, &record), [first.clone(), second]);

    // No set at all: one message of no corrections.
    record.derived.corrections = None;
    let frames = sent(&VP_EIGHT, &record);
    assert_eq!(frames.len(), 1);
    assert_eq!(frames[0][3..], [0; 42]);
}

/// VP pp. 105 and 106; UT pp. 6.20, 6.93 and 6.95: the record's result
/// while Time RAIM is on and the engine tracks enough satellites for
/// one, isolation only in position hold.
#[test]
fn a_time_raim_result_is_sent_while_time_raim_is_on() {
    let mut record = full();
    let mut raim = Raim::new(RaimSolution::Alarm, RaimAbility::Isolation);
    raim.isolated = PrnSet::EMPTY.with(prn(4));
    record.derived.raim = Some(raim);
    let traim = |engine: &mut Engine, setup: &[u8]| {
        let frames = ask(engine, &record, EN_TIME_RAIM, setup);
        only(&frames, EN_TIME_RAIM)[15..19].to_vec()
    };
    let channels = |engine: &mut Engine| {
        let frames = ask(engine, &record, EA_POSITION_STATUS, &[0]);
        let data = only(&frames, EA_POSITION_STATUS);
        (data[39], data[43])
    };

    let mut engine = Engine::new(&UT_PLUS_2_2, Options::DEFAULT);
    // Off: unknown, and nothing isolated.
    assert_eq!(traim(&mut engine, &[0xff; 15]), [1, 0, 2, 1]);
    assert_eq!(channels(&mut engine), (0x82, 0x82));
    // On, not holding: detection only, nothing isolated.
    assert_eq!(traim(&mut engine, &TRAIM_EACH_SECOND), [1, 0, 1, 1]);
    assert_eq!(channels(&mut engine), (0x82, 0x82));
    // On, and no satellite heard: unknown, whatever the record has.
    let mut unheard = record;
    unheard.antenna = Antenna::Open;
    let frames = ask(&mut engine, &unheard, EN_TIME_RAIM, &[0xff; 15]);
    assert_eq!(only(&frames, EN_TIME_RAIM)[17..19], [2, 2]);
    // On and holding: satellite 4 is no longer used for time.
    ask(&mut engine, &record, AT_POSITION_HOLD, &[1]);
    assert_eq!(traim(&mut engine, &[0xff; 15]), [1, 0, 1, 0]);
    assert_eq!(channels(&mut engine), (0x82, 0x80));

    // A pulse only when the solution is confirmed: not in an alarm.
    let mut engine = fixing(&VP_EIGHT);
    let mut confirmed = TRAIM_EACH_SECOND;
    confirmed[4] = 3;
    assert_eq!(traim(&mut engine, &confirmed), [0, 0, 1, 1]);
}

/// VP pp. 74 and 75: mode 1 sends `@@Bb` when its data changes, until
/// `@@Cf` puts the engine back to polled.
#[test]
fn visible_satellites_are_sent_when_they_change() {
    let mut engine = fixing(&VP_EIGHT);
    let mut record = full();
    let frames = ask(&mut engine, &record, BB_VISIBLE_SATELLITES, &[1]);
    assert_eq!(only(&frames, BB_VISIBLE_SATELLITES).len(), 85);
    let mut sent = Sent::default();
    engine.second(&record, NOW, &mut sent);
    assert!(sent.0.is_empty());

    record
        .satellites
        .get_mut(prn(3))
        .expect("in view")
        .elevation = Degrees(79);
    engine.second(&record, NOW, &mut sent);
    assert_eq!(only(&sent.0, BB_VISIBLE_SATELLITES)[4], 79);
    let mut sent = Sent::default();
    engine.second(&record, NOW, &mut sent);
    assert!(sent.0.is_empty());

    ask(&mut engine, &record, CF_SET_TO_DEFAULTS, &[]);
    ask(&mut engine, &record, CG_FIX_MODE, &FIX);
    record
        .satellites
        .get_mut(prn(3))
        .expect("in view")
        .elevation = Degrees(80);
    engine.second(&record, NOW, &mut sent);
    assert!(sent.0.is_empty());
}

/// The satellite IDs `@@Bb`'s data lists, after checking its count
/// against them and that the slots past them are zeros.
fn listed(data: &[u8]) -> Vec<u8> {
    const SLOT: usize = 7;
    let count = usize::from(data[0]);
    let (slots, rest) = data[1..].split_at(count * SLOT);
    assert!(rest.iter().all(|byte| *byte == 0), "{rest:02x?}");
    slots.chunks(SLOT).map(|slot| slot[0]).collect()
}

/// The receivers bisect the `@@Bb` they keep on the satellite ID
/// (`satellite_find`), so it lists the record's satellites in that
/// order, whatever the record's, and all of them: one below the mask
/// angle too.
#[test]
fn visible_satellites_are_listed_in_the_order_of_their_ids() {
    let mut engine = fixing(&VP_EIGHT);
    let mut record = full();
    record
        .satellites
        .push(Satellite::new(prn(1), Degrees(2), Degrees(359)));
    let frames = ask(&mut engine, &record, BB_VISIBLE_SATELLITES, &[0]);
    let data = only(&frames, BB_VISIBLE_SATELLITES);
    assert_eq!(listed(data), [1, 3, 4, 16, 26, 31]);
    // Each keeps its own fields: the first's elevation and azimuth,
    // and the last's elevation, satellite 31's.
    assert_eq!(data[1..8], [1, 0, 0, 2, 0x01, 0x67, 0]);
    assert_eq!(data[1 + 5 * 7 + 3], 41);

    // One the record gives twice is listed once, as it first has it.
    record
        .satellites
        .push(Satellite::new(prn(4), Degrees(7), Degrees(0)));
    let frames = ask(&mut engine, &record, BB_VISIBLE_SATELLITES, &[0]);
    let data = only(&frames, BB_VISIBLE_SATELLITES);
    assert_eq!(listed(data), [1, 3, 4, 16, 26, 31]);
    assert_eq!(data[1 + 2 * 7 + 3], 51);

    // More than there are slots for: the highest are kept, then put in
    // order.
    let mut record = Record::EMPTY;
    for (id, elevation) in (1..=13).rev().zip(20..) {
        record
            .satellites
            .push(Satellite::new(prn(id), Degrees(elevation), Degrees(0)));
    }
    let frames = ask(&mut engine, &record, BB_VISIBLE_SATELLITES, &[0]);
    let data = only(&frames, BB_VISIBLE_SATELLITES);
    assert_eq!(listed(data), (1..=12).collect::<Vec<_>>());
}

/// The same satellites in another order are no change to `@@Bb`.
#[test]
fn a_record_put_in_another_order_is_no_change_to_the_visible_satellites() {
    let mut engine = fixing(&VP_EIGHT);
    let mut record = full();
    let frames = ask(&mut engine, &record, BB_VISIBLE_SATELLITES, &[1]);
    let first = only(&frames, BB_VISIBLE_SATELLITES).to_vec();

    let all = record.satellites;
    record.satellites = crate::record::Satellites::EMPTY;
    for satellite in all.iter().collect::<Vec<_>>().into_iter().rev() {
        record.satellites.push(*satellite);
    }
    assert_ne!(record.satellites, all);
    let mut sent = Sent::default();
    engine.second(&record, NOW, &mut sent);
    assert!(sent.0.is_empty());
    let frames = ask(&mut engine, &record, BB_VISIBLE_SATELLITES, &[0]);
    assert_eq!(only(&frames, BB_VISIBLE_SATELLITES), first);
}

/// VP pp. 27, 70 and 73: a VP counts as visible the satellites above
/// ten degrees, whatever `@@Ag`'s mask.  UT pp. 6.18 and 6.22: a UT
/// Plus, those above the horizon.  Either counts none with no almanac,
/// and `@@Bb` lists them all.
#[test]
fn the_visible_count_is_of_the_satellites_above_the_manuals_elevation() {
    let counted = |identity: &'static Identity, elevations: &[i8], almanac: bool| {
        let mut engine = Engine::new(identity, Options::DEFAULT);
        ask(&mut engine, &Record::EMPTY, CG_FIX_MODE, &FIX);
        ask(&mut engine, &Record::EMPTY, AG_MASK_ANGLE, &[45]);
        let mut record = full();
        record.satellites = crate::record::Satellites::EMPTY;
        for (id, elevation) in (1..).zip(elevations) {
            record
                .satellites
                .push(Satellite::new(prn(id), Degrees(*elevation), Degrees(0)));
        }
        if !almanac {
            record.almanac = None;
        }
        let frames = ask(&mut engine, &record, EA_POSITION_STATUS, &[0]);
        let data = only(&frames, EA_POSITION_STATUS);
        let frames = ask(&mut engine, &record, BB_VISIBLE_SATELLITES, &[0]);
        let listed = only(&frames, BB_VISIBLE_SATELLITES)[0];
        (data[34], data[68] & 0x02, listed)
    };
    // At ten degrees is not above it.
    assert_eq!(counted(&VP_EIGHT, &[9, 10, 11, 12, 90], true), (3, 0, 5));
    assert_eq!(counted(&VP_EIGHT, &[0, 10, 11, 12], true), (2, 0x02, 4));
    assert_eq!(counted(&VP_EIGHT, &[11, 12, 90], false), (0, 0x02, 3));
    // On the horizon is not above it.
    assert_eq!(counted(&UT_PLUS_2_2, &[-1, 0, 1, 9, 10], true), (3, 0, 5));
    assert_eq!(counted(&UT_PLUS_2_2, &[0, 1, 9], true), (2, 0x02, 3));
    assert_eq!(counted(&UT_PLUS_2_2, &[1, 9, 10], false), (0, 0x02, 3));
}

/// VP pp. 39, 60, 70 and 73; UT pp. 6.18 and 6.20.
#[test]
fn the_receiver_status_says_what_the_record_and_settings_make_of_the_fix() {
    let status = |identity: &'static Identity, change: &dyn Fn(&mut Record)| {
        let mut engine = Engine::new(identity, Options::DEFAULT);
        ask(&mut engine, &Record::EMPTY, CG_FIX_MODE, &FIX);
        let mut record = full();
        change(&mut record);
        let frames = ask(&mut engine, &record, EA_POSITION_STATUS, &[0]);
        let data = only(&frames, EA_POSITION_STATUS);
        (u16::from_be_bytes([data[31], data[32]]), data[34], data[68])
    };
    let dops = |position, horizontal| {
        move |record: &mut Record| {
            let dops = record.dops.as_mut().expect("DOPs");
            dops.position = Tenths(position);
            dops.horizontal = Tenths(horizontal);
        }
    };
    // A PDOP above @@Al's 6.0 holds the altitude.
    assert_eq!(status(&VP_EIGHT, &dops(60, 12)), (60, 5, 0x20));
    assert_eq!(status(&VP_EIGHT, &dops(61, 12)), (12, 5, 0x10));
    // Poor geometry is a DOP above 20 on a VP, above 12 on a UT Plus.
    assert_eq!(status(&VP_EIGHT, &dops(300, 201)), (201, 5, 0x10 | 0x40));
    assert_eq!(status(&VP_EIGHT, &dops(300, 130)), (130, 5, 0x10));
    assert_eq!(status(&UT_PLUS_2_2, &dops(300, 130)), (130, 5, 0x10 | 0x40));
    // Differential, while there is a fix.
    let differential = |record: &mut Record| record.differential = true;
    assert_eq!(status(&VP_EIGHT, &differential), (20, 5, 0x20 | 0x04));
    let unfixed = |record: &mut Record| {
        record.differential = true;
        record.fix = Fix::None;
    };
    assert_eq!(status(&VP_EIGHT, &unfixed), (0, 5, 0x08));
    // With no almanac, none visible.
    let forgetful = |record: &mut Record| record.almanac = None;
    assert_eq!(status(&VP_EIGHT, &forgetful), (20, 0, 0x20 | 0x02 | 0x01));
}

/// VP pp. 70 and 124: idle, a position status is the last one sent
/// while fixing, once when polled; before any, the current one, which
/// tracks nothing.
#[test]
fn an_idle_engine_answers_with_its_last_position_status() {
    let mut record = full();
    let poll = |engine: &mut Engine, record: &Record| {
        only(
            &ask(engine, record, EA_POSITION_STATUS, &[0]),
            EA_POSITION_STATUS,
        )
        .to_vec()
    };
    let mut engine = Engine::new(&VP_EIGHT, Options::DEFAULT);
    let before = poll(&mut engine, &record);
    assert_eq!(before[4..7], [12, 0, 0]);
    assert_eq!(before[34..36], [0, 0]);
    assert_eq!(before[36..68], [0; 32]);

    ask(&mut engine, &record, CG_FIX_MODE, &FIX);
    let fixing = poll(&mut engine, &record);
    assert_eq!(fixing[34..36], [5, 5]);
    ask(&mut engine, &record, CG_FIX_MODE, &[0]);
    record.time = Some(GpsTime {
        seconds: noon() + Seconds(5),
        subsecond: Nanoseconds(0),
    });
    engine.second(&record, NOW, &mut Sent::default());
    assert_eq!(poll(&mut engine, &record), fixing);
    ask(&mut engine, &record, CF_SET_TO_DEFAULTS, &[]);
    assert_eq!(poll(&mut engine, &record), fixing);
}

/// VP pp. 29 and 30: highest in the sky tracks the highest satellites,
/// where automatic selection takes the record's first.
#[test]
fn highest_in_the_sky_tracks_the_highest() {
    let mut record = full();
    for (id, elevation) in [(7, 85), (8, 15), (9, 60)] {
        record.satellites.push(
            Satellite::new(prn(id), Degrees(elevation), Degrees(0)).tracked(DecibelHertz(40)),
        );
    }
    let channels = |engine: &mut Engine| {
        let frames = ask(engine, &record, BA_POSITION_STATUS, &[0]);
        let data = only(&frames, BA_POSITION_STATUS);
        data[36..60].iter().step_by(4).copied().collect::<Vec<_>>()
    };
    let mut engine = fixing(&VP_SIX);
    assert_eq!(channels(&mut engine), [3, 4, 31, 26, 16, 7]);
    ask(&mut engine, &record, AH_SATELLITE_SELECT, &[2]);
    assert_eq!(channels(&mut engine), [3, 4, 31, 26, 7, 9]);
}

/// The events one command made.
fn tell(engine: &mut Engine, id: MessageId, data: &[u8]) -> Vec<Event> {
    engine
        .receive(&command(id, data), &full(), NOW, &mut Sent::default())
        .collect()
}

/// Each setting has its own bit and its own name, and
/// [`Setting::ALL`] is in the order of the bits.
#[test]
fn settings_are_distinct() {
    for (index, setting) in Setting::ALL.into_iter().enumerate() {
        assert_eq!(setting as usize, index);
        assert_eq!(Setting::of_name(setting.name()), Some(setting));
        assert!(!SettingSet::of(&Setting::ALL[..index]).contains(setting));
    }
}

fn changed(settings: &[Setting]) -> Event {
    Event::SettingsChanged(
        settings
            .iter()
            .fold(SettingSet::EMPTY, |set, setting| set.with(*setting)),
    )
}

/// A position made up, which is nowhere a receiver was.
const MADE_UP: Location = Location {
    latitude: MilliArcSeconds(1_000_000),
    longitude: MilliArcSeconds(-2_000_000),
    height: Height {
        height: Centimeters(12_345),
        kind: HeightKind::MeanSeaLevel,
    },
};

/// A height as `@@Af`, `@@As` and `@@Au` carry one (VP pp. 26, 55 and
/// 59).
fn height_data(height: Height) -> Vec<u8> {
    let kind = match height.kind {
        HeightKind::Ellipsoid => 0,
        HeightKind::MeanSeaLevel => 1,
    };
    [&height.height.0.to_be_bytes()[..], &[kind]].concat()
}

/// What an Oncore is set to at power-up: each command's default (VP
/// pp. 11, 15, 17, 19, 21, 25, 27, 29, 33, 37, 41, 43, 50, 52, 54, 56,
/// 58, 60, 62, 64, 66, 68 and 103).
fn defaults() -> Settings {
    Settings {
        mask_angle: Degrees(10),
        position_hold: false,
        held_position: Location::default(),
        altitude_hold: false,
        altitude_hold_height: Height::default(),
        time_base: TimeBase::Utc,
        cable_delay: Nanoseconds(0),
        pulse_offset: Nanoseconds(0),
        pulse_mode: PulseMode::Always,
        raim_algorithm: false,
        raim_alarm_limit: HundredNanoseconds(10),
        application: Application::Land,
        initial_position: Location::default(),
        satellite_select: SatelliteSelect::BestGeometry,
        manual_satellites: [0; CHANNELS_MOST],
        dop_kind: DopKind::Position,
        xdop_hysteresis: Tenths(10),
        ignore_list: PrnSet::EMPTY,
        almanac_update: true,
        atmospheric_correction: AtmosphericCorrection {
            ionospheric: true,
            tropospheric: false,
        },
        fix_algorithm: FixAlgorithm::AllInView,
        epoch_offset: Milliseconds(0),
        hdop_threshold: Tenths(120),
        correction_thresholds: CorrectionThresholds {
            fix: Seconds(0),
            output: Seconds(32),
        },
        output_format: OutputFormat::Binary,
    }
}

#[test]
fn settings_start_at_the_manuals_defaults() {
    for identity in PRESETS {
        let mut engine = Engine::new(identity, Options::DEFAULT);
        assert_eq!(engine.settings(), defaults());
        // Set to its defaults when at them, it is reset and nothing
        // changes.
        assert_eq!(tell(&mut engine, CF_SET_TO_DEFAULTS, &[]), [Event::Reset]);
        assert_eq!(engine.settings(), defaults());
    }
}

/// Each setting is read back as its command gave it and is the one
/// setting the command changes; given again, or asked for, nothing
/// changes.
#[test]
fn each_setting_reads_back_after_its_command() {
    /// A command, the data that asks for its value, the setting it
    /// sets and the settings after it.
    type Case = (MessageId, Vec<u8>, Vec<u8>, Setting, Settings);
    const POSITION_ASKED: u8 = 0x99;
    let hold_position = [
        &MADE_UP.latitude.0.to_be_bytes()[..],
        &MADE_UP.longitude.0.to_be_bytes()[..],
        &height_data(MADE_UP.height),
    ]
    .concat();
    let hold_position_asked = [&[0x7f, 0xff, 0xff, 0xff].repeat(3)[..], &[0xff]].concat();
    let ellipsoid = Height {
        height: Centimeters(-4321),
        kind: HeightKind::Ellipsoid,
    };
    let traim = |algorithm: u8, alarm_limit: u16, pps_mode: u8| {
        let [high, low] = alarm_limit.to_be_bytes();
        [&[0, algorithm, high, low, pps_mode][..], &[0; 10]].concat()
    };
    let cases: [Case; 29] = [
        (
            AH_SATELLITE_SELECT,
            vec![1],
            vec![0xff],
            Setting::SatelliteSelect,
            {
                let mut after = defaults();
                after.satellite_select = SatelliteSelect::Manual;
                after
            },
        ),
        (
            AH_SATELLITE_SELECT,
            vec![2],
            vec![0xff],
            Setting::SatelliteSelect,
            {
                let mut after = defaults();
                after.satellite_select = SatelliteSelect::HighestInSky;
                after
            },
        ),
        (
            AI_MANUAL_SATELLITE,
            vec![8, 37],
            vec![8, 0xff],
            Setting::ManualSatellites,
            {
                let mut after = defaults();
                after.manual_satellites[7] = 37;
                after
            },
        ),
        (AJ_XDOP_TYPE, vec![4], vec![0xff], Setting::DopKind, {
            let mut after = defaults();
            after.dop_kind = DopKind::Time;
            after
        }),
        (
            AM_IGNORE_LIST,
            vec![0, 0x80, 0, 0, 0x04],
            [&[0xff][..], &[0; 4]].concat(),
            Setting::IgnoreList,
            {
                let mut after = defaults();
                after.ignore_list = PrnSet::EMPTY.with(prn(3)).with(prn(32));
                after
            },
        ),
        (
            AK_XDOP_HYSTERESIS,
            vec![0x03, 0xe7],
            vec![0xff; 2],
            Setting::XdopHysteresis,
            {
                let mut after = defaults();
                after.xdop_hysteresis = Tenths(999);
                after
            },
        ),
        (
            AN_ALMANAC_UPDATE,
            vec![0],
            vec![0xff],
            Setting::AlmanacUpdate,
            {
                let mut after = defaults();
                after.almanac_update = false;
                after
            },
        ),
        (
            AQ_ATMOSPHERIC_CORRECTION,
            vec![0],
            vec![0xff],
            Setting::AtmosphericCorrection,
            {
                let mut after = defaults();
                after.atmospheric_correction.ionospheric = false;
                after
            },
        ),
        (
            AR_FIX_ALGORITHM,
            vec![0],
            vec![0xff],
            Setting::FixAlgorithm,
            {
                let mut after = defaults();
                after.fix_algorithm = FixAlgorithm::BestFour;
                after
            },
        ),
        (
            AX_EPOCH_OFFSET,
            vec![0x01, 0xf4],
            vec![0xff; 2],
            Setting::EpochOffset,
            {
                let mut after = defaults();
                after.epoch_offset = Milliseconds(500);
                after
            },
        ),
        (
            AC_HDOP_THRESHOLD,
            vec![0, 55],
            vec![0xff; 2],
            Setting::HdopThreshold,
            {
                let mut after = defaults();
                after.hdop_threshold = Tenths(55);
                after
            },
        ),
        (AG_MASK_ANGLE, vec![25], vec![0xff], Setting::MaskAngle, {
            let mut after = defaults();
            after.mask_angle = Degrees(25);
            after
        }),
        (
            AT_POSITION_HOLD,
            vec![1],
            vec![0xff],
            Setting::PositionHold,
            {
                let mut after = defaults();
                after.position_hold = true;
                after
            },
        ),
        (
            AS_HOLD_POSITION,
            hold_position,
            hold_position_asked,
            Setting::HeldPosition,
            {
                let mut after = defaults();
                after.held_position = MADE_UP;
                after
            },
        ),
        (
            AV_ALTITUDE_HOLD,
            vec![1],
            vec![0xff],
            Setting::AltitudeHold,
            {
                let mut after = defaults();
                after.altitude_hold = true;
                after
            },
        ),
        (
            AU_ALTITUDE_HOLD_HEIGHT,
            height_data(MADE_UP.height),
            vec![0xff; 5],
            Setting::AltitudeHoldHeight,
            {
                let mut after = defaults();
                after.altitude_hold_height = MADE_UP.height;
                after
            },
        ),
        (AW_TIME_MODE, vec![0], vec![0xff], Setting::TimeBase, {
            let mut after = defaults();
            after.time_base = TimeBase::Gps;
            after
        }),
        (
            AZ_CABLE_DELAY,
            250_u32.to_be_bytes().to_vec(),
            vec![0xff; 4],
            Setting::CableDelay,
            {
                let mut after = defaults();
                after.cable_delay = Nanoseconds(250);
                after
            },
        ),
        (
            AY_PULSE_OFFSET,
            999_999_999_u32.to_be_bytes().to_vec(),
            vec![0xff; 4],
            Setting::PulseOffset,
            {
                let mut after = defaults();
                after.pulse_offset = Nanoseconds(999_999_999);
                after
            },
        ),
        (
            BN_TIME_RAIM,
            traim(0, 10, 0),
            vec![0xff; 15],
            Setting::PulseMode,
            {
                let mut after = defaults();
                after.pulse_mode = PulseMode::Off;
                after
            },
        ),
        (
            BN_TIME_RAIM,
            traim(0, 10, 2),
            vec![0xff; 15],
            Setting::PulseMode,
            {
                let mut after = defaults();
                after.pulse_mode = PulseMode::Tracking;
                after
            },
        ),
        (
            EN_TIME_RAIM,
            traim(0, 10, 3),
            vec![0xff; 15],
            Setting::PulseMode,
            {
                let mut after = defaults();
                after.pulse_mode = PulseMode::Confirmed;
                after
            },
        ),
        (
            BN_TIME_RAIM,
            traim(1, 10, 1),
            vec![0xff; 15],
            Setting::RaimAlgorithm,
            {
                let mut after = defaults();
                after.raim_algorithm = true;
                after
            },
        ),
        (
            EN_TIME_RAIM,
            traim(0, 0xffff, 1),
            vec![0xff; 15],
            Setting::RaimAlarmLimit,
            {
                let mut after = defaults();
                after.raim_alarm_limit = HundredNanoseconds(0xffff);
                after
            },
        ),
        (
            AB_APPLICATION_TYPE,
            vec![4],
            vec![0xff],
            Setting::Application,
            {
                let mut after = defaults();
                after.application = Application::Static;
                after
            },
        ),
        (
            AD_LATITUDE,
            MADE_UP.latitude.0.to_be_bytes().to_vec(),
            vec![POSITION_ASKED; 4],
            Setting::InitialPosition,
            {
                let mut after = defaults();
                after.initial_position.latitude = MADE_UP.latitude;
                after
            },
        ),
        (
            AE_LONGITUDE,
            MADE_UP.longitude.0.to_be_bytes().to_vec(),
            vec![POSITION_ASKED; 4],
            Setting::InitialPosition,
            {
                let mut after = defaults();
                after.initial_position.longitude = MADE_UP.longitude;
                after
            },
        ),
        (
            AF_HEIGHT,
            height_data(MADE_UP.height),
            vec![POSITION_ASKED; 5],
            Setting::InitialPosition,
            {
                let mut after = defaults();
                after.initial_position.height = MADE_UP.height;
                after
            },
        ),
        (
            AF_HEIGHT,
            height_data(ellipsoid),
            vec![POSITION_ASKED; 5],
            Setting::InitialPosition,
            {
                let mut after = defaults();
                after.initial_position.height = ellipsoid;
                after
            },
        ),
    ];
    for (id, data, asked, setting, after) in cases {
        let mut engine = Engine::new(&VP_EIGHT, Options::DEFAULT);
        assert_eq!(tell(&mut engine, id, &asked), [], "{id:?} asked");
        assert_eq!(engine.settings(), defaults(), "{id:?} asked");
        assert_eq!(
            tell(&mut engine, id, &data),
            [changed(&[setting])],
            "{id:?}"
        );
        assert_eq!(engine.settings(), after, "{id:?}");
        assert_eq!(tell(&mut engine, id, &data), [], "{id:?} again");
        assert_eq!(tell(&mut engine, id, &asked), [], "{id:?} asked after");
        assert_eq!(engine.settings(), after, "{id:?} asked after");
    }
}

/// `@@AD` (VP p. 20) and a UT Plus's `@@Aq` (UT p. 6.28), each read
/// back.
#[test]
fn thresholds_and_a_ut_plus_correction_read_back() {
    let mut engine = Engine::new(&VP_EIGHT, Options::DEFAULT);
    assert_eq!(
        tell(&mut engine, AD_CORRECTION_THRESHOLDS, &[16, 8]),
        [changed(&[Setting::CorrectionThresholds])]
    );
    assert_eq!(
        engine.settings().correction_thresholds,
        CorrectionThresholds {
            fix: Seconds(16),
            output: Seconds(8),
        }
    );
    // A VP has no mode past 1 (VP p. 51).
    assert_eq!(tell(&mut engine, AQ_ATMOSPHERIC_CORRECTION, &[2]), []);

    let mut engine = Engine::new(&UT_PLUS_3_1, Options::DEFAULT);
    for (mode, ionospheric, tropospheric) in [(3, true, true), (2, false, true), (0, false, false)]
    {
        assert_eq!(
            tell(&mut engine, AQ_ATMOSPHERIC_CORRECTION, &[mode]),
            [changed(&[Setting::AtmosphericCorrection])]
        );
        assert_eq!(
            engine.settings().atmospheric_correction,
            AtmosphericCorrection {
                ionospheric,
                tropospheric,
            }
        );
    }
}

/// VP pp. 5, 6, 13 and 14: `@@Aa` and `@@Ac` set the clock of an engine
/// whose record tells no time, which then runs on from there; one
/// whose record tells it ignores them, as it does a value out of range.
#[test]
fn the_time_and_date_set_the_clock_the_record_does_not() {
    let mut engine = fixing(&VP_EIGHT);
    let record = Record::EMPTY;
    // Not kept powered, it starts at 12:00:00 (VP p. 5).
    let frames = ask(&mut engine, &record, AA_TIME_OF_DAY, &[0xff; 3]);
    assert_eq!(only(&frames, AA_TIME_OF_DAY), [12, 0, 0]);
    let frames = ask(&mut engine, &record, AC_DATE, &[5, 17, 0x07, 0xd1]);
    assert_eq!(only(&frames, AC_DATE), [5, 17, 0x07, 0xd1]);
    let frames = ask(&mut engine, &record, AA_TIME_OF_DAY, &[23, 59, 58]);
    assert_eq!(only(&frames, AA_TIME_OF_DAY), [23, 59, 58]);
    let mut sent = Sent::default();
    engine.second(&record, NOW, &mut sent);
    engine.second(&record, NOW, &mut sent);
    engine.second(&record, NOW, &mut sent);
    let frames = ask(&mut engine, &record, AC_DATE, &[0xff; 4]);
    assert_eq!(only(&frames, AC_DATE), [5, 18, 0x07, 0xd1]);
    let frames = ask(&mut engine, &record, AA_TIME_OF_DAY, &[0xff; 3]);
    assert_eq!(only(&frames, AA_TIME_OF_DAY), [0, 0, 0]);

    // Out of range: the 31st of February, the year 1979, 60 seconds.
    for (id, data) in [
        (AC_DATE, &[2, 31, 0x07, 0xd1][..]),
        (AC_DATE, &[5, 17, 0x07, 0xbb]),
        (AC_DATE, &[5, 17, 0x08, 0x1e]),
    ] {
        let frames = ask(&mut engine, &record, id, data);
        assert_eq!(only(&frames, id), [5, 18, 0x07, 0xd1]);
    }
    for data in [[24, 0, 0], [0, 60, 0], [23, 59, 60]] {
        let frames = ask(&mut engine, &record, AA_TIME_OF_DAY, &data);
        assert_eq!(only(&frames, AA_TIME_OF_DAY), [0, 0, 0]);
    }

    let record = full();
    let mut engine = fixing(&VP_EIGHT);
    let before = ask(&mut engine, &record, AA_TIME_OF_DAY, &[0xff; 3]);
    assert_eq!(
        ask(&mut engine, &record, AA_TIME_OF_DAY, &[1, 2, 3]),
        before
    );
    let before = ask(&mut engine, &record, AC_DATE, &[0xff; 4]);
    assert_eq!(
        ask(&mut engine, &record, AC_DATE, &[5, 17, 0x07, 0xd1]),
        before
    );
}

/// VP pp. 17, 21 and 25: the position given to start from is the one
/// reported until the record has one.
#[test]
fn the_initial_position_is_reported_until_there_is_a_fix() {
    let mut engine = fixing(&VP_EIGHT);
    let mut record = full();
    record.position = None;
    ask(
        &mut engine,
        &record,
        AD_LATITUDE,
        &MADE_UP.latitude.0.to_be_bytes(),
    );
    ask(
        &mut engine,
        &record,
        AE_LONGITUDE,
        &MADE_UP.longitude.0.to_be_bytes(),
    );
    ask(
        &mut engine,
        &record,
        AF_HEIGHT,
        &height_data(MADE_UP.height),
    );
    let frames = ask(&mut engine, &record, EA_POSITION_STATUS, &[0]);
    let data = only(&frames, EA_POSITION_STATUS);
    assert_eq!(data[11..15], MADE_UP.latitude.0.to_be_bytes());
    assert_eq!(data[15..19], MADE_UP.longitude.0.to_be_bytes());
    assert_eq!(data[19..23], MADE_UP.height.height.0.to_be_bytes());
    assert_eq!(data[23..27], MADE_UP.height.height.0.to_be_bytes());
    let frames = ask(&mut engine, &record, AD_LATITUDE, &[0x99; 4]);
    assert_eq!(only(&frames, AD_LATITUDE), MADE_UP.latitude.0.to_be_bytes());

    let record = full();
    let frames = ask(&mut engine, &record, AD_LATITUDE, &[0x99; 4]);
    assert_eq!(
        only(&frames, AD_LATITUDE),
        record.position.unwrap().latitude.0.to_be_bytes()
    );
}

/// VP pp. 77 and 79: in mode 1 the xDOP table and the almanac status
/// are sent again when they change, and only then; mode 0 ends it.
#[test]
fn a_table_asked_for_at_each_change_is_sent_when_it_changes() {
    /// A message and a change to a record that changes it.
    type Case = (MessageId, fn(&mut Record));
    let cases: [Case; 2] = [
        (BC_XDOP_TABLE, |record| {
            let mut table = XdopTable::new(DopKind::Position);
            assert!(table.push(XdopCombination::EMPTY));
            record.derived.xdops = Some(table);
        }),
        (BD_ALMANAC_STATUS, |record| record.almanac = None),
    ];
    for (id, change) in cases {
        let mut engine = fixing(&VP_EIGHT);
        let mut record = full();
        assert_eq!(ask(&mut engine, &record, id, &[1]).len(), 1);
        let mut sent = Sent::default();
        engine.second(&record, NOW, &mut sent);
        assert_eq!(sent.0, Vec::<Vec<u8>>::new());
        change(&mut record);
        engine.second(&record, NOW, &mut sent);
        engine.second(&record, NOW, &mut sent);
        assert_eq!(sent.0, ask(&mut engine, &record, id, &[0]));
        let mut sent = Sent::default();
        engine.second(&full(), NOW, &mut sent);
        assert_eq!(sent.0, Vec::<Vec<u8>>::new());
    }
}

/// VP pp. 120 and 121: `@@Ci` is not answered; its formats 1 and 2 are
/// kept and told, and no other value is.  The engine goes on in binary.
#[test]
fn a_switched_format_is_held_and_told() {
    let record = full();
    for (format, switched) in [(1, OutputFormat::Nmea), (2, OutputFormat::Loran)] {
        let mut engine = fixing(&VP_EIGHT);
        ask(&mut engine, &record, EA_POSITION_STATUS, &[1]);
        assert_eq!(tell(&mut engine, CI_IO_FORMAT, &[0]), []);
        assert_eq!(
            tell(&mut engine, CI_IO_FORMAT, &[format]),
            [changed(&[Setting::OutputFormat])]
        );
        assert_eq!(engine.settings().output_format, switched);
        let mut sent = Sent::default();
        engine.second(&record, NOW, &mut sent);
        assert_eq!(sent.0.len(), 1);
        engine.receive(&command(CJ_RECEIVER_ID, &[]), &record, NOW, &mut sent);
        assert_eq!(sent.0.len(), 2);
        assert_eq!(engine.settings().output_format, switched);
    }
}

/// VP pp. 97, 98, 136 and 137: a magnetic variation is within 180
/// degrees, and a velocity and a clock bias no less than -32767.
#[test]
fn an_extension_keeps_to_the_manuals_ranges() {
    let mut record = full();
    record.derived.magnetic_variation = Some(TenthDegreesEast(1801));
    record.derived.velocity = Some(VelocityAxes {
        north: DecimetersPerSecond(i16::MIN),
        east: DecimetersPerSecond(i16::MAX),
        up: DecimetersPerSecond(i16::MIN),
    });
    let mut solver = Solver::ZERO;
    solver.clock_bias = Decimeters(i16::MIN);
    record.derived.solver = Some(solver);
    let mut engine = fixing(&VP_EIGHT);
    let frames = ask(&mut engine, &record, EK_EXTENSION, &[0]);
    let data = only(&frames, EK_EXTENSION);
    assert_eq!(data[10..12], 1800_i16.to_be_bytes());
    assert_eq!(data[12..18], [0x80, 0x01, 0x7f, 0xff, 0x80, 0x01]);
    assert_eq!(data[60..62], [0x80, 0x01]);

    record.derived.magnetic_variation = Some(TenthDegreesEast(-1801));
    let frames = ask(&mut engine, &record, EK_EXTENSION, &[0]);
    assert_eq!(
        only(&frames, EK_EXTENSION)[10..12],
        (-1800_i16).to_be_bytes()
    );
}

/// The other application types (VP p. 12), each read back.
#[test]
fn every_application_type_reads_back() {
    let mut engine = Engine::new(&VP_EIGHT, Options::DEFAULT);
    for (kind, application) in [
        (0, Application::Air),
        (1, Application::Handheld),
        (3, Application::Marine),
        (4, Application::Static),
        (2, Application::Land),
    ] {
        assert_eq!(
            tell(&mut engine, AB_APPLICATION_TYPE, &[kind]),
            [changed(&[Setting::Application])]
        );
        assert_eq!(engine.settings().application, application);
    }
}

/// A value a command does not take changes nothing (VP pp. 28, 67 and
/// 104), nor does a command the engine does not take: a UT Plus has no
/// `@@AB`, and a deaf engine hears none.
#[test]
fn a_command_not_taken_changes_no_setting() {
    let mut engine = Engine::new(&VP_EIGHT, Options::DEFAULT);
    assert_eq!(tell(&mut engine, AG_MASK_ANGLE, &[90]), []);
    assert_eq!(
        tell(
            &mut engine,
            AY_PULSE_OFFSET,
            &1_000_000_000_u32.to_be_bytes()
        ),
        []
    );
    assert_eq!(
        tell(
            &mut engine,
            EN_TIME_RAIM,
            &[0, 1, 0, 10, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]
        ),
        []
    );
    assert_eq!(engine.settings(), defaults());

    let mut engine = Engine::new(&UT_PLUS_3_1, Options::DEFAULT);
    assert_eq!(tell(&mut engine, AB_APPLICATION_TYPE, &[4]), []);
    assert_eq!(engine.settings(), defaults());

    let mut options = Options::DEFAULT;
    options.deaf = true;
    let mut engine = Engine::new(&VP_EIGHT, options);
    assert_eq!(tell(&mut engine, AG_MASK_ANGLE, &[25]), []);
    assert_eq!(tell(&mut engine, CF_SET_TO_DEFAULTS, &[]), []);
    assert_eq!(engine.settings(), defaults());
}

/// VP pp. 104 and 140: a Time RAIM setup with a byte out of range is
/// not taken, none of it.
#[test]
fn a_time_raim_setup_out_of_range_changes_nothing() {
    let mut engine = Engine::new(&VP_EIGHT, Options::DEFAULT);
    for setup in [[0, 2, 0, 3, 3], [0, 1, 0, 2, 3], [0, 1, 0, 3, 4]] {
        let data = [&setup[..], &[0; 10]].concat();
        assert_eq!(tell(&mut engine, BN_TIME_RAIM, &data), []);
        assert_eq!(tell(&mut engine, EN_TIME_RAIM, &data), []);
    }
    assert_eq!(engine.settings(), defaults());
}

/// VP pp. 116 and 117: `@@Cf` puts every setting back, and is a reset
/// that names the settings it changed.
#[test]
fn set_to_defaults_is_a_reset_that_names_what_it_put_back() {
    let mut engine = Engine::new(&VP_EIGHT, Options::DEFAULT);
    tell(&mut engine, AG_MASK_ANGLE, &[25]);
    tell(&mut engine, AW_TIME_MODE, &[0]);
    tell(
        &mut engine,
        BN_TIME_RAIM,
        &[0, 1, 0, 3, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
    );
    tell(&mut engine, AD_LATITUDE, &MADE_UP.latitude.0.to_be_bytes());
    assert_eq!(
        tell(&mut engine, CF_SET_TO_DEFAULTS, &[]),
        [
            Event::Reset,
            changed(&[
                Setting::MaskAngle,
                Setting::TimeBase,
                Setting::PulseMode,
                Setting::RaimAlgorithm,
                Setting::RaimAlarmLimit,
                Setting::InitialPosition,
            ]),
        ]
    );
    assert_eq!(engine.settings(), defaults());
}

/// The commands of one call are told of together, a reset first
/// wherever it came: the settings named are those to read afterward.
#[test]
fn one_calls_commands_are_told_of_together() {
    let mut engine = Engine::new(&VP_EIGHT, Options::DEFAULT);
    let bytes = [
        command(AG_MASK_ANGLE, &[25]),
        command(CF_SET_TO_DEFAULTS, &[]),
        command(AW_TIME_MODE, &[0]),
    ]
    .concat();
    let mut sent = Sent::default();
    let events = engine.receive(&bytes, &full(), NOW, &mut sent);
    assert!(!events.is_empty());
    assert_eq!(
        events.collect::<Vec<_>>(),
        [
            Event::Reset,
            changed(&[Setting::MaskAngle, Setting::TimeBase]),
        ]
    );
    assert_eq!(sent.0.len(), 3);
    let mut after = defaults();
    after.time_base = TimeBase::Gps;
    assert_eq!(engine.settings(), after);

    // Half a command changes nothing until the rest comes.
    let frame = command(AG_MASK_ANGLE, &[30]);
    let (head, tail) = frame.split_at(3);
    let mut sent = Sent::default();
    assert_eq!(engine.receive(head, &full(), NOW, &mut sent), Events::NONE);
    assert_eq!(
        engine
            .receive(tail, &full(), NOW, &mut sent)
            .collect::<Vec<_>>(),
        [changed(&[Setting::MaskAngle])]
    );
}

/// What is held is what the engine reports while it holds a position:
/// the settings and the messages agree (VP pp. 17, 21 and 55).
#[test]
fn the_held_position_read_is_the_one_reported() {
    let mut engine = Engine::new(&VP_EIGHT, Options::DEFAULT);
    let record = full();
    let data = [
        &MADE_UP.latitude.0.to_be_bytes()[..],
        &MADE_UP.longitude.0.to_be_bytes()[..],
        &height_data(MADE_UP.height),
    ]
    .concat();
    ask(&mut engine, &record, AS_HOLD_POSITION, &data);
    ask(&mut engine, &record, AT_POSITION_HOLD, &[1]);
    let held = engine.settings().held_position;
    let frames = ask(&mut engine, &record, AD_LATITUDE, &[0x99; 4]);
    assert_eq!(only(&frames, AD_LATITUDE), held.latitude.0.to_be_bytes());
    let frames = ask(&mut engine, &record, AE_LONGITUDE, &[0x99; 4]);
    assert_eq!(only(&frames, AE_LONGITUDE), held.longitude.0.to_be_bytes());
    // Asking where it is does not move where it was told to start.
    assert_eq!(engine.settings().initial_position, Location::default());
}
