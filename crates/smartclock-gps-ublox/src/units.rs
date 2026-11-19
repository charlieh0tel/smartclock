//! Units as types, all integers, each at a UBX field's width and
//! resolution.
//!
//! A field the document gives as floating point (`R4`, `R8`) is kept
//! as its bits, [`F32Bits`] and [`F64Bits`]: nothing here does
//! floating-point arithmetic, which a microcontroller without a
//! floating-point unit pays for in code, and a caller that wants the
//! number has `f32::from_bits` and `f64::from_bits`.

/// A count of seconds.
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash, Default)]
pub struct Seconds<T>(pub T);

/// A count of milliseconds.
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash, Default)]
pub struct Milliseconds<T>(pub T);

/// A count of nanoseconds.
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash, Default)]
pub struct Nanoseconds<T>(pub T);

/// An angle in degrees.
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash, Default)]
pub struct Degrees<T>(pub T);

/// A length in decimeters: a pseudorange residual.
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash, Default)]
pub struct Decimeters(pub i16);

/// A carrier-to-noise density ratio in dB-Hz: a satellite's signal.
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash, Default)]
pub struct DecibelHertz(pub u8);

/// A length in millimeters.
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash, Default)]
pub struct Millimeters<T>(pub T);

/// A speed in millimeters a second.
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash, Default)]
pub struct MillimetersPerSecond<T>(pub T);

/// A length in centimeters.
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash, Default)]
pub struct Centimeters<T>(pub T);

/// A speed in centimeters a second.
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash, Default)]
pub struct CentimetersPerSecond<T>(pub T);

/// An angle in units of 1e-7 degree: a latitude or longitude.
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash, Default)]
pub struct HundredNanodegrees(pub i32);

/// An angle in units of 1e-5 degree: a heading.
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash, Default)]
pub struct TenMicrodegrees<T>(pub T);

/// An angle in hundredths of a degree: a magnetic declination.
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash, Default)]
pub struct HundredthDegrees<T>(pub T);

/// A dimensionless figure in hundredths: a dilution of precision.
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash, Default)]
pub struct Hundredths(pub u16);

/// A count of picoseconds.
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash, Default)]
pub struct Picoseconds(pub i32);

/// A rate in nanoseconds a second: a clock's drift.
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash, Default)]
pub struct NanosecondsPerSecond(pub i32);

/// A rate in picoseconds a second: a frequency's accuracy.
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash, Default)]
pub struct PicosecondsPerSecond(pub u32);

/// A fraction of a millisecond in units of 2^-32 ms.
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash, Default)]
pub struct SubMilliseconds(pub u32);

/// An `R4` field's bits: `f32::from_bits` gives its value.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Default)]
pub struct F32Bits(pub u32);

/// An `R8` field's bits: `f64::from_bits` gives its value.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Default)]
pub struct F64Bits(pub u64);
