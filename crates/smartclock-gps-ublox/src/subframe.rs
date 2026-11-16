//! A GPS satellite's navigation data: the subframes of its L1 C/A
//! signal, as `RXM-SFRBX` gives them
//! ([`RxmSfrbx::gps_words`](crate::rxm::RxmSfrbx::gps_words)).
//!
//! The layout is IS-GPS-200N's, section 20.3, cited by section, table
//! and figure; `third_party/` has no copy
//! (<https://archive.gps.gov/technical/icwg/IS-GPS-200N.pdf>).  A
//! subframe is ten words of 30 bits, each 24 data bits, first sent
//! first, and six of parity (section 20.3.2, Figure 20-1, sheets 1 to
//! 11, pp. 80-90).  The receiver gives each word in the low 30 bits of
//! four bytes, the two above them padding, and sends a subframe only
//! once its parity checks have passed, with the data bits the parity
//! algorithm inverts already inverted back (UBX-13003221 R28, sections
//! 10.1 and 10.2.1, pp. 30-31).  So the decoder checks no parity and
//! inverts nothing.  Bit `n` of a word here is the document's: 1 is
//! the first sent.
//!
//! A field is its raw broadcast integer, sign-extended if the document
//! gives it in two's complement, as a [`Scaled`]: the integer, the
//! power of two that scales it and the unit, in its type.  Nothing here
//! converts to floating point; a caller that wants the number
//! multiplies `raw` by two to the [`Scaled::SCALE`].
//!
//! - Subframe 1 ([`Subframe1`]): the week, the satellite's accuracy
//!   and health, and its clock.  Subframes 2 and 3 ([`Subframe2`],
//!   [`Subframe3`]): its ephemeris.
//! - Subframes 4 and 5 are pages, each named by its SV ID (section
//!   20.3.3.5.1.1): 1 to 32, a satellite's almanac ([`Almanac`]); 51
//!   in subframe 5, page 25, the almanac's time and week and the health
//!   of satellites 1 to 24 ([`AlmanacHealth`]); 56 in subframe 4, page
//!   18, the ionosphere and UTC ([`IonosphereUtc`]); 63 in subframe 4,
//!   page 25, each satellite's configuration and the health of 25 to 32
//!   ([`Configurations`]).  Any other is given by its IDs alone.

use core::marker::PhantomData;

use smartclock_gps::units::Seconds;

/// A subframe's words.
pub const WORDS: usize = 10;

/// The bits of a word: 30, in the low bits of a `u32`.
const WORD_BITS: u32 = 30;
/// Of those, the data bits.
const DATA_BITS: u32 = 24;
/// The TLM word's preamble, `10001011` (section 20.3.3.1).
pub const PREAMBLE: u8 = 0x8b;

/// The SV IDs of a satellite's almanac page (section 20.3.3.5.1.1).
const ALMANAC_PAGES: core::ops::RangeInclusive<u8> = 1..=32;
/// The SV ID of subframe 5, page 25.
const PAGE_ALMANAC_HEALTH: u8 = 51;
/// The SV ID of subframe 4, page 18.
const PAGE_IONOSPHERE_UTC: u8 = 56;
/// The SV ID of subframe 4, page 25.
const PAGE_CONFIGURATIONS: u8 = 63;

/// The satellites whose health subframe 5, page 25 gives, and those
/// whose configuration subframe 4, page 25 gives.
const HEALTH_PAGE_SATELLITES: usize = 24;
const CONFIGURED_SATELLITES: usize = 32;
/// The satellites, 25 to 32, whose health subframe 4, page 25 gives.
const LATE_SATELLITES: usize = 8;
/// A health of subframes 4 and 5, page 25: six bits.
const HEALTH_BITS: u32 = 6;
/// A configuration: four bits.
const CONFIGURATION_BITS: u32 = 4;

/// A subframe's ten words as the receiver gives them, each held to its
/// 30 bits.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct Words([u32; WORDS]);

impl Words {
    /// Ten words, each taken for its low 30 bits.
    #[must_use]
    pub const fn new(words: [u32; WORDS]) -> Self {
        let mut held = words;
        let mut index = 0;
        while index < WORDS {
            held[index] &= (1 << WORD_BITS) - 1;
            index += 1;
        }
        Self(held)
    }

    /// The words.
    #[must_use]
    pub const fn get(&self) -> [u32; WORDS] {
        self.0
    }

    /// `width` bits of word `word`, 1 to 10, from its bit `first`, 1
    /// being the first sent.
    const fn unsigned(&self, word: usize, first: u32, width: u32) -> u32 {
        (self.0[word - 1] >> (WORD_BITS + 1 - first - width)) & ((1 << width) - 1)
    }

    /// The same, in two's complement.
    const fn signed(&self, word: usize, first: u32, width: u32) -> i32 {
        extend(self.unsigned(word, first, width), width)
    }

    /// A field whose most significant `high` bits end word `word`'s data
    /// and whose `low` bits start the next's.
    const fn joined(&self, word: usize, high: u32, low: u32) -> u32 {
        (self.unsigned(word, DATA_BITS + 1 - high, high) << low) | self.unsigned(word + 1, 1, low)
    }
}

/// `value`'s low `width` bits in two's complement.
const fn extend(value: u32, width: u32) -> i32 {
    let unused = u32::BITS - width;
    (value << unused).cast_signed() >> unused
}

/// `value`, which the field it came from fits.
fn narrow<T: TryFrom<i64> + Default>(value: impl Into<i64>) -> T {
    T::try_from(value.into()).unwrap_or_default()
}

/// The units a [`Scaled`] is in.  Each is a type with no values, there
/// to tell one unit from another.
pub mod unit {
    /// Seconds.
    #[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
    pub enum Second {}
    /// Seconds a second: a clock's drift.
    #[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
    pub enum SecondPerSecond {}
    /// Seconds a second squared: a clock's drift's rate.
    #[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
    pub enum SecondPerSecondSquared {}
    /// Meters.
    #[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
    pub enum Meter {}
    /// The square root of meters: of a semi-major axis.
    #[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
    pub enum RootMeter {}
    /// Radians.
    #[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
    pub enum Radian {}
    /// Semicircles: pi radians.
    #[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
    pub enum SemiCircle {}
    /// Semicircles a second.
    #[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
    pub enum SemiCirclePerSecond {}
    /// Seconds a semicircle: an ionospheric coefficient.
    #[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
    pub enum SecondPerSemiCircle {}
    /// Seconds a semicircle squared.
    #[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
    pub enum SecondPerSemiCircleSquared {}
    /// Seconds a semicircle cubed.
    #[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
    pub enum SecondPerSemiCircleCubed {}
    /// No unit: an eccentricity.
    #[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
    pub enum Ratio {}
}

/// A broadcast value: `raw` times two to the `SCALE`, in `U`.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct Scaled<T, U, const SCALE: i8> {
    /// The integer broadcast.
    pub raw: T,
    unit: PhantomData<U>,
}

impl<T, U, const SCALE: i8> Scaled<T, U, SCALE> {
    /// The power of two that scales [`Self::raw`].
    pub const SCALE: i8 = SCALE;

    /// The value broadcast as `raw`.
    #[must_use]
    pub const fn new(raw: T) -> Self {
        Self {
            raw,
            unit: PhantomData,
        }
    }
}

/// The TLM word, word 1 (section 20.3.3.1).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct Telemetry {
    /// Bits 1 to 8: [`PREAMBLE`] in a subframe received as sent.
    pub preamble: u8,
    /// Bits 9 to 22: the TLM message.
    pub message: u16,
    /// Bit 23: the integrity status flag.
    pub integrity: bool,
}

/// The handover word, word 2 (section 20.3.3.2).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct Handover {
    /// Bits 1 to 17: the truncated time of week count, in units of six
    /// seconds: the time of the next subframe's start.
    pub time_of_week: u32,
    /// Bit 18: the alert flag, the satellite's accuracy worse than its
    /// URA says.
    pub alert: bool,
    /// Bit 19: the anti-spoof flag.
    pub anti_spoof: bool,
    /// Bits 20 to 22: the subframe's ID, 1 to 5.
    pub subframe: u8,
}

/// Subframe 1 (section 20.3.3.3, Table 20-I): the satellite's clock and
/// health.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct Subframe1 {
    /// Word 3, bits 1 to 10: the week, modulo 1024.
    pub week: u16,
    /// Word 3, bits 11 and 12: the codes on L2.
    pub l2_codes: u8,
    /// Word 3, bits 13 to 16: the URA index.
    pub ura: u8,
    /// Word 3, bits 17 to 22: the satellite's health.
    pub health: u8,
    /// Word 3, bits 23 and 24, and word 8, bits 1 to 8: IODC.
    pub iodc: u16,
    /// Word 4, bit 1: the L2 P code's data is off.
    pub l2_p_data_off: bool,
    /// Word 7, bits 17 to 24: `TGD`, the group delay.
    pub group_delay: Scaled<i8, unit::Second, -31>,
    /// Word 8, bits 9 to 24: `toc`, the clock's reference time.
    pub toc: Scaled<u16, unit::Second, 4>,
    /// Word 9, bits 1 to 8: `af2`.
    pub af2: Scaled<i8, unit::SecondPerSecondSquared, -55>,
    /// Word 9, bits 9 to 24: `af1`.
    pub af1: Scaled<i16, unit::SecondPerSecond, -43>,
    /// Word 10, bits 1 to 22: `af0`.
    pub af0: Scaled<i32, unit::Second, -31>,
}

impl Subframe1 {
    fn parse(words: &Words) -> Self {
        Self {
            week: narrow(words.unsigned(3, 1, 10)),
            l2_codes: narrow(words.unsigned(3, 11, 2)),
            ura: narrow(words.unsigned(3, 13, 4)),
            health: narrow(words.unsigned(3, 17, 6)),
            iodc: narrow((words.unsigned(3, 23, 2) << 8) | words.unsigned(8, 1, 8)),
            l2_p_data_off: words.unsigned(4, 1, 1) != 0,
            group_delay: Scaled::new(narrow(words.signed(7, 17, 8))),
            toc: Scaled::new(narrow(words.unsigned(8, 9, 16))),
            af2: Scaled::new(narrow(words.signed(9, 1, 8))),
            af1: Scaled::new(narrow(words.signed(9, 9, 16))),
            af0: Scaled::new(words.signed(10, 1, 22)),
        }
    }
}

/// Subframe 2 (section 20.3.3.4, Table 20-III): the first half of the
/// ephemeris.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct Subframe2 {
    /// Word 3, bits 1 to 8: IODE.
    pub iode: u8,
    /// Word 3, bits 9 to 24: `Crs`, the orbit radius's sine correction.
    pub crs: Scaled<i16, unit::Meter, -5>,
    /// Word 4, bits 1 to 16: delta n, the mean motion's difference from
    /// the computed value.
    pub mean_motion_difference: Scaled<i16, unit::SemiCirclePerSecond, -43>,
    /// Words 4 and 5: `M0`, the mean anomaly at the reference time.
    pub mean_anomaly: Scaled<i32, unit::SemiCircle, -31>,
    /// Word 6, bits 1 to 16: `Cuc`, the argument of latitude's cosine
    /// correction.
    pub cuc: Scaled<i16, unit::Radian, -29>,
    /// Words 6 and 7: `e`, the eccentricity.
    pub eccentricity: Scaled<u32, unit::Ratio, -33>,
    /// Word 8, bits 1 to 16: `Cus`, the argument of latitude's sine
    /// correction.
    pub cus: Scaled<i16, unit::Radian, -29>,
    /// Words 8 and 9: the square root of the semi-major axis.
    pub root_semi_major_axis: Scaled<u32, unit::RootMeter, -19>,
    /// Word 10, bits 1 to 16: `toe`, the ephemeris's reference time.
    pub toe: Scaled<u16, unit::Second, 4>,
    /// Word 10, bit 17: the fit interval flag.
    pub fit_interval: bool,
    /// Word 10, bits 18 to 22: the age of data offset, in units of 900
    /// seconds.
    pub age_of_data_offset: u8,
}

impl Subframe2 {
    fn parse(words: &Words) -> Self {
        Self {
            iode: narrow(words.unsigned(3, 1, 8)),
            crs: Scaled::new(narrow(words.signed(3, 9, 16))),
            mean_motion_difference: Scaled::new(narrow(words.signed(4, 1, 16))),
            mean_anomaly: Scaled::new(words.joined(4, 8, 24).cast_signed()),
            cuc: Scaled::new(narrow(words.signed(6, 1, 16))),
            eccentricity: Scaled::new(words.joined(6, 8, 24)),
            cus: Scaled::new(narrow(words.signed(8, 1, 16))),
            root_semi_major_axis: Scaled::new(words.joined(8, 8, 24)),
            toe: Scaled::new(narrow(words.unsigned(10, 1, 16))),
            fit_interval: words.unsigned(10, 17, 1) != 0,
            age_of_data_offset: narrow(words.unsigned(10, 18, 5)),
        }
    }
}

/// Subframe 3 (section 20.3.3.4, Table 20-III): the second half of the
/// ephemeris.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct Subframe3 {
    /// Word 3, bits 1 to 16: `Cic`, the inclination's cosine
    /// correction.
    pub cic: Scaled<i16, unit::Radian, -29>,
    /// Words 3 and 4: `OMEGA0`, the longitude of the ascending node at
    /// the start of the week.
    pub right_ascension: Scaled<i32, unit::SemiCircle, -31>,
    /// Word 5, bits 1 to 16: `Cis`, the inclination's sine correction.
    pub cis: Scaled<i16, unit::Radian, -29>,
    /// Words 5 and 6: `i0`, the inclination at the reference time.
    pub inclination: Scaled<i32, unit::SemiCircle, -31>,
    /// Word 7, bits 1 to 16: `Crc`, the orbit radius's cosine
    /// correction.
    pub crc: Scaled<i16, unit::Meter, -5>,
    /// Words 7 and 8: `omega`, the argument of perigee.
    pub perigee: Scaled<i32, unit::SemiCircle, -31>,
    /// Word 9: `OMEGADOT`, the rate of right ascension.
    pub right_ascension_rate: Scaled<i32, unit::SemiCirclePerSecond, -43>,
    /// Word 10, bits 1 to 8: IODE.
    pub iode: u8,
    /// Word 10, bits 9 to 22: `IDOT`, the rate of inclination.
    pub inclination_rate: Scaled<i16, unit::SemiCirclePerSecond, -43>,
}

impl Subframe3 {
    fn parse(words: &Words) -> Self {
        Self {
            cic: Scaled::new(narrow(words.signed(3, 1, 16))),
            right_ascension: Scaled::new(words.joined(3, 8, 24).cast_signed()),
            cis: Scaled::new(narrow(words.signed(5, 1, 16))),
            inclination: Scaled::new(words.joined(5, 8, 24).cast_signed()),
            crc: Scaled::new(narrow(words.signed(7, 1, 16))),
            perigee: Scaled::new(words.joined(7, 8, 24).cast_signed()),
            right_ascension_rate: Scaled::new(words.signed(9, 1, 24)),
            iode: narrow(words.unsigned(10, 1, 8)),
            inclination_rate: Scaled::new(narrow(words.signed(10, 9, 14))),
        }
    }
}

/// A satellite's almanac, a page of subframe 4 or 5 (section
/// 20.3.3.5.1.2, Table 20-VI).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct Almanac {
    /// Word 3, bits 3 to 8: the satellite, 1 to 32.
    pub satellite: u8,
    /// Word 3, bits 9 to 24: `e`, the eccentricity.
    pub eccentricity: Scaled<u16, unit::Ratio, -21>,
    /// Word 4, bits 1 to 8: `toa`, the almanac's reference time.
    pub time_of_almanac: Scaled<u8, unit::Second, 12>,
    /// Word 4, bits 9 to 24: delta i, the inclination less 0.3
    /// semicircles.
    pub inclination_offset: Scaled<i16, unit::SemiCircle, -19>,
    /// Word 5, bits 1 to 16: `OMEGADOT`, the rate of right ascension.
    pub right_ascension_rate: Scaled<i16, unit::SemiCirclePerSecond, -38>,
    /// Word 5, bits 17 to 24: the satellite's health.
    pub health: u8,
    /// Word 6: the square root of the semi-major axis.
    pub root_semi_major_axis: Scaled<u32, unit::RootMeter, -11>,
    /// Word 7: `OMEGA0`, the longitude of the ascending node.
    pub right_ascension: Scaled<i32, unit::SemiCircle, -23>,
    /// Word 8: `omega`, the argument of perigee.
    pub perigee: Scaled<i32, unit::SemiCircle, -23>,
    /// Word 9: `M0`, the mean anomaly.
    pub mean_anomaly: Scaled<i32, unit::SemiCircle, -23>,
    /// Word 10, bits 1 to 8 and 20 to 22: `af0`.
    pub af0: Scaled<i16, unit::Second, -20>,
    /// Word 10, bits 9 to 19: `af1`.
    pub af1: Scaled<i16, unit::SecondPerSecond, -38>,
}

impl Almanac {
    fn parse(words: &Words) -> Self {
        let af0 = (words.unsigned(10, 1, 8) << 3) | words.unsigned(10, 20, 3);
        Self {
            satellite: narrow(words.unsigned(3, 3, 6)),
            eccentricity: Scaled::new(narrow(words.unsigned(3, 9, 16))),
            time_of_almanac: Scaled::new(narrow(words.unsigned(4, 1, 8))),
            inclination_offset: Scaled::new(narrow(words.signed(4, 9, 16))),
            right_ascension_rate: Scaled::new(narrow(words.signed(5, 1, 16))),
            health: narrow(words.unsigned(5, 17, 8)),
            root_semi_major_axis: Scaled::new(words.unsigned(6, 1, 24)),
            right_ascension: Scaled::new(words.signed(7, 1, 24)),
            perigee: Scaled::new(words.signed(8, 1, 24)),
            mean_anomaly: Scaled::new(words.signed(9, 1, 24)),
            af0: Scaled::new(narrow(extend(af0, 11))),
            af1: Scaled::new(narrow(words.signed(10, 9, 11))),
        }
    }
}

/// Subframe 5, page 25 (sections 20.3.3.5.1.3 and 20.3.3.5.1.5): the
/// almanac's time and week, and the health of satellites 1 to 24.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct AlmanacHealth {
    /// Word 3, bits 9 to 16: `toa`, the almanac's reference time.
    pub time_of_almanac: Scaled<u8, unit::Second, 12>,
    /// Word 3, bits 17 to 24: `WNa`, the almanac's week, modulo 256.
    pub week: u8,
    /// Words 4 to 9: each satellite's six bits of health, satellite 1
    /// first.
    pub health: [u8; HEALTH_PAGE_SATELLITES],
}

/// `count` fields of `width` bits from word 3, bit `first`, on through
/// each word's data bits and into the next's: the six-bit health and
/// four-bit configurations of the pages 25.
fn packed<const N: usize>(words: &Words, word: usize, first: u32, width: u32) -> [u8; N] {
    let mut fields = [0; N];
    let (mut word, mut first) = (word, first);
    for field in &mut fields {
        if first + width > DATA_BITS + 1 {
            word += 1;
            first = 1;
        }
        *field = narrow(words.unsigned(word, first, width));
        first += width;
    }
    fields
}

impl AlmanacHealth {
    fn parse(words: &Words) -> Self {
        Self {
            time_of_almanac: Scaled::new(narrow(words.unsigned(3, 9, 8))),
            week: narrow(words.unsigned(3, 17, 8)),
            health: packed(words, 4, 1, HEALTH_BITS),
        }
    }
}

/// Subframe 4, page 25 (sections 20.3.3.5.1.3 and 20.3.3.5.1.4): each
/// satellite's anti-spoof flag and configuration, and the health of
/// satellites 25 to 32.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct Configurations {
    /// Words 3 to 8: each satellite's four bits, satellite 1 first:
    /// the anti-spoof flag in the high bit, its configuration in the
    /// low three.
    pub configurations: [u8; CONFIGURED_SATELLITES],
    /// Words 8 to 10: the six bits of health of satellites 25 to 32.
    pub health: [u8; LATE_SATELLITES],
}

impl Configurations {
    fn parse(words: &Words) -> Self {
        // Satellite 25's ends word 8; the rest fill words 9 and 10.
        let mut health = [0; LATE_SATELLITES];
        let (first, rest) = health.split_at_mut(1);
        first[0] = narrow(words.unsigned(8, 19, HEALTH_BITS));
        rest.copy_from_slice(&packed::<{ LATE_SATELLITES - 1 }>(words, 9, 1, HEALTH_BITS));
        Self {
            configurations: packed(words, 3, 9, CONFIGURATION_BITS),
            health,
        }
    }
}

/// Subframe 4, page 18 (sections 20.3.3.5.1.6 and 20.3.3.5.1.7, Tables
/// 20-IX and 20-X): the ionosphere's coefficients and UTC.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct IonosphereUtc {
    /// Word 3, bits 9 to 16: `alpha0`.
    pub alpha0: Scaled<i8, unit::Second, -30>,
    /// Word 3, bits 17 to 24: `alpha1`.
    pub alpha1: Scaled<i8, unit::SecondPerSemiCircle, -27>,
    /// Word 4, bits 1 to 8: `alpha2`.
    pub alpha2: Scaled<i8, unit::SecondPerSemiCircleSquared, -24>,
    /// Word 4, bits 9 to 16: `alpha3`.
    pub alpha3: Scaled<i8, unit::SecondPerSemiCircleCubed, -24>,
    /// Word 4, bits 17 to 24: `beta0`.
    pub beta0: Scaled<i8, unit::Second, 11>,
    /// Word 5, bits 1 to 8: `beta1`.
    pub beta1: Scaled<i8, unit::SecondPerSemiCircle, 14>,
    /// Word 5, bits 9 to 16: `beta2`.
    pub beta2: Scaled<i8, unit::SecondPerSemiCircleSquared, 16>,
    /// Word 5, bits 17 to 24: `beta3`.
    pub beta3: Scaled<i8, unit::SecondPerSemiCircleCubed, 16>,
    /// Word 6: `A1`, UTC's drift from GPS time.
    pub a1: Scaled<i32, unit::SecondPerSecond, -50>,
    /// Words 7 and 8: `A0`, UTC's offset from GPS time.
    pub a0: Scaled<i32, unit::Second, -30>,
    /// Word 8, bits 9 to 16: `tot`, the UTC data's reference time.
    pub reference_time: Scaled<u8, unit::Second, 12>,
    /// Word 8, bits 17 to 24: `WNt`, its week, modulo 256.
    pub reference_week: u8,
    /// Word 9, bits 1 to 8: delta `tLS`, GPS less UTC.
    pub gps_less_utc: Seconds<i8>,
    /// Word 9, bits 9 to 16: `WNLSF`, the week of the leap second,
    /// modulo 256.
    pub leap_week: u8,
    /// Word 9, bits 17 to 24: `DN`, the day of that week it ends, 1 to
    /// 7.
    pub leap_day: u8,
    /// Word 10, bits 1 to 8: delta `tLSF`, GPS less UTC after it.
    pub gps_less_utc_after: Seconds<i8>,
}

impl IonosphereUtc {
    fn parse(words: &Words) -> Self {
        let byte = |word, first| narrow::<i8>(words.signed(word, first, 8));
        Self {
            alpha0: Scaled::new(byte(3, 9)),
            alpha1: Scaled::new(byte(3, 17)),
            alpha2: Scaled::new(byte(4, 1)),
            alpha3: Scaled::new(byte(4, 9)),
            beta0: Scaled::new(byte(4, 17)),
            beta1: Scaled::new(byte(5, 1)),
            beta2: Scaled::new(byte(5, 9)),
            beta3: Scaled::new(byte(5, 17)),
            a1: Scaled::new(words.signed(6, 1, 24)),
            a0: Scaled::new(
                ((words.unsigned(7, 1, 24) << 8) | words.unsigned(8, 1, 8)).cast_signed(),
            ),
            reference_time: Scaled::new(narrow(words.unsigned(8, 9, 8))),
            reference_week: narrow(words.unsigned(8, 17, 8)),
            gps_less_utc: Seconds(byte(9, 1)),
            leap_week: narrow(words.unsigned(9, 9, 8)),
            leap_day: narrow(words.unsigned(9, 17, 8)),
            gps_less_utc_after: Seconds(byte(10, 1)),
        }
    }
}

/// What a subframe after its TLM and handover words carries.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum Body {
    /// Subframe 1.
    Subframe1(Subframe1),
    /// Subframe 2.
    Subframe2(Subframe2),
    /// Subframe 3.
    Subframe3(Subframe3),
    /// A page of subframe 4 or 5 of SV ID 1 to 32.
    Almanac(Almanac),
    /// Subframe 5, page 25.
    AlmanacHealth(AlmanacHealth),
    /// Subframe 4, page 18.
    IonosphereUtc(IonosphereUtc),
    /// Subframe 4, page 25.
    Configurations(Configurations),
    /// Any other page of subframe 4 or 5, by its SV ID (word 3, bits 3
    /// to 8).
    Page(u8),
}

/// A subframe decoded (section 20.3.3).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct Subframe {
    /// Word 1.
    pub telemetry: Telemetry,
    /// Word 2.
    pub handover: Handover,
    /// For subframes 4 and 5, word 3, bits 1 and 2: the data ID, `01`
    /// for this signal's data; 0 for the others.
    pub data_id: u8,
    /// The rest.
    pub body: Body,
}

/// The subframes of subframe 4 and 5's pages.
const SUBFRAME_FOUR: u8 = 4;
const SUBFRAME_FIVE: u8 = 5;

impl Subframe {
    /// Decodes a subframe's words.  `None` for a subframe ID outside 1
    /// to 5.  The preamble is reported, not checked: the receiver sends
    /// a subframe only once its parity checks have passed and has
    /// righted inverted data (UBX-13003221 R28, section 10.1, p. 30),
    /// so a preamble other than [`PREAMBLE`] would be the receiver's
    /// fault, not the link's, and refusing the subframe would hide it.
    /// [`Telemetry::preamble`] is for a caller that wants to check.
    #[must_use]
    pub fn decode(words: &Words) -> Option<Self> {
        let handover = Handover {
            time_of_week: words.unsigned(2, 1, 17),
            alert: words.unsigned(2, 18, 1) != 0,
            anti_spoof: words.unsigned(2, 19, 1) != 0,
            subframe: narrow(words.unsigned(2, 20, 3)),
        };
        let page: u8 = narrow(words.unsigned(3, 3, 6));
        let body = match (handover.subframe, page) {
            (1, _) => Body::Subframe1(Subframe1::parse(words)),
            (2, _) => Body::Subframe2(Subframe2::parse(words)),
            (3, _) => Body::Subframe3(Subframe3::parse(words)),
            (SUBFRAME_FOUR | SUBFRAME_FIVE, page) if ALMANAC_PAGES.contains(&page) => {
                Body::Almanac(Almanac::parse(words))
            }
            (SUBFRAME_FIVE, PAGE_ALMANAC_HEALTH) => {
                Body::AlmanacHealth(AlmanacHealth::parse(words))
            }
            (SUBFRAME_FOUR, PAGE_IONOSPHERE_UTC) => {
                Body::IonosphereUtc(IonosphereUtc::parse(words))
            }
            (SUBFRAME_FOUR, PAGE_CONFIGURATIONS) => {
                Body::Configurations(Configurations::parse(words))
            }
            (SUBFRAME_FOUR | SUBFRAME_FIVE, page) => Body::Page(page),
            _ => return None,
        };
        let paged = matches!(handover.subframe, SUBFRAME_FOUR | SUBFRAME_FIVE);
        Some(Self {
            telemetry: Telemetry {
                preamble: narrow(words.unsigned(1, 1, 8)),
                message: narrow(words.unsigned(1, 9, 14)),
                integrity: words.unsigned(1, 23, 1) != 0,
            },
            handover,
            data_id: if paged {
                narrow(words.unsigned(3, 1, 2))
            } else {
                0
            },
            body,
        })
    }
}

#[cfg(test)]
pub(crate) mod tests;
