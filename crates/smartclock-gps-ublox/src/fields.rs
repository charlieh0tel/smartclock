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

    /// `iTOW`, every navigation message's first field.
    pub(crate) fn time_of_week(self) -> Milliseconds<u32> {
        Milliseconds(self.u32(0))
    }
}
