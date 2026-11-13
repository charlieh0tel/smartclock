//! One second of what a GPS engine knows, whatever the engine.
//!
//! A [`Record`] is what a source of GPS data hands on each second: the
//! time, the fix and the sky.  It names no message; what stands in for
//! an Oncore or a Furuno turns it into that engine's own.
//!
//! A field is here only if a message some `SmartClock` image decodes
//! carries it, and its documentation cites that message's page.  The
//! manuals, by the page each prints: VP, the VP Oncore's command
//! reference, `third_party/motorola/VPCommands.pdf`; UT, TRM0003 chapter
//! 6, `third_party/motorola/oncore-ut-users-guide/ch6.pdf`; M12+,
//! `third_party/motorola/m12plus-users-guide.pdf`; GT, Furuno's GT-8031
//! protocol specification,
//! `third_party/furuno/Furuno_GT8031_Protocol_Specification_Rev1.pdf`.
//!
//! A record is some hundreds of bytes ([`RECORD_BYTES_MAX`] bounds it),
//! so it is meant to live in a `static` or a struct and be filled in
//! place through a reference, not passed by value.  [`Record`] and the
//! parts of it that may gain a field are `#[non_exhaustive]`: start from
//! [`Record::EMPTY`], [`Satellite::new`], [`Timing::ZERO`] or
//! [`Almanac::new`] and set fields.

use crate::time::GpsSeconds;
use crate::time::GpsWeek;
use crate::time::LeapEvent;
use crate::units::Centimeters;
use crate::units::CentimetersPerSecond;
use crate::units::DecibelHertz;
use crate::units::Degrees;
use crate::units::Hertz;
use crate::units::MilliArcSeconds;
use crate::units::Nanoseconds;
use crate::units::Prn;
use crate::units::PrnSet;
use crate::units::Seconds;
use crate::units::TenthDegrees;
use crate::units::Tenths;

/// The satellites a record holds at most: the slots of `@@Bb` (VP
/// p. 75), and what three `GSV` sentences carry (GT p. 23).
pub const SATELLITES: usize = 12;

/// A bound on a record's size in bytes, on every target.
pub const RECORD_BYTES_MAX: usize = 512;

const _: () = assert!(size_of::<Record>() <= RECORD_BYTES_MAX);

/// A time to the nanosecond.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct GpsTime {
    /// The second.
    pub seconds: GpsSeconds,
    /// How far into it, less than a second (VP p. 71: 0 to
    /// 999,999,999).
    pub subsecond: Nanoseconds<u32>,
}

/// What kind of fix the engine has.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Fix {
    /// None.
    None,
    /// Latitude and longitude, the height held (VP p. 73, receiver
    /// status bit 4; `GSA` mode 2, GT p. 22).
    TwoD,
    /// Latitude, longitude and height (VP p. 73, bit 5; `GSA` mode 3).
    ThreeD,
    /// Time alone, the position held (VP p. 73, bit 3, position hold).
    TimeOnly,
}

/// Where the antenna is (VP p. 71).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Position {
    /// Latitude, north positive.
    pub latitude: MilliArcSeconds,
    /// Longitude, east positive.
    pub longitude: MilliArcSeconds,
    /// Height above the GPS reference ellipsoid.
    pub ellipsoid: Centimeters,
    /// Height above mean sea level.
    pub mean_sea_level: Centimeters,
}

/// How the antenna moves (VP p. 72).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Velocity {
    /// Its speed.
    pub speed: CentimetersPerSecond,
    /// Its heading from true north.
    pub heading: TenthDegrees,
}

/// The dilutions of precision (`@@Bk`, VP p. 96; `GSA` carries the
/// position, horizontal and vertical ones, GT p. 22).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Dops {
    /// GDOP.
    pub geometric: Tenths,
    /// PDOP.
    pub position: Tenths,
    /// HDOP.
    pub horizontal: Tenths,
    /// VDOP.
    pub vertical: Tenths,
    /// TDOP.
    pub time: Tenths,
}

/// The quality of the time solution, as Time RAIM's message reports it
/// (`@@Bn`, VP p. 106).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[non_exhaustive]
pub struct Timing {
    /// The negative sawtooth: the time error of the next 1 PPS pulse.
    pub sawtooth: Nanoseconds<i8>,
    /// The time solution's one-sigma accuracy estimate.
    pub sigma: Nanoseconds<u16>,
}

impl Timing {
    /// No sawtooth error, and an estimate of zero.
    pub const ZERO: Self = Self {
        sawtooth: Nanoseconds(0),
        sigma: Nanoseconds(0),
    };
}

/// How far a receiver channel has got with a satellite, least first.
/// An engine reports this in its own terms: an Oncore as its channel
/// tracking modes 0 to 8 (VP p. 72).
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum Tracking {
    /// Looking for the satellite's code.
    Searching,
    /// Its code is found.
    CodeLock,
    /// Its carrier is found.
    CarrierLock,
    /// Its data bits are in step.
    BitSync,
    /// Its data frames are in step, so its time is known.
    FrameSync,
    /// Its ephemeris is being read.
    Ephemeris,
    /// It can be used for a fix.
    Position,
}

/// What a satellite's own data says of its health (`@@Bb`, VP p. 75;
/// `$PFEC,GPanc`, GT p. 27).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Health {
    /// Nothing yet.
    Unknown,
    /// Usable.
    Healthy,
    /// Not to be used.
    Unhealthy,
}

/// One satellite in view.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[non_exhaustive]
pub struct Satellite {
    /// Its ID.
    pub prn: Prn,
    /// Its elevation, 0 to 90 (VP p. 75).
    pub elevation: Degrees<i8>,
    /// Its azimuth from true north, 0 to 359 (VP p. 75).
    pub azimuth: Degrees<u16>,
    /// Its signal, if one is received (UT p. 6.20, the channel's C/No;
    /// `GSV`, GT p. 23).
    pub signal: Option<DecibelHertz>,
    /// How far a channel has got with it (VP p. 72).
    pub tracking: Tracking,
    /// Whether the fix uses it (VP p. 73, channel status bit 7; `GSA`,
    /// GT p. 22).
    pub used: bool,
    /// Its health.
    pub health: Health,
    /// Whether its ephemeris is held.
    pub ephemeris: bool,
    /// Its Doppler shift (VP p. 75).
    pub doppler: Option<Hertz>,
    /// Its own time solution less the mean of them all, which Time RAIM
    /// judges (the per-satellite time estimate of `@@Bn`, VP p. 106).
    pub time_error: Option<Nanoseconds<i32>>,
}

impl Satellite {
    /// A satellite in view at a place in the sky, with no signal yet.
    #[must_use]
    pub const fn new(prn: Prn, elevation: Degrees<i8>, azimuth: Degrees<u16>) -> Self {
        Self {
            prn,
            elevation,
            azimuth,
            signal: None,
            tracking: Tracking::Searching,
            used: false,
            health: Health::Unknown,
            ephemeris: false,
            doppler: None,
            time_error: None,
        }
    }

    /// The same satellite received at `signal`, healthy, its ephemeris
    /// held, and used for the fix.
    #[must_use]
    pub const fn tracked(self, signal: DecibelHertz) -> Self {
        Self {
            signal: Some(signal),
            tracking: Tracking::Position,
            used: true,
            health: Health::Healthy,
            ephemeris: true,
            ..self
        }
    }
}

/// The satellites in view, [`SATELLITES`] at most, in the order a
/// receiver would take them up: whoever fills it puts first the ones to
/// track first.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Satellites {
    /// The first `count` are the satellites.
    slots: [Option<Satellite>; SATELLITES],
    /// How many slots are filled.
    count: usize,
    /// Whether one was left out for want of room.
    truncated: bool,
}

impl Satellites {
    /// None in view.
    pub const EMPTY: Self = Self {
        slots: [None; SATELLITES],
        count: 0,
        truncated: false,
    };

    /// The satellites of `table`, as by [`Self::push`] in turn.
    #[must_use]
    pub const fn from_slice(table: &[Satellite]) -> Self {
        let mut satellites = Self::EMPTY;
        let mut index = 0;
        while index < table.len() {
            satellites.push(table[index]);
            index += 1;
        }
        satellites
    }

    /// Adds one after the rest.  With no room, the lowest in the sky
    /// goes, the new one if it is that, and [`Self::truncated`] is set.
    pub const fn push(&mut self, satellite: Satellite) {
        if self.count < SATELLITES {
            self.slots[self.count] = Some(satellite);
            self.count += 1;
            return;
        }
        self.truncated = true;
        let mut lowest = satellite.elevation.0;
        let mut lowest_index = None;
        let mut index = 0;
        while index < self.count {
            if let Some(held) = self.slots[index]
                && held.elevation.0 < lowest
            {
                lowest = held.elevation.0;
                lowest_index = Some(index);
            }
            index += 1;
        }
        if let Some(index) = lowest_index {
            self.remove_at(index);
            self.slots[self.count] = Some(satellite);
            self.count += 1;
        }
    }

    /// Takes one out, the rest keeping their order.  Returns whether it
    /// was there.
    pub const fn remove(&mut self, prn: Prn) -> bool {
        let mut index = 0;
        while index < self.count {
            if let Some(held) = self.slots[index]
                && held.prn.get() == prn.get()
            {
                self.remove_at(index);
                return true;
            }
            index += 1;
        }
        false
    }

    const fn remove_at(&mut self, index: usize) {
        let mut next = index + 1;
        while next < self.count {
            self.slots[next - 1] = self.slots[next];
            next += 1;
        }
        self.count -= 1;
        self.slots[self.count] = None;
    }

    /// One satellite, to read.
    #[must_use]
    pub fn get(&self, prn: Prn) -> Option<&Satellite> {
        self.iter().find(|satellite| satellite.prn == prn)
    }

    /// One satellite, to change.
    #[must_use]
    pub fn get_mut(&mut self, prn: Prn) -> Option<&mut Satellite> {
        self.iter_mut().find(|satellite| satellite.prn == prn)
    }

    /// The satellites in order.
    pub fn iter(&self) -> impl Iterator<Item = &Satellite> {
        self.slots.iter().flatten()
    }

    /// The satellites in order, to change.
    pub fn iter_mut(&mut self) -> impl Iterator<Item = &mut Satellite> {
        self.slots.iter_mut().flatten()
    }

    /// How many there are.
    #[must_use]
    pub const fn len(&self) -> usize {
        self.count
    }

    /// Whether there is none.
    #[must_use]
    pub const fn is_empty(&self) -> bool {
        self.count == 0
    }

    /// Whether more were in view than there was room for.
    #[must_use]
    pub const fn truncated(&self) -> bool {
        self.truncated
    }
}

/// The almanac the engine holds (`@@Bd`, VP p. 79; `$PFEC,GPanc`, GT
/// p. 27).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[non_exhaustive]
pub struct Almanac {
    /// Its week.  `@@Bd` sends [`GpsWeek::almanac`] of it.
    pub week: GpsWeek,
    /// Its time of almanac, raw as broadcast: 0 to 147.
    pub time_of_almanac: u8,
    /// The satellites it covers.
    pub held: PrnSet,
    /// Those of them it calls healthy.
    pub healthy: PrnSet,
}

impl Almanac {
    /// An almanac that covers `held` and calls each of them healthy.
    #[must_use]
    pub const fn new(week: GpsWeek, time_of_almanac: u8, held: PrnSet) -> Self {
        Self {
            week,
            time_of_almanac,
            held,
            healthy: held,
        }
    }
}

/// What the engine senses of its antenna's feed (UT p. 6.43, the
/// undercurrent and overcurrent bits read together; p. 6.20).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Antenna {
    /// Not sensed.
    Unknown,
    /// Properly connected.
    Connected,
    /// Open: no antenna connected.
    Open,
    /// Shorted.
    Shorted,
}

/// One second of what a GPS engine knows.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[non_exhaustive]
pub struct Record {
    /// The time of the 1 PPS pulse this record goes with (the date and
    /// time of `@@Ba`, VP p. 71; of `$PFEC,GPtps`, GT p. 36).  `None`
    /// when whoever fills the record has no time.
    pub time: Option<GpsTime>,
    /// GPS less UTC at that time (`@@Bo`, UT p. 6.81; `$PFEC,GPtps`
    /// field 8, GT p. 37): the offset in effect, so from the second of
    /// [`Self::leap`] on, the one that leap leaves, and never the one
    /// before it with the step still to add.  `None` until the
    /// satellites' UTC parameters have been read.
    pub utc_offset: Option<Seconds<i8>>,
    /// The leap second announced, kept once it has passed (`@@Bj`, VP
    /// p. 94; `@@Gj`, M12+ p. 153; `$PFEC,GPtps` fields 6 and 7, which
    /// go on reporting a past one, GT pp. 36-37).  `None` when none is
    /// known.
    pub leap: Option<LeapEvent>,
    /// The kind of fix.
    pub fix: Fix,
    /// The position.  `None` when there is none to give.
    pub position: Option<Position>,
    /// The velocity.  `None` when it is not computed.
    pub velocity: Option<Velocity>,
    /// The dilutions of precision.  `None` when they cannot be computed.
    pub dops: Option<Dops>,
    /// The time solution's quality.  `None` when it is not judged.
    pub timing: Option<Timing>,
    /// The satellites in view.
    pub satellites: Satellites,
    /// The almanac.  `None` when the engine has none.
    pub almanac: Option<Almanac>,
    /// The antenna's feed.
    pub antenna: Antenna,
}

impl Record {
    /// A record of an engine that knows nothing yet.
    pub const EMPTY: Self = Self {
        time: None,
        utc_offset: None,
        leap: None,
        fix: Fix::None,
        position: None,
        velocity: None,
        dops: None,
        timing: None,
        satellites: Satellites::EMPTY,
        almanac: None,
        antenna: Antenna::Unknown,
    };
}

#[cfg(test)]
mod tests {
    use super::Fix;
    use super::RECORD_BYTES_MAX;
    use super::Record;
    use super::SATELLITES;
    use super::Satellite;
    use super::Satellites;
    use super::Tracking;
    use crate::units::DecibelHertz;
    use crate::units::Degrees;
    use crate::units::Prn;

    const fn satellite(id: u8, elevation: i8) -> Satellite {
        Satellite::new(Prn::new(id).unwrap(), Degrees(elevation), Degrees(0))
    }

    fn ids(satellites: &Satellites) -> std::vec::Vec<u8> {
        satellites.iter().map(|held| held.prn.get()).collect()
    }

    /// A record fits its bound on this target; the same assertion, made
    /// where the bound is declared, holds for every target built.
    #[test]
    fn a_record_fits_its_bound() {
        assert!(size_of::<Record>() <= RECORD_BYTES_MAX);
    }

    /// A record and a sky are built where a `static` is: no allocation,
    /// no code at run time.
    #[test]
    fn a_record_is_built_in_a_static() {
        static SKY: Satellites = Satellites::from_slice(&[
            satellite(3, 60).tracked(DecibelHertz(45)),
            satellite(17, 20),
        ]);
        static RECORD: Record = {
            let mut record = Record::EMPTY;
            record.fix = Fix::ThreeD;
            record.satellites = SKY;
            record
        };
        assert_eq!(RECORD.satellites.len(), 2);
        let first = RECORD.satellites.get(Prn::new(3).unwrap()).unwrap();
        assert_eq!(first.signal, Some(DecibelHertz(45)));
        assert_eq!(first.tracking, Tracking::Position);
        assert!(first.used);
        assert_eq!(RECORD.fix, Fix::ThreeD);
    }

    #[test]
    fn satellites_keep_their_order() {
        let mut satellites = Satellites::EMPTY;
        assert!(satellites.is_empty());
        for id in [9, 2, 30] {
            satellites.push(satellite(id, 45));
        }
        assert_eq!(ids(&satellites), [9, 2, 30]);
        assert!(satellites.remove(Prn::new(2).unwrap()));
        assert!(!satellites.remove(Prn::new(2).unwrap()));
        assert_eq!(ids(&satellites), [9, 30]);
        assert_eq!(satellites.len(), 2);
        assert!(!satellites.truncated());

        satellites.get_mut(Prn::new(30).unwrap()).unwrap().used = true;
        assert!(satellites.get(Prn::new(30).unwrap()).unwrap().used);
        assert!(satellites.get(Prn::new(2).unwrap()).is_none());
    }

    /// More in view than `@@Bb` has slots for: the highest are kept.
    #[test]
    fn the_lowest_satellite_gives_way() {
        let full = u8::try_from(SATELLITES).unwrap();
        let mut satellites = Satellites::EMPTY;
        for id in 1..=full {
            // Satellite 1 is the lowest, at 11 degrees.
            satellites.push(satellite(id, 10 + i8::try_from(id).unwrap()));
        }
        assert!(!satellites.truncated());

        // Lower than all: left out.
        satellites.push(satellite(20, 5));
        assert!(satellites.truncated());
        assert_eq!(satellites.len(), SATELLITES);
        assert!(satellites.get(Prn::new(20).unwrap()).is_none());

        // Higher than the lowest: takes its place, at the end.
        satellites.push(satellite(21, 80));
        assert_eq!(satellites.len(), SATELLITES);
        assert!(satellites.get(Prn::new(1).unwrap()).is_none());
        assert_eq!(ids(&satellites).last(), Some(&21));
        assert_eq!(ids(&satellites).first(), Some(&2));
    }
}
