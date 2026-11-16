//! The messages this crate decodes: their IDs, their payloads'
//! lengths, and a frame decoded.

use smartclock_gps::units::Milliseconds;

use crate::fields::Fields;
use crate::frame::Frame;
use crate::frame::MessageId;
use crate::mon::MonHw;
use crate::nav::NAV_SAT_BLOCK;
use crate::nav::NAV_SAT_HEADER;
use crate::nav::NavDop;
use crate::nav::NavEoe;
use crate::nav::NavPosEcef;
use crate::nav::NavPvt;
use crate::nav::NavSat;
use crate::nav::NavTimeGps;
use crate::nav::NavTimeLs;
use crate::tim::TimTp;
/// The navigation results' class (section 32.6, p. 172).
const CLASS_NAV: u8 = 0x01;
/// The monitoring class.
const CLASS_MON: u8 = 0x0a;
/// The timing class.
const CLASS_TIM: u8 = 0x0d;

/// `UBX-NAV-POSECEF`, the position in Earth-centered coordinates
/// (section 32.17.15, p. 374).
pub const NAV_POSECEF: MessageId = MessageId::new(CLASS_NAV, 0x01);
/// `UBX-NAV-DOP`, the dilutions of precision (section 32.17.6, p. 361).
pub const NAV_DOP: MessageId = MessageId::new(CLASS_NAV, 0x04);
/// `UBX-NAV-PVT`, the position, velocity and time solution (section
/// 32.17.17, p. 375).
pub const NAV_PVT: MessageId = MessageId::new(CLASS_NAV, 0x07);
/// `UBX-NAV-TIMEGPS`, the GPS time solution (section 32.17.30, p. 397).
pub const NAV_TIMEGPS: MessageId = MessageId::new(CLASS_NAV, 0x20);
/// `UBX-NAV-TIMELS`, the leap second (section 32.17.31, p. 398).
pub const NAV_TIMELS: MessageId = MessageId::new(CLASS_NAV, 0x26);
/// `UBX-NAV-SAT`, the satellites (section 32.17.20, p. 381).
pub const NAV_SAT: MessageId = MessageId::new(CLASS_NAV, 0x35);
/// `UBX-NAV-EOE`, the end of an epoch (section 32.17.8, p. 363).
pub const NAV_EOE: MessageId = MessageId::new(CLASS_NAV, 0x61);
/// `UBX-MON-HW`, the hardware's status (section 32.16.4, p. 342).
pub const MON_HW: MessageId = MessageId::new(CLASS_MON, 0x09);
/// `UBX-TIM-TP`, the next time pulse (section 32.20.8, p. 432).
pub const TIM_TP: MessageId = MessageId::new(CLASS_TIM, 0x01);

/// How long a message's payload is.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum PayloadLength {
    /// Always this many bytes.
    Fixed(usize),
    /// `header` bytes and any number of blocks of `block`.
    Blocks {
        /// The bytes before the first block.
        header: usize,
        /// Each block's bytes.
        block: usize,
    },
}

impl PayloadLength {
    /// Whether a payload may be `length` bytes.
    #[must_use]
    pub const fn allows(self, length: usize) -> bool {
        match self {
            Self::Fixed(fixed) => length == fixed,
            Self::Blocks { header, block } => {
                length >= header && (length - header).is_multiple_of(block)
            }
        }
    }
}

/// The messages this crate decodes, each with its payload's length as
/// its section's "Message Structure" gives it.
pub const MESSAGES: [(MessageId, PayloadLength); 9] = [
    (NAV_POSECEF, PayloadLength::Fixed(20)),
    (NAV_DOP, PayloadLength::Fixed(18)),
    (NAV_PVT, PayloadLength::Fixed(92)),
    (NAV_TIMEGPS, PayloadLength::Fixed(16)),
    (NAV_TIMELS, PayloadLength::Fixed(24)),
    (
        NAV_SAT,
        PayloadLength::Blocks {
            header: NAV_SAT_HEADER,
            block: NAV_SAT_BLOCK,
        },
    ),
    (NAV_EOE, PayloadLength::Fixed(4)),
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

/// A message of [`MESSAGES`], decoded.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Message<'a> {
    /// `UBX-NAV-POSECEF`.
    NavPosEcef(NavPosEcef),
    /// `UBX-NAV-DOP`.
    NavDop(NavDop),
    /// `UBX-NAV-PVT`.
    NavPvt(NavPvt),
    /// `UBX-NAV-TIMEGPS`.
    NavTimeGps(NavTimeGps),
    /// `UBX-NAV-TIMELS`.
    NavTimeLs(NavTimeLs),
    /// `UBX-NAV-SAT`.
    NavSat(NavSat<'a>),
    /// `UBX-NAV-EOE`.
    NavEoe(NavEoe),
    /// `UBX-MON-HW`.
    MonHw(MonHw),
    /// `UBX-TIM-TP`.
    TimTp(TimTp),
}

impl<'a> Message<'a> {
    /// What a frame says.  `None` for a message not in [`MESSAGES`],
    /// and for a `NAV-SAT` or `NAV-TIMELS` of a version other than the
    /// document's.
    #[must_use]
    pub fn parse(frame: Frame<'a>) -> Option<Self> {
        let payload = frame.payload();
        if !payload_length(frame.id())?.allows(payload.len()) {
            return None;
        }
        let fields = Fields(payload);
        Some(match frame.id() {
            NAV_POSECEF => Self::NavPosEcef(NavPosEcef::parse(fields)),
            NAV_DOP => Self::NavDop(NavDop::parse(fields)),
            NAV_PVT => Self::NavPvt(NavPvt::parse(fields)),
            NAV_TIMEGPS => Self::NavTimeGps(NavTimeGps::parse(fields)),
            NAV_TIMELS => Self::NavTimeLs(NavTimeLs::parse(fields)?),
            NAV_SAT => Self::NavSat(NavSat::parse(payload)?),
            NAV_EOE => Self::NavEoe(NavEoe {
                time_of_week: fields.time_of_week(),
            }),
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
            Self::NavTimeGps(message) => Some(message.time_of_week),
            Self::NavTimeLs(message) => Some(message.time_of_week),
            Self::NavSat(message) => Some(message.time_of_week),
            Self::NavEoe(message) => Some(message.time_of_week),
            Self::MonHw(_) | Self::TimTp(_) => None,
        }
    }
}
