//! A model that stands in for a Motorola Oncore: it takes the commands
//! a `SmartClock` sends and a [`Record`] each second, and writes the
//! answers and the continuous messages the receiver expects.
//!
//! An [`Engine`] is one of the Oncores the receivers hold, chosen when
//! it is made by its [`Identity`]: a six-channel VP, an eight-channel
//! VP or a UT Plus.  It answers only what the receivers' firmware
//! sends, and fills in only what that firmware decodes.  One made
//! six-channel can become an eight-channel Oncore of another identity
//! at its first `@@Ea` ([`Options::eight_on_ea`]).  One can power up
//! configured, fixing and sending unasked, as an Oncore a receiver
//! configured before it was last turned off ([`Options::saved`]), as
//! a 58503A leaves its engine
//! ([`CONFIGURATION_58503A`](saved::CONFIGURATION_58503A)) or as the
//! caller's own configuration has it.
//!
//! Whoever holds the engine owns the clock and the serial link:
//!
//! - once a second, at the 1 PPS, it calls [`Engine::second`] with that
//!   second's record;
//! - as bytes arrive from the receiver, it calls [`Engine::receive`]
//!   with them and the same record;
//! - it writes each frame the engine hands to its [`Output`] to the
//!   link.
//!
//! The engine counts the seconds it is given and sends each continuous
//! message when that count is a multiple of the message's rate, so a
//! record whose time steps neither doubles a message nor skips one.
//! The time it reports is the record's; when a record's time is the
//! same as the last one's, or it has none, the engine's own clock runs
//! on a second.  That clock starts at 12:00:00 (VP p. 5), on a date the
//! manual leaves to the firmware (VP p. 13): a VP's at 1997-01-01, as a
//! test of a real VP read; a UT Plus's, of which there is no capture,
//! at the day GPS time starts, running.  A clock `@@Ab` sets behind UTC
//! reads the day before.
//!
//! That clock, which `@@Aa` (VP p. 5) and `@@Ac` (VP p. 13) answer
//! with and set, always counts.  The time a position status gives
//! (`@@Ea`, VP p. 71), and Time RAIM's next pulse from it, is that
//! clock's while the engine is fixing, as tests of a real VP with no
//! antenna showed.  While a VP is idle it stands at the time it went
//! idle, or at its power-up time, a record's time or not; `@@Ac` sets
//! it to 12:00:00 on the date, and the counting clock with it, `@@Aa`
//! sets the time of both, and it stands at what they set.  The tests
//! went idle by `@@Cf`; that `@@Cg` 0 and a self-test, which idle it
//! too, leave it standing the same way is untested.  An idle engine
//! given a record with a time is its holder's doing, as a real idle VP
//! tracks nothing, and the date and time its standing status is then
//! labeled with, by the record's offset of GPS time from UTC, is not
//! measured.
//! A UT Plus is always fixing, and its `@@Ac` keeps the time of day.
//! `@@Aa` and `@@Ac` set nothing while the record tells the time, as
//! an Oncore tracking a satellite ignores them (VP pp. 5 and 13).  A
//! second in which the engine is to be silent is one
//! [`Engine::second`] is not called for.
//!
//! One answer is not sent at once: a self-test's, which comes six
//! seconds after its command, as a real VP's comes some seconds after
//! (VP p. 107).  [`Engine::deadline`] says when one is
//! due and [`Engine::poll`] sends it; [`Engine::second`] and
//! [`Engine::receive`] send what is due before anything else, so a
//! caller whose link can wait up to a second for it need not poll.
//!
//! What a record cannot say, the engine does not make up: with no
//! [`Dops`](crate::record::Dops) it sends zeros, and with no
//! [`Ecef`](crate::record::Ecef) position, zeros for `@@Bk`'s.
//!
//! A command is one of two kinds.  What it changes is in the bytes the
//! engine builds, and the engine does it: the time and date, the mask
//! angle, the ignore list, the position held or to start from, which
//! is reported while the record has none (VP pp. 17 and 25), a message
//! sent again at each change.  Or it changes how a receiver tracks,
//! fixes or puts out its pulse, which is the record's to show: the
//! engine keeps the value, answers with it and tells its holder, and
//! tracks and fixes as the record says.  Manual satellite selection
//! (`@@Ah` 1 with `@@Ai`'s assignments, VP pp. 29 to 32) and manual
//! altitude hold (`@@Av` 1 at `@@Au`'s height, VP pp. 58 to 61) are of
//! that kind.
//!
//! What commands have set the engine to, a holder reads as types:
//! [`Engine::settings`] gives the [`Settings`], and
//! [`Engine::receive`] returns the [`Events`] its commands made, a
//! reset to defaults and which settings changed.
//!
//! The Z3815A's Furuno GT-74 has a model of its own, [`furuno`], which
//! shares [`Output`], the [`Settings`] and the [`Events`] with this one.
//!
//! The pages cited are, as printed: VP, the VP Oncore's command
//! reference, `third_party/motorola/VPCommands.pdf`; UT, TRM0003
//! chapter 6, `third_party/motorola/oncore-ut-users-guide/ch6.pdf`;
//! M12+, `third_party/motorola/m12plus-users-guide.pdf`.

mod data;
pub mod event;
mod framer;
pub mod furuno;
pub mod identity;
pub mod saved;
pub mod settings;
mod store;

use crate::engine::data::Data;
use crate::engine::event::Events;
use crate::engine::framer::Framer;
use crate::engine::identity::Identity;
use crate::engine::identity::LINE_END;
use crate::engine::identity::Model;
use crate::engine::identity::Software;
use crate::engine::saved::ChannelCount;
use crate::engine::saved::ConfigurationError;
use crate::engine::saved::SavedConfiguration;
use crate::engine::settings::CHANNELS_MOST;
use crate::engine::settings::Height;
use crate::engine::settings::HeightKind;
use crate::engine::settings::Location;
use crate::engine::settings::OutputFormat;
use crate::engine::settings::PulseMode;
use crate::engine::settings::SatelliteSelect;
use crate::engine::settings::Settings;
use crate::engine::settings::TimeBase;
use crate::engine::store::Place;
use crate::engine::store::QUERY;
use crate::engine::store::Store;
use crate::engine::store::dop_kind_code;
use crate::engine::store::height_of;
use crate::engine::store::height_takes;
use crate::engine::store::is_setting;
use crate::engine::store::latitude_takes;
use crate::engine::store::longitude_takes;
use crate::oncore::AA_TIME_OF_DAY;
use crate::oncore::AB_GMT_CORRECTION;
use crate::oncore::AC_DATE;
use crate::oncore::AD_LATITUDE;
use crate::oncore::AE_LONGITUDE;
use crate::oncore::AF_HEIGHT;
use crate::oncore::AG_MASK_ANGLE;
use crate::oncore::AI_MANUAL_SATELLITE;
use crate::oncore::AM_IGNORE_LIST;
use crate::oncore::AO_DATUM_SELECT;
use crate::oncore::AP_USER_DATUM;
use crate::oncore::AQ_ATMOSPHERIC_CORRECTION;
use crate::oncore::AR_FIX_ALGORITHM;
use crate::oncore::AS_HOLD_POSITION;
use crate::oncore::AT_POSITION_HOLD;
use crate::oncore::AW_TIME_MODE;
use crate::oncore::AY_PULSE_OFFSET;
use crate::oncore::AZ_CABLE_DELAY;
use crate::oncore::BA_POSITION_STATUS;
use crate::oncore::BB_VISIBLE_SATELLITES;
use crate::oncore::BC_XDOP_TABLE;
use crate::oncore::BD_ALMANAC_STATUS;
use crate::oncore::BH_CORRECTION_OUTPUT;
use crate::oncore::BJ_LEAP_SECOND_PENDING;
use crate::oncore::BK_EXTENSION;
use crate::oncore::BN_TIME_RAIM;
use crate::oncore::BO_UTC_OFFSET;
use crate::oncore::CA_SELF_TEST;
use crate::oncore::CE_PSEUDORANGE_CORRECTION;
use crate::oncore::CF_SET_TO_DEFAULTS;
use crate::oncore::CG_FIX_MODE;
use crate::oncore::CI_IO_FORMAT;
use crate::oncore::CJ_RECEIVER_ID;
use crate::oncore::Channels;
use crate::oncore::EA_POSITION_STATUS;
use crate::oncore::EK_EXTENSION;
use crate::oncore::EN_TIME_RAIM;
use crate::oncore::FA_SELF_TEST;
use crate::oncore::GJ_LEAP_SECOND_PENDING;
use crate::oncore::MAX_RESPONSE;
use crate::oncore::MessageId;
use crate::oncore::OVERHEAD;
use crate::oncore::SZ_POWER_ON_FAILURE;
use crate::oncore::encode;
use crate::record::Antenna;
use crate::record::Correction;
use crate::record::Corrections;
use crate::record::Fault;
use crate::record::Fix;
use crate::record::Health;
use crate::record::RaimAbility;
use crate::record::RaimSolution;
use crate::record::Record;
use crate::record::SATELLITES;
use crate::record::Satellite;
use crate::record::SatelliteFlag;
use crate::record::Solver;
use crate::record::Tracking;
use crate::record::Ura;
use crate::record::XDOP_COMBINATIONS;
use crate::record::XDOP_SATELLITES;
use crate::record::XdopCombination;
use crate::record::XdopTable;
use crate::time::DateTime;
use crate::time::GpsSeconds;
use crate::time::LeapChange;
use crate::time::LeapEvent;
use crate::units::Centimeters;
use crate::units::Degrees;
use crate::units::HundredNanoseconds;
use crate::units::MilliArcSeconds;
use crate::units::Milliseconds;
use crate::units::PRNS;
use crate::units::Prn;
use crate::units::PrnSet;
use crate::units::Seconds;

/// Where an engine's frames go: the serial link to the receiver.
pub trait Output {
    /// Takes one whole frame to its line feed: an Oncore's message from
    /// its `@@`, a Furuno's sentence ([`furuno`]) from its `$`.
    fn send(&mut self, frame: &[u8]);
}

/// How an engine departs from the Oncore its identity names.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[non_exhaustive]
pub struct Options {
    /// With an identity, a six-channel engine becomes an eight-channel
    /// one at its first `@@Ea`: it answers the eight-channel messages
    /// from then on, and is the Oncore this identity names in place of
    /// the one it was made with, in its `@@Cj` and in all else an
    /// identity decides.  It has eight channels whatever this
    /// identity's model, so the one the engine was made with keeps it
    /// who it was.  At the turn, each setting no command has set takes
    /// this identity's default, and a UT Plus, always fixing, fixes.  `@@Cf` undoes neither, as it restores settings and
    /// an Oncore's channels are not one; only a new engine, a power-up,
    /// has six again.  `None` for an engine that stays as made.
    pub eight_on_ea: Option<&'static Identity>,
    /// It takes no commands.
    pub deaf: bool,
    /// What it powers up configured to, as an Oncore a receiver
    /// configured before it was last turned off
    /// ([`saved`]).  `None` for one that powers
    /// up as it left the factory and waits to be told.
    pub saved: Option<&'static SavedConfiguration>,
}

impl Options {
    /// The Oncore as it is.
    pub const DEFAULT: Self = Self {
        eight_on_ea: None,
        deaf: false,
        saved: None,
    };
}

/// How long a self-test takes to answer, in milliseconds: within the
/// "up to ten seconds" the manual allows (VP p. 107), and between the
/// two answers of a real VP to `@@Ca` captured, which came 5.2 and
/// 7.1 seconds after the command.
const SELF_TEST: u64 = 6000;
/// The self-test answers an engine holds at once.
const DEFERRED: usize = 4;

/// The six-channel messages' channels, and the eight-channel ones'.
const SIX_CHANNELS: usize = 6;
const EIGHT_CHANNELS: usize = 8;

/// `@@Cg`'s modes (VP p. 118).
const MODE_IDLE: u8 = 0;
const MODE_FIX: u8 = 1;
/// The `@@Ar` argument the 58503A 3704, the 58503B and the Z3816A send
/// a software 8.4 VP after `@@Ar`(1), and that release
/// (`docs/firmware/gps.md`, "Six or eight channels").  VP p. 53
/// defines only 0 and 1, so an engine of that release answers it by
/// echoing it and changes nothing: the echo is what those images need
/// to finish their set-up, and that a real 8.4 does the same is
/// inferred.
const ALGORITHM_SOFTWARE_8_4: u8 = 65;
const SOFTWARE_8_4: Software = Software {
    version: 8,
    revision: 4,
};
/// `@@Bo`'s mode that asks for the offset each time it changes (UT
/// p. 6.81).
const OFFSET_EACH_CHANGE: u8 = 1;
/// The bytes that ask for a position in place of setting one (`@@Ad`,
/// VP p. 17).
const POSITION_QUERY: u8 = 0x99;

/// WGS-84, as `@@Ap` gives a datum (VP p. 47): the semi-major axis in
/// meters, and the inverse flattening's whole part and fraction.
const WGS_84_AXIS: u32 = 6_378_137;
const WGS_84_INVERSE_FLATTENING: u16 = 298;
const WGS_84_INVERSE_FLATTENING_FRACTION: u32 = 257_223_563;

/// The bytes of one channel in `@@Ba` and `@@Ea` (VP p. 72), and where
/// the channels start, after the count of satellites tracked (VP
/// pp. 72 and 125).
const STATUS_CHANNEL: usize = 4;
const STATUS_CHANNELS_AT: usize = 36;
/// A channel's status flag bits (VP p. 73; UT p. 6.20).
const CHANNEL_USED: u8 = 0x80;
const CHANNEL_UNHEALTHY: u8 = 0x10;
/// The bit of a channel's status flags for each of a satellite's (VP
/// p. 73).
const CHANNEL_FLAG_BITS: [(SatelliteFlag, u8); 4] = [
    (SatelliteFlag::MomentumAlert, 0x40),
    (SatelliteFlag::AntiSpoof, 0x20),
    (SatelliteFlag::Inaccurate, 0x08),
    (SatelliteFlag::ParityError, 0x01),
];
const CHANNEL_USED_FOR_TIME: u8 = 0x02;
/// The receiver status bits (VP p. 73).
const STATUS_POSITION_PROPAGATE: u8 = 0x80;
const STATUS_POOR_GEOMETRY: u8 = 0x40;
const STATUS_3D_FIX: u8 = 0x20;
const STATUS_2D_FIX: u8 = 0x10;
const STATUS_ACQUIRING_OR_HOLD: u8 = 0x08;
const STATUS_DIFFERENTIAL: u8 = 0x04;
const STATUS_FEW_VISIBLE: u8 = 0x02;
const STATUS_BAD_ALMANAC: u8 = 0x01;
/// The least channel tracking mode a position status counts as
/// tracked: 5, message sync detect (VP p. 72).  The manual draws its
/// line there, a channel "not using the channel as part of the
/// position fix solution" reporting "mode 4 or lower" and one using
/// it "modes 5 through 8" (VP p. 84); a capture of a real VP reported
/// none tracked with a satellite at mode 3 and one with a satellite at
/// mode 5.
const TRACKING_LEAST: Tracking = Tracking::MessageSync;
/// Fewer visible than this is too few.
const FEW_VISIBLE: usize = 3;
/// The elevation, in degrees, a satellite is at or above to count as
/// visible in a position status, whatever `@@Ag`'s mask: a VP's
/// "default mask angle of ten degrees" (VP pp. 70 and 124) and a UT
/// Plus's "mask angle of zero degrees, representing the horizon" (UT
/// p. 6.18).  At or above, as a mask angle is taken ([`reaches_mask`]).
const VISIBLE_FROM_VP: i8 = 10;
const VISIBLE_FROM_UT_PLUS: i8 = 0;

/// Whether a satellite at `elevation` is within a mask angle of
/// `mask`, both in whole degrees: at or above it.  The manual has a VP
/// track satellites whose elevation "is greater than the satellite mask
/// angle" (VP p. 27; UT p. 6.22), but a test of a real VP kept a
/// satellite exactly at the mask on a channel and tracked it, and
/// listed it in `@@Bb`.  The test wins: a VP reports elevations in
/// whole degrees (`@@Ea`, `@@Bb`), so one it reports at the mask can
/// be above it by a fraction, and the comparison seen on the wire is at
/// or above.
const fn reaches_mask(elevation: i8, mask: i8) -> bool {
    elevation >= mask
}
/// A DOP above this, in tenths, is poor geometry: a VP's (VP p. 73)
/// and a UT Plus's (UT p. 6.20).
const POOR_GEOMETRY_VP: u16 = 200;
const POOR_GEOMETRY_UT_PLUS: u16 = 120;
/// The DOP types (VP p. 72), and the antenna sense bits a UT Plus puts
/// in the same byte (UT p. 6.20).
const DOP_TYPE_PDOP: u8 = 0;
const DOP_TYPE_HDOP: u8 = 1;
const DOP_TYPE_ANTENNA_UNDERCURRENT: u8 = 0x80;
const DOP_TYPE_ANTENNA_OVERCURRENT: u8 = 0x40;
/// The satellites a 3D fix takes, and a 2D one.
const SATELLITES_3D: usize = 4;
const SATELLITES_2D: usize = 3;
/// A self-test's antenna bits (UT p. 6.43).
const SELF_TEST_ANTENNA_UNDERCURRENT: u16 = 0x8000;
const SELF_TEST_ANTENNA_OVERCURRENT: u16 = 0x4000;

/// The bits of a self-test's result that tell one part's tests, in
/// each of the three layouts.  A part with more than one test, as a
/// VP's RAM and ROM, fails them all; zero where the engine has no test
/// of the part.
struct SelfTestBits {
    /// The part.
    fault: Fault,
    /// In `@@Ca` (VP p. 108).
    six: u16,
    /// In a VP's `@@Fa` (VP p. 143).
    eight: u16,
    /// In a UT Plus's `@@Fa` (UT p. 6.43).
    ut_plus: u16,
}

const SELF_TEST_BITS: [SelfTestBits; 7] = [
    SelfTestBits {
        fault: Fault::RealTimeClock,
        six: 1 << 13,
        eight: 1 << 15,
        ut_plus: 1 << 13,
    },
    SelfTestBits {
        fault: Fault::OscillatorLink,
        six: 1 << 12,
        eight: 1 << 14,
        ut_plus: 0,
    },
    SelfTestBits {
        fault: Fault::TemperatureSensor,
        six: 0,
        eight: 0,
        ut_plus: 1 << 12,
    },
    SelfTestBits {
        fault: Fault::Eeprom,
        six: 1 << 11,
        eight: 1 << 13,
        ut_plus: 0,
    },
    SelfTestBits {
        fault: Fault::Ram,
        six: 1 << 10 | 1 << 8,
        eight: 1 << 12 | 1 << 11,
        ut_plus: 1 << 10,
    },
    SelfTestBits {
        fault: Fault::Rom,
        six: 1 << 9 | 1 << 7,
        eight: 1 << 10 | 1 << 9,
        ut_plus: 1 << 9,
    },
    SelfTestBits {
        fault: Fault::Timebase,
        six: 1 << 6,
        eight: 1 << 8,
        ut_plus: 1 << 8,
    },
];
/// The channels a self-test's result has a correlation bit for, from
/// bit 0: `@@Ca`'s and `@@Fa`'s.
const SELF_TEST_CHANNELS_SIX: u16 = 0x003f;
const SELF_TEST_CHANNELS_EIGHT: u16 = 0x00ff;

/// A VP's signal strength for each C/N0 in dB-Hz, the last for all
/// above it (`docs/firmware/gps.md`, "The scale against C/N0").
const SIGNAL: [u8; 61] = [
    1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 4, 4, 4, 5, 6, 6, 7, 8, 9, 10, 11, 13, 14, 16, 18,
    20, 22, 25, 28, 32, 35, 40, 45, 50, 56, 63, 71, 79, 89, 100, 112, 126, 141, 158, 178, 200, 224,
    251, 255, 255, 255, 255, 255, 255, 255, 255, 255,
];

/// `@@Bb`'s slots and the bytes of one (VP p. 75), and its health
/// field's bits.
const VISIBLE_SLOT: usize = 7;
const VISIBLE_UNHEALTHY: u8 = 2;
const VISIBLE_REMOVED: u8 = 1;
/// `@@Bb`'s data: the count, then the slots.
const VISIBILITY: usize = 1 + SATELLITES * VISIBLE_SLOT;
/// `@@Bb`'s mode that asks for it each time it changes (VP p. 75).
const VISIBILITY_EACH_CHANGE: u8 = 1;
/// How often the engine makes its satellite alert computation, which
/// `@@Bb` in that mode is sent from "when visibility data changes"
/// (VP pp. 74 and 75): the middle of the "approximately once every 5-7
/// seconds" of an M12+ (M12+ p. 105), and the least of the 6 to 7
/// seconds between `@@Bb`s a capture of a real VP shows.  A record's
/// Doppler changes each second, so a comparison made each second would
/// send it each second.
const ALERT_PERIOD: Rate = Rate(6);
/// `@@Bc`: the bytes of one combination, a DOP and its satellites (VP
/// p. 77), those before the combinations and its data's.
const XDOP_ENTRY: usize = size_of::<u16>() + XDOP_SATELLITES;
const XDOP_HEAD: usize = 3;
const XDOP_TABLE: usize = XDOP_HEAD + XDOP_COMBINATIONS * XDOP_ENTRY;
/// `@@Bc`'s and `@@Bd`'s mode that asks for the message each time it
/// changes (VP pp. 77 and 79).
const TABLE_EACH_CHANGE: u8 = 1;
/// `@@Ci`'s formats (VP p. 121).
const FORMAT_NMEA: u8 = 1;
const FORMAT_LORAN: u8 = 2;
/// The years `@@Ac` takes, and the most seconds `@@Aa` does (VP pp. 6
/// and 13).
const YEAR_LEAST: u16 = 1980;
const YEAR_MOST: u16 = 2077;
const SECOND_MOST: u8 = 59;
/// `@@Bd`'s almanac-valid flag, and the bytes of one almanac's status
/// (VP p. 79).
const ALMANAC_VALID: u8 = 1;
const ALMANAC_STATUS: usize = 8;
/// `@@Ce` (VP pp. 90 and 115): the corrections one message carries,
/// the bytes of one satellite's, the greatest time of week, and the
/// greatest correction and rate either way.
const CORRECTIONS_EACH: usize = 6;
const CORRECTION_ENTRY: usize = 7;
const TIME_OF_WEEK_MOST: u32 = 6_047_999;
const CORRECTION_RANGE_MOST: i32 = 1_048_576;
const CORRECTION_RATE_MOST: i16 = 4096;
/// `@@Bk` (VP pp. 96 to 98): its DOPs, the axes a velocity and a
/// position are given along, and the elements of its least squares
/// matrix.
const EXTENSION_DOPS: usize = 5;
const AXES: usize = 3;
/// A channel's URA with none decoded yet (VP p. 98).
const URA_NOT_DECODED: u8 = 63;
/// The most a magnetic variation is either way, in tenths of a degree,
/// and the least a velocity or a clock bias is (VP pp. 97 and 98).
const VARIATION_MOST: i16 = 1800;
const SIGNED_LEAST: i16 = -i16::MAX;
/// `@@Bj`'s statuses (VP p. 94).
const NO_LEAP_PENDING: u8 = 0;
const LEAP_ADDITION_PENDING: u8 = 1;
const LEAP_SUBTRACTION_PENDING: u8 = 2;

/// Time RAIM's setup (VP pp. 102 to 106): the algorithm's states, the
/// least alarm limit and the one at power-up, and the 1 PPS modes.
const TRAIM_OFF: u8 = 0;
const TRAIM_ON: u8 = 1;
const ALARM_LIMIT_LEAST: HundredNanoseconds = HundredNanoseconds(3);
const ALARM_LIMIT_DEFAULT: HundredNanoseconds = HundredNanoseconds(10);
const PPS_OFF: u8 = 0;
const PPS_ALWAYS: u8 = 1;
const PPS_TRACKING: u8 = 2;
const PPS_CONFIRMED: u8 = 3;
/// The 1 PPS rate a VP reports, a pulse a second.
const PPS_RATE: u32 = 1;
/// The bytes a UT Plus leaves zero where a VP gives the rate and the
/// next pulse's time (UT p. 6.95).
const NEXT_PULSE: usize = 3 + 4 + 3;
/// Time RAIM's solution statuses (VP p. 105) and what it can do (VP
/// p. 106).
const TRAIM_SOLUTION_OK: u8 = 0;
const TRAIM_SOLUTION_ALARM: u8 = 1;
const TRAIM_SOLUTION_UNKNOWN: u8 = 2;
// What it can do, the most able first.
const TRAIM_ISOLATION: u8 = 0;
const TRAIM_DETECTION: u8 = 1;
const TRAIM_NEITHER: u8 = 2;
/// The satellites a time solution needs before its spread means
/// anything.
const SPREAD_OF: usize = 2;
const NANOSECONDS_PER_SECOND: i32 = 1_000_000_000;

/// The time an engine has before it is given one.  A real-time clock
/// that was not kept powered "is initialized at 12:00:00 at power up"
/// (`@@Aa`, VP p. 5), and to "the default date", which "varies
/// depending upon the firmware" (`@@Ac`, VP p. 13).  A test of a real
/// VP (software 8.8) told `@@Cf` and power-cycled with no backup power
/// read 1997-01-01 12:00:00 in its position status, standing; that is
/// a VP's.  The clock `@@Aa` and `@@Ac` answer from counted on from a
/// time out of range (hour 64, year 2054) in that test, which the
/// engine does not send: it starts that one here too.  In that test,
/// with the counting clock still out of range, `@@Cf` put the position
/// status's time from 2026-01-01 12:00:44 to 12:00:00 of the same day,
/// where with it valid `@@Cf` leaves the time as it was: a VP may fall
/// back to 12:00:00 on January 1 of the year it has when its clock is
/// not valid, which the engine, its clock always valid, does not
/// model.  A UT Plus keeps
/// the day GPS time starts until a capture of one says otherwise.
/// Either is read with GPS less UTC as zero: a VP's almanac,
/// which carries the offset, is dropped with its clock, so none is
/// known, and 12:00:00 is the same in GPS time and in UTC.
const POWER_UP_VP: GpsSeconds = noon(1997, 1, 1);
const POWER_UP_UT_PLUS: GpsSeconds = noon(1980, 1, 6);

/// The hour a clock not kept powered starts at (VP p. 5), and that
/// `@@Ac` puts a VP's at.
const NOON_HOUR: u8 = 12;

/// The time a `model` powers up at with no time given.
const fn power_up(model: Model) -> GpsSeconds {
    match model {
        Model::VpSix | Model::VpEight => POWER_UP_VP,
        Model::UtPlus => POWER_UP_UT_PLUS,
    }
}

/// 12:00:00 on a day, with no offset of GPS time from UTC.
const fn noon(year: u16, month: u8, day: u8) -> GpsSeconds {
    match GpsSeconds::from_utc(
        match DateTime::new(year, month, day, NOON_HOUR, 0, 0) {
            Some(noon) => noon.to_unix(),
            None => panic!("not a date"),
        },
        Seconds(0),
    ) {
        Some(time) => time,
        None => panic!("out of the count"),
    }
}

/// The date and time sent when none can be worked out: the start of GPS
/// time.
const NO_LABEL: DateTime = DateTime {
    year: 1980,
    month: 1,
    day: 6,
    hour: 0,
    minute: 0,
    second: 0,
};

/// What a position status says of the fix (VP pp. 72 and 73).
#[derive(Debug, Clone, Copy)]
struct Solution {
    /// The DOP, in tenths; zero when there is none to give.
    dop: u16,
    /// Which DOP that is.
    dop_type: u8,
    /// The receiver status bit for the kind of fix.
    status: u8,
}

/// What Time RAIM reports (VP pp. 105 and 106).
#[derive(Debug, Clone, Copy)]
struct TraimResult {
    /// The solution's status.
    solution: u8,
    /// What it can do.
    ability: u8,
    /// The satellites it took out of the time solution.
    isolated: PrnSet,
}

/// The most data a position status carries: an eight-channel one's
/// message length less its framing (VP p. 126).
const STATUS_MOST: usize = 76 - OVERHEAD;
/// The bytes of a position status's date, and of its date and time,
/// which come first (VP p. 71).
const STATUS_DATE: usize = 4;
const STATUS_DATE_TIME: usize = STATUS_DATE + 3;

/// A position status's data, kept to answer a poll while idle.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
struct KeptStatus {
    /// The first `length` are the data.
    bytes: [u8; STATUS_MOST],
    /// How many there are.
    length: usize,
}

impl KeptStatus {
    fn of(data: &Data) -> Self {
        let data = data.as_slice();
        let length = data.len().min(STATUS_MOST);
        let mut bytes = [0; STATUS_MOST];
        bytes[..length].copy_from_slice(&data[..length]);
        Self { bytes, length }
    }

    fn as_slice(&self) -> &[u8] {
        &self.bytes[..self.length]
    }

    /// An eight-channel status as a six-channel one: the same up to the
    /// channels (VP pp. 72 and 125), the first six channels, and the
    /// receiver status, its count of tracked recounted from those six.
    fn six(&self) -> Self {
        let channels = STATUS_CHANNELS_AT + SIX_CHANNELS * STATUS_CHANNEL;
        let eight = &self.bytes[..self.length];
        let mut bytes = [0; STATUS_MOST];
        let Some(receiver_status) = eight.last() else {
            return *self;
        };
        bytes[..channels].copy_from_slice(&eight[..channels]);
        bytes[channels] = *receiver_status;
        let least = tracking_mode(TRACKING_LEAST);
        let tracked = bytes[STATUS_CHANNELS_AT..channels]
            .as_chunks::<STATUS_CHANNEL>()
            .0
            .iter()
            .filter(|[prn, mode, ..]| *prn != 0 && *mode >= least)
            .count();
        bytes[STATUS_CHANNELS_AT - 1] = count_byte(tracked);
        Self {
            bytes,
            length: channels + 1,
        }
    }
}

/// The satellites Time RAIM needs to detect a bad one and to isolate
/// it, for alarm limits from `from` up.
#[derive(Debug)]
struct TraimNeed {
    /// The least alarm limit this row is for.
    from: HundredNanoseconds,
    /// To detect.
    detect: usize,
    /// To isolate.
    isolate: usize,
}

/// The needs by alarm limit, greatest first.
const TRAIM_NEEDS: [TraimNeed; 4] = [
    TraimNeed {
        from: HundredNanoseconds(10),
        detect: 2,
        isolate: 3,
    },
    TraimNeed {
        from: HundredNanoseconds(7),
        detect: 3,
        isolate: 3,
    },
    TraimNeed {
        from: HundredNanoseconds(5),
        detect: 4,
        isolate: 4,
    },
    TraimNeed {
        from: HundredNanoseconds(3),
        detect: 4,
        isolate: 8,
    },
];

/// The messages that come in a six-channel and an eight-channel form.
#[derive(Debug, PartialEq, Eq)]
struct Family {
    /// The position status.
    status: MessageId,
    /// Its extension.
    extension: MessageId,
    /// Time RAIM's setup and status.
    traim: MessageId,
    /// The self-test.
    self_test: MessageId,
    /// The channels its messages carry.
    channels: usize,
    /// Its place in [`FAMILIES`].
    index: usize,
}

const SIX: Family = Family {
    status: BA_POSITION_STATUS,
    extension: BK_EXTENSION,
    traim: BN_TIME_RAIM,
    self_test: CA_SELF_TEST,
    channels: SIX_CHANNELS,
    index: 0,
};

const EIGHT: Family = Family {
    status: EA_POSITION_STATUS,
    extension: EK_EXTENSION,
    traim: EN_TIME_RAIM,
    self_test: FA_SELF_TEST,
    channels: EIGHT_CHANNELS,
    index: 1,
};

/// Both, in the order their continuous messages are sent.
const FAMILIES: [&Family; 2] = [&SIX, &EIGHT];

/// Which of a family's messages one is.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum Member {
    Status,
    Extension,
    Traim,
    SelfTest,
}

fn family_of(id: MessageId) -> Option<(&'static Family, Member)> {
    let family = match id.channels() {
        Channels::Common => return None,
        Channels::Six => &SIX,
        Channels::Eight => &EIGHT,
    };
    [
        (family.status, Member::Status),
        (family.extension, Member::Extension),
        (family.traim, Member::Traim),
        (family.self_test, Member::SelfTest),
    ]
    .into_iter()
    .find_map(|(known, member)| (known == id).then_some((family, member)))
}

/// How often a continuous message is sent, in seconds; zero is never.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
struct Rate(u8);

impl Rate {
    /// The rate a command's first byte asks for.
    fn of(data: &[u8]) -> Self {
        Self(data.first().copied().unwrap_or(0))
    }

    /// Whether the message goes in the engine's `count`th second.
    fn due(self, count: u32) -> bool {
        self.0 != 0 && count.is_multiple_of(u32::from(self.0))
    }
}

/// The rates of one family's continuous messages.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
struct Rates {
    /// The position status.
    status: Rate,
    /// Its extension.
    extension: Rate,
    /// Time RAIM's status.
    traim: Rate,
}

/// Time RAIM as it is set up, one setup for both families.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
struct TraimSetup {
    /// Whether the algorithm is on.
    algorithm: bool,
    /// The alarm limit.
    alarm_limit: HundredNanoseconds,
    /// When the 1 PPS is put out.
    pulse_mode: PulseMode,
}

impl TraimSetup {
    const DEFAULT: Self = Self {
        algorithm: false,
        alarm_limit: ALARM_LIMIT_DEFAULT,
        pulse_mode: PulseMode::Always,
    };

    /// The setup a command's bytes give (VP p. 104): `None` if any of
    /// them is out of range.
    fn of(algorithm: u8, alarm_limit: u16, pps_mode: u8) -> Option<Self> {
        let algorithm = match algorithm {
            TRAIM_OFF => false,
            TRAIM_ON => true,
            _ => return None,
        };
        let pulse_mode = match pps_mode {
            PPS_OFF => PulseMode::Off,
            PPS_ALWAYS => PulseMode::Always,
            PPS_TRACKING => PulseMode::Tracking,
            PPS_CONFIRMED => PulseMode::Confirmed,
            _ => return None,
        };
        let alarm_limit = HundredNanoseconds(alarm_limit);
        (alarm_limit >= ALARM_LIMIT_LEAST).then_some(Self {
            algorithm,
            alarm_limit,
            pulse_mode,
        })
    }

    /// The algorithm's state as a message carries it.
    const fn algorithm_byte(self) -> u8 {
        if self.algorithm { TRAIM_ON } else { TRAIM_OFF }
    }

    /// The 1 PPS control mode as a message carries it.
    const fn pps_mode(self) -> u8 {
        match self.pulse_mode {
            PulseMode::Off => PPS_OFF,
            PulseMode::Always => PPS_ALWAYS,
            PulseMode::Tracking => PPS_TRACKING,
            PulseMode::Confirmed => PPS_CONFIRMED,
        }
    }
}

/// What commands have made of an engine, all of it put back by `@@Cf`.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
struct State {
    /// Whether it is fixing, not idle.
    fixing: bool,
    /// The values its settings have.
    store: Store,
    /// The satellite assigned to each channel by hand, zero for none.
    assignments: [u8; CHANNELS_MOST],
    /// Each family's rates.
    rates: [Rates; FAMILIES.len()],
    /// Time RAIM's setup.
    traim: TraimSetup,
    /// The latitude, longitude and height last given by `@@Ad`, `@@Ae`
    /// and `@@Af`, the position reported while the record has none.
    initial: Location,
    /// The rate of `@@Ce`.
    corrections: Rate,
    /// Whether `@@Bo` is sent each time the offset changes.
    offset_updates: bool,
    /// Whether `@@Bb` is sent each time it changes.
    visibility_updates: bool,
    /// Whether `@@Bc` is.
    xdop_updates: bool,
    /// Whether `@@Bd` is.
    almanac_updates: bool,
    /// What the port speaks.
    format: OutputFormat,
}

impl State {
    /// The state at power-up with nothing saved, and after `@@Cf`: a
    /// VP idle, a UT Plus fixing.
    const fn starting(model: Model) -> Self {
        Self {
            fixing: matches!(model, Model::UtPlus),
            store: Store::starting(model),
            assignments: [0; CHANNELS_MOST],
            rates: [Rates {
                status: Rate(0),
                extension: Rate(0),
                traim: Rate(0),
            }; FAMILIES.len()],
            traim: TraimSetup::DEFAULT,
            initial: Location {
                latitude: MilliArcSeconds(0),
                longitude: MilliArcSeconds(0),
                height: Height {
                    height: Centimeters(0),
                    kind: HeightKind::Ellipsoid,
                },
            },
            corrections: Rate(0),
            offset_updates: false,
            visibility_updates: false,
            xdop_updates: false,
            almanac_updates: false,
            format: OutputFormat::Binary,
        }
    }

    /// The state at power-up with `saved`, which suits `model`
    /// ([`SavedConfiguration::check`]).
    const fn saved(model: Model, saved: &SavedConfiguration) -> Self {
        let starting = Self::starting(model);
        let family = match saved.channels {
            ChannelCount::Six => &SIX,
            ChannelCount::Eight => &EIGHT,
        };
        let mut rates = starting.rates;
        rates[family.index].status = Rate(saved.position_status_rate.0);
        rates[family.index].traim = Rate(saved.time_raim_rate.0);
        Self {
            fixing: saved.fixing,
            rates,
            traim: TraimSetup {
                algorithm: saved.raim_algorithm,
                alarm_limit: saved.raim_alarm_limit,
                pulse_mode: saved.pulse_mode,
            },
            visibility_updates: saved.visibility_updates,
            ..starting
        }
    }
}

/// A self-test's answer, waiting to be sent.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
struct Deferred {
    /// When it is due.
    due: Milliseconds<u64>,
    /// The self-test's ID.
    id: MessageId,
    /// Its result.
    result: u16,
}

/// A model that stands in for an Oncore.  See the [module](self).
#[derive(Debug)]
pub struct Engine {
    /// Which Oncore it is now: the one it was made as, or the one
    /// [`Options::eight_on_ea`] made of it.
    identity: &'static Identity,
    /// How it departs from it.
    options: Options,
    /// The receiver channels it has now, which `@@Cf` leaves with the
    /// identity.
    channels: usize,
    /// Finds the commands in what it is sent.
    framer: Framer,
    /// What commands have made of it.
    state: State,
    /// The seconds it has been given since power-up.
    count: u32,
    /// The time its position status reports, and the messages timed
    /// from it: [`Engine::counting`](Self::counting) while fixing, and
    /// standing while idle.
    clock: GpsSeconds,
    /// The time `@@Aa` and `@@Ac` answer with, which runs on a second
    /// each second, or is the record's.
    counting: GpsSeconds,
    /// The last record's time, to tell a new one from the same again.
    heard: Option<GpsSeconds>,
    /// GPS less UTC as `@@Bo` gave it in the second before.
    offset: Option<u8>,
    /// The data of the last `@@Bb` sent.
    visibility: Option<[u8; VISIBILITY]>,
    /// The data of `@@Bb` as the last satellite alert computation
    /// ([`ALERT_PERIOD`]) found it, which a poll answers.
    alert: Option<[u8; VISIBILITY]>,
    /// The data of the last `@@Bc` sent.
    xdops: Option<[u8; XDOP_TABLE]>,
    /// The data of the last `@@Bd` sent.
    almanac: Option<[u8; 2 * ALMANAC_STATUS]>,
    /// The last position status while fixing, kept each second in
    /// its eight-channel form, which `@@Cf` does not forget.  A
    /// six-channel one is made from it ([`KeptStatus::six`]).
    last_status: Option<KeptStatus>,
    /// The self-tests' answers waiting, oldest
    /// first.
    deferred: [Option<Deferred>; DEFERRED],
}

impl Engine {
    /// The Oncore `identity` names, just powered up: configured as
    /// [`Options::saved`] has it, if it has one.
    ///
    /// # Errors
    ///
    /// A [`ConfigurationError`] if the saved configuration does not suit
    /// the identity's Oncore.
    pub const fn new(
        identity: &'static Identity,
        options: Options,
    ) -> Result<Self, ConfigurationError> {
        let model = identity.model();
        let state = match options.saved {
            None => State::starting(model),
            Some(saved) => {
                if let Err(error) = saved.check(model) {
                    return Err(error);
                }
                State::saved(model, saved)
            }
        };
        Ok(Self {
            identity,
            options,
            channels: model.channels(),
            framer: Framer::new(),
            state,
            count: 0,
            clock: power_up(model),
            counting: power_up(model),
            heard: None,
            offset: None,
            visibility: None,
            alert: None,
            xdops: None,
            almanac: None,
            last_status: None,
            deferred: [None; DEFERRED],
        })
    }

    /// Which Oncore it is now: the one it was made as, until
    /// [`Options::eight_on_ea`] makes it another.
    #[must_use]
    pub const fn identity(&self) -> &'static Identity {
        self.identity
    }

    /// Whether it is fixing, not idle.
    #[must_use]
    pub const fn fixing(&self) -> bool {
        self.state.fixing
    }

    /// What commands have set it to.
    #[must_use]
    pub fn settings(&self) -> Settings {
        let store = &self.state.store;
        Settings {
            mask_angle: Degrees(store.mask_angle()),
            position_hold: store.holding(),
            held_position: store.hold_position(),
            altitude_hold: store.altitude_hold(),
            altitude_hold_height: store.altitude_hold_height(),
            time_base: if store.gps_time() {
                TimeBase::Gps
            } else {
                TimeBase::Utc
            },
            cable_delay: store.cable_delay(),
            pulse_offset: store.pulse_offset(),
            pulse_mode: self.state.traim.pulse_mode,
            raim_algorithm: self.state.traim.algorithm,
            raim_alarm_limit: self.state.traim.alarm_limit,
            application: store.application(),
            initial_position: self.state.initial,
            satellite_select: self.satellite_select(),
            manual_satellites: self.state.assignments,
            dop_kind: store.dop_kind(),
            xdop_hysteresis: store.xdop_hysteresis(),
            ignore_list: store.ignore_list(),
            almanac_update: store.almanac_update(),
            atmospheric_correction: store.atmospheric_correction(),
            fix_algorithm: store.fix_algorithm(),
            epoch_offset: store.epoch_offset(),
            hdop_threshold: store.hdop_threshold(),
            correction_thresholds: store.correction_thresholds(),
            output_format: self.state.format,
        }
    }

    /// When the next answer that is waiting is due, by the caller's
    /// clock.  `None` when none waits.
    #[must_use]
    pub fn deadline(&self) -> Option<Milliseconds<u64>> {
        self.deferred
            .iter()
            .flatten()
            .map(|answer| answer.due)
            .min()
    }

    /// Sends the answers that are due at `now`.
    pub fn poll<O: Output + ?Sized>(&mut self, now: Milliseconds<u64>, out: &mut O) {
        let waiting = self.deferred;
        self.deferred = [None; DEFERRED];
        let mut kept = 0;
        for answer in waiting.into_iter().flatten() {
            if answer.due <= now {
                send(out, answer.id, Data::new().word(answer.result));
            } else {
                self.deferred[kept] = Some(answer);
                kept += 1;
            }
        }
    }

    /// A second starts, `record` being what is known in it: sends the
    /// continuous messages due.  `now` is the caller's clock, of any
    /// origin, which only the answers that wait are timed by.
    pub fn second<O: Output + ?Sized>(
        &mut self,
        record: &Record,
        now: Milliseconds<u64>,
        out: &mut O,
    ) {
        self.poll(now, out);
        self.count = self.count.wrapping_add(1);
        let told = record.time.map(|time| time.seconds);
        self.counting = match told {
            Some(time) if told != self.heard => time,
            _ if self.count == 1 => self.counting,
            _ => self.counting + Seconds(1),
        };
        self.follow();
        self.heard = told;

        let offset = self.wire_offset(record);
        let stepped = self.offset.is_some_and(|before| before != offset);
        self.offset = Some(offset);

        if self.state.fixing {
            self.keep_status(record);
            for family in FAMILIES {
                let rates = self.state.rates[family.index];
                if rates.status.due(self.count) {
                    self.position_status(record, family, out);
                }
                if rates.extension.due(self.count) {
                    self.extension(record, family, out);
                }
                if rates.traim.due(self.count) {
                    self.traim_status(record, family, out);
                }
            }
            if self.state.corrections.due(self.count) {
                self.corrections(record, out);
            }
            if ALERT_PERIOD.due(self.count) {
                self.alert_computation(record, out);
            }
            if self.state.xdop_updates && self.xdops != kept(&self.xdop_table(record)) {
                self.send_xdop_table(record, out);
            }
            if self.state.almanac_updates && self.almanac != kept(&Self::almanac_status(record)) {
                self.send_almanac_status(record, out);
            }
        }
        if self.state.offset_updates && stepped {
            send(out, BO_UTC_OFFSET, Data::new().byte(offset));
        }
    }

    /// Takes bytes from the receiver, at `now` by the caller's clock,
    /// and answers each whole command among them.  `record` is the
    /// current second's.  Returns what those commands did that the
    /// caller may have to act on, which it is free to drop.
    pub fn receive<O: Output + ?Sized>(
        &mut self,
        bytes: &[u8],
        record: &Record,
        now: Milliseconds<u64>,
        out: &mut O,
    ) -> Events {
        self.poll(now, out);
        if self.count == 0
            && let Some(time) = record.time
        {
            self.counting = time.seconds;
            self.follow();
        }
        let mut events = Events::NONE;
        for byte in bytes {
            self.framer.push(*byte);
            while let Some(command) = self.framer.next() {
                if self.takes(command.id) {
                    let before = self.settings();
                    self.answer(command.id, command.data(), record, now, out);
                    events = events.and(Events::of(
                        command.id == CF_SET_TO_DEFAULTS,
                        before.changed(&self.settings()),
                    ));
                }
            }
        }
        events
    }

    /// Whether this Oncore takes a command at all: none if it is deaf,
    /// else those its model has.
    fn takes(&self, id: MessageId) -> bool {
        const UT_PLUS: [MessageId; 23] = [
            AB_GMT_CORRECTION,
            AW_TIME_MODE,
            AC_DATE,
            AA_TIME_OF_DAY,
            AD_LATITUDE,
            AE_LONGITUDE,
            AF_HEIGHT,
            AG_MASK_ANGLE,
            EA_POSITION_STATUS,
            BB_VISIBLE_SATELLITES,
            BJ_LEAP_SECOND_PENDING,
            AQ_ATMOSPHERIC_CORRECTION,
            AO_DATUM_SELECT,
            CJ_RECEIVER_ID,
            FA_SELF_TEST,
            CF_SET_TO_DEFAULTS,
            BO_UTC_OFFSET,
            AZ_CABLE_DELAY,
            AY_PULSE_OFFSET,
            AS_HOLD_POSITION,
            AT_POSITION_HOLD,
            EN_TIME_RAIM,
            AM_IGNORE_LIST,
        ];
        if self.options.deaf {
            return false;
        }
        if id == GJ_LEAP_SECOND_PENDING {
            return self.identity.leap_details();
        }
        match self.identity.model() {
            Model::VpSix | Model::VpEight => {
                id != BO_UTC_OFFSET && id != SZ_POWER_ON_FAILURE && id != AP_USER_DATUM
            }
            Model::UtPlus => UT_PLUS.contains(&id),
        }
    }

    /// Answers a command it [takes](Self::takes).
    fn answer<O: Output + ?Sized>(
        &mut self,
        id: MessageId,
        data: &[u8],
        record: &Record,
        now: Milliseconds<u64>,
        out: &mut O,
    ) {
        if let Some((family, member)) = family_of(id) {
            self.answer_family(family, member, data, record, now, out);
            return;
        }
        let model = self.identity.model();
        if id == AA_TIME_OF_DAY {
            if let &[hour, minute, second] = data
                && second <= SECOND_MOST
            {
                self.set_clocks(record, |now| {
                    DateTime::new(now.year, now.month, now.day, hour, minute, second)
                });
            }
            send(
                out,
                id,
                Data::new().bytes(&self.time_of_day(record, self.counting)),
            );
        } else if id == AC_DATE {
            if let &[month, day, high, low] = data {
                let year = u16::from_be_bytes([high, low]);
                if (YEAR_LEAST..=YEAR_MOST).contains(&year) {
                    // A VP's goes to 12:00:00 on the date, as a test of
                    // a real VP showed; a UT Plus's keeps its time.
                    self.set_clocks(record, |now| match model {
                        Model::VpSix | Model::VpEight => {
                            DateTime::new(year, month, day, NOON_HOUR, 0, 0)
                        }
                        Model::UtPlus => {
                            DateTime::new(year, month, day, now.hour, now.minute, now.second)
                        }
                    });
                }
            }
            send(
                out,
                id,
                Data::new().bytes(&self.date(record, self.counting)),
            );
        } else if id == AB_GMT_CORRECTION {
            self.state.store.set_or_query(id, data, model);
            // A correction behind UTC by minutes alone is answered as
            // its minutes negated, with no sign.
            match self.state.store.get(id) {
                Some(&[sign, 0, minutes]) if sign != 0 => {
                    send(out, id, Data::new().bytes(&[0, 0, minutes.wrapping_neg()]));
                }
                Some(held) => send(out, id, Data::new().bytes(held)),
                None => {}
            }
        } else if id == AD_LATITUDE || id == AE_LONGITUDE {
            // One out of range (pp. 17 and 21; UT pp. 6.12 and 6.14) is
            // not taken, and answered as the query is.
            let latitude = id == AD_LATITUDE;
            let given = match data {
                &[a, b, c, d] if data.iter().any(|byte| *byte != POSITION_QUERY) => {
                    Some(i32::from_be_bytes([a, b, c, d]))
                }
                _ => None,
            }
            .filter(|given| {
                if latitude {
                    latitude_takes(*given)
                } else {
                    longitude_takes(*given)
                }
            });
            if let Some(given) = given {
                if latitude {
                    self.state.initial.latitude = MilliArcSeconds(given);
                } else {
                    self.state.initial.longitude = MilliArcSeconds(given);
                }
                send(out, id, Data::new().bytes(data));
            } else {
                let place = self.place(record);
                let value = if latitude {
                    place.latitude
                } else {
                    place.longitude
                };
                send(out, id, Data::new().signed_long(value));
            }
        } else if id == AF_HEIGHT {
            // As `@@Ad` and `@@Ae` (p. 25; UT p. 6.16).
            let given = height_of(data).filter(|_| {
                data.iter().any(|byte| *byte != POSITION_QUERY) && height_takes(data, model)
            });
            if let Some(height) = given {
                self.state.initial.height = height;
            }
            self.height(given, record, out);
        } else if id == AI_MANUAL_SATELLITE {
            self.assign(data, out);
        } else if id == AO_DATUM_SELECT {
            self.state.store.set_or_query(id, data, model);
            send(
                out,
                AP_USER_DATUM,
                Data::new()
                    .byte(self.state.store.datum())
                    .triple(WGS_84_AXIS)
                    .word(0)
                    .word(WGS_84_INVERSE_FLATTENING)
                    .long(WGS_84_INVERSE_FLATTENING_FRACTION)
                    .zeros(6),
            );
        } else if id == BB_VISIBLE_SATELLITES {
            self.state.visibility_updates = data == [VISIBILITY_EACH_CHANGE];
            let alert = self.alert.unwrap_or_else(|| self.visibility_now(record));
            self.send_visibility(alert, out);
        } else if id == BC_XDOP_TABLE {
            self.state.xdop_updates = data == [TABLE_EACH_CHANGE];
            self.send_xdop_table(record, out);
        } else if id == BD_ALMANAC_STATUS {
            self.state.almanac_updates = data == [TABLE_EACH_CHANGE];
            self.send_almanac_status(record, out);
        } else if id == CI_IO_FORMAT {
            // VP p. 121: no answer, and no format but these two.  The
            // engine goes on in binary, the one format it speaks.
            match data {
                [FORMAT_NMEA] => self.state.format = OutputFormat::Nmea,
                [FORMAT_LORAN] => self.state.format = OutputFormat::Loran,
                _ => {}
            }
        } else if id == BH_CORRECTION_OUTPUT {
            self.state.corrections = Rate::of(data);
            self.corrections(record, out);
        } else if id == BJ_LEAP_SECOND_PENDING {
            send(out, id, Data::new().byte(self.leap_pending(record)));
        } else if id == BO_UTC_OFFSET {
            self.state.offset_updates = data == [OFFSET_EACH_CHANGE];
            send(out, id, Data::new().byte(self.wire_offset(record)));
        } else if id == GJ_LEAP_SECOND_PENDING {
            self.leap_details(record, out);
        } else if id == CF_SET_TO_DEFAULTS {
            self.state = State::starting(model);
            self.follow();
            send(out, id, &Data::new());
        } else if id == CG_FIX_MODE {
            match data.first() {
                Some(&MODE_FIX) => self.state.fixing = true,
                Some(&MODE_IDLE) => self.state.fixing = false,
                _ => {}
            }
            self.follow();
            let mode = if self.state.fixing {
                MODE_FIX
            } else {
                MODE_IDLE
            };
            send(out, id, Data::new().byte(mode));
        } else if id == CJ_RECEIVER_ID {
            self.receiver_id(out);
        } else if id == AR_FIX_ALGORITHM
            && data == [ALGORITHM_SOFTWARE_8_4]
            && self.identity.software() == Some(SOFTWARE_8_4)
        {
            send(out, id, Data::new().bytes(data));
        } else if is_setting(id) {
            self.state.store.set_or_query(id, data, model);
            if let Some(held) = self.state.store.get(id) {
                send(out, id, Data::new().bytes(held));
            }
        }
    }

    fn answer_family<O: Output + ?Sized>(
        &mut self,
        family: &'static Family,
        member: Member,
        data: &[u8],
        record: &Record,
        now: Milliseconds<u64>,
        out: &mut O,
    ) {
        if family.channels > self.channels {
            let Some(identity) = self.options.eight_on_ea else {
                return;
            };
            if family.status != EA_POSITION_STATUS || member != Member::Status {
                return;
            }
            self.turn_into(identity, family.channels);
        }
        match member {
            Member::Status => {
                self.state.rates[family.index].status = Rate::of(data);
                self.position_status(record, family, out);
            }
            Member::Extension => {
                self.state.rates[family.index].extension = Rate::of(data);
                self.extension(record, family, out);
            }
            Member::Traim => {
                self.set_traim(family, data);
                self.traim_status(record, family, out);
            }
            Member::SelfTest => self.self_test(family.self_test, record, now),
        }
    }

    /// Becomes the Oncore `identity` names, with `channels`
    /// ([`Options::eight_on_ea`]): what no command has set is as that
    /// Oncore powers up with, and a UT Plus, always fixing, fixes.
    fn turn_into(&mut self, identity: &'static Identity, channels: usize) {
        let model = identity.model();
        self.identity = identity;
        self.channels = channels;
        self.state.store.default_untold(model);
        if model == Model::UtPlus {
            self.state.fixing = true;
            self.follow();
        }
    }

    /// Starts a self-test (VP p. 107; UT p. 6.42).  A VP goes idle for
    /// it and stays so; a UT Plus tells what its antenna's feed draws.
    fn self_test(&mut self, id: MessageId, record: &Record, now: Milliseconds<u64>) {
        let result = self.self_test_result(id, record);
        if self.identity.model() != Model::UtPlus {
            self.state.fixing = false;
        }
        if let Some(free) = self.deferred.iter_mut().find(|slot| slot.is_none()) {
            *free = Some(Deferred {
                due: Milliseconds(now.0.saturating_add(SELF_TEST)),
                id,
                result,
            });
        }
    }

    /// What the self-test `id` finds (VP pp. 108 and 143; UT p. 6.43):
    /// the record's faults in that message's bits, and for a UT Plus
    /// what its antenna's feed draws.
    fn self_test_result(&self, id: MessageId, record: &Record) -> u16 {
        let model = self.identity.model();
        let six = id == CA_SELF_TEST;
        let antenna = match (model, record.antenna) {
            (Model::UtPlus, Antenna::Open) => SELF_TEST_ANTENNA_UNDERCURRENT,
            (Model::UtPlus, Antenna::Shorted) => SELF_TEST_ANTENNA_OVERCURRENT,
            _ => 0,
        };
        let channels = if six {
            SELF_TEST_CHANNELS_SIX
        } else {
            SELF_TEST_CHANNELS_EIGHT
        };
        SELF_TEST_BITS
            .iter()
            .filter(|bits| record.self_test.hardware.contains(bits.fault))
            .map(|bits| match (six, model) {
                (true, _) => bits.six,
                (false, Model::UtPlus) => bits.ut_plus,
                (false, Model::VpSix | Model::VpEight) => bits.eight,
            })
            .fold(
                antenna | record.self_test.channels & channels,
                |result, bits| result | bits,
            )
    }

    /// `@@Aa` and `@@Ac` (VP pp. 5 and 13): sets each clock to the date
    /// and time `to` makes of the one it reports, unless that is none
    /// there is, and leaves each running or standing as it was.  An
    /// Oncore tracking a satellite sets its clock by it and ignores the
    /// command; so does the engine while the record tells the time, its
    /// clocks then being the record's.  No command moves the engine off
    /// a record's time.
    fn set_clocks(&mut self, record: &Record, to: impl Fn(DateTime) -> Option<DateTime>) {
        if record.time.is_some() {
            return;
        }
        let clock = self.shifted(record, self.clock, &to);
        let counting = self.shifted(record, self.counting, &to);
        if let (Some(clock), Some(counting)) = (clock, counting) {
            self.clock = clock;
            self.counting = counting;
        }
    }

    /// `time` moved to the date and time `to` makes of how it is
    /// reported, if there is one.
    fn shifted(
        &self,
        record: &Record,
        time: GpsSeconds,
        to: impl FnOnce(DateTime) -> Option<DateTime>,
    ) -> Option<GpsSeconds> {
        let now = self.label(record, time);
        to(now)
            .and_then(|then| then.to_unix().checked_since(now.to_unix()))
            .and_then(|shift| time.checked_add(shift))
    }

    /// Puts the position status's time at the counting clock's while
    /// fixing; idle, it stands where it is.  Tests of a real VP with no
    /// antenna found `@@Ea`'s time counting with `@@Aa`'s while fixing,
    /// and standing at the time of `@@Cf` once that left it idle, its
    /// date kept, while `@@Aa`'s counted on.
    fn follow(&mut self) {
        if self.state.fixing {
            self.clock = self.counting;
        }
    }

    /// `@@Ai` (VP p. 31): a channel's satellite set by hand, or asked.
    fn assign<O: Output + ?Sized>(&mut self, data: &[u8], out: &mut O) {
        let &[channel, satellite] = data else {
            return;
        };
        let channels = self.channels;
        let Some(slot) = usize::from(channel)
            .checked_sub(1)
            .filter(|index| *index < channels)
            .and_then(|index| self.state.assignments.get_mut(index))
        else {
            return;
        };
        if satellite != QUERY {
            *slot = satellite;
        }
        send(
            out,
            AI_MANUAL_SATELLITE,
            Data::new().byte(channel).byte(*slot),
        );
    }

    /// `@@Bn` and `@@En` as commands (VP pp. 104 and 140): all bytes out of
    /// range ask; a setup out of range is not taken, nor its rate.
    fn set_traim(&mut self, family: &Family, data: &[u8]) {
        if data.iter().all(|byte| *byte == QUERY) {
            return;
        }
        let &[rate, algorithm, high, low, pps_mode, ..] = data else {
            return;
        };
        if let Some(setup) = TraimSetup::of(algorithm, u16::from_be_bytes([high, low]), pps_mode) {
            self.state.rates[family.index].traim = Rate(rate);
            self.state.traim = setup;
        }
    }

    /// The ellipsoid's height above mean sea level where the record is,
    /// in centimeters; zero with no position.
    fn separation(record: &Record) -> i32 {
        record.position.map_or(0, |position| {
            position.ellipsoid.0.wrapping_sub(position.mean_sea_level.0)
        })
    }

    /// The position reported: the one held, when holding, else the
    /// record's, and while it has none, the one `@@Ad`, `@@Ae` and
    /// `@@Af` gave to start from (VP pp. 17 and 25).
    fn place(&self, record: &Record) -> Place {
        if self.state.store.holding() {
            return self.state.store.held_position(Self::separation(record));
        }
        record.position.map_or_else(
            || Place::of(self.state.initial, 0),
            |position| Place {
                latitude: position.latitude.0,
                longitude: position.longitude.0,
                ellipsoid: position.ellipsoid.0,
                mean_sea_level: position.mean_sea_level.0,
            },
        )
    }

    /// `@@Af` (VP p. 25): a height given, or the current one asked, is
    /// answered as both kinds of height.
    fn height<O: Output + ?Sized>(&self, given: Option<Height>, record: &Record, out: &mut O) {
        let separation = Self::separation(record);
        let given = match given {
            Some(Height {
                height,
                kind: HeightKind::MeanSeaLevel,
            }) => height.0,
            Some(Height {
                height,
                kind: HeightKind::Ellipsoid,
            }) => height.0.wrapping_sub(separation),
            None => self.place(record).mean_sea_level,
        };
        send(
            out,
            AF_HEIGHT,
            Data::new()
                .signed_long(given.wrapping_add(separation))
                .signed_long(given),
        );
    }

    /// Whether `time` is a leap second the record's event inserts.
    fn inserted(record: &Record, time: GpsSeconds) -> bool {
        matches!(
            record.leap,
            Some(LeapEvent { at, change: LeapChange::Insert }) if at == time
        )
    }

    /// GPS less UTC at `time`, which is the engine's second or the one
    /// after it.
    fn utc_offset(&self, record: &Record, time: GpsSeconds) -> Seconds<i8> {
        let now = record.utc_offset.unwrap_or(Seconds(0));
        match record.leap {
            Some(leap) if leap.pending(self.clock) => leap.offset(time, now),
            _ => now,
        }
    }

    /// GPS less UTC as `@@Bo` gives it (UT p. 6.81).  Through an
    /// inserted leap second it is still the offset before.
    fn wire_offset(&self, record: &Record) -> u8 {
        let offset = self.utc_offset(record, self.clock).0;
        let offset = if Self::inserted(record, self.clock) {
            offset.wrapping_sub(1)
        } else {
            offset
        };
        offset.cast_unsigned()
    }

    /// `@@Bj`'s status (VP p. 94): pending until the leap second is
    /// over.
    fn leap_pending(&self, record: &Record) -> u8 {
        match record.leap {
            Some(leap) if leap.pending(self.clock) || Self::inserted(record, self.clock) => {
                match leap.change {
                    LeapChange::Insert => LEAP_ADDITION_PENDING,
                    LeapChange::Remove => LEAP_SUBTRACTION_PENDING,
                }
            }
            _ => NO_LEAP_PENDING,
        }
    }

    /// The date and time `time` is reported as: UTC or GPS time as
    /// `@@Aw` says, on the clock `@@Ab` sets.
    fn label(&self, record: &Record, time: GpsSeconds) -> DateTime {
        let ahead = self.state.store.gmt_offset();
        let label = if self.state.store.gps_time() {
            DateTime::local(time, Seconds(0), None, ahead)
        } else {
            DateTime::local(time, self.utc_offset(record, time), record.leap, ahead)
        };
        label.unwrap_or(NO_LABEL)
    }

    /// Month, day and year (VP p. 71).
    fn date(&self, record: &Record, time: GpsSeconds) -> [u8; 4] {
        let label = self.label(record, time);
        let [high, low] = label.year.to_be_bytes();
        [label.month, label.day, high, low]
    }

    /// Hours, minutes and seconds (VP p. 71).
    fn time_of_day(&self, record: &Record, time: GpsSeconds) -> [u8; 3] {
        let label = self.label(record, time);
        [label.hour, label.minute, label.second]
    }

    /// Whether the antenna's feed says no antenna can be heard.
    fn antenna_off(record: &Record) -> bool {
        matches!(record.antenna, Antenna::Open | Antenna::Shorted)
    }

    /// How it chooses the satellites it tracks: as `@@Ah` sets it (VP
    /// pp. 29 and 30), but a UT Plus, which has no `@@Ah`, the eight
    /// highest in the sky (UT p. 6.18).
    fn satellite_select(&self) -> SatelliteSelect {
        match self.identity.model() {
            Model::VpSix | Model::VpEight => self.state.store.satellite_select(),
            Model::UtPlus => SatelliteSelect::HighestInSky,
        }
    }

    /// `@@Ag`'s mask angle, in degrees as elevations are.
    fn mask_angle(&self) -> i8 {
        i8::try_from(self.state.store.mask_angle()).unwrap_or(i8::MAX)
    }

    /// The satellites on a family's channels: those of the record with
    /// a signal, at or above the mask angle ([`reaches_mask`]) and not
    /// ignored, in its order, as
    /// many as there are channels.  Set to track the highest in the
    /// sky (VP p. 29), as a UT Plus always is, those are the highest of them, still in the
    /// record's order; else the record's order is its preference.
    fn tracked<'a>(
        &self,
        record: &'a Record,
        family: &Family,
    ) -> impl Iterator<Item = &'a Satellite> + use<'a> {
        let mask = self.mask_angle();
        let ignored = self.state.store.ignore_list();
        let listening = self.state.fixing && !Self::antenna_off(record);
        let channels = self.channels.min(family.channels);
        let highest = self.satellite_select() == SatelliteSelect::HighestInSky;
        let eligible = move |satellite: &Satellite| {
            listening
                && satellite.signal.is_some()
                && reaches_mask(satellite.elevation.0, mask)
                && !ignored.contains(satellite.prn)
        };
        // Those above a satellite, a tie going to the one first.
        let above = move |index: usize, satellite: &Satellite| {
            record
                .satellites
                .iter()
                .enumerate()
                .filter(|&(other_index, other)| {
                    eligible(other)
                        && (other.elevation.0 > satellite.elevation.0
                            || (other.elevation.0 == satellite.elevation.0 && other_index < index))
                })
                .count()
        };
        record
            .satellites
            .iter()
            .enumerate()
            .filter(move |&(index, satellite)| {
                eligible(satellite) && (!highest || above(index, satellite) < channels)
            })
            .map(|(_, satellite)| satellite)
            .take(channels)
    }

    /// The satellites a position status counts as tracked: those on a
    /// family's channels at a tracking mode of [`TRACKING_LEAST`] or
    /// above, not every satellite a channel holds.  The fix, Time RAIM
    /// and the 1 PPS go by this count too.
    fn tracking(&self, record: &Record, family: &Family) -> usize {
        self.tracked(record, family)
            .filter(|satellite| satellite.tracking >= TRACKING_LEAST)
            .count()
    }

    /// A family's channels, each with the satellite on it if it has
    /// one.
    fn channels<'a>(
        &self,
        record: &'a Record,
        family: &Family,
    ) -> impl Iterator<Item = Option<&'a Satellite>> + use<'a> {
        self.tracked(record, family)
            .map(Some)
            .chain(core::iter::repeat(None))
            .take(family.channels)
    }

    /// The fix `tracked` satellites give, no better than the record's
    /// (VP pp. 72 and 73).  A PDOP above `@@Al`'s threshold holds the
    /// altitude, as three satellites do (VP pp. 39 and 60).
    fn solution(&self, record: &Record, tracked: usize) -> Solution {
        const NONE: Solution = Solution {
            dop: 0,
            dop_type: DOP_TYPE_PDOP,
            status: STATUS_ACQUIRING_OR_HOLD,
        };
        let position = record.dops.map_or(0, |dops| dops.position.0);
        let horizontal = record.dops.map_or(0, |dops| dops.horizontal.0);
        let three_d = position <= self.state.store.xdop_threshold();
        match (self.state.store.holding(), tracked, record.fix) {
            (true, _, _) | (false, _, Fix::None) => NONE,
            (false, _, Fix::Propagated) => Solution {
                status: STATUS_POSITION_PROPAGATE,
                ..NONE
            },
            (false, SATELLITES_3D.., Fix::ThreeD | Fix::TimeOnly) if three_d => Solution {
                dop: position,
                dop_type: DOP_TYPE_PDOP,
                status: STATUS_3D_FIX,
            },
            (false, SATELLITES_2D.., _) => Solution {
                dop: horizontal,
                dop_type: DOP_TYPE_HDOP,
                status: STATUS_2D_FIX,
            },
            (false, _, _) => NONE,
        }
    }

    /// The satellites in view as a position status counts them: the
    /// record's at or above the elevation the manual computes the number
    /// at ([`reaches_mask`]),
    /// not `@@Ag`'s; none while idle, nor with no almanac to tell (VP
    /// p. 70; UT p. 6.18).  `@@Bb` lists by its own elevation
    /// ([`Self::listed_from`]).
    fn visible(&self, record: &Record) -> usize {
        if !self.state.fixing || record.almanac.is_none() {
            return 0;
        }
        let from = match self.identity.model() {
            Model::VpSix | Model::VpEight => VISIBLE_FROM_VP,
            Model::UtPlus => VISIBLE_FROM_UT_PLUS,
        };
        record
            .satellites
            .iter()
            .filter(|satellite| reaches_mask(satellite.elevation.0, from))
            .count()
    }

    /// The receiver status byte of a position status (VP p. 73; UT
    /// p. 6.20).
    fn receiver_status(&self, record: &Record, solution: Solution, visible: usize) -> u8 {
        let poor_geometry = match self.identity.model() {
            Model::VpSix | Model::VpEight => POOR_GEOMETRY_VP,
            Model::UtPlus => POOR_GEOMETRY_UT_PLUS,
        };
        let fix = matches!(solution.status, STATUS_3D_FIX | STATUS_2D_FIX);
        [
            (true, solution.status),
            (solution.dop > poor_geometry, STATUS_POOR_GEOMETRY),
            (fix && record.differential, STATUS_DIFFERENTIAL),
            (visible < FEW_VISIBLE, STATUS_FEW_VISIBLE),
            (record.almanac.is_none(), STATUS_BAD_ALMANAC),
        ]
        .into_iter()
        .filter(|(set, _)| *set)
        .fold(0, |status, (_, bit)| status | bit)
    }

    /// Keeps the current position status as the last known valid one
    /// (VP pp. 70 and 124).
    fn keep_status(&mut self, record: &Record) {
        self.last_status = Some(KeptStatus::of(&self.status(record, &EIGHT)));
    }

    /// `@@Ba` and `@@Ea`.
    /// Idle, it is "the last known valid" one, kept each second while
    /// fixing, once more (VP pp. 70 and 124): one status for both
    /// families, `@@Ba` its first six channels.  Its date and time are
    /// the standing ones, though, as tests of a real VP showed: idle
    /// after `@@Cf`, its `@@Ea` gave the time it went idle at, and then
    /// those `@@Ac` and `@@Aa` set.  With none kept yet, it is the
    /// current one, which tracks nothing.
    fn position_status<O: Output + ?Sized>(
        &mut self,
        record: &Record,
        family: &Family,
        out: &mut O,
    ) {
        if self.state.fixing {
            self.keep_status(record);
        } else if let Some(kept) = &self.last_status {
            let mut kept = if family.index == SIX.index {
                kept.six()
            } else {
                *kept
            };
            kept.bytes[..STATUS_DATE].copy_from_slice(&self.date(record, self.clock));
            kept.bytes[STATUS_DATE..STATUS_DATE_TIME]
                .copy_from_slice(&self.time_of_day(record, self.clock));
            send(out, family.status, Data::new().bytes(kept.as_slice()));
            return;
        }
        send(out, family.status, &self.status(record, family));
    }

    /// The data of `@@Ba` and `@@Ea` (VP pp. 70 to 73 and 124 to 126;
    /// UT pp. 6.18 to 6.20).
    fn status(&self, record: &Record, family: &Family) -> Data {
        let ut_plus = self.identity.model() == Model::UtPlus;
        let place = self.place(record);
        let tracked = self.tracking(record, family);
        let antenna_sense = match (ut_plus, record.antenna) {
            (true, Antenna::Open) => DOP_TYPE_ANTENNA_UNDERCURRENT,
            (true, Antenna::Shorted) => DOP_TYPE_ANTENNA_OVERCURRENT,
            _ => 0,
        };
        let solution = self.solution(record, tracked);
        let isolated = self.traim_result(record, tracked).isolated;
        let visible = self.visible(record);
        let subsecond = match record.time {
            Some(time) if time.seconds == self.clock => time.subsecond.0,
            _ => 0,
        };
        let mut data = Data::new();
        data.bytes(&self.date(record, self.clock))
            .bytes(&self.time_of_day(record, self.clock))
            .long(subsecond)
            .signed_long(place.latitude)
            .signed_long(place.longitude)
            .signed_long(place.ellipsoid)
            .signed_long(if ut_plus { 0 } else { place.mean_sea_level })
            .word(record.velocity.map_or(0, |velocity| velocity.speed.0))
            .word(record.velocity.map_or(0, |velocity| velocity.heading.0))
            .word(solution.dop)
            .byte(solution.dop_type | antenna_sense)
            .byte(count_byte(visible))
            .byte(count_byte(tracked));
        for channel in self.channels(record, family) {
            match channel {
                Some(satellite) => {
                    let strength = satellite.signal.map_or(0, |signal| signal.0);
                    data.byte(satellite.prn.get())
                        .byte(tracking_mode(satellite.tracking))
                        .byte(if ut_plus { strength } else { signal(strength) })
                        .byte(self.channel_flags(satellite, isolated));
                }
                None => {
                    data.zeros(STATUS_CHANNEL);
                }
            }
        }
        data.byte(self.receiver_status(record, solution, visible));
        data
    }

    /// A channel's status flags (VP p. 73; UT p. 6.20): whether its
    /// satellite is used, what the satellite flags of itself and that
    /// it is unhealthy.  A UT Plus
    /// uses for time what it uses for position, less the `isolated`
    /// that Time RAIM removed (UT pp. 6.20, 6.93).
    fn channel_flags(&self, satellite: &Satellite, isolated: PrnSet) -> u8 {
        let ut_plus = self.identity.model() == Model::UtPlus;
        let for_time = !isolated.contains(satellite.prn);
        let used = match (satellite.used, ut_plus, for_time) {
            (false, _, _) => 0,
            (true, false, _) | (true, true, false) => CHANNEL_USED,
            (true, true, true) => CHANNEL_USED | CHANNEL_USED_FOR_TIME,
        };
        let unhealthy = match satellite.health {
            Health::Unhealthy => CHANNEL_UNHEALTHY,
            Health::Unknown | Health::Healthy => 0,
        };
        CHANNEL_FLAG_BITS
            .iter()
            .filter(|(flag, _)| satellite.flags.contains(*flag))
            .fold(used | unhealthy, |flags, (_, bit)| flags | bit)
    }

    /// `@@Bb` with `data`, kept to tell a change by.
    fn send_visibility<O: Output + ?Sized>(&mut self, data: [u8; VISIBILITY], out: &mut O) {
        self.visibility = Some(data);
        send(out, BB_VISIBLE_SATELLITES, Data::new().bytes(&data));
    }

    /// `@@Bb`'s data now.
    fn visibility_now(&self, record: &Record) -> [u8; VISIBILITY] {
        let mut data = [0; VISIBILITY];
        data.copy_from_slice(self.visibility(record).as_slice());
        data
    }

    /// A satellite alert computation: keeps `@@Bb`'s data for a poll
    /// and, in its mode 1, sends it if it differs from the last sent
    /// (VP pp. 74 and 75).
    fn alert_computation<O: Output + ?Sized>(&mut self, record: &Record, out: &mut O) {
        let alert = self.visibility_now(record);
        self.alert = Some(alert);
        if self.state.visibility_updates && self.visibility != Some(alert) {
            self.send_visibility(alert, out);
        }
    }

    /// The elevation, in degrees, a satellite must be at or above for
    /// `@@Bb` to list it: a VP's mask angle (`@@Ag`) and a UT Plus's
    /// horizon (UT p. 6.23).  For the VP, tests of a real eight-channel
    /// VP: with the mask at 10 or 20 degrees the lowest listed was at 21,
    /// with it at 0, satellites at 0, 6 and 8 degrees were listed, and
    /// one exactly at the mask was listed ([`reaches_mask`]); the
    /// six-channel VP is untested and taken to do the same.  None below
    /// the horizon is listed.
    fn listed_from(&self) -> i8 {
        match self.identity.model() {
            Model::VpSix | Model::VpEight => self.mask_angle(),
            Model::UtPlus => 0,
        }
        .max(0)
    }

    /// The data of `@@Bb` (VP p. 75): the record's satellites at or
    /// above [`Self::listed_from`], in the order of their IDs whatever
    /// the record's, one given twice listed once.  The manual gives no
    /// order; the receivers' firmware needs this one, finding a
    /// satellite in the `@@Bb` it keeps by bisecting on the ID
    /// (`satellite_find`, `screen.c` of each image under
    /// `third_party/firmware/rewritten/`).
    fn visibility(&self, record: &Record) -> Data {
        let from = self.listed_from();
        let by_id = (1..=PRNS)
            .filter_map(Prn::new)
            .filter_map(|prn| record.satellites.get(prn))
            .filter(move |satellite| reaches_mask(satellite.elevation.0, from));
        let count = by_id.clone().count();
        let mut data = Data::new();
        data.byte(count_byte(count));
        for satellite in by_id {
            data.byte(satellite.prn.get())
                .signed_word(satellite.doppler.map_or(0, |doppler| doppler.0))
                .byte(satellite.elevation.0.max(0).cast_unsigned())
                .word(satellite.azimuth.0)
                .byte(self.visible_health(satellite));
        }
        data.zeros((SATELLITES - count) * VISIBLE_SLOT);
        data
    }

    /// A satellite's health as `@@Bb` gives it (VP p. 75).
    fn visible_health(&self, satellite: &Satellite) -> u8 {
        let unhealthy = match satellite.health {
            Health::Unhealthy => VISIBLE_UNHEALTHY,
            Health::Unknown | Health::Healthy => 0,
        };
        if self.state.store.ignore_list().contains(satellite.prn) {
            unhealthy | VISIBLE_REMOVED
        } else {
            unhealthy
        }
    }

    /// `@@Bc` (VP pp. 76 and 77): the kind of DOP `@@Aj` asks for, and
    /// the record's combinations if they are of that kind.  The
    /// satellites a combination holds, Best-4 or N-in-View, is the most
    /// any of them names; six with none.
    fn xdop_table(&self, record: &Record) -> Data {
        let asked = self.state.store.dop_type();
        let table = record
            .derived
            .xdops
            .filter(|table| dop_kind_code(table.kind()) == asked);
        let combinations = table.iter().flat_map(XdopTable::iter);
        let count = combinations.clone().count();
        let taken = combinations
            .clone()
            .map(XdopCombination::len)
            .max()
            .unwrap_or(XDOP_SATELLITES);
        let mut data = Data::new();
        data.byte(asked)
            .byte(count_byte(count))
            .byte(count_byte(taken));
        for combination in combinations {
            data.word(combination.dop.0);
            for satellite in combination.satellites {
                data.byte(satellite.map_or(0, Prn::get));
            }
        }
        data.zeros((XDOP_COMBINATIONS - count) * XDOP_ENTRY);
        data
    }

    /// `@@Bc`, kept to tell a change by.
    fn send_xdop_table<O: Output + ?Sized>(&mut self, record: &Record, out: &mut O) {
        let data = self.xdop_table(record);
        self.xdops = kept(&data);
        send(out, BC_XDOP_TABLE, &data);
    }

    /// `@@Bd`, kept to tell a change by.
    fn send_almanac_status<O: Output + ?Sized>(&mut self, record: &Record, out: &mut O) {
        let data = Self::almanac_status(record);
        self.almanac = kept(&data);
        send(out, BD_ALMANAC_STATUS, &data);
    }

    /// `@@Bd` (VP p. 79): the almanac's status, given twice, as the one
    /// in use and the one last read.
    fn almanac_status(record: &Record) -> Data {
        let mut status = Data::new();
        match record.almanac {
            Some(almanac) => status
                .byte(ALMANAC_VALID)
                .byte(almanac.week.almanac())
                .byte(almanac.time_of_almanac)
                .byte(count_byte(almanac.held.len() as usize))
                .long(almanac.held.bits()),
            None => status.zeros(ALMANAC_STATUS),
        };
        let mut data = Data::new();
        data.bytes(status.as_slice()).bytes(status.as_slice());
        data
    }

    /// `@@Ce` (VP pp. 89, 90, 114 and 115): the record's corrections
    /// and their time, or with none, the engine's time and no
    /// corrections.  A message carries six: an engine with more
    /// channels whose record has a correction past the sixth sends a
    /// second straight after, for the same time, with the rest in its
    /// first fields and zeros in the others (VP p. 89).  One with six
    /// channels sends the first six and no more.
    fn corrections<O: Output + ?Sized>(&self, record: &Record, out: &mut O) {
        let set = record
            .derived
            .corrections
            .unwrap_or_else(|| Corrections::new(self.clock.time_of_week().deciseconds()));
        let (first, rest) = set.satellites.split_at(CORRECTIONS_EACH);
        Self::correction_message(&set, first, out);
        let rest = &rest[..self
            .channels
            .saturating_sub(CORRECTIONS_EACH)
            .min(rest.len())];
        if rest.iter().any(Option::is_some) {
            Self::correction_message(&set, rest, out);
        }
    }

    /// One `@@Ce` (VP pp. 90 and 115): the set's time and `slots`, at
    /// most six, then zeros for the fields they leave.
    fn correction_message<O: Output + ?Sized>(
        set: &Corrections,
        slots: &[Option<Correction>],
        out: &mut O,
    ) {
        let mut data = Data::new();
        data.triple(set.time.0.min(TIME_OF_WEEK_MOST));
        for slot in slots {
            match slot {
                Some(correction) => {
                    let range = correction
                        .range
                        .0
                        .clamp(-CORRECTION_RANGE_MOST, CORRECTION_RANGE_MOST);
                    let rate = correction
                        .rate
                        .0
                        .clamp(-CORRECTION_RATE_MOST, CORRECTION_RATE_MOST);
                    data.byte(correction.prn.get())
                        .triple(range.cast_unsigned())
                        .signed_word(rate)
                        .byte(correction.ephemeris_issue);
                }
                None => {
                    data.zeros(CORRECTION_ENTRY);
                }
            }
        }
        data.zeros(CORRECTION_ENTRY * CORRECTIONS_EACH.saturating_sub(slots.len()));
        send(out, CE_PSEUDORANGE_CORRECTION, &data);
    }

    /// `@@Bk` and `@@Ek` (VP pp. 95 to 98 and 134 to 137).  A channel
    /// with no URA decoded, or with no satellite, has the manual's
    /// value for that.
    fn extension<O: Output + ?Sized>(&self, record: &Record, family: &Family, out: &mut O) {
        let mut data = Data::new();
        match record.dops {
            Some(dops) => data
                .word(dops.geometric.0)
                .word(dops.position.0)
                .word(dops.horizontal.0)
                .word(dops.vertical.0)
                .word(dops.time.0),
            None => data.zeros(EXTENSION_DOPS * size_of::<u16>()),
        };
        let derived = &record.derived;
        data.signed_word(derived.magnetic_variation.map_or(0, |variation| {
            variation.0.clamp(-VARIATION_MOST, VARIATION_MOST)
        }));
        match derived.velocity {
            Some(velocity) => data
                .signed_word(velocity.north.0.max(SIGNED_LEAST))
                .signed_word(velocity.east.0.max(SIGNED_LEAST))
                .signed_word(velocity.up.0.max(SIGNED_LEAST)),
            None => data.zeros(AXES * size_of::<i16>()),
        };
        let age = record.differential_age.map_or(0, |age| age.0);
        data.word(u16::try_from(age).unwrap_or(u16::MAX));
        match derived.ecef {
            Some(ecef) => data
                .signed_long(ecef.x.0)
                .signed_long(ecef.y.0)
                .signed_long(ecef.z.0),
            None => data.zeros(AXES * size_of::<i32>()),
        };
        for channel in self.channels(record, family) {
            let ura = channel.and_then(|satellite| satellite.ura);
            data.byte(ura.map_or(URA_NOT_DECODED, Ura::get));
        }
        let solver = derived.solver.unwrap_or(Solver::ZERO);
        for element in solver.matrix {
            data.signed_word(element);
        }
        data.signed_word(solver.clock_bias.0.max(SIGNED_LEAST))
            .word(solver.oscillator_offset.0);
        send(out, family.extension, &data);
    }

    /// What Time RAIM can do with `tracked` satellites (VP p. 106).
    fn traim_ability(&self, tracked: usize) -> u8 {
        let limit = self.state.traim.alarm_limit;
        match TRAIM_NEEDS.iter().find(|need| limit >= need.from) {
            Some(need) if tracked >= need.isolate && self.state.store.holding() => TRAIM_ISOLATION,
            Some(need) if tracked >= need.detect => TRAIM_DETECTION,
            _ => TRAIM_NEITHER,
        }
    }

    /// What Time RAIM reports with `tracked` satellites: the record's
    /// result while the algorithm is on, as far as the engine's own
    /// goes, else the engine's own.  Off, with the alarm limit too low
    /// or with too few satellites tracked, the solution is unknown (VP
    /// p. 105) and nothing is isolated, whatever the record has: it
    /// knows of neither the limit nor which satellites the engine was
    /// set to track.  Out of position hold, it can only detect (UT
    /// p. 6.93).
    fn traim_result(&self, record: &Record, tracked: usize) -> TraimResult {
        let holding = self.state.store.holding();
        let on = self.state.traim.algorithm;
        let able = self.traim_ability(tracked);
        match record.derived.raim.filter(|_| on && able != TRAIM_NEITHER) {
            Some(raim) => TraimResult {
                solution: match raim.solution {
                    RaimSolution::Ok => TRAIM_SOLUTION_OK,
                    RaimSolution::Alarm => TRAIM_SOLUTION_ALARM,
                    RaimSolution::Unknown => TRAIM_SOLUTION_UNKNOWN,
                },
                // The less able of the record's and the engine's.
                ability: match raim.ability {
                    RaimAbility::Isolation => TRAIM_ISOLATION,
                    RaimAbility::Detection => TRAIM_DETECTION,
                    RaimAbility::Neither => TRAIM_NEITHER,
                }
                .max(able),
                isolated: if holding {
                    raim.isolated
                } else {
                    PrnSet::EMPTY
                },
            },
            None => TraimResult {
                solution: if on && able != TRAIM_NEITHER {
                    TRAIM_SOLUTION_OK
                } else {
                    TRAIM_SOLUTION_UNKNOWN
                },
                ability: able,
                isolated: PrnSet::EMPTY,
            },
        }
    }

    /// `@@Bn` and `@@En` (VP pp. 102 to 106 and 138 to 142; UT
    /// pp. 6.92 to 6.95).
    fn traim_status<O: Output + ?Sized>(&self, record: &Record, family: &Family, out: &mut O) {
        let setup = self.state.traim;
        let tracked = self.tracking(record, family);
        let TraimResult {
            solution, ability, ..
        } = self.traim_result(record, tracked);
        let pulse = match setup.pulse_mode {
            PulseMode::Off => false,
            PulseMode::Always => true,
            PulseMode::Tracking => tracked != 0,
            PulseMode::Confirmed => solution == TRAIM_SOLUTION_OK,
        };
        let timing = record.timing.filter(|_| tracked >= SPREAD_OF);
        let mut data = Data::new();
        data.byte(self.state.rates[family.index].traim.0)
            .byte(setup.algorithm_byte())
            .word(setup.alarm_limit.0)
            .byte(setup.pps_mode());
        match self.identity.model() {
            Model::VpSix | Model::VpEight => {
                let next = self.clock + Seconds(1);
                data.triple(PPS_RATE)
                    .bytes(&self.date(record, next))
                    .bytes(&self.time_of_day(record, next));
            }
            Model::UtPlus => {
                data.zeros(NEXT_PULSE);
            }
        }
        data.byte(u8::from(pulse))
            .byte(u8::from(self.state.store.gps_time()))
            .byte(solution)
            .byte(ability)
            .word(timing.map_or(0, |timing| timing.sigma.0))
            .signed_byte(record.timing.map_or(0, |timing| timing.sawtooth.0));
        for channel in self.channels(record, family) {
            // A satellite's time is sent as the fraction of a second
            // its solution falls at: its error, counted on from the
            // second or back from the next.
            let fraction = channel
                .and_then(|satellite| satellite.time_error)
                .map_or(0, |error| error.0.rem_euclid(NANOSECONDS_PER_SECOND))
                .cast_unsigned();
            data.byte(channel.map_or(0, |satellite| satellite.prn.get()))
                .long(fraction);
        }
        send(out, family.traim, &data);
    }

    /// `@@Gj` (M12+ pp. 152 to 153): the leap seconds before and after
    /// the leap the record announces, when it takes effect, and GPS
    /// less UTC now.  With no leap announced, the date and time are
    /// zero.
    fn leap_details<O: Output + ?Sized>(&self, record: &Record, out: &mut O) {
        let now = self.utc_offset(record, self.clock);
        let (present, future, applied) = match record.leap {
            Some(leap) => {
                let before = if leap.pending(self.clock) {
                    now
                } else {
                    Seconds(now.0.wrapping_sub(leap.step().0))
                };
                let after = Seconds(before.0.wrapping_add(leap.step().0));
                // An insertion takes effect in the second it adds; a
                // removal after the last second before the one it
                // drops.
                let (second, offset) = match leap.change {
                    LeapChange::Insert => (leap.at, after),
                    LeapChange::Remove => (leap.at - Seconds(1), before),
                };
                (before, after, DateTime::of(second, offset, Some(leap)))
            }
            None => (now, now, None),
        };
        let current = if self.state.store.gps_time() {
            0
        } else {
            self.wire_offset(record)
        };
        let mut data = Data::new();
        data.byte(present.0.cast_unsigned())
            .byte(future.0.cast_unsigned());
        match applied {
            Some(applied) => data
                .word(applied.year)
                .byte(applied.month)
                .byte(applied.day),
            None => data.zeros(4),
        };
        data.byte(current).long(0);
        match applied {
            Some(applied) => data
                .byte(applied.hour)
                .byte(applied.minute)
                .byte(applied.second),
            None => data.zeros(3),
        };
        send(out, GJ_LEAP_SECOND_PENDING, &data);
    }

    /// `@@Cj` (VP p. 123).
    fn receiver_id<O: Output + ?Sized>(&self, out: &mut O) {
        let mut data = Data::new();
        for line in self.identity.lines() {
            data.bytes(LINE_END).bytes(line.as_bytes());
        }
        send(out, CJ_RECEIVER_ID, &data);
    }
}

/// Frames `data` as `id` and hands it on.
fn send<O: Output + ?Sized>(out: &mut O, id: MessageId, data: &Data) {
    let mut frame = [0; MAX_RESPONSE];
    if let Ok(length) = encode(id, data.as_slice(), &mut frame) {
        out.send(&frame[..length]);
    }
}

/// A message's data as an engine keeps it to tell a change by.
fn kept<const N: usize>(data: &Data) -> Option<[u8; N]> {
    data.as_slice().try_into().ok()
}

/// A count as a message's byte carries it.
fn count_byte(count: usize) -> u8 {
    u8::try_from(count).unwrap_or(u8::MAX)
}

/// A VP's signal strength for a C/N0.
fn signal(carrier_to_noise: u8) -> u8 {
    SIGNAL[usize::from(carrier_to_noise).min(SIGNAL.len() - 1)]
}

/// A channel's tracking mode (VP p. 72).
const fn tracking_mode(tracking: Tracking) -> u8 {
    match tracking {
        Tracking::Searching => 0,
        Tracking::CodeLock => 1,
        Tracking::AgcSet => 2,
        Tracking::CarrierLock => 3,
        Tracking::BitSync => 4,
        Tracking::MessageSync => 5,
        Tracking::FrameSync => 6,
        Tracking::Ephemeris => 7,
        Tracking::Position => 8,
    }
}

#[cfg(test)]
mod tests;
