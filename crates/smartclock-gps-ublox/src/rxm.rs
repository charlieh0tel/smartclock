//! The receiver manager's messages, `UBX-RXM-*`: raw measurements and
//! the satellites' own navigation data.

use smartclock_gps::units::DecibelHertz;
use smartclock_gps::units::Milliseconds;
use smartclock_gps::units::Seconds;

use crate::fields::Fields;
use crate::fields::bit_field;
use crate::message::SYSTEM_GPS;
use crate::subframe::WORDS;
use crate::subframe::Words;
use crate::units::F32Bits;
use crate::units::F64Bits;

/// `RXM-RAWX`'s payload before its first measurement.
pub(crate) const RAWX_HEADER: usize = 16;
/// One measurement's block of `RXM-RAWX`.
pub(crate) const RAWX_BLOCK: usize = 32;
/// `RXM-SFRBX`'s payload before its first word.
pub(crate) const SFRBX_HEADER: usize = 8;
/// One word of `RXM-SFRBX`.
pub(crate) const SFRBX_WORD: usize = 4;

/// `RXM-RAWX`'s `recStat` bits.
const RAWX_LEAP_KNOWN: u32 = 0;
const RAWX_CLOCK_RESET: u32 = 1;
/// A standard deviation's field: an index in bits 0 to 3.
const DEVIATION_WIDTH: u32 = 4;
/// `RXM-RAWX`'s `trkStat` bits.
const RAWX_PSEUDORANGE_VALID: u32 = 0;
const RAWX_CARRIER_PHASE_VALID: u32 = 1;
const RAWX_HALF_CYCLE_VALID: u32 = 2;
const RAWX_HALF_CYCLE_SUBTRACTED: u32 = 3;

/// The length of an `RXM-RAWX` payload of `measurements`.
#[must_use]
pub const fn rxm_rawx_length(measurements: usize) -> usize {
    RAWX_HEADER + RAWX_BLOCK * measurements
}

/// `UBX-RXM-RAWX`'s header (section 32.18.4, pp. 409-411), before its
/// measurements.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct RxmRawxHeader {
    /// `rcvTow`, in seconds: the receiver's time of week of the
    /// measurements.
    pub receiver_time_of_week: F64Bits,
    /// `week`: the receiver's GPS week.
    pub week: u16,
    /// `leapS`: GPS less UTC.
    pub gps_less_utc: Seconds<i8>,
    /// `numMeas`: how many measurements' blocks follow.
    pub measurements: u8,
    /// `recStat` bit 0, `leapSec`: the leap seconds are known.
    pub leap_seconds_known: bool,
    /// `recStat` bit 1, `clkReset`: the clock was reset, and the
    /// carrier phases are discontinuous.
    pub clock_reset: bool,
}

impl RxmRawxHeader {
    /// The header of a payload with `blocks` measurements after it.
    /// `None` for a `numMeas` that is not `blocks`.
    pub(crate) fn parse(fields: Fields<'_>, blocks: usize) -> Option<Self> {
        let measurements = fields.u8(11);
        (usize::from(measurements) == blocks).then(|| Self {
            receiver_time_of_week: F64Bits(fields.u64(0)),
            week: fields.u16(8),
            gps_less_utc: Seconds(fields.i8(10)),
            measurements,
            leap_seconds_known: fields.bit(12, RAWX_LEAP_KNOWN),
            clock_reset: fields.bit(12, RAWX_CLOCK_RESET),
        })
    }
}

/// One measurement's block of `UBX-RXM-RAWX` (pp. 410-411), every
/// field.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Measurement {
    /// `prMes`, in meters: the pseudorange.
    pub pseudorange: F64Bits,
    /// `cpMes`, in cycles: the carrier phase.
    pub carrier_phase: F64Bits,
    /// `doMes`, in hertz: the Doppler, positive for an approaching
    /// satellite.
    pub doppler: F32Bits,
    /// `gnssId`: which system's satellite, [`SYSTEM_GPS`] for GPS.
    pub system: u8,
    /// `svId`: its number in that system.
    pub id: u8,
    /// `freqId`: a GLONASS satellite's frequency slot plus 7.
    pub frequency: u8,
    /// `locktime`: how long the carrier phase has been locked, up to
    /// 64500 ms.
    pub lock_time: Milliseconds<u16>,
    /// `cno`: the signal.
    pub signal: DecibelHertz,
    /// `prStdev` bits 0 to 3: the pseudorange's standard deviation is
    /// 0.01 m times two to this.
    pub pseudorange_deviation: u8,
    /// `cpStdev` bits 0 to 3: the carrier phase's is 0.004 cycles times
    /// this.
    pub carrier_phase_deviation: u8,
    /// `doStdev` bits 0 to 3: the Doppler's is 0.002 Hz times two to
    /// this.
    pub doppler_deviation: u8,
    /// `trkStat` bit 0, `prValid`.
    pub pseudorange_valid: bool,
    /// `trkStat` bit 1, `cpValid`.
    pub carrier_phase_valid: bool,
    /// `trkStat` bit 2, `halfCyc`: the half-cycle ambiguity is
    /// resolved.
    pub half_cycle_valid: bool,
    /// `trkStat` bit 3, `subHalfCyc`: half a cycle was subtracted from
    /// the phase.
    pub half_cycle_subtracted: bool,
}

impl Measurement {
    pub(crate) fn parse(fields: Fields<'_>) -> Self {
        let deviation = |offset| bit_field(u32::from(fields.u8(offset)), 0, DEVIATION_WIDTH);
        Self {
            pseudorange: F64Bits(fields.u64(0)),
            carrier_phase: F64Bits(fields.u64(8)),
            doppler: F32Bits(fields.u32(16)),
            system: fields.u8(20),
            id: fields.u8(21),
            frequency: fields.u8(23),
            lock_time: Milliseconds(fields.u16(24)),
            signal: DecibelHertz(fields.u8(26)),
            pseudorange_deviation: deviation(27),
            carrier_phase_deviation: deviation(28),
            doppler_deviation: deviation(29),
            pseudorange_valid: fields.bit(30, RAWX_PSEUDORANGE_VALID),
            carrier_phase_valid: fields.bit(30, RAWX_CARRIER_PHASE_VALID),
            half_cycle_valid: fields.bit(30, RAWX_HALF_CYCLE_VALID),
            half_cycle_subtracted: fields.bit(30, RAWX_HALF_CYCLE_SUBTRACTED),
        }
    }
}

/// `UBX-RXM-RAWX` (section 32.18.4, p. 409), whole.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct RxmRawx<'a> {
    /// Its header.
    pub header: RxmRawxHeader,
    /// The measurements' blocks.
    blocks: &'a [u8],
}

impl<'a> RxmRawx<'a> {
    /// `None` as for the header.
    pub(crate) fn parse(payload: &'a [u8]) -> Option<Self> {
        let blocks = payload.get(RAWX_HEADER..)?;
        Some(Self {
            header: RxmRawxHeader::parse(Fields(payload), blocks.len() / RAWX_BLOCK)?,
            blocks,
        })
    }

    /// The measurements, in the message's order.
    pub fn measurements(&self) -> impl Iterator<Item = Measurement> + use<'a> {
        self.blocks
            .as_chunks::<RAWX_BLOCK>()
            .0
            .iter()
            .map(|block| Measurement::parse(Fields(block)))
    }
}

/// `UBX-RXM-SFRBX` (section 32.18.7, pp. 419-420): one subframe of a satellite's
/// navigation data, as broadcast.  The layout is the same in both of
/// the document's versions but for bytes 2 and 5, reserved in the
/// first and `sigId` and `chn` in the second, so either is taken.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct RxmSfrbx<'a> {
    /// `gnssId`: which system's satellite, [`SYSTEM_GPS`] for GPS.
    pub system: u8,
    /// `svId`: its number in that system.
    pub id: u8,
    /// `freqId`: a GLONASS satellite's frequency slot plus 7.
    pub frequency: u8,
    /// `sigId`: the signal, in the second version; 0 in the first.
    pub signal: u8,
    /// `numWords`: how many words follow.
    pub count: u8,
    /// `chn`: the tracking channel it came on, in the second version;
    /// 0 in the first.
    pub channel: u8,
    /// `version`.
    pub version: u8,
    /// The words, `dwrd`, four bytes each.
    words: &'a [u8],
}

impl<'a> RxmSfrbx<'a> {
    /// `None` for a `numWords` that does not match the payload's
    /// length.
    pub(crate) fn parse(payload: &'a [u8]) -> Option<Self> {
        let fields = Fields(payload);
        let words = payload.get(SFRBX_HEADER..)?;
        let count = fields.u8(4);
        (usize::from(count) * SFRBX_WORD == words.len()).then(|| Self {
            system: fields.u8(0),
            id: fields.u8(1),
            signal: fields.u8(2),
            frequency: fields.u8(3),
            count,
            channel: fields.u8(5),
            version: fields.u8(6),
            words,
        })
    }

    /// The words, in the order broadcast, each as the message gives it.
    pub fn words(&self) -> impl Iterator<Item = u32> + use<'a> {
        self.words
            .as_chunks::<SFRBX_WORD>()
            .0
            .iter()
            .map(|&word| u32::from_le_bytes(word))
    }

    /// A GPS L1 C/A subframe's ten words, for
    /// [`Subframe::decode`](crate::subframe::Subframe::decode): `None`
    /// unless the satellite is a GPS one and there are ten words.
    /// The receiver sends a subframe only once its parity checks have
    /// passed, with the data bits it had to invert inverted (section
    /// 10.1, p. 30), and of GPS gives only L1 C/A's (section 10.9,
    /// p. 34).
    #[must_use]
    pub fn gps_words(&self) -> Option<Words> {
        let words: &[[u8; SFRBX_WORD]; WORDS] = self.words.as_chunks().0.try_into().ok()?;
        (self.system == SYSTEM_GPS).then(|| Words::new(words.map(u32::from_le_bytes)))
    }
}
