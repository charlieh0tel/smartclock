//! NMEA sentence framing, and the sentences a `SmartClock` uses.
//!
//! The Z3815A's engine is a Furuno on NMEA sentences, Furuno's own
//! `$PFEC` ones among them (`docs/firmware/gps.md`, "In every image").
//! A sentence is `$`, comma-separated fields, an optional `*` and two
//! hexadecimal digits of checksum, and CR LF; the checksum is the XOR of
//! the bytes between `$` and `*`; a sentence is 82 bytes at most.  That
//! is the format of Furuno's GT-8031 protocol specification
//! (`third_party/furuno/Furuno_GT8031_Protocol_Specification_Rev1.pdf`,
//! pp. 2-3), the nearest to the Z3815A's engine that is at hand.
//!
//! [`SENTENCES`] lists every sentence the Z3815A's firmware sends or
//! decodes, and no other, with how many fields each has.
//!
//! A page cited here is the one the specification prints, which is its
//! PDF's page number less 3.
//!
//! # What is a sentence
//!
//! The specification does not say what a receiver of sentences does with
//! a malformed one, so [`Deframer`] follows the firmware's own framer,
//! `gps_receive` (`third_party/firmware/rewritten/z3815a-4010/gps_nmea.c`),
//! except as noted:
//!
//! - Outside a sentence every byte but `$` is skipped, with no error.
//! - A `$` starts a sentence wherever it comes.  One that cuts a sentence
//!   short is [`SentenceError::Interrupted`]; the firmware counts a frame
//!   error for it only in the sentence's first field.
//! - A field's characters are letters, digits, punctuation and space, but
//!   none of `$*,!\^~`.  The first field cannot be empty.
//! - The checksum is optional: CR may follow the fields directly.  If
//!   there is one it is two digits, and `a` to `f` are not digits.
//! - A sentence is 82 bytes at most, `$` through LF.  A byte more is
//!   [`SentenceError::Overflow`].
//! - Any other byte out of place is [`SentenceError::BadCharacter`], and a
//!   wrong checksum [`SentenceError::BadChecksum`], reported at the LF.
//!   After either, and after an overflow, the next `$` is waited for.

use core::fmt;

use thiserror::Error;

use crate::Direction;
use crate::NoRoom;

/// A sentence's first byte.
const START: u8 = b'$';
/// What separates a sentence's fields.
const SEPARATOR: u8 = b',';
/// What comes between a sentence's fields and its checksum.
const CHECKSUM_MARK: u8 = b'*';
/// A sentence's last two bytes.
const END: [u8; 2] = *b"\r\n";
/// Digits in a checksum.
const CHECKSUM_DIGITS: usize = 2;
/// A checksum's digits by value: hexadecimal, upper case.
const DIGITS: &[u8; 16] = b"0123456789ABCDEF";
/// The radix of a checksum's digits.
const RADIX: u8 = 16;
/// Printing characters a field cannot hold.
const RESERVED: &[u8] = b"$*,!\\^~";
/// The talker of every sentence a `SmartClock` uses, which starts the
/// field that names the sentence.
const TALKER: &[u8] = b"GP";
/// The first field of a Furuno proprietary sentence, before the one that
/// names it (specification p. 3).
const PROPRIETARY: &[u8] = b"PFEC";

/// The longest sentence, `$` through LF (specification p. 2; the room of
/// the firmware's framer).
pub const MAX_SENTENCE: usize = 82;

/// A sentence's checksum: the XOR of its body, the bytes between `$` and
/// `*`.
#[must_use]
pub fn checksum(body: &[u8]) -> u8 {
    body.iter().fold(0, |sum, byte| sum ^ byte)
}

/// Whether a field may hold this byte.
fn is_field_character(byte: u8) -> bool {
    (byte.is_ascii_graphic() || byte == b' ') && !RESERVED.contains(&byte)
}

/// A checksum digit's value, `None` for a byte that is not one.
fn digit_value(byte: u8) -> Option<u8> {
    DIGITS
        .iter()
        .position(|&digit| digit == byte)
        .and_then(|value| u8::try_from(value).ok())
}

/// Whether an encoded sentence carries a checksum.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Checksum {
    /// CR LF directly after the fields, as the Z3815A's firmware sends
    /// its sentences (`gps_request_build`) and the engine all of its own
    /// but `RMC` (specification p. 25).
    Omitted,
    /// `*` and two digits before CR LF.
    Appended,
}

impl Checksum {
    /// The bytes it adds to a sentence.
    const fn length(self) -> usize {
        match self {
            Self::Omitted => 0,
            Self::Appended => size_of::<u8>() + CHECKSUM_DIGITS,
        }
    }
}

/// Why [`encode`] refused a sentence.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Error)]
pub enum EncodeError {
    /// The output buffer is shorter than the sentence.
    #[error(transparent)]
    NoRoom(#[from] NoRoom),
    /// The sentence would be longer than [`MAX_SENTENCE`].
    #[error("a sentence of {length} bytes is over the limit of {limit}", limit = MAX_SENTENCE)]
    TooLong {
        /// The length it would be.
        length: usize,
    },
    /// The body's first field is empty.
    #[error("a sentence's first field is empty")]
    NoAddress,
    /// The body holds a byte no field can.
    #[error("byte {0:#04x} cannot be in a sentence's field")]
    BadCharacter(u8),
}

/// Writes a sentence into `out` and returns its length.  `body` is what
/// goes between `$` and the checksum or CR: the fields with their commas,
/// `PFEC,GPsrq` for `$PFEC,GPsrq`.
///
/// # Errors
///
/// [`EncodeError`] for a body no [`Deframer`] would take back, or a
/// buffer too short.
pub fn encode(body: &[u8], check: Checksum, out: &mut [u8]) -> Result<usize, EncodeError> {
    if !body.first().is_some_and(|&byte| is_field_character(byte)) {
        return Err(EncodeError::NoAddress);
    }
    if let Some(&byte) = body
        .iter()
        .find(|&&byte| !is_field_character(byte) && byte != SEPARATOR)
    {
        return Err(EncodeError::BadCharacter(byte));
    }
    let needed = size_of::<u8>() + body.len() + check.length() + END.len();
    if needed > MAX_SENTENCE {
        return Err(EncodeError::TooLong { length: needed });
    }
    let room = out.len();
    let sentence = out.get_mut(..needed).ok_or(NoRoom { needed, room })?;
    let (start, rest) = sentence.split_at_mut(size_of::<u8>());
    let (text, rest) = rest.split_at_mut(body.len());
    let (check_text, end) = rest.split_at_mut(check.length());
    start.fill(START);
    text.copy_from_slice(body);
    if let [mark, high, low] = check_text {
        let sum = checksum(body);
        *mark = CHECKSUM_MARK;
        *high = DIGITS[usize::from(sum / RADIX)];
        *low = DIGITS[usize::from(sum % RADIX)];
    }
    end.copy_from_slice(&END);
    Ok(needed)
}

/// Whether a sentence is one of NMEA's own or a maker's.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Class {
    /// One of NMEA's: `$GPGGA,...`.
    Approved,
    /// Furuno's: `$PFEC,GPtps,...`.
    Proprietary,
}

impl Class {
    /// The fields of a sentence's address: the one that names it, and
    /// `PFEC` before it for a proprietary one.
    const fn address_fields(self) -> usize {
        match self {
            Self::Approved => 1,
            Self::Proprietary => 2,
        }
    }
}

/// A sentence's address: what names it, read as the firmware reads it
/// (`nmea_sentence_address`).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Address<'a> {
    /// Whether `PFEC` came first.
    pub class: Class,
    /// The sentence's name, the rest of the field after `GP`: `GGA`,
    /// `tps`.
    pub name: &'a [u8],
}

/// A whole sentence, its checksum good if it has one, lent by a
/// [`Deframer`] until its next byte.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Sentence<'a> {
    /// The sentence, `$` through LF.
    bytes: &'a [u8],
}

impl<'a> Sentence<'a> {
    /// Where its body ends: at its `*`, or else its CR.  No field holds
    /// either.
    fn body_end(&self) -> usize {
        self.bytes
            .iter()
            .position(|&byte| byte == CHECKSUM_MARK || byte == END[0])
            .unwrap_or(self.bytes.len())
    }

    /// The whole sentence, `$` through LF.
    #[must_use]
    pub fn bytes(&self) -> &'a [u8] {
        self.bytes
    }

    /// Its body, the fields with their commas: what the checksum covers.
    #[must_use]
    pub fn body(&self) -> &'a [u8] {
        &self.bytes[size_of::<u8>()..self.body_end()]
    }

    /// The checksum it carried, `None` if it had none.
    #[must_use]
    pub fn checksum(&self) -> Option<u8> {
        let (_, after) = self.bytes.split_at(self.body_end());
        let digits = after
            .strip_prefix(&[CHECKSUM_MARK])?
            .get(..CHECKSUM_DIGITS)?;
        digits
            .iter()
            .try_fold(0, |sum, &digit| Some(sum * RADIX + digit_value(digit)?))
    }

    /// Its fields, the address's first.
    pub fn fields(&self) -> impl Iterator<Item = &'a [u8]> + use<'a> {
        self.body().split(|&byte| byte == SEPARATOR)
    }

    /// Its address, `None` if it does not name a `GP` sentence.
    #[must_use]
    pub fn address(&self) -> Option<Address<'a>> {
        let mut fields = self.fields();
        let first = fields.next()?;
        let (class, field) = if first == PROPRIETARY {
            (Class::Proprietary, fields.next()?)
        } else {
            (Class::Approved, first)
        };
        let name = field.strip_prefix(TALKER).filter(|name| !name.is_empty())?;
        Some(Address { class, name })
    }

    /// Its data: the fields after its address, the ones [`Fields`] counts.
    /// The whole of a sentence without an address.
    pub fn data(&self) -> impl Iterator<Item = &'a [u8]> + use<'a> {
        let address = self
            .address()
            .map_or(0, |address| address.class.address_fields());
        self.fields().skip(address)
    }
}

/// Why a [`Deframer`] discarded what it was reading.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Error)]
pub enum SentenceError {
    /// A `$` came before the sentence's end, and starts another.
    #[error("a $ cut a sentence short")]
    Interrupted,
    /// A byte where it cannot be: one no field can hold, a comma, `*` or
    /// CR straight after `$`, a checksum of other than two upper-case
    /// hexadecimal digits, or a CR without its LF.
    #[error("byte {0:#04x} is out of place in a sentence")]
    BadCharacter(u8),
    /// A sentence longer than [`MAX_SENTENCE`].
    #[error("a sentence is over the limit of {limit} bytes", limit = MAX_SENTENCE)]
    Overflow,
    /// A sentence whose checksum is not the XOR of its body.
    #[error("a sentence has checksum {found:#04x}, not {expected:#04x}")]
    BadChecksum {
        /// The XOR of its body.
        expected: u8,
        /// The checksum it carried.
        found: u8,
    },
}

/// Where a [`Deframer`] is in a sentence.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum State {
    /// Outside a sentence.
    Idle,
    /// In the fields.
    Body,
    /// After `*` and this many checksum digits.
    Checksum { digits: usize },
    /// After CR: LF is next.
    LineFeed,
}

/// What one byte did to a [`Deframer`]: nothing yet, ended a sentence of
/// some length, or an error.  [`Sentence`] borrows the buffer, so the
/// step is taken first and the sentence made from it after.
type Step = Option<Result<usize, SentenceError>>;

/// Finds sentences in a stream of bytes, however it is cut into chunks
/// ("What is a sentence" in the module's documentation).
pub struct Deframer {
    state: State,
    /// The sentence so far, from its `$`.
    buffer: [u8; MAX_SENTENCE],
    /// The bytes of `buffer` in use.
    filled: usize,
}

impl fmt::Debug for Deframer {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("Deframer")
            .field("state", &self.state)
            .field("buffer", &&self.buffer[..self.filled])
            .finish()
    }
}

impl Default for Deframer {
    fn default() -> Self {
        Self::new()
    }
}

impl Deframer {
    /// A deframer outside any sentence.
    #[must_use]
    pub const fn new() -> Self {
        Self {
            state: State::Idle,
            buffer: [0; MAX_SENTENCE],
            filled: 0,
        }
    }

    /// Takes one byte, and returns a sentence or an error if the byte
    /// ended one.
    pub fn push(&mut self, byte: u8) -> Option<Result<Sentence<'_>, SentenceError>> {
        let step = self.step(byte)?;
        Some(step.map(|length| self.sentence(length)))
    }

    /// Takes bytes from the front of `chunk` up to the first that ends a
    /// sentence or is an error, and returns how many it took and what
    /// that one ended.  With bytes left over, call again with the rest:
    ///
    /// ```
    /// use smartclock_gps::nmea::Deframer;
    ///
    /// let mut deframer = Deframer::new();
    /// let mut rest: &[u8] = b"noise$PFEC,GPsrq\r\n$PFEC,GPint,tst00\r\n";
    /// let mut sentences = 0;
    /// while !rest.is_empty() {
    ///     let (taken, event) = deframer.feed(rest);
    ///     rest = &rest[taken..];
    ///     if let Some(Ok(_sentence)) = event {
    ///         sentences += 1;
    ///     }
    /// }
    /// assert_eq!(sentences, 2);
    /// ```
    pub fn feed(&mut self, chunk: &[u8]) -> (usize, Option<Result<Sentence<'_>, SentenceError>>) {
        let ended = chunk
            .iter()
            .enumerate()
            .find_map(|(index, &byte)| self.step(byte).map(|step| (index + 1, step)));
        match ended {
            Some((taken, step)) => (taken, Some(step.map(|length| self.sentence(length)))),
            None => (chunk.len(), None),
        }
    }

    /// Forgets any sentence in progress.
    pub fn reset(&mut self) {
        self.state = State::Idle;
    }

    fn sentence(&self, length: usize) -> Sentence<'_> {
        Sentence {
            bytes: &self.buffer[..length],
        }
    }

    fn store(&mut self, byte: u8) {
        self.buffer[self.filled] = byte;
        self.filled += 1;
    }

    fn step(&mut self, byte: u8) -> Step {
        let state = self.state;
        if byte == START {
            self.filled = 0;
            self.store(byte);
            self.state = State::Body;
            return (state != State::Idle).then_some(Err(SentenceError::Interrupted));
        }
        if state == State::Idle {
            return None;
        }
        self.state = State::Idle;
        if self.filled == MAX_SENTENCE {
            return Some(Err(SentenceError::Overflow));
        }
        let addressed = self.filled > size_of::<u8>();
        let next = match (state, byte) {
            (State::Body, _) if is_field_character(byte) => State::Body,
            (State::Body, SEPARATOR) if addressed => State::Body,
            (State::Body, CHECKSUM_MARK) if addressed => State::Checksum { digits: 0 },
            (State::Body, _) if addressed && byte == END[0] => State::LineFeed,
            (State::Checksum { digits }, _)
                if digits < CHECKSUM_DIGITS && digit_value(byte).is_some() =>
            {
                State::Checksum { digits: digits + 1 }
            }
            (
                State::Checksum {
                    digits: CHECKSUM_DIGITS,
                },
                _,
            ) if byte == END[0] => State::LineFeed,
            (State::LineFeed, _) if byte == END[1] => {
                self.store(byte);
                let sentence = self.sentence(self.filled);
                let expected = checksum(sentence.body());
                return Some(match sentence.checksum() {
                    Some(found) if found != expected => {
                        Err(SentenceError::BadChecksum { expected, found })
                    }
                    _ => Ok(self.filled),
                });
            }
            _ => return Some(Err(SentenceError::BadCharacter(byte))),
        };
        self.store(byte);
        self.state = next;
        None
    }
}

/// How many data fields a sentence has: the fields after its address, an
/// empty one counted.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Fields {
    /// The fewest.
    pub least: usize,
    /// The most.
    pub most: usize,
}

impl Fields {
    const fn exactly(count: usize) -> Self {
        Self {
            least: count,
            most: count,
        }
    }

    const fn between(least: usize, most: usize) -> Self {
        Self { least, most }
    }

    /// Whether a sentence may have this many.
    #[must_use]
    pub const fn allows(&self, count: usize) -> bool {
        self.least <= count && count <= self.most
    }
}

/// One sentence: a row of [`SENTENCES`].
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct SentenceType {
    /// Its name, what follows `GP` in its address.
    pub name: &'static str,
    /// Whether its address starts with `PFEC`.
    pub class: Class,
    /// Which way the Z3815A uses it.
    pub direction: Direction,
    /// How many data fields it has.
    pub fields: Fields,
}

impl SentenceType {
    /// A sentence of this name, its class by the name's case: the
    /// firmware puts `PFEC` before a lower-case one (`gps_request_build`).
    const fn new(name: &'static str, direction: Direction, fields: Fields) -> Self {
        let class = if name.as_bytes()[0].is_ascii_lowercase() {
            Class::Proprietary
        } else {
            Class::Approved
        };
        Self {
            name,
            class,
            direction,
            fields,
        }
    }

    /// A sentence the firmware sends its engine.
    const fn sent(name: &'static str, fields: Fields) -> Self {
        Self::new(name, Direction::ToEngine, fields)
    }

    /// A sentence the firmware decodes.
    const fn decoded(name: &'static str, fields: Fields) -> Self {
        Self::new(name, Direction::FromEngine, fields)
    }
}

/// Every sentence the Z3815A's firmware sends its engine or decodes from
/// it.
///
/// Each row cites the page of Furuno's GT-8031 protocol specification
/// (`third_party/furuno/Furuno_GT8031_Protocol_Specification_Rev1.pdf`,
/// by printed page) that gives its fields.  That the Z3815A uses them is
/// `docs/firmware/gps.md`, "In every image": it sends the first eight,
/// built by `gps_request_build`, and decodes the rest by its table
/// `nmea_sentences`
/// (`third_party/firmware/rewritten/z3815a-4010/gps_nmea.c`).  Tests
/// check the names, and the counts of the ones it decodes, against the
/// image's tables.
pub const SENTENCES: &[SentenceType] = &[
    // p. 10.
    SentenceType::sent("clr", Fields::exactly(1)),
    // p. 11: up to eight parameters.  The firmware sends one a sentence.
    SentenceType::sent("set", Fields::between(1, 8)),
    // p. 12.
    SentenceType::sent("srq", Fields::exactly(0)),
    // p. 13: up to eleven sentence names and intervals.  The firmware
    // sends one a sentence.
    SentenceType::sent("int", Fields::between(1, 11)),
    // p. 15.
    SentenceType::sent("rrs", Fields::exactly(3)),
    // p. 15.
    SentenceType::sent("rrq", Fields::exactly(0)),
    // p. 8.
    SentenceType::sent("ZDA", Fields::exactly(6)),
    // p. 6: seven fields, the last three null and ignored; p. 2's example
    // has the first four alone.
    SentenceType::sent("GLL", Fields::between(4, 7)),
    // p. 19.
    SentenceType::decoded("GGA", Fields::exactly(14)),
    // p. 22.
    SentenceType::decoded("GSA", Fields::exactly(17)),
    // p. 23: three fields, and four more for each of up to four
    // satellites.
    SentenceType::decoded("GSV", Fields::between(3, 19)),
    // p. 25: twelve fields and a checksum.  The firmware's table takes
    // one field fewer with a checksum, or twelve without.
    SentenceType::decoded("RMC", Fields::between(11, 12)),
    // p. 27.
    SentenceType::decoded("anc", Fields::exactly(2)),
    // p. 31.
    SentenceType::decoded("tst", Fields::exactly(4)),
    // p. 32, which gives no count: one field for each parameter set.  The
    // count is the one the firmware's table takes.
    SentenceType::decoded("ssd", Fields::exactly(13)),
    // p. 36.
    SentenceType::decoded("tps", Fields::exactly(10)),
    // p. 40.
    SentenceType::decoded("rrm", Fields::exactly(8)),
    // p. 40.
    SentenceType::decoded("rsd", Fields::exactly(3)),
];

/// The row of [`SENTENCES`] for a name, as an [`Address`] gives it.
#[must_use]
pub fn sentence_type(name: &[u8]) -> Option<&'static SentenceType> {
    SENTENCES
        .iter()
        .find(|sentence| sentence.name.as_bytes() == name)
}

#[cfg(test)]
mod tests {
    use std::collections::BTreeMap;
    use std::collections::BTreeSet;
    use std::fs;
    use std::ops::Range;
    use std::path::Path;
    use std::string::String;
    use std::string::ToString;
    use std::vec::Vec;

    use super::Address;
    use super::Checksum;
    use super::Class;
    use super::Deframer;
    use super::EncodeError;
    use super::MAX_SENTENCE;
    use super::SENTENCES;
    use super::SentenceError;
    use super::checksum;
    use super::encode;
    use super::sentence_type;
    use crate::Direction;
    use crate::NoRoom;

    /// What a deframer found, owned: a sentence's bytes, or an error.
    type Event = Result<Vec<u8>, SentenceError>;

    /// The specification's example of `tps`, p. 36.
    const TPS: &[u8] =
        b"$PFEC,GPtps,940630123000,2,0,1,000000000000,00,00,000000000000,0755,390600\r\n";

    /// The Z3815A's image, from the crate's directory.
    const IMAGE: &str = "../../third_party/firmware/images/z3815a-4010.bin";
    /// The image's table of the sentences it decodes, `nmea_sentences`,
    /// and its entry, `nmea_sentence_entry`: a pointer to the name, then
    /// two signed bytes, the second the count of fields
    /// (`third_party/firmware/rewritten/z3815a-4010/types/smartclock.h`).
    const DECODED: usize = 0x5e32a;
    const DECODED_ENTRY: usize = 10;
    const DECODED_FIELDS: usize = 5;
    /// The image's table of the sentences it sends,
    /// `gps_sentence_descriptors`, and its entry,
    /// `gps_sentence_descriptor`: pointers to the address and to the
    /// query's.
    const SENT: Range<usize> = 0x5e3cc..0x5e64c;
    const SENT_ENTRY: usize = 0x20;
    const SENT_QUERY: usize = 0x16;
    /// The address of the descriptors that only query, which is never
    /// sent.
    const PLACEHOLDER: &str = "zzz";
    /// Characters in a sentence's name.
    const NAME: usize = 3;

    /// A sentence of `body`.
    fn framed(body: &[u8], check: Checksum) -> Vec<u8> {
        let mut out = [0; MAX_SENTENCE];
        let length = encode(body, check, &mut out).expect("a sentence");
        out[..length].to_vec()
    }

    /// Everything a deframer finds in the chunks, in order.
    fn deframe(chunks: &[&[u8]]) -> Vec<Event> {
        let mut deframer = Deframer::new();
        let mut events = Vec::new();
        for chunk in chunks {
            let mut rest = *chunk;
            while !rest.is_empty() {
                let (taken, event) = deframer.feed(rest);
                events.extend(event.map(|event| event.map(|s| s.bytes().to_vec())));
                rest = &rest[taken..];
            }
        }
        events
    }

    /// The one sentence in a stream.
    fn only(stream: &[u8]) -> Vec<u8> {
        let events: [Event; 1] = deframe(&[stream]).try_into().expect("one event");
        let [Ok(sentence)] = events else {
            panic!("not a sentence");
        };
        sentence
    }

    /// The specification's example of a checksum, p. 17:
    /// `$PFEC,GPmge,7BFFF,*5B`, whose 5B is the sum without the comma it
    /// prints before the `*`.
    #[test]
    fn checksum_is_the_specifications_example() {
        assert_eq!(checksum(b"PFEC,GPmge,7BFFF"), 0x5b);
    }

    /// Sentences as the Z3815A's firmware sends them, without a checksum:
    /// the specification's example of `int` (p. 13) and the two queries.
    #[test]
    fn encodes_sentences_the_firmware_sends() {
        for text in [
            "$PFEC,GPint,tst00\r\n",
            "$PFEC,GPsrq\r\n",
            "$PFEC,GPrrq\r\n",
        ] {
            let body = text.trim_start_matches('$').trim_end();
            let bytes = framed(body.as_bytes(), Checksum::Omitted);
            assert_eq!(bytes, text.as_bytes());
            assert_eq!(deframe(&[&bytes]), [Ok(bytes)]);
        }
    }

    #[test]
    fn encodes_a_checksum_in_upper_case() {
        let bytes = framed(b"PFEC,GPmge,7BFFF", Checksum::Appended);
        assert_eq!(bytes, b"$PFEC,GPmge,7BFFF*5B\r\n");
        assert_eq!(deframe(&[&bytes]), [Ok(bytes)]);
    }

    #[test]
    fn encode_refuses_what_no_deframer_takes() {
        let mut out = [0; MAX_SENTENCE];
        assert_eq!(
            encode(b"", Checksum::Omitted, &mut out),
            Err(EncodeError::NoAddress)
        );
        assert_eq!(
            encode(b",GPsrq", Checksum::Omitted, &mut out),
            Err(EncodeError::NoAddress)
        );
        for reserved in *b"$*!\\^~\r\n\x7f" {
            assert_eq!(
                encode(&[b'P', reserved], Checksum::Omitted, &mut out),
                Err(EncodeError::BadCharacter(reserved))
            );
        }
        let longest = [b'A'; MAX_SENTENCE - 3];
        assert_eq!(
            encode(&longest, Checksum::Omitted, &mut out),
            Ok(MAX_SENTENCE)
        );
        assert_eq!(
            encode(&longest, Checksum::Appended, &mut out),
            Err(EncodeError::TooLong {
                length: MAX_SENTENCE + 3
            })
        );
        assert_eq!(
            encode(b"GPZDA", Checksum::Omitted, &mut out[..7]),
            Err(EncodeError::NoRoom(NoRoom { needed: 8, room: 7 }))
        );
    }

    /// A sentence's parts, of the specification's `tps`.
    #[test]
    fn a_sentence_gives_its_parts() {
        let mut deframer = Deframer::new();
        let (taken, event) = deframer.feed(TPS);
        assert_eq!(taken, TPS.len());
        let sentence = event.expect("an event").expect("a sentence");
        assert_eq!(sentence.bytes(), TPS);
        assert_eq!(sentence.body(), &TPS[1..TPS.len() - 2]);
        assert_eq!(sentence.checksum(), None);
        assert_eq!(sentence.fields().count(), 12);
        let address = sentence.address().expect("an address");
        assert_eq!(
            address,
            Address {
                class: Class::Proprietary,
                name: b"tps"
            }
        );
        let row = sentence_type(address.name).expect("a row");
        assert!(row.fields.allows(sentence.data().count()));
        assert_eq!(sentence.data().next(), Some(b"940630123000".as_slice()));
        assert_eq!(sentence.data().last(), Some(b"390600".as_slice()));
    }

    #[test]
    fn an_address_is_a_gp_name_after_an_optional_pfec() {
        let address = |stream: &[u8]| {
            let mut deframer = Deframer::new();
            let (_, event) = deframer.feed(stream);
            let sentence = event.expect("an event").expect("a sentence");
            sentence
                .address()
                .map(|a| (a.class, a.name.to_vec(), sentence.data().count()))
        };
        assert_eq!(
            address(b"$GPZDA,1,2\r\n"),
            Some((Class::Approved, b"ZDA".to_vec(), 2))
        );
        assert_eq!(
            address(b"$PFEC,GPrrq\r\n"),
            Some((Class::Proprietary, b"rrq".to_vec(), 0))
        );
        assert_eq!(
            address(b"$GPGSV,\r\n"),
            Some((Class::Approved, b"GSV".to_vec(), 1))
        );
        assert_eq!(address(b"$PFEC\r\n"), None);
        assert_eq!(address(b"$GP,1\r\n"), None);
        assert_eq!(address(b"$GLGSV,1\r\n"), None);
    }

    /// A checksum is optional, checked if there, and of two upper-case
    /// digits.
    #[test]
    fn a_checksum_is_checked_if_there() {
        let mut deframer = Deframer::new();
        let (_, event) = deframer.feed(b"$GPRMC,L*2B\r\n");
        assert_eq!(
            event.expect("an event").expect("a sentence").checksum(),
            Some(0x2b)
        );
        assert_eq!(
            deframe(&[b"$GPRMC,L*2C\r\n"]),
            [Err(SentenceError::BadChecksum {
                expected: 0x2b,
                found: 0x2c
            })]
        );
        assert_eq!(
            deframe(&[b"$GPRMC,L*2b\r\n"]),
            [Err(SentenceError::BadCharacter(b'b'))]
        );
        assert_eq!(
            deframe(&[b"$GPRMC,L*2\r\n"]),
            [Err(SentenceError::BadCharacter(b'\r'))]
        );
        assert_eq!(
            deframe(&[b"$GPRMC,L*\r\n"]),
            [Err(SentenceError::BadCharacter(b'\r'))]
        );
        assert_eq!(
            deframe(&[b"$GPRMC,L*2B0\r\n"]),
            [Err(SentenceError::BadCharacter(b'0'))]
        );
    }

    #[test]
    fn a_byte_out_of_place_waits_for_the_next_start() {
        let good = framed(b"PFEC,GPsrq", Checksum::Omitted);
        for (bad, byte) in [
            (b"$,GPsrq\r\n".as_slice(), b','),
            (b"$\r\n", b'\r'),
            (b"$*00\r\n", b'*'),
            (b"$GP!\r\n", b'!'),
            (b"$GP\x00\r\n", 0),
            (b"$GPZDA\n", b'\n'),
            (b"$GPZDA\r\r\n", b'\r'),
            (b"$GPZDA\rX\n", b'X'),
        ] {
            let stream = [bad, &good].concat();
            assert_eq!(
                deframe(&[&stream]),
                [Err(SentenceError::BadCharacter(byte)), Ok(good.clone())],
                "{}",
                bad.escape_ascii()
            );
        }
    }

    #[test]
    fn noise_outside_a_sentence_is_skipped_silently() {
        let good = framed(b"PFEC,GPsrq", Checksum::Omitted);
        let stream = [b"\r\n@@Cf%\r\n,*00 noise".as_slice(), &good, b"more\r\n"].concat();
        assert_eq!(deframe(&[&stream]), [Ok(good)]);
    }

    /// A `$` starts a sentence wherever it comes, and reports the one it
    /// cut short.
    #[test]
    fn a_start_inside_a_sentence_starts_another() {
        let good = framed(b"PFEC,GPsrq", Checksum::Omitted);
        for cut in [
            b"$".as_slice(),
            b"$GP",
            b"$GPZDA,1",
            b"$GPRMC,L*2",
            b"$GPRMC,L*2B\r",
        ] {
            let stream = [cut, &good].concat();
            assert_eq!(
                deframe(&[&stream]),
                [Err(SentenceError::Interrupted), Ok(good.clone())],
                "{}",
                cut.escape_ascii()
            );
        }
    }

    /// A sentence of 82 bytes is framed, with or without a checksum, and
    /// one of 83 is not.
    #[test]
    fn a_sentence_is_82_bytes_at_most() {
        let longest = framed(&[b'A'; MAX_SENTENCE - 3], Checksum::Omitted);
        assert_eq!(longest.len(), MAX_SENTENCE);
        assert_eq!(only(&longest), longest);
        let checked = framed(&[b'A'; MAX_SENTENCE - 6], Checksum::Appended);
        assert_eq!(checked.len(), MAX_SENTENCE);
        assert_eq!(only(&checked), checked);

        let good = framed(b"PFEC,GPsrq", Checksum::Omitted);
        let over = [b"$".as_slice(), &[b'A'; MAX_SENTENCE - 2], b"\r\n", &good].concat();
        assert_eq!(
            deframe(&[&over]),
            [Err(SentenceError::Overflow), Ok(good.clone())]
        );
        let run = [
            [b'A'; 3 * MAX_SENTENCE].as_slice(),
            b"$",
            &[b'A'; 3 * MAX_SENTENCE],
            &good,
        ]
        .concat();
        assert_eq!(deframe(&[&run]), [Err(SentenceError::Overflow), Ok(good)]);
    }

    /// A stream of sentences, noise and errors gives the same events cut
    /// in two at any byte, and fed a byte at a time.
    #[test]
    fn any_split_gives_the_same_events() {
        let srq = framed(b"PFEC,GPsrq", Checksum::Omitted);
        let rmc = framed(b"GPRMC,123456,A,,,,,,,020102,,", Checksum::Appended);
        let stream = [
            b"noise\r\n".as_slice(),
            &srq,
            TPS,
            b"$GPGGA,1$GPRMC,L*2C\r\n",
            &rmc,
            b"$GP!",
            &srq,
        ]
        .concat();
        let whole = deframe(&[&stream]);
        assert_eq!(
            whole,
            [
                Ok(srq.clone()),
                Ok(TPS.to_vec()),
                Err(SentenceError::Interrupted),
                Err(SentenceError::BadChecksum {
                    expected: 0x2b,
                    found: 0x2c
                }),
                Ok(rmc),
                Err(SentenceError::BadCharacter(b'!')),
                Ok(srq),
            ]
        );
        for cut in 0..=stream.len() {
            let (head, tail) = stream.split_at(cut);
            assert_eq!(deframe(&[head, tail]), whole, "cut {cut}");
        }
        let singly: Vec<&[u8]> = stream.chunks(1).collect();
        assert_eq!(deframe(&singly), whole);
    }

    #[test]
    fn reset_forgets_a_sentence_in_progress() {
        let mut deframer = Deframer::default();
        assert!(deframer.push(b'$').is_none());
        assert!(deframer.push(b'G').is_none());
        deframer.reset();
        assert!(
            b"PZDA\r\n"
                .iter()
                .all(|&byte| deframer.push(byte).is_none())
        );
    }

    #[test]
    fn errors_display() {
        assert_eq!(
            SentenceError::BadChecksum {
                expected: 0x2b,
                found: 0x2c
            }
            .to_string(),
            "a sentence has checksum 0x2c, not 0x2b"
        );
        assert_eq!(
            SentenceError::Overflow.to_string(),
            "a sentence is over the limit of 82 bytes"
        );
        assert_eq!(
            EncodeError::TooLong { length: 85 }.to_string(),
            "a sentence of 85 bytes is over the limit of 82"
        );
        assert_eq!(
            EncodeError::NoRoom(NoRoom { needed: 8, room: 7 }).to_string(),
            "a frame of 8 bytes does not fit in 7"
        );
    }

    /// The table is well formed: each name once, of three characters all
    /// of the one case its class goes by, and a sentence of each row's
    /// most fields, each empty, fits.
    #[test]
    fn the_table_is_consistent() {
        let names: BTreeSet<&str> = SENTENCES.iter().map(|row| row.name).collect();
        assert_eq!(names.len(), SENTENCES.len(), "a name twice");
        for row in SENTENCES {
            assert_eq!(row.name.len(), NAME, "{}", row.name);
            let lower_case = row.name.bytes().all(|byte| byte.is_ascii_lowercase());
            let upper_case = row.name.bytes().all(|byte| byte.is_ascii_uppercase());
            assert_eq!(lower_case, row.class == Class::Proprietary, "{}", row.name);
            assert_eq!(upper_case, row.class == Class::Approved, "{}", row.name);
            assert!(row.fields.least <= row.fields.most, "{}", row.name);
            assert_eq!(sentence_type(row.name.as_bytes()), Some(row));

            let prefix = match row.class {
                Class::Approved => "GP",
                Class::Proprietary => "PFEC,GP",
            };
            let body = [prefix, row.name, &",".repeat(row.fields.most)].concat();
            let bytes = framed(body.as_bytes(), Checksum::Appended);
            let mut deframer = Deframer::new();
            let (_, event) = deframer.feed(&bytes);
            let sentence = event.expect("an event").expect("a sentence");
            let address = sentence.address().expect("an address");
            assert_eq!(
                address,
                Address {
                    class: row.class,
                    name: row.name.as_bytes()
                }
            );
            assert_eq!(sentence.data().count(), row.fields.most, "{}", row.name);
        }
        assert_eq!(sentence_type(b"VTG"), None);
    }

    fn image() -> Vec<u8> {
        fs::read(Path::new(env!("CARGO_MANIFEST_DIR")).join(IMAGE)).expect("the image")
    }

    /// The big-endian pointer at an offset of the image.
    fn pointer(image: &[u8], at: usize) -> usize {
        let bytes = image[at..at + size_of::<u32>()]
            .try_into()
            .expect("four bytes");
        usize::try_from(u32::from_be_bytes(bytes)).expect("an offset")
    }

    /// The NUL-terminated text a pointer of the image points at, `None`
    /// for a null one.
    fn text(image: &[u8], at: usize) -> Option<String> {
        let start = pointer(image, at);
        let bytes = image[start..]
            .split(|&byte| byte == 0)
            .next()
            .expect("some text");
        (start != 0).then(|| String::from_utf8(bytes.to_vec()).expect("ASCII"))
    }

    /// The table holds the sentences the image decodes and no other, and
    /// each one's fields are the count the image's table takes.  That
    /// count is of the fields from the address on, one more with a
    /// checksum, and negative for a sentence that may have more: `GSV`.
    /// `RMC`'s is one under the specification's twelve fields and
    /// checksum.
    #[test]
    fn the_table_matches_the_sentences_the_image_decodes() {
        let image = image();
        let decoded: BTreeMap<String, i8> = (DECODED..)
            .step_by(DECODED_ENTRY)
            .map_while(|at| {
                let name = text(&image, at)?;
                Some((name, i8::from_be_bytes([image[at + DECODED_FIELDS]])))
            })
            .collect();
        let ours: BTreeSet<&str> = SENTENCES
            .iter()
            .filter(|row| row.direction == Direction::FromEngine)
            .map(|row| row.name)
            .collect();
        assert_eq!(ours, decoded.keys().map(String::as_str).collect());
        for (name, count) in &decoded {
            let row = sentence_type(name.as_bytes()).expect("a row");
            let data = usize::from(count.unsigned_abs()) - row.class.address_fields();
            if name == "RMC" {
                assert_eq!((row.fields.least, row.fields.most), (data - 1, data));
            } else if *count < 0 {
                assert_eq!(row.fields.least, data, "{name}");
                assert!(row.fields.most > data, "{name}");
            } else {
                assert_eq!((row.fields.least, row.fields.most), (data, data), "{name}");
            }
        }
    }

    /// The table holds the sentences the image sends and no other: each
    /// descriptor's address and query, but the placeholder's.
    #[test]
    fn the_table_matches_the_sentences_the_image_sends() {
        let image = image();
        let sent: BTreeSet<String> = SENT
            .step_by(SENT_ENTRY)
            .flat_map(|at| [text(&image, at), text(&image, at + SENT_QUERY)])
            .flatten()
            .map(|address| address[..NAME].to_string())
            .filter(|name| name != PLACEHOLDER)
            .collect();
        let ours: BTreeSet<&str> = SENTENCES
            .iter()
            .filter(|row| row.direction == Direction::ToEngine)
            .map(|row| row.name)
            .collect();
        assert_eq!(ours, sent.iter().map(String::as_str).collect());
    }
}
