//! Records of a stream of UBX bytes ("From messages to a record" in
//! the crate's documentation).

use core::mem;

use smartclock_gps::engine::settings::Setting;
use smartclock_gps::engine::settings::SettingSet;
use smartclock_gps::engine::settings::Settings;
use smartclock_gps::record::Antenna;
use smartclock_gps::record::Dops;
use smartclock_gps::record::Ecef;
use smartclock_gps::record::Fix;
use smartclock_gps::record::GpsTime;
use smartclock_gps::record::Raim;
use smartclock_gps::record::ReceiverMode;
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
use smartclock_gps::time::SECONDS_PER_WEEK;
use smartclock_gps::units::Nanoseconds;
use smartclock_gps::units::Seconds;

use crate::almanac::Gathered;
use crate::frame::Deframer;
use crate::message::Message;
use crate::nav::LeapNotice;
use crate::nav::NavPvt;
use crate::nav::NavTimeGps;
use crate::nav::NavTimeLs;
use crate::record::LeapNoticeRecord;
use crate::record::MonHwRecord;
use crate::record::NavDopRecord;
use crate::record::NavPosEcefRecord;
use crate::record::NavPvtRecord;
use crate::record::NavSatRecord;
use crate::record::NavTimeGpsRecord;
use crate::record::leap_change;
use crate::record::time_raim;
use crate::record::utc_offset;
use crate::subframe::Subframe;
use crate::units::Milliseconds;

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
    /// The sky its `NAV-SAT` gave.
    satellites: Option<Satellites>,
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
        satellites: None,
    };
}

/// How long after its `NAV-SAT` a sky is carried into an epoch that has
/// a fix and lost its own ("From messages to a record" in the crate's
/// documentation): three epochs of one a second.  A `NAV-SAT` the
/// receiver or gpsd now and then drops costs one epoch's sky, and the
/// bound rides
/// out a short run of them, over which a GPS satellite moves less than
/// a tenth of a degree; a longer loss is a stream that no longer
/// carries the sky, and the record says so.
const SKY_CARRIED: Milliseconds<u32> = Milliseconds(3_000);

/// The milliseconds of a week, at which `iTOW` wraps.
const MILLISECONDS_PER_WEEK: u32 = SECONDS_PER_WEEK * 1_000;

/// The sky an epoch's `NAV-SAT` gave, kept to carry forward.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
struct Sky {
    /// Its epoch's `iTOW`.
    time_of_week: Milliseconds<u32>,
    /// Its satellites.
    satellites: Satellites,
}

impl Sky {
    /// Whether it may stand for the sky of the epoch at `time_of_week`,
    /// a later one no more than [`SKY_CARRIED`] on, across the week's
    /// end too.
    const fn carries_to(&self, time_of_week: Milliseconds<u32>) -> bool {
        let age =
            (time_of_week.0 + MILLISECONDS_PER_WEEK - self.time_of_week.0) % MILLISECONDS_PER_WEEK;
        age > 0 && age <= SKY_CARRIED.0
    }
}

/// A leap second an [`Assembler`] was told of, with GPS less UTC until
/// it.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
struct Announced {
    event: LeapEvent,
    before: Seconds<i8>,
}

/// What an [`Assembler`] fills of a record, and the engine's settings
/// it acts on ([`act`]): the mask angle and the satellites to ignore,
/// which it applies to its own records.  It reads a receiver and sends
/// it nothing, so the rest of those the images set
/// ([`IMAGE_HOLDS`](smartclock_gps::record::field::IMAGE_HOLDS)), each
/// of which would be a configuration sent, are excused it by
/// `docs/firmware/gps-setting-exceptions.tsv`.  It makes no pulse: the
/// receiver makes its own, which the holder shifts by the pulse settings.
/// Of Time RAIM it fills the ability alone: the solution it gives is
/// always unknown, and `docs/firmware/gps-field-exceptions.tsv` excuses
/// it the solution.
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
        RecordField::Almanac,
        RecordField::Antenna,
        RecordField::SelfTest,
        RecordField::Ecef,
        RecordField::RaimAbility,
        RecordField::ReceiverMode,
    ]),
    acts: SettingSet::of(&[Setting::MaskAngle, Setting::IgnoreList]),
    makes_pulse: false,
};

/// Acts on `settings` in a record an [`Assembler`] made, as a receiver
/// set so would have: its fix leaves out the satellites below the mask
/// angle and those it is told to ignore ([`Settings::leave_out`]).  The
/// receiver itself is not configured; its fix and DOPs are its own.
pub fn act(record: &mut Record, settings: &Settings) {
    settings.leave_out(record);
}

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
    /// The receiver's mode, as the last `NAV-PVT` that told it gave
    /// it.
    mode: ReceiverMode,
    /// The sky the last `NAV-SAT` gave.
    sky: Option<Sky>,
    /// The epochs with a fix and no `NAV-SAT` of their own since the
    /// first `NAV-SAT`.
    lost_skies: u32,
    /// The leap second last announced.
    announced: Option<Announced>,
    /// The almanac's pages, as `RXM-SFRBX` has given them.
    almanac: Gathered,
    /// The week of the last epoch with a time, to resolve the
    /// almanac's.
    week: Option<GpsWeek>,
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
                mode: ReceiverMode::Unknown,
                sky: None,
                lost_skies: 0,
                announced: None,
                almanac: Gathered::EMPTY,
                week: None,
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

    /// How many epochs with a fix came without a `NAV-SAT` of their own
    /// since the stream's first, whether or not the sky was carried
    /// into them: a `NAV-SAT` the receiver did not send or the stream
    /// lost whole, which [`errors`](Self::errors) does not see.  It
    /// stops at the count's end.
    #[must_use]
    pub const fn lost_skies(&self) -> u32 {
        self.assembly.lost_skies
    }
}

impl Assembly {
    /// Takes a message, and returns whether it ended an epoch, whose
    /// record is then in `record`.
    fn take(&mut self, message: &Message<'_>, record: &mut Record) -> bool {
        let Some(time_of_week) = message.epoch() else {
            match message {
                Message::MonHw(hardware) => self.antenna = hardware.antenna(),
                Message::TimTp(pulse) => self.raim = time_raim(pulse),
                Message::RxmSfrbx(subframe) => {
                    if let Some(subframe) = subframe.gps_words().as_ref().and_then(Subframe::decode)
                    {
                        self.almanac.take(&subframe);
                    }
                }
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
            Message::NavPosEcef(ecef) => self.epoch.ecef = Some(ecef.ecef()),
            Message::NavDop(dop) => self.epoch.dops = Some(dop.dops()),
            Message::NavPvt(solution) => self.epoch.solution = Some(*solution),
            Message::NavTimeGps(time) => self.epoch.time = Some(*time),
            Message::NavTimeLs(leap) => self.epoch.leap = Some(*leap),
            Message::NavSat(sat) => self.epoch.satellites = Some(sat.sky()),
            Message::NavVelEcef(_)
            | Message::NavClock(_)
            | Message::NavEoe(_)
            | Message::RxmSfrbx(_)
            | Message::RxmRawx(_)
            | Message::AckNak(_)
            | Message::AckAck(_)
            | Message::MonHw(_)
            | Message::TimTp(_) => {}
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
        if let Some(mode) = epoch.solution.and_then(|solution| solution.receiver_mode()) {
            self.mode = mode;
        }
        record.receiver_mode = self.mode;
        let solution = epoch
            .solution
            .filter(|solution| solution.fix() != Fix::None);
        record.satellites = self.sky(&epoch, solution.is_some());
        if let Some(solution) = solution {
            record.fix = solution.fix();
            record.position = solution.position();
            record.velocity = Some(solution.velocity());
            // In position hold the receiver solves for no position, and
            // gives its DOPs as placeholders ("Position hold" in the
            // crate's documentation).
            record.dops = epoch.dops.filter(|_| solution.fix() != Fix::TimeOnly);
            record.derived.ecef = epoch.ecef.filter(|_| record.position.is_some());
            record.time = epoch
                .time
                .and_then(|time| time.seconds(self.reference))
                .map(|seconds| GpsTime {
                    seconds,
                    subsecond: Nanoseconds(0),
                });
        }
        let now = record.time.map(|time| time.seconds);
        self.week = now.and_then(GpsSeconds::week).or(self.week);
        record.almanac = self
            .week
            .or(self.reference)
            .and_then(|week| self.almanac.almanac(week));
        let count = epoch
            .leap
            .and_then(|leap| leap.count())
            .or_else(|| epoch.time.and_then(|time| time.leap_seconds()))
            .map(utc_offset);
        self.note(now, count, epoch.leap);
        (record.utc_offset, record.leap) = match (self.announced, now) {
            (Some(Announced { event, before }), Some(now)) => {
                (Some(event.offset(now, before)), Some(event))
            }
            _ => (count, None),
        };
    }

    /// The sky of `epoch`, `fixed` if it has a fix: its own `NAV-SAT`'s,
    /// which is kept; or, with a fix, the last one's if it is no more
    /// than [`SKY_CARRIED`] older; or none.
    fn sky(&mut self, epoch: &Epoch, fixed: bool) -> Satellites {
        // An epoch with a `NAV-SAT` has its `iTOW`.
        let Some(time_of_week) = epoch.time_of_week else {
            return Satellites::EMPTY;
        };
        if let Some(satellites) = epoch.satellites {
            self.sky = Some(Sky {
                time_of_week,
                satellites,
            });
            return satellites;
        }
        if fixed && self.sky.is_some() {
            self.lost_skies = self.lost_skies.saturating_add(1);
        }
        self.sky
            .filter(|sky| fixed && sky.carries_to(time_of_week))
            .map_or(Satellites::EMPTY, |sky| sky.satellites)
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
        let notice = leap.notice().filter(LeapNotice::pending);
        let withdrawn = self
            .announced
            .is_some_and(|announced| announced.event.pending(now));
        match (notice, count) {
            (Some(notice), Some(before)) => {
                // One announced already stands, with the offset it was
                // announced under: a receiver that steps its count
                // before the leap would otherwise move it a second.
                let stands = withdrawn
                    && self.announced.is_some_and(|announced| {
                        announced.event.change == leap_change(notice.change)
                    });
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
