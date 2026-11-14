//! A model that stands in for a Motorola Oncore: it takes the commands
//! a `SmartClock` sends and a [`Record`] each second, and writes the
//! answers and the continuous messages the receiver expects.
//!
//! An [`Engine`] is one of the Oncores the receivers hold, chosen when
//! it is made by its [`Identity`]: a six-channel VP, an eight-channel
//! VP or a UT Plus.  It answers only what the receivers' firmware
//! sends, and fills in only what that firmware decodes.
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
//! on a second.  That clock starts at 12:00:00 on the day GPS time
//! starts, as an Oncore's does that was not kept powered (VP p. 5), so
//! a clock `@@Ab` sets behind UTC reads the day before.  A second in which the engine is to be silent is one
//! [`Engine::second`] is not called for.
//!
//! One answer is not sent at once: a self-test's, which comes two
//! seconds after its command.  [`Engine::deadline`] says when one is
//! due and [`Engine::poll`] sends it; [`Engine::second`] and
//! [`Engine::receive`] send what is due before anything else, so a
//! caller whose link can wait up to a second for it need not poll.
//!
//! What a record cannot say, the engine does not make up: with no
//! [`Dops`](crate::record::Dops) it sends zeros, and with no
//! [`Ecef`](crate::record::Ecef) position, or while it holds a position
//! of the host's, zeros for `@@Bk`'s.
//!
//! The pages cited are, as printed: VP, the VP Oncore's command
//! reference, `third_party/motorola/VPCommands.pdf`; UT, TRM0003
//! chapter 6, `third_party/motorola/oncore-ut-users-guide/ch6.pdf`;
//! M12+, `third_party/motorola/m12plus-users-guide.pdf`.

mod data;
mod framer;
pub mod identity;
mod settings;

use crate::engine::data::Data;
use crate::engine::framer::Framer;
use crate::engine::identity::Identity;
use crate::engine::identity::LINE_END;
use crate::engine::identity::Model;
use crate::engine::settings::HEIGHT_MEAN_SEA_LEVEL;
use crate::engine::settings::Place;
use crate::engine::settings::QUERY;
use crate::engine::settings::Settings;
use crate::engine::settings::is_setting;
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
use crate::oncore::CJ_RECEIVER_ID;
use crate::oncore::Channels;
use crate::oncore::EA_POSITION_STATUS;
use crate::oncore::EK_EXTENSION;
use crate::oncore::EN_TIME_RAIM;
use crate::oncore::FA_SELF_TEST;
use crate::oncore::GJ_LEAP_SECOND_PENDING;
use crate::oncore::MAX_RESPONSE;
use crate::oncore::MessageId;
use crate::oncore::SZ_POWER_ON_FAILURE;
use crate::oncore::encode;
use crate::record::Antenna;
use crate::record::Fix;
use crate::record::Health;
use crate::record::Record;
use crate::record::SATELLITES;
use crate::record::Satellite;
use crate::record::Tracking;
use crate::time::DateTime;
use crate::time::GpsSeconds;
use crate::time::LeapChange;
use crate::time::LeapEvent;
use crate::units::Milliseconds;
use crate::units::Prn;
use crate::units::Seconds;

/// Where an engine's frames go: the serial link to the receiver.
pub trait Output {
    /// Takes one whole frame, from its `@@` to its line feed.
    fn send(&mut self, frame: &[u8]);
}

/// How an engine departs from the Oncore its identity names.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[non_exhaustive]
pub struct Options {
    /// A six-channel engine becomes an eight-channel one at its first
    /// `@@Ea`, and answers the eight-channel messages from then on.
    pub eight_on_ea: bool,
    /// It takes no commands.
    pub deaf: bool,
    /// It fixes from power-up and sends its own position status at this
    /// rate unasked, as an engine whose saved configuration says so.
    /// Zero for an engine that waits to be told.
    pub status_rate: Seconds<u8>,
}

impl Options {
    /// The Oncore as it is.
    pub const DEFAULT: Self = Self {
        eight_on_ea: false,
        deaf: false,
        status_rate: Seconds(0),
    };
}

/// How long a self-test takes to answer.
const SELF_TEST: u64 = 2000;
/// The self-test answers an engine holds at once.
const DEFERRED: usize = 4;

/// The six-channel messages' channels, and the eight-channel ones'.
const SIX_CHANNELS: usize = 6;
const EIGHT_CHANNELS: usize = 8;

/// `@@Cg`'s modes (VP p. 118).
const MODE_IDLE: u8 = 0;
const MODE_FIX: u8 = 1;
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

/// A channel's status flag bits (VP p. 73; UT p. 6.20).
const CHANNEL_USED: u8 = 0x80;
const CHANNEL_USED_FOR_TIME: u8 = 0x02;
/// The receiver status bits (VP p. 73).
const STATUS_3D_FIX: u8 = 0x20;
const STATUS_2D_FIX: u8 = 0x10;
const STATUS_ACQUIRING_OR_HOLD: u8 = 0x08;
const STATUS_FEW_VISIBLE: u8 = 0x02;
/// Fewer visible than this is too few.
const FEW_VISIBLE: usize = 3;
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
/// `@@Bc`: the satellites its table is for, and the bytes of its nine
/// combinations (VP p. 77).
const XDOP_IN_VIEW: u8 = 6;
const XDOP_TABLE: usize = 9 * 8;
/// `@@Bd`'s almanac-valid flag, and the bytes of one almanac's status
/// (VP p. 79).
const ALMANAC_VALID: u8 = 1;
const ALMANAC_STATUS: usize = 8;
/// `@@Ce`: the bytes that follow the time, six channels of seven (VP
/// p. 115).
const CORRECTIONS: usize = 6 * 7;
/// `@@Bk`: the bytes between the DOPs and the position, and those after
/// the channels (VP pp. 96 to 98).
const EXTENSION_BEFORE_POSITION: usize = 10;
const EXTENSION_REST: usize = 24;
/// `@@Bj`'s statuses (VP p. 94).
const NO_LEAP_PENDING: u8 = 0;
const LEAP_ADDITION_PENDING: u8 = 1;
const LEAP_SUBTRACTION_PENDING: u8 = 2;

/// Time RAIM's setup (VP pp. 102 to 106): the algorithm's states, the
/// least alarm limit and the one at power-up, in hundreds of
/// nanoseconds, and the 1 PPS modes.
const TRAIM_OFF: u8 = 0;
const TRAIM_ON: u8 = 1;
const ALARM_LIMIT_LEAST: u16 = 3;
const ALARM_LIMIT_DEFAULT: u16 = 10;
const PPS_OFF: u8 = 0;
const PPS_ALWAYS: u8 = 1;
const PPS_TRACKING: u8 = 2;
const PPS_CONFIRMED: u8 = 3;
/// The 1 PPS rate a VP reports, a pulse a second.
const PPS_RATE: u32 = 1;
/// The bytes a UT Plus leaves zero where a VP gives the rate and the
/// next pulse's time (UT p. 6.95).
const NEXT_PULSE: usize = 3 + 4 + 3;
/// Time RAIM's solution statuses and what it can do (VP p. 106).
const TRAIM_SOLUTION_OK: u8 = 0;
const TRAIM_SOLUTION_UNKNOWN: u8 = 2;
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
/// depending upon the firmware" (`@@Ac`, VP p. 13); the model's is the
/// day GPS time starts, so that 12:00:00 is the same in GPS time and in
/// UTC with no offset known.
const POWER_UP: GpsSeconds = match GpsSeconds::from_utc(
    match DateTime::new(1980, 1, 6, 12, 0, 0) {
        Some(noon) => noon.to_unix(),
        None => panic!("not a date"),
    },
    Seconds(0),
) {
    Some(time) => time,
    None => panic!("out of the count"),
};

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

/// The satellites Time RAIM needs to detect a bad one and to isolate
/// it, for alarm limits from `from` up.
#[derive(Debug)]
struct TraimNeed {
    /// The least alarm limit this row is for.
    from: u16,
    /// To detect.
    detect: usize,
    /// To isolate.
    isolate: usize,
}

/// The needs by alarm limit, greatest first.
const TRAIM_NEEDS: [TraimNeed; 4] = [
    TraimNeed {
        from: 10,
        detect: 2,
        isolate: 3,
    },
    TraimNeed {
        from: 7,
        detect: 3,
        isolate: 3,
    },
    TraimNeed {
        from: 5,
        detect: 4,
        isolate: 4,
    },
    TraimNeed {
        from: 3,
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
    /// Off or on.
    algorithm: u8,
    /// The alarm limit, in hundreds of nanoseconds.
    alarm_limit: u16,
    /// When the 1 PPS is put out.
    pps_mode: u8,
}

impl TraimSetup {
    const DEFAULT: Self = Self {
        algorithm: TRAIM_OFF,
        alarm_limit: ALARM_LIMIT_DEFAULT,
        pps_mode: PPS_ALWAYS,
    };

    fn in_range(self) -> bool {
        matches!(self.algorithm, TRAIM_OFF | TRAIM_ON)
            && self.alarm_limit >= ALARM_LIMIT_LEAST
            && self.pps_mode <= PPS_CONFIRMED
    }
}

/// What commands have made of an engine, all of it put back by `@@Cf`.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
struct State {
    /// Whether it is fixing, not idle.
    fixing: bool,
    /// The settings.
    settings: Settings,
    /// The satellite assigned to each channel by hand, zero for none.
    assignments: [u8; EIGHT_CHANNELS],
    /// Each family's rates.
    rates: [Rates; FAMILIES.len()],
    /// Time RAIM's setup.
    traim: TraimSetup,
    /// The rate of `@@Ce`.
    corrections: Rate,
    /// Whether `@@Bo` is sent each time the offset changes.
    offset_updates: bool,
}

impl State {
    /// The state at power-up: a VP idle, a UT Plus fixing.
    const fn starting(model: Model) -> Self {
        Self {
            fixing: matches!(model, Model::UtPlus),
            settings: Settings::DEFAULT,
            assignments: [0; EIGHT_CHANNELS],
            rates: [Rates {
                status: Rate(0),
                extension: Rate(0),
                traim: Rate(0),
            }; FAMILIES.len()],
            traim: TraimSetup::DEFAULT,
            corrections: Rate(0),
            offset_updates: false,
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
    /// Which Oncore it is.
    identity: &'static Identity,
    /// How it departs from it.
    options: Options,
    /// The receiver channels it has now.
    channels: usize,
    /// Finds the commands in what it is sent.
    framer: Framer,
    /// What commands have made of it.
    state: State,
    /// The seconds it has been given since power-up.
    count: u32,
    /// The time it reports.
    clock: GpsSeconds,
    /// The last record's time, to tell a new one from the same again.
    heard: Option<GpsSeconds>,
    /// GPS less UTC as `@@Bo` gave it in the second before.
    offset: Option<u8>,
    /// The self-tests' answers waiting, oldest
    /// first.
    deferred: [Option<Deferred>; DEFERRED],
}

impl Engine {
    /// The Oncore `identity` names, just powered up.
    #[must_use]
    pub const fn new(identity: &'static Identity, options: Options) -> Self {
        let model = identity.model();
        let starting = State::starting(model);
        let state = if options.status_rate.0 == 0 {
            starting
        } else {
            let own = match model {
                Model::VpSix => SIX.index,
                Model::VpEight | Model::UtPlus => EIGHT.index,
            };
            let mut rates = starting.rates;
            rates[own].status = Rate(options.status_rate.0);
            State {
                fixing: true,
                rates,
                ..starting
            }
        };
        Self {
            identity,
            options,
            channels: model.channels(),
            framer: Framer::new(),
            state,
            count: 0,
            clock: POWER_UP,
            heard: None,
            offset: None,
            deferred: [None; DEFERRED],
        }
    }

    /// Which Oncore it is.
    #[must_use]
    pub const fn identity(&self) -> &'static Identity {
        self.identity
    }

    /// Whether it is fixing, not idle.
    #[must_use]
    pub const fn fixing(&self) -> bool {
        self.state.fixing
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
        self.clock = match told {
            Some(time) if told != self.heard => time,
            _ if self.count == 1 => self.clock,
            _ => self.clock + Seconds(1),
        };
        self.heard = told;

        let offset = self.wire_offset(record);
        let stepped = self.offset.is_some_and(|before| before != offset);
        self.offset = Some(offset);

        if self.state.fixing {
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
                self.corrections(out);
            }
        }
        if self.state.offset_updates && stepped {
            send(out, BO_UTC_OFFSET, Data::new().byte(offset));
        }
    }

    /// Takes bytes from the receiver, at `now` by the caller's clock,
    /// and answers each whole command among them.  `record` is the
    /// current second's.
    pub fn receive<O: Output + ?Sized>(
        &mut self,
        bytes: &[u8],
        record: &Record,
        now: Milliseconds<u64>,
        out: &mut O,
    ) {
        self.poll(now, out);
        if self.count == 0
            && let Some(time) = record.time
        {
            self.clock = time.seconds;
        }
        for byte in bytes {
            if let Some(command) = self.framer.push(*byte)
                && !self.options.deaf
            {
                self.answer(command.id, command.data(), record, now, out);
            }
        }
    }

    /// Whether this Oncore answers a command at all.
    fn answers(&self, id: MessageId) -> bool {
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

    fn answer<O: Output + ?Sized>(
        &mut self,
        id: MessageId,
        data: &[u8],
        record: &Record,
        now: Milliseconds<u64>,
        out: &mut O,
    ) {
        if !self.answers(id) {
            return;
        }
        if let Some((family, member)) = family_of(id) {
            self.answer_family(family, member, data, record, now, out);
            return;
        }
        let model = self.identity.model();
        if id == AA_TIME_OF_DAY {
            send(
                out,
                id,
                Data::new().bytes(&self.time_of_day(record, self.clock)),
            );
        } else if id == AC_DATE {
            send(out, id, Data::new().bytes(&self.date(record, self.clock)));
        } else if id == AB_GMT_CORRECTION {
            self.state.settings.set_or_query(id, data, model);
            // A correction behind UTC by minutes alone is answered as
            // its minutes negated, with no sign.
            match self.state.settings.get(id) {
                Some(&[sign, 0, minutes]) if sign != 0 => {
                    send(out, id, Data::new().bytes(&[0, 0, minutes.wrapping_neg()]));
                }
                Some(held) => send(out, id, Data::new().bytes(held)),
                None => {}
            }
        } else if id == AD_LATITUDE || id == AE_LONGITUDE {
            if data.iter().all(|byte| *byte == POSITION_QUERY) {
                let place = self.place(record);
                let value = if id == AD_LATITUDE {
                    place.latitude
                } else {
                    place.longitude
                };
                send(out, id, Data::new().signed_long(value));
            } else {
                send(out, id, Data::new().bytes(data));
            }
        } else if id == AF_HEIGHT {
            self.height(data, record, out);
        } else if id == AI_MANUAL_SATELLITE {
            self.assign(data, out);
        } else if id == AO_DATUM_SELECT {
            self.state.settings.set_or_query(id, data, model);
            send(
                out,
                AP_USER_DATUM,
                Data::new()
                    .byte(self.state.settings.datum())
                    .triple(WGS_84_AXIS)
                    .word(0)
                    .word(WGS_84_INVERSE_FLATTENING)
                    .long(WGS_84_INVERSE_FLATTENING_FRACTION)
                    .zeros(6),
            );
        } else if id == BB_VISIBLE_SATELLITES {
            Self::visibility(record, out);
        } else if id == BC_XDOP_TABLE {
            send(
                out,
                id,
                Data::new()
                    .byte(self.state.settings.dop_type())
                    .byte(0)
                    .byte(XDOP_IN_VIEW)
                    .zeros(XDOP_TABLE),
            );
        } else if id == BD_ALMANAC_STATUS {
            Self::almanac_status(record, out);
        } else if id == BH_CORRECTION_OUTPUT {
            self.state.corrections = Rate::of(data);
            self.corrections(out);
        } else if id == BJ_LEAP_SECOND_PENDING {
            send(out, id, Data::new().byte(self.leap_pending(record)));
        } else if id == BO_UTC_OFFSET {
            self.state.offset_updates = data == [OFFSET_EACH_CHANGE];
            send(out, id, Data::new().byte(self.wire_offset(record)));
        } else if id == GJ_LEAP_SECOND_PENDING {
            self.leap_details(record, out);
        } else if id == CF_SET_TO_DEFAULTS {
            self.state = State::starting(model);
            send(out, id, &Data::new());
        } else if id == CG_FIX_MODE {
            match data.first() {
                Some(&MODE_FIX) => self.state.fixing = true,
                Some(&MODE_IDLE) => self.state.fixing = false,
                _ => {}
            }
            let mode = if self.state.fixing {
                MODE_FIX
            } else {
                MODE_IDLE
            };
            send(out, id, Data::new().byte(mode));
        } else if id == CJ_RECEIVER_ID {
            self.receiver_id(out);
        } else if is_setting(id) {
            self.state.settings.set_or_query(id, data, model);
            if let Some(held) = self.state.settings.get(id) {
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
            if !(self.options.eight_on_ea && family.status == EA_POSITION_STATUS)
                || member != Member::Status
            {
                return;
            }
            self.channels = family.channels;
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

    /// Starts a self-test (VP p. 107; UT p. 6.42).  A VP goes idle for
    /// it and stays so; a UT Plus tells what its antenna's feed draws.
    fn self_test(&mut self, id: MessageId, record: &Record, now: Milliseconds<u64>) {
        let result = match self.identity.model() {
            Model::VpSix | Model::VpEight => {
                self.state.fixing = false;
                0
            }
            Model::UtPlus => match record.antenna {
                Antenna::Open => SELF_TEST_ANTENNA_UNDERCURRENT,
                Antenna::Shorted => SELF_TEST_ANTENNA_OVERCURRENT,
                Antenna::Unknown | Antenna::Connected => 0,
            },
        };
        if let Some(free) = self.deferred.iter_mut().find(|slot| slot.is_none()) {
            *free = Some(Deferred {
                due: Milliseconds(now.0.saturating_add(SELF_TEST)),
                id,
                result,
            });
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
        let setup = TraimSetup {
            algorithm,
            alarm_limit: u16::from_be_bytes([high, low]),
            pps_mode,
        };
        if setup.in_range() {
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
    /// record's.
    fn place(&self, record: &Record) -> Place {
        if self.state.settings.holding() {
            return self.state.settings.held_position(Self::separation(record));
        }
        record
            .position
            .map_or_else(Place::default, |position| Place {
                latitude: position.latitude.0,
                longitude: position.longitude.0,
                ellipsoid: position.ellipsoid.0,
                mean_sea_level: position.mean_sea_level.0,
            })
    }

    /// `@@Af` (VP p. 25): a height given, or the current one asked, is
    /// answered as both kinds of height.
    fn height<O: Output + ?Sized>(&self, data: &[u8], record: &Record, out: &mut O) {
        let separation = Self::separation(record);
        let given = match data {
            &[a, b, c, d, kind] if data.iter().any(|byte| *byte != POSITION_QUERY) => {
                let height = i32::from_be_bytes([a, b, c, d]);
                if kind == HEIGHT_MEAN_SEA_LEVEL {
                    height
                } else {
                    height.wrapping_sub(separation)
                }
            }
            _ => self.place(record).mean_sea_level,
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
        let ahead = self.state.settings.gmt_offset();
        let label = if self.state.settings.gps_time() {
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

    /// The satellites on a family's channels: those of the record with
    /// a signal, above the mask angle and not ignored, in its order, as
    /// many as there are channels.
    fn tracked<'a>(
        &self,
        record: &'a Record,
        family: &Family,
    ) -> impl Iterator<Item = &'a Satellite> + use<'a> {
        let mask = i16::from(self.state.settings.mask_angle());
        let ignored = self.state.settings.ignore_list();
        let listening = self.state.fixing && !Self::antenna_off(record);
        record
            .satellites
            .iter()
            .filter(move |satellite| {
                listening
                    && satellite.signal.is_some()
                    && i16::from(satellite.elevation.0) > mask
                    && !is_ignored(ignored, satellite.prn)
            })
            .take(self.channels.min(family.channels))
    }

    /// `@@Ba` and `@@Ea` (VP pp. 70 to 73 and 124 to 126; UT pp. 6.18
    /// to 6.20).
    fn position_status<O: Output + ?Sized>(&self, record: &Record, family: &Family, out: &mut O) {
        let ut_plus = self.identity.model() == Model::UtPlus;
        let place = self.place(record);
        let tracked = self.tracked(record, family).count();
        let antenna_sense = match (ut_plus, record.antenna) {
            (true, Antenna::Open) => DOP_TYPE_ANTENNA_UNDERCURRENT,
            (true, Antenna::Shorted) => DOP_TYPE_ANTENNA_OVERCURRENT,
            _ => 0,
        };
        let dops = record.dops;
        let (dop, dop_type, fix) = match (self.state.settings.holding(), tracked, record.fix) {
            (true, _, _) | (false, _, Fix::None) => (0, DOP_TYPE_PDOP, STATUS_ACQUIRING_OR_HOLD),
            (false, SATELLITES_3D.., Fix::ThreeD | Fix::TimeOnly) => (
                dops.map_or(0, |dops| dops.position.0),
                DOP_TYPE_PDOP,
                STATUS_3D_FIX,
            ),
            (false, SATELLITES_2D.., _) => (
                dops.map_or(0, |dops| dops.horizontal.0),
                DOP_TYPE_HDOP,
                STATUS_2D_FIX,
            ),
            (false, _, _) => (0, DOP_TYPE_PDOP, STATUS_ACQUIRING_OR_HOLD),
        };
        let visible = if self.state.fixing {
            record.satellites.len()
        } else {
            0
        };
        let few = if visible < FEW_VISIBLE {
            STATUS_FEW_VISIBLE
        } else {
            0
        };
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
            .word(dop)
            .byte(dop_type | antenna_sense)
            .byte(count_byte(visible))
            .byte(count_byte(tracked));
        let satellites = self.tracked(record, family);
        let channels = satellites
            .map(Some)
            .chain(core::iter::repeat(None))
            .take(family.channels);
        for channel in channels {
            match channel {
                Some(satellite) => {
                    let strength = satellite.signal.map_or(0, |signal| signal.0);
                    let used = match (satellite.used, ut_plus) {
                        (false, _) => 0,
                        (true, false) => CHANNEL_USED,
                        (true, true) => CHANNEL_USED | CHANNEL_USED_FOR_TIME,
                    };
                    data.byte(satellite.prn.get())
                        .byte(tracking_mode(satellite.tracking))
                        .byte(if ut_plus { strength } else { signal(strength) })
                        .byte(used);
                }
                None => {
                    data.zeros(4);
                }
            }
        }
        send(out, family.status, data.byte(fix | few));
    }

    /// `@@Bb` (VP p. 75).
    fn visibility<O: Output + ?Sized>(record: &Record, out: &mut O) {
        let mut data = Data::new();
        data.byte(count_byte(record.satellites.len()));
        for satellite in record.satellites.iter() {
            let health = match satellite.health {
                Health::Unhealthy => VISIBLE_UNHEALTHY,
                Health::Unknown | Health::Healthy => 0,
            };
            data.byte(satellite.prn.get())
                .signed_word(satellite.doppler.map_or(0, |doppler| doppler.0))
                .byte(satellite.elevation.0.max(0).cast_unsigned())
                .word(satellite.azimuth.0)
                .byte(health);
        }
        data.zeros((SATELLITES - record.satellites.len()) * VISIBLE_SLOT);
        send(out, BB_VISIBLE_SATELLITES, &data);
    }

    /// `@@Bd` (VP p. 79): the almanac's status, given twice, as the one
    /// in use and the one last read.
    fn almanac_status<O: Output + ?Sized>(record: &Record, out: &mut O) {
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
        send(
            out,
            BD_ALMANAC_STATUS,
            Data::new()
                .bytes(status.as_slice())
                .bytes(status.as_slice()),
        );
    }

    /// `@@Ce` (VP p. 115): the time of the corrections, and none.
    fn corrections<O: Output + ?Sized>(&self, out: &mut O) {
        send(
            out,
            CE_PSEUDORANGE_CORRECTION,
            Data::new()
                .triple(self.clock.time_of_week().deciseconds().0)
                .zeros(CORRECTIONS),
        );
    }

    /// `@@Bk` and `@@Ek` (VP pp. 95 to 98 and 134 to 137).
    fn extension<O: Output + ?Sized>(&self, record: &Record, family: &Family, out: &mut O) {
        let mut data = Data::new();
        match record.dops {
            Some(dops) => data
                .word(dops.geometric.0)
                .word(dops.position.0)
                .word(dops.horizontal.0)
                .word(dops.vertical.0)
                .word(dops.time.0),
            None => data.zeros(5 * size_of::<u16>()),
        };
        data.zeros(EXTENSION_BEFORE_POSITION);
        match record.derived.ecef {
            Some(ecef) if !self.state.settings.holding() => data
                .signed_long(ecef.x.0)
                .signed_long(ecef.y.0)
                .signed_long(ecef.z.0),
            _ => data.zeros(3 * size_of::<i32>()),
        };
        send(
            out,
            family.extension,
            data.zeros(family.channels).zeros(EXTENSION_REST),
        );
    }

    /// What Time RAIM can do with `tracked` satellites (VP p. 106).
    fn traim_ability(&self, tracked: usize) -> u8 {
        let limit = self.state.traim.alarm_limit;
        match TRAIM_NEEDS.iter().find(|need| limit >= need.from) {
            Some(need) if tracked >= need.isolate && self.state.settings.holding() => {
                TRAIM_ISOLATION
            }
            Some(need) if tracked >= need.detect => TRAIM_DETECTION,
            _ => TRAIM_NEITHER,
        }
    }

    /// `@@Bn` and `@@En` (VP pp. 102 to 106 and 138 to 142; UT
    /// pp. 6.92 to 6.95).
    fn traim_status<O: Output + ?Sized>(&self, record: &Record, family: &Family, out: &mut O) {
        let setup = self.state.traim;
        let tracked = self.tracked(record, family).count();
        let ability = self.traim_ability(tracked);
        let solution = if setup.algorithm == TRAIM_ON && ability != TRAIM_NEITHER {
            TRAIM_SOLUTION_OK
        } else {
            TRAIM_SOLUTION_UNKNOWN
        };
        let pulse = match setup.pps_mode {
            PPS_OFF => false,
            PPS_TRACKING => tracked != 0,
            PPS_CONFIRMED => solution == TRAIM_SOLUTION_OK,
            _ => true,
        };
        let timing = record.timing.filter(|_| tracked >= SPREAD_OF);
        let mut data = Data::new();
        data.byte(self.state.rates[family.index].traim.0)
            .byte(setup.algorithm)
            .word(setup.alarm_limit)
            .byte(setup.pps_mode);
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
            .byte(u8::from(self.state.settings.gps_time()))
            .byte(solution)
            .byte(ability)
            .word(timing.map_or(0, |timing| timing.sigma.0))
            .signed_byte(record.timing.map_or(0, |timing| timing.sawtooth.0));
        let satellites = self.tracked(record, family);
        let channels = satellites
            .map(Some)
            .chain(core::iter::repeat(None))
            .take(family.channels);
        for channel in channels {
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
        let current = if self.state.settings.gps_time() {
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

/// A count as a message's byte carries it.
fn count_byte(count: usize) -> u8 {
    u8::try_from(count).unwrap_or(u8::MAX)
}

/// Whether an ignore list, bit 0 satellite 1, names `prn`.
fn is_ignored(list: u32, prn: Prn) -> bool {
    list & (1 << (prn.get() - 1)) != 0
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
        Tracking::CarrierLock => 3,
        Tracking::BitSync => 4,
        Tracking::FrameSync => 6,
        Tracking::Ephemeris => 7,
        Tracking::Position => 8,
    }
}

#[cfg(test)]
mod tests;
