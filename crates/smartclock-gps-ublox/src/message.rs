//! The messages this crate decodes: their IDs, their payloads'
//! lengths, and a frame decoded.

use crate::ack::Ack;
use crate::fields::Fields;
use crate::frame::Block;
use crate::frame::Frame;
use crate::frame::Header;
use crate::frame::MessageId;
use crate::mon::MonHw;
use crate::nav::NAV_SAT_BLOCK;
use crate::nav::NAV_SAT_COUNT;
use crate::nav::NAV_SAT_HEADER;
use crate::nav::NavClock;
use crate::nav::NavDop;
use crate::nav::NavEoe;
use crate::nav::NavPosEcef;
use crate::nav::NavPvt;
use crate::nav::NavSat;
use crate::nav::NavSatHeader;
use crate::nav::NavTimeGps;
use crate::nav::NavTimeLs;
use crate::nav::NavVelEcef;
use crate::nav::SatelliteInfo;
use crate::rxm::Measurement;
use crate::rxm::RAWX_BLOCK;
use crate::rxm::RAWX_COUNT;
use crate::rxm::RAWX_HEADER;
use crate::rxm::RxmRawx;
use crate::rxm::RxmRawxHeader;
use crate::rxm::RxmSfrbx;
use crate::rxm::SFRBX_COUNT;
use crate::rxm::SFRBX_HEADER;
use crate::rxm::SFRBX_WORD;
use crate::tim::TimTp;
use crate::units::Milliseconds;

/// The navigation results' class (section 32.6, p. 172).
const CLASS_NAV: u8 = 0x01;
/// The receiver manager's class.
const CLASS_RXM: u8 = 0x02;
/// The acknowledgments' class.
const CLASS_ACK: u8 = 0x05;
/// The monitoring class.
const CLASS_MON: u8 = 0x0a;
/// The timing class.
const CLASS_TIM: u8 = 0x0d;

/// `gnssId` for GPS (appendix A, p. 446).
pub const SYSTEM_GPS: u8 = 0;

/// `UBX-NAV-POSECEF`, the position in Earth-centered coordinates
/// (section 32.17.15, p. 374).
pub const NAV_POSECEF: MessageId = MessageId::new(CLASS_NAV, 0x01);
/// `UBX-NAV-DOP`, the dilutions of precision (section 32.17.6, p. 361).
pub const NAV_DOP: MessageId = MessageId::new(CLASS_NAV, 0x04);
/// `UBX-NAV-PVT`, the position, velocity and time solution (section
/// 32.17.17, p. 375).
pub const NAV_PVT: MessageId = MessageId::new(CLASS_NAV, 0x07);
/// `UBX-NAV-VELECEF`, the velocity in Earth-centered coordinates
/// (section 32.17.33, p. 401).
pub const NAV_VELECEF: MessageId = MessageId::new(CLASS_NAV, 0x11);
/// `UBX-NAV-TIMEGPS`, the GPS time solution (section 32.17.30, p. 397).
pub const NAV_TIMEGPS: MessageId = MessageId::new(CLASS_NAV, 0x20);
/// `UBX-NAV-CLOCK`, the clock solution (section 32.17.3, p. 359).
pub const NAV_CLOCK: MessageId = MessageId::new(CLASS_NAV, 0x22);
/// `UBX-NAV-TIMELS`, the leap second (section 32.17.31, p. 398).
pub const NAV_TIMELS: MessageId = MessageId::new(CLASS_NAV, 0x26);
/// `UBX-NAV-SAT`, the satellites (section 32.17.20, p. 381).
pub const NAV_SAT: MessageId = MessageId::new(CLASS_NAV, 0x35);
/// `UBX-NAV-EOE`, the end of an epoch (section 32.17.8, p. 363).
pub const NAV_EOE: MessageId = MessageId::new(CLASS_NAV, 0x61);
/// `UBX-RXM-SFRBX`, a subframe of a satellite's navigation data
/// (section 32.18.7, p. 419).
pub const RXM_SFRBX: MessageId = MessageId::new(CLASS_RXM, 0x13);
/// `UBX-RXM-RAWX`, the raw measurements (section 32.18.4, p. 409).
pub const RXM_RAWX: MessageId = MessageId::new(CLASS_RXM, 0x15);
/// `UBX-ACK-NAK`, a message refused (section 32.8.2, p. 179).
pub const ACK_NAK: MessageId = MessageId::new(CLASS_ACK, 0x00);
/// `UBX-ACK-ACK`, a message acknowledged (section 32.8.1, p. 179).
pub const ACK_ACK: MessageId = MessageId::new(CLASS_ACK, 0x01);
/// `UBX-MON-HW`, the hardware's status (section 32.16.4, p. 342).
pub const MON_HW: MessageId = MessageId::new(CLASS_MON, 0x09);
/// `UBX-TIM-TP`, the next time pulse (section 32.20.8, p. 432).
pub const TIM_TP: MessageId = MessageId::new(CLASS_TIM, 0x01);

/// The most blocks a message of a header and blocks has: its count,
/// `numSvs`, `numMeas` or `numWords`, is a `U1` (sections 32.17.20,
/// 32.18.4 and 32.18.7, pp. 381, 409 and 419).
const BLOCKS_MAX: usize = u8::MAX as usize;

/// How long a message's payload is.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum PayloadLength {
    /// Always this many bytes.
    Fixed(usize),
    /// `header` bytes and any number of blocks of `block`, the header
    /// giving their count in its byte at `count`.
    Blocks {
        /// The bytes before the first block.
        header: usize,
        /// Each block's bytes.
        block: usize,
        /// Where in the header the count of blocks is.
        count: usize,
    },
}

impl PayloadLength {
    /// Whether a payload may be `length` bytes: a fixed message's own,
    /// or a header and a whole number of blocks, no more than its
    /// header's one-byte count can give.
    #[must_use]
    pub const fn allows(self, length: usize) -> bool {
        match self {
            Self::Fixed(fixed) => length == fixed,
            Self::Blocks { header, block, .. } => {
                length >= header
                    && (length - header).is_multiple_of(block)
                    && (length - header) / block <= BLOCKS_MAX
            }
        }
    }
}

/// The messages this crate decodes, each with its payload's length as
/// its section's "Message Structure" gives it.
pub const MESSAGES: [(MessageId, PayloadLength); 15] = [
    (NAV_POSECEF, PayloadLength::Fixed(20)),
    (NAV_DOP, PayloadLength::Fixed(18)),
    (NAV_PVT, PayloadLength::Fixed(92)),
    (NAV_VELECEF, PayloadLength::Fixed(20)),
    (NAV_TIMEGPS, PayloadLength::Fixed(16)),
    (NAV_CLOCK, PayloadLength::Fixed(20)),
    (NAV_TIMELS, PayloadLength::Fixed(24)),
    (
        NAV_SAT,
        PayloadLength::Blocks {
            header: NAV_SAT_HEADER,
            block: NAV_SAT_BLOCK,
            count: NAV_SAT_COUNT,
        },
    ),
    (NAV_EOE, PayloadLength::Fixed(4)),
    (
        RXM_SFRBX,
        PayloadLength::Blocks {
            header: SFRBX_HEADER,
            block: SFRBX_WORD,
            count: SFRBX_COUNT,
        },
    ),
    (
        RXM_RAWX,
        PayloadLength::Blocks {
            header: RAWX_HEADER,
            block: RAWX_BLOCK,
            count: RAWX_COUNT,
        },
    ),
    (ACK_NAK, PayloadLength::Fixed(2)),
    (ACK_ACK, PayloadLength::Fixed(2)),
    (MON_HW, PayloadLength::Fixed(60)),
    (TIM_TP, PayloadLength::Fixed(16)),
];

/// The length of a message's payload, `None` for one not in
/// [`MESSAGES`].
#[must_use]
pub fn payload_length(id: MessageId) -> Option<PayloadLength> {
    MESSAGES
        .iter()
        .find_map(|&(known, length)| (known == id).then_some(length))
}

/// The messages a [`Streamer`](crate::frame::Streamer) gives a header
/// and a block at a time: those whose length grows with the sky.
/// `RXM-SFRBX` is not among them: a GPS subframe is 48 bytes, and is
/// decoded whole.
pub const STREAMED: [MessageId; 2] = [NAV_SAT, RXM_RAWX];

/// How a streamed message's payload is cut.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) struct Layout {
    /// The header's length.
    pub(crate) header: usize,
    /// Each block's.
    pub(crate) block: usize,
    /// Where in the header the count of blocks is.
    pub(crate) count: usize,
}

/// A streamed message's layout; `None` for a message not streamed.
pub(crate) fn streamed_layout(id: MessageId) -> Option<Layout> {
    match payload_length(id).filter(|_| STREAMED.contains(&id))? {
        PayloadLength::Blocks {
            header,
            block,
            count,
        } => Some(Layout {
            header,
            block,
            count,
        }),
        PayloadLength::Fixed(_) => None,
    }
}

/// The longest header or block of a message of [`STREAMED`]: the least
/// a [`Streamer`](crate::frame::Streamer)'s buffer holds.
pub const LONGEST_PART: usize = {
    let mut longest = 0;
    let mut index = 0;
    while index < MESSAGES.len() {
        let (id, length) = MESSAGES[index];
        let mut streamed = 0;
        while streamed < STREAMED.len() {
            let other = STREAMED[streamed];
            if let PayloadLength::Blocks { header, block, .. } = length
                && other.class == id.class
                && other.id == id.id
            {
                if header > longest {
                    longest = header;
                }
                if block > longest {
                    longest = block;
                }
            }
            streamed += 1;
        }
        index += 1;
    }
    longest
};

/// A part of a streamed message, decoded.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Part {
    /// `UBX-NAV-SAT`'s header.
    NavSatHeader(NavSatHeader),
    /// One of its satellites.
    Satellite(SatelliteInfo),
    /// `UBX-RXM-RAWX`'s header.
    RxmRawxHeader(RxmRawxHeader),
    /// One of its measurements.
    Measurement(Measurement),
}

impl Part {
    /// What a header says.  `None` as for the whole message's
    /// [`Message::parse`].
    #[must_use]
    pub fn header(header: &Header<'_>) -> Option<Self> {
        let fields = Fields(header.bytes);
        match header.id {
            NAV_SAT => NavSatHeader::parse(fields, header.blocks).map(Self::NavSatHeader),
            RXM_RAWX => RxmRawxHeader::parse(fields, header.blocks).map(Self::RxmRawxHeader),
            _ => None,
        }
    }

    /// What a block says.
    #[must_use]
    pub fn block(block: &Block<'_>) -> Option<Self> {
        let fields = Fields(block.bytes);
        match block.id {
            NAV_SAT => Some(Self::Satellite(SatelliteInfo::parse(fields))),
            RXM_RAWX => Some(Self::Measurement(Measurement::parse(fields))),
            _ => None,
        }
    }
}

/// A message of [`MESSAGES`], decoded.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Message<'a> {
    /// `UBX-NAV-POSECEF`.
    NavPosEcef(NavPosEcef),
    /// `UBX-NAV-DOP`.
    NavDop(NavDop),
    /// `UBX-NAV-PVT`.
    NavPvt(NavPvt),
    /// `UBX-NAV-VELECEF`.
    NavVelEcef(NavVelEcef),
    /// `UBX-NAV-TIMEGPS`.
    NavTimeGps(NavTimeGps),
    /// `UBX-NAV-CLOCK`.
    NavClock(NavClock),
    /// `UBX-NAV-TIMELS`.
    NavTimeLs(NavTimeLs),
    /// `UBX-NAV-SAT`.
    NavSat(NavSat<'a>),
    /// `UBX-NAV-EOE`.
    NavEoe(NavEoe),
    /// `UBX-RXM-SFRBX`.
    RxmSfrbx(RxmSfrbx<'a>),
    /// `UBX-RXM-RAWX`.
    RxmRawx(RxmRawx<'a>),
    /// `UBX-ACK-NAK`.
    AckNak(Ack),
    /// `UBX-ACK-ACK`.
    AckAck(Ack),
    /// `UBX-MON-HW`.
    MonHw(MonHw),
    /// `UBX-TIM-TP`.
    TimTp(TimTp),
}

impl<'a> Message<'a> {
    /// What a frame says.  `None` for a message not in [`MESSAGES`] or
    /// not of a length it may have; for a `NAV-SAT`, `RXM-RAWX` or
    /// `RXM-SFRBX` whose count of blocks is not the one its length
    /// gives; and for a `NAV-SAT` or `NAV-TIMELS` of a version other
    /// than the document's.
    #[must_use]
    pub fn parse(frame: Frame<'a>) -> Option<Self> {
        Self::parse_payload(frame.id(), frame.payload())
    }

    /// What a message's payload says, for a caller that has its ID and
    /// payload without a frame, such as a tool rewriting a recording.
    /// `None` as for [`Message::parse`], whose checks it makes.
    #[must_use]
    pub fn parse_payload(id: MessageId, payload: &'a [u8]) -> Option<Self> {
        if !payload_length(id)?.allows(payload.len()) {
            return None;
        }
        let fields = Fields(payload);
        Some(match id {
            NAV_POSECEF => Self::NavPosEcef(NavPosEcef::parse(fields)),
            NAV_DOP => Self::NavDop(NavDop::parse(fields)),
            NAV_PVT => Self::NavPvt(NavPvt::parse(fields)),
            NAV_VELECEF => Self::NavVelEcef(NavVelEcef::parse(fields)),
            NAV_TIMEGPS => Self::NavTimeGps(NavTimeGps::parse(fields)),
            NAV_CLOCK => Self::NavClock(NavClock::parse(fields)),
            NAV_TIMELS => Self::NavTimeLs(NavTimeLs::parse(fields)?),
            NAV_SAT => Self::NavSat(NavSat::parse(payload)?),
            NAV_EOE => Self::NavEoe(NavEoe {
                time_of_week: fields.time_of_week(),
            }),
            RXM_SFRBX => Self::RxmSfrbx(RxmSfrbx::parse(payload)?),
            RXM_RAWX => Self::RxmRawx(RxmRawx::parse(payload)?),
            ACK_NAK => Self::AckNak(Ack::parse(fields)),
            ACK_ACK => Self::AckAck(Ack::parse(fields)),
            MON_HW => Self::MonHw(MonHw::parse(fields)),
            TIM_TP => Self::TimTp(TimTp::parse(fields)),
            _ => return None,
        })
    }

    /// The epoch a navigation message belongs to, its `iTOW`; `None`
    /// for a message of another class.
    #[must_use]
    pub const fn epoch(&self) -> Option<Milliseconds<u32>> {
        match self {
            Self::NavPosEcef(message) => Some(message.time_of_week),
            Self::NavDop(message) => Some(message.time_of_week),
            Self::NavPvt(message) => Some(message.time_of_week),
            Self::NavVelEcef(message) => Some(message.time_of_week),
            Self::NavTimeGps(message) => Some(message.time_of_week),
            Self::NavClock(message) => Some(message.time_of_week),
            Self::NavTimeLs(message) => Some(message.time_of_week),
            Self::NavSat(message) => Some(message.header.time_of_week),
            Self::NavEoe(message) => Some(message.time_of_week),
            Self::RxmSfrbx(_)
            | Self::RxmRawx(_)
            | Self::AckNak(_)
            | Self::AckAck(_)
            | Self::MonHw(_)
            | Self::TimTp(_) => None,
        }
    }
}
