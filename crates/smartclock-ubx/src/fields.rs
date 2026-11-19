//! A payload's fields, read by their offsets.

use crate::units::Milliseconds;

/// A payload's fields, by their byte offsets: little-endian, as every
/// number in a payload is (section 32.3, p. 169).
#[derive(Debug, Clone, Copy)]
pub(crate) struct Fields<'a>(pub(crate) &'a [u8]);

impl Fields<'_> {
    pub(crate) fn bytes<const K: usize>(self, offset: usize) -> [u8; K] {
        self.0
            .get(offset..offset + K)
            .and_then(|field| field.try_into().ok())
            .unwrap_or([0; K])
    }

    pub(crate) fn u8(self, offset: usize) -> u8 {
        u8::from_le_bytes(self.bytes(offset))
    }

    pub(crate) fn i8(self, offset: usize) -> i8 {
        i8::from_le_bytes(self.bytes(offset))
    }

    pub(crate) fn u16(self, offset: usize) -> u16 {
        u16::from_le_bytes(self.bytes(offset))
    }

    pub(crate) fn i16(self, offset: usize) -> i16 {
        i16::from_le_bytes(self.bytes(offset))
    }

    pub(crate) fn u32(self, offset: usize) -> u32 {
        u32::from_le_bytes(self.bytes(offset))
    }

    pub(crate) fn i32(self, offset: usize) -> i32 {
        i32::from_le_bytes(self.bytes(offset))
    }

    pub(crate) fn u64(self, offset: usize) -> u64 {
        u64::from_le_bytes(self.bytes(offset))
    }

    /// Whether bit `bit` of the byte at `offset` is set.
    pub(crate) fn bit(self, offset: usize, bit: u32) -> bool {
        self.u8(offset) & (1 << bit) != 0
    }

    /// `iTOW`, every navigation message's first field.
    pub(crate) fn time_of_week(self) -> Milliseconds<u32> {
        Milliseconds(self.u32(0))
    }
}

/// A payload being written, by its fields' byte offsets: the writing
/// counterpart of [`Fields`], little-endian likewise.  It starts
/// zeroed, so a reserved field left alone is zero, as an input
/// message's must be (section 32.3.2, p. 169).
#[derive(Debug)]
pub(crate) struct Writer<'a>(pub(crate) &'a mut [u8]);

impl Writer<'_> {
    pub(crate) fn bytes(&mut self, offset: usize, field: &[u8]) {
        self.0[offset..offset + field.len()].copy_from_slice(field);
    }

    pub(crate) fn u8(&mut self, offset: usize, value: u8) {
        self.bytes(offset, &value.to_le_bytes());
    }

    pub(crate) fn i8(&mut self, offset: usize, value: i8) {
        self.bytes(offset, &value.to_le_bytes());
    }

    pub(crate) fn u16(&mut self, offset: usize, value: u16) {
        self.bytes(offset, &value.to_le_bytes());
    }

    pub(crate) fn i16(&mut self, offset: usize, value: i16) {
        self.bytes(offset, &value.to_le_bytes());
    }

    pub(crate) fn u32(&mut self, offset: usize, value: u32) {
        self.bytes(offset, &value.to_le_bytes());
    }

    pub(crate) fn i32(&mut self, offset: usize, value: i32) {
        self.bytes(offset, &value.to_le_bytes());
    }

    pub(crate) fn u64(&mut self, offset: usize, value: u64) {
        self.bytes(offset, &value.to_le_bytes());
    }
}

/// `width` bits of `value` from bit `shift` up: a bit field of an `X`
/// field.
pub(crate) const fn bit_field(value: u32, shift: u32, width: u32) -> u8 {
    ((value >> shift) & ((1 << width) - 1)) as u8
}
