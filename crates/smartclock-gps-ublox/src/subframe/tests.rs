//! Subframes built from IS-GPS-200N's Figure 20-1, each field placed
//! by its bit numbers in the subframe as a whole, 1 to 300, not by the
//! word and bit the decoder reads: the two numberings must agree.
//! Each word's parity and the two bits above it are filled with
//! something, which the decoder must not read.

use super::Almanac;
use super::AlmanacHealth;
use super::Body;
use super::Configurations;
use super::Handover;
use super::IonosphereUtc;
use super::PREAMBLE;
use super::Scaled;
use super::Subframe;
use super::Subframe1;
use super::Subframe2;
use super::Subframe3;
use super::Telemetry;
use super::WORDS;
use super::Words;
use smartclock_gps::units::Seconds;

/// A subframe's bits, set by their numbers in the subframe.
struct Bits([u32; WORDS]);

impl Bits {
    /// Bits a decoder must not read: the parity of each word, and the
    /// two bits above its 30.
    fn new() -> Self {
        Self([0xc000_002a; WORDS])
    }

    /// `width` bits of `value` from subframe bit `first`, 1 the first
    /// sent; within one word.
    fn set(&mut self, first: u32, width: u32, value: i64) -> &mut Self {
        let word = usize::try_from((first - 1) / 30).unwrap();
        let bit = (first - 1) % 30 + 1;
        assert!(bit + width - 1 <= 24, "field at {first} runs past the data");
        let mask = (1_u32 << width) - 1;
        let shift = 31 - bit - width;
        let bits = u32::try_from(value & i64::from(mask)).unwrap();
        self.0[word] = (self.0[word] & !(mask << shift)) | (bits << shift);
        self
    }

    /// TLM and HOW: the preamble, a TLM message, the integrity flag,
    /// a time of week, the alert flag and the subframe's ID.
    fn head(&mut self, subframe: i64) -> &mut Self {
        self.set(1, 8, i64::from(PREAMBLE))
            .set(9, 14, 0x1555)
            .set(23, 1, 1)
            .set(31, 17, 100_799)
            .set(48, 1, 1)
            .set(49, 1, 0)
            .set(50, 3, subframe)
    }

    /// A page of subframe 4 or 5: data ID `01` and its SV ID.
    fn page(&mut self, subframe: i64, id: i64) -> &mut Self {
        self.head(subframe).set(61, 2, 1).set(63, 6, id)
    }

    fn decode(&self) -> Option<Subframe> {
        Subframe::decode(&Words::new(self.0))
    }
}

/// The body of a subframe that decodes, with its head checked.
fn body(bits: &Bits, subframe: u8) -> Body {
    let decoded = bits.decode().unwrap();
    assert_eq!(
        decoded.telemetry,
        Telemetry {
            preamble: PREAMBLE,
            message: 0x1555,
            integrity: true,
        }
    );
    assert_eq!(
        decoded.handover,
        Handover {
            time_of_week: 100_799,
            alert: true,
            anti_spoof: false,
            subframe,
        }
    );
    decoded.body
}

#[test]
fn subframe_1_clock_and_health() {
    let mut bits = Bits::new();
    bits.head(1)
        .set(61, 10, 905)
        .set(71, 2, 1)
        .set(73, 4, 3)
        .set(77, 6, 42)
        .set(83, 2, 0b10)
        .set(91, 1, 1)
        .set(197, 8, -12)
        .set(211, 8, 0xa5)
        .set(219, 16, 22_500)
        .set(241, 8, -3)
        .set(249, 16, -1_234)
        .set(271, 22, -123_456);
    assert_eq!(
        body(&bits, 1),
        Body::Subframe1(Subframe1 {
            week: 905,
            l2_codes: 1,
            ura: 3,
            health: 42,
            iodc: 0x2a5,
            l2_p_data_off: true,
            group_delay: Scaled::new(-12),
            toc: Scaled::new(22_500),
            af2: Scaled::new(-3),
            af1: Scaled::new(-1_234),
            af0: Scaled::new(-123_456),
        })
    );
    assert_eq!(bits.decode().unwrap().data_id, 0);
}

#[test]
fn subframe_2_ephemeris() {
    let mean_anomaly: i32 = -1_500_000_000;
    let eccentricity: u32 = 0x0123_4567;
    let root_axis: u32 = 2_702_000_000;
    let mut bits = Bits::new();
    bits.head(2)
        .set(61, 8, 0x5a)
        .set(69, 16, -1_000)
        .set(91, 16, 12_345)
        .set(107, 8, i64::from(mean_anomaly >> 24))
        .set(121, 24, i64::from(mean_anomaly))
        .set(151, 16, -2_000)
        .set(167, 8, i64::from(eccentricity >> 24))
        .set(181, 24, i64::from(eccentricity))
        .set(211, 16, 3_000)
        .set(227, 8, i64::from(root_axis >> 24))
        .set(241, 24, i64::from(root_axis))
        .set(271, 16, 22_500)
        .set(287, 1, 1)
        .set(288, 5, 31);
    assert_eq!(
        body(&bits, 2),
        Body::Subframe2(Subframe2 {
            iode: 0x5a,
            crs: Scaled::new(-1_000),
            mean_motion_difference: Scaled::new(12_345),
            mean_anomaly: Scaled::new(mean_anomaly),
            cuc: Scaled::new(-2_000),
            eccentricity: Scaled::new(eccentricity),
            cus: Scaled::new(3_000),
            root_semi_major_axis: Scaled::new(root_axis),
            toe: Scaled::new(22_500),
            fit_interval: true,
            age_of_data_offset: 31,
        })
    );
}

#[test]
fn subframe_3_ephemeris() {
    let right_ascension: i32 = -2_000_000_000;
    let inclination: i32 = 650_000_000;
    let perigee: i32 = -1;
    let mut bits = Bits::new();
    bits.head(3)
        .set(61, 16, -5)
        .set(77, 8, i64::from(right_ascension >> 24))
        .set(91, 24, i64::from(right_ascension))
        .set(121, 16, 7)
        .set(137, 8, i64::from(inclination >> 24))
        .set(151, 24, i64::from(inclination))
        .set(181, 16, 8_000)
        .set(197, 8, i64::from(perigee >> 24))
        .set(211, 24, i64::from(perigee))
        .set(241, 24, -8_000)
        .set(271, 8, 0x5a)
        .set(279, 14, -100);
    assert_eq!(
        body(&bits, 3),
        Body::Subframe3(Subframe3 {
            cic: Scaled::new(-5),
            right_ascension: Scaled::new(right_ascension),
            cis: Scaled::new(7),
            inclination: Scaled::new(inclination),
            crc: Scaled::new(8_000),
            perigee: Scaled::new(perigee),
            right_ascension_rate: Scaled::new(-8_000),
            iode: 0x5a,
            inclination_rate: Scaled::new(-100),
        })
    );
}

/// Satellite 27's almanac, on a page of subframe 4; and the same page
/// in subframe 5.
#[test]
fn an_almanac_page() {
    let af0: i64 = -700;
    let mut bits = Bits::new();
    bits.page(4, 27)
        .set(69, 16, 12_345)
        .set(91, 8, 144)
        .set(99, 16, -500)
        .set(121, 16, -600)
        .set(137, 8, 0xa5)
        .set(151, 24, 10_554_000)
        .set(181, 24, -4_000_000)
        .set(211, 24, 3_000_000)
        .set(241, 24, -1)
        .set(271, 8, af0 >> 3)
        .set(279, 11, -300)
        .set(290, 3, af0);
    let almanac = Almanac {
        satellite: 27,
        eccentricity: Scaled::new(12_345),
        time_of_almanac: Scaled::new(144),
        inclination_offset: Scaled::new(-500),
        right_ascension_rate: Scaled::new(-600),
        health: 0xa5,
        root_semi_major_axis: Scaled::new(10_554_000),
        right_ascension: Scaled::new(-4_000_000),
        perigee: Scaled::new(3_000_000),
        mean_anomaly: Scaled::new(-1),
        af0: Scaled::new(-700),
        af1: Scaled::new(-300),
    };
    assert_eq!(body(&bits, 4), Body::Almanac(almanac));
    assert_eq!(bits.decode().unwrap().data_id, 1);
    bits.set(50, 3, 5);
    assert_eq!(body(&bits, 5), Body::Almanac(almanac));
}

/// Subframe 4, page 18, with the leap second of 2016-12-31: 17 seconds
/// before it and 18 after, at the end of day 7 of week 1929, whose low
/// eight bits are 137.
#[test]
fn the_ionosphere_and_utc_page() {
    let a0: i32 = -1_000_000_000;
    let mut bits = Bits::new();
    bits.page(4, 56)
        .set(69, 8, 12)
        .set(77, 8, -1)
        .set(91, 8, -2)
        .set(99, 8, 3)
        .set(107, 8, 80)
        .set(121, 8, -4)
        .set(129, 8, -128)
        .set(137, 8, 127)
        .set(151, 24, -2)
        .set(181, 24, i64::from(a0 >> 8))
        .set(211, 8, i64::from(a0))
        .set(219, 8, 144)
        .set(227, 8, 137)
        .set(241, 8, 17)
        .set(249, 8, 137)
        .set(257, 8, 7)
        .set(271, 8, 18);
    assert_eq!(
        body(&bits, 4),
        Body::IonosphereUtc(IonosphereUtc {
            alpha0: Scaled::new(12),
            alpha1: Scaled::new(-1),
            alpha2: Scaled::new(-2),
            alpha3: Scaled::new(3),
            beta0: Scaled::new(80),
            beta1: Scaled::new(-4),
            beta2: Scaled::new(-128),
            beta3: Scaled::new(127),
            a1: Scaled::new(-2),
            a0: Scaled::new(a0),
            reference_time: Scaled::new(144),
            reference_week: 137,
            gps_less_utc: Seconds(17),
            leap_week: 137,
            leap_day: 7,
            gps_less_utc_after: Seconds(18),
        })
    );
}

/// The first bit of each word's fields on a page 25, words 4 to 9 or 3
/// to 8: the subframe's bits 91, 121, ... or 61, 91, ...
fn word_start(word: u32) -> u32 {
    (word - 1) * 30 + 1
}

#[test]
fn subframe_5_page_25_health() {
    let health: [u8; 24] = core::array::from_fn(|index| u8::try_from(index * 7 % 64).unwrap());
    let mut bits = Bits::new();
    bits.page(5, 51).set(69, 8, 144).set(77, 8, 137);
    for (index, &value) in (0_u32..).zip(&health) {
        bits.set(
            word_start(4 + index / 4) + 6 * (index % 4),
            6,
            i64::from(value),
        );
    }
    assert_eq!(
        body(&bits, 5),
        Body::AlmanacHealth(AlmanacHealth {
            time_of_almanac: Scaled::new(144),
            week: 137,
            health,
        })
    );
}

#[test]
fn subframe_4_page_25_configurations() {
    let configurations: [u8; 32] =
        core::array::from_fn(|index| u8::try_from(15 - index % 16).unwrap());
    let health: [u8; 8] = [0x3f, 1, 2, 3, 4, 5, 6, 0x2a];
    let mut bits = Bits::new();
    bits.page(4, 63);
    // Satellites 1 to 4 after the SV ID; then six a word.
    for (index, &value) in (0_u32..).zip(&configurations) {
        let first = if index < 4 {
            69 + 4 * index
        } else {
            word_start(4 + (index - 4) / 6) + 4 * ((index - 4) % 6)
        };
        bits.set(first, 4, i64::from(value));
    }
    // Satellite 25 at 229, 26 to 29 at 241, 30 to 32 at 271.
    for (first, &value) in [229, 241, 247, 253, 259, 271, 277, 283].iter().zip(&health) {
        bits.set(*first, 6, i64::from(value));
    }
    assert_eq!(
        body(&bits, 4),
        Body::Configurations(Configurations {
            configurations,
            health,
        })
    );
}

/// A page of another SV ID is given by it; a subframe ID outside 1 to 5
/// is no subframe.
#[test]
fn other_pages_and_subframes() {
    let mut bits = Bits::new();
    bits.page(4, 52);
    assert_eq!(body(&bits, 4), Body::Page(52));
    // Page 25's IDs in the other subframe are no such page.
    bits.page(5, 63);
    assert_eq!(body(&bits, 5), Body::Page(63));
    bits.page(4, 51);
    assert_eq!(body(&bits, 4), Body::Page(51));
    bits.page(5, 0);
    assert_eq!(body(&bits, 5), Body::Page(0));
    for subframe in [0, 6, 7] {
        bits.head(subframe);
        assert_eq!(bits.decode(), None, "{subframe}");
    }
}

/// The preamble is given, not checked.
#[test]
fn an_inverted_preamble_is_given_as_it_came() {
    let mut bits = Bits::new();
    bits.head(1).set(1, 8, i64::from(!PREAMBLE));
    assert_eq!(bits.decode().unwrap().telemetry.preamble, !PREAMBLE);
}

/// Words are held to their 30 bits.
#[test]
fn words_keep_thirty_bits() {
    assert_eq!(Words::new([u32::MAX; WORDS]).get(), [0x3fff_ffff; WORDS]);
}
