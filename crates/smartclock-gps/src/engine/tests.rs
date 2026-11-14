//! Each message against its layout in the manuals, and what the test
//! vectors do not reach: a record with gaps, a shorted antenna, a leap
//! second removed, `@@Gj`, and the options.

use std::vec;
use std::vec::Vec;

use crate::engine::Engine;
use crate::engine::Options;
use crate::engine::Output;
use crate::engine::identity::Identity;
use crate::engine::identity::UT_PLUS_2_2;
use crate::engine::identity::UT_PLUS_3_1;
use crate::engine::identity::VP_EIGHT;
use crate::engine::identity::VP_EIGHT_WITHOUT_TIME_RAIM;
use crate::engine::identity::VP_SIX;
use crate::oncore::AA_TIME_OF_DAY;
use crate::oncore::AB_GMT_CORRECTION;
use crate::oncore::AC_DATE;
use crate::oncore::BA_POSITION_STATUS;
use crate::oncore::BJ_LEAP_SECOND_PENDING;
use crate::oncore::BN_TIME_RAIM;
use crate::oncore::BO_UTC_OFFSET;
use crate::oncore::CA_SELF_TEST;
use crate::oncore::CG_FIX_MODE;
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
use crate::record::Antenna;
use crate::record::Dops;
use crate::record::Ecef;
use crate::record::Fix;
use crate::record::GpsTime;
use crate::record::Position;
use crate::record::Record;
use crate::record::Satellite;
use crate::record::Timing;
use crate::record::Tracking;
use crate::record::Velocity;
use crate::time::DateTime;
use crate::time::GpsSeconds;
use crate::time::LeapChange;
use crate::time::LeapEvent;
use crate::units::Centimeters;
use crate::units::CentimetersPerSecond;
use crate::units::DecibelHertz;
use crate::units::Degrees;
use crate::units::MilliArcSeconds;
use crate::units::Milliseconds;
use crate::units::Nanoseconds;
use crate::units::Prn;
use crate::units::Seconds;
use crate::units::TenthDegrees;
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
    assert_eq!(data[68], 0x08 | 0x02);

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
    assert_eq!(only(&frames, EK_EXTENSION), [0; 64]);
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
