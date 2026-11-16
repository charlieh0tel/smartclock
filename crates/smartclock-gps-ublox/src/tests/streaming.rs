//! A streamer against the whole-frame path: the same messages, cut
//! into reads at every place and of every small size, give the same
//! headers and blocks a whole frame does, from a buffer far shorter
//! than the messages.

use std::vec::Vec;

use super::PVT;
use super::Pvt;
use super::eoe;
use super::frame;
use super::payload;
use super::stream;
use crate::frame::Deframer;
use crate::frame::Event;
use crate::frame::FrameError;
use crate::frame::Streamer;
use crate::message::LONGEST_PART;
use crate::message::Message;
use crate::message::NAV_EOE;
use crate::message::NAV_PVT;
use crate::message::NAV_SAT;
use crate::message::Part;
use crate::message::RXM_RAWX;
use crate::message::STREAMED;
use crate::nav::nav_sat_length;
use crate::rxm::rxm_rawx_length;

/// The buffer a board gives its streamer: room for `NAV-PVT`, the
/// longest message not streamed that it reads.
const BOARD: usize = 92;

/// What a streamer gave, owned.
#[derive(Debug, Clone, PartialEq, Eq)]
enum Given {
    Frame(crate::frame::MessageId, Vec<u8>),
    Header(crate::frame::MessageId, usize, Vec<u8>),
    Block(crate::frame::MessageId, usize, Vec<u8>),
    End(crate::frame::MessageId),
    Error(FrameError),
}

fn given(event: Result<Event<'_>, FrameError>) -> Given {
    match event {
        Ok(Event::Frame(frame)) => Given::Frame(frame.id(), frame.payload().to_vec()),
        Ok(Event::Header(header)) => Given::Header(header.id, header.blocks, header.bytes.to_vec()),
        Ok(Event::Block(block)) => Given::Block(block.id, block.index, block.bytes.to_vec()),
        Ok(Event::End(id)) => Given::End(id),
        Err(error) => Given::Error(error),
    }
}

/// Everything `bytes` give, read in pieces of the sizes `sizes` cycles
/// through.
fn streamed(bytes: &[u8], sizes: &[usize]) -> Vec<Given> {
    let mut streamer = Streamer::<BOARD>::new();
    let mut out = Vec::new();
    let mut rest = bytes;
    for &size in sizes.iter().cycle() {
        if rest.is_empty() {
            break;
        }
        let (mut piece, tail) = rest.split_at(size.min(rest.len()));
        rest = tail;
        while !piece.is_empty() {
            let (taken, event) = streamer.feed(piece);
            piece = &piece[taken..];
            out.extend(event.map(given));
        }
    }
    out
}

/// A `NAV-SAT` of `count` satellites, each different.
fn many_satellites(count: u8) -> Vec<u8> {
    let mut out = payload::<8>(&[(0, &77_u32.to_le_bytes()), (4, &[1, count])]).to_vec();
    for id in 0..count {
        out.extend(payload::<12>(&[
            (0, &[id % 7, id, 30 + id % 20, id % 90]),
            (4, &u16::from(id).to_le_bytes()),
            (8, &(u32::from(id) << 3).to_le_bytes()),
        ]));
    }
    out
}

/// An `RXM-RAWX` of `count` measurements, each different.
fn many_measurements(count: u8) -> Vec<u8> {
    let mut out = payload::<16>(&[(11, &[count])]).to_vec();
    for id in 0..count {
        let mut block = [id; 32];
        block[20] = 0;
        out.extend(block);
    }
    out
}

/// What the streamer should give for a streamed message's payload.
fn parts(id: crate::frame::MessageId, payload: &[u8], header: usize, block: usize) -> Vec<Given> {
    let blocks = (payload.len() - header) / block;
    let mut out = std::vec![Given::Header(id, blocks, payload[..header].to_vec())];
    out.extend(
        payload[header..]
            .chunks(block)
            .enumerate()
            .map(|(index, bytes)| Given::Block(id, index, bytes.to_vec())),
    );
    out.push(Given::End(id));
    out
}

#[test]
fn the_longest_part_is_a_measurement() {
    assert_eq!(STREAMED, [NAV_SAT, RXM_RAWX]);
    assert_eq!(LONGEST_PART, 32);
}

/// `NAV-SAT` of 40 satellites and `RXM-RAWX` of 24 measurements, each
/// far past the buffer, between messages given whole, cut at every
/// place and read in pieces of every size up to 7.
#[test]
fn many_blocks_are_given_however_the_stream_is_cut() {
    let sat = many_satellites(40);
    let rawx = many_measurements(24);
    assert!(sat.len() > BOARD && rawx.len() > BOARD);
    let pvt = PVT.payload();
    let bytes = stream(&[
        (NAV_PVT, &pvt),
        (NAV_SAT, &sat),
        (RXM_RAWX, &rawx),
        (NAV_EOE, &eoe(77)),
    ]);
    let mut expected = std::vec![Given::Frame(NAV_PVT, pvt.to_vec())];
    expected.extend(parts(NAV_SAT, &sat, 8, 12));
    expected.extend(parts(RXM_RAWX, &rawx, 16, 32));
    expected.push(Given::Frame(NAV_EOE, eoe(77).to_vec()));
    for size in 1..=7 {
        assert_eq!(streamed(&bytes, &[size]), expected, "pieces of {size}");
    }
    for cut in 0..=bytes.len() {
        assert_eq!(
            streamed(&bytes, &[cut.max(1), bytes.len()]),
            expected,
            "cut at {cut}"
        );
    }
    assert_eq!(streamed(&bytes, &[3, 1, 4, 1, 5, 9, 2, 6]), expected);
}

/// The parts decode to what the whole message does.
#[test]
fn the_parts_decode_as_the_whole_message_does() {
    let sat = many_satellites(40);
    let rawx = many_measurements(24);
    let bytes = stream(&[(NAV_SAT, &sat), (RXM_RAWX, &rawx)]);
    let mut streamer = Streamer::<BOARD>::new();
    let mut parts = Vec::new();
    for &byte in &bytes {
        match streamer.push(byte) {
            Some(Ok(Event::Header(header))) => parts.push(Part::header(&header).unwrap()),
            Some(Ok(Event::Block(block))) => parts.push(Part::block(&block).unwrap()),
            Some(Ok(Event::End(_))) | None => {}
            other => panic!("{other:?}"),
        }
    }
    let mut whole = Vec::new();
    let mut deframer = Deframer::<{ rxm_rawx_length(24) }>::new();
    for &byte in &bytes {
        match deframer
            .push(byte)
            .map(|frame| Message::parse(frame.unwrap()))
        {
            Some(Some(Message::NavSat(sat))) => {
                whole.push(Part::NavSatHeader(sat.header));
                whole.extend(sat.satellites().map(Part::Satellite));
            }
            Some(Some(Message::RxmRawx(rawx))) => {
                whole.push(Part::RxmRawxHeader(rawx.header));
                whole.extend(rawx.measurements().map(Part::Measurement));
            }
            None => {}
            other => panic!("{other:?}"),
        }
    }
    assert_eq!(whole.len(), 1 + 40 + 1 + 24);
    assert_eq!(parts, whole);
}

/// A streamed message with a bad checksum gives its parts and then the
/// error, not its end; the frame after it is found.
#[test]
fn a_bad_checksum_ends_a_streamed_message_with_an_error() {
    let sat = many_satellites(10);
    let mut bytes = frame(NAV_SAT, &sat);
    let last = bytes.len() - 1;
    bytes[last] ^= 0xff;
    bytes.extend(frame(NAV_EOE, &eoe(1)));
    let given = streamed(&bytes, &[bytes.len()]);
    assert_eq!(given.len(), 1 + 10 + 2);
    assert!(matches!(given[11], Given::Error(FrameError::BadChecksum { id, .. }) if id == NAV_SAT));
    assert_eq!(given[12], Given::Frame(NAV_EOE, eoe(1).to_vec()));
}

/// A streamed message's header whose count disagrees with its length
/// does not decode, as the whole message would not; its length is still
/// checked at the frame's header, and a message not streamed must still
/// fit the buffer.
#[test]
fn a_streamer_keeps_the_deframers_checks() {
    let mut sat = many_satellites(3);
    sat[5] = 4;
    let bytes = frame(NAV_SAT, &sat);
    let mut streamer = Streamer::<BOARD>::new();
    let header = bytes
        .iter()
        .find_map(|&byte| match streamer.push(byte) {
            Some(Ok(Event::Header(header))) => Some(Part::header(&header)),
            _ => None,
        })
        .unwrap();
    assert_eq!(header, None);
    // A length a whole number of blocks past the header, or not.
    let short = std::vec![0xb5, 0x62, 0x01, 0x35, 0x09, 0x00];
    assert_eq!(
        streamed(&short, &[1]),
        [Given::Error(FrameError::BadLength {
            id: NAV_SAT,
            length: 9
        })]
    );
    let long = frame(crate::frame::MessageId::new(0x0a, 0x04), &[0; BOARD + 1]);
    assert_eq!(
        streamed(&long, &[1])[0],
        Given::Error(FrameError::Overflow {
            id: crate::frame::MessageId::new(0x0a, 0x04),
            length: BOARD + 1,
            capacity: BOARD
        })
    );
    // A whole-frame deframer still refuses a long NAV-SAT.
    let mut deframer = Deframer::<BOARD>::new();
    let sat = frame(NAV_SAT, &many_satellites(40));
    let errors: Vec<_> = sat
        .iter()
        .filter_map(|&byte| {
            deframer
                .push(byte)
                .map(|event| event.map(|frame| frame.id()))
        })
        .collect();
    assert!(matches!(errors[0], Err(FrameError::Overflow { .. })));
    assert!(nav_sat_length(40) > BOARD);
}

/// Reset forgets a streamed message half taken.
#[test]
fn reset_forgets_a_streamed_message() {
    let bytes = frame(NAV_SAT, &many_satellites(5));
    let mut streamer = Streamer::<BOARD>::new();
    for &byte in &bytes[..30] {
        streamer.push(byte);
    }
    streamer.reset();
    let pvt = frame(NAV_PVT, &Pvt { ..PVT }.payload());
    let found: Vec<_> = pvt
        .iter()
        .filter_map(|&byte| streamer.push(byte).map(given))
        .collect();
    assert_eq!(found, [Given::Frame(NAV_PVT, PVT.payload().to_vec())]);
}
