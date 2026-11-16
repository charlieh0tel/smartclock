//! A payload's fields, read by their offsets.

use smartclock_gps::units::Milliseconds;

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

/// `width` bits of `value` from bit `shift` up: a bit field of an `X`
/// field.
pub(crate) const fn bit_field(value: u32, shift: u32, width: u32) -> u8 {
    ((value >> shift) & ((1 << width) - 1)) as u8
}
