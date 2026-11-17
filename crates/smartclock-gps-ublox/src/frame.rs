//! UBX framing: a frame's layout, its checksum, an encoder and a
//! deframer ("Resynchronizing" in the crate's documentation).

use core::fmt;

use thiserror::Error;

use smartclock_gps::NoRoom;
use smartclock_gps::framing::Step;
use smartclock_gps::framing::feed;

use crate::message::LONGEST_PART;
use crate::message::Layout;
use crate::message::payload_length;
use crate::message::streamed_layout;

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
/// length is the caller's to get right: [`MESSAGES`](crate::message::MESSAGES) is not consulted.
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
    /// A message of [`MESSAGES`](crate::message::MESSAGES) with a length that is not its own.
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

/// What the byte a [`Deframer`] last took ended.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum Ended {
    /// A whole frame.
    Frame,
    /// A streamed message's header.
    Header,
    /// A streamed message's block, the one of this index.
    Block(usize),
    /// A streamed message, with a good checksum.
    End,
}

/// Finds frames in a stream of bytes, however it is cut into chunks
/// ("Resynchronizing" in the crate's documentation).  `N` is the
/// longest payload it holds; a longer one is not framed, it is
/// [`FrameError::Overflow`].
#[derive(Debug, Clone)]
pub struct Deframer<const N: usize> {
    state: State,
    /// Whether it gives the messages of
    /// [`STREAMED`](crate::message::STREAMED) a part at a time: a
    /// [`Streamer`]'s.
    streaming: bool,
    /// The message of the frame in progress.
    id: MessageId,
    /// Its payload's length.
    length: usize,
    /// How it is cut, if it is streamed.
    parts: Option<Layout>,
    /// The parts of it given so far.
    given: usize,
    /// The bytes of its payload taken so far.
    taken: usize,
    /// The sum of its bytes so far.
    sum: [u8; CHECKSUM],
    /// Its payload so far, or a streamed one's part so far.
    buffer: [u8; N],
    /// The bytes of `buffer` in use.
    filled: usize,
    /// What the byte last taken ended.
    ended: Ended,
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
        Self::with(false)
    }

    const fn with(streaming: bool) -> Self {
        Self {
            state: State::Idle,
            streaming,
            id: MessageId::new(0, 0),
            length: 0,
            parts: None,
            given: 0,
            taken: 0,
            sum: [0; CHECKSUM],
            buffer: [0; N],
            filled: 0,
            ended: Ended::Frame,
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
    /// use smartclock_gps_ublox::frame::Deframer;
    /// use smartclock_gps_ublox::message::NAV_EOE;
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
                self.taken = 0;
                self.given = 0;
                self.head(byte, self.body());
                let (id, length) = (self.id, self.length);
                self.parts = streamed_layout(id).filter(|_| self.streaming);
                let error = if payload_length(id).is_some_and(|own| !own.allows(length)) {
                    FrameError::BadLength { id, length }
                } else if length > N && self.parts.is_none() {
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
                self.taken += 1;
                self.sum = sum_with(self.sum, byte);
                if self.taken == self.length {
                    self.state = State::ChecksumFirst;
                }
                if let Some(parts) = self.parts {
                    let (part, ended) = match self.given {
                        0 => (parts.header, Ended::Header),
                        given => (parts.block, Ended::Block(given - 1)),
                    };
                    if self.filled == part {
                        self.ended = ended;
                        self.given += 1;
                        self.filled = 0;
                        return Some(Ok(part));
                    }
                }
            }
            State::ChecksumFirst => self.state = State::ChecksumSecond(byte),
            State::ChecksumSecond(first) => {
                self.state = State::Idle;
                let found = [first, byte];
                return Some(if found == self.sum {
                    self.ended = if self.parts.is_some() {
                        Ended::End
                    } else {
                        Ended::Frame
                    };
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

/// A streamed message's header, lent by a [`Streamer`] until its next
/// byte.  Its checksum is not yet known.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Header<'a> {
    /// Which message it is.
    pub id: MessageId,
    /// How many blocks follow, from the payload's length.
    pub blocks: usize,
    /// The header's bytes.
    pub bytes: &'a [u8],
}

/// One block of a streamed message, lent by a [`Streamer`] until its
/// next byte.  Its checksum is not yet known.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Block<'a> {
    /// Which message it is.
    pub id: MessageId,
    /// Which block, from 0.
    pub index: usize,
    /// The block's bytes.
    pub bytes: &'a [u8],
}

/// What a byte a [`Streamer`] takes ends.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Event<'a> {
    /// A whole frame with a good checksum, of a message not streamed.
    Frame(Frame<'a>),
    /// A streamed message's header.
    Header(Header<'a>),
    /// One of its blocks.
    Block(Block<'a>),
    /// Its checksum, which is good: what its header and blocks said
    /// stands.  A bad one is [`FrameError::BadChecksum`] instead, and
    /// they are to be forgotten.
    End(MessageId),
}

/// A [`Deframer`] that gives the messages of
/// [`STREAMED`](crate::message::STREAMED), `NAV-SAT` and `RXM-RAWX`,
/// a header and a block at a time, as each is taken, rather than whole:
/// so its buffer need hold only the longest message not streamed and
/// the longest part ([`LONGEST_PART`]), however many satellites a
/// message reports.  The parts come before the checksum that vouches
/// for them, so a caller keeps what they say aside until
/// [`Event::End`], and drops it at an error ("Streaming" in the crate's
/// documentation).
///
/// ```
/// use smartclock_gps_ublox::frame::Event;
/// use smartclock_gps_ublox::frame::OVERHEAD;
/// use smartclock_gps_ublox::frame::Streamer;
/// use smartclock_gps_ublox::frame::encode;
/// use smartclock_gps_ublox::message::NAV_SAT;
/// use smartclock_gps_ublox::message::Part;
///
/// // NAV-SAT of one GPS satellite, 12 at 40 dB-Hz, used.
/// let payload = [
///     0x10, 0x27, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00, // iTOW, version, numSvs
///     0x00, 0x0c, 0x28, 0x2d, 0x5a, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, // satellite
/// ];
/// let mut stream = [0; OVERHEAD + 20];
/// encode(NAV_SAT, &payload, &mut stream).unwrap();
/// let mut streamer = Streamer::<32>::new();
/// let (mut satellites, mut ended) = (0, false);
/// for &byte in &stream {
///     match streamer.push(byte) {
///         Some(Ok(Event::Block(block))) => {
///             if let Some(Part::Satellite(satellite)) = Part::block(&block) {
///                 assert_eq!(satellite.id, 12);
///                 satellites += 1;
///             }
///         }
///         Some(Ok(Event::End(_))) => ended = true,
///         _ => {}
///     }
/// }
/// assert_eq!((satellites, ended), (1, true));
/// ```
#[derive(Debug, Clone)]
pub struct Streamer<const N: usize>(Deframer<N>);

impl<const N: usize> Default for Streamer<N> {
    fn default() -> Self {
        Self::new()
    }
}

impl<const N: usize> Streamer<N> {
    /// A streamer outside any frame.  `N` must hold [`LONGEST_PART`].
    #[must_use]
    pub const fn new() -> Self {
        const {
            assert!(
                N >= LONGEST_PART,
                "a streamer's buffer holds the longest part"
            )
        };
        Self(Deframer::with(true))
    }

    /// Takes one byte, and returns what it ended, if anything.
    pub fn push(&mut self, byte: u8) -> Option<Result<Event<'_>, FrameError>> {
        let step = self.0.step(byte)?;
        Some(step.map(|length| self.event(length)))
    }

    /// Takes bytes from the front of `chunk` up to the first that ends
    /// something or is an error, as [`Deframer::feed`] does.
    pub fn feed(&mut self, chunk: &[u8]) -> (usize, Option<Result<Event<'_>, FrameError>>) {
        let (taken, ended) = feed(chunk, |byte| self.0.step(byte));
        (
            taken,
            ended.map(|step| step.map(|length| self.event(length))),
        )
    }

    /// Forgets any frame in progress.
    pub const fn reset(&mut self) {
        self.0.reset();
    }

    /// What the step that ended `length` bytes ended.
    fn event(&self, length: usize) -> Event<'_> {
        let deframer = &self.0;
        let bytes = &deframer.buffer[..length.min(N)];
        let id = deframer.id;
        match deframer.ended {
            Ended::Frame => Event::Frame(deframer.frame(length)),
            Ended::Header => Event::Header(Header {
                id,
                blocks: deframer
                    .parts
                    .map_or(0, |parts| (deframer.length - parts.header) / parts.block),
                bytes,
            }),
            Ended::Block(index) => Event::Block(Block { id, index, bytes }),
            Ended::End => Event::End(id),
        }
    }
}
