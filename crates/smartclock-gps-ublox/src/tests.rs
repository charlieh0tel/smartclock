//! Frames built by hand from the layouts of UBX-13003221 R28, through
//! the deframer, the decoders and the assembler.  The position is the
//! one `docs/firmware/gps.md` gives: 37.3255833 N, 121.9978333 W, 55 m.

use std::boxed::Box;
use std::vec;
use std::vec::Vec;

use crate::assembler::Assembler;
use crate::assembler::DECLARATION;
use crate::frame::Deframer;
use crate::frame::EncodeError;
use crate::frame::FrameError;
use crate::frame::MessageId;
use crate::frame::OVERHEAD;
use crate::frame::checksum;
use crate::frame::encode;
use crate::message::ACK_ACK;
use crate::message::ACK_NAK;
use crate::message::MESSAGES;
use crate::message::MON_HW;
use crate::message::Message;
use crate::message::NAV_CLOCK;
use crate::message::NAV_DOP;
use crate::message::NAV_EOE;
use crate::message::NAV_POSECEF;
use crate::message::NAV_PVT;
use crate::message::NAV_SAT;
use crate::message::NAV_TIMEGPS;
use crate::message::NAV_TIMELS;
use crate::message::NAV_VELECEF;
use crate::message::PayloadLength;
use crate::message::RXM_RAWX;
use crate::message::RXM_SFRBX;
use crate::message::TIM_TP;
use crate::message::payload_length;
use crate::nav::LeapNotice;
use crate::nav::nav_sat_length;
use crate::rxm::rxm_rawx_length;
use crate::units::Picoseconds;
use smartclock_gps::NoRoom;
use smartclock_gps::record::Antenna;
use smartclock_gps::record::Dops;
use smartclock_gps::record::Ecef;
use smartclock_gps::record::Fix;
use smartclock_gps::record::GpsTime;
use smartclock_gps::record::Health;
use smartclock_gps::record::Position;
use smartclock_gps::record::Raim;
use smartclock_gps::record::RaimAbility;
use smartclock_gps::record::RaimSolution;
use smartclock_gps::record::Record;
use smartclock_gps::record::SelfTest;
use smartclock_gps::record::Tracking;
use smartclock_gps::record::Velocity;
use smartclock_gps::record::field::UNFILLED;
use smartclock_gps::record::field::changed;
use smartclock_gps::source::Presence;
use smartclock_gps::source::Source;
use smartclock_gps::source::Tick;
use smartclock_gps::source::replay::Assemble;
use smartclock_gps::source::replay::Replay;
use smartclock_gps::time::GpsSeconds;
use smartclock_gps::time::GpsWeek;
use smartclock_gps::time::LeapChange;
use smartclock_gps::time::LeapEvent;
use smartclock_gps::units::Centimeters;
use smartclock_gps::units::CentimetersPerSecond;
use smartclock_gps::units::DecibelHertz;
use smartclock_gps::units::Degrees;
use smartclock_gps::units::MilliArcSeconds;
use smartclock_gps::units::Milliseconds;
use smartclock_gps::units::Nanoseconds;
use smartclock_gps::units::Seconds;
use smartclock_gps::units::TenthDegrees;
use smartclock_gps::units::Tenths;

/// Room for a `NAV-SAT` of every GPS satellite.
type Bench = Assembler<{ nav_sat_length(32) }>;

/// The week of 2016-12-31, whose last second was a leap second, and
/// noon of that day, a Saturday, in its milliseconds.
const WEEK: u16 = 1929;
const NOON: u32 = (6 * 24 + 12) * 3600 * 1000;
/// GPS less UTC until that leap second.
const BEFORE: Seconds<i8> = Seconds(17);

/// The leap second: the first second of the next week is 2017-01-01
/// 00:00:00 in GPS time, and UTC reached 23:59:60 seventeen later.
fn leap() -> LeapEvent {
    LeapEvent {
        at: second(WEEK + 1, 17_000),
        change: LeapChange::Insert,
    }
}

fn second(week: u16, milliseconds: u32) -> GpsSeconds {
    GpsSeconds::nearest(
        GpsWeek::new(week),
        Milliseconds(milliseconds),
        Nanoseconds(0),
    )
    .unwrap()
}

fn frame(id: MessageId, payload: &[u8]) -> Vec<u8> {
    let mut out = vec![0; OVERHEAD + payload.len()];
    assert_eq!(encode(id, payload, &mut out), Ok(out.len()));
    out
}

fn stream(messages: &[(MessageId, &[u8])]) -> Vec<u8> {
    messages
        .iter()
        .flat_map(|&(id, payload)| frame(id, payload))
        .collect()
}

/// A payload of `N` bytes with `fields` at their offsets.
fn payload<const N: usize>(fields: &[(usize, &[u8])]) -> [u8; N] {
    let mut out = [0; N];
    for &(offset, field) in fields {
        out[offset..offset + field.len()].copy_from_slice(field);
    }
    out
}

/// `NAV-PVT`'s fields that the decoder reads (pp. 375-378).
#[derive(Clone, Copy)]
struct Pvt {
    time_of_week: u32,
    fix_type: u8,
    flags: u8,
    longitude: i32,
    latitude: i32,
    height: i32,
    sea_level: i32,
    speed: i32,
    heading: i32,
    flags3: u16,
}

/// A 3D fix at the test position, moving east.
const PVT: Pvt = Pvt {
    time_of_week: NOON,
    fix_type: 3,
    flags: 0x01,
    longitude: -1_219_978_333,
    latitude: 373_255_833,
    height: 22_995,
    sea_level: 55_000,
    speed: 1_234,
    heading: 9_000_000,
    flags3: 0,
};

impl Pvt {
    fn payload(self) -> [u8; 92] {
        payload(&[
            (0, &self.time_of_week.to_le_bytes()),
            (20, &[self.fix_type, self.flags]),
            (24, &self.longitude.to_le_bytes()),
            (28, &self.latitude.to_le_bytes()),
            (32, &self.height.to_le_bytes()),
            (36, &self.sea_level.to_le_bytes()),
            (60, &self.speed.to_le_bytes()),
            (64, &self.heading.to_le_bytes()),
            (78, &self.flags3.to_le_bytes()),
        ])
    }
}

/// `NAV-TIMEGPS` (p. 397): `valid` 7 is time of week, week and leap
/// seconds all valid.
fn time_gps(time_of_week: u32, fraction: i32, week: i16, leap: i8, valid: u8) -> [u8; 16] {
    payload(&[
        (0, &time_of_week.to_le_bytes()),
        (4, &fraction.to_le_bytes()),
        (8, &week.to_le_bytes()),
        (10, &leap.to_le_bytes()),
        (11, &[valid]),
        (12, &25_u32.to_le_bytes()),
    ])
}

/// `NAV-TIMELS`'s fields (pp. 398-400).
#[derive(Clone, Copy)]
struct Ls {
    time_of_week: u32,
    version: u8,
    count_source: u8,
    count: i8,
    change_source: u8,
    change: i8,
    until: i32,
    week: u16,
    day: u16,
    valid: u8,
}

/// The leap second of 2016, announced at noon of its day by GPS.
const LS: Ls = Ls {
    time_of_week: NOON,
    version: 0,
    count_source: 2,
    count: 17,
    change_source: 2,
    change: 1,
    until: 12 * 3600 + 17,
    week: WEEK,
    day: 7,
    valid: 0b11,
};

impl Ls {
    fn payload(self) -> [u8; 24] {
        payload(&[
            (0, &self.time_of_week.to_le_bytes()),
            (4, &[self.version]),
            (8, &[self.count_source]),
            (9, &self.count.to_le_bytes()),
            (10, &[self.change_source]),
            (11, &self.change.to_le_bytes()),
            (12, &self.until.to_le_bytes()),
            (16, &self.week.to_le_bytes()),
            (18, &self.day.to_le_bytes()),
            (23, &[self.valid]),
        ])
    }

    /// The same announcement `seconds` before the leap second.
    fn before_leap(self, seconds: i32) -> Self {
        Self {
            time_of_week: time_before_leap(seconds),
            until: seconds,
            ..self
        }
    }
}

/// The `iTOW` of the epoch `seconds` before the leap second, which is
/// 17 seconds into the week after [`WEEK`].
fn time_before_leap(seconds: i32) -> u32 {
    u32::try_from((17 - seconds) * 1000).unwrap()
}

/// `NAV-DOP` (p. 361): geometric, position, time, vertical, horizontal.
fn dop(time_of_week: u32, dops: [u16; 5]) -> [u8; 18] {
    let [geometric, position, time, vertical, horizontal] = dops.map(u16::to_le_bytes);
    payload(&[
        (0, &time_of_week.to_le_bytes()),
        (4, &geometric),
        (6, &position),
        (8, &time),
        (10, &vertical),
        (12, &horizontal),
    ])
}

/// `NAV-POSECEF` (p. 374).
fn pos_ecef(time_of_week: u32, position: [i32; 3]) -> [u8; 20] {
    let [x, y, z] = position.map(i32::to_le_bytes);
    payload(&[(0, &time_of_week.to_le_bytes()), (4, &x), (8, &y), (12, &z)])
}

/// `NAV-EOE` (p. 363).
fn eoe(time_of_week: u32) -> [u8; 4] {
    time_of_week.to_le_bytes()
}

/// One satellite's block of `NAV-SAT` (p. 381).
#[derive(Clone, Copy)]
struct Sat {
    system: u8,
    id: u8,
    signal: u8,
    elevation: i8,
    azimuth: i16,
    flags: u32,
}

/// `flags` of a satellite used in the fix: code and carrier locked,
/// used, healthy, ephemeris held.
const SAT_IN_FIX: u32 = 5 | 1 << 3 | 1 << 4 | 1 << 11;

/// `NAV-SAT` (p. 381), version 1.
fn sat(time_of_week: u32, satellites: &[Sat]) -> Vec<u8> {
    let count = u8::try_from(satellites.len()).unwrap();
    let mut out = payload::<8>(&[(0, &time_of_week.to_le_bytes()), (4, &[1, count])]).to_vec();
    for satellite in satellites {
        out.extend(payload::<12>(&[
            (0, &[satellite.system, satellite.id, satellite.signal]),
            (3, &satellite.elevation.to_le_bytes()),
            (4, &satellite.azimuth.to_le_bytes()),
            (8, &satellite.flags.to_le_bytes()),
        ]));
    }
    out
}

/// `MON-HW` (p. 342) with `aStatus`.
fn hw(status: u8) -> [u8; 60] {
    payload(&[(20, &[status])])
}

/// `TIM-TP` (p. 432).
/// `TIM-TP`'s `flags` with `raim` not active and active, and with
/// `TpNotLocked` (p. 433).
const RAIM_NOT_ACTIVE: u8 = 1 << 2;
const RAIM_ACTIVE: u8 = 2 << 2;
const NOT_LOCKED: u8 = 1 << 5;

fn tp(time_of_week: u32, error: i32, week: u16, flags: u8) -> [u8; 16] {
    payload(&[
        (0, &time_of_week.to_le_bytes()),
        (8, &error.to_le_bytes()),
        (12, &week.to_le_bytes()),
        (14, &[flags]),
    ])
}

fn decode(id: MessageId, payload: &[u8]) -> Option<Message<'static>> {
    let bytes = frame(id, payload);
    // The message borrows the deframer's buffer.
    let deframer = Box::leak(Box::new(Deframer::<512>::new()));
    let (taken, event) = deframer.feed(&bytes);
    assert_eq!(taken, bytes.len());
    let message = Message::parse(event.unwrap().unwrap());
    assert_eq!(Message::parse_payload(id, payload), message);
    message
}

/// Every record `bytes` make.
fn records<const N: usize>(assembler: &mut Assembler<N>, bytes: &[u8]) -> Vec<Record> {
    let mut record = Record::EMPTY;
    bytes
        .iter()
        .filter_map(|&byte| assembler.push(byte, &mut record).then_some(record))
        .collect()
}

/// A whole epoch at `time_of_week` of [`WEEK`]: a fix at the test
/// position and the time, ended by `NAV-EOE`.
fn epoch(time_of_week: u32) -> Vec<u8> {
    stream(&[
        (
            NAV_PVT,
            &Pvt {
                time_of_week,
                ..PVT
            }
            .payload(),
        ),
        (
            NAV_TIMEGPS,
            &time_gps(time_of_week, 0, WEEK.cast_signed(), 17, 7),
        ),
        (NAV_EOE, &eoe(time_of_week)),
    ])
}

fn time(week: u16, milliseconds: u32) -> Option<GpsTime> {
    Some(GpsTime {
        seconds: second(week, milliseconds),
        subsecond: Nanoseconds(0),
    })
}

/// Section 32.4's sums, by hand: `CK_A` is 0x01, 0x62, 0x66, 0x66,
/// 0x76, 0x9d, 0x9d, 0x9d and `CK_B` their running sum.
#[test]
fn a_frame_is_sync_header_payload_and_fletcher_sum() {
    let frame = frame(NAV_EOE, &eoe(10_000));
    assert_eq!(
        frame,
        [
            0xb5, 0x62, 0x01, 0x61, 0x04, 0x00, 0x10, 0x27, 0x00, 0x00, 0x9d, 0x7c
        ]
    );
    assert_eq!(checksum(&frame[2..10]), [0x9d, 0x7c]);
    assert_eq!(checksum(&[]), [0, 0]);
    assert_eq!(OVERHEAD, 8);
}

#[test]
fn encoding_needs_room_and_a_length_that_fits() {
    let mut out = [0; 11];
    assert_eq!(
        encode(NAV_EOE, &eoe(0), &mut out),
        Err(EncodeError::NoRoom(NoRoom {
            needed: 12,
            room: 11
        }))
    );
    let long = vec![0; usize::from(u16::MAX) + 1];
    let mut out = vec![0; long.len() + OVERHEAD];
    assert_eq!(
        encode(MessageId::new(0x7f, 0x7f), &long, &mut out),
        Err(EncodeError::TooLong(long.len()))
    );
    assert_eq!(
        encode(MessageId::new(0x7f, 0x7f), &long[1..], &mut out),
        Ok(out.len() - 1)
    );
    // No payload at all.
    assert_eq!(
        frame(MessageId::new(0x0a, 0x04), &[]),
        [0xb5, 0x62, 0x0a, 0x04, 0x00, 0x00, 0x0e, 0x34]
    );
}

#[test]
fn every_message_has_its_documented_length() {
    for (id, length) in [
        (NAV_POSECEF, 20),
        (NAV_DOP, 18),
        (NAV_PVT, 92),
        (NAV_TIMEGPS, 16),
        (NAV_TIMELS, 24),
        (NAV_SAT, 8),
        (NAV_SAT, 8 + 12 * 32),
        (NAV_EOE, 4),
        (NAV_CLOCK, 20),
        (NAV_VELECEF, 20),
        (RXM_RAWX, 16),
        (RXM_RAWX, 16 + 32 * 20),
        (RXM_SFRBX, 8 + 4 * 10),
        (ACK_ACK, 2),
        (ACK_NAK, 2),
        (MON_HW, 60),
        (TIM_TP, 16),
    ] {
        let own = payload_length(id).unwrap();
        assert!(own.allows(length), "{id}");
        assert!(!own.allows(length + 1), "{id}");
        assert!(!own.allows(length - 1), "{id}");
    }
    assert_eq!(MESSAGES.len(), 15);
    assert_eq!(rxm_rawx_length(20), 656);
    assert_eq!(payload_length(MessageId::new(0x01, 0x03)), None);
    assert_eq!(nav_sat_length(32), 392);
}

/// A frame is found however the stream is cut into reads.
#[test]
fn a_frame_split_across_reads_is_one_frame() {
    let bytes = stream(&[(NAV_PVT, &PVT.payload()), (NAV_EOE, &eoe(NOON))]);
    for cut in 0..=bytes.len() {
        let mut deframer = Deframer::<92>::new();
        let mut found = Vec::new();
        for part in [&bytes[..cut], &bytes[cut..]] {
            let mut rest = part;
            while !rest.is_empty() {
                let (taken, event) = deframer.feed(rest);
                rest = &rest[taken..];
                if let Some(frame) = event {
                    let frame = frame.unwrap();
                    found.push((frame.id(), frame.payload().to_vec()));
                }
            }
        }
        assert_eq!(
            found,
            [
                (NAV_PVT, PVT.payload().to_vec()),
                (NAV_EOE, eoe(NOON).to_vec())
            ],
            "cut at {cut}"
        );
    }
}

fn events<const N: usize>(bytes: &[u8]) -> Vec<Result<MessageId, FrameError>> {
    let mut deframer = Deframer::<N>::new();
    bytes
        .iter()
        .filter_map(|&byte| {
            deframer
                .push(byte)
                .map(|event| event.map(|frame| frame.id()))
        })
        .collect()
}

#[test]
fn noise_and_a_stray_sync_byte_are_skipped() {
    let mut bytes = vec![0x00, 0x62, 0xb5, 0x00, 0xb5, 0xb5];
    bytes.extend(&frame(NAV_EOE, &eoe(NOON))[1..]);
    bytes.extend([0xb5, 0x61]);
    bytes.extend(frame(NAV_EOE, &eoe(NOON)));
    assert_eq!(events::<4>(&bytes), [Ok(NAV_EOE), Ok(NAV_EOE)]);
}

/// A known message whose length is not its own is dropped at its
/// header: the frame that follows at once is found.
#[test]
fn a_length_that_lies_is_not_followed() {
    let mut bytes = vec![0xb5, 0x62, 0x01, 0x07, 0xff, 0xff];
    bytes.extend(frame(NAV_EOE, &eoe(NOON)));
    assert_eq!(
        events::<92>(&bytes),
        [
            Err(FrameError::BadLength {
                id: NAV_PVT,
                length: 0xffff
            }),
            Ok(NAV_EOE)
        ]
    );
    // One byte short is as bad.
    let mut bytes = vec![0xb5, 0x62, 0x01, 0x07, 91, 0];
    bytes.extend(frame(NAV_EOE, &eoe(NOON)));
    assert_eq!(
        events::<92>(&bytes),
        [
            Err(FrameError::BadLength {
                id: NAV_PVT,
                length: 91
            }),
            Ok(NAV_EOE)
        ]
    );
}

#[test]
fn a_payload_past_the_buffer_is_not_followed() {
    let unknown = MessageId::new(0x0a, 0x04);
    let mut bytes = vec![0xb5, 0x62, 0x0a, 0x04, 0x11, 0x00];
    bytes.extend(frame(NAV_EOE, &eoe(NOON)));
    assert_eq!(
        events::<16>(&bytes),
        [
            Err(FrameError::Overflow {
                id: unknown,
                length: 17,
                capacity: 16
            }),
            Ok(NAV_EOE)
        ]
    );
    // What fits is framed, known or not.
    let bytes = frame(unknown, &[0xb5; 16]);
    assert_eq!(events::<16>(&bytes), [Ok(unknown)]);
    // A NAV-SAT with more satellites than there is room for.
    let bytes = frame(NAV_SAT, &sat(NOON, &[SAT; 2]));
    assert_eq!(
        events::<{ nav_sat_length(1) }>(&bytes)[0],
        Err(FrameError::Overflow {
            id: NAV_SAT,
            length: 32,
            capacity: 20
        })
    );
}

#[test]
fn a_bad_checksum_drops_the_frame_and_the_next_is_found() {
    let mut bytes = frame(NAV_EOE, &eoe(NOON));
    let good = [bytes[10], bytes[11]];
    bytes[7] ^= 0x01;
    let expected = checksum(&bytes[2..10]);
    bytes.extend(frame(NAV_EOE, &eoe(NOON)));
    assert_eq!(
        events::<4>(&bytes),
        [
            Err(FrameError::BadChecksum {
                id: NAV_EOE,
                expected,
                found: good
            }),
            Ok(NAV_EOE)
        ]
    );
}

/// A length damaged into another the checks allow costs no more than
/// the buffer's length of what follows.
#[test]
fn a_damaged_length_costs_at_most_the_buffer() {
    let unknown = MessageId::new(0x0a, 0x04);
    let mut bytes = frame(unknown, &[0; 2]);
    bytes[4] = 16;
    // The 16 bytes it now claims run through the next frame and into
    // the one after, which is lost with it; the third is found.
    for _ in 0..3 {
        bytes.extend(frame(NAV_EOE, &eoe(NOON)));
    }
    let events = events::<16>(&bytes);
    assert!(matches!(events[0], Err(FrameError::BadChecksum { .. })));
    assert_eq!(events[1..], [Ok(NAV_EOE)]);
}

#[test]
fn reset_forgets_a_frame_in_progress() {
    let bytes = frame(NAV_EOE, &eoe(NOON));
    let mut deframer = Deframer::<4>::new();
    assert!(deframer.feed(&bytes[..6]).1.is_none());
    deframer.reset();
    let (taken, event) = deframer.feed(&bytes);
    assert_eq!(taken, bytes.len());
    assert_eq!(event.unwrap().unwrap().id(), NAV_EOE);
}

const POSITION: Position = Position {
    latitude: MilliArcSeconds(134_372_100),
    longitude: MilliArcSeconds(-439_192_200),
    ellipsoid: Centimeters(2_300),
    mean_sea_level: Centimeters(5_500),
};

const VELOCITY: Velocity = Velocity {
    speed: CentimetersPerSecond(123),
    heading: TenthDegrees(900),
};

fn pvt(fields: Pvt) -> crate::nav::NavPvt {
    match decode(NAV_PVT, &fields.payload()) {
        Some(Message::NavPvt(pvt)) => pvt,
        other => panic!("{other:?}"),
    }
}

#[test]
fn nav_pvt_is_converted_to_the_records_units() {
    let decoded = pvt(PVT);
    assert_eq!(decoded.time_of_week, Milliseconds(NOON));
    assert_eq!(decoded.fix(), Fix::ThreeD);
    assert_eq!(decoded.position(), Some(POSITION));
    assert_eq!(decoded.velocity(), VELOCITY);
}

#[test]
fn nav_pvt_fix_needs_gnss_fix_ok() {
    for (fix_type, fix) in [
        (0, Fix::None),
        (1, Fix::None),
        (2, Fix::TwoD),
        (3, Fix::ThreeD),
        (4, Fix::ThreeD),
        (5, Fix::TimeOnly),
        (6, Fix::None),
    ] {
        assert_eq!(pvt(Pvt { fix_type, ..PVT }).fix(), fix, "{fix_type}");
        let not_ok = Pvt {
            fix_type,
            flags: 0xfe,
            ..PVT
        };
        assert_eq!(pvt(not_ok).fix(), Fix::None, "{fix_type}");
    }
}

#[test]
fn nav_pvt_edges() {
    assert_eq!(pvt(Pvt { flags3: 1, ..PVT }).position(), None);
    // Heading wraps into 0 to 3599 tenths; speed is never negative.
    for (heading, tenths) in [
        (0, 0),
        (-4_999, 0),
        (-5_001, 3_599),
        (35_999_500, 0),
        (35_994_999, 3_599),
    ] {
        let decoded = pvt(Pvt { heading, ..PVT });
        assert_eq!(
            decoded.velocity().heading,
            TenthDegrees(tenths),
            "{heading}"
        );
    }
    for (speed, centimeters) in [(-20, 0), (4, 0), (5, 1), (i32::MAX, u16::MAX)] {
        let decoded = pvt(Pvt { speed, ..PVT });
        assert_eq!(
            decoded.velocity().speed,
            CentimetersPerSecond(centimeters),
            "{speed}"
        );
    }
    // The poles and the date line, to the milliarcsecond.
    let corner = pvt(Pvt {
        latitude: -900_000_000,
        longitude: 1_800_000_000,
        height: -15,
        sea_level: -14,
        ..PVT
    });
    assert_eq!(
        corner.position(),
        Some(Position {
            latitude: MilliArcSeconds(-324_000_000),
            longitude: MilliArcSeconds(648_000_000),
            ellipsoid: Centimeters(-1),
            mean_sea_level: Centimeters(-1),
        })
    );
}

fn nav_time_gps(payload: [u8; 16]) -> crate::nav::NavTimeGps {
    match decode(NAV_TIMEGPS, &payload) {
        Some(Message::NavTimeGps(time)) => time,
        other => panic!("{other:?}"),
    }
}

#[test]
fn nav_timegps_gives_the_nearest_second() {
    let decoded = nav_time_gps(time_gps(NOON, -250_000, WEEK.cast_signed(), 17, 7));
    assert_eq!(decoded.time_of_week, Milliseconds(NOON));
    assert_eq!(decoded.fraction, Nanoseconds(-250_000));
    assert_eq!(decoded.week(), Some(GpsWeek::new(WEEK)));
    assert_eq!(decoded.leap_seconds(), Some(Seconds(17)));
    assert_eq!(decoded.accuracy, Nanoseconds(25));
    assert_eq!(decoded.seconds(None), Some(second(WEEK, NOON)));
    // A millisecond short of a second is that second; so is the last
    // of the week, which is the next week's first.
    let late = nav_time_gps(time_gps(NOON - 1, 400_000, WEEK.cast_signed(), 17, 7));
    assert_eq!(late.seconds(None), Some(second(WEEK, NOON)));
    let last = nav_time_gps(time_gps(604_799_999, 0, WEEK.cast_signed(), 17, 7));
    assert_eq!(last.seconds(None), Some(second(WEEK + 1, 0)));
    // Not a time of week at all.
    let past = nav_time_gps(time_gps(604_800_000, 0, WEEK.cast_signed(), 17, 7));
    assert_eq!(past.seconds(None), None);
}

#[test]
fn nav_timegps_validity() {
    for valid in [0b000, 0b001, 0b010, 0b100] {
        let decoded = nav_time_gps(time_gps(NOON, 0, WEEK.cast_signed(), 17, valid));
        assert_eq!(decoded.week(), None, "{valid}");
        assert_eq!(decoded.seconds(None), None, "{valid}");
        assert_eq!(
            decoded.leap_seconds(),
            (valid == 0b100).then_some(Seconds(17))
        );
    }
    let timed = nav_time_gps(time_gps(NOON, 0, WEEK.cast_signed(), 17, 0b011));
    assert_eq!(timed.week(), Some(GpsWeek::new(WEEK)));
    assert_eq!(timed.leap_seconds(), None);
    assert_eq!(nav_time_gps(time_gps(NOON, 0, -1, 17, 7)).week(), None);
}

/// A receiver that resolves the week 1024 off gives the right second
/// near a reference, either side of it.
#[test]
fn nav_timegps_week_near_a_reference() {
    let wrong = nav_time_gps(time_gps(NOON, 0, (WEEK - 1024).cast_signed(), 17, 7));
    assert_eq!(wrong.seconds(None), Some(second(WEEK - 1024, NOON)));
    for reference in [WEEK - 400, WEEK, WEEK + 400] {
        assert_eq!(
            wrong.seconds(Some(GpsWeek::new(reference))),
            Some(second(WEEK, NOON)),
            "{reference}"
        );
    }
    assert_eq!(
        wrong.seconds(Some(GpsWeek::new(WEEK + 600))),
        Some(second(WEEK + 1024, NOON))
    );
}

fn nav_time_ls(fields: Ls) -> Option<crate::nav::NavTimeLs> {
    match decode(NAV_TIMELS, &fields.payload()) {
        Some(Message::NavTimeLs(leap)) => Some(leap),
        None => None,
        other => panic!("{other:?}"),
    }
}

const NOTICE: LeapNotice = LeapNotice {
    change: LeapChange::Insert,
    until: Seconds(12 * 3600 + 17),
    week: WEEK,
    day: 7,
};

#[test]
fn nav_timels_count_and_notice() {
    let decoded = nav_time_ls(LS).unwrap();
    assert_eq!(decoded.time_of_week, Milliseconds(NOON));
    assert_eq!(decoded.count(), Some(BEFORE));
    assert_eq!(decoded.notice(), Some(NOTICE));
    assert!(NOTICE.pending());
    assert_eq!(nav_time_ls(Ls { version: 1, ..LS }), None);
}

#[test]
fn nav_timels_count_needs_a_source() {
    // The firmware's default and "unknown" are not believed.
    for (count_source, valid, count) in [
        (0, 0b11, None),
        (255, 0b11, None),
        (2, 0b10, None),
        (1, 0b11, Some(BEFORE)),
        (7, 0b01, Some(BEFORE)),
    ] {
        let fields = Ls {
            count_source,
            valid,
            ..LS
        };
        assert_eq!(
            nav_time_ls(fields).unwrap().count(),
            count,
            "{count_source} {valid}"
        );
    }
}

#[test]
fn nav_timels_notice_needs_a_gps_date() {
    let none = |fields: Ls| assert_eq!(nav_time_ls(fields).unwrap().notice(), None);
    none(Ls { valid: 0b01, ..LS });
    none(Ls { change: 0, ..LS });
    none(Ls { change: 2, ..LS });
    none(Ls { day: 0, ..LS });
    none(Ls { day: 8, ..LS });
    // No source, SBAS, BeiDou, whose days run 0 to 6, and GLONASS.
    for change_source in [0, 3, 4, 6, 7] {
        none(Ls {
            change_source,
            ..LS
        });
    }
    let galileo = Ls {
        change_source: 5,
        change: -1,
        until: -5,
        ..LS
    };
    let notice = nav_time_ls(galileo).unwrap().notice().unwrap();
    assert_eq!(notice.change, LeapChange::Remove);
    assert_eq!(notice.until, Seconds(-5));
    assert!(!notice.pending());
}

/// The leap second of 2016 ended a Saturday: day 7 of week 1929 ends
/// where week 1930 starts.
#[test]
fn a_notice_places_the_leap_second_at_its_days_end() {
    let now = second(WEEK, NOON);
    assert_eq!(NOTICE.event(now, BEFORE), Some(leap()));
    // The week in the almanac's eight bits, and off by a whole 256.
    for week in [WEEK % 256, WEEK - 256, WEEK + 256] {
        let notice = LeapNotice { week, ..NOTICE };
        assert_eq!(notice.event(now, BEFORE), Some(leap()), "{week}");
    }
    // Counted to the midnight after the inserted second.
    let notice = LeapNotice {
        until: Seconds(NOTICE.until.0 + 1),
        ..NOTICE
    };
    assert_eq!(notice.event(now, BEFORE), Some(leap()));
    // Months ahead, from a week long before.
    let early = second(WEEK - 20, 0);
    let until = leap().at.checked_since(early).unwrap();
    let notice = LeapNotice {
        until: Seconds(i32::try_from(until.0).unwrap()),
        ..NOTICE
    };
    assert_eq!(notice.event(early, BEFORE), Some(leap()));
}

/// A leap second in the middle of a week: 2015-06-30, a Tuesday, day 3
/// of week 1851, GPS less UTC 16 until it.
#[test]
fn a_notice_in_the_middle_of_a_week() {
    let now = second(1851, 0);
    let notice = LeapNotice {
        change: LeapChange::Insert,
        until: Seconds(3 * 86_400 + 16),
        week: 1851,
        day: 3,
    };
    let event = notice.event(now, Seconds(16)).unwrap();
    assert_eq!(event.at, second(1851, (3 * 86_400 + 16) * 1000));
    // A second removed comes a second sooner.
    let notice = LeapNotice {
        change: LeapChange::Remove,
        until: Seconds(3 * 86_400 + 15),
        ..notice
    };
    let event = notice.event(now, Seconds(16)).unwrap();
    assert_eq!(event.at, second(1851, (3 * 86_400 + 15) * 1000));
    assert_eq!(event.change, LeapChange::Remove);
}

#[test]
fn a_notice_whose_date_and_countdown_disagree_is_not_believed() {
    let now = second(WEEK, NOON);
    for notice in [
        LeapNotice { day: 6, ..NOTICE },
        LeapNotice {
            week: WEEK + 1,
            ..NOTICE
        },
        LeapNotice {
            until: Seconds(NOTICE.until.0 + 3),
            ..NOTICE
        },
        LeapNotice {
            until: Seconds(NOTICE.until.0 - 3),
            ..NOTICE
        },
    ] {
        assert_eq!(notice.event(now, BEFORE), None, "{notice:?}");
    }
}

fn nav_dop(payload: [u8; 18]) -> Dops {
    match decode(NAV_DOP, &payload) {
        Some(Message::NavDop(dop)) => {
            assert_eq!(dop.time_of_week, Milliseconds(NOON));
            dop.dops()
        }
        other => panic!("{other:?}"),
    }
}

const DOPS: Dops = Dops {
    geometric: Tenths(25),
    position: Tenths(20),
    horizontal: Tenths(9),
    vertical: Tenths(15),
    time: Tenths(16),
};

#[test]
fn nav_dop_is_rounded_to_tenths() {
    assert_eq!(nav_dop(dop(NOON, [251, 199, 155, 150, 94])), DOPS);
    let high = nav_dop(dop(NOON, [u16::MAX; 5]));
    assert_eq!(high.geometric, Tenths(6_554));
}

const ECEF: [i32; 3] = [-269_100_000, -430_700_000, 384_600_000];

#[test]
fn nav_posecef_is_centimeters_already() {
    match decode(NAV_POSECEF, &pos_ecef(NOON, ECEF)) {
        Some(Message::NavPosEcef(ecef)) => {
            assert_eq!(ecef.time_of_week, Milliseconds(NOON));
            assert_eq!(
                ecef.position,
                Ecef {
                    x: Centimeters(ECEF[0]),
                    y: Centimeters(ECEF[1]),
                    z: Centimeters(ECEF[2]),
                }
            );
        }
        other => panic!("{other:?}"),
    }
}

/// GPS 7, high in the south-east, in the fix.
const SAT: Sat = Sat {
    system: 0,
    id: 7,
    signal: 44,
    elevation: 62,
    azimuth: 135,
    flags: SAT_IN_FIX,
};

fn nav_sat(satellites: &[Sat]) -> smartclock_gps::record::Satellites {
    match decode(NAV_SAT, &sat(NOON, satellites)) {
        Some(Message::NavSat(sat)) => {
            assert_eq!(sat.header.time_of_week, Milliseconds(NOON));
            assert_eq!(sat.satellites().count(), satellites.len());
            sat.sky()
        }
        other => panic!("{other:?}"),
    }
}

#[test]
fn nav_sat_gives_a_satellite_as_a_record_holds_it() {
    let sky = nav_sat(&[SAT]);
    let satellite = sky.iter().next().unwrap();
    assert_eq!(satellite.prn.get(), 7);
    assert_eq!(satellite.elevation, Degrees(62));
    assert_eq!(satellite.azimuth, Degrees(135));
    assert_eq!(satellite.signal, Some(DecibelHertz(44)));
    assert_eq!(satellite.tracking, Tracking::Position);
    assert!(satellite.used);
    assert_eq!(satellite.health, Health::Healthy);
    assert!(satellite.ephemeris);
    assert_eq!(satellite.doppler, None);
    assert_eq!(satellite.time_error, None);
}

#[test]
fn nav_sat_quality_health_and_azimuth() {
    for (quality, tracking) in [
        (0, Tracking::Searching),
        (1, Tracking::Searching),
        (2, Tracking::CodeLock),
        (3, Tracking::CodeLock),
        (4, Tracking::Ephemeris),
        (7, Tracking::Ephemeris),
    ] {
        let sky = nav_sat(&[Sat {
            flags: quality,
            ..SAT
        }]);
        let satellite = sky.iter().next().unwrap();
        assert_eq!(satellite.tracking, tracking, "{quality}");
        assert_eq!(
            satellite.signal,
            (quality != 0).then_some(DecibelHertz(44)),
            "{quality}"
        );
        assert!(!satellite.used);
        assert_eq!(satellite.health, Health::Unknown);
    }
    let sky = nav_sat(&[Sat {
        flags: 2 << 4,
        azimuth: 360,
        ..SAT
    }]);
    let satellite = sky.iter().next().unwrap();
    assert_eq!(satellite.health, Health::Unhealthy);
    assert_eq!(satellite.azimuth, Degrees(0));
}

#[test]
fn nav_sat_leaves_out_what_a_record_cannot_hold() {
    let sky = nav_sat(&[
        // SBAS, Galileo and GLONASS.
        Sat { system: 1, ..SAT },
        Sat {
            system: 2,
            id: 3,
            ..SAT
        },
        Sat {
            system: 6,
            id: 255,
            ..SAT
        },
        // No such GPS satellites.
        Sat { id: 0, ..SAT },
        Sat { id: 33, ..SAT },
        // Below the horizon, and a place not known.
        Sat {
            id: 1,
            elevation: -1,
            ..SAT
        },
        Sat {
            id: 2,
            elevation: -91,
            ..SAT
        },
        Sat {
            id: 3,
            elevation: 91,
            ..SAT
        },
        Sat {
            id: 32,
            elevation: 0,
            ..SAT
        },
        Sat {
            id: 31,
            elevation: 90,
            ..SAT
        },
    ]);
    let kept: Vec<u8> = sky.iter().map(|satellite| satellite.prn.get()).collect();
    assert_eq!(kept, [32, 31]);
    assert!(nav_sat(&[]).is_empty());
    // A version this module does not know.
    assert_eq!(decode(NAV_SAT, &[0, 0, 0, 0, 2, 0, 0, 0]), None);
}

/// Fourteen in view and a record's twelve places: the eight in the
/// fix come first, though the message has them last, and the two
/// lowest are left out.
#[test]
fn nav_sat_puts_the_fix_first_and_drops_the_lowest() {
    let satellites: Vec<Sat> = (1..=14)
        .map(|id| Sat {
            id,
            elevation: i8::try_from(id).unwrap() * 5,
            flags: if id >= 7 { SAT_IN_FIX } else { 4 },
            ..SAT
        })
        .collect();
    let sky = nav_sat(&satellites);
    assert!(sky.truncated());
    let kept: Vec<u8> = sky.iter().map(|satellite| satellite.prn.get()).collect();
    assert_eq!(kept, [7, 8, 9, 10, 11, 12, 13, 14, 3, 4, 5, 6]);
    assert!(sky.iter().take(8).all(|satellite| satellite.used));
}

#[test]
fn mon_hw_gives_the_antenna() {
    for (status, antenna) in [
        (0, Antenna::Unknown),
        (1, Antenna::Unknown),
        (2, Antenna::Connected),
        (3, Antenna::Shorted),
        (4, Antenna::Open),
        (5, Antenna::Unknown),
    ] {
        match decode(MON_HW, &hw(status)) {
            Some(Message::MonHw(hardware)) => assert_eq!(hardware.antenna, antenna),
            other => panic!("{other:?}"),
        }
    }
}

#[test]
fn tim_tp_is_decoded() {
    let decode = |flags| match decode(TIM_TP, &tp(NOON + 1000, -4_321, WEEK, flags)) {
        Some(Message::TimTp(pulse)) => pulse,
        other => panic!("{other:?}"),
    };
    let pulse = decode(0);
    assert_eq!(pulse.time_of_week, Milliseconds(NOON + 1000));
    assert_eq!(pulse.week, WEEK);
    assert_eq!(pulse.quantization_error, Some(Picoseconds(-4_321)));
    assert!(!pulse.utc_base);
    assert!(pulse.locked);
    assert_eq!(pulse.raim, None);
    let pulse = decode(0b11_0001);
    assert_eq!(pulse.quantization_error, None);
    assert!(pulse.utc_base);
    assert!(!pulse.locked);
    assert_eq!(Message::TimTp(pulse).epoch(), None);
    assert_eq!(decode(RAIM_NOT_ACTIVE).raim, Some(false));
    assert_eq!(decode(RAIM_ACTIVE).raim, Some(true));
    assert_eq!(decode(0b1100).raim, None);
}

/// A record's Time RAIM result is the last `TIM-TP`'s, read as the
/// crate's documentation says, and none without one.
#[test]
fn time_raim_is_the_last_pulses() {
    for (flags, raim) in [
        (0, None),
        (
            RAIM_ACTIVE,
            Some(Raim::new(RaimSolution::Ok, RaimAbility::Detection)),
        ),
        (
            RAIM_ACTIVE | NOT_LOCKED,
            Some(Raim::new(RaimSolution::Unknown, RaimAbility::Detection)),
        ),
        (
            RAIM_NOT_ACTIVE,
            Some(Raim::new(RaimSolution::Unknown, RaimAbility::Neither)),
        ),
    ] {
        let bytes = stream(&[
            (TIM_TP, &tp(NOON, 0, WEEK, RAIM_ACTIVE)),
            (TIM_TP, &tp(NOON + 1000, 0, WEEK, flags)),
            (NAV_EOE, &eoe(NOON)),
        ]);
        let records = records(&mut Bench::new(), &bytes);
        let [record] = records[..] else {
            panic!("{records:?}");
        };
        assert_eq!(record.derived.raim, raim);
    }
    let records = records(&mut Bench::new(), &stream(&[(NAV_EOE, &eoe(NOON))]));
    assert_eq!(records[0].derived.raim, None);
}

#[test]
fn a_message_of_another_kind_or_length_is_not_decoded() {
    assert_eq!(decode(MessageId::new(0x01, 0x03), &[0; 16]), None);
}

#[test]
fn a_payload_off_its_length_is_not_decoded() {
    // A fixed message's length, a streamed one's header's.
    let lengths = MESSAGES.map(|(id, length)| match length {
        PayloadLength::Fixed(own) => (id, own),
        PayloadLength::Blocks { header, .. } => (id, header),
    });
    let bytes = vec![0; lengths.iter().map(|&(_, own)| own).max().unwrap() + 1];
    for (id, own) in lengths {
        assert_eq!(Message::parse_payload(id, &bytes[..own - 1]), None, "{id}");
        assert_eq!(Message::parse_payload(id, &bytes[..=own]), None, "{id}");
    }
    let pvt = PVT.payload();
    assert!(Message::parse_payload(NAV_PVT, &pvt).is_some());
    assert_eq!(Message::parse_payload(NAV_PVT, &pvt[1..]), None);
}

/// One whole epoch, every message of it, as a receiver sends them.
fn whole_epoch() -> Vec<u8> {
    stream(&[
        (MON_HW, &hw(2)),
        (NAV_POSECEF, &pos_ecef(NOON, ECEF)),
        (NAV_DOP, &dop(NOON, [251, 199, 155, 150, 94])),
        (NAV_PVT, &PVT.payload()),
        (NAV_TIMEGPS, &time_gps(NOON, 0, WEEK.cast_signed(), 17, 7)),
        (NAV_TIMELS, &LS.payload()),
        (
            NAV_SAT,
            &sat(
                NOON,
                &[
                    SAT,
                    Sat {
                        id: 9,
                        flags: 2,
                        ..SAT
                    },
                ],
            ),
        ),
        (TIM_TP, &tp(NOON + 1000, -4_321, WEEK, RAIM_ACTIVE)),
        (NAV_EOE, &eoe(NOON)),
    ])
}

/// The exceptions tables, the fields' and the settings', from the
/// crate's directory.
const EXCEPTIONS: &str = "../../docs/firmware/gps-field-exceptions.tsv";
const SETTING_EXCEPTIONS: &str = "../../docs/firmware/gps-setting-exceptions.tsv";

/// The assembler writes what [`DECLARATION`] declares and nothing else, and
/// all the images use.  It acts on none of the engine's settings,
/// nothing here configuring the receiver from them, and is excused
/// each the images set, once.
#[test]
fn the_provider_fills_what_it_declares() {
    let table = |path: &str| {
        std::fs::read_to_string(std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join(path))
            .expect("an exceptions table")
    };
    let shortfall = DECLARATION.shortfall(&table(EXCEPTIONS));
    assert!(shortfall.is_empty(), "shortfall: {shortfall:?}");
    let exceptions = table(SETTING_EXCEPTIONS);
    let unacted = DECLARATION.unacted(&exceptions);
    assert!(unacted.is_empty(), "unacted: {unacted:?}");
    let needless = DECLARATION.needless(&exceptions);
    assert!(needless.is_empty(), "needless: {needless:?}");

    let records = records(&mut Bench::new(), &whole_epoch());
    let [record] = records[..] else {
        panic!("{records:?}");
    };
    let undeclared = DECLARATION.undeclared(&UNFILLED, &record);
    assert!(undeclared.is_empty(), "undeclared: {undeclared:?}");
    let unshown = DECLARATION.unshown(changed(&UNFILLED, &record));
    assert!(unshown.is_empty(), "unshown: {unshown:?}");
}

/// The record of a whole epoch has all of it.
#[test]
fn an_epoch_makes_a_record() {
    let bytes = whole_epoch();
    let mut assembler = Bench::new();
    let records = records(&mut assembler, &bytes);
    let [record] = records[..] else {
        panic!("{records:?}");
    };
    assert_eq!(record.time, time(WEEK, NOON));
    assert_eq!(record.utc_offset, Some(BEFORE));
    assert_eq!(record.leap, Some(leap()));
    assert_eq!(record.fix, Fix::ThreeD);
    assert_eq!(record.position, Some(POSITION));
    assert_eq!(record.velocity, Some(VELOCITY));
    assert_eq!(record.dops, Some(DOPS));
    assert_eq!(record.timing, None);
    assert_eq!(record.satellites.len(), 2);
    assert_eq!(record.almanac, None);
    assert_eq!(record.antenna, Antenna::Connected);
    assert_eq!(record.self_test, SelfTest::PASS);
    assert_eq!(
        record.derived.raim,
        Some(Raim::new(RaimSolution::Ok, RaimAbility::Detection))
    );
    assert_eq!(
        record.derived.ecef,
        Some(Ecef {
            x: Centimeters(ECEF[0]),
            y: Centimeters(ECEF[1]),
            z: Centimeters(ECEF[2]),
        })
    );
    assert_eq!(assembler.errors(), 0);
}

/// However the stream is cut, the record comes with the epoch's last
/// byte and not before.
#[test]
fn the_record_comes_with_the_last_byte() {
    let bytes = epoch(NOON);
    let mut assembler = Bench::new();
    let mut record = Record::EMPTY;
    let (&last, rest) = bytes.split_last().unwrap();
    for &byte in rest {
        assert!(!assembler.push(byte, &mut record));
        assert_eq!(record, Record::EMPTY);
    }
    assert!(assembler.push(last, &mut record));
    assert_eq!(record.time, time(WEEK, NOON));
}

/// Without a fix the receiver's time is its own clock's: none is given,
/// and nothing of the solution.
#[test]
fn no_time_without_a_fix() {
    let no_fix = Pvt { flags: 0, ..PVT };
    let bytes = stream(&[
        (NAV_PVT, &no_fix.payload()),
        (NAV_TIMEGPS, &time_gps(NOON, 0, WEEK.cast_signed(), 17, 7)),
        (NAV_DOP, &dop(NOON, [9_999; 5])),
        (NAV_POSECEF, &pos_ecef(NOON, ECEF)),
        (NAV_TIMELS, &LS.payload()),
        (NAV_SAT, &sat(NOON, &[SAT])),
        (NAV_EOE, &eoe(NOON)),
    ]);
    let records = records(&mut Bench::new(), &bytes);
    let [record] = records[..] else {
        panic!("{records:?}");
    };
    assert_eq!(record.time, None);
    assert_eq!(record.fix, Fix::None);
    assert_eq!(record.position, None);
    assert_eq!(record.velocity, None);
    assert_eq!(record.dops, None);
    assert_eq!(record.derived.ecef, None);
    // What does not hang on the fix is there.
    assert_eq!(record.utc_offset, Some(BEFORE));
    assert_eq!(record.leap, None);
    assert_eq!(record.satellites.len(), 1);
}

/// In time mode a NEO-M8T gives `pDOP`, `hDOP` and `vDOP` as 9999 and
/// only `gDOP` and `tDOP`: the time-only fix has the time and the
/// position, and no DOPs.  A 3D fix's are kept.
#[test]
fn a_time_only_fix_has_no_dops() {
    let time_mode = Pvt { fix_type: 5, ..PVT };
    let placeholders = dop(NOON, [32, 9_999, 32, 9_999, 9_999]);
    let bytes = stream(&[
        (NAV_PVT, &time_mode.payload()),
        (NAV_TIMEGPS, &time_gps(NOON, 0, WEEK.cast_signed(), 17, 7)),
        (NAV_DOP, &placeholders),
        (NAV_EOE, &eoe(NOON)),
    ]);
    let made = records(&mut Bench::new(), &bytes);
    let [record] = made[..] else {
        panic!("{made:?}");
    };
    assert_eq!(record.fix, Fix::TimeOnly);
    assert_eq!(record.time, time(WEEK, NOON));
    assert_eq!(record.position, Some(POSITION));
    assert_eq!(record.dops, None);

    let bytes = stream(&[
        (NAV_PVT, &PVT.payload()),
        (NAV_DOP, &placeholders),
        (NAV_EOE, &eoe(NOON)),
    ]);
    let made = records(&mut Bench::new(), &bytes);
    let [record] = made[..] else {
        panic!("{made:?}");
    };
    assert_eq!(record.fix, Fix::ThreeD);
    assert_eq!(record.dops.map(|dops| dops.position), Some(Tenths(1_000)));
}

#[test]
fn no_time_without_a_valid_week_or_its_message() {
    let invalid = stream(&[
        (NAV_PVT, &PVT.payload()),
        (
            NAV_TIMEGPS,
            &time_gps(NOON, 0, WEEK.cast_signed(), 17, 0b101),
        ),
        (NAV_EOE, &eoe(NOON)),
    ]);
    let absent = stream(&[(NAV_PVT, &PVT.payload()), (NAV_EOE, &eoe(NOON))]);
    for (bytes, offset) in [(invalid, Some(BEFORE)), (absent, None)] {
        let records = records(&mut Bench::new(), &bytes);
        assert_eq!(records.len(), 1);
        assert_eq!(records[0].time, None);
        assert_eq!(records[0].fix, Fix::ThreeD);
        assert_eq!(records[0].position, Some(POSITION));
        assert_eq!(records[0].utc_offset, offset);
    }
}

/// A receiver that sends no `NAV-EOE`, or a stream that lost one: the
/// next epoch's first message ends the one before, and nothing of one
/// `iTOW` is put with another's.
#[test]
fn another_itow_ends_the_epoch() {
    let next = NOON + 1000;
    let next_pvt = Pvt {
        time_of_week: next,
        flags3: 1,
        ..PVT
    };
    let first = stream(&[
        (NAV_DOP, &dop(NOON, [251, 199, 155, 150, 94])),
        (NAV_PVT, &PVT.payload()),
        (NAV_TIMEGPS, &time_gps(NOON, 0, WEEK.cast_signed(), 17, 7)),
        (NAV_SAT, &sat(NOON, &[SAT])),
    ]);
    let second = stream(&[
        (NAV_PVT, &next_pvt.payload()),
        (NAV_TIMEGPS, &time_gps(next, 0, WEEK.cast_signed(), 17, 7)),
    ]);
    let third = stream(&[(NAV_POSECEF, &pos_ecef(next + 1000, ECEF))]);
    let mut assembler = Bench::new();
    assert_eq!(records(&mut assembler, &first), []);
    // The first message of the second epoch gives the first's record.
    let (head, tail) = second.split_at(OVERHEAD + 92);
    let records_first = records(&mut assembler, head);
    let [record] = records_first[..] else {
        panic!("{records_first:?}");
    };
    assert_eq!(record.time, time(WEEK, NOON));
    assert_eq!(record.position, Some(POSITION));
    assert_eq!(record.dops, Some(DOPS));
    assert_eq!(record.satellites.len(), 1);
    assert_eq!(records(&mut assembler, tail), []);
    let records_second = records(&mut assembler, &third);
    let [record] = records_second[..] else {
        panic!("{records_second:?}");
    };
    assert_eq!(record.time, time(WEEK, next));
    assert_eq!(record.position, None);
    assert_eq!(record.dops, None);
    assert_eq!(record.satellites.len(), 0);
}

/// A `NAV-EOE` of another `iTOW` than the epoch in progress: its own
/// epoch's messages were lost, and the one in progress is given.
#[test]
fn an_end_of_another_epoch_gives_the_one_in_progress() {
    let mut bytes = stream(&[
        (NAV_PVT, &PVT.payload()),
        (NAV_TIMEGPS, &time_gps(NOON, 0, WEEK.cast_signed(), 17, 7)),
        (NAV_EOE, &eoe(NOON + 1000)),
    ]);
    bytes.extend(epoch(NOON + 2000));
    let records = records(&mut Bench::new(), &bytes);
    assert_eq!(records.len(), 2);
    assert_eq!(records[0].time, time(WEEK, NOON));
    assert_eq!(records[1].time, time(WEEK, NOON + 2000));
}

/// A time message of one `iTOW` and a fix of another never make a time.
#[test]
fn time_and_fix_must_share_an_itow() {
    let bytes = stream(&[
        (NAV_TIMEGPS, &time_gps(NOON, 0, WEEK.cast_signed(), 17, 7)),
        (
            NAV_PVT,
            &Pvt {
                time_of_week: NOON + 1000,
                ..PVT
            }
            .payload(),
        ),
        (NAV_EOE, &eoe(NOON + 1000)),
    ]);
    let records = records(&mut Bench::new(), &bytes);
    assert_eq!(records.len(), 2);
    assert_eq!(records[0].time, None);
    assert_eq!(records[0].fix, Fix::None);
    assert_eq!(records[0].utc_offset, Some(BEFORE));
    assert_eq!(records[1].time, None);
    assert_eq!(records[1].fix, Fix::ThreeD);
    assert_eq!(records[1].utc_offset, None);
}

/// A damaged frame is counted and its epoch goes on without it.
#[test]
fn a_damaged_frame_is_left_out_of_its_epoch() {
    let mut damaged = frame(NAV_PVT, &PVT.payload());
    damaged[40] ^= 0x80;
    let mut bytes = damaged;
    bytes.extend(stream(&[
        (NAV_TIMEGPS, &time_gps(NOON, 0, WEEK.cast_signed(), 17, 7)),
        (NAV_EOE, &eoe(NOON)),
    ]));
    bytes.extend(epoch(NOON + 1000));
    let mut assembler = Bench::new();
    let records = records(&mut assembler, &bytes);
    assert_eq!(assembler.errors(), 1);
    assert_eq!(records.len(), 2);
    assert_eq!(records[0].time, None);
    assert_eq!(records[0].position, None);
    assert_eq!(records[1].time, time(WEEK, NOON + 1000));
    assert_eq!(records[1].position, Some(POSITION));
}

/// A receiver older than `NAV-TIMELS`: the count is `NAV-TIMEGPS`'s,
/// and no leap second is known.
#[test]
fn without_nav_timels_the_count_is_nav_timegps_own() {
    let records = records(&mut Bench::new(), &epoch(NOON));
    assert_eq!(records[0].utc_offset, Some(BEFORE));
    assert_eq!(records[0].leap, None);
    // And `NAV-TIMELS` with only the firmware's default defers to it.
    let default = Ls {
        count_source: 0,
        count: 16,
        valid: 0b01,
        ..LS
    };
    let bytes = stream(&[
        (NAV_PVT, &PVT.payload()),
        (NAV_TIMEGPS, &time_gps(NOON, 0, WEEK.cast_signed(), 17, 7)),
        (NAV_TIMELS, &default.payload()),
        (NAV_EOE, &eoe(NOON)),
    ]);
    let records = self::records(&mut Bench::new(), &bytes);
    assert_eq!(records[0].utc_offset, Some(BEFORE));
    assert_eq!(records[0].leap, None);
}

/// An epoch of the week after [`WEEK`], `seconds` before the leap
/// second, with `ls` for its `NAV-TIMELS` and `count` in its
/// `NAV-TIMEGPS`.
fn leap_epoch(seconds: i32, ls: Option<Ls>, count: i8) -> Vec<u8> {
    let time_of_week = time_before_leap(seconds);
    let pvt = Pvt {
        time_of_week,
        ..PVT
    };
    let week = (WEEK + 1).cast_signed();
    let mut bytes = stream(&[
        (NAV_PVT, &pvt.payload()),
        (NAV_TIMEGPS, &time_gps(time_of_week, 0, week, count, 7)),
    ]);
    if let Some(ls) = ls {
        bytes.extend(frame(NAV_TIMELS, &ls.payload()));
    }
    bytes.extend(frame(NAV_EOE, &eoe(time_of_week)));
    bytes
}

/// What a run of epochs gives for GPS less UTC and the leap second.
fn offsets(epochs: &[Vec<u8>]) -> Vec<(Option<Seconds<i8>>, Option<LeapEvent>)> {
    let mut assembler = Bench::new();
    epochs
        .iter()
        .flat_map(|bytes| records(&mut assembler, bytes))
        .map(|record| (record.utc_offset, record.leap))
        .collect()
}

const AFTER: Seconds<i8> = Seconds(18);

/// Across the leap second at a week's start, the offset steps in the
/// leap second itself, and the event stays once past, whichever second
/// the receiver steps its own count in.
#[test]
fn the_offset_steps_at_the_leap_second() {
    let past = |seconds: i32, count| Ls {
        count,
        ..LS.before_leap(seconds)
    };
    let pending = (Some(BEFORE), Some(leap()));
    let stepped = (Some(AFTER), Some(leap()));
    let expected = [pending, pending, stepped, stepped, stepped];
    // The receiver steps in the leap second.
    let prompt = [
        leap_epoch(2, Some(LS.before_leap(2)), 17),
        leap_epoch(1, Some(LS.before_leap(1)), 17),
        leap_epoch(0, Some(past(0, 18)), 18),
        leap_epoch(-1, Some(past(-1, 18)), 18),
        leap_epoch(-2, None, 18),
    ];
    assert_eq!(offsets(&prompt), expected);
    // It steps a second late.
    let late = [
        leap_epoch(2, Some(LS.before_leap(2)), 17),
        leap_epoch(1, Some(LS.before_leap(1)), 17),
        leap_epoch(0, Some(past(0, 17)), 17),
        leap_epoch(-1, Some(past(-1, 18)), 18),
        leap_epoch(-2, Some(past(-2, 18)), 18),
    ];
    assert_eq!(offsets(&late), expected);
    // It steps a second early.
    let early = [
        leap_epoch(2, Some(LS.before_leap(2)), 17),
        leap_epoch(1, Some(past(1, 18)), 18),
        leap_epoch(0, Some(past(0, 18)), 18),
        leap_epoch(-1, Some(past(-1, 18)), 18),
        leap_epoch(-2, Some(past(-2, 18)), 18),
    ];
    assert_eq!(offsets(&early), expected);
}

/// A stream that starts after the leap second has no event to give:
/// the count is the receiver's.
#[test]
fn a_leap_second_already_past_is_not_reported() {
    let past = Ls {
        count: 18,
        ..LS.before_leap(-1)
    };
    assert_eq!(
        offsets(&[leap_epoch(-1, Some(past), 18)]),
        [(Some(AFTER), None)]
    );
}

/// The receiver stops announcing a leap second still to come.
#[test]
fn a_leap_second_withdrawn_is_dropped() {
    let withdrawn = Ls {
        valid: 0b01,
        ..LS.before_leap(4)
    };
    let epochs = [
        leap_epoch(5, Some(LS.before_leap(5)), 17),
        leap_epoch(4, Some(withdrawn), 17),
        leap_epoch(3, None, 17),
    ];
    assert_eq!(
        offsets(&epochs),
        [
            (Some(BEFORE), Some(leap())),
            (Some(BEFORE), None),
            (Some(BEFORE), None)
        ]
    );
    // An epoch without the message withdraws nothing.
    let epochs = [
        leap_epoch(5, Some(LS.before_leap(5)), 17),
        leap_epoch(4, None, 17),
    ];
    assert_eq!(offsets(&epochs)[1], (Some(BEFORE), Some(leap())));
}

/// A notice whose date is not where its countdown ends announces
/// nothing.
#[test]
fn a_notice_not_believed_announces_nothing() {
    let wrong = Ls {
        day: 6,
        ..LS.before_leap(5)
    };
    assert_eq!(
        offsets(&[leap_epoch(5, Some(wrong), 17)]),
        [(Some(BEFORE), None)]
    );
}

/// The antenna's state is the last `MON-HW`'s, across epochs, until a
/// reset.
#[test]
fn the_antenna_is_kept_across_epochs() {
    let mut assembler = Bench::new();
    let mut bytes = frame(MON_HW, &hw(4));
    bytes.extend(epoch(NOON));
    bytes.extend(epoch(NOON + 1000));
    let records = records(&mut assembler, &bytes);
    assert_eq!(records[0].antenna, Antenna::Open);
    assert_eq!(records[1].antenna, Antenna::Open);
    assembler.reset();
    let records = self::records(&mut assembler, &epoch(NOON + 2000));
    assert_eq!(records[0].antenna, Antenna::Unknown);
}

/// Reset forgets an epoch in progress and the leap second, and keeps
/// the reference week.
#[test]
fn reset_starts_the_stream_again() {
    let mut assembler = Bench::near(GpsWeek::new(WEEK + 1024));
    let mut bytes = leap_epoch(5, Some(LS.before_leap(5)), 17);
    bytes.extend(&frame(NAV_PVT, &PVT.payload())[..50]);
    assert_eq!(records(&mut assembler, &bytes).len(), 1);
    assembler.reset();
    let records = records(&mut assembler, &epoch(NOON));
    let [record] = records[..] else {
        panic!("{records:?}");
    };
    assert_eq!(record.time, time(WEEK + 1024, NOON));
    assert_eq!(record.leap, None);
    assert_eq!(assembler.errors(), 0);
}

/// A recording read through [`Replay`]: a record a tick, and back to
/// the start.
#[test]
fn a_recording_replays_a_record_a_tick() {
    let mut bytes = Vec::new();
    for tick in 0..3 {
        bytes.extend(epoch(NOON + tick * 1000));
    }
    let mut replay = Replay::new(&bytes[..], Bench::new());
    let mut record = Record::EMPTY;
    for tick in [0, 1, 2, 1] {
        assert_eq!(replay.record(Tick(tick), &mut record), Presence::Fresh);
        let milliseconds = u32::try_from(tick).unwrap() * 1000;
        assert_eq!(record.time, time(WEEK, NOON + milliseconds));
    }
    assert_eq!(replay.record(Tick(3), &mut record), Presence::Silent);
}

mod decoders;
mod streaming;
