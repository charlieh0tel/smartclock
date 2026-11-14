//! A message's data, written a field at a time.

use crate::oncore::MAX_RESPONSE;
use crate::oncore::OVERHEAD;

/// The most data a message from an engine carries.
const DATA_MAX: usize = MAX_RESPONSE - OVERHEAD;

/// The data of one message being built, fields big-endian as an Oncore
/// sends them.  What does not fit is dropped; no message's fields
/// exceed its length.
#[derive(Debug)]
pub(super) struct Data {
    /// The first `length` are the data so far.
    bytes: [u8; DATA_MAX],
    /// How many are written.
    length: usize,
}

impl Data {
    pub(super) const fn new() -> Self {
        Self {
            bytes: [0; DATA_MAX],
            length: 0,
        }
    }

    pub(super) fn as_slice(&self) -> &[u8] {
        &self.bytes[..self.length]
    }

    pub(super) fn bytes(&mut self, bytes: &[u8]) -> &mut Self {
        let end = (self.length + bytes.len()).min(DATA_MAX);
        let room = end - self.length;
        self.bytes[self.length..end].copy_from_slice(&bytes[..room]);
        self.length = end;
        self
    }

    pub(super) fn zeros(&mut self, count: usize) -> &mut Self {
        let end = (self.length + count).min(DATA_MAX);
        self.bytes[self.length..end].fill(0);
        self.length = end;
        self
    }

    pub(super) fn byte(&mut self, value: u8) -> &mut Self {
        self.bytes(&[value])
    }

    pub(super) fn signed_byte(&mut self, value: i8) -> &mut Self {
        self.bytes(&value.to_be_bytes())
    }

    pub(super) fn word(&mut self, value: u16) -> &mut Self {
        self.bytes(&value.to_be_bytes())
    }

    pub(super) fn signed_word(&mut self, value: i16) -> &mut Self {
        self.bytes(&value.to_be_bytes())
    }

    /// The low three bytes of `value`.
    pub(super) fn triple(&mut self, value: u32) -> &mut Self {
        let [_, bytes @ ..] = value.to_be_bytes();
        self.bytes(&bytes)
    }

    pub(super) fn long(&mut self, value: u32) -> &mut Self {
        self.bytes(&value.to_be_bytes())
    }

    pub(super) fn signed_long(&mut self, value: i32) -> &mut Self {
        self.bytes(&value.to_be_bytes())
    }
}
