//! The synthetic GPS engine on DUART channel A: smartclock-gps's
//! engine, the one the project's tools share (`smartclock_gps::engine`),
//! as the Oncore each model carries, the six-channel VP the Z3805A
//! carries, the eight-channel VP the 58503A does, or the UT Plus a
//! shipped 58503B reports, or the Furuno GT-74 of the Z3815A and the
//! Z3805A 1.01.03 (`Engine`; docs/firmware/gps.md), told each
//! second what the made-up sky of its `fake_receiver` shows at the antenna's
//! place (`sky`).
//!
//! It is the far end of the channel's `Line`, run on the emulator's
//! thread a slice at a time with the board's clock, so its seconds are
//! the board's: second *n* of its time scale begins *n* seconds of
//! board time after power-up, at `start` plus *n*.  The emulator makes
//! the 1 PPS, so it acts on the engine's pulse settings: each pulse is
//! second *n*'s start moved by the pulse offset and the cable delay,
//! put out as the pulse mode says, and `on_pulse` gives it to whoever
//! drives it into the board.
//!
//! Its serial side is behind `Port`, so the engine knows nothing of
//! what carries its bytes: on the Unicorn board, the line's `FarEnd`;
//! on an mc683xx-board board, its serial ports' frames.
//!
//! `engine` holds the shared engine on the board's clock, `sky` is
//! what it is told, `range` the position and power-up time it can be
//! given, and `external` an engine elsewhere, `--gps`'s.

pub mod engine;
pub(crate) mod external;
pub mod range;
pub mod sky;

use std::io::Read;
use std::io::Write;

use jiff::Timestamp;
use smartclock_gps::engine::Options;
use smartclock_gps::engine::furuno;
use smartclock_gps::engine::furuno::Identity as FurunoIdentity;
use smartclock_gps::engine::identity::Identity;
use smartclock_gps::engine::identity::UT_PLUS_2_2;
use smartclock_gps::engine::identity::VP_EIGHT;
use smartclock_gps::engine::identity::VP_EIGHT_WITHOUT_TIME_RAIM;
use smartclock_gps::engine::identity::VP_SIX;
use smartclock_gps::record::Position;
use smartclock_gps::units::Seconds;

use crate::board::time::CPU_HZ;
use crate::board::time::Cycles;
use crate::line::FarEnd;

/// GPS time less UTC, seconds, unless `--gps-leap-seconds` gives
/// another: the leap seconds since 1980, 18 since 2017-01-01 (IERS
/// Bulletin C).
pub const GPS_LESS_UTC: i8 = 18;

/// An engine the synthetic one can be, by the name a board file's
/// `engine` and `--gps-engine` give it.
#[derive(Debug)]
pub(crate) struct Engine {
    /// An Oncore's model's number's first five characters, lower case,
    /// and `-no-time-raim` where its options leave Time RAIM out; a
    /// Furuno's model, `gt-74`, and what its software differs in, as
    /// smartclock-gps-feed's `--engine` names it.
    pub(crate) name: &'static str,
    /// Which engine of which family the shared engine is.
    pub(crate) family: Family,
}

/// An engine's family, and which of it the shared engine is.
#[derive(Debug, Clone, Copy)]
pub(crate) enum Family {
    /// A Motorola Oncore, on its binary `@@` messages: its model and its
    /// `@@Cj`.
    Oncore(&'static Identity),
    /// A Furuno GT-74, on NMEA and Furuno's `$PFEC` sentences: its
    /// software, as `tst` gives it (`smartclock_gps::engine::furuno`).
    Furuno(&'static FurunoIdentity),
}

/// The protocols a board file's GPS engine part names, an Oncore's and
/// a Furuno's.
pub(crate) const MOTOROLA_BINARY: &str = "motorola-binary";
pub(crate) const NMEA: &str = "nmea";

impl Family {
    /// The protocol it speaks, as a board file's `protocol` names it.
    pub(crate) const fn protocol(self) -> &'static str {
        match self {
            Self::Oncore(_) => MOTOROLA_BINARY,
            Self::Furuno(_) => NMEA,
        }
    }
}

/// The Z3805A's engine: a six-channel VP, software 8.4.
pub(crate) const B1121: Engine = Engine {
    name: "b1121",
    family: Family::Oncore(&VP_SIX),
};

/// The 58503A's engine: an eight-channel VP, software 8.8, so the
/// firmware sends no second @@Ar (docs/firmware/gps.md, "Six or eight
/// channels").
pub(crate) const B4121: Engine = Engine {
    name: "b4121",
    family: Family::Oncore(&VP_EIGHT),
};

/// The 58503A's engine with no Time RAIM in its options, `B` alone, so
/// that a host asking the option sends no Time RAIM command.  No such
/// engine has been seen.
pub(crate) const B4121_WITHOUT_TIME_RAIM: Engine = Engine {
    name: "b4121-no-time-raim",
    family: Family::Oncore(&VP_EIGHT_WITHOUT_TIME_RAIM),
};

/// The 58503B's engine: an eight-channel UT Plus Oncore, the R5122U1152
/// a shipped 58503B reports, software 2.2 (docs/firmware/gps.md, "The
/// UT Plus").
pub(crate) const UT_PLUS: Engine = Engine {
    name: "r5122",
    family: Family::Oncore(&UT_PLUS_2_2),
};

/// The Z3815A's engine, and the Z3805A 1.01.03's: a Furuno GT-74 with
/// Time RAIM, its software made up (`GT_74`), which the images take for
/// a GT-74 of six channels with no correction to its 1 PPS
/// (docs/firmware/z3805a-1.01.03.md, "The GPS engine").
pub(crate) const GT_74: Engine = Engine {
    name: "gt-74",
    family: Family::Furuno(&furuno::GT_74),
};

/// A GT-74 whose software the Z3815A's image knows to have no Time
/// RAIM, 4850113 version 2.
pub(crate) const GT_74_WITHOUT_TIME_RAIM: Engine = Engine {
    name: "gt-74-without-time-raim",
    family: Family::Furuno(&furuno::GT_74_WITHOUT_TIME_RAIM),
};

/// A GT-74 whose software, 4850102 version 8, puts its 1 PPS 400 ns
/// early, which the images correct in the delay they send.
pub(crate) const GT_74_PULSE_EARLY: Engine = Engine {
    name: "gt-74-pulse-early",
    family: Family::Furuno(&furuno::GT_74_PULSE_EARLY),
};

/// Every engine, by name (`Engine::named`).
#[cfg(feature = "mc683xx")]
pub(crate) const ENGINES: [&Engine; 7] = [
    &B1121,
    &B4121,
    &B4121_WITHOUT_TIME_RAIM,
    &UT_PLUS,
    &GT_74,
    &GT_74_WITHOUT_TIME_RAIM,
    &GT_74_PULSE_EARLY,
];

impl Engine {
    /// The engine named `name`, as a board file's GPS engine gives it.
    #[cfg(feature = "mc683xx")]
    pub(crate) fn named(name: &str) -> Option<&'static Self> {
        ENGINES.into_iter().find(|engine| engine.name == name)
    }
}

/// Picoseconds a second, the 1 PPS's unit on board time.
pub(crate) const PICOSECONDS_PER_SECOND: u64 = 1_000_000_000_000;
/// Picoseconds a nanosecond, the pulse settings' unit.
pub(crate) const PICOSECONDS_PER_NANOSECOND: u64 = 1000;

/// The engine's 1 PPS: its rise.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) struct Pulse {
    /// When, picoseconds of board time from power-up.
    pub(crate) picoseconds: u64,
}

impl Pulse {
    /// The first of the board's cycles at or after it.
    pub(crate) fn cycles(self) -> Cycles {
        let cycles = (u128::from(self.picoseconds) * u128::from(CPU_HZ))
            .div_ceil(u128::from(PICOSECONDS_PER_SECOND));
        Cycles(u64::try_from(cycles).unwrap_or(u64::MAX))
    }
}

/// What a pulse is given to.
pub(crate) type PulseHook = Box<dyn FnMut(&Pulse)>;

/// What the bytes the firmware sends the engine are given to, as they
/// are read.
pub(crate) type HeardHook = Box<dyn FnMut(&[u8])>;

/// How the synthetic engine is set up.
#[derive(Debug, Clone, Copy)]
pub(crate) struct Config {
    /// Where its antenna is.
    pub(crate) position: Position,
    /// UTC at power-up, a whole second.
    pub(crate) start: Timestamp,
    /// GPS time less UTC: the leap seconds since 1980.
    pub(crate) leap_seconds: Seconds<i8>,
    /// Which Oncore it is.
    pub(crate) engine: &'static Engine,
    /// How it departs from that Oncore: deaf, or powering up as a
    /// receiver left it configured.
    pub(crate) options: Options,
}

/// The engine's serial port: what the firmware sends it, and what it
/// sends back.
pub(crate) trait Port {
    /// What has come since the last call, into `buffer`: how many
    /// bytes, 0 for none.
    fn receive(&mut self, buffer: &mut [u8]) -> usize;
    /// Sends `bytes`.
    fn send(&mut self, bytes: &[u8]);
}

impl Port for FarEnd {
    fn receive(&mut self, buffer: &mut [u8]) -> usize {
        self.read(buffer).unwrap_or(0)
    }

    fn send(&mut self, bytes: &[u8]) {
        // The line takes everything; a write cannot fail.
        let _ = self.write_all(bytes);
    }
}

#[cfg(test)]
mod tests;
