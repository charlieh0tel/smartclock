//! Units as types, all integers.
//!
//! Each is the unit and resolution some message a `SmartClock` decodes
//! carries, so that a [`Record`](crate::record::Record) holds a value as
//! it will be sent and nothing needs floating point.  The pages cited
//! are those of the VP Oncore's command reference,
//! `third_party/motorola/VPCommands.pdf`, as printed.

use core::num::NonZeroU8;

/// A count of seconds.
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash, Default)]
pub struct Seconds<T>(pub T);

/// A count of nanoseconds.
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash, Default)]
pub struct Nanoseconds<T>(pub T);

/// An angle in whole degrees.
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash, Default)]
pub struct Degrees<T>(pub T);

/// An angle in tenths of a degree: a heading, 0 to 3599 (p. 72).
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash, Default)]
pub struct TenthDegrees(pub u16);

/// An angle in milliarcseconds: a latitude, north positive, or a
/// longitude, east positive (p. 71).
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash, Default)]
pub struct MilliArcSeconds(pub i32);

/// A length in centimeters: a height (p. 71).
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash, Default)]
pub struct Centimeters(pub i32);

/// A speed in centimeters a second (p. 72).
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash, Default)]
pub struct CentimetersPerSecond(pub u16);

/// A frequency in hertz: a satellite's Doppler (p. 75).
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash, Default)]
pub struct Hertz(pub i16);

/// A carrier-to-noise density in dB-Hz: a satellite's signal (`@@Ea`,
/// `third_party/motorola/oncore-ut-users-guide/ch6.pdf` p. 6.20; `GSV`,
/// `third_party/furuno/Furuno_GT8031_Protocol_Specification_Rev1.pdf`
/// p. 23).
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash, Default)]
pub struct DecibelHertz(pub u8);

/// A dimensionless figure in tenths: a dilution of precision (pp. 72,
/// 96).
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash, Default)]
pub struct Tenths(pub u16);

/// The highest GPS satellite ID (p. 75: 1 to 32).
pub const PRNS: u8 = 32;

/// A GPS satellite's ID, 1 to [`PRNS`].
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct Prn(NonZeroU8);

impl Prn {
    /// `None` unless `id` is 1 to [`PRNS`].
    #[must_use]
    pub const fn new(id: u8) -> Option<Self> {
        if id > PRNS {
            return None;
        }
        match NonZeroU8::new(id) {
            Some(id) => Some(Self(id)),
            None => None,
        }
    }

    /// The ID.
    #[must_use]
    pub const fn get(self) -> u8 {
        self.0.get()
    }

    /// Its bit in a [`PrnSet`].
    const fn bit(self) -> u32 {
        1 << (self.get() - 1)
    }
}

/// A set of satellites, a bit each: bit 0 is satellite 1 and bit 31
/// satellite 32, as `@@Bd` sends an almanac's (p. 79).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Default)]
pub struct PrnSet(u32);

impl PrnSet {
    /// No satellite.
    pub const EMPTY: Self = Self(0);

    /// The set a bit field holds.
    #[must_use]
    pub const fn from_bits(bits: u32) -> Self {
        Self(bits)
    }

    /// The set as a bit field.
    #[must_use]
    pub const fn bits(self) -> u32 {
        self.0
    }

    /// The set with `prn` in it.
    #[must_use]
    pub const fn with(self, prn: Prn) -> Self {
        Self(self.0 | prn.bit())
    }

    /// The set with `prn` out of it.
    #[must_use]
    pub const fn without(self, prn: Prn) -> Self {
        Self(self.0 & !prn.bit())
    }

    /// Whether `prn` is in it.
    #[must_use]
    pub const fn contains(self, prn: Prn) -> bool {
        self.0 & prn.bit() != 0
    }

    /// How many satellites it holds.
    #[must_use]
    pub const fn len(self) -> u32 {
        self.0.count_ones()
    }

    /// Whether it holds none.
    #[must_use]
    pub const fn is_empty(self) -> bool {
        self.0 == 0
    }
}

#[cfg(test)]
mod tests {
    use super::PRNS;
    use super::Prn;
    use super::PrnSet;

    #[test]
    fn a_prn_is_one_to_thirty_two() {
        assert_eq!(Prn::new(0), None);
        assert_eq!(Prn::new(1).map(Prn::get), Some(1));
        assert_eq!(Prn::new(PRNS).map(Prn::get), Some(PRNS));
        assert_eq!(Prn::new(PRNS + 1), None);
    }

    /// VP p. 79: the field's lsb is satellite 1, its msb satellite 32.
    #[test]
    fn a_set_is_the_almanac_bit_field() {
        let first = Prn::new(1).unwrap();
        let last = Prn::new(PRNS).unwrap();
        let set = PrnSet::EMPTY.with(first).with(last);
        assert_eq!(set.bits(), 0x8000_0001);
        assert_eq!(PrnSet::from_bits(0x8000_0001), set);
        assert_eq!(set.len(), 2);
        assert!(set.contains(first) && set.contains(last));
        assert!(!set.contains(Prn::new(2).unwrap()));
        assert_eq!(set.without(last).bits(), 1);
        assert!(set.without(first).without(last).is_empty());
    }
}
