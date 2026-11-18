//! A model that stands in for the Z3815A's Furuno GT-74: it takes the
//! sentences the Z3815A's firmware sends and a [`Record`] each second,
//! and writes the NMEA sentences, Furuno's `$PFEC` ones among them, that
//! the firmware decodes.
//!
//! The GT-74's own protocol specification was not found; the nearest at
//! hand is Furuno's for the GT-8031 (GT,
//! `third_party/furuno/Furuno_GT8031_Protocol_Specification_Rev1.pdf`,
//! cited by printed page, and its hardware specification, HW,
//! `third_party/furuno/Furuno_GT8031_Hardware_Specification_2005.04.28.pdf`,
//! likewise), and the image, the Z3815A 4010
//! (`third_party/firmware/rewritten/z3815a-4010/gps_nmea.c`, its tables
//! read from `third_party/firmware/images/z3815a-4010.bin`), decides
//! wherever the two part or the specification is silent.  Each such
//! decision is listed below, under "Assumptions".  The Z3805A 1.01.03
//! speaks the same protocol, and one sentence more, `acc`
//! (`third_party/firmware/rewritten/z3805a-1.01.03/gps_nmea.c`).
//!
//! # What it speaks
//!
//! The sentences are those the images send and decode
//! ([`SENTENCES`](crate::nmea::SENTENCES)), and no other:
//!
//! - It sends `GGA`, `GSA`, `GSV`, `RMC`, `anc`, `acc`, `tst`, `tps` and
//!   `rrm` at the intervals `int` sets (GT p. 13), in the order of GT
//!   p. 4's priorities, and once at an `int` of interval 0, straight
//!   after it;
//!   `ssd` in answer to `srq` (GT pp. 12 and 32) and `rsd` to `rrq` (GT
//!   pp. 15 and 40).
//! - It takes `set` (GT p. 11), `srq`, `int`, `rrs`, `rrq`, `clr` (GT
//!   p. 10), `ZDA` (GT p. 8) and `GLL` (GT p. 6), and ignores any other
//!   sentence: the image's command monitor sends `$PFEC,GPzzz` (its steps
//!   at `0x5e824` and `0x5e828` are of mode 0), which no specification
//!   gives.
//! - `int` takes `ZDA` and `VTG` as it takes the others, the image
//!   sending `int,ZDA00` and `int,VTG00` in its start-up scripts, but
//!   the engine sends neither sentence: the image does not decode them.
//!
//! A command is one of three kinds, as an Oncore's are
//! ([module](super)).  The engine does what changes the bytes it builds:
//! the intervals, the clock `ZDA` sets while the record tells no time,
//! the position reported in fixed observation point mode, the satellites
//! the mask and `set,U` leave out of the fix, the parameters `ssd` and
//! `rsd` report back.  What changes how the receiver tracks, fixes or
//! puts out its pulse it keeps in its [`Settings`] and tells its holder
//! of ([`Events`]): the mask angle, the satellites to ignore, position
//! hold and the position, the 1 PPS's correction, Time RAIM and the
//! pulse's control mode.  `clr` restarts it, an [`Event::Reset`].  The
//! rest of `set`'s parameters, which the image never sends, it keeps and
//! reports, and does nothing more with: gaps (`docs/firmware/gps.md`,
//! "The fields").
//!
//! [`Event::Reset`]: crate::engine::event::Event::Reset
//!
//! # Timing
//!
//! A GT-8031 sends each second's sentences between two pulses, starting
//! 500 ms, give or take 2 ms, after the first, and they are of the
//! second the next pulse marks (HW p. 9, Fig. 2.2.2.4: "time = t serial
//! data" between the pulses of t - 1 and t); `tps`'s 1 PPS flag says
//! the pulse "will be output following this sentence" (GT p. 36).  How
//! long the sentences take is the rest of the second less a gap that
//! narrows as there are more of them, over 250 ms with `tps` alone (HW
//! p. 9).  So whoever holds the engine calls [`Engine::second`] once a
//! second, [`SENTENCES_AFTER_PULSE`] after a pulse
//! ([`Engine::sentences_after_pulse`]), with the record of the second
//! the next pulse marks, and [`Engine::receive`] with the bytes the
//! receiver sends; the engine counts the seconds it is given and sends
//! a sentence when that count is a multiple of its interval.  Its clock
//! is the record's time; with none, it runs on from its power-up time,
//! 2002-01-01 00:00:00 (GT p. 5), which `ZDA` sets.  Nothing it answers
//! waits.
//!
//! # Assumptions
//!
//! Where the GT-8031's specification and the Z3815A image part, or the
//! specification says nothing:
//!
//! - **One `ssd` sentence of thirteen parameters, 82 bytes.**  GT p. 32
//!   has the parameters "output in two sentences"; the image takes one,
//!   of exactly 82 bytes and 15 fields, and reads `H` at field 6, `M` at
//!   7, `U` at 10, `Z` at 13 and `t` at 14, each of the length
//!   `set` gives it (its table at `0x5e1ea`).  That leaves eight fields
//!   of 25 characters: `A`, `D`, `G`, `S`, `T`, `W` and `X`, 22 of them,
//!   and one more of three, which the GT-8031 does not have.  The fields
//!   are taken to be in the order of their letters, as the five the
//!   image reads are, and the one more to be at field 4, between `D`
//!   and `G`; it is sent as `E06`, a letter and a value made up to give
//!   the image its length.  `Z` is its mode alone, two characters, `Z3`'s
//!   averaging time not among them: with it the sentence would be 84
//!   bytes, past the 82 a sentence may be (GT p. 2).
//! - **`T` and `t` are one correction** (GT p. 11 gives both as "1PPS
//!   Correction", p. 5 one), `T` in tenths of a microsecond and `t` in
//!   nanoseconds; `ssd` gives `T` as `t` truncated to its unit.
//! - **`tps` every second at power-up**: GT pp. 4 and 5 have it among
//!   the sentences sent each second until told otherwise, p. 13
//!   `{tps00}`; the two pages win.
//! - **Time RAIM off at power-up**: GT p. 15 has `{0}`, p. 5 "1 (On)";
//!   the sentence's own page wins.
//! - **`rrm` reports four isolated satellites**, in fields 4 to 7: GT
//!   p. 40 has one in field 4 and fields 5 to 7 reserved; the image reads
//!   all four as satellites (`nmea_decode_4_satellites`).  Field 8,
//!   reserved, is sent as GT p. 40's example has it, `+000`.  Time RAIM
//!   works only in fixed observation point mode (GT p. 15), detecting a
//!   bad satellite among two used and isolating one among three or more
//!   (GT p. 47); otherwise the result is "not sufficient" and "not
//!   possible", as with the algorithm off.
//! - **`GSV` lists satellites in the order of their IDs.**  GT p. 23
//!   gives no order; the image gathers each sentence's satellites only
//!   while each is above the last (`nmea_gsv_gather`).  It lists those
//!   at or above 5 degrees, GT p. 23's range, and gives a satellite with
//!   no signal an SNR of `00`: the image takes each satellite's fields at
//!   their full length or not at all.
//! - **`RMC` has eleven fields**, and its checksum: GT p. 25 has a
//!   twelfth, the positioning system mode indication, which NMEA 0183
//!   added after the version 2.0 GT p. 1 names.  The image counts 13
//!   field ends in an `RMC` (its table at `0x5e320`), and its framer
//!   ends one at the `*` and another at the CR after the checksum
//!   (`nmea_frame_is_star`, `nmea_frame_is_cr_after_checksum`), so it
//!   takes eleven fields with a checksum.  It reads none of them.
//! - **No differential fix.**  `GGA`'s last two fields are null "unless
//!   DGPS mode is selected" (GT p. 19), which only `$PFEC,GPwas` does;
//!   the image sends none, and takes `GGA` only at the length null
//!   fields give.  So a record's differential fix is reported as a GPS
//!   one.
//! - **In fixed observation point mode there is no fix**: the GT-8031
//!   "does not perform position fixing in this mode of operation, but it
//!   merely outputs the position data which you entered" (GT p. 46).
//!   So `GGA` and `RMC` give the position entered, `GGA`'s quality is 0
//!   and `GSA`'s mode 1.  The satellites it uses for time are counted in
//!   `GGA` and listed in `GSA` all the same: the image puts `GSA`'s on
//!   its channels, as available for position, and has no other list of
//!   those in use (`nmea_gsa_commit`).
//! - **One position entered**, by `GLL` (GT p. 6) and `set,H` (GT
//!   p. 11): where the GT-8031 starts looking from (GT p. 6, "Set initial
//!   position") and, in fixed observation point mode, where it is (GT
//!   p. 46).  [`Settings::initial_position`] and
//!   [`Settings::held_position`] both give it; its height is above mean
//!   sea level, as `GGA`'s altitude is (GT p. 19) and the image's `set,H`
//!   sends it (`nmea_encode_height`).  `GGA` reports it while the record
//!   has no position, and at power-up it is GT p. 5's, N34 44.0000,
//!   E135 21.0000, 0 m.
//! - **`set,t` puts the pulse earlier or later**: a negative correction
//!   advances it (GT p. 11), as [`Settings::cable_delay`] does, and a
//!   positive one, which an Oncore has no cable delay for, is
//!   [`Settings::pulse_offset`].  For the software the image adds 400 ns
//!   to `t` for, [`GT_74_PULSE_EARLY`], the settings take those 400 ns
//!   back off, its pulse being taken to come that much early.
//! - **`rrs`'s alarm limit** is in tens of nanoseconds (GT p. 15) and
//!   [`Settings::raim_alarm_limit`] in hundreds: the setting is the
//!   limit truncated to its unit, `rsd` the limit as sent.  The image
//!   sends a multiple of ten (`nmea_encode_tenths`).
//! - **`ZDA` and `GLL` from a `GP` talker alone**: GT pp. 6 and 8 take
//!   any talker; the image sends `GP` and [`Sentence::address`]
//!   recognizes no other.  `ZDA` sets the clock only while the record
//!   tells no time, as an Oncore tracking a satellite ignores the time
//!   it is given; its local zone time always applies.  The zone's
//!   minutes take its hours' sign, that of `-00` too: GT p. 8 shows the
//!   hours as "-13 ... +00 ... +13", and `-00,30` is taken to be a zone
//!   of -30 minutes.
//! - **`acc` gives the URA the record has**, a hex digit for each
//!   satellite 1 to 32 that has one, the URA index it sends (VP p. 98's
//!   0 to 15), and GT p. 28's `X`, "SV Accuracy not available", for
//!   the rest.  GT p. 28 does not say which accuracy the column is; a
//!   hex digit, 0 to F, is the navigation message's URA index, and the
//!   1.01.03 marks a satellite whose digit is above 6 in its channel's
//!   status bit 3 (`nmea_decode_satellite_bits`, `nmea_acc_commit`), an
//!   Oncore's "satellite reported inaccurate" (VP p. 73).  Like the
//!   other sentences but `RMC` it is sent with no checksum: the
//!   1.01.03 takes it only at the 46 bytes that gives (its table at
//!   `0x54dee`), where GT p. 13 counts 49 with one.
//! - **`anc`'s date is its almanac's reference time**, in local time
//!   (GT p. 27: "Almanac Date/Time (Local Date/Time)"), the zone `ZDA`
//!   gave taken off UTC (GT p. 8); GT p. 27 says the almanac is
//!   "obtained" then, which a record does not tell.  With no almanac it
//!   is GT p. 5's almanac date, 1980-01-06 00:00:00.  `tps`'s field 9,
//!   when the UTC parameter was last updated, is the same reference
//!   time, the parameter coming with the almanac (GT p. 38), and zeros
//!   until the record has both.
//! - **A leap second not announced is `00`, at a date of zeros.**  GT
//!   p. 37 has an engine that has seen none report the last one's date
//!   with `00`, which a record does not tell.  One announced is reported
//!   from then on, past or pending (GT p. 37), until `clr` 1 or 3, and
//!   again from the next record that announces it.
//! - **The time standard** of `tps` (GT p. 36) is the RTC's while the
//!   record tells no time, GPS time while it tells no offset to UTC, and
//!   UTC once it does.  The image's names for the values (`nmea_time_mode`,
//!   `smartclock.h`) have 2 for UTC and 3 for GPS time; what it does with
//!   them is GT p. 36's, 3 being UTC: with its receiver on GPS time and
//!   the engine on UTC it takes the time from the week and seconds
//!   (`nmea_tps_time_commit`).
//! - **The 1 PPS availability flag** (GT p. 36) follows GT p. 47: never
//!   in control mode 0, always in mode 1, and in modes 2 and 3 once the
//!   almanac and the offset to UTC are held, with a satellite used, two
//!   in mode 3 in fixed observation point mode, and, in estimated
//!   observation point mode, a first fix made since power-up, one of
//!   four satellites or more usable with a PDOP under 6; mode 3 drops
//!   it at a Time RAIM alarm.
//! - **`clr`** restarts the engine as a power-up would (GT p. 10): mode
//!   1 puts every parameter of `set`, the position and, while the record
//!   tells no time, the clock back to their defaults, and the intervals,
//!   which are not among `set`'s parameters (GT p. 10, note 1) and are
//!   backed up (GT p. 5), stay; every mode puts back the parameters GT
//!   p. 5 does not back up, the mask by signal strength, the satellites
//!   to ignore, the smoothing and dynamic indexes, the Time RAIM switch
//!   and the 1 PPS control mode.  Modes 1 and 3 forget the leap second
//!   (GT p. 37).
//! - **Ranges are the specification's**: a parameter out of its range
//!   is ignored, the rest of its sentence taken.
//!
//! The image's identification template reads `MODEL # FURUNO GT-74`
//! whatever the engine (`nmea_tst_commit`, `0x5d298`); what it takes
//! from the engine is `tst`'s program number and version, which an
//! [`Identity`] holds, and from them whether the engine has Time RAIM
//! (`gps_receiver_abilities_update`, `0x5c00e`).

use core::fmt::Arguments;
use core::fmt::Result as FmtResult;
use core::fmt::Write;

use crate::engine::Output;
use crate::engine::event::Events;
use crate::engine::reaches_mask;
use crate::engine::settings::Height;
use crate::engine::settings::HeightKind;
use crate::engine::settings::Location;
use crate::engine::settings::PulseMode;
use crate::engine::settings::Settings;
use crate::engine::settings::TimeBase;
use crate::engine::store::Store;
use crate::nmea::Checksum;
use crate::nmea::Class;
use crate::nmea::Deframer;
use crate::nmea::MAX_SENTENCE;
use crate::nmea::Sentence;
use crate::nmea::encode;
use crate::record::Antenna;
use crate::record::Fault;
use crate::record::Fix;
use crate::record::Position;
use crate::record::RaimAbility;
use crate::record::RaimSolution;
use crate::record::Record;
use crate::record::Satellite;
use crate::time::DateTime;
use crate::time::GpsSeconds;
use crate::time::LeapChange;
use crate::time::LeapEvent;
use crate::time::TimeOfWeek;
use crate::units::Centimeters;
use crate::units::Degrees;
use crate::units::HundredNanoseconds;
use crate::units::MilliArcSeconds;
use crate::units::Milliseconds;
use crate::units::Nanoseconds;
use crate::units::PRNS;
use crate::units::Prn;
use crate::units::PrnSet;
use crate::units::Seconds;
use crate::units::Tenths;

/// The digits of `tst`'s program number and of its version (GT p. 31).
const PROGRAM_DIGITS: u32 = 7;
const VERSION_DIGITS: u32 = 3;
/// The most a program number and a version can be in those digits.
const PROGRAM_MOST: u32 = 9_999_999;
const VERSION_MOST: u16 = 999;

/// One GT-74: what its `tst` gives as its software (GT p. 31), and
/// whether it has Time RAIM, which the image decides by that software
/// (`gps_receiver_abilities_update`).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Identity {
    /// The program number, seven digits.
    program: u32,
    /// The version, three digits.
    version: u16,
    /// Whether it takes `rrs` and `rrq` and sends `rrm` and `rsd`.
    time_raim: bool,
    /// How much earlier than its `t` says its software puts the 1 PPS
    /// out.
    pulse_early: Nanoseconds<u16>,
}

impl Identity {
    /// A GT-74 of program `program` and version `version`, its pulse
    /// where `t` puts it.  `None` if either is more than its digits
    /// hold.
    #[must_use]
    pub const fn new(program: u32, version: u16, time_raim: bool) -> Option<Self> {
        if program > PROGRAM_MOST || version > VERSION_MOST {
            return None;
        }
        Some(Self {
            program,
            version,
            time_raim,
            pulse_early: Nanoseconds(0),
        })
    }

    /// The same GT-74, its software putting the 1 PPS out `early` before
    /// where `t` puts it.
    #[must_use]
    pub const fn with_pulse_early(self, early: Nanoseconds<u16>) -> Self {
        Self {
            pulse_early: early,
            ..self
        }
    }

    /// How much earlier than `t` says its software puts the 1 PPS out.
    #[must_use]
    pub const fn pulse_early(&self) -> Nanoseconds<u16> {
        self.pulse_early
    }

    /// Its program number.
    #[must_use]
    pub const fn program(&self) -> u32 {
        self.program
    }

    /// Its version.
    #[must_use]
    pub const fn version(&self) -> u16 {
        self.version
    }

    /// Whether it has Time RAIM.
    #[must_use]
    pub const fn time_raim(&self) -> bool {
        self.time_raim
    }
}

/// How long after a pulse a GT-8031 starts the sentences of the second
/// the next pulse marks, 500 ms give or take 2 ms (HW p. 9, Fig.
/// 2.2.2.4; [module](self), "Timing").
pub const SENTENCES_AFTER_PULSE: Milliseconds<u16> = Milliseconds(500);

/// A GT-74 with Time RAIM, its program number and version made up.
pub const GT_74: Identity = Identity {
    program: 4_850_120,
    version: 1,
    time_raim: true,
    pulse_early: Nanoseconds(0),
};

/// The GT-74 the image knows to have no Time RAIM: program `0x4a01c1`,
/// 4850113, version 2 (`GPS_SOFTWARE_NO_RAIM`, `gps_nmea.c`).
pub const GT_74_WITHOUT_TIME_RAIM: Identity = Identity {
    program: 4_850_113,
    version: 2,
    time_raim: false,
    pulse_early: Nanoseconds(0),
};

/// The GT-74 whose cable delay the image corrects: program `0x4a01b6`,
/// 4850102, version 8, to whose `t` the image adds 400 ns, sending
/// `-(delay - 400)` (`GPS_SOFTWARE_CABLE_DELAY`,
/// `GPS_CABLE_DELAY_CORRECTION`, `nmea_encode_cable_delay`).  That its
/// software puts the pulse 400 ns earlier than `t` says, which the
/// correction makes good, is inferred from the correction; so its
/// [`Settings::cable_delay`] is the delay the image was asked for.
pub const GT_74_PULSE_EARLY: Identity = Identity {
    program: 4_850_102,
    version: 8,
    time_raim: true,
    pulse_early: Nanoseconds(400),
};

/// A sentence the engine sends, in the order GT p. 4 gives them
/// priority.  `ZDA` and `VTG` are here because `int` sets their
/// intervals; the engine never sends them.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum Periodic {
    Gga,
    Zda,
    Gsa,
    Gsv,
    Vtg,
    Rmc,
    Anc,
    Acc,
    Tst,
    Tps,
    Rrm,
}

/// The sentences `int` sets, in their priority.
const PERIODIC: [Periodic; 11] = [
    Periodic::Gga,
    Periodic::Zda,
    Periodic::Gsa,
    Periodic::Gsv,
    Periodic::Vtg,
    Periodic::Rmc,
    Periodic::Anc,
    Periodic::Acc,
    Periodic::Tst,
    Periodic::Tps,
    Periodic::Rrm,
];

impl Periodic {
    /// Its name in `int` (GT p. 13).
    const fn name(self) -> &'static [u8] {
        match self {
            Self::Gga => b"GGA",
            Self::Zda => b"ZDA",
            Self::Gsa => b"GSA",
            Self::Gsv => b"GSV",
            Self::Vtg => b"VTG",
            Self::Rmc => b"RMC",
            Self::Anc => b"anc",
            Self::Acc => b"acc",
            Self::Tst => b"tst",
            Self::Tps => b"tps",
            Self::Rrm => b"rrm",
        }
    }

    /// Its interval at power-up with no backup, in seconds: those GT
    /// p. 4 marks as going back to one second, which p. 5 lists (and
    /// "Assumptions" for `tps`).
    const fn power_up(self) -> u8 {
        match self {
            Self::Gga | Self::Zda | Self::Gsv | Self::Vtg | Self::Tps => 1,
            Self::Gsa | Self::Rmc | Self::Anc | Self::Acc | Self::Tst | Self::Rrm => 0,
        }
    }

    /// Where its interval is kept.
    const fn index(self) -> usize {
        self as usize
    }
}

/// The longest interval `int` sets (GT p. 13).
const INTERVAL_MOST: u8 = 60;
/// The characters of a sentence's name in `int`, and of its interval.
const NAME_LENGTH: usize = 3;
const INTERVAL_DIGITS: usize = 2;

/// `set`'s parameters (GT p. 11), each as the engine keeps it.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
struct Parameters {
    /// `A`: 1 almanacs expire, 2 they do not.
    almanac_validity: u8,
    /// `D`: the PDOP above which a 3D fix is given up for a 2D one, 0
    /// for none ever.
    pdop_threshold: u8,
    /// `G`: the geodetic datum (GT p. 51).
    datum: u8,
    /// `H`: the height entered, in centimeters above mean sea level.
    height: Centimeters,
    /// `M`: the elevation mask.
    mask_angle: Degrees<u8>,
    /// `S`: the mask by signal strength, in dB-Hz.
    signal_mask: u8,
    /// `t`, and `T` in its unit: the 1 PPS correction, negative earlier.
    correction: Nanoseconds<i32>,
    /// `U`: the satellites to ignore.
    ignored: PrnSet,
    /// `W`: the smoothing index.
    smoothing: u8,
    /// `X`: the dynamic index.
    dynamic: u8,
    /// `Z`: the observation point mode, 1 to 4.
    observation: u8,
    /// `Z3`'s averaging time, in hours.
    averaging: u8,
}

/// `Z`'s modes (GT p. 12).
const ESTIMATED: u8 = 1;
const FIXED: u8 = 2;
const AUTOMATIC: u8 = 3;
const BACK_TO_FIXED: u8 = 4;
/// `Z3`'s averaging time when none is given, and its range (GT p. 12).
const AVERAGING_DEFAULT: u8 = 8;
const AVERAGING_LEAST: u8 = 1;
const AVERAGING_MOST: u8 = 48;

impl Parameters {
    /// Each at its default (GT pp. 5 and 11).
    const DEFAULT: Self = Self {
        almanac_validity: 1,
        pdop_threshold: 6,
        datum: 1,
        height: Centimeters(0),
        mask_angle: Degrees(5),
        signal_mask: 1,
        correction: Nanoseconds(0),
        ignored: PrnSet::EMPTY,
        smoothing: 2,
        dynamic: 2,
        observation: ESTIMATED,
        averaging: AVERAGING_DEFAULT,
    };

    /// These as a restart leaves them: those GT p. 5 does not back up
    /// at their defaults.
    const fn restarted(self) -> Self {
        Self {
            signal_mask: Self::DEFAULT.signal_mask,
            ignored: Self::DEFAULT.ignored,
            smoothing: Self::DEFAULT.smoothing,
            dynamic: Self::DEFAULT.dynamic,
            ..self
        }
    }

    /// Whether the position is held: fixed observation point mode, or
    /// back to it (GT p. 12).
    const fn holding(&self) -> bool {
        matches!(self.observation, FIXED | BACK_TO_FIXED)
    }
}

/// Time RAIM's setup, as `rrs` sets it (GT p. 15).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
struct Traim {
    /// The switch.
    on: bool,
    /// The alarm limit, in tens of nanoseconds, as sent.
    limit: u16,
    /// The 1 PPS control mode.
    pulse: PulseMode,
}

impl Traim {
    /// At power-up (GT p. 15; "Assumptions").
    const DEFAULT: Self = Self {
        on: false,
        limit: 100,
        pulse: PulseMode::Tracking,
    };
}

/// `rrs`'s alarm limit's most, three digits (GT p. 15), and how many of
/// its units make one of [`Settings::raim_alarm_limit`]'s.
const LIMIT_MOST: u16 = 999;
const LIMIT_PER_SETTING: u16 = 10;

/// The local zone time `ZDA` gives (GT p. 8): local time is UTC less it.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
struct Zone {
    /// Its hours and minutes in minutes, of the sign its hours field
    /// gives, `-00` among them: -13:59 to 13:59.
    minutes: i32,
}

impl Zone {
    /// None (GT p. 5: "+0h").
    const NONE: Self = Self { minutes: 0 };
    /// The most hours and minutes `ZDA` gives (GT p. 8).
    const HOURS_MOST: u8 = 13;
    const MINUTES_MOST: u8 = 59;

    /// The zone `ZDA`'s fields 5 and 6 give: a sign and two digits of
    /// hours, and two of minutes, the hours' sign theirs (GT p. 8).
    fn of(hours: &[u8], minutes: &[u8]) -> Option<Self> {
        let (&sign, hours) = hours.split_first()?;
        let hours = digits_in(hours, 2, 0, Self::HOURS_MOST)?;
        let minutes = digits_in(minutes, 2, 0, Self::MINUTES_MOST)?;
        Some(Self {
            minutes: sign_of(sign)? * (i32::from(hours) * MINUTES_PER_HOUR + i32::from(minutes)),
        })
    }

    /// How far local time is ahead of UTC: the zone, negated.
    const fn ahead(self) -> Seconds<i64> {
        Seconds(-(self.minutes as i64) * SECONDS_PER_MINUTE)
    }
}

const MINUTES_PER_HOUR: i32 = 60;
const SECONDS_PER_MINUTE: i64 = 60;

/// GT p. 5's defaults: the time, 2002-01-01 00:00:00, and the estimated
/// position, N34 44.0000, E135 21.0000.
const POWER_UP: GpsSeconds = match DateTime::new(2002, 1, 1, 0, 0, 0) {
    Some(date) => match GpsSeconds::from_utc(date.to_unix(), Seconds(0)) {
        Some(time) => time,
        None => panic!("GT p. 5's date is in GPS time"),
    },
    None => panic!("GT p. 5's date is a date"),
};
const POWER_UP_LATITUDE: MilliArcSeconds =
    MilliArcSeconds(34 * MAS_PER_DEGREE + 44 * MAS_PER_MINUTE);
const POWER_UP_LONGITUDE: MilliArcSeconds =
    MilliArcSeconds(135 * MAS_PER_DEGREE + 21 * MAS_PER_MINUTE);
/// GT p. 5's almanac date with no almanac, 1980-01-06 00:00:00.
const NO_ALMANAC: DateTime = DateTime {
    year: 1980,
    month: 1,
    day: 6,
    hour: 0,
    minute: 0,
    second: 0,
};

const MAS_PER_DEGREE: i32 = 3_600_000;
const MAS_PER_MINUTE: i32 = 60_000;
/// Milliarcseconds in a ten-thousandth of a minute, the angles' last
/// digit (GT p. 19).
const MAS_PER_MINUTE_DIGIT: i32 = 6;
const MINUTE_DIGITS: usize = 4;
const LATITUDE_DEGREES_MOST: i32 = 90;
const LONGITUDE_DEGREES_MOST: i32 = 180;
const MINUTES_MOST: i32 = 59;
/// The seconds a time of almanac counts in (`@@Bd`, VP p. 79).
const ALMANAC_TIME_UNIT: u32 = 4096;
/// `ZDA`'s years (GT p. 8).
const ZDA_YEAR_LEAST: u16 = 2002;
const ZDA_YEAR_MOST: u16 = 2079;
/// The elevation from which `GSV` lists a satellite (GT p. 23).
const LISTED_FROM: i8 = 5;
/// Satellites a `GSV` sentence carries, and the sentences at most.
const GSV_SATELLITES: usize = 4;
const GSV_SENTENCES: usize = 3;
/// `GSA`'s satellite fields (GT p. 22).
const GSA_SATELLITES: usize = 12;
/// `rrm`'s isolated satellites, fields 4 to 7 ("Assumptions").
const RRM_SATELLITES: usize = 4;
/// `rrm`'s sawtooth range (GT p. 40).
const SAWTOOTH_LEAST: i8 = -15;
const SAWTOOTH_MOST: i8 = 14;
/// Satellites a fix needs: 3D, 2D (as an Oncore's, [module](super)).
const SATELLITES_3D: usize = 4;
const SATELLITES_2D: usize = 3;
/// Satellites the first fix needs, and the PDOP in tenths it must be
/// under (GT p. 47, (2)-1).
const FIRST_FIX_SATELLITES: usize = 4;
const FIRST_FIX_PDOP: u16 = 60;
/// Satellites Time RAIM needs to detect a bad one, and to isolate one
/// (GT p. 47).
const RAIM_DETECTS: usize = 2;
const RAIM_ISOLATES: usize = 3;
/// The tenths of a DOP that `GGA` and `GSA` can give, 99.99 (GT
/// pp. 19 and 22).
const DOP_MOST: u16 = 999;
/// `tst`'s result bits (GT p. 31): field 4's, field 5's.
const TEST_BACKUP: u8 = 1;
const TEST_PARAMETERS: u8 = 8;
const TEST_ANTENNA: u8 = 4;
const TEST_RAM: u8 = 2;
const TEST_ROM: u8 = 1;
/// Centimeters a second in a tenth of a knot's: 1852 m an hour.
const CENTIMETERS_PER_HOUR_PER_KNOT: u32 = 185_200;
const SECONDS_PER_HOUR: u32 = 3600;
const TENTHS: u32 = 10;
/// `RMC`'s speed and course, in tenths (GT p. 25).
const SPEED_MOST: u32 = 9999;
const VARIATION_MOST: u16 = 1800;
/// The value of the parameter the GT-8031 does not have ("Assumptions").
const UNNAMED_PARAMETER: &str = "E06";

/// A sentence being built: its body, from its address to its checksum.
struct Body {
    bytes: [u8; MAX_SENTENCE],
    length: usize,
}

impl Write for Body {
    fn write_str(&mut self, text: &str) -> FmtResult {
        let end = self.length + text.len();
        let room = self
            .bytes
            .get_mut(self.length..end)
            .ok_or(core::fmt::Error)?;
        room.copy_from_slice(text.as_bytes());
        self.length = end;
        Ok(())
    }
}

impl Body {
    /// A body starting with `address`.
    fn new(address: &str) -> Self {
        let mut body = Self {
            bytes: [0; MAX_SENTENCE],
            length: 0,
        };
        body.put(format_args!("{address}"));
        body
    }

    /// Adds text, which always fits: no sentence the engine builds is
    /// longer than the most a sentence is.
    fn put(&mut self, text: Arguments<'_>) {
        let written = self.write_fmt(text);
        debug_assert!(written.is_ok(), "a sentence the engine builds fits");
    }

    /// Adds a comma and then `text`: the next field.
    fn field(&mut self, text: Arguments<'_>) {
        self.put(format_args!(","));
        self.put(text);
    }

    /// Frames it and hands it to `out`.
    fn send<O: Output + ?Sized>(&self, check: Checksum, out: &mut O) {
        let mut frame = [0; MAX_SENTENCE];
        if let Ok(length) = encode(&self.bytes[..self.length], check, &mut frame) {
            out.send(&frame[..length]);
        } else {
            debug_assert!(false, "a sentence the engine builds frames");
        }
    }
}

/// A value of tenths as the GT-8031 gives a height in `width` characters
/// (GT pp. 11 and 19): a negative one a `-`, the whole part to `width`
/// less three digits, a point and the tenth; a positive one its whole
/// part to a digit more, no sign.  So a positive value is `%+0W.1f`'s,
/// its `+` made a `0`, while its whole part fits that, and `GGA`'s
/// geoid separation reaches GT p. 19's 9999.9 in six characters.
struct SignedTenths {
    tenths: i32,
    width: usize,
}

impl core::fmt::Display for SignedTenths {
    fn fmt(&self, formatter: &mut core::fmt::Formatter<'_>) -> FmtResult {
        let magnitude = self.tenths.unsigned_abs();
        let whole = magnitude / TENTHS;
        let tenth = magnitude % TENTHS;
        let digits = self.width - 2;
        if self.tenths < 0 {
            let digits = digits - 1;
            write!(formatter, "-{whole:0digits$}.{tenth}")
        } else {
            write!(formatter, "{whole:0digits$}.{tenth}")
        }
    }
}

/// Centimeters to tenths of a meter, to the nearest, within `least` and
/// `most` tenths.
fn tenths_of_meter(height: Centimeters, least: i32, most: i32) -> i32 {
    let tenths = (height.0 + if height.0 < 0 { -5 } else { 5 }) / 10;
    tenths.clamp(least, most)
}

/// A DOP as `GGA` and `GSA` give one, `dd.dd` (GT pp. 19 and 22).
struct Dop(Tenths);

impl core::fmt::Display for Dop {
    fn fmt(&self, formatter: &mut core::fmt::Formatter<'_>) -> FmtResult {
        let tenths = self.0.0.min(DOP_MOST);
        write!(formatter, "{:02}.{}0", tenths / 10, tenths % 10)
    }
}

/// A latitude or longitude as the GT-8031 gives one, `ddmm.mmmm,N` or
/// `dddmm.mmmm,E` (GT p. 19).
struct Angle {
    angle: MilliArcSeconds,
    longitude: bool,
}

impl core::fmt::Display for Angle {
    fn fmt(&self, formatter: &mut core::fmt::Formatter<'_>) -> FmtResult {
        let magnitude = self.angle.0.unsigned_abs();
        // The ten-thousandths of a minute, to the nearest.
        let units = (magnitude + MAS_PER_MINUTE_DIGIT.unsigned_abs() / 2)
            / MAS_PER_MINUTE_DIGIT.unsigned_abs();
        let per_degree = (MAS_PER_DEGREE / MAS_PER_MINUTE_DIGIT).unsigned_abs();
        let per_minute = (MAS_PER_MINUTE / MAS_PER_MINUTE_DIGIT).unsigned_abs();
        let degrees = units / per_degree;
        let minutes = units % per_degree / per_minute;
        let fraction = units % per_minute;
        let (digits, hemisphere) = match (self.longitude, self.angle.0 < 0) {
            (false, false) => (2, 'N'),
            (false, true) => (2, 'S'),
            (true, false) => (3, 'E'),
            (true, true) => (3, 'W'),
        };
        write!(
            formatter,
            "{degrees:0digits$}{minutes:02}.{fraction:0MINUTE_DIGITS$},{hemisphere}"
        )
    }
}

/// A date and time as `YYMMDDhhmmss` (GT pp. 27 and 36).
struct Stamp(DateTime);

impl core::fmt::Display for Stamp {
    fn fmt(&self, formatter: &mut core::fmt::Formatter<'_>) -> FmtResult {
        let date = self.0;
        write!(
            formatter,
            "{:02}{:02}{:02}{:02}{:02}{:02}",
            date.year % 100,
            date.month,
            date.day,
            date.hour,
            date.minute,
            date.second
        )
    }
}

/// `tps`'s field for no date (GT p. 36).
const NO_STAMP: &str = "000000000000";

/// Where an [`Engine`]'s position comes from, for `GGA` and `RMC`.
struct Reported {
    latitude: MilliArcSeconds,
    longitude: MilliArcSeconds,
    mean_sea_level: Centimeters,
    /// The ellipsoid's height above mean sea level, the geoid's
    /// separation (GT p. 19's "Geoide Altitude"), zero when unknown.
    separation: Centimeters,
}

/// A model that stands in for a GT-74.  See the [module](self).
#[derive(Debug)]
pub struct Engine {
    /// Finds the sentences in what it is sent.
    deframer: Deframer,
    /// The rest of it.
    state: State,
}

impl Engine {
    /// The GT-74 `identity` names, just powered up with no backup.
    #[must_use]
    pub const fn new(identity: &'static Identity) -> Self {
        Self {
            deframer: Deframer::new(),
            state: State::new(identity),
        }
    }

    /// Which GT-74 it is.
    #[must_use]
    pub const fn identity(&self) -> &'static Identity {
        self.state.identity
    }

    /// How long after a pulse to call [`Engine::second`], with the record
    /// of the second the next pulse marks: [`SENTENCES_AFTER_PULSE`].
    #[must_use]
    pub const fn sentences_after_pulse(&self) -> Milliseconds<u16> {
        SENTENCES_AFTER_PULSE
    }

    /// What commands have set it to.  The settings none of its sentences
    /// sets stay at fixed values: the 1 PPS on UTC, which it reports
    /// once it knows it (GT p. 36), satellites chosen by their geometry,
    /// the port speaking NMEA, and the rest as an Oncore has them at
    /// power-up.
    #[must_use]
    pub fn settings(&self) -> Settings {
        self.state.settings()
    }

    /// Whether its 1 PPS goes out with `record`: the availability flag
    /// its `tps` reports (GT p. 36), by the 1 PPS control mode as GT
    /// p. 47 has it ("Assumptions"), for whoever makes the pulse to
    /// gate it by, as [`Engine::pulsing`](crate::engine::Engine::pulsing)
    /// is for an Oncore's.
    #[must_use]
    pub fn pulsing(&self, record: &Record) -> bool {
        self.state.pulse(record)
    }

    /// The sentences of a second are due, [`SENTENCES_AFTER_PULSE`]
    /// after the pulse before it, `record` being what is known of the
    /// second the next pulse marks: sends them.
    pub fn second<O: Output + ?Sized>(&mut self, record: &Record, out: &mut O) {
        self.state.second(record, out);
    }

    /// Takes bytes from the receiver and answers each whole sentence
    /// among them.  `record` is the current second's.  Returns what
    /// those sentences did that the caller may have to act on, which it
    /// is free to drop.
    pub fn receive<O: Output + ?Sized>(
        &mut self,
        bytes: &[u8],
        record: &Record,
        out: &mut O,
    ) -> Events {
        let state = &mut self.state;
        if state.count == 0
            && let Some(time) = record.time
        {
            state.clock = time.seconds;
        }
        let mut events = Events::NONE;
        for &byte in bytes {
            if let Some(Ok(sentence)) = self.deframer.push(byte) {
                let before = state.settings();
                let reset = state.take(&sentence, record, out);
                events = events.and(Events::of(reset, before.changed(&state.settings())));
            }
        }
        events
    }
}

/// What a GT-74 holds but the sentence it is reading.
#[derive(Debug)]
struct State {
    /// Which GT-74 it is.
    identity: &'static Identity,
    /// The seconds it has been given since power-up.
    count: u32,
    /// The time it reports.
    clock: GpsSeconds,
    /// The last record's time, to tell a new one from the same again.
    heard: Option<GpsSeconds>,
    /// Each sentence's interval in seconds, by [`Periodic::index`], 0
    /// for none.
    intervals: [u8; PERIODIC.len()],
    /// `set`'s parameters.
    parameters: Parameters,
    /// Time RAIM's setup.
    traim: Traim,
    /// The local zone time.
    zone: Zone,
    /// The latitude and longitude entered.
    latitude: MilliArcSeconds,
    longitude: MilliArcSeconds,
    /// The leap second last announced, past or pending.
    leap: Option<LeapEvent>,
    /// Whether it has made its first fix since power-up, which the 1 PPS
    /// waits for in estimated observation point mode (GT p. 47).
    fixed: bool,
    /// The time of the last fix, which `RMC` goes on giving (GT p. 25).
    last_fix: Option<GpsSeconds>,
}

impl State {
    /// A GT-74 just powered up with no backup.
    const fn new(identity: &'static Identity) -> Self {
        let mut intervals = [0; PERIODIC.len()];
        let mut index = 0;
        while index < PERIODIC.len() {
            intervals[index] = PERIODIC[index].power_up();
            index += 1;
        }
        Self {
            identity,
            count: 0,
            clock: POWER_UP,
            heard: None,
            intervals,
            parameters: Parameters::DEFAULT,
            traim: Traim::DEFAULT,
            zone: Zone::NONE,
            latitude: POWER_UP_LATITUDE,
            longitude: POWER_UP_LONGITUDE,
            leap: None,
            fixed: false,
            last_fix: None,
        }
    }

    /// What commands have set it to ([`Engine::settings`]).
    fn settings(&self) -> Settings {
        let store = Store::DEFAULT;
        let entered = Location {
            latitude: self.latitude,
            longitude: self.longitude,
            height: Height {
                height: self.parameters.height,
                kind: HeightKind::MeanSeaLevel,
            },
        };
        // Where the pulse goes: `t`, less how early the software puts it.
        let correction = self.parameters.correction.0 - i32::from(self.identity.pulse_early.0);
        Settings {
            mask_angle: self.parameters.mask_angle,
            position_hold: self.parameters.holding(),
            held_position: entered,
            altitude_hold: store.altitude_hold(),
            altitude_hold_height: store.altitude_hold_height(),
            time_base: TimeBase::Utc,
            cable_delay: Nanoseconds(if correction < 0 {
                correction.unsigned_abs()
            } else {
                0
            }),
            pulse_offset: Nanoseconds(correction.max(0).unsigned_abs()),
            pulse_mode: self.traim.pulse,
            raim_algorithm: self.traim.on,
            raim_alarm_limit: HundredNanoseconds(self.traim.limit / LIMIT_PER_SETTING),
            application: store.application(),
            initial_position: entered,
            satellite_select: super::settings::SatelliteSelect::BestGeometry,
            manual_satellites: [0; super::settings::CHANNELS_MOST],
            dop_kind: store.dop_kind(),
            xdop_hysteresis: store.xdop_hysteresis(),
            ignore_list: self.parameters.ignored,
            almanac_update: store.almanac_update(),
            atmospheric_correction: store.atmospheric_correction(),
            fix_algorithm: store.fix_algorithm(),
            epoch_offset: store.epoch_offset(),
            hdop_threshold: store.hdop_threshold(),
            correction_thresholds: store.correction_thresholds(),
            output_format: super::settings::OutputFormat::Nmea,
        }
    }

    /// A second starts ([`Engine::second`]).
    fn second<O: Output + ?Sized>(&mut self, record: &Record, out: &mut O) {
        self.count = self.count.wrapping_add(1);
        let told = record.time.map(|time| time.seconds);
        self.clock = match told {
            Some(time) if told != self.heard => time,
            _ if self.count == 1 => self.clock,
            _ => self.clock + Seconds(1),
        };
        self.heard = told;
        if record.leap.is_some() {
            self.leap = record.leap;
        }
        if matches!(self.fix(record), Fix::TwoD | Fix::ThreeD) {
            self.last_fix = Some(self.clock);
        }
        if self.first_fix(record) {
            self.fixed = true;
        }
        for periodic in PERIODIC {
            let interval = self.intervals[periodic.index()];
            if interval != 0 && self.count.is_multiple_of(u32::from(interval)) {
                self.send(periodic, record, out);
            }
        }
    }

    /// Does what one sentence asks; whether it restarted the engine.
    fn take<O: Output + ?Sized>(
        &mut self,
        sentence: &Sentence<'_>,
        record: &Record,
        out: &mut O,
    ) -> bool {
        let Some(address) = sentence.address() else {
            return false;
        };
        let raim = self.identity.time_raim;
        match (address.class, address.name) {
            (Class::Proprietary, b"set") => sentence.data().for_each(|field| self.set(field)),
            (Class::Proprietary, b"srq") => self.ssd(out),
            (Class::Proprietary, b"int") => {
                for field in sentence.data() {
                    self.interval(field, record, out);
                }
            }
            (Class::Proprietary, b"rrs") if raim => self.rrs(sentence),
            (Class::Proprietary, b"rrq") if raim => self.rsd(out),
            (Class::Proprietary, b"clr") => return self.clear(sentence, record),
            (Class::Approved, b"ZDA") => self.zda(sentence, record),
            (Class::Approved, b"GLL") => self.gll(sentence),
            _ => {}
        }
        false
    }

    /// One parameter of `set` (GT p. 11).
    fn set(&mut self, field: &[u8]) {
        let Some((&letter, value)) = field.split_first() else {
            return;
        };
        let parameters = &mut self.parameters;
        match letter {
            b'A' => {
                if let Some(validity) = digits_in(value, 1, 1, 2) {
                    parameters.almanac_validity = validity;
                }
            }
            b'D' => {
                if let Some(threshold) = digits_in(value, 2, 0, 10) {
                    parameters.pdop_threshold = threshold;
                }
            }
            b'G' => {
                if let Some(datum) = number(value, 3).filter(|datum| (1..=254).contains(datum)) {
                    parameters.datum = u8::try_from(datum).unwrap_or(1);
                }
            }
            b'H' => {
                if let Some(height) =
                    signed_tenths(value).filter(|tenths| (-9999..=400_000).contains(tenths))
                {
                    parameters.height = Centimeters(height * 10);
                }
            }
            b'M' => {
                if let Some(mask) = digits_in(value, 2, 5, 90) {
                    parameters.mask_angle = Degrees(mask);
                }
            }
            b'S' => {
                if let Some(mask) = digits_in(value, 2, 1, 99) {
                    parameters.signal_mask = mask;
                }
            }
            b'T' => {
                if let Some(tenths) = signed(value, 4) {
                    parameters.correction = Nanoseconds(tenths * 100);
                }
            }
            b't' => {
                if let Some(nanoseconds) = signed(value, 6) {
                    parameters.correction = Nanoseconds(nanoseconds);
                }
            }
            b'U' => {
                if let Some(bits) = hexadecimal(value) {
                    parameters.ignored = PrnSet::from_bits(bits);
                }
            }
            b'W' => {
                if let Some(index) = digits_in(value, 1, 1, 3) {
                    parameters.smoothing = index;
                }
            }
            b'X' => {
                if let Some(index) = digits_in(value, 1, 1, 3) {
                    parameters.dynamic = index;
                }
            }
            b'Z' => self.observation(value),
            _ => {}
        }
    }

    /// `set`'s `Z`: a mode, and for 3 its averaging time (GT p. 12).
    fn observation(&mut self, value: &[u8]) {
        let (mode, hours) = value.split_at(value.len().min(1));
        let Some(mode) = digits_in(mode, 1, ESTIMATED, BACK_TO_FIXED) else {
            return;
        };
        let averaging = match (mode, hours) {
            (_, []) => AVERAGING_DEFAULT,
            (AUTOMATIC, hours) => match digits_in(hours, 2, AVERAGING_LEAST, AVERAGING_MOST) {
                Some(hours) => hours,
                None => return,
            },
            _ => return,
        };
        self.parameters.observation = mode;
        if mode == AUTOMATIC {
            self.parameters.averaging = averaging;
        }
    }

    /// One `int` parameter, a sentence's name and its interval (GT
    /// p. 13); interval 0 sends it now and then no more.
    fn interval<O: Output + ?Sized>(&mut self, field: &[u8], record: &Record, out: &mut O) {
        if field.len() != NAME_LENGTH + INTERVAL_DIGITS {
            return;
        }
        let (name, interval) = field.split_at(NAME_LENGTH);
        let Some(periodic) = PERIODIC
            .into_iter()
            .find(|periodic| periodic.name() == name)
        else {
            return;
        };
        if periodic == Periodic::Rrm && !self.identity.time_raim {
            return;
        }
        let Some(interval) = digits_in(interval, INTERVAL_DIGITS, 0, INTERVAL_MOST) else {
            return;
        };
        self.intervals[periodic.index()] = interval;
        if interval == 0 {
            self.send(periodic, record, out);
        }
    }

    /// `rrs`: the switch, the alarm limit and the control mode (GT
    /// p. 15).
    fn rrs(&mut self, sentence: &Sentence<'_>) {
        let mut fields = sentence.data();
        if let Some(on) = fields.next().and_then(|field| digits_in(field, 1, 0, 1)) {
            self.traim.on = on == 1;
        }
        if let Some(limit) = fields
            .next()
            .and_then(|field| number(field, 3))
            .filter(|limit| *limit <= u32::from(LIMIT_MOST))
        {
            self.traim.limit = u16::try_from(limit).unwrap_or(LIMIT_MOST);
        }
        if let Some(mode) = fields.next().and_then(|field| digits_in(field, 1, 0, 3)) {
            self.traim.pulse = match mode {
                0 => PulseMode::Off,
                1 => PulseMode::Always,
                2 => PulseMode::Tracking,
                _ => PulseMode::Confirmed,
            };
        }
    }

    /// `clr`: a restart (GT p. 10).  Whether it was one.
    fn clear(&mut self, sentence: &Sentence<'_>, record: &Record) -> bool {
        const DEFAULTS: u8 = 1;
        const ALMANAC_DELETED: u8 = 3;
        let Some(mode) = sentence
            .data()
            .next()
            .and_then(|field| digits_in(field, 1, DEFAULTS, ALMANAC_DELETED))
        else {
            return false;
        };
        self.parameters = self.parameters.restarted();
        self.traim = Traim {
            limit: self.traim.limit,
            ..Traim::DEFAULT
        };
        if mode == DEFAULTS {
            self.parameters = Parameters::DEFAULT;
            self.latitude = POWER_UP_LATITUDE;
            self.longitude = POWER_UP_LONGITUDE;
            if record.time.is_none() {
                self.clock = POWER_UP;
            }
        }
        if mode == DEFAULTS || mode == ALMANAC_DELETED {
            self.leap = None;
        }
        self.fixed = false;
        true
    }

    /// `ZDA`: the time, the date and the local zone time (GT p. 8).
    fn zda(&mut self, sentence: &Sentence<'_>, record: &Record) {
        let mut fields = sentence.data();
        let (Some(time), Some(day), Some(month), Some(year), Some(zone_hours), Some(zone_minutes)) = (
            fields.next(),
            fields.next(),
            fields.next(),
            fields.next(),
            fields.next(),
            fields.next(),
        ) else {
            return;
        };
        let (Some(hour), Some(minute), Some(second)) = (
            time.get(0..2).and_then(|text| digits_in(text, 2, 0, 23)),
            time.get(2..4).and_then(|text| digits_in(text, 2, 0, 59)),
            time.get(4..).and_then(|text| digits_in(text, 2, 0, 59)),
        ) else {
            return;
        };
        let year = number(year, 4)
            .and_then(|year| u16::try_from(year).ok())
            .filter(|year| (ZDA_YEAR_LEAST..=ZDA_YEAR_MOST).contains(year));
        let (Some(year), Some(day), Some(month)) =
            (year, digits_in(day, 2, 1, 31), digits_in(month, 2, 1, 12))
        else {
            return;
        };
        let Some(date) = DateTime::new(year, month, day, hour, minute, second) else {
            return;
        };
        let Some(zone) = Zone::of(zone_hours, zone_minutes) else {
            return;
        };
        self.zone = zone;
        if record.time.is_none() {
            let offset = record.utc_offset.unwrap_or(Seconds(0));
            if let Some(time) = GpsSeconds::from_utc(date.to_unix(), offset) {
                self.clock = time;
            }
        }
    }

    /// `GLL`: the position entered (GT p. 6).
    fn gll(&mut self, sentence: &Sentence<'_>) {
        let mut fields = sentence.data();
        let (Some(latitude), Some(north), Some(longitude), Some(east)) =
            (fields.next(), fields.next(), fields.next(), fields.next())
        else {
            return;
        };
        let latitude = angle(latitude, 2, LATITUDE_DEGREES_MOST, north, b'N', b'S');
        let longitude = angle(longitude, 3, LONGITUDE_DEGREES_MOST, east, b'E', b'W');
        if let (Some(latitude), Some(longitude)) = (latitude, longitude) {
            self.latitude = latitude;
            self.longitude = longitude;
        }
    }

    /// Sends one sentence, if it is one the engine sends.
    fn send<O: Output + ?Sized>(&self, periodic: Periodic, record: &Record, out: &mut O) {
        match periodic {
            Periodic::Gga => self.gga(record, out),
            Periodic::Gsa => self.gsa(record, out),
            Periodic::Gsv => self.gsv(record, out),
            Periodic::Rmc => self.rmc(record, out),
            Periodic::Anc => self.anc(record, out),
            Periodic::Acc => Self::acc(record, out),
            Periodic::Tst => self.tst(record, out),
            Periodic::Tps => self.tps(record, out),
            Periodic::Rrm if self.identity.time_raim => self.rrm(record, out),
            Periodic::Zda | Periodic::Vtg | Periodic::Rrm => {}
        }
    }

    /// GPS less UTC as the engine reads its clock: the record's, or none.
    fn offset(record: &Record) -> Seconds<i8> {
        record.utc_offset.unwrap_or(Seconds(0))
    }

    /// The clock's UTC date and time, a leap second's 60 among them.
    fn utc(&self, record: &Record) -> DateTime {
        DateTime::of(self.clock, Self::offset(record), record.leap).unwrap_or(NO_ALMANAC)
    }

    /// Whether no satellite is heard: the antenna is off.
    fn deaf(record: &Record) -> bool {
        matches!(record.antenna, Antenna::Open | Antenna::Shorted)
    }

    /// The satellites the fix uses: the record's, above the mask and not
    /// ignored (GT p. 11), in the order of their IDs.
    fn used<'a>(&'a self, record: &'a Record) -> impl Iterator<Item = &'a Satellite> + 'a {
        let mask = i8::try_from(self.parameters.mask_angle.0).unwrap_or(i8::MAX);
        sorted(record).filter(move |satellite| {
            !Self::deaf(record)
                && satellite.used
                && reaches_mask(satellite.elevation.0, mask)
                && !self.parameters.ignored.contains(satellite.prn)
        })
    }

    /// Whether `tps`, the fix, the 1 PPS and Time RAIM go by the
    /// position held: fixed observation point mode, as `set,Z` set it.
    /// [`Self::reported`] goes by the command alone.
    fn holds(&self) -> bool {
        self.parameters.holding()
    }

    /// The fix it reports: none in fixed observation point mode, and only
    /// with the satellites a fix needs, 2D above `D`'s PDOP (GT p. 11).
    fn fix(&self, record: &Record) -> Fix {
        if self.holds() {
            return Fix::None;
        }
        let used = self.used(record).count();
        let pdop = record.dops.map_or(0, |dops| dops.position.0);
        let three_d = self.parameters.pdop_threshold != 0
            && pdop <= u16::from(self.parameters.pdop_threshold) * 10;
        match record.fix {
            Fix::ThreeD | Fix::TimeOnly if used >= SATELLITES_3D && three_d => Fix::ThreeD,
            Fix::ThreeD | Fix::TimeOnly | Fix::TwoD if used >= SATELLITES_2D => Fix::TwoD,
            _ => Fix::None,
        }
    }

    /// Whether the record's fix is a first fix: four satellites or more
    /// usable for it, and their PDOP under 6 (GT p. 47, (2)-1).
    fn first_fix(&self, record: &Record) -> bool {
        matches!(self.fix(record), Fix::TwoD | Fix::ThreeD)
            && self.used(record).count() >= FIRST_FIX_SATELLITES
            && record
                .dops
                .is_some_and(|dops| dops.position.0 < FIRST_FIX_PDOP)
    }

    /// The position `GGA` and `RMC` report: the one entered in fixed
    /// observation point mode or while the record has none, else the
    /// record's.
    fn reported(&self, record: &Record) -> Reported {
        let separation = record.position.map_or(Centimeters(0), |position| {
            Centimeters(position.ellipsoid.0 - position.mean_sea_level.0)
        });
        match record.position {
            Some(Position {
                latitude,
                longitude,
                mean_sea_level,
                ..
            }) if !self.parameters.holding() => Reported {
                latitude,
                longitude,
                mean_sea_level,
                separation,
            },
            _ => Reported {
                latitude: self.latitude,
                longitude: self.longitude,
                mean_sea_level: self.parameters.height,
                separation,
            },
        }
    }

    /// `GGA` (GT p. 19).
    fn gga<O: Output + ?Sized>(&self, record: &Record, out: &mut O) {
        const ALTITUDE_LEAST: i32 = -9999;
        const ALTITUDE_MOST: i32 = 40_000;
        // The geoid separation's range in six characters (GT p. 19).
        const SEPARATION_LEAST: i32 = -9999;
        const SEPARATION_MOST: i32 = 99_999;
        let utc = self.utc(record);
        let place = self.reported(record);
        let fix = self.fix(record);
        let dop = match (fix, record.dops) {
            (Fix::ThreeD, Some(dops)) => dops.position,
            (Fix::TwoD, Some(dops)) => dops.horizontal,
            _ => Tenths(0),
        };
        let quality = u8::from(matches!(fix, Fix::TwoD | Fix::ThreeD));
        let mut body = Body::new("GPGGA");
        body.field(format_args!(
            "{:02}{:02}{:02}",
            utc.hour, utc.minute, utc.second
        ));
        body.field(format_args!(
            "{}",
            Angle {
                angle: place.latitude,
                longitude: false
            }
        ));
        body.field(format_args!(
            "{}",
            Angle {
                angle: place.longitude,
                longitude: true
            }
        ));
        body.field(format_args!("{quality}"));
        body.field(format_args!("{:02}", self.used(record).count()));
        body.field(format_args!("{}", Dop(dop)));
        body.field(format_args!(
            "{},M",
            SignedTenths {
                tenths: tenths_of_meter(place.mean_sea_level, ALTITUDE_LEAST, ALTITUDE_MOST),
                width: 8
            }
        ));
        body.field(format_args!(
            "{},M",
            SignedTenths {
                tenths: tenths_of_meter(place.separation, SEPARATION_LEAST, SEPARATION_MOST),
                width: 6
            }
        ));
        body.field(format_args!(","));
        body.send(Checksum::Omitted, out);
    }

    /// `GSA` (GT p. 22).
    fn gsa<O: Output + ?Sized>(&self, record: &Record, out: &mut O) {
        const MODE_NONE: u8 = 1;
        const MODE_2D: u8 = 2;
        const MODE_3D: u8 = 3;
        let fix = self.fix(record);
        let (mode, pdop, hdop, vdop) = match (fix, record.dops) {
            (Fix::ThreeD, Some(dops)) => (MODE_3D, dops.position, dops.horizontal, dops.vertical),
            (Fix::ThreeD, None) => (MODE_3D, Tenths(0), Tenths(0), Tenths(0)),
            (Fix::TwoD, dops) => (
                MODE_2D,
                Tenths(0),
                dops.map_or(Tenths(0), |dops| dops.horizontal),
                Tenths(0),
            ),
            _ => (MODE_NONE, Tenths(0), Tenths(0), Tenths(0)),
        };
        let operation = if self.parameters.pdop_threshold == 0 {
            'M'
        } else {
            'A'
        };
        let mut body = Body::new("GPGSA");
        body.field(format_args!("{operation}"));
        body.field(format_args!("{mode}"));
        let mut used = self.used(record);
        for _ in 0..GSA_SATELLITES {
            match used.next() {
                Some(satellite) => body.field(format_args!("{:02}", satellite.prn.get())),
                None => body.field(format_args!("")),
            }
        }
        body.field(format_args!("{}", Dop(pdop)));
        body.field(format_args!("{}", Dop(hdop)));
        body.field(format_args!("{}", Dop(vdop)));
        body.send(Checksum::Omitted, out);
    }

    /// `GSV` (GT p. 23), one sentence for each four satellites listed.
    fn gsv<O: Output + ?Sized>(&self, record: &Record, out: &mut O) {
        const SNR_MOST: u8 = 99;
        let listed = || {
            sorted(record)
                .filter(|satellite| reaches_mask(satellite.elevation.0, LISTED_FROM))
                .take(GSV_SATELLITES * GSV_SENTENCES)
        };
        let count = listed().count();
        let sentences = count.div_ceil(GSV_SATELLITES).max(1);
        for sentence in 0..sentences {
            let mut body = Body::new("GPGSV");
            body.field(format_args!("{sentences}"));
            body.field(format_args!("{}", sentence + 1));
            body.field(format_args!("{count:02}"));
            for satellite in listed()
                .skip(sentence * GSV_SATELLITES)
                .take(GSV_SATELLITES)
            {
                let snr = match satellite.signal {
                    Some(signal) if !Self::deaf(record) => signal.0.min(SNR_MOST),
                    _ => 0,
                };
                body.field(format_args!("{:02}", satellite.prn.get()));
                body.field(format_args!("{:02}", satellite.elevation.0));
                body.field(format_args!("{:03}", satellite.azimuth.0));
                body.field(format_args!("{snr:02}"));
            }
            body.send(Checksum::Omitted, out);
        }
    }

    /// `RMC` (GT p. 25), the one sentence the engine sends with a
    /// checksum.
    fn rmc<O: Output + ?Sized>(&self, record: &Record, out: &mut O) {
        let fixed = matches!(self.fix(record), Fix::TwoD | Fix::ThreeD);
        let place = self.reported(record);
        let at = self
            .last_fix
            .and_then(|time| DateTime::of(time, Self::offset(record), record.leap));
        let mut body = Body::new("GPRMC");
        match at {
            Some(at) => body.field(format_args!(
                "{:02}{:02}{:02}",
                at.hour, at.minute, at.second
            )),
            None => body.field(format_args!("")),
        }
        body.field(format_args!("{}", if fixed { 'A' } else { 'V' }));
        body.field(format_args!(
            "{}",
            Angle {
                angle: place.latitude,
                longitude: false
            }
        ));
        body.field(format_args!(
            "{}",
            Angle {
                angle: place.longitude,
                longitude: true
            }
        ));
        match record.velocity.filter(|_| fixed) {
            Some(velocity) => {
                let knots = (u32::from(velocity.speed.0) * SECONDS_PER_HOUR * TENTHS
                    + CENTIMETERS_PER_HOUR_PER_KNOT / 2)
                    / CENTIMETERS_PER_HOUR_PER_KNOT;
                let knots = knots.min(SPEED_MOST);
                let course = velocity.heading.0;
                body.field(format_args!("{:03}.{}", knots / TENTHS, knots % TENTHS));
                body.field(format_args!("{:03}.{}", course / 10, course % 10));
            }
            None => {
                body.field(format_args!(""));
                body.field(format_args!(""));
            }
        }
        match at {
            Some(at) => body.field(format_args!(
                "{:02}{:02}{:02}",
                at.day,
                at.month,
                at.year % 100
            )),
            None => body.field(format_args!("")),
        }
        match record.derived.magnetic_variation {
            Some(variation) => {
                let magnitude = variation.0.unsigned_abs().min(VARIATION_MOST);
                let side = if variation.0 < 0 { 'W' } else { 'E' };
                body.field(format_args!(
                    "{:03}.{},{side}",
                    magnitude / 10,
                    magnitude % 10
                ));
            }
            None => body.field(format_args!(",")),
        }
        body.send(Checksum::Appended, out);
    }

    /// `anc` (GT p. 27): the almanac's date and each satellite's health.
    fn anc<O: Output + ?Sized>(&self, record: &Record, out: &mut O) {
        const NOT_HELD: char = '0';
        const UNHEALTHY: char = '1';
        const HEALTHY: char = '2';
        let date = record
            .almanac
            .and_then(|almanac| {
                let time = GpsSeconds::from_week(
                    almanac.week,
                    TimeOfWeek::new(u32::from(almanac.time_of_almanac) * ALMANAC_TIME_UNIT)?,
                );
                DateTime::local(time, Self::offset(record), None, self.zone.ahead())
            })
            .unwrap_or(NO_ALMANAC);
        let mut body = Body::new("PFEC,GPanc");
        body.field(format_args!("{}", Stamp(date)));
        body.put(format_args!(","));
        for id in 1..=PRNS {
            let health = match (record.almanac, Prn::new(id)) {
                (Some(almanac), Some(prn)) if almanac.held.contains(prn) => {
                    if almanac.healthy.contains(prn) {
                        HEALTHY
                    } else {
                        UNHEALTHY
                    }
                }
                _ => NOT_HELD,
            };
            body.put(format_args!("{health}"));
        }
        body.send(Checksum::Omitted, out);
    }

    /// `acc` (GT p. 28): each satellite's URA index, or `X`
    /// ("Assumptions").
    fn acc<O: Output + ?Sized>(record: &Record, out: &mut O) {
        const NOT_AVAILABLE: char = 'X';
        let mut body = Body::new("PFEC,GPacc");
        body.put(format_args!(","));
        for id in 1..=PRNS {
            let ura = Prn::new(id)
                .and_then(|prn| record.satellites.get(prn))
                .and_then(|satellite| satellite.ura);
            match ura {
                Some(ura) => body.put(format_args!("{:X}", ura.get())),
                None => body.put(format_args!("{NOT_AVAILABLE}")),
            }
        }
        body.send(Checksum::Omitted, out);
    }

    /// `tst` (GT p. 31): the self-test's result and the software.
    fn tst<O: Output + ?Sized>(&self, record: &Record, out: &mut O) {
        const COMPLETED: u8 = 0;
        let faults = record.self_test.hardware;
        let backup = if faults.contains(Fault::RealTimeClock) {
            TEST_BACKUP
        } else {
            0
        };
        let antenna = matches!(record.antenna, Antenna::Open | Antenna::Shorted);
        let result = [
            (faults.contains(Fault::Eeprom), TEST_PARAMETERS),
            (antenna, TEST_ANTENNA),
            (faults.contains(Fault::Ram), TEST_RAM),
            (faults.contains(Fault::Rom), TEST_ROM),
        ]
        .into_iter()
        .filter(|(failed, _)| *failed)
        .fold(0, |bits, (_, bit)| bits | bit);
        let program_digits = PROGRAM_DIGITS as usize;
        let version_digits = VERSION_DIGITS as usize;
        let mut body = Body::new("PFEC,GPtst");
        body.field(format_args!("{COMPLETED}"));
        body.field(format_args!(
            "{:0program_digits$}{:0version_digits$}",
            self.identity.program, self.identity.version
        ));
        body.field(format_args!("{backup}"));
        body.field(format_args!("{result:X}"));
        body.send(Checksum::Omitted, out);
    }

    /// Whether the 1 PPS goes out after this second's `tps`
    /// ("Assumptions").
    fn pulse(&self, record: &Record) -> bool {
        let used = self.used(record).count();
        let holding = self.holds();
        let ready =
            record.time.is_some() && record.utc_offset.is_some() && record.almanac.is_some();
        match self.traim.pulse {
            PulseMode::Off => false,
            PulseMode::Always => true,
            PulseMode::Tracking => ready && used >= 1 && (holding || self.fixed),
            PulseMode::Confirmed => {
                let least = if holding { RAIM_DETECTS } else { 1 };
                ready
                    && used >= least
                    && (holding || self.fixed)
                    && self.raim(record).0 != RaimSolution::Alarm
            }
        }
    }

    /// `tps` (GT pp. 36 to 38): the time, its standard, the pulse, the
    /// mode and the leap second.
    fn tps<O: Output + ?Sized>(&self, record: &Record, out: &mut O) {
        const RTC: u8 = 1;
        const GPS: u8 = 2;
        const UTC: u8 = 3;
        let (standard, present) = match (record.time, record.utc_offset) {
            (None, _) => (RTC, self.utc(record)),
            (Some(_), None) => (
                GPS,
                DateTime::of(self.clock, Seconds(0), None).unwrap_or(NO_ALMANAC),
            ),
            (Some(_), Some(_)) => (UTC, self.utc(record)),
        };
        let mode = if self.holds() { FIXED } else { ESTIMATED };
        let mut body = Body::new("PFEC,GPtps");
        body.field(format_args!("{}", Stamp(present)));
        body.field(format_args!("{standard}"));
        body.field(format_args!("{}", u8::from(self.pulse(record))));
        body.field(format_args!("{mode}"));
        match (record.utc_offset, self.leap) {
            (Some(offset), Some(leap)) => {
                let after = if leap.pending(self.clock) {
                    Seconds(offset.0.saturating_add(leap.step().0))
                } else {
                    offset
                };
                let midnight = match leap.change {
                    LeapChange::Insert => leap.at + Seconds(1),
                    LeapChange::Remove => leap.at,
                };
                match DateTime::of(midnight, after, None) {
                    Some(date) => body.field(format_args!("{}", Stamp(date))),
                    None => body.field(format_args!("{NO_STAMP}")),
                }
                body.field(format_args!("{:+}", leap.step().0));
            }
            _ => {
                body.field(format_args!("{NO_STAMP}"));
                body.field(format_args!("00"));
            }
        }
        let offset = record.utc_offset.map_or(0, |offset| offset.0.clamp(0, 99));
        body.field(format_args!("{offset:02}"));
        let stamp = record
            .almanac
            .filter(|_| record.utc_offset.is_some())
            .and_then(|almanac| {
                let time = GpsSeconds::from_week(
                    almanac.week,
                    TimeOfWeek::new(u32::from(almanac.time_of_almanac) * ALMANAC_TIME_UNIT)?,
                );
                DateTime::of(time, Self::offset(record), None)
            });
        match stamp {
            Some(stamp) => body.field(format_args!("{}", Stamp(stamp))),
            None => body.field(format_args!("{NO_STAMP}")),
        }
        let week = self.clock.week().map_or(0, |week| week.get());
        body.field(format_args!("{week:04}"));
        body.field(format_args!("{:06}", self.clock.time_of_week().get()));
        body.send(Checksum::Omitted, out);
    }

    /// What Time RAIM makes of the second: its result, what it can do,
    /// and the satellites it isolated ("Assumptions").
    fn raim(&self, record: &Record) -> (RaimSolution, RaimAbility, PrnSet) {
        const NONE: (RaimSolution, RaimAbility, PrnSet) =
            (RaimSolution::Unknown, RaimAbility::Neither, PrnSet::EMPTY);
        let used = self.used(record).count();
        let can = match used {
            RAIM_ISOLATES.. => RaimAbility::Isolation,
            RAIM_DETECTS => RaimAbility::Detection,
            _ => RaimAbility::Neither,
        };
        let Some(result) = record.derived.raim else {
            return NONE;
        };
        if !self.traim.on || !self.holds() {
            return NONE;
        }
        let ability = match (can, result.ability) {
            (RaimAbility::Neither, _) | (_, RaimAbility::Neither) => RaimAbility::Neither,
            (RaimAbility::Detection, _) | (_, RaimAbility::Detection) => RaimAbility::Detection,
            _ => RaimAbility::Isolation,
        };
        match ability {
            RaimAbility::Neither => NONE,
            RaimAbility::Detection => (result.solution, ability, PrnSet::EMPTY),
            RaimAbility::Isolation => (result.solution, ability, result.isolated),
        }
    }

    /// `rrm` (GT p. 40): Time RAIM's result.
    fn rrm<O: Output + ?Sized>(&self, record: &Record, out: &mut O) {
        const RESERVED: &str = "+000";
        let (solution, ability, isolated) = self.raim(record);
        let solution = match solution {
            RaimSolution::Ok => 0,
            RaimSolution::Alarm => 1,
            RaimSolution::Unknown => 2,
        };
        let ability = match ability {
            RaimAbility::Isolation => 0,
            RaimAbility::Detection => 1,
            RaimAbility::Neither => 2,
        };
        let sawtooth = record.timing.map_or(0, |timing| {
            timing.sawtooth.0.clamp(SAWTOOTH_LEAST, SAWTOOTH_MOST)
        });
        let mut body = Body::new("PFEC,GPrrm");
        body.field(format_args!("{solution}"));
        body.field(format_args!("{ability}"));
        let mut removed = (1..=PRNS)
            .filter_map(Prn::new)
            .filter(|prn| isolated.contains(*prn));
        for _ in 0..RRM_SATELLITES {
            let id = removed.next().map_or(0, Prn::get);
            body.field(format_args!("{id:02}"));
        }
        body.field(format_args!("{RESERVED}"));
        body.field(format_args!("{sawtooth:+03}"));
        body.send(Checksum::Omitted, out);
    }

    /// `rsd` (GT p. 40): Time RAIM's setup as `rrs` set it.
    fn rsd<O: Output + ?Sized>(&self, out: &mut O) {
        let mode = match self.traim.pulse {
            PulseMode::Off => 0,
            PulseMode::Always => 1,
            PulseMode::Tracking => 2,
            PulseMode::Confirmed => 3,
        };
        let mut body = Body::new("PFEC,GPrsd");
        body.field(format_args!("{}", u8::from(self.traim.on)));
        body.field(format_args!("{:03}", self.traim.limit));
        body.field(format_args!("{mode}"));
        body.send(Checksum::Omitted, out);
    }

    /// `ssd` (GT p. 32): `set`'s parameters, in one sentence
    /// ("Assumptions").
    fn ssd<O: Output + ?Sized>(&self, out: &mut O) {
        const HEIGHT_TENTHS_PER_CENTIMETER: i32 = 10;
        const T_UNIT: i32 = 100;
        let parameters = &self.parameters;
        let mut body = Body::new("PFEC,GPssd");
        body.field(format_args!("A{}", parameters.almanac_validity));
        body.field(format_args!("D{:02}", parameters.pdop_threshold));
        body.field(format_args!("{UNNAMED_PARAMETER}"));
        body.field(format_args!("G{:03}", parameters.datum));
        body.field(format_args!(
            "H{}",
            SignedTenths {
                tenths: parameters.height.0 / HEIGHT_TENTHS_PER_CENTIMETER,
                width: 8
            }
        ));
        body.field(format_args!("M{:02}", parameters.mask_angle.0));
        body.field(format_args!("S{:02}", parameters.signal_mask));
        body.field(format_args!("T{:+05}", parameters.correction.0 / T_UNIT));
        body.field(format_args!("U{:08X}", parameters.ignored.bits()));
        body.field(format_args!("W{}", parameters.smoothing));
        body.field(format_args!("X{}", parameters.dynamic));
        body.field(format_args!("Z{}", parameters.observation));
        body.field(format_args!("t{:+07}", parameters.correction.0));
        body.send(Checksum::Omitted, out);
    }
}

/// The record's satellites in the order of their IDs.
fn sorted(record: &Record) -> impl Iterator<Item = &Satellite> {
    (1..=PRNS)
        .filter_map(Prn::new)
        .filter_map(|prn| record.satellites.get(prn))
}

/// A field of exactly `length` decimal digits, its value; `None` too for
/// one more than a `u32` holds.
fn number(field: &[u8], length: usize) -> Option<u32> {
    const RADIX: u32 = 10;
    if field.len() != length {
        return None;
    }
    field.iter().try_fold(0, |value: u32, &digit| {
        value
            .checked_mul(RADIX)?
            .checked_add(char::from(digit).to_digit(RADIX)?)
    })
}

/// A field of exactly `length` digits whose value is `least` to `most`.
fn digits_in(field: &[u8], length: usize, least: u8, most: u8) -> Option<u8> {
    number(field, length)
        .and_then(|value| u8::try_from(value).ok())
        .filter(|value| (least..=most).contains(value))
}

/// A sign's value, its `+` perhaps a `0`.
const fn sign_of(sign: u8) -> Option<i32> {
    match sign {
        b'+' | b'0' => Some(1),
        b'-' => Some(-1),
        _ => None,
    }
}

/// A sign and exactly `length` digits, its value.
fn signed(field: &[u8], length: usize) -> Option<i32> {
    let (&sign, digits) = field.split_first()?;
    let magnitude = i32::try_from(number(digits, length)?).ok()?;
    Some(sign_of(sign)? * magnitude)
}

/// A height as `set,H` gives one, a sign, five digits of whole meters, a
/// point and a tenth, nine bytes with its letter (GT p. 11), its `+`
/// perhaps a `0`; its value in tenths.
fn signed_tenths(field: &[u8]) -> Option<i32> {
    const WHOLE_DIGITS: usize = 5;
    let (&sign, rest) = field.split_first()?;
    let (whole, tenth) = rest.split_at(rest.len().checked_sub(2)?);
    let [b'.', tenth] = *tenth else {
        return None;
    };
    let whole = i32::try_from(number(whole, WHOLE_DIGITS)?).ok()?;
    let tenth = i32::try_from(number(&[tenth], 1)?).ok()?;
    let magnitude = whole.checked_mul(10)?.checked_add(tenth)?;
    Some(sign_of(sign)? * magnitude)
}

/// Eight hexadecimal digits, either case, their value (GT p. 11).
fn hexadecimal(field: &[u8]) -> Option<u32> {
    const DIGITS: usize = 8;
    const RADIX: u32 = 16;
    if field.len() != DIGITS {
        return None;
    }
    field.iter().try_fold(0, |value: u32, &digit| {
        Some(value * RADIX + char::from(digit).to_digit(RADIX)?)
    })
}

/// An angle as `GLL` gives one: `degree_digits` digits of degrees, two
/// of minutes and any of a fraction of a minute, of which those past
/// the fourth are ignored (GT p. 6), and its hemisphere.
fn angle(
    field: &[u8],
    degree_digits: usize,
    most: i32,
    hemisphere: &[u8],
    positive: u8,
    negative: u8,
) -> Option<MilliArcSeconds> {
    const MINUTE_FIELD: usize = 2;
    let (whole, fraction) = match field.iter().position(|&byte| byte == b'.') {
        Some(point) => (&field[..point], &field[point + 1..]),
        None => (field, &[][..]),
    };
    if whole.len() != degree_digits + MINUTE_FIELD || !fraction.iter().all(u8::is_ascii_digit) {
        return None;
    }
    let (degrees, minutes) = whole.split_at(degree_digits);
    let degrees = i32::try_from(number(degrees, degree_digits)?).ok()?;
    let minutes = i32::try_from(number(minutes, MINUTE_FIELD)?).ok()?;
    let digits = fraction
        .iter()
        .chain(core::iter::repeat(&b'0'))
        .take(MINUTE_DIGITS)
        .fold(0, |value, digit| value * 10 + i32::from(digit - b'0'));
    if degrees > most
        || minutes > MINUTES_MOST
        || (degrees == most && (minutes != 0 || digits != 0))
    {
        return None;
    }
    let magnitude =
        degrees * MAS_PER_DEGREE + minutes * MAS_PER_MINUTE + digits * MAS_PER_MINUTE_DIGIT;
    match hemisphere {
        [sign] if *sign == positive => Some(MilliArcSeconds(magnitude)),
        [sign] if *sign == negative => Some(MilliArcSeconds(-magnitude)),
        _ => None,
    }
}

#[cfg(test)]
mod tests;
