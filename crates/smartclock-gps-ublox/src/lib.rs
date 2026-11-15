//! u-blox UBX framing, and the messages that fill a `smartclock-gps`
//! record.
//!
//! No `SmartClock` holds a u-blox receiver, so this is not in
//! `smartclock-gps` with the formats the receivers' engines speak.  It
//! is for the other end: a u-blox receiver on a bench is where a tool
//! that stands in for an engine gets each second's [`Record`], live or
//! from a recording of its output.  Like `smartclock-gps` it is
//! `no_std` and does not allocate.
//!
//! The format is that of the u-blox 8 / u-blox M8 receiver description
//! including protocol specification, UBX-13003221, revision R28.  No
//! copy is in `third_party/`; a section and page cited here are that
//! revision's, the page as printed.
//!
//! A frame is the two sync bytes `B5 62`, a class, an ID, the payload's
//! length as a little-endian 16-bit count, the payload, and two checksum
//! bytes: the 8-bit Fletcher sum of everything from the class through
//! the payload (sections 32.2 and 32.4, pp. 168 and 171).  Every number
//! in a payload is little-endian.
//!
//! # Resynchronizing
//!
//! [`Deframer`] finds frames in a stream that may start mid-frame or
//! carry noise.  The document says only what a frame is, so where to
//! pick the stream up again is this crate's own:
//!
//! - Outside a frame every byte but `B5` is skipped, and a `B5` not
//!   followed by `62` too, with no error.  A further `B5` after one is
//!   taken as the start.
//! - A length is believed only as far as it can be checked before the
//!   checksum.  For a message of [`MESSAGES`] it must be that message's
//!   own, or the frame is [`FrameError::BadLength`]; for any message it
//!   must fit the deframer's buffer, or it is [`FrameError::Overflow`].
//!   Either is reported at the length's second byte and a new start is
//!   looked for from the next: no payload is skipped on the word of a
//!   length nothing vouches for, which could be 65535 bytes.
//! - Inside a payload nothing is looked for: a `B5 62` there is data.
//! - A bad checksum is [`FrameError::BadChecksum`], reported at its
//!   second byte, and the frame's bytes are not examined again.  So a
//!   damaged length that passes the checks above costs at most the
//!   buffer's length of what follows it.
//!
//! # From messages to a record
//!
//! [`Assembler`] takes the stream a byte at a time and gives a
//! [`Record`] for each navigation epoch.  It is an
//! [`Assemble`], so [`Replay`](smartclock_gps::source::replay::Replay) reads a
//! recording through it, and a caller with a live receiver pushes each
//! byte with the slot of a [`Latest`](smartclock_gps::source::Latest).  The
//! receiver is to be set to one epoch a second with the messages of
//! [`MESSAGES`] turned on; configuring it is the caller's affair.
//!
//! - **Epochs.**  Messages with the same `iTOW` are one navigation
//!   solution (section 9.3, p. 27).  `NAV-EOE` ends an epoch; so does a
//!   navigation message with another `iTOW`, for a receiver that sends
//!   no `NAV-EOE` or a stream that lost one, and then the record comes
//!   a message late.  Nothing from one `iTOW` is put with another's.
//! - **Time** is `NAV-TIMEGPS`'s, to the nearest second, and only from
//!   an epoch whose `NAV-PVT` has `gnssFixOK` set and whose
//!   `NAV-TIMEGPS` has a valid time of week and week: without a fix the
//!   receiver gives plausible times from its own clock (section 9.2,
//!   p. 27).  The week is the receiver's own resolution of the
//!   satellites' ten bits (section 9.9.1, p. 29) unless the assembler
//!   was made with a week to resolve it near ([`Assembler::near`]).
//! - **The leap second.**  GPS less UTC is `NAV-TIMELS`'s `currLs`, or
//!   `NAV-TIMEGPS`'s `leapS` from a receiver that sends no `NAV-TIMELS`.
//!   A leap second is `NAV-TIMELS`'s while its countdown is above zero,
//!   placed by its week and day ([`LeapNotice::event`]).  Once
//!   announced it is remembered, and from its second on the record's
//!   offset is the one it leaves whatever the receiver reports in that
//!   second, the document not saying in which second `currLs` steps.
//! - **Satellites** are `NAV-SAT`'s GPS ones with a known place in the
//!   sky, those used in the fix first, and the lowest left out if there
//!   are more than a record holds.
//! - **Not filled.**  [`Record::timing`]: `TIM-TP`'s `qErr` is decoded
//!   ([`TimTp`]), but the document gives neither its sign nor what
//!   multiple of sigma `tAcc` is.  [`Record::almanac`], and a
//!   satellite's Doppler and time error: no message here carries them.
//!   The Time RAIM result: a u-blox receiver has none to report.
//!
//! [`FILLER`] says which parts of a record that comes to, and the
//! crate's tests hold it to what [`Assembler`] writes.

#![no_std]

use core::fmt;
use core::mem;

use thiserror::Error;

use smartclock_gps::NoRoom;
use smartclock_gps::engine::settings::SettingSet;
use smartclock_gps::framing::Step;
use smartclock_gps::framing::feed;
use smartclock_gps::record::Antenna;
use smartclock_gps::record::Dops;
use smartclock_gps::record::Ecef;
use smartclock_gps::record::Fix;
use smartclock_gps::record::GpsTime;
use smartclock_gps::record::Health;
use smartclock_gps::record::Position;
use smartclock_gps::record::Record;
use smartclock_gps::record::Satellite;
use smartclock_gps::record::Satellites;
use smartclock_gps::record::Tracking;
use smartclock_gps::record::Velocity;
use smartclock_gps::record::field::FieldSet;
use smartclock_gps::record::field::Filler;
use smartclock_gps::record::field::RecordField;
use smartclock_gps::source::replay::Assemble;
use smartclock_gps::time::ALMANAC_WEEKS;
use smartclock_gps::time::BROADCAST_WEEKS;
use smartclock_gps::time::GpsSeconds;
use smartclock_gps::time::GpsWeek;
use smartclock_gps::time::LeapChange;
use smartclock_gps::time::LeapEvent;
use smartclock_gps::time::SECONDS_PER_DAY;
use smartclock_gps::units::Centimeters;
use smartclock_gps::units::CentimetersPerSecond;
use smartclock_gps::units::DecibelHertz;
use smartclock_gps::units::Degrees;
use smartclock_gps::units::MilliArcSeconds;
use smartclock_gps::units::Milliseconds;
use smartclock_gps::units::Nanoseconds;
use smartclock_gps::units::Prn;
use smartclock_gps::units::Seconds;
use smartclock_gps::units::TenthDegrees;
use smartclock_gps::units::Tenths;

/// A frame's first two bytes (section 32.2, p. 168).
pub const SYNC: [u8; 2] = [0xb5, 0x62];

/// The class, ID and length that follow the sync bytes.
const HEADER: usize = 4;

/// The checksum's two bytes, `CK_A` and `CK_B`.
const CHECKSUM: usize = 2;

/// The bytes a frame adds to its payload.
pub const OVERHEAD: usize = SYNC.len() + HEADER + CHECKSUM;

/// Which message a frame is: its class and its ID within the class
/// (section 32.2, p. 168).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct MessageId {
    /// The class, a group of related messages.
    pub class: u8,
    /// The message within its class.
    pub id: u8,
}

impl MessageId {
    /// The message `id` of `class`.
    #[must_use]
    pub const fn new(class: u8, id: u8) -> Self {
        Self { class, id }
    }
}

impl fmt::Display for MessageId {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{:#04x} {:#04x}", self.class, self.id)
    }
}

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

/// `NAV-SAT`'s payload before its first satellite.
const NAV_SAT_HEADER: usize = 8;
/// One satellite's block of `NAV-SAT`.
const NAV_SAT_BLOCK: usize = 12;

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

/// The length of a `NAV-SAT` payload that reports `satellites`: what a
/// [`Deframer`]'s or an [`Assembler`]'s `N` must reach for a receiver
/// that reports so many, of every system it tracks.
#[must_use]
pub const fn nav_sat_length(satellites: usize) -> usize {
    NAV_SAT_HEADER + NAV_SAT_BLOCK * satellites
}

/// The Fletcher sum with one more byte in it (section 32.4, p. 171).
const fn sum_with(sum: [u8; CHECKSUM], byte: u8) -> [u8; CHECKSUM] {
    let first = sum[0].wrapping_add(byte);
    [first, sum[1].wrapping_add(first)]
}

/// A frame's checksum, `CK_A` then `CK_B`: the 8-bit Fletcher sum of
/// `body`, the frame's bytes from its class through its payload
/// (section 32.4, p. 171).
#[must_use]
pub fn checksum(body: &[u8]) -> [u8; CHECKSUM] {
    body.iter()
        .fold([0; CHECKSUM], |sum, &byte| sum_with(sum, byte))
}

/// Why [`encode`] wrote no frame.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Error)]
pub enum EncodeError {
    /// A payload longer than a frame's 16-bit length can say.
    #[error("a payload of {0} bytes is more than a frame can hold")]
    TooLong(usize),
    /// The output buffer is shorter than the frame.
    #[error(transparent)]
    NoRoom(#[from] NoRoom),
}

/// Writes a frame into `out` and returns its length.  The payload's
/// length is the caller's to get right: [`MESSAGES`] is not consulted.
///
/// # Errors
///
/// [`EncodeError::TooLong`] for a payload over 65535 bytes, and
/// [`EncodeError::NoRoom`] if `out` is shorter than the frame.
pub fn encode(id: MessageId, payload: &[u8], out: &mut [u8]) -> Result<usize, EncodeError> {
    let length = u16::try_from(payload.len()).map_err(|_| EncodeError::TooLong(payload.len()))?;
    let needed = OVERHEAD + payload.len();
    let room = out.len();
    let frame = out.get_mut(..needed).ok_or(NoRoom { needed, room })?;
    let (sync, rest) = frame.split_at_mut(SYNC.len());
    let (body, sum) = rest.split_at_mut(HEADER + payload.len());
    let (header, data) = body.split_at_mut(HEADER);
    let [low, high] = length.to_le_bytes();
    sync.copy_from_slice(&SYNC);
    header.copy_from_slice(&[id.class, id.id, low, high]);
    data.copy_from_slice(payload);
    sum.copy_from_slice(&checksum(body));
    Ok(needed)
}

/// A whole frame with a good checksum, lent by a [`Deframer`] until its
/// next byte.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Frame<'a> {
    /// Which message it is.
    id: MessageId,
    /// Its payload.
    payload: &'a [u8],
}

impl<'a> Frame<'a> {
    /// Which message it is.
    #[must_use]
    pub const fn id(&self) -> MessageId {
        self.id
    }

    /// Its payload.
    #[must_use]
    pub const fn payload(&self) -> &'a [u8] {
        self.payload
    }
}

/// Why a [`Deframer`] discarded what it was reading.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Error)]
pub enum FrameError {
    /// A message of [`MESSAGES`] with a length that is not its own.
    #[error("{id} cannot have a payload of {length} bytes")]
    BadLength {
        /// The message.
        id: MessageId,
        /// The length its frame gave.
        length: usize,
    },
    /// A payload longer than the deframer's buffer.
    #[error("{id} has a payload of {length} bytes, more than the buffer's {capacity}")]
    Overflow {
        /// The message.
        id: MessageId,
        /// The length its frame gave.
        length: usize,
        /// The buffer's, the deframer's `N`.
        capacity: usize,
    },
    /// A frame whose checksum is not the sum of its bytes.
    #[error("{id} has checksum {found:02x?}, not {expected:02x?}")]
    BadChecksum {
        /// The message.
        id: MessageId,
        /// The sum of its class, ID, length and payload.
        expected: [u8; CHECKSUM],
        /// The two bytes it carried.
        found: [u8; CHECKSUM],
    },
}

/// Where a [`Deframer`] is in a frame.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum State {
    /// Outside a frame.
    Idle,
    /// After the first sync byte.
    Sync,
    /// After both: the class is next.
    Class,
    /// The ID is next.
    Id,
    /// The length's low byte is next.
    LengthLow,
    /// Its high byte is next.
    LengthHigh,
    /// In the payload.
    Payload,
    /// The checksum's first byte is next.
    ChecksumFirst,
    /// After the checksum's first byte, which was this.
    ChecksumSecond(u8),
}

/// Finds frames in a stream of bytes, however it is cut into chunks
/// ("Resynchronizing" in the crate's documentation).  `N` is the
/// longest payload it holds; a longer one is not framed, it is
/// [`FrameError::Overflow`].
#[derive(Debug, Clone)]
pub struct Deframer<const N: usize> {
    state: State,
    /// The message of the frame in progress.
    id: MessageId,
    /// Its payload's length.
    length: usize,
    /// The sum of its bytes so far.
    sum: [u8; CHECKSUM],
    /// Its payload so far.
    buffer: [u8; N],
    /// The bytes of `buffer` in use.
    filled: usize,
}

impl<const N: usize> Default for Deframer<N> {
    fn default() -> Self {
        Self::new()
    }
}

impl<const N: usize> Deframer<N> {
    /// A deframer outside any frame.
    #[must_use]
    pub const fn new() -> Self {
        Self {
            state: State::Idle,
            id: MessageId::new(0, 0),
            length: 0,
            sum: [0; CHECKSUM],
            buffer: [0; N],
            filled: 0,
        }
    }

    /// Takes one byte, and returns a frame or an error if the byte ended
    /// one.
    pub fn push(&mut self, byte: u8) -> Option<Result<Frame<'_>, FrameError>> {
        let step = self.step(byte)?;
        Some(step.map(|length| self.frame(length)))
    }

    /// Takes bytes from the front of `chunk` up to the first that ends a
    /// frame or is an error, and returns how many it took and what that
    /// one ended.  With bytes left over, call again with the rest:
    ///
    /// ```
    /// use smartclock_gps_ublox::Deframer;
    /// use smartclock_gps_ublox::NAV_EOE;
    ///
    /// let mut deframer = Deframer::<4>::new();
    /// let mut rest: &[u8] = &[
    ///     0x00, 0xb5, // noise
    ///     0xb5, 0x62, 0x01, 0x61, 0x04, 0x00, 0x10, 0x27, 0x00, 0x00, 0x9d, 0x7c,
    /// ];
    /// let mut frames = 0;
    /// while !rest.is_empty() {
    ///     let (taken, event) = deframer.feed(rest);
    ///     rest = &rest[taken..];
    ///     if let Some(Ok(frame)) = event {
    ///         assert_eq!(frame.id(), NAV_EOE);
    ///         frames += 1;
    ///     }
    /// }
    /// assert_eq!(frames, 1);
    /// ```
    pub fn feed(&mut self, chunk: &[u8]) -> (usize, Option<Result<Frame<'_>, FrameError>>) {
        let (taken, ended) = feed(chunk, |byte| self.step(byte));
        (
            taken,
            ended.map(|step| step.map(|length| self.frame(length))),
        )
    }

    /// Forgets any frame in progress.
    pub const fn reset(&mut self) {
        self.state = State::Idle;
    }

    fn frame(&self, length: usize) -> Frame<'_> {
        Frame {
            id: self.id,
            payload: &self.buffer[..length],
        }
    }

    /// Takes a byte of the frame's class, ID or length into its sum.
    const fn head(&mut self, byte: u8, next: State) {
        self.sum = sum_with(self.sum, byte);
        self.state = next;
    }

    /// Where the payload starts, or the checksum if there is none.
    const fn body(&self) -> State {
        if self.length == 0 {
            State::ChecksumFirst
        } else {
            State::Payload
        }
    }

    fn step(&mut self, byte: u8) -> Step<FrameError> {
        match self.state {
            State::Idle => {
                if byte == SYNC[0] {
                    self.state = State::Sync;
                }
            }
            State::Sync => {
                if byte == SYNC[1] {
                    self.sum = [0; CHECKSUM];
                    self.state = State::Class;
                } else if byte != SYNC[0] {
                    self.state = State::Idle;
                }
            }
            State::Class => {
                self.id.class = byte;
                self.head(byte, State::Id);
            }
            State::Id => {
                self.id.id = byte;
                self.head(byte, State::LengthLow);
            }
            State::LengthLow => {
                self.length = usize::from(byte);
                self.head(byte, State::LengthHigh);
            }
            State::LengthHigh => {
                self.length |= usize::from(byte) << u8::BITS;
                self.filled = 0;
                self.head(byte, self.body());
                let (id, length) = (self.id, self.length);
                let error = if payload_length(id).is_some_and(|own| !own.allows(length)) {
                    FrameError::BadLength { id, length }
                } else if length > N {
                    FrameError::Overflow {
                        id,
                        length,
                        capacity: N,
                    }
                } else {
                    return None;
                };
                self.state = State::Idle;
                return Some(Err(error));
            }
            State::Payload => {
                self.buffer[self.filled] = byte;
                self.filled += 1;
                self.sum = sum_with(self.sum, byte);
                if self.filled == self.length {
                    self.state = State::ChecksumFirst;
                }
            }
            State::ChecksumFirst => self.state = State::ChecksumSecond(byte),
            State::ChecksumSecond(first) => {
                self.state = State::Idle;
                let found = [first, byte];
                return Some(if found == self.sum {
                    Ok(self.length)
                } else {
                    Err(FrameError::BadChecksum {
                        id: self.id,
                        expected: self.sum,
                        found,
                    })
                });
            }
        }
        None
    }
}

/// A payload's fields, by their byte offsets: little-endian, as every
/// number in a payload is (section 32.3, p. 169).
#[derive(Debug, Clone, Copy)]
struct Fields<'a>(&'a [u8]);

impl Fields<'_> {
    fn bytes<const K: usize>(self, offset: usize) -> [u8; K] {
        self.0
            .get(offset..offset + K)
            .and_then(|field| field.try_into().ok())
            .unwrap_or([0; K])
    }

    fn u8(self, offset: usize) -> u8 {
        u8::from_le_bytes(self.bytes(offset))
    }

    fn i8(self, offset: usize) -> i8 {
        i8::from_le_bytes(self.bytes(offset))
    }

    fn u16(self, offset: usize) -> u16 {
        u16::from_le_bytes(self.bytes(offset))
    }

    fn i16(self, offset: usize) -> i16 {
        i16::from_le_bytes(self.bytes(offset))
    }

    fn u32(self, offset: usize) -> u32 {
        u32::from_le_bytes(self.bytes(offset))
    }

    fn i32(self, offset: usize) -> i32 {
        i32::from_le_bytes(self.bytes(offset))
    }

    /// `iTOW`, every navigation message's first field.
    fn time_of_week(self) -> Milliseconds<u32> {
        Milliseconds(self.u32(0))
    }
}

/// `value` times `numerator` over `denominator`, to the nearest whole.
const fn rescale(value: i64, numerator: i64, denominator: i64) -> i64 {
    (value * numerator + denominator / 2).div_euclid(denominator)
}

/// `value` as an `i32`, held to its ends.
fn to_i32(value: i64) -> i32 {
    i32::try_from(value).unwrap_or(if value < 0 { i32::MIN } else { i32::MAX })
}

/// `value` as a `u16`, held to its ends.
fn to_u16(value: i64) -> u16 {
    u16::try_from(value.max(0)).unwrap_or(u16::MAX)
}

/// The units of `lon` and `lat` in a degree: they are scaled 1e-7.
const ANGLE_UNITS_PER_DEGREE: i64 = 10_000_000;
const MILLI_ARC_SECONDS_PER_DEGREE: i64 = 3_600_000;
/// The units of `headMot` in a tenth of a degree: it is scaled 1e-5.
const HEADING_UNITS_PER_TENTH_DEGREE: i64 = 10_000;
const TENTH_DEGREES_PER_TURN: i64 = 3600;
const MILLIMETERS_PER_CENTIMETER: i64 = 10;
/// The units of a dilution of precision in a tenth: it is scaled 0.01.
const DOP_UNITS_PER_TENTH: i64 = 10;
const DEGREES_PER_TURN: i16 = 360;

/// An angle scaled 1e-7 degrees, in milliarcseconds.
fn milli_arc_seconds(angle: i32) -> MilliArcSeconds {
    MilliArcSeconds(to_i32(rescale(
        i64::from(angle),
        MILLI_ARC_SECONDS_PER_DEGREE,
        ANGLE_UNITS_PER_DEGREE,
    )))
}

/// Millimeters, in centimeters.
fn centimeters(millimeters: i32) -> Centimeters {
    Centimeters(to_i32(rescale(
        i64::from(millimeters),
        1,
        MILLIMETERS_PER_CENTIMETER,
    )))
}

/// A dilution of precision scaled 0.01, in tenths.
fn tenths(dop: u16) -> Tenths {
    Tenths(to_u16(rescale(i64::from(dop), 1, DOP_UNITS_PER_TENTH)))
}

/// `NAV-PVT`'s `fixType` values (p. 375).  1, dead reckoning alone, is
/// no fix from satellites, and is left out.
const FIX_TWO_D: u8 = 2;
const FIX_THREE_D: u8 = 3;
const FIX_WITH_DEAD_RECKONING: u8 = 4;
const FIX_TIME_ONLY: u8 = 5;
/// `NAV-PVT`'s `flags` bit 0, `gnssFixOK` (p. 377).
const PVT_FIX_OK: u8 = 1 << 0;
/// `NAV-PVT`'s `flags3` bit 0, `invalidLlh` (p. 378).
const PVT_INVALID_POSITION: u16 = 1 << 0;

/// `UBX-NAV-PVT` (section 32.17.17, pp. 375-378): what of it a record
/// holds.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct NavPvt {
    /// `iTOW`: the epoch.
    pub time_of_week: Milliseconds<u32>,
    /// The fix: `fixType`, and [`Fix::None`] unless `gnssFixOK` is set.
    /// A fix combined with dead reckoning counts as three-dimensional.
    pub fix: Fix,
    /// `lat`, `lon`, `height` and `hMSL`, to the record's units; `None`
    /// with `invalidLlh` set.
    pub position: Option<Position>,
    /// `gSpeed` and `headMot`, to the record's units.
    pub velocity: Velocity,
}

impl NavPvt {
    fn parse(fields: Fields<'_>) -> Self {
        let fix = match fields.u8(20) {
            _ if fields.u8(21) & PVT_FIX_OK == 0 => Fix::None,
            FIX_TWO_D => Fix::TwoD,
            FIX_THREE_D | FIX_WITH_DEAD_RECKONING => Fix::ThreeD,
            FIX_TIME_ONLY => Fix::TimeOnly,
            _ => Fix::None,
        };
        let position = (fields.u16(78) & PVT_INVALID_POSITION == 0).then(|| Position {
            longitude: milli_arc_seconds(fields.i32(24)),
            latitude: milli_arc_seconds(fields.i32(28)),
            ellipsoid: centimeters(fields.i32(32)),
            mean_sea_level: centimeters(fields.i32(36)),
        });
        let speed = rescale(i64::from(fields.i32(60)), 1, MILLIMETERS_PER_CENTIMETER);
        let heading = rescale(i64::from(fields.i32(64)), 1, HEADING_UNITS_PER_TENTH_DEGREE)
            .rem_euclid(TENTH_DEGREES_PER_TURN);
        Self {
            time_of_week: fields.time_of_week(),
            fix,
            position,
            velocity: Velocity {
                speed: CentimetersPerSecond(to_u16(speed)),
                heading: TenthDegrees(to_u16(heading)),
            },
        }
    }
}

/// `NAV-TIMEGPS`'s `valid` bits (p. 397).
const TIMEGPS_TIME_OF_WEEK_VALID: u8 = 1 << 0;
const TIMEGPS_WEEK_VALID: u8 = 1 << 1;
const TIMEGPS_LEAP_VALID: u8 = 1 << 2;

/// `UBX-NAV-TIMEGPS` (section 32.17.30, pp. 397-398).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct NavTimeGps {
    /// `iTOW`: the epoch, and with [`Self::fraction`] its time of week.
    pub time_of_week: Milliseconds<u32>,
    /// `fTOW`: what to add to `iTOW` for the precise time of week.
    pub fraction: Nanoseconds<i32>,
    /// `week`, as the receiver resolved it; `None` without `towValid`
    /// and `weekValid` both, or for a week below zero.
    pub week: Option<GpsWeek>,
    /// `leapS`, GPS less UTC; `None` without `leapSValid`.
    pub leap_seconds: Option<Seconds<i8>>,
    /// `tAcc`, the receiver's estimate of the time's accuracy.
    pub accuracy: Nanoseconds<u32>,
}

impl NavTimeGps {
    fn parse(fields: Fields<'_>) -> Self {
        let valid = fields.u8(11);
        let timed = TIMEGPS_TIME_OF_WEEK_VALID | TIMEGPS_WEEK_VALID;
        let week = u16::try_from(fields.i16(8)).ok().map(GpsWeek::new);
        Self {
            time_of_week: fields.time_of_week(),
            fraction: Nanoseconds(fields.i32(4)),
            week: week.filter(|_| valid & timed == timed),
            leap_seconds: (valid & TIMEGPS_LEAP_VALID != 0).then(|| Seconds(fields.i8(10))),
            accuracy: Nanoseconds(fields.u32(12)),
        }
    }

    /// The epoch's second, the nearest to its time of week.  With a
    /// `reference`, the week is taken for its ten broadcast bits alone
    /// and is the one nearest `reference` that has them: for a receiver
    /// whose own resolution cannot be trusted.  `None` without a valid
    /// time of week and week.
    #[must_use]
    pub fn seconds(&self, reference: Option<GpsWeek>) -> Option<GpsSeconds> {
        let week = self.week?;
        let week = match reference {
            Some(reference) => reference.nearest(week.get(), BROADCAST_WEEKS)?,
            None => week,
        };
        GpsSeconds::nearest(week, self.time_of_week, self.fraction)
    }
}

/// `NAV-TIMELS`'s version this crate knows (p. 398).
const TIMELS_VERSION: u8 = 0;
/// `srcOfCurrLs`: the firmware's built-in count, which "can be
/// outdated", and no source known (p. 398).
const TIMELS_COUNT_DEFAULT: u8 = 0;
const TIMELS_COUNT_UNKNOWN: u8 = 255;
/// `srcOfLsChange`: the systems whose week and day are GPS's, Galileo's
/// week being GPS's less a multiple of [`ALMANAC_WEEKS`] (p. 399).
const TIMELS_CHANGE_GPS: u8 = 2;
const TIMELS_CHANGE_GALILEO: u8 = 5;
/// `NAV-TIMELS`'s `valid` bits (p. 400).
const TIMELS_COUNT_VALID: u8 = 1 << 0;
const TIMELS_EVENT_VALID: u8 = 1 << 1;
/// `dateOfLsGpsDn` runs from 1, Sunday, to 7, Saturday (p. 399).
const DAYS: core::ops::RangeInclusive<u16> = 1..=7;

/// How far a leap second placed by its week and day may lie from where
/// `timeToLsEvent` puts it and still be believed.  The document does
/// not say which second the countdown ends on, the inserted one or the
/// midnight after it, so they may differ by one.
const LEAP_SLACK: i64 = 2;

/// A leap second as `UBX-NAV-TIMELS` announces it (p. 399).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct LeapNotice {
    /// `lsChange`: which way.
    pub change: LeapChange,
    /// `timeToLsEvent`: seconds until it, zero or less once it has
    /// come.
    pub until: Seconds<i32>,
    /// `dateOfLsGpsWn`: the week of the day it ends.  Whether this is
    /// the week in full or the eight bits a satellite sends, the
    /// document does not say.
    pub week: u16,
    /// `dateOfLsGpsDn`: that day, 1 for Sunday to 7 for Saturday.
    pub day: u16,
}

impl LeapNotice {
    /// Whether it is still to come.
    #[must_use]
    pub const fn pending(&self) -> bool {
        self.until.0 > 0
    }

    /// The leap second, `now` being the second the notice was given in
    /// and `before` GPS less UTC until the leap.
    ///
    /// It is put at the end of [`Self::day`] of [`Self::week`], the
    /// week read modulo [`ALMANAC_WEEKS`] and resolved nearest the week
    /// [`Self::until`] counts to, so it is found whether the receiver
    /// gives the week in full or cut short, at a week's end as on any
    /// other day.  `None` if that is not where the countdown ends, to
    /// within two seconds.
    #[must_use]
    pub fn event(&self, now: GpsSeconds, before: Seconds<i8>) -> Option<LeapEvent> {
        let counted = now.checked_add(Seconds(i64::from(self.until.0)))?;
        let week = counted.week()?.nearest(self.week, ALMANAC_WEEKS)?;
        let days = Seconds(i64::from(self.day) * SECONDS_PER_DAY);
        // GPS time with no offset reads as UTC's calendar does.
        let midnight = GpsSeconds::week_start(week)
            .checked_add(days)?
            .to_utc(Seconds(0))?;
        let event = LeapEvent::at_day_end(midnight, before, self.change)?;
        let apart = event.at.checked_since(counted)?;
        (apart.0.abs() <= LEAP_SLACK).then_some(event)
    }
}

/// `UBX-NAV-TIMELS` (section 32.17.31, pp. 398-400).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct NavTimeLs {
    /// `iTOW`: the epoch.
    pub time_of_week: Milliseconds<u32>,
    /// `currLs`, GPS less UTC; `None` without `validCurrLs`, and when
    /// its source is the firmware's built-in value or unknown.
    pub count: Option<Seconds<i8>>,
    /// The leap second announced or last seen; `None` without
    /// `validTimeToLsEvent`, with an `lsChange` that is not one second
    /// either way, with a day outside 1 to 7, and from a source whose
    /// week and day are not GPS's.
    pub notice: Option<LeapNotice>,
}

impl NavTimeLs {
    /// `None` for a version other than 0.
    fn parse(fields: Fields<'_>) -> Option<Self> {
        if fields.u8(4) != TIMELS_VERSION {
            return None;
        }
        let valid = fields.u8(23);
        let count_known = valid & TIMELS_COUNT_VALID != 0
            && !matches!(fields.u8(8), TIMELS_COUNT_DEFAULT | TIMELS_COUNT_UNKNOWN);
        let change = match fields.i8(11) {
            1 => Some(LeapChange::Insert),
            -1 => Some(LeapChange::Remove),
            _ => None,
        };
        let day = fields.u16(18);
        let dated = valid & TIMELS_EVENT_VALID != 0
            && matches!(fields.u8(10), TIMELS_CHANGE_GPS | TIMELS_CHANGE_GALILEO)
            && DAYS.contains(&day);
        Some(Self {
            time_of_week: fields.time_of_week(),
            count: count_known.then(|| Seconds(fields.i8(9))),
            notice: change.filter(|_| dated).map(|change| LeapNotice {
                change,
                until: Seconds(fields.i32(12)),
                week: fields.u16(16),
                day,
            }),
        })
    }
}

/// `UBX-NAV-DOP` (section 32.17.6, pp. 361-362).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct NavDop {
    /// `iTOW`: the epoch.
    pub time_of_week: Milliseconds<u32>,
    /// `gDOP`, `pDOP`, `hDOP`, `vDOP` and `tDOP`, to the nearest tenth.
    pub dops: Dops,
}

impl NavDop {
    fn parse(fields: Fields<'_>) -> Self {
        Self {
            time_of_week: fields.time_of_week(),
            dops: Dops {
                geometric: tenths(fields.u16(4)),
                position: tenths(fields.u16(6)),
                time: tenths(fields.u16(8)),
                vertical: tenths(fields.u16(10)),
                horizontal: tenths(fields.u16(12)),
            },
        }
    }
}

/// `UBX-NAV-POSECEF` (section 32.17.15, p. 374).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct NavPosEcef {
    /// `iTOW`: the epoch.
    pub time_of_week: Milliseconds<u32>,
    /// `ecefX`, `ecefY` and `ecefZ`, which are centimeters.
    pub position: Ecef,
}

impl NavPosEcef {
    fn parse(fields: Fields<'_>) -> Self {
        Self {
            time_of_week: fields.time_of_week(),
            position: Ecef {
                x: Centimeters(fields.i32(4)),
                y: Centimeters(fields.i32(8)),
                z: Centimeters(fields.i32(12)),
            },
        }
    }
}

/// `UBX-NAV-EOE` (section 32.17.8, p. 363): the epoch's navigation
/// messages have all been sent.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct NavEoe {
    /// `iTOW`: the epoch.
    pub time_of_week: Milliseconds<u32>,
}

/// `NAV-SAT`'s version this crate knows (p. 381).
const SAT_VERSION: u8 = 1;
/// `gnssId` for GPS (appendix A, p. 446).
const SYSTEM_GPS: u8 = 0;
/// `NAV-SAT`'s `flags` (p. 382): `qualityInd` in bits 0 to 2, `svUsed`
/// bit 3, `health` in bits 4 and 5, `ephAvail` bit 11.
const SAT_QUALITY: u32 = 0b111;
const SAT_USED: u32 = 1 << 3;
const SAT_HEALTH_SHIFT: u32 = 4;
const SAT_HEALTH: u32 = 0b11;
const SAT_EPHEMERIS: u32 = 1 << 11;
/// `qualityInd` values (p. 382).
const QUALITY_NO_SIGNAL: u8 = 0;
const QUALITY_SEARCHING: u8 = 1;
const QUALITY_CODE_LOCKED: u8 = 4;
/// `health` values (p. 382).
const HEALTH_HEALTHY: u32 = 1;
const HEALTH_UNHEALTHY: u32 = 2;
/// The elevations of a satellite above the horizon.  `elev` is "unknown
/// if out of range" of 90 degrees either way (p. 381).
const ELEVATIONS: core::ops::RangeInclusive<i8> = 0..=90;

/// One satellite's block of `UBX-NAV-SAT` (pp. 381-383).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct SatelliteInfo {
    /// `gnssId`: which system's it is, 0 for GPS.
    pub system: u8,
    /// `svId`: its number in that system.
    pub id: u8,
    /// `cno`: its signal.
    pub signal: DecibelHertz,
    /// `elev`: its elevation, not known if beyond 90 either way.
    pub elevation: Degrees<i8>,
    /// `azim`: its azimuth, not known if the elevation is not.
    pub azimuth: Degrees<i16>,
    /// `qualityInd`, 0 to 7: 0 no signal, 1 searching, 2 acquired, 3
    /// unusable, 4 code locked, 5 to 7 code and carrier locked.
    pub quality: u8,
    /// `svUsed`: whether the fix uses it.
    pub used: bool,
    /// `health`.
    pub health: Health,
    /// `ephAvail`: whether its ephemeris is held.
    pub ephemeris: bool,
}

impl SatelliteInfo {
    fn parse(fields: Fields<'_>) -> Self {
        let flags = fields.u32(8);
        Self {
            system: fields.u8(0),
            id: fields.u8(1),
            signal: DecibelHertz(fields.u8(2)),
            elevation: Degrees(fields.i8(3)),
            azimuth: Degrees(fields.i16(4)),
            quality: u8::try_from(flags & SAT_QUALITY).unwrap_or(QUALITY_NO_SIGNAL),
            used: flags & SAT_USED != 0,
            health: match (flags >> SAT_HEALTH_SHIFT) & SAT_HEALTH {
                HEALTH_HEALTHY => Health::Healthy,
                HEALTH_UNHEALTHY => Health::Unhealthy,
                _ => Health::Unknown,
            },
            ephemeris: flags & SAT_EPHEMERIS != 0,
        }
    }

    /// This satellite as a record holds one: `None` unless it is a GPS
    /// satellite at a known place above the horizon.
    ///
    /// How far a channel has got with it is this crate's reading of
    /// `qualityInd`, which does not name an Oncore's stages: searching
    /// below 2, code lock at 2 and 3, and from 4, where the receiver
    /// has its time, reading its ephemeris until `ephAvail`, then
    /// ready for the fix.
    #[must_use]
    pub fn satellite(&self) -> Option<Satellite> {
        let prn = Prn::new(self.id).filter(|_| self.system == SYSTEM_GPS)?;
        if !ELEVATIONS.contains(&self.elevation.0) {
            return None;
        }
        let azimuth = u16::try_from(self.azimuth.0.rem_euclid(DEGREES_PER_TURN)).ok()?;
        let mut satellite = Satellite::new(prn, self.elevation, Degrees(azimuth));
        satellite.signal = (self.quality != QUALITY_NO_SIGNAL).then_some(self.signal);
        satellite.tracking = match self.quality {
            QUALITY_NO_SIGNAL | QUALITY_SEARCHING => Tracking::Searching,
            quality if quality < QUALITY_CODE_LOCKED => Tracking::CodeLock,
            _ if self.ephemeris => Tracking::Position,
            _ => Tracking::Ephemeris,
        };
        satellite.used = self.used;
        satellite.health = self.health;
        satellite.ephemeris = self.ephemeris;
        Some(satellite)
    }
}

/// `UBX-NAV-SAT` (section 32.17.20, pp. 381-383).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct NavSat<'a> {
    /// `iTOW`: the epoch.
    pub time_of_week: Milliseconds<u32>,
    /// The satellites' blocks.
    blocks: &'a [u8],
}

impl<'a> NavSat<'a> {
    /// `None` for a version other than 1.
    fn parse(payload: &'a [u8]) -> Option<Self> {
        let fields = Fields(payload);
        Some(Self {
            time_of_week: fields.time_of_week(),
            blocks: payload
                .get(NAV_SAT_HEADER..)
                .filter(|_| fields.u8(4) == SAT_VERSION)?,
        })
    }

    /// The satellites, in the message's order.
    pub fn satellites(&self) -> impl Iterator<Item = SatelliteInfo> + use<'a> {
        self.blocks
            .as_chunks::<NAV_SAT_BLOCK>()
            .0
            .iter()
            .map(|block| SatelliteInfo::parse(Fields(block)))
    }

    /// The GPS satellites with a known place in the sky, as a record
    /// holds them: those the fix uses first, then the rest, each group
    /// in the message's order.  Past a record's room the lowest in the
    /// sky go, as [`Satellites::push`] has it.
    #[must_use]
    pub fn sky(&self) -> Satellites {
        let mut sky = Satellites::EMPTY;
        for used in [true, false] {
            self.satellites()
                .filter(|info| info.used == used)
                .filter_map(|info| info.satellite())
                .for_each(|satellite| sky.push(satellite));
        }
        sky
    }
}

/// `MON-HW`'s `aStatus` values (p. 342).
const ANTENNA_OK: u8 = 2;
const ANTENNA_SHORT: u8 = 3;
const ANTENNA_OPEN: u8 = 4;

/// `UBX-MON-HW` (section 32.16.4, pp. 342-343): what of it a record
/// holds.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct MonHw {
    /// `aStatus`, the antenna supervisor's state: unknown while it
    /// starts up or does not know.
    pub antenna: Antenna,
}

impl MonHw {
    fn parse(fields: Fields<'_>) -> Self {
        Self {
            antenna: match fields.u8(20) {
                ANTENNA_OK => Antenna::Connected,
                ANTENNA_SHORT => Antenna::Shorted,
                ANTENNA_OPEN => Antenna::Open,
                _ => Antenna::Unknown,
            },
        }
    }
}

/// `TIM-TP`'s `flags` bits (p. 433).
const TP_UTC_BASE: u8 = 1 << 0;
const TP_ERROR_INVALID: u8 = 1 << 4;
const TP_NOT_LOCKED: u8 = 1 << 5;

/// A count of picoseconds, `TIM-TP`'s unit for `qErr`.
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct Picoseconds(pub i32);

/// `UBX-TIM-TP` (section 32.20.8, pp. 432-433): the next time pulse.
/// The receiver sends it with the epoch before that pulse (section
/// 19.2, pp. 72-73).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct TimTp {
    /// `towMS`: the pulse's time of week, in the pulse's time base.
    pub time_of_week: Milliseconds<u32>,
    /// `week`: its week, in that time base.
    pub week: u16,
    /// `qErr`, the pulse's quantization error; `None` with
    /// `qErrInvalid` set.  The document does not give its sign.
    pub quantization_error: Option<Picoseconds>,
    /// `timeBase`: whether the time base is UTC, not a satellite
    /// system's time.
    pub utc_base: bool,
    /// Whether the pulse is locked to the satellites: `TpNotLocked`
    /// clear.
    pub locked: bool,
}

impl TimTp {
    fn parse(fields: Fields<'_>) -> Self {
        let flags = fields.u8(14);
        Self {
            time_of_week: Milliseconds(fields.u32(0)),
            week: fields.u16(12),
            quantization_error: (flags & TP_ERROR_INVALID == 0).then(|| Picoseconds(fields.i32(8))),
            utc_base: flags & TP_UTC_BASE != 0,
            locked: flags & TP_NOT_LOCKED == 0,
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

/// What an [`Assembler`] has of the epoch in progress.  Every message
/// in it has the same `iTOW`.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
struct Epoch {
    /// Its `iTOW`; `None` when no epoch is in progress.
    time_of_week: Option<Milliseconds<u32>>,
    /// Its `NAV-PVT`.
    solution: Option<NavPvt>,
    /// Its `NAV-TIMEGPS`.
    time: Option<NavTimeGps>,
    /// Its `NAV-TIMELS`.
    leap: Option<NavTimeLs>,
    /// Its `NAV-DOP`.
    dops: Option<Dops>,
    /// Its `NAV-POSECEF`.
    ecef: Option<Ecef>,
    /// Its `NAV-SAT`.
    satellites: Satellites,
}

impl Epoch {
    /// No epoch in progress.
    const EMPTY: Self = Self {
        time_of_week: None,
        solution: None,
        time: None,
        leap: None,
        dops: None,
        ecef: None,
        satellites: Satellites::EMPTY,
    };
}

/// A leap second an [`Assembler`] was told of, with GPS less UTC until
/// it.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
struct Announced {
    event: LeapEvent,
    before: Seconds<i8>,
}

/// Makes a [`Record`] of each navigation epoch in a stream of UBX bytes
/// ("From messages to a record" in the crate's documentation).  `N`
/// is the longest payload it takes, as a [`Deframer`]'s is:
/// What an [`Assembler`] fills of a record.  It acts on none of an
/// engine's settings: it reads a receiver and sets nothing in it.
pub const FILLER: Filler = Filler {
    name: "u-blox",
    fills: FieldSet::of(&[
        RecordField::Time,
        RecordField::UtcOffset,
        RecordField::Leap,
        RecordField::Fix,
        RecordField::Latitude,
        RecordField::Longitude,
        RecordField::EllipsoidHeight,
        RecordField::MeanSeaLevelHeight,
        RecordField::Speed,
        RecordField::Heading,
        RecordField::GeometricDop,
        RecordField::PositionDop,
        RecordField::HorizontalDop,
        RecordField::VerticalDop,
        RecordField::TimeDop,
        RecordField::Satellites,
        RecordField::SatelliteElevation,
        RecordField::SatelliteAzimuth,
        RecordField::SatelliteSignal,
        RecordField::SatelliteTracking,
        RecordField::SatelliteUsed,
        RecordField::SatelliteHealth,
        RecordField::SatelliteEphemeris,
        RecordField::Antenna,
        RecordField::Ecef,
    ]),
    acts: SettingSet::EMPTY,
};

/// [`nav_sat_length`] of the satellites the receiver may report.
///
/// ```
/// use smartclock_gps::record::Fix;
/// use smartclock_gps::record::Record;
/// use smartclock_gps::source::replay::Assemble;
/// use smartclock_gps_ublox::Assembler;
/// use smartclock_gps_ublox::nav_sat_length;
///
/// let mut assembler = Assembler::<{ nav_sat_length(32) }>::new();
/// let mut record = Record::EMPTY;
/// // NAV-EOE alone: an epoch of a receiver that says nothing else.
/// let stream = [0xb5, 0x62, 0x01, 0x61, 0x04, 0x00, 0x10, 0x27, 0x00, 0x00, 0x9d, 0x7c];
/// let records = stream
///     .iter()
///     .filter(|&&byte| assembler.push(byte, &mut record))
///     .count();
/// assert_eq!(records, 1);
/// assert_eq!(record.fix, Fix::None);
/// ```
#[derive(Debug, Clone)]
pub struct Assembler<const N: usize> {
    deframer: Deframer<N>,
    /// What the frames so far have said.
    assembly: Assembly,
    /// The frames discarded.
    errors: u32,
}

/// What an [`Assembler`] keeps of the messages it has taken: apart from
/// its deframer, which lends the message being taken.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
struct Assembly {
    /// The week to resolve the receiver's near, if it is not trusted.
    reference: Option<GpsWeek>,
    /// The epoch in progress.
    epoch: Epoch,
    /// The antenna's feed, as `MON-HW` last gave it.
    antenna: Antenna,
    /// The leap second last announced.
    announced: Option<Announced>,
}

impl<const N: usize> Default for Assembler<N> {
    fn default() -> Self {
        Self::new()
    }
}

impl<const N: usize> Assembler<N> {
    /// An assembler at a stream's start, which takes the receiver's
    /// week as it gives it.
    #[must_use]
    pub const fn new() -> Self {
        Self::resolving(None)
    }

    /// An assembler that takes only the ten broadcast bits of the
    /// receiver's week, and resolves them to the week nearest
    /// `reference`: for a receiver that dates the stream 1024 weeks
    /// off, as one does whose firmware is older than the rollover it
    /// is run after (section 9.9.1, p. 29).
    #[must_use]
    pub const fn near(reference: GpsWeek) -> Self {
        Self::resolving(Some(reference))
    }

    const fn resolving(reference: Option<GpsWeek>) -> Self {
        Self {
            deframer: Deframer::new(),
            assembly: Assembly {
                reference,
                epoch: Epoch::EMPTY,
                antenna: Antenna::Unknown,
                announced: None,
            },
            errors: 0,
        }
    }

    /// How many frames were discarded for a bad length, a length past
    /// `N` or a bad checksum.  It stops at the count's end.
    #[must_use]
    pub const fn errors(&self) -> u32 {
        self.errors
    }
}

impl Assembly {
    /// Takes a message, and returns whether it ended an epoch, whose
    /// record is then in `record`.
    fn take(&mut self, message: &Message<'_>, record: &mut Record) -> bool {
        let Some(time_of_week) = message.epoch() else {
            if let Message::MonHw(hardware) = message {
                self.antenna = hardware.antenna;
            }
            return false;
        };
        let last = matches!(message, Message::NavEoe(_));
        let other = self
            .epoch
            .time_of_week
            .is_some_and(|open| open != time_of_week);
        if other {
            self.close(record);
        }
        if last {
            if !other {
                self.close(record);
            }
            return true;
        }
        self.epoch.time_of_week = Some(time_of_week);
        match message {
            Message::NavPosEcef(ecef) => self.epoch.ecef = Some(ecef.position),
            Message::NavDop(dop) => self.epoch.dops = Some(dop.dops),
            Message::NavPvt(solution) => self.epoch.solution = Some(*solution),
            Message::NavTimeGps(time) => self.epoch.time = Some(*time),
            Message::NavTimeLs(leap) => self.epoch.leap = Some(*leap),
            Message::NavSat(sat) => self.epoch.satellites = sat.sky(),
            Message::NavEoe(_) | Message::MonHw(_) | Message::TimTp(_) => {}
        }
        other
    }

    /// Ends the epoch in progress and writes its record.
    fn close(&mut self, record: &mut Record) {
        let epoch = mem::replace(&mut self.epoch, Epoch::EMPTY);
        *record = Record::EMPTY;
        record.antenna = self.antenna;
        record.satellites = epoch.satellites;
        if let Some(solution) = epoch.solution.filter(|solution| solution.fix != Fix::None) {
            record.fix = solution.fix;
            record.position = solution.position;
            record.velocity = Some(solution.velocity);
            record.dops = epoch.dops;
            record.derived.ecef = epoch.ecef.filter(|_| solution.position.is_some());
            record.time = epoch
                .time
                .and_then(|time| time.seconds(self.reference))
                .map(|seconds| GpsTime {
                    seconds,
                    subsecond: Nanoseconds(0),
                });
        }
        let now = record.time.map(|time| time.seconds);
        let count = epoch
            .leap
            .and_then(|leap| leap.count)
            .or_else(|| epoch.time.and_then(|time| time.leap_seconds));
        self.note(now, count, epoch.leap);
        (record.utc_offset, record.leap) = match (self.announced, now) {
            (Some(Announced { event, before }), Some(now)) => {
                (Some(event.offset(now, before)), Some(event))
            }
            _ => (count, None),
        };
    }

    /// Keeps what an epoch's `NAV-TIMELS` says of a leap second to
    /// come, `now` being the epoch's second and `count` GPS less UTC in
    /// it.  One announced before and not yet come is dropped when the
    /// receiver no longer announces it.
    fn note(
        &mut self,
        now: Option<GpsSeconds>,
        count: Option<Seconds<i8>>,
        leap: Option<NavTimeLs>,
    ) {
        let (Some(now), Some(leap)) = (now, leap) else {
            return;
        };
        let notice = leap.notice.filter(LeapNotice::pending);
        let withdrawn = self
            .announced
            .is_some_and(|announced| announced.event.pending(now));
        match (notice, count) {
            (Some(notice), Some(before)) => {
                // One announced already stands, with the offset it was
                // announced under: a receiver that steps its count
                // before the leap would otherwise move it a second.
                let stands = withdrawn
                    && self
                        .announced
                        .is_some_and(|announced| announced.event.change == notice.change);
                if let Some(event) = notice.event(now, before).filter(|_| !stands) {
                    self.announced = Some(Announced { event, before });
                }
            }
            (None, _) if withdrawn => self.announced = None,
            _ => {}
        }
    }
}

impl<const N: usize> Assemble for Assembler<N> {
    /// `record` is written only by the byte that ends an epoch, and
    /// then in full.
    fn push(&mut self, byte: u8, record: &mut Record) -> bool {
        match self.deframer.push(byte) {
            None => false,
            Some(Err(_)) => {
                self.errors = self.errors.saturating_add(1);
                false
            }
            Some(Ok(frame)) => {
                Message::parse(frame).is_some_and(|message| self.assembly.take(&message, record))
            }
        }
    }

    fn reset(&mut self) {
        *self = Self::resolving(self.assembly.reference);
    }
}

#[cfg(test)]
extern crate std;

#[cfg(test)]
mod tests;
