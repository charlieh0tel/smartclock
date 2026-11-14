//! Commands found in the bytes a receiver sends.
//!
//! An engine does not resynchronize as the receiver's firmware does,
//! which [`Deframer`](crate::oncore::Deframer) follows: what does not
//! turn out to be a command is dropped a byte at a time, and a command
//! is looked for from each byte after, so a whole command straight
//! after the start of a broken one is still taken.

use crate::oncore::HEADER;
use crate::oncore::MAX_COMMAND;
use crate::oncore::MessageId;
use crate::oncore::OVERHEAD;
use crate::oncore::TRAILER;
use crate::oncore::checksum;
use crate::oncore::command_length;

/// What starts a frame.
const START: [u8; 2] = *b"@@";
/// What ends one.
const END: [u8; 2] = *b"\r\n";

/// One command: its ID and its data.
#[derive(Debug, Clone, Copy)]
pub(super) struct Command {
    /// The ID.
    pub(super) id: MessageId,
    /// The first `length` are the data.
    data: [u8; MAX_COMMAND - OVERHEAD],
    /// How many they are.
    length: usize,
}

impl Command {
    pub(super) fn data(&self) -> &[u8] {
        &self.data[..self.length]
    }
}

/// What a framer's pending bytes hold.
enum Examined {
    /// The start of a command.
    Incomplete,
    /// A command.
    Complete(Command),
    /// Not a command from their first byte.
    Bad,
}

/// Finds commands in a stream of bytes.
#[derive(Debug)]
pub(super) struct Framer {
    /// The first `filled` are the bytes of a command not yet whole,
    /// from its `@`.
    pending: [u8; MAX_COMMAND],
    /// How many they are.
    filled: usize,
}

impl Framer {
    pub(super) const fn new() -> Self {
        Self {
            pending: [0; MAX_COMMAND],
            filled: 0,
        }
    }

    /// Takes a byte, and returns the command it ends, if any.
    pub(super) fn push(&mut self, byte: u8) -> Option<Command> {
        self.pending[self.filled] = byte;
        self.filled += 1;
        loop {
            match self.examine() {
                Examined::Incomplete => return None,
                Examined::Complete(command) => {
                    self.filled = 0;
                    return Some(command);
                }
                Examined::Bad => {
                    self.pending.copy_within(1..self.filled, 0);
                    self.filled -= 1;
                }
            }
        }
    }

    fn examine(&self) -> Examined {
        let pending = &self.pending[..self.filled];
        if !pending
            .iter()
            .zip(START)
            .all(|(byte, start)| *byte == start)
        {
            return Examined::Bad;
        }
        let Some(&[first, second]) = pending.get(START.len()..HEADER) else {
            return Examined::Incomplete;
        };
        let id = MessageId::new([first, second]);
        let Some(length) = command_length(id).filter(|length| *length >= OVERHEAD) else {
            return Examined::Bad;
        };
        if pending.len() < length {
            return Examined::Incomplete;
        }
        let body = &pending[HEADER..length - TRAILER];
        if pending[length - TRAILER] != checksum(id, body) || pending[length - END.len()..] != END {
            return Examined::Bad;
        }
        let mut data = [0; MAX_COMMAND - OVERHEAD];
        data[..body.len()].copy_from_slice(body);
        Examined::Complete(Command {
            id,
            data,
            length: body.len(),
        })
    }
}
