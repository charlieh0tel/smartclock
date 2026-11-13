//! The wire formats a `SmartClock`'s GPS engine speaks, for tools that
//! talk to one or stand in for one.
//!
//! A `SmartClock` receiver holds a GPS engine on an internal serial link.
//! Most hold a Motorola Oncore, on its binary `@@` messages ([`oncore`]);
//! the Z3815A holds a Furuno, on NMEA sentences with Furuno's `$PFEC`
//! ones among them ([`nmea`]).  `docs/firmware/gps.md` describes both
//! links from the firmware's side.
//!
//! This crate covers only what the receivers' firmware sends and decodes.
//! It is not a general library for either engine.  So far it has:
//!
//! - the framing: IDs, lengths, checksums, an encoder and a streaming
//!   deframer for each format;
//! - what an engine knows in one second, whichever engine it is: a
//!   [`record`], in the integer [`units`] the messages carry, with GPS
//!   [`time`], UTC and the calendar.
//!
//! What turns a record into an engine's messages is not here yet.
//!
//! It is `no_std` and does not allocate: an encoder writes into the
//! caller's buffer, a deframer owns a buffer of fixed size and lends
//! each frame out of it, and a record is of fixed size, built in a
//! `static` and filled in place.

#![no_std]

#[cfg(test)]
extern crate std;

use thiserror::Error;

pub mod nmea;
pub mod oncore;
pub mod record;
pub mod time;
pub mod units;

/// Which way a message travels on the engine's serial link.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Direction {
    /// From the receiver to its engine: a command.
    ToEngine,
    /// From the engine to the receiver: a response, solicited or not.
    FromEngine,
}

/// An encoder's output buffer is shorter than the frame.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Error)]
#[error("a frame of {needed} bytes does not fit in {room}")]
pub struct NoRoom {
    /// The frame's length.
    pub needed: usize,
    /// The buffer's.
    pub room: usize,
}
