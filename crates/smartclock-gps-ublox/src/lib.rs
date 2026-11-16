//! u-blox UBX framing, the messages a caller reads from a u-blox
//! receiver, and those that fill a `smartclock-gps` record.
//!
//! No `SmartClock` holds a u-blox receiver, so this is not in
//! `smartclock-gps` with the formats the receivers' engines speak.  It
//! is for the other end: a u-blox receiver on a bench is where a tool
//! that stands in for an engine gets each second's
//! [`Record`](smartclock_gps::record::Record), live or from a
//! recording of its output, and a microcontroller board with a u-blox
//! module reads it with the same decoders.  Like `smartclock-gps` it
//! is `no_std`, does not allocate and does no floating-point
//! arithmetic.
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
//!   must be that message's own, or the frame is
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
//! module ([`nav`], [`rxm`], [`mon`], [`tim`], [`ack`]).  A decoder
//! keeps every field of its message, in the message's own units
//! ([`units`] has those `smartclock-gps` lacks); where a record holds
//! the same thing, a method gives it in the record's units, as
//! [`NavPvt::fix`](nav::NavPvt::fix) does.  A message is decoded only
//! at its exact length: a fixed one's, or, for one of a header and
//! blocks, a length whose count of blocks is the count the header
//! gives.  A field the document gives as floating point is kept as its
//! bits, for a caller to convert.  Which satellite system's data a
//! caller keeps is the caller's: the decoders give the system of each
//! satellite and drop none.
//!
//! A GPS satellite's own navigation data, the subframes `RXM-SFRBX`
//! carries, is decoded by [`subframe`], to IS-GPS-200N.
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
//! buffer.
//!
//! # From messages to a record
//!
//! [`Assembler`](assembler::Assembler) takes the stream a byte at a
//! time and gives a [`Record`](smartclock_gps::record::Record) for each
//! navigation epoch.  It is an
//! [`Assemble`](smartclock_gps::source::replay::Assemble), so
//! [`Replay`](smartclock_gps::source::replay::Replay) reads a recording
//! through it, and a caller with a live receiver pushes each byte with
//! the slot of a [`Latest`](smartclock_gps::source::Latest).  The
//! receiver is to be set to one epoch a second with the messages it
//! reads turned on: `NAV-PVT`, `NAV-TIMEGPS`, `NAV-TIMELS`, `NAV-DOP`,
//! `NAV-POSECEF`, `NAV-SAT`, `NAV-EOE`, `MON-HW` and `TIM-TP`.
//! Configuring it is the caller's affair.
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
//!   was made with a week to resolve it near
//!   ([`Assembler::near`](assembler::Assembler::near)).
//! - **The leap second.**  GPS less UTC is `NAV-TIMELS`'s `currLs`, or
//!   `NAV-TIMEGPS`'s `leapS` from a receiver that sends no `NAV-TIMELS`.
//!   A leap second is `NAV-TIMELS`'s while its countdown is above zero,
//!   placed by its week and day
//!   ([`LeapNotice::event`](nav::LeapNotice::event)).  Once announced
//!   it is remembered, and from its second on the record's offset is
//!   the one it leaves whatever the receiver reports in that second,
//!   the document not saying in which second `currLs` steps.
//! - **Satellites** are `NAV-SAT`'s GPS ones with a known place in the
//!   sky, those used in the fix first, and the lowest left out if there
//!   are more than a record holds.
//! - **Time RAIM** is `TIM-TP`'s `raim` (p. 433), as the last one
//!   before the epoch's end gave it, and none from a receiver that
//!   sends no `TIM-TP` or has no information.  The document says only
//!   whether the receiver's (T)RAIM is active, so what a record makes
//!   of it is this crate's reading: active with the pulse locked to
//!   the satellites, the solution is good and a bad satellite can be
//!   detected; active and not locked, the solution is not known; not
//!   active, the solution is not known and nothing can be detected.
//!   No satellite is given as isolated, and isolation is never
//!   claimed.
//! - **The self-test** passes: no message here tells of a part that
//!   fails, and the receiver that sent the epoch runs.
//! - **Not filled.**
//!   [`Record::timing`](smartclock_gps::record::Record::timing):
//!   `TIM-TP`'s `qErr` is decoded ([`TimTp`](tim::TimTp)), but the
//!   document gives neither its sign nor what multiple of sigma `tAcc`
//!   is.  [`Record::almanac`](smartclock_gps::record::Record::almanac):
//!   the assembler reads no satellite's own data, which [`subframe`]
//!   decodes for a caller.  A satellite's Doppler: only `RXM-RAWX`
//!   carries it, as floating point.  A
//!   satellite's time error: no message here carries it.
//!   [`Fix::Propagated`](smartclock_gps::record::Fix::Propagated) is
//!   never given: a fix by dead reckoning alone is no fix here.  A
//!   satellite's flags and URA, the age of differential data, the
//!   magnetic variation, the velocity along each axis and the inside
//!   of the solution: not filled, though `NAV-PVT`'s velocity along
//!   each axis and magnetic declination are decoded.
//!
//! [`DECLARATION`](assembler::DECLARATION) says which parts of a record
//! that comes to, and the crate's tests hold it to what
//! [`Assembler`](assembler::Assembler) writes.

#![no_std]

pub mod ack;
pub mod assembler;
mod fields;
pub mod frame;
pub mod message;
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
