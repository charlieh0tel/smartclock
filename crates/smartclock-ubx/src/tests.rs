//! Frames built by hand from the layouts of UBX-13003221 R28, through
//! the deframer and the decoders.

use std::boxed::Box;
use std::vec;
use std::vec::Vec;

use crate::frame::Deframer;
use crate::frame::EncodeError;
use crate::frame::FrameError;
use crate::frame::MessageId;
use crate::frame::NoRoom;
use crate::frame::OVERHEAD;
use crate::frame::checksum;
use crate::frame::encode;
use crate::message::ACK_ACK;
use crate::message::ACK_NAK;
use crate::message::CFG_TMODE2;
use crate::message::CFG_TP5;
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
use crate::nav::LeapChange;
use crate::nav::LeapNotice;
use crate::nav::nav_sat_length;
use crate::rxm::rxm_rawx_length;
use crate::units::Milliseconds;
use crate::units::Picoseconds;
use crate::units::Seconds;

/// The week of 2016-12-31, whose last second was a leap second, and
/// noon of that day, a Saturday, in its milliseconds.
const WEEK: u16 = 1929;
const NOON: u32 = (6 * 24 + 12) * 3600 * 1000;
/// GPS less UTC until that leap second.
const BEFORE: Seconds<i8> = Seconds(17);

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

/// GPS 7, high in the south-east, in the fix.
const SAT: Sat = Sat {
    system: 0,
    id: 7,
    signal: 44,
    elevation: 62,
    azimuth: 135,
    flags: SAT_IN_FIX,
};

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

/// `TIM-TP` (p. 432).
/// `TIM-TP`'s `flags` with `raim` not active and active (p. 433).
const RAIM_NOT_ACTIVE: u8 = 1 << 2;
const RAIM_ACTIVE: u8 = 2 << 2;

fn tp(time_of_week: u32, error: i32, week: u16, flags: u8) -> [u8; 16] {
    payload(&[
        (0, &time_of_week.to_le_bytes()),
        (8, &error.to_le_bytes()),
        (12, &week.to_le_bytes()),
        (14, &[flags]),
    ])
}

/// An `RXM-SFRBX` (section 32.18.7) of `words` from satellite 12 of
/// `system`.
fn sfrbx(system: u8, words: &[u32]) -> Vec<u8> {
    let count = u8::try_from(words.len()).unwrap();
    let mut bytes = vec![system, 12, 0, 0, count, 0, 2, 0];
    bytes.extend(words.iter().flat_map(|word| word.to_le_bytes()));
    bytes
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
        (CFG_TMODE2, 28),
        (CFG_TP5, 32),
    ] {
        let own = payload_length(id).unwrap();
        assert!(own.allows(length), "{id}");
        assert!(!own.allows(length + 1), "{id}");
        assert!(!own.allows(length - 1), "{id}");
    }
    assert_eq!(MESSAGES.len(), 17);
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
        PayloadLength::Polled { answer, .. } => (id, answer),
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

mod decoders;
mod encoders;
mod streaming;
