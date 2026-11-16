//! Records of a stream of UBX bytes ("From messages to a record" in
//! the crate's documentation).

use core::mem;

use smartclock_gps::engine::settings::SettingSet;
use smartclock_gps::record::Antenna;
use smartclock_gps::record::Dops;
use smartclock_gps::record::Ecef;
use smartclock_gps::record::Fix;
use smartclock_gps::record::GpsTime;
use smartclock_gps::record::Raim;
use smartclock_gps::record::Record;
use smartclock_gps::record::Satellites;
use smartclock_gps::record::SelfTest;
use smartclock_gps::record::field::Declaration;
use smartclock_gps::record::field::FieldSet;
use smartclock_gps::record::field::RecordField;
use smartclock_gps::source::replay::Assemble;
use smartclock_gps::time::GpsSeconds;
use smartclock_gps::time::GpsWeek;
use smartclock_gps::time::LeapEvent;
use smartclock_gps::units::Milliseconds;
use smartclock_gps::units::Nanoseconds;
use smartclock_gps::units::Seconds;

use crate::frame::Deframer;
use crate::message::Message;
use crate::nav::LeapNotice;
use crate::nav::NavPvt;
use crate::nav::NavTimeGps;
use crate::nav::NavTimeLs;
/// What an [`Assembler`] has of the epoch in progress.  Every message
/// in it has the same `iTOW`.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
struct Epoch {
    /// Its `iTOW`; `None` when no epoch is in progress.
    time_of_week: Option<Milliseconds<u32>>,
    /// Its `NAV-PVT`.
    solution: Option<NavPvt>,
    /// Its `NAV-TIMEGPS`.
    time: Option<NavTimeGps>,
    /// Its `NAV-TIMELS`.
    leap: Option<NavTimeLs>,
    /// Its `NAV-DOP`.
    dops: Option<Dops>,
    /// Its `NAV-POSECEF`.
    ecef: Option<Ecef>,
    /// Its `NAV-SAT`.
    satellites: Satellites,
}

impl Epoch {
    /// No epoch in progress.
    const EMPTY: Self = Self {
        time_of_week: None,
        solution: None,
        time: None,
        leap: None,
        dops: None,
        ecef: None,
        satellites: Satellites::EMPTY,
    };
}

/// A leap second an [`Assembler`] was told of, with GPS less UTC until
/// it.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
struct Announced {
    event: LeapEvent,
    before: Seconds<i8>,
}

/// What an [`Assembler`] fills of a record.  It acts on none of an
/// engine's settings: it reads a receiver and sends it nothing, and
/// each of those the images set
/// ([`IMAGE_SETS`](smartclock_gps::record::field::IMAGE_SETS)) would
/// be a configuration sent.  `docs/firmware/gps-setting-exceptions.tsv`
/// excuses it each.
pub const DECLARATION: Declaration = Declaration {
    name: "u-blox",
    fills: FieldSet::of(&[
        RecordField::Time,
        RecordField::UtcOffset,
        RecordField::Leap,
        RecordField::Fix,
        RecordField::Latitude,
        RecordField::Longitude,
        RecordField::EllipsoidHeight,
        RecordField::MeanSeaLevelHeight,
        RecordField::Speed,
        RecordField::Heading,
        RecordField::GeometricDop,
        RecordField::PositionDop,
        RecordField::HorizontalDop,
        RecordField::VerticalDop,
        RecordField::TimeDop,
        RecordField::Satellites,
        RecordField::SatelliteElevation,
        RecordField::SatelliteAzimuth,
        RecordField::SatelliteSignal,
        RecordField::SatelliteTracking,
        RecordField::SatelliteUsed,
        RecordField::SatelliteHealth,
        RecordField::SatelliteEphemeris,
        RecordField::Antenna,
        RecordField::SelfTest,
        RecordField::Ecef,
        RecordField::RaimSolution,
        RecordField::RaimAbility,
    ]),
    acts: SettingSet::EMPTY,
};

/// Makes a [`Record`] of each navigation epoch in a stream of UBX bytes
/// ("From messages to a record" in the crate's documentation).  `N`
/// is the longest payload it takes, as a [`Deframer`]'s is:
/// [`nav_sat_length`](crate::nav::nav_sat_length) of the satellites the receiver may report.
///
/// ```
/// use smartclock_gps::record::Fix;
/// use smartclock_gps::record::Record;
/// use smartclock_gps::source::replay::Assemble;
/// use smartclock_gps_ublox::assembler::Assembler;
/// use smartclock_gps_ublox::nav::nav_sat_length;
///
/// let mut assembler = Assembler::<{ nav_sat_length(32) }>::new();
/// let mut record = Record::EMPTY;
/// // NAV-EOE alone: an epoch of a receiver that says nothing else.
/// let stream = [0xb5, 0x62, 0x01, 0x61, 0x04, 0x00, 0x10, 0x27, 0x00, 0x00, 0x9d, 0x7c];
/// let records = stream
///     .iter()
///     .filter(|&&byte| assembler.push(byte, &mut record))
///     .count();
/// assert_eq!(records, 1);
/// assert_eq!(record.fix, Fix::None);
/// ```
#[derive(Debug, Clone)]
pub struct Assembler<const N: usize> {
    deframer: Deframer<N>,
    /// What the frames so far have said.
    assembly: Assembly,
    /// The frames discarded.
    errors: u32,
}

/// What an [`Assembler`] keeps of the messages it has taken: apart from
/// its deframer, which lends the message being taken.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
struct Assembly {
    /// The week to resolve the receiver's near, if it is not trusted.
    reference: Option<GpsWeek>,
    /// The epoch in progress.
    epoch: Epoch,
    /// The antenna's feed, as `MON-HW` last gave it.
    antenna: Antenna,
    /// The Time RAIM result, as `TIM-TP` last gave it.
    raim: Option<Raim>,
    /// The leap second last announced.
    announced: Option<Announced>,
}

impl<const N: usize> Default for Assembler<N> {
    fn default() -> Self {
        Self::new()
    }
}

impl<const N: usize> Assembler<N> {
    /// An assembler at a stream's start, which takes the receiver's
    /// week as it gives it.
    #[must_use]
    pub const fn new() -> Self {
        Self::resolving(None)
    }

    /// An assembler that takes only the ten broadcast bits of the
    /// receiver's week, and resolves them to the week nearest
    /// `reference`: for a receiver that dates the stream 1024 weeks
    /// off, as one does whose firmware is older than the rollover it
    /// is run after (section 9.9.1, p. 29).
    #[must_use]
    pub const fn near(reference: GpsWeek) -> Self {
        Self::resolving(Some(reference))
    }

    const fn resolving(reference: Option<GpsWeek>) -> Self {
        Self {
            deframer: Deframer::new(),
            assembly: Assembly {
                reference,
                epoch: Epoch::EMPTY,
                antenna: Antenna::Unknown,
                raim: None,
                announced: None,
            },
            errors: 0,
        }
    }

    /// How many frames were discarded for a bad length, a length past
    /// `N` or a bad checksum.  It stops at the count's end.
    #[must_use]
    pub const fn errors(&self) -> u32 {
        self.errors
    }
}

impl Assembly {
    /// Takes a message, and returns whether it ended an epoch, whose
    /// record is then in `record`.
    fn take(&mut self, message: &Message<'_>, record: &mut Record) -> bool {
        let Some(time_of_week) = message.epoch() else {
            match message {
                Message::MonHw(hardware) => self.antenna = hardware.antenna,
                Message::TimTp(pulse) => self.raim = pulse.time_raim(),
                _ => {}
            }
            return false;
        };
        let last = matches!(message, Message::NavEoe(_));
        let other = self
            .epoch
            .time_of_week
            .is_some_and(|open| open != time_of_week);
        if other {
            self.close(record);
        }
        if last {
            if !other {
                self.close(record);
            }
            return true;
        }
        self.epoch.time_of_week = Some(time_of_week);
        match message {
            Message::NavPosEcef(ecef) => self.epoch.ecef = Some(ecef.position),
            Message::NavDop(dop) => self.epoch.dops = Some(dop.dops),
            Message::NavPvt(solution) => self.epoch.solution = Some(*solution),
            Message::NavTimeGps(time) => self.epoch.time = Some(*time),
            Message::NavTimeLs(leap) => self.epoch.leap = Some(*leap),
            Message::NavSat(sat) => self.epoch.satellites = sat.sky(),
            Message::NavEoe(_) | Message::MonHw(_) | Message::TimTp(_) => {}
        }
        other
    }

    /// Ends the epoch in progress and writes its record.
    fn close(&mut self, record: &mut Record) {
        let epoch = mem::replace(&mut self.epoch, Epoch::EMPTY);
        *record = Record::EMPTY;
        record.antenna = self.antenna;
        // The receiver that sent the epoch runs, and tells of no fault.
        record.self_test = SelfTest::PASS;
        record.derived.raim = self.raim;
        record.satellites = epoch.satellites;
        if let Some(solution) = epoch.solution.filter(|solution| solution.fix != Fix::None) {
            record.fix = solution.fix;
            record.position = solution.position;
            record.velocity = Some(solution.velocity);
            record.dops = epoch.dops;
            record.derived.ecef = epoch.ecef.filter(|_| solution.position.is_some());
            record.time = epoch
                .time
                .and_then(|time| time.seconds(self.reference))
                .map(|seconds| GpsTime {
                    seconds,
                    subsecond: Nanoseconds(0),
                });
        }
        let now = record.time.map(|time| time.seconds);
        let count = epoch
            .leap
            .and_then(|leap| leap.count)
            .or_else(|| epoch.time.and_then(|time| time.leap_seconds));
        self.note(now, count, epoch.leap);
        (record.utc_offset, record.leap) = match (self.announced, now) {
            (Some(Announced { event, before }), Some(now)) => {
                (Some(event.offset(now, before)), Some(event))
            }
            _ => (count, None),
        };
    }

    /// Keeps what an epoch's `NAV-TIMELS` says of a leap second to
    /// come, `now` being the epoch's second and `count` GPS less UTC in
    /// it.  One announced before and not yet come is dropped when the
    /// receiver no longer announces it.
    fn note(
        &mut self,
        now: Option<GpsSeconds>,
        count: Option<Seconds<i8>>,
        leap: Option<NavTimeLs>,
    ) {
        let (Some(now), Some(leap)) = (now, leap) else {
            return;
        };
        let notice = leap.notice.filter(LeapNotice::pending);
        let withdrawn = self
            .announced
            .is_some_and(|announced| announced.event.pending(now));
        match (notice, count) {
            (Some(notice), Some(before)) => {
                // One announced already stands, with the offset it was
                // announced under: a receiver that steps its count
                // before the leap would otherwise move it a second.
                let stands = withdrawn
                    && self
                        .announced
                        .is_some_and(|announced| announced.event.change == notice.change);
                if let Some(event) = notice.event(now, before).filter(|_| !stands) {
                    self.announced = Some(Announced { event, before });
                }
            }
            (None, _) if withdrawn => self.announced = None,
            _ => {}
        }
    }
}

impl<const N: usize> Assemble for Assembler<N> {
    /// `record` is written only by the byte that ends an epoch, and
    /// then in full.
    fn push(&mut self, byte: u8, record: &mut Record) -> bool {
        match self.deframer.push(byte) {
            None => false,
            Some(Err(_)) => {
                self.errors = self.errors.saturating_add(1);
                false
            }
            Some(Ok(frame)) => {
                Message::parse(frame).is_some_and(|message| self.assembly.take(&message, record))
            }
        }
    }

    fn reset(&mut self) {
        *self = Self::resolving(self.assembly.reference);
    }
}
