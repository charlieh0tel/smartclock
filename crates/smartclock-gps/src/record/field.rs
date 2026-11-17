//! What a [`Record`] is made of, named, so that whoever fills one can
//! say which parts it fills and have that checked.
//!
//! A [`RecordField`] is one thing a provider either supplies or does not.
//! A provider declares the [`FieldSet`] it fills and the
//! [`SettingSet`] of the engine's settings it acts on, as a [`Declaration`],
//! and its tests hold the declaration to two things:
//!
//! - to the receivers: [`Declaration::shortfall`] is what the images use
//!   ([`IMAGE_USES`]) that it does not fill and that
//!   `docs/firmware/gps-field-exceptions.tsv` does not excuse it;
//! - to itself: [`changed`] says which fields a fill made differ from
//!   the record before it, [`Declaration::undeclared`] which of those it did
//!   not declare, and [`Declaration::unshown`] which it declared that no
//!   fill has been seen to change.  A field may rightly be left at the
//!   value an empty record has, so the fills compared must be ones made
//!   to give each field another.
//!
//! [`IMAGE_USES`] is not written by hand: `tools/firmware/gps_fields.py`
//! makes it from the field table, `docs/firmware/gps-fields.tsv`, whose
//! rows name these fields by [`RecordField::path`], and
//! `make check-gps-fields` fails when the two differ.
//!
//! A provider declares as well the engine's settings it acts on
//! ([`Declaration::acts`]): a command whose effect is not on the bytes the
//! engine builds leaves its value in the engine's
//! [`Settings`](crate::engine::settings::Settings) for the engine's
//! holder to apply.  [`IMAGE_SETS`], made the same way, is those the
//! images set, by any path (`docs/firmware/gps.md`, "Every path to a
//! setting"), and [`IMAGE_HOLDS`] those of them a holder acts on, the
//! rest being the engine's.  A provider's test holds that it acts on
//! each it is held to ([`Declaration::held`]), all of [`IMAGE_HOLDS`]
//! less, for one that makes no pulse, the [`PULSE_SETTINGS`], which the
//! pulse's maker acts on, or is excused it by a row of
//! `docs/firmware/gps-setting-exceptions.tsv`, and is excused nothing
//! else: [`Declaration::unacted`] and [`Declaration::needless`] are both
//! empty.  A provider may act on more than the images set; only a row
//! can be needless.

use crate::engine::settings::Setting;
use crate::engine::settings::SettingSet;
use crate::record::Fault;
use crate::record::FaultSet;
use crate::record::RaimSolution;
use crate::record::Record;
use crate::record::Satellite;
use crate::record::SelfTest;
use crate::record::Timing;
use crate::units::Degrees;
use crate::units::Nanoseconds;
use crate::units::Prn;
use crate::units::PrnSet;
use core::fmt::Debug;
use core::fmt::Formatter;
use core::fmt::Result as FmtResult;

/// One part of a [`Record`] a provider supplies or does not.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
#[non_exhaustive]
pub enum RecordField {
    /// The second of [`Record::time`].
    Time,
    /// How far into that second.
    Subsecond,
    /// [`Record::utc_offset`].
    UtcOffset,
    /// [`Record::leap`].
    Leap,
    /// [`Record::fix`].
    Fix,
    /// [`Record::differential`].
    Differential,
    /// The latitude of [`Record::position`].
    Latitude,
    /// Its longitude.
    Longitude,
    /// Its height above the ellipsoid.
    EllipsoidHeight,
    /// Its height above mean sea level.
    MeanSeaLevelHeight,
    /// The speed of [`Record::velocity`].
    Speed,
    /// Its heading.
    Heading,
    /// The GDOP of [`Record::dops`].
    GeometricDop,
    /// Its PDOP.
    PositionDop,
    /// Its HDOP.
    HorizontalDop,
    /// Its VDOP.
    VerticalDop,
    /// Its TDOP.
    TimeDop,
    /// The sawtooth of [`Record::timing`].
    Sawtooth,
    /// Its accuracy estimate.
    Sigma,
    /// Which satellites are in view: the IDs of
    /// [`Record::satellites`].
    Satellites,
    /// Each one's elevation.
    SatelliteElevation,
    /// Each one's azimuth.
    SatelliteAzimuth,
    /// Each one's signal.
    SatelliteSignal,
    /// How far a channel has got with each.
    SatelliteTracking,
    /// Whether the fix uses each.
    SatelliteUsed,
    /// Each one's health.
    SatelliteHealth,
    /// Whether each one's ephemeris is held.
    SatelliteEphemeris,
    /// Each one's Doppler shift.
    SatelliteDoppler,
    /// Each one's time error.
    SatelliteTimeError,
    /// [`Record::almanac`].
    Almanac,
    /// [`Record::antenna`].
    Antenna,
    /// [`Record::self_test`].
    SelfTest,
    /// The position in Earth-centered coordinates.
    Ecef,
    /// The DOPs of combinations of satellites.
    Xdops,
    /// The pseudorange corrections put out.
    Corrections,
    /// Time RAIM's solution status.
    RaimSolution,
    /// What Time RAIM can do.
    RaimAbility,
    /// The satellites Time RAIM isolated.
    RaimIsolated,
    /// [`Record::differential_age`].
    DifferentialAge,
    /// Each satellite's flags.
    SatelliteFlags,
    /// Each one's user range accuracy.
    SatelliteUra,
    /// The magnetic variation.
    MagneticVariation,
    /// The velocity along each axis.
    VelocityAxes,
    /// The inside of the solution.
    Solver,
}

impl RecordField {
    /// Every field, in the order of their bits in a [`FieldSet`].
    pub const ALL: [Self; 44] = [
        Self::Time,
        Self::Subsecond,
        Self::UtcOffset,
        Self::Leap,
        Self::Fix,
        Self::Differential,
        Self::Latitude,
        Self::Longitude,
        Self::EllipsoidHeight,
        Self::MeanSeaLevelHeight,
        Self::Speed,
        Self::Heading,
        Self::GeometricDop,
        Self::PositionDop,
        Self::HorizontalDop,
        Self::VerticalDop,
        Self::TimeDop,
        Self::Sawtooth,
        Self::Sigma,
        Self::Satellites,
        Self::SatelliteElevation,
        Self::SatelliteAzimuth,
        Self::SatelliteSignal,
        Self::SatelliteTracking,
        Self::SatelliteUsed,
        Self::SatelliteHealth,
        Self::SatelliteEphemeris,
        Self::SatelliteDoppler,
        Self::SatelliteTimeError,
        Self::Almanac,
        Self::Antenna,
        Self::SelfTest,
        Self::Ecef,
        Self::Xdops,
        Self::Corrections,
        Self::RaimSolution,
        Self::RaimAbility,
        Self::RaimIsolated,
        Self::DifferentialAge,
        Self::SatelliteFlags,
        Self::SatelliteUra,
        Self::MagneticVariation,
        Self::VelocityAxes,
        Self::Solver,
    ];

    /// Where it is in a [`Record`], as the field table's engine column
    /// names it after `record.`: the struct fields that lead to it.  A
    /// path of the table that goes on past one of these is that field.
    #[must_use]
    pub const fn path(self) -> &'static str {
        match self {
            Self::Time => "time.seconds",
            Self::Subsecond => "time.subsecond",
            Self::UtcOffset => "utc_offset",
            Self::Leap => "leap",
            Self::Fix => "fix",
            Self::Differential => "differential",
            Self::Latitude => "position.latitude",
            Self::Longitude => "position.longitude",
            Self::EllipsoidHeight => "position.ellipsoid",
            Self::MeanSeaLevelHeight => "position.mean_sea_level",
            Self::Speed => "velocity.speed",
            Self::Heading => "velocity.heading",
            Self::GeometricDop => "dops.geometric",
            Self::PositionDop => "dops.position",
            Self::HorizontalDop => "dops.horizontal",
            Self::VerticalDop => "dops.vertical",
            Self::TimeDop => "dops.time",
            Self::Sawtooth => "timing.sawtooth",
            Self::Sigma => "timing.sigma",
            Self::Satellites => "satellites",
            Self::SatelliteElevation => "satellites.elevation",
            Self::SatelliteAzimuth => "satellites.azimuth",
            Self::SatelliteSignal => "satellites.signal",
            Self::SatelliteTracking => "satellites.tracking",
            Self::SatelliteUsed => "satellites.used",
            Self::SatelliteHealth => "satellites.health",
            Self::SatelliteEphemeris => "satellites.ephemeris",
            Self::SatelliteDoppler => "satellites.doppler",
            Self::SatelliteTimeError => "satellites.time_error",
            Self::Almanac => "almanac",
            Self::Antenna => "antenna",
            Self::SelfTest => "self_test",
            Self::Ecef => "derived.ecef",
            Self::Xdops => "derived.xdops",
            Self::Corrections => "derived.corrections",
            Self::RaimSolution => "derived.raim.solution",
            Self::RaimAbility => "derived.raim.ability",
            Self::RaimIsolated => "derived.raim.isolated",
            Self::DifferentialAge => "differential_age",
            Self::SatelliteFlags => "satellites.flags",
            Self::SatelliteUra => "satellites.ura",
            Self::MagneticVariation => "derived.magnetic_variation",
            Self::VelocityAxes => "derived.velocity",
            Self::Solver => "derived.solver",
        }
    }

    /// The field of that path.
    #[must_use]
    pub fn of_path(path: &str) -> Option<Self> {
        Self::ALL.into_iter().find(|field| field.path() == path)
    }

    /// Its bit in a [`FieldSet`].
    const fn bit(self) -> u64 {
        1 << self as u64
    }

    /// Whether `one` and `other` differ in it.  A satellite's field
    /// differs when some satellite of `one` has it otherwise than the
    /// same satellite of `other` does, or, `other` not having that
    /// satellite, than one just come into view does
    /// ([`Satellite::new`]).  The part of a second, the timing's two
    /// values and the satellites isolated are taken as zero or none
    /// where what holds them is absent, so that giving the second, one
    /// of the timing's values or a RAIM status is not giving them.  A
    /// RAIM solution not known is taken as none, so that giving a RAIM
    /// status whose solution is the manual's value for not known is not
    /// giving the solution.
    #[must_use]
    pub fn differs(self, one: &Record, other: &Record) -> bool {
        match self {
            Self::Time => one.time.map(|time| time.seconds) != other.time.map(|time| time.seconds),
            Self::Subsecond => {
                let subsecond =
                    |record: &Record| record.time.map_or(Nanoseconds(0), |time| time.subsecond);
                subsecond(one) != subsecond(other)
            }
            Self::UtcOffset => one.utc_offset != other.utc_offset,
            Self::Leap => one.leap != other.leap,
            Self::Fix => one.fix != other.fix,
            Self::Differential => one.differential != other.differential,
            Self::Latitude => {
                one.position.map(|position| position.latitude)
                    != other.position.map(|position| position.latitude)
            }
            Self::Longitude => {
                one.position.map(|position| position.longitude)
                    != other.position.map(|position| position.longitude)
            }
            Self::EllipsoidHeight => {
                one.position.map(|position| position.ellipsoid)
                    != other.position.map(|position| position.ellipsoid)
            }
            Self::MeanSeaLevelHeight => {
                one.position.map(|position| position.mean_sea_level)
                    != other.position.map(|position| position.mean_sea_level)
            }
            Self::Speed => {
                one.velocity.map(|velocity| velocity.speed)
                    != other.velocity.map(|velocity| velocity.speed)
            }
            Self::Heading => {
                one.velocity.map(|velocity| velocity.heading)
                    != other.velocity.map(|velocity| velocity.heading)
            }
            Self::GeometricDop => {
                one.dops.map(|dops| dops.geometric) != other.dops.map(|dops| dops.geometric)
            }
            Self::PositionDop => {
                one.dops.map(|dops| dops.position) != other.dops.map(|dops| dops.position)
            }
            Self::HorizontalDop => {
                one.dops.map(|dops| dops.horizontal) != other.dops.map(|dops| dops.horizontal)
            }
            Self::VerticalDop => {
                one.dops.map(|dops| dops.vertical) != other.dops.map(|dops| dops.vertical)
            }
            Self::TimeDop => one.dops.map(|dops| dops.time) != other.dops.map(|dops| dops.time),
            Self::Sawtooth => {
                one.timing.unwrap_or(Timing::ZERO).sawtooth
                    != other.timing.unwrap_or(Timing::ZERO).sawtooth
            }
            Self::Sigma => {
                one.timing.unwrap_or(Timing::ZERO).sigma
                    != other.timing.unwrap_or(Timing::ZERO).sigma
            }
            Self::Satellites => !one
                .satellites
                .iter()
                .map(|satellite| satellite.prn)
                .eq(other.satellites.iter().map(|satellite| satellite.prn)),
            Self::SatelliteElevation => {
                satellites_differ(one, other, |satellite| satellite.elevation)
            }
            Self::SatelliteAzimuth => satellites_differ(one, other, |satellite| satellite.azimuth),
            Self::SatelliteSignal => satellites_differ(one, other, |satellite| satellite.signal),
            Self::SatelliteTracking => {
                satellites_differ(one, other, |satellite| satellite.tracking)
            }
            Self::SatelliteUsed => satellites_differ(one, other, |satellite| satellite.used),
            Self::SatelliteHealth => satellites_differ(one, other, |satellite| satellite.health),
            Self::SatelliteEphemeris => {
                satellites_differ(one, other, |satellite| satellite.ephemeris)
            }
            Self::SatelliteDoppler => satellites_differ(one, other, |satellite| satellite.doppler),
            Self::SatelliteTimeError => {
                satellites_differ(one, other, |satellite| satellite.time_error)
            }
            Self::Almanac => one.almanac != other.almanac,
            Self::Antenna => one.antenna != other.antenna,
            Self::SelfTest => one.self_test != other.self_test,
            Self::Ecef => one.derived.ecef != other.derived.ecef,
            Self::Xdops => one.derived.xdops != other.derived.xdops,
            Self::Corrections => one.derived.corrections != other.derived.corrections,
            Self::RaimSolution => {
                let solution = |record: &Record| {
                    record
                        .derived
                        .raim
                        .map(|raim| raim.solution)
                        .filter(|&solution| solution != RaimSolution::Unknown)
                };
                solution(one) != solution(other)
            }
            Self::RaimAbility => {
                one.derived.raim.map(|raim| raim.ability)
                    != other.derived.raim.map(|raim| raim.ability)
            }
            Self::RaimIsolated => {
                let isolated = |record: &Record| {
                    record
                        .derived
                        .raim
                        .map_or(PrnSet::EMPTY, |raim| raim.isolated)
                };
                isolated(one) != isolated(other)
            }
            Self::DifferentialAge => one.differential_age != other.differential_age,
            Self::SatelliteFlags => satellites_differ(one, other, |satellite| satellite.flags),
            Self::SatelliteUra => satellites_differ(one, other, |satellite| satellite.ura),
            Self::MagneticVariation => {
                one.derived.magnetic_variation != other.derived.magnetic_variation
            }
            Self::VelocityAxes => one.derived.velocity != other.derived.velocity,
            Self::Solver => one.derived.solver != other.derived.solver,
        }
    }
}

/// Whether some satellite of `one` has what `part` takes from it
/// otherwise than `other`'s same satellite, or with none there, than
/// one just come into view.
fn satellites_differ<T: PartialEq>(
    one: &Record,
    other: &Record,
    part: impl Fn(&Satellite) -> T,
) -> bool {
    let new = |prn: Prn| Satellite::new(prn, Degrees(0), Degrees(0));
    one.satellites.iter().any(|satellite| {
        let against = other
            .satellites
            .get(satellite.prn)
            .copied()
            .unwrap_or_else(|| new(satellite.prn));
        part(satellite) != part(&against)
    })
}

/// A set of [`RecordField`]s.
#[derive(Clone, Copy, PartialEq, Eq, Hash, Default)]
pub struct FieldSet(u64);

impl FieldSet {
    /// No field.
    pub const EMPTY: Self = Self(0);

    /// The fields of `fields`.
    #[must_use]
    pub const fn of(fields: &[RecordField]) -> Self {
        let mut set = Self::EMPTY;
        let mut index = 0;
        while index < fields.len() {
            set = set.with(fields[index]);
            index += 1;
        }
        set
    }

    /// The set with `field` in it.
    #[must_use]
    pub const fn with(self, field: RecordField) -> Self {
        Self(self.0 | field.bit())
    }

    /// The set with all of `other` in it too.
    #[must_use]
    pub const fn union(self, other: Self) -> Self {
        Self(self.0 | other.0)
    }

    /// The set with none of `other` in it.
    #[must_use]
    pub const fn without(self, other: Self) -> Self {
        Self(self.0 & !other.0)
    }

    /// Whether `field` is in it.
    #[must_use]
    pub const fn contains(self, field: RecordField) -> bool {
        self.0 & field.bit() != 0
    }

    /// Whether it holds none.
    #[must_use]
    pub const fn is_empty(self) -> bool {
        self.0 == 0
    }

    /// Its fields, in the order of [`RecordField::ALL`].
    pub fn iter(self) -> impl Iterator<Item = RecordField> {
        RecordField::ALL
            .into_iter()
            .filter(move |field| self.contains(*field))
    }
}

impl Debug for FieldSet {
    fn fmt(&self, formatter: &mut Formatter<'_>) -> FmtResult {
        formatter.debug_set().entries(self.iter()).finish()
    }
}

/// A record with nothing in it and a self-test that fails: what a
/// fill is seen against, so that one that writes a pass shows as every
/// other field it writes does.
pub const UNFILLED: Record = {
    let mut record = Record::EMPTY;
    record.self_test = SelfTest {
        hardware: FaultSet::EMPTY.with(Fault::Ram),
        channels: 0,
    };
    record
};

/// The fields in which `after` differs from `before`
/// ([`RecordField::differs`]).
#[must_use]
pub fn changed(before: &Record, after: &Record) -> FieldSet {
    RecordField::ALL
        .into_iter()
        .filter(|field| field.differs(after, before))
        .fold(FieldSet::EMPTY, FieldSet::with)
}

// gps_fields.py image uses
/// The fields a receiver's image uses, to control something or to
/// report it: those behind a row of the field table whose use is
/// control or reported.
pub const IMAGE_USES: FieldSet = FieldSet::of(&[
    RecordField::Time,
    RecordField::UtcOffset,
    RecordField::Leap,
    RecordField::Fix,
    RecordField::Latitude,
    RecordField::Longitude,
    RecordField::EllipsoidHeight,
    RecordField::MeanSeaLevelHeight,
    RecordField::PositionDop,
    RecordField::HorizontalDop,
    RecordField::Satellites,
    RecordField::SatelliteElevation,
    RecordField::SatelliteAzimuth,
    RecordField::SatelliteSignal,
    RecordField::SatelliteTracking,
    RecordField::SatelliteUsed,
    RecordField::SatelliteHealth,
    RecordField::Almanac,
    RecordField::Antenna,
    RecordField::SelfTest,
    RecordField::RaimSolution,
    RecordField::RaimAbility,
    RecordField::RaimIsolated,
]);

/// The settings a receiver's image sets: those a command row of the
/// field table holds or gives whose message a path of some image
/// sends to set (`docs/firmware/gps.md`, "Every path to a setting").
pub const IMAGE_SETS: SettingSet = SettingSet::of(&[
    Setting::MaskAngle,
    Setting::PositionHold,
    Setting::HeldPosition,
    Setting::AltitudeHold,
    Setting::TimeBase,
    Setting::CableDelay,
    Setting::PulseOffset,
    Setting::PulseMode,
    Setting::RaimAlgorithm,
    Setting::RaimAlarmLimit,
    Setting::Application,
    Setting::InitialPosition,
    Setting::SatelliteSelect,
    Setting::ManualSatellites,
    Setting::DopKind,
    Setting::XdopHysteresis,
    Setting::IgnoreList,
    Setting::AlmanacUpdate,
    Setting::AtmosphericCorrection,
    Setting::FixAlgorithm,
    Setting::EpochOffset,
    Setting::HdopThreshold,
    Setting::CorrectionThresholds,
    Setting::OutputFormat,
]);

/// Of [`IMAGE_SETS`], those a holder acts on: some such row holds
/// them.  A provider is held to each.
pub const IMAGE_HOLDS: SettingSet = SettingSet::of(&[
    Setting::MaskAngle,
    Setting::PositionHold,
    Setting::HeldPosition,
    Setting::AltitudeHold,
    Setting::TimeBase,
    Setting::CableDelay,
    Setting::PulseOffset,
    Setting::PulseMode,
    Setting::RaimAlgorithm,
    Setting::RaimAlarmLimit,
    Setting::Application,
    Setting::InitialPosition,
    Setting::SatelliteSelect,
    Setting::ManualSatellites,
    Setting::XdopHysteresis,
    Setting::AlmanacUpdate,
    Setting::AtmosphericCorrection,
    Setting::FixAlgorithm,
    Setting::EpochOffset,
    Setting::HdopThreshold,
    Setting::CorrectionThresholds,
    Setting::OutputFormat,
]);
// end gps_fields.py

/// The first line of the field exceptions table that is not a
/// comment: its columns.
const EXCEPTION_COLUMNS: &str = "provider\tfield\treason";
/// And that of the setting exceptions table.
const SETTING_EXCEPTION_COLUMNS: &str = "provider\tsetting\treason";
/// What a comment line of an exceptions table starts with.
const EXCEPTION_COMMENT: char = '#';
/// What parts the cells of a row of one.
const EXCEPTION_CELL: char = '\t';

/// The rows of an exceptions table, `exceptions` being its text and
/// `columns` its heading: each row's cells.
fn exception_rows<'a>(
    exceptions: &'a str,
    columns: &'a str,
) -> impl Iterator<Item = impl Iterator<Item = &'a str>> {
    exceptions
        .lines()
        .filter(move |line| !line.starts_with(EXCEPTION_COMMENT) && *line != columns)
        .map(|line| line.split(EXCEPTION_CELL))
}

/// What the rows of an exceptions table excuse the provider called
/// `name`: the second cell of each of its rows, the first being the
/// provider and the third the reason.
fn excused_names<'a>(
    name: &'a str,
    exceptions: &'a str,
    columns: &'a str,
) -> impl Iterator<Item = &'a str> {
    exception_rows(exceptions, columns).filter_map(move |mut cells| {
        let provider = cells.next()?;
        let excused = cells.next()?;
        (provider == name).then_some(excused)
    })
}

/// The fields the text of `docs/firmware/gps-field-exceptions.tsv`
/// excuses the provider called `name`: a row for each, of the provider, the
/// field's [`RecordField::path`] and the reason.
fn excused(name: &str, exceptions: &str) -> FieldSet {
    excused_names(name, exceptions, EXCEPTION_COLUMNS)
        .filter_map(RecordField::of_path)
        .fold(FieldSet::EMPTY, FieldSet::with)
}

/// The settings the text of `docs/firmware/gps-setting-exceptions.tsv`
/// excuses the provider called `name`: a row for each, of the provider, the
/// setting's [`Setting::name`] and the reason.
fn excused_settings(name: &str, exceptions: &str) -> SettingSet {
    excused_names(name, exceptions, SETTING_EXCEPTION_COLUMNS)
        .filter_map(Setting::of_name)
        .fold(SettingSet::EMPTY, SettingSet::with)
}

/// The settings that change only the 1 PPS: where it is put, `@@Az`'s
/// cable delay (VP p. 68) and `@@Ay`'s offset (VP p. 66), and when it
/// is put out at all, `@@Bn`'s 1PPS control mode, which turns the pulse
/// on, off, on while tracking or on while Time RAIM confirms the time
/// (VP p. 102).  Whoever makes the pulse acts on them: a provider of
/// records that makes none is not held to them ([`Declaration::makes_pulse`]).
pub const PULSE_SETTINGS: SettingSet = SettingSet::of(&[
    Setting::CableDelay,
    Setting::PulseOffset,
    Setting::PulseMode,
]);

/// What a provider of records declares of itself.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Declaration {
    /// What the exceptions tables call the provider.
    pub name: &'static str,
    /// The fields it fills from what it knows.
    pub fills: FieldSet,
    /// The engine's settings it acts on.
    pub acts: SettingSet,
    /// Whether it makes the 1 PPS the engine's holder puts out.  One
    /// that does is held to [`PULSE_SETTINGS`] as to the rest of
    /// [`IMAGE_HOLDS`]; one that does not is held to none of them, and
    /// a row excusing it one is needless: the pulse's maker in its setup
    /// acts on them.
    pub makes_pulse: bool,
}

impl Declaration {
    /// The fields the images use that it does not fill and `exceptions`,
    /// the text of `docs/firmware/gps-field-exceptions.tsv`, does not
    /// excuse it.  A provider's test fails unless this is empty.
    #[must_use]
    pub fn shortfall(&self, exceptions: &str) -> FieldSet {
        IMAGE_USES
            .without(self.fills)
            .without(excused(self.name, exceptions))
    }

    /// The fields a fill changed, `before` to `after`, that it does not
    /// declare.  A provider's test fails unless this is empty, `before`
    /// being [`UNFILLED`].
    #[must_use]
    pub fn undeclared(&self, before: &Record, after: &Record) -> FieldSet {
        changed(before, after).without(self.fills)
    }

    /// The fields it declares that are not among `shown`, those its
    /// fills have been seen to change from [`UNFILLED`].  A provider's
    /// test fails unless this is empty.
    #[must_use]
    pub const fn unshown(&self, shown: FieldSet) -> FieldSet {
        self.fills.without(shown)
    }

    /// The settings it is held to: [`IMAGE_HOLDS`], less
    /// [`PULSE_SETTINGS`] unless it makes the pulse.
    #[must_use]
    pub const fn held(&self) -> SettingSet {
        if self.makes_pulse {
            IMAGE_HOLDS
        } else {
            IMAGE_HOLDS.without(PULSE_SETTINGS)
        }
    }

    /// The settings it is held to ([`Declaration::held`]) that it does
    /// not act on and `exceptions`, the text of
    /// `docs/firmware/gps-setting-exceptions.tsv`, does not excuse it.
    /// A provider's test fails unless this is empty.
    #[must_use]
    pub fn unacted(&self, exceptions: &str) -> SettingSet {
        self.held()
            .without(self.acts)
            .without(excused_settings(self.name, exceptions))
    }

    /// The settings `exceptions` excuses it that need no excuse: one it
    /// is not held to, and one it acts on, a row gone stale.  A setting
    /// it acts on that it is not held to is not among them: a provider
    /// may act on more than the images set.  A provider's test fails
    /// unless this is empty.
    #[must_use]
    pub fn needless(&self, exceptions: &str) -> SettingSet {
        let excused = excused_settings(self.name, exceptions);
        excused
            .without(self.held())
            .union(self.acts.common(excused))
    }
}

#[cfg(test)]
mod tests {
    use super::Declaration;
    use super::FieldSet;
    use super::IMAGE_HOLDS;
    use super::IMAGE_SETS;
    use super::IMAGE_USES;
    use super::PULSE_SETTINGS;
    use super::RecordField;
    use super::changed;
    use super::excused;
    use crate::engine::settings::Setting;
    use crate::engine::settings::SettingSet;
    use crate::record::Fix;
    use crate::record::Health;
    use crate::record::Raim;
    use crate::record::RaimAbility;
    use crate::record::RaimSolution;
    use crate::record::Record;
    use crate::record::Satellite;
    use crate::units::DecibelHertz;
    use crate::units::Degrees;
    use crate::units::Prn;
    use std::collections::BTreeSet;
    use std::fs;
    use std::path::Path;
    use std::vec::Vec;

    /// The exceptions table, from the crate's directory.
    const EXCEPTIONS: &str = "../../docs/firmware/gps-field-exceptions.tsv";
    /// And the settings'.
    const SETTING_EXCEPTIONS: &str = "../../docs/firmware/gps-setting-exceptions.tsv";
    /// The cells of a row of an exceptions table: the provider, what it
    /// is excused and the reason.
    const EXCEPTION_CELLS: usize = 3;

    /// The first line of an exceptions table that is not a comment.
    fn heading(text: &str) -> Option<&str> {
        text.lines()
            .find(|line| !line.starts_with(super::EXCEPTION_COMMENT))
    }

    /// Each field has its own bit and its own path, and
    /// [`RecordField::ALL`] is in the order of the bits.
    #[test]
    fn fields_are_distinct() {
        let paths: BTreeSet<&str> = RecordField::ALL.iter().map(|field| field.path()).collect();
        assert_eq!(paths.len(), RecordField::ALL.len());
        for (index, field) in RecordField::ALL.into_iter().enumerate() {
            assert_eq!(field as usize, index);
            assert_eq!(RecordField::of_path(field.path()), Some(field));
        }
        assert_eq!(
            FieldSet::of(&RecordField::ALL).iter().count(),
            RecordField::ALL.len()
        );
    }

    /// A fill is seen field by field, a satellite's against the same
    /// satellite or a new one.
    #[test]
    fn a_fill_is_seen_by_field() {
        let prn = Prn::new(7).unwrap();
        let mut record = Record::EMPTY;
        assert!(changed(&Record::EMPTY, &record).is_empty());
        record.fix = Fix::ThreeD;
        record
            .satellites
            .push(Satellite::new(prn, Degrees(40), Degrees(0)));
        assert_eq!(
            changed(&Record::EMPTY, &record),
            FieldSet::of(&[
                RecordField::Fix,
                RecordField::Satellites,
                RecordField::SatelliteElevation
            ])
        );
        let before = record;
        let satellite = record.satellites.get_mut(prn).unwrap();
        satellite.signal = Some(DecibelHertz(40));
        satellite.health = Health::Healthy;
        assert_eq!(
            changed(&before, &record),
            FieldSet::of(&[RecordField::SatelliteSignal, RecordField::SatelliteHealth])
        );
    }

    /// A RAIM status whose solution is not known gives its ability and
    /// not its solution; one whose solution is known gives both.
    #[test]
    fn an_unknown_raim_solution_is_not_given() {
        let mut record = Record::EMPTY;
        record.derived.raim = Some(Raim::new(RaimSolution::Unknown, RaimAbility::Detection));
        assert_eq!(
            changed(&Record::EMPTY, &record),
            FieldSet::of(&[RecordField::RaimAbility])
        );
        let before = record;
        record.derived.raim = Some(Raim::new(RaimSolution::Ok, RaimAbility::Detection));
        assert_eq!(
            changed(&before, &record),
            FieldSet::of(&[RecordField::RaimSolution])
        );
        assert_eq!(
            changed(&Record::EMPTY, &record),
            FieldSet::of(&[RecordField::RaimSolution, RecordField::RaimAbility])
        );
    }

    /// A provider is held to what the images use, less what the table
    /// excuses it by name, and to what its fills change.
    #[test]
    fn a_provider_is_held_to_its_declaration() {
        let all = Declaration {
            name: "all",
            fills: FieldSet::of(&RecordField::ALL),
            acts: SettingSet::EMPTY,
            makes_pulse: false,
        };
        assert!(all.shortfall("").is_empty());
        let none = Declaration {
            name: "none",
            fills: FieldSet::EMPTY,
            acts: SettingSet::EMPTY,
            makes_pulse: false,
        };
        assert_eq!(none.shortfall(""), IMAGE_USES);
        assert!(IMAGE_USES.contains(RecordField::Fix));
        let table =
            "# a comment\nprovider\tfield\treason\nnone\tfix\tfor the test\nother\tleap\t-\n";
        assert_eq!(excused("none", table), FieldSet::of(&[RecordField::Fix]));
        assert_eq!(
            none.shortfall(table),
            IMAGE_USES.without(FieldSet::of(&[RecordField::Fix]))
        );

        let mut record = Record::EMPTY;
        record.fix = Fix::TwoD;
        let shown = changed(&Record::EMPTY, &record);
        assert_eq!(none.undeclared(&Record::EMPTY, &record), shown);
        assert!(all.undeclared(&Record::EMPTY, &record).is_empty());
        assert_eq!(all.unshown(shown), all.fills.without(shown));
    }

    /// A provider is held to the settings the images set: it acts on
    /// each or the table excuses it, and the table excuses it none for
    /// nothing.  It may act on one no image sets.
    #[test]
    fn a_provider_is_held_to_the_settings() {
        let hold = SettingSet::of(&[Setting::PositionHold]);
        let declaration = Declaration {
            name: "one",
            fills: FieldSet::EMPTY,
            acts: hold,
            makes_pulse: true,
        };
        assert_eq!(declaration.unacted(""), IMAGE_HOLDS.without(hold));
        assert!(declaration.needless("").is_empty());

        let rest = IMAGE_HOLDS
            .without(hold)
            .iter()
            .fold(std::string::String::new(), |table, setting| {
                table + "one\t" + setting.name() + "\tfor the test\n"
            });
        let table = std::string::String::from("# a comment\nprovider\tsetting\treason\n") + &rest;
        assert!(declaration.unacted(&table).is_empty());
        assert!(declaration.needless(&table).is_empty());
        // Another provider's rows are not this one's.
        let other = Declaration {
            name: "other",
            ..declaration
        };
        assert_eq!(other.unacted(&table), IMAGE_HOLDS.without(hold));

        // A row for a setting acted on, and one for a setting no image
        // sets, are both needless.
        let stale = table.clone() + "one\tposition_hold\tstale\n";
        assert_eq!(declaration.needless(&stale), hold);
        assert!(!IMAGE_HOLDS.contains(Setting::AltitudeHoldHeight));
        let unset = SettingSet::of(&[Setting::AltitudeHoldHeight]);
        let unset_excused = table.clone() + "one\taltitude_hold_height\t-\n";
        assert_eq!(declaration.needless(&unset_excused), unset);

        // Acting on a setting no image sets is not, with the rest
        // acted on or excused; acting on it and being excused it is.
        let keen = Declaration {
            acts: IMAGE_HOLDS.union(unset),
            ..declaration
        };
        assert!(keen.unacted("").is_empty());
        assert!(keen.needless("").is_empty());
        let also = Declaration {
            acts: hold.union(unset),
            ..declaration
        };
        assert!(also.unacted(&table).is_empty());
        assert!(also.needless(&table).is_empty());
        assert_eq!(also.needless(&unset_excused), unset);

        // The kind of DOP and the ignore list, which the images set, are
        // the engine's to act on: no provider is held to either, and one
        // may act on them all the same.
        assert!(IMAGE_SETS.contains(Setting::DopKind) && !IMAGE_HOLDS.contains(Setting::DopKind));
        assert!(
            IMAGE_SETS.contains(Setting::IgnoreList) && !IMAGE_HOLDS.contains(Setting::IgnoreList)
        );
        let tabler = Declaration {
            acts: hold.with(Setting::DopKind).with(Setting::IgnoreList),
            ..declaration
        };
        assert!(tabler.unacted(&table).is_empty());
        assert!(tabler.needless(&table).is_empty());
    }

    /// Whoever makes the pulse acts on the pulse settings, all of which
    /// the images set for a holder to act on: a provider that makes it
    /// is held to them, and one that does not is held to none of them,
    /// a row excusing it one being needless.
    #[test]
    fn the_pulse_settings_are_the_pulse_makers() {
        assert_eq!(IMAGE_HOLDS.common(PULSE_SETTINGS), PULSE_SETTINGS);
        let maker = Declaration {
            name: "maker",
            fills: FieldSet::EMPTY,
            acts: SettingSet::EMPTY,
            makes_pulse: true,
        };
        assert_eq!(maker.unacted("").common(PULSE_SETTINGS), PULSE_SETTINGS);
        let reader = Declaration {
            name: "reader",
            makes_pulse: false,
            ..maker
        };
        assert!(reader.unacted("").common(PULSE_SETTINGS).is_empty());
        assert_eq!(reader.unacted(""), IMAGE_HOLDS.without(PULSE_SETTINGS));
        let row = "reader\tcable_delay\tmakes no pulse\n";
        assert_eq!(reader.needless(row), SettingSet::of(&[Setting::CableDelay]));
        assert!(maker.needless("maker\tcable_delay\t-\n").is_empty());
    }

    /// The setting exceptions table as it is: every row names a
    /// setting the images set, once for its provider, and gives a reason.
    #[test]
    fn the_setting_exceptions_name_settings() {
        let text =
            fs::read_to_string(Path::new(env!("CARGO_MANIFEST_DIR")).join(SETTING_EXCEPTIONS))
                .expect("the setting exceptions table");
        assert_eq!(heading(&text), Some(super::SETTING_EXCEPTION_COLUMNS));
        let mut seen = BTreeSet::new();
        for cells in super::exception_rows(&text, super::SETTING_EXCEPTION_COLUMNS) {
            let cells: Vec<&str> = cells.collect();
            assert_eq!(cells.len(), EXCEPTION_CELLS, "{cells:?}");
            let setting = Setting::of_name(cells[1]);
            assert!(
                setting.is_some_and(|setting| IMAGE_HOLDS.contains(setting)),
                "{cells:?}"
            );
            assert!(!cells[2].is_empty(), "{cells:?}");
            assert!(seen.insert((cells[0], cells[1])), "twice: {cells:?}");
        }
    }

    /// The exceptions table as it is: every row names a field.
    #[test]
    fn the_exceptions_name_fields() {
        let text = fs::read_to_string(Path::new(env!("CARGO_MANIFEST_DIR")).join(EXCEPTIONS))
            .expect("the exceptions table");
        assert_eq!(heading(&text), Some(super::EXCEPTION_COLUMNS));
        for cells in super::exception_rows(&text, super::EXCEPTION_COLUMNS) {
            let cells: Vec<&str> = cells.collect();
            assert_eq!(cells.len(), EXCEPTION_CELLS, "{cells:?}");
            assert!(RecordField::of_path(cells[1]).is_some(), "{cells:?}");
        }
    }
}
