//! The wire formats a `SmartClock`'s GPS engine speaks, for tools that
//! talk to one or stand in for one.
//!
//! A `SmartClock` receiver holds a GPS engine on an internal serial link.
//! Most hold a Motorola Oncore, on its binary `@@` messages ([`oncore`]).
//! `docs/firmware/gps.md` describes the link from the firmware's side.
//!
//! This crate covers only what the receivers' firmware sends and decodes,
//! and so far only the framing: IDs, lengths, checksums, an encoder and a
//! streaming deframer.  It is not a general library for the engine.
//!
//! It is `no_std` and does not allocate: an encoder writes into the
//! caller's buffer, and a deframer owns a buffer of fixed size and lends
//! each frame out of it.

#![no_std]

#[cfg(test)]
extern crate std;

use thiserror::Error;

pub mod oncore;

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
