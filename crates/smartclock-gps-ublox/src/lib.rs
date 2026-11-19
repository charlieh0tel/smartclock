//! u-blox UBX messages, as `smartclock-ubx` decodes them, made into
//! `smartclock-gps` records.
//!
//! No `SmartClock` holds a u-blox receiver, so this is not in
//! `smartclock-gps` with the formats the receivers' engines speak.  It
//! is for the other end: a u-blox receiver on a bench is where a tool
//! that stands in for an engine gets each second's
//! [`Record`](smartclock_gps::record::Record), live or from a
//! recording of its output, and a microcontroller with a u-blox module
//! gets it the same way.  Like `smartclock-gps` it is `no_std`, does
//! not allocate and does no floating-point arithmetic.
//!
//! The format is that of the u-blox 8 / u-blox M8 receiver description
//! including protocol specification, UBX-13003221, revision R28.  No
//! copy is in `third_party/`; a section and page cited here are that
//! revision's, the page as printed.  The framing and the decoders are
//! `smartclock-ubx`'s.  Where a record holds what a decoded message
//! says, a trait of [`record`] gives it in the record's units, as
//! [`NavPvtRecord::fix`](record::NavPvtRecord::fix) does, and a
//! value's own mapping is a function there, as
//! [`satellite_health`](record::satellite_health) is.
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
//!   `RXM-SFRBX`
//!   ([`RxmSfrbx::gps_words`](smartclock_ubx::rxm::RxmSfrbx::gps_words)),
//!   decoded by [`subframe`](smartclock_ubx::subframe), across epochs, each page the last one
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
//!   `TIM-TP`'s `qErr` is decoded ([`TimTp`](smartclock_ubx::tim::TimTp)), but the
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

mod almanac;
pub mod assembler;
pub mod record;

#[cfg(test)]
extern crate std;

#[cfg(test)]
mod tests;
