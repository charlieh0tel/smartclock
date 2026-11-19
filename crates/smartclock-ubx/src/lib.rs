//! u-blox UBX framing, both ways: the messages a caller reads from a
//! u-blox receiver, every field decoded, and those it sends one to set
//! it up, each encoded whole.
//!
//! A codec alone: it depends on no other crate of this workspace, so a
//! microcontroller with a u-blox module and a host reading a recording
//! frame and unframe with the same code.  `smartclock-gps-ublox` makes its
//! messages into `smartclock-gps` records.  It is `no_std`, does not
//! allocate and does no floating-point arithmetic.
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
//! [`Deframer`](frame::Deframer) finds frames in a stream that may
//! start mid-frame or carry noise.  The document says only what a frame
//! is, so where to pick the stream up again is this crate's own:
//!
//! - Outside a frame every byte but `B5` is skipped, and a `B5` not
//!   followed by `62` too, with no error.  A further `B5` after one is
//!   taken as the start.
//! - A length is believed only as far as it can be checked before the
//!   checksum.  For a message of [`MESSAGES`](message::MESSAGES) it
//!   must be that message's own, or for a configuration message a poll
//!   request's, for one of a header and blocks no more blocks than its
//!   header's one-byte count can give, or the frame is
//!   [`FrameError::BadLength`](frame::FrameError::BadLength); for any
//!   message it must fit the deframer's buffer, or it is
//!   [`FrameError::Overflow`](frame::FrameError::Overflow).  Either is
//!   reported at the length's second byte and a new start is looked for
//!   from the next: no payload is skipped on the word of a length
//!   nothing vouches for, which could be 65535 bytes.
//! - Inside a payload nothing is looked for: a `B5 62` there is data.
//! - A bad checksum is
//!   [`FrameError::BadChecksum`](frame::FrameError::BadChecksum),
//!   reported at its second byte, and the frame's bytes are not
//!   examined again.  So a damaged length that passes the checks above
//!   costs at most the buffer's length of what follows it.
//!
//! # The messages
//!
//! [`Message::parse`](message::Message::parse) decodes a frame of
//! [`MESSAGES`](message::MESSAGES), each by a type in its class's
//! module ([`nav`], [`rxm`], [`mon`], [`tim`], [`ack`], [`cfg`](mod@cfg)).  A decoder
//! keeps every field of its message, in the message's own units
//! ([`units`]).  A message is decoded only at its exact length: a
//! fixed one's, or, for one of a header and blocks, a length whose
//! count of blocks is the count the header gives.  A field the document
//! gives as floating point is kept as its bits, for a caller to
//! convert.  Which satellite system's data a
//! caller keeps is the caller's: the decoders give the system of each
//! satellite and drop none.
//! [`Message::parse_payload`](message::Message::parse_payload) decodes
//! an ID and payload a caller already has, with the same checks.
//!
//! A GPS satellite's own navigation data, the subframes `RXM-SFRBX`
//! carries, is decoded by [`subframe`], to IS-GPS-200N.
//!
//! # Configuring
//!
//! A message a caller sends is a struct of its fields in [`cfg`](mod@cfg) or
//! [`mga`], const-constructible, whose `encode` writes the whole frame,
//! sync bytes to checksum, into the caller's slice and returns its
//! length, with no allocation.  It writes the values it holds: which
//! to send, and any state carried from one message to the next, is
//! the caller's.  A field of a list of values is an enum that keeps a
//! value the document does not give; a bit field is its bits, with its
//! named bits and field values as constants, so a reserved bit a
//! caller sets is sent too.  [`poll`](frame::poll) asks for a message,
//! and [`message_id`](frame::message_id) reads back which message a
//! frame is, to match the receiver's `UBX-ACK-ACK` or `UBX-ACK-NAK` to
//! it.  The answers to the polls of `CFG-TMODE2` and `CFG-TP5` are
//! decoded by [`Message::parse`](message::Message::parse), into the
//! same structs.
//!
//! # Streaming
//!
//! `NAV-SAT` and `RXM-RAWX` grow with the sky, past 1 KB from a
//! receiver tracking every system, which a [`Deframer`](frame::Deframer)
//! must hold whole.  A [`Streamer`](frame::Streamer) gives them
//! instead a part at a time, the header and then each block as its
//! last byte is taken, each lent from a buffer that holds only the
//! longest part and the longest message not streamed; the count of
//! blocks comes from the length, and
//! [`Part`](message::Part) decodes each part.  Every other message
//! comes whole, as from a deframer.  The checksum comes last, so the
//! parts come before anything vouches for them: a caller keeps what
//! they say aside, and takes it at
//! [`Event::End`](frame::Event::End) or drops it at the error that
//! comes instead.  The rules of "Resynchronizing" hold as they are; a
//! streamed message's length must be its own, and need not fit the
//! buffer.  Its header's count of blocks must be the count its length
//! gives, or the message is
//! [`FrameError::BadLength`](frame::FrameError::BadLength), reported at
//! the header's last byte, and a new start is looked for from the
//! next.  So a damaged length that passes costs at most the longest
//! such message, 8176 bytes of `RXM-RAWX`'s 255 measurements, and only
//! if the header's count agrees with it.

#![no_std]

#[macro_use]
mod macros;

pub mod ack;
pub mod cfg;
mod fields;
pub mod frame;
pub mod message;
pub mod mga;
pub mod mon;
pub mod nav;
pub mod rxm;
pub mod subframe;
pub mod tim;
pub mod units;

#[cfg(test)]
extern crate std;

#[cfg(test)]
mod tests;
