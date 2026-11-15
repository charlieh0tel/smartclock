//! What a [`Record`] is made of, named, so that whoever fills one can
//! say which parts it fills and have that checked.
//!
//! A [`RecordField`] is one thing a filler either supplies or does not.
//! A filler declares the [`FieldSet`] it fills and the
//! [`SettingSet`] of the engine's settings it acts on, as a [`Filler`],
//! and its tests hold the declaration to two things:
//!
//! - to the receivers: [`Filler::shortfall`] is what the images use
//!   ([`IMAGE_USES`]) that it does not fill and that
//!   `docs/firmware/gps-field-exceptions.tsv` does not excuse it;
//! - to itself: [`changed`] says which fields a fill made differ from
//!   the record before it, [`Filler::undeclared`] which of those it did
//!   not declare, and [`Filler::unshown`] which it declared that no
//!   fill has been seen to change.  A field may rightly be left at the
//!   value an empty record has, so the fills compared must be ones made
//!   to give each field another.
//!
//! [`IMAGE_USES`] is not written by hand: `tools/firmware/gps_fields.py`
//! makes it from the field table, `docs/firmware/gps-fields.tsv`, whose
//! rows name these fields by [`RecordField::path`], and
//! `make check-gps-fields` fails when the two differ.
//!
//! A filler declares as well the engine's settings it acts on
//! ([`Filler::acts`]): a command whose effect is not on the bytes the
//! engine builds leaves its value in the engine's
//! [`Settings`](crate::engine::settings::Settings) for the engine's
//! holder to apply.  [`IMAGE_SETS`], made the same way, is those the
//! images set, and [`Filler::unacted`] those of them a filler leaves
//! alone.

use crate::engine::settings::Setting;
use crate::engine::settings::SettingSet;
use crate::record::Fault;
use crate::record::FaultSet;
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

/// One part of a [`Record`] a filler supplies or does not.
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
    /// of the timing's values or a RAIM status is not giving them.
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
                one.derived.raim.map(|raim| raim.solution)
                    != other.derived.raim.map(|raim| raim.solution)
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
    RecordField::SatelliteHealth,
    RecordField::Antenna,
    RecordField::SelfTest,
    RecordField::RaimSolution,
    RecordField::RaimAbility,
]);

/// The settings a receiver's image sets that are a holder's to act
/// on: those a command row of the field table holds and some image
/// sets.
pub const IMAGE_SETS: SettingSet = SettingSet::of(&[
    Setting::PositionHold,
    Setting::HeldPosition,
    Setting::AltitudeHold,
    Setting::TimeBase,
    Setting::PulseOffset,
    Setting::PulseMode,
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

/// The first line of the exceptions table that is not a comment: its
/// columns.
const EXCEPTION_COLUMNS: &str = "filler\tfield\treason";
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

/// What the rows of an exceptions table excuse the filler called
/// `name`: the second cell of each of its rows, the first being the
/// filler and the third the reason.
fn excused_names<'a>(
    name: &'a str,
    exceptions: &'a str,
    columns: &'a str,
) -> impl Iterator<Item = &'a str> {
    exception_rows(exceptions, columns).filter_map(move |mut cells| {
        let filler = cells.next()?;
        let excused = cells.next()?;
        (filler == name).then_some(excused)
    })
}

/// The fields the text of `docs/firmware/gps-field-exceptions.tsv`
/// excuses the filler called `name`: a row for each, of the filler, the
/// field's [`RecordField::path`] and the reason.
fn excused(name: &str, exceptions: &str) -> FieldSet {
    excused_names(name, exceptions, EXCEPTION_COLUMNS)
        .filter_map(RecordField::of_path)
        .fold(FieldSet::EMPTY, FieldSet::with)
}

/// What fills records, as it declares itself.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Filler {
    /// What the exceptions table calls it.
    pub name: &'static str,
    /// The fields it fills from what it knows.
    pub fills: FieldSet,
    /// The engine's settings it acts on.
    pub acts: SettingSet,
}

impl Filler {
    /// The fields the images use that it does not fill and `exceptions`,
    /// the text of `docs/firmware/gps-field-exceptions.tsv`, does not
    /// excuse it.  A filler's test fails unless this is empty.
    #[must_use]
    pub fn shortfall(&self, exceptions: &str) -> FieldSet {
        IMAGE_USES
            .without(self.fills)
            .without(excused(self.name, exceptions))
    }

    /// The fields a fill changed, `before` to `after`, that it does not
    /// declare.  A filler's test fails unless this is empty, `before`
    /// being [`UNFILLED`].
    #[must_use]
    pub fn undeclared(&self, before: &Record, after: &Record) -> FieldSet {
        changed(before, after).without(self.fills)
    }

    /// The fields it declares that are not among `shown`, those its
    /// fills have been seen to change from [`UNFILLED`].  A filler's
    /// test fails unless this is empty.
    #[must_use]
    pub const fn unshown(&self, shown: FieldSet) -> FieldSet {
        self.fills.without(shown)
    }

    /// The settings the images set that it does not act on.  A
    /// filler's test says what this is, so that it changes only when
    /// someone means it to.
    #[must_use]
    pub const fn unacted(&self) -> SettingSet {
        IMAGE_SETS.without(self.acts)
    }
}

#[cfg(test)]
mod tests {
    use super::FieldSet;
    use super::Filler;
    use super::IMAGE_USES;
    use super::RecordField;
    use super::changed;
    use super::excused;
    use crate::engine::settings::SettingSet;
    use crate::record::Fix;
    use crate::record::Health;
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
    /// The cells of a row of an exceptions table: the filler, what it
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

    /// A filler is held to what the images use, less what the table
    /// excuses it by name, and to what its fills change.
    #[test]
    fn a_filler_is_held_to_its_declaration() {
        let all = Filler {
            name: "all",
            fills: FieldSet::of(&RecordField::ALL),
            acts: SettingSet::EMPTY,
        };
        assert!(all.shortfall("").is_empty());
        let none = Filler {
            name: "none",
            fills: FieldSet::EMPTY,
            acts: SettingSet::EMPTY,
        };
        assert_eq!(none.shortfall(""), IMAGE_USES);
        assert!(IMAGE_USES.contains(RecordField::Fix));
        let table = "# a comment\nfiller\tfield\treason\nnone\tfix\tfor the test\nother\tleap\t-\n";
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
