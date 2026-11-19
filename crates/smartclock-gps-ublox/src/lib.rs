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
//!   must be that message's own, for one of a header and blocks no
//!   more blocks than its header's one-byte count can give, or the
//!   frame is
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
//! ([`units`]), and no type of `smartclock-gps`; where a record holds
//! the same thing, a trait of [`record`] gives it in the record's
//! units, as [`NavPvtRecord::fix`](record::NavPvtRecord::fix) does.
//! A message is decoded only at its exact length: a fixed one's, or,
//! for one of a header and blocks, a length whose count of blocks is
//! the count the header gives.  A field the document gives as floating point is kept as its
//! bits, for a caller to convert.  Which satellite system's data a
//! caller keeps is the caller's: the decoders give the system of each
//! satellite and drop none.
//! [`Message::parse_payload`](message::Message::parse_payload) decodes
//! an ID and payload a caller already has, with the same checks.
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
//! buffer.  Its header's count of blocks must be the count its length
//! gives, or the message is
//! [`FrameError::BadLength`](frame::FrameError::BadLength), reported at
//! the header's last byte, and a new start is looked for from the
//! next.  So a damaged length that passes costs at most the longest
//! such message, 8176 bytes of `RXM-RAWX`'s 255 measurements, and only
//! if the header's count agrees with it.
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
//! `NAV-POSECEF`, `NAV-SAT`, `NAV-EOE`, `MON-HW` and `TIM-TP`, and
//! `RXM-SFRBX` for the almanac.  Configuring it is the caller's
//! affair.
//!
//! - **Epochs.**  Messages with the same `iTOW` are one navigation
//!   solution (section 9.3, p. 27).  `NAV-EOE` ends an epoch; so does a
//!   navigation message with another `iTOW`, for a receiver that sends
//!   no `NAV-EOE` or a stream that lost one, and then the record comes
//!   a message late.  Nothing from one `iTOW` is put with another's,
//!   but for a lost `NAV-SAT`'s sky (below).
//! - **Time** is `NAV-TIMEGPS`'s, to the nearest second, and only from
//!   an epoch whose `NAV-PVT` has `gnssFixOK` set and whose
//!   `NAV-TIMEGPS` has a valid time of week and week: without a fix the
//!   receiver gives plausible times from its own clock (section 9.2,
//!   p. 27).  The week is the receiver's own resolution of the
//!   satellites' ten bits (section 9.9.1, p. 29) unless the assembler
//!   was made with a week to resolve it near
//!   ([`Assembler::near`](assembler::Assembler::near)).
//! - **Position hold.**  A timing receiver whose position is fixed,
//!   known or surveyed, calculates only the time (section 25.1, p. 85), and
//!   `NAV-PVT` gives the time-only fix
//!   ([`Fix::TimeOnly`](smartclock_gps::record::Fix::TimeOnly)).  Its
//!   `NAV-DOP` then gives `pDOP`, `hDOP` and `vDOP` as 9999, the most
//!   the field holds, and only `gDOP` and `tDOP` carry values: so a
//!   NEO-M8T of timing firmware 1.10 sends in every epoch of position
//!   hold, though the document says nothing of it.  Those are not the
//!   geometry of a position, and a record of a time-only fix has no
//!   DOPs.
//! - **The receiver's mode** is position hold from the `NAV-PVT` whose
//!   `fixType` is 5, the time-only fix, and navigating from one of a
//!   2D or 3D fix
//!   ([`NavPvtRecord::receiver_mode`](record::NavPvtRecord::receiver_mode)),
//!   and is kept through the epochs whose `NAV-PVT` gives no fix from
//!   satellites, or that have none: position hold is the receiver's
//!   configuration (section 25.1, p. 85), which losing its satellites
//!   does not change.  Unknown until a `NAV-PVT` tells it, and again after
//!   [`reset`](smartclock_gps::source::replay::Assemble::reset).  So a
//!   receiver that leaves position hold with no fix is taken to hold
//!   until its next fix.
//! - **The leap second.**  GPS less UTC is `NAV-TIMELS`'s `currLs`, or
//!   `NAV-TIMEGPS`'s `leapS` from a receiver that sends no `NAV-TIMELS`.
//!   A leap second is `NAV-TIMELS`'s while its countdown is above zero,
//!   placed by its week and day
//!   ([`LeapNoticeRecord::event`](record::LeapNoticeRecord::event)).
//!   Once announced it is remembered, and from its second on the
//!   record's offset is the one it leaves whatever the receiver reports
//!   in that second, the document not saying in which second `currLs`
//!   steps.
//! - **Satellites** are `NAV-SAT`'s GPS ones with a known place in the
//!   sky, those used in the fix first, and the lowest left out if there
//!   are more than a record holds.  An epoch with a fix and no `NAV-SAT`
//!   of its own, one the receiver did not send or that was lost on the
//!   way, has the last
//!   `NAV-SAT`'s satellites if that came no more than three seconds of
//!   `iTOW` before it, three epochs at one a second: a receiver does
//!   not lose its sky with a message, and an engine given none would
//!   report no satellites tracked for that second.  A longer loss, or
//!   an epoch without a fix, has none, as a receiver that sends no
//!   `NAV-SAT` does.  Such epochs after the stream's first `NAV-SAT`
//!   are counted
//!   ([`Assembler::lost_skies`](assembler::Assembler::lost_skies)):
//!   a `NAV-SAT` that never came whole is no frame error, and only
//!   that count shows it.
//! - **Time RAIM** is `TIM-TP`'s `raim` (p. 433), as the last one
//!   before the epoch's end gave it, and none from a receiver that
//!   sends no `TIM-TP` or has no information.  The document says only
//!   whether the receiver's (T)RAIM is active, so what a record makes
//!   of it is this crate's reading: active, a bad satellite can be
//!   detected; not active, nothing can be.  The solution is always
//!   [`RaimSolution::Unknown`](smartclock_gps::record::RaimSolution::Unknown),
//!   the message giving no RAIM result or alarm: a receiver alarming
//!   on a bad satellite may still have its pulse locked.  No satellite
//!   is given as isolated, and isolation is never claimed.
//! - **The almanac** is gathered from the GPS L1 C/A subframes of
//!   `RXM-SFRBX` ([`RxmSfrbx::gps_words`](rxm::RxmSfrbx::gps_words)),
//!   decoded by [`subframe`], across epochs, each page the last one
//!   broadcast.  A record has it from the first epoch's end after
//!   subframe 5, page 25 has given its time and week: it holds the
//!   satellites whose page, of subframe 4 or 5 with the data ID `01`,
//!   has that time, and calls healthy those whose page gives a health
//!   of zero and whose page 25 does too or has not come (IS-GPS-200N,
//!   sections 20.3.3.5.1.2 to 20.3.3.5.1.5).  Its eight bits of week
//!   are resolved to the week nearest the last epoch with a time, or
//!   before one, the week the assembler was made to resolve near.  It
//!   need not be complete: a satellite whose page has not come is not
//!   held.  A page 25 of a later time or week than the last drops
//!   every page before it, page 25s too: a page gives no week, and the
//!   control segment uses the same time week after week, so a page of
//!   that time from an earlier upload would pass for one of this.  A
//!   page of the new upload that came before its page 25 is gathered
//!   again at its next broadcast.  A page 25 of an earlier time or
//!   week is ignored: while the satellites change over to a new
//!   upload, those not yet changed still send the old one's, and the
//!   almanac would otherwise come and go.  A satellite's own page of
//!   the old upload is still taken, and counts if its time is the new
//!   one's.  None without `RXM-SFRBX`, before page 25, with no page of
//!   page 25's time, or with no week to resolve near.
//! - **The self-test** passes: no message here tells of a part that
//!   fails, and the receiver that sent the epoch runs.
//! - **Not filled.**
//!   [`Record::timing`](smartclock_gps::record::Record::timing):
//!   `TIM-TP`'s `qErr` is decoded ([`TimTp`](tim::TimTp)), but the
//!   document gives neither its sign nor what multiple of sigma `tAcc`
//!   is.  A satellite's Doppler: only `RXM-RAWX`
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
mod almanac;
pub mod assembler;
mod fields;
pub mod frame;
pub mod message;
pub mod mon;
pub mod nav;
pub mod record;
pub mod rxm;
pub mod subframe;
pub mod tim;
pub mod units;

#[cfg(test)]
extern crate std;

#[cfg(test)]
mod tests;
