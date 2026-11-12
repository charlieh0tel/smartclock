//! Typed access to one receiver.
//!
//! Sits on a [`Session`] and resolves logical operations through the
//! dialect table, so a caller names what it wants rather than how a
//! particular firmware spells it.

use crate::command::COMPLETE_MODELS;
use crate::command::CommandId;
use crate::command::Dialect;
use crate::command::Spec;
use crate::control::Control;
use crate::error::Error;
use crate::error::Result;
use crate::error::is_state_refusal;
use crate::parse;
use crate::parse::Identity;
use crate::rollover::ReceiverDate;
use crate::screen;
use crate::screen::Screen;
use crate::session::Session;
use crate::snapshot::FieldFailure;
use crate::snapshot::Snapshot;
use crate::snapshot::Tier;
use crate::transport::Transport;
use crate::types::AlarmCondition;
use crate::types::Datum;
use crate::types::EfcPercent;
use crate::types::Ffom;
use crate::types::HardwareCondition;
use crate::types::HoldoverCondition;
use crate::types::HoldoverDuration;
use crate::types::HoldoverWaitReason;
use crate::types::OperationCondition;
use crate::types::Position;
use crate::types::PowerupCondition;
use crate::types::ReferenceStatus;
use crate::types::Seconds;
use crate::types::SmartClockMode;
use crate::types::Tfom;
use crate::types::TimeOfDay;

use jiff::Timestamp;
use jiff::civil::Date;

/// One receiver, addressed by logical operation.
#[derive(Debug)]
pub struct Device<T: Transport> {
    session: Session<T>,
    dialect: Dialect,
    identity: Identity,
    /// The queries refused on their own in the step being polled, for
    /// [`Device::poll_step`] to file against its tier.
    failed: Vec<FieldFailure>,
}

impl<T: Transport> Device<T> {
    /// Identify the receiver and choose a dialect for it.
    ///
    /// The session is synchronized first, since the receiver may be
    /// mid-reply from whatever spoke to it last.
    pub fn open(mut session: Session<T>) -> Result<Self> {
        session.sync()?;
        let identity = read_identity(&mut session)?;
        let dialect = dialect_for(&identity.model);
        Ok(Self {
            session,
            dialect,
            identity,
            failed: Vec::new(),
        })
    }

    /// Override the dialect chosen from `*IDN?`.
    pub fn with_dialect(mut self, dialect: Dialect) -> Self {
        self.dialect = dialect;
        self
    }

    /// What the receiver said it is.
    pub fn identity(&self) -> &Identity {
        &self.identity
    }

    /// Which command tree is in use.
    pub fn dialect(&self) -> Dialect {
        self.dialect
    }

    /// The underlying session, for raw commands.
    pub fn session(&mut self) -> &mut Session<T> {
        &mut self.session
    }

    /// A handle for changing receiver state.
    ///
    /// Separate from the read paths on purpose: reaching a control
    /// command means naming this, so nothing that only meant to read
    /// can send one by accident.
    pub fn control(&mut self) -> Control<'_, T> {
        Control::new(&mut self.session, self.dialect)
    }

    /// How this receiver is asked `id`, if it is asked it at all.
    fn spec(&self, id: CommandId) -> Result<&'static Spec> {
        // Naming the operation, not "the requested command": a typed
        // error carrying a constant string is worse than no error.
        let spec = self.dialect.spec(id).ok_or(Error::Unsupported {
            dialect: self.dialect.name(),
            operation: id,
        })?;
        if asked(spec, &self.identity.model) {
            Ok(spec)
        } else {
            Err(Error::NotListed {
                model: self.identity.model.clone(),
                operation: id,
            })
        }
    }

    /// Send one logical operation and return its single reply line.
    fn ask(&mut self, id: CommandId) -> Result<String> {
        let scpi = self.spec(id)?.scpi;
        let reply = self.session.query(scpi)?;
        Ok(reply.one_line("a single line")?.to_owned())
    }

    /// As [`Device::ask`], but a receiver declining on state yields
    /// `None` rather than an error.
    ///
    /// -221 and -230 mean the value does not exist right now, such as
    /// present holdover error while locked.  That is an answer, not a
    /// failure, and a monitor should show it as absent.
    fn ask_optional(&mut self, id: CommandId) -> Result<Option<String>> {
        absent_if_unsupported(self.ask(id))
    }

    /// Which mode the disciplining loop is in.
    pub fn mode(&mut self) -> Result<SmartClockMode> {
        let line = self.ask(CommandId::SyncState)?;
        SmartClockMode::parse(&line).ok_or(Error::Parse {
            reply: line.clone(),
            expected: "a SmartClock mode",
        })
    }

    /// Time figure of merit.
    pub fn tfom(&mut self) -> Result<Tfom> {
        let line = self.ask(CommandId::Tfom)?;
        to_u8(&line).and_then(Tfom::new).ok_or(Error::Parse {
            reply: line,
            expected: Tfom::EXPECTED,
        })
    }

    /// Frequency figure of merit.
    pub fn ffom(&mut self) -> Result<Ffom> {
        let line = self.ask(CommandId::Ffom)?;
        to_u8(&line).and_then(Ffom::new).ok_or(Error::Parse {
            reply: line,
            expected: Ffom::EXPECTED,
        })
    }

    /// Interval between the receiver's 1 PPS and the GPS 1 PPS.
    pub fn time_interval(&mut self) -> Result<Option<Seconds>> {
        self.ask_optional(CommandId::Tinterval)?
            .map(|l| parse::seconds(&l))
            .transpose()
    }

    /// Oscillator control voltage, as a share of its range.
    pub fn efc(&mut self) -> Result<EfcPercent> {
        let line = self.ask(CommandId::Efc)?;
        parse::efc(&line)
    }

    /// Internal temperature, in degrees Celsius.
    ///
    /// Undocumented.  Worth having because an OCXO's control voltage
    /// moves with temperature, so EFC drift cannot be read as aging
    /// without it.
    pub fn temperature(&mut self) -> Result<Option<f64>> {
        self.ask_optional(CommandId::Temperature)?
            .map(|l| parse::real(&l))
            .transpose()
    }

    /// Oven current.  Undocumented.
    pub fn oven_current(&mut self) -> Result<Option<f64>> {
        self.ask_optional(CommandId::OvenCurrent)?
            .map(|l| parse::real(&l))
            .transpose()
    }

    /// `:DIAGnostic:ROSCillator:TCOefficient?`, undocumented.  Despite the
    /// name, not a temperature coefficient: the constant `c` in the
    /// loop's EFC term `c.s`, in EFC counts per unit of oscillator
    /// current, set by a calibration and held in EEPROM; nothing in the
    /// firmware learns it (`docs/efc.md`, "The reported tempco").
    pub fn oven_tempco(&mut self) -> Result<Option<f64>> {
        self.ask_optional(CommandId::OvenTempco)?
            .map(|l| parse::real(&l))
            .transpose()
    }

    /// EFC as the raw DAC code.  Undocumented.
    pub fn efc_dac(&mut self) -> Result<Option<u32>> {
        let Some(line) = self.ask_optional(CommandId::EfcAbsolute)? else {
            return Ok(None);
        };
        let code = parse::int(&line)?;
        Ok(u32::try_from(code).ok())
    }

    /// The hardware condition register.
    pub fn hardware_condition(&mut self) -> Result<HardwareCondition> {
        self.register(CommandId::HardwareCondition)
            .map(HardwareCondition::from_bits)
    }

    /// The alarm condition register, which `*STB?` reads.
    ///
    /// Non-destructive, unlike every event register, which is the whole
    /// reason it is what gets polled: it reports which groups have
    /// latched something without taking the latch away.
    pub fn alarm_condition(&mut self) -> Result<AlarmCondition> {
        self.register(CommandId::Stb).map(AlarmCondition::from_bits)
    }

    /// The operation condition register.
    pub fn operation_condition(&mut self) -> Result<OperationCondition> {
        self.register(CommandId::OperCondition)
            .map(OperationCondition::from_bits)
    }

    /// The holdover condition register.
    pub fn holdover_condition(&mut self) -> Result<HoldoverCondition> {
        self.register(CommandId::HoldoverCondition)
            .map(HoldoverCondition::from_bits)
    }

    /// The powerup condition register.
    pub fn powerup_condition(&mut self) -> Result<PowerupCondition> {
        self.register(CommandId::PowerupCondition)
            .map(PowerupCondition::from_bits)
    }

    /// Read a status register as a bare 16-bit word.
    ///
    /// Condition registers only.  Reading an event register clears it,
    /// which would take the latched bit away from whatever else is
    /// watching and, through the summary bits, retract the receiver's
    /// own alarm; a logger must not do that as a side effect of
    /// logging.
    fn register(&mut self, id: CommandId) -> Result<u16> {
        let line = self.ask(id)?;
        let bits = parse::int(&line)?;
        u16::try_from(bits).map_err(|_| Error::Parse {
            reply: line,
            expected: "a 16-bit register",
        })
    }

    /// Why the receiver has not left holdover.
    pub fn holdover_waiting(&mut self) -> Result<HoldoverWaitReason> {
        let line = self.ask(CommandId::HoldoverWaiting)?;
        HoldoverWaitReason::parse(&line).ok_or(Error::Parse {
            reply: line.clone(),
            expected: "a holdover wait reason",
        })
    }

    /// How long holdover has lasted, and whether it is current.
    pub fn holdover_duration(&mut self) -> Result<HoldoverDuration> {
        let line = self.ask(CommandId::HoldoverDuration)?;
        parse::holdover_duration(&line)
    }

    /// Predicted time error over a day of holdover.
    pub fn holdover_predicted(&mut self) -> Result<Option<Seconds>> {
        Ok(self
            .ask_optional(CommandId::HoldoverUncPred)?
            .map(|l| parse::holdover_duration(&l))
            .transpose()?
            .map(|d| d.elapsed))
    }

    /// Time error accumulated so far in holdover.  Absent unless the
    /// receiver is in holdover.
    pub fn holdover_present(&mut self) -> Result<Option<Seconds>> {
        self.ask_optional(CommandId::HoldoverUncNow)?
            .map(|l| parse::seconds(&l))
            .transpose()
    }

    /// A Z3817A's time reference: which input, which pulse, and what
    /// is arriving on it.
    pub fn reference_status(&mut self) -> Result<ReferenceStatus> {
        let line = self.ask(CommandId::ReferenceStatus)?;
        parse::reference_status(&line)
    }

    /// How many satellites are being used.
    pub fn tracking_count(&mut self) -> Result<i64> {
        let line = self.ask(CommandId::SatTrackingCount)?;
        parse::int(&line)
    }

    /// How many the almanac expects to be visible.
    pub fn visible_count(&mut self) -> Result<i64> {
        let line = self.ask(CommandId::SatVisibleCount)?;
        parse::int(&line)
    }

    /// The averaged antenna position.
    pub fn position(&mut self) -> Result<Option<Position>> {
        let datum = datum_for(&self.identity.model);
        self.ask_optional(CommandId::PositionAvg)?
            .map(|l| parse::position(&l, datum))
            .transpose()
    }

    /// The receiver's date, checked against `today` for a GPS week
    /// rollover.
    pub fn date(&mut self, today: Date) -> Result<ReceiverDate> {
        let line = self.ask(CommandId::Date)?;
        Ok(ReceiverDate::checked(parse::ymd(&line)?, today))
    }

    /// The receiver's time of day.
    pub fn time(&mut self) -> Result<TimeOfDay> {
        let line = self.ask(CommandId::Time)?;
        parse::hms(&line)
    }

    /// How many diagnostic log entries are held.
    pub fn log_count(&mut self) -> Result<i64> {
        let line = self.ask(CommandId::LogCount)?;
        parse::int(&line)
    }

    /// The whole status screen, scraped.
    ///
    /// This is the only source of per-satellite elevation, azimuth and
    /// signal strength, and it is the most expensive query the receiver
    /// offers: roughly 1.8 KB, about a second of wire time at 19200.
    pub fn screen(&mut self) -> Result<Screen> {
        let scpi = self.spec(CommandId::StatusScreen)?.scpi;
        let reply = self.session.query(scpi)?;
        Ok(screen::parse(&reply.lines.join("\n")))
    }
}

/// A value this receiver cannot give reads as absent, not as a failure.
///
/// Two things mean that, and they mean the same to a reader.  The
/// receiver may refuse with a state error, because the value does not
/// exist yet -- present holdover error while locked.  Or the dialect
/// may have no command for it at all, because this model does not
/// expose it.
///
/// Neither is a reason to abandon a tier.  A Z3805A has no spelling for
/// TFOM in the Z3801 tree, and one `?` on that line used to void every
/// field after it: the snapshot went unstamped and empty, a hundred and
/// twenty-eight rows carrying one frozen timestamp and no readings,
/// while every other value on the tier was there for the asking.
///
/// A command the table does not list for a model whose lists are
/// binding is the second case too, said by the table rather than the
/// tree.
fn absent_if_unsupported<T>(result: Result<T>) -> Result<Option<T>> {
    match result {
        Ok(value) => Ok(Some(value)),
        Err(Error::Unsupported { .. } | Error::NotListed { .. }) => Ok(None),
        Err(Error::Device { code, .. }) if is_state_refusal(code) => Ok(None),
        Err(e) => Err(e),
    }
}

/// Ask the receiver what it is.
fn read_identity<T: Transport>(session: &mut Session<T>) -> Result<Identity> {
    let reply = session.query("*IDN?")?;
    parse::identity(reply.one_line("an identity")?)
}

fn to_u8(line: &str) -> Option<u8> {
    parse::int(line).ok().and_then(|n| u8::try_from(n).ok())
}

/// Pick a command tree from the model in `*IDN?`.
pub fn dialect_for(model: &str) -> Dialect {
    let model = model.to_ascii_uppercase();
    if model.starts_with("Z38") {
        Dialect::Z3801
    } else {
        Dialect::Hp58503
    }
}

/// How `dialect` spells `id` for a `model`, if that model is asked it
/// at all ([`asked`]).
pub fn spec_for(dialect: Dialect, model: &str, id: CommandId) -> Option<&'static Spec> {
    dialect.spec(id).filter(|spec| asked(spec, model))
}

/// Whether a receiver of `model` is asked the command `spec` spells.
///
/// The table's `models` lists bind only the models in
/// [`COMPLETE_MODELS`], whose lists have been audited to name every
/// command they answer: one of those is asked only what names it.  Any
/// other model is asked everything its tree has, as it was before the
/// lists bound anyone, so a receiver nobody has audited is not starved
/// of what it answers -- except an entry naming only audited models,
/// which is theirs alone and would draw -113 from the rest.  Compared
/// without case.
fn asked(spec: &Spec, model: &str) -> bool {
    let complete = |m: &str| COMPLETE_MODELS.iter().any(|c| c.eq_ignore_ascii_case(m));
    spec.models.iter().any(|m| m.eq_ignore_ascii_case(model))
        || (!complete(model) && !spec.models.iter().all(|m| complete(m)))
}

/// A satellite count as an unsigned number.
///
/// The receiver answers `+7`, parsed as a signed integer because that
/// is what the response type says.  A negative count is not a reading
/// the hardware can produce, so it is floored rather than refused.
fn satellite_count(n: i64) -> u32 {
    u32::try_from(n).unwrap_or(0)
}

/// Which vertical datum a model's heights use.
///
/// The 58503A and 59551A report height above mean sea level; the 58503B
/// reports it above the GPS ellipsoid.  Getting this wrong misplaces a
/// position by tens of meters vertically.
fn datum_for(model: &str) -> Datum {
    match model.to_ascii_uppercase().as_str() {
        "58503B" => Datum::Ellipsoid,
        _ => Datum::MeanSeaLevel,
    }
}

/// One step of a tier's pass.
///
/// Naming the steps lets each tier be written as a list, so the number
/// of steps is the length of that list rather than a second constant
/// to keep in agreement with it.  Dispatch is an exhaustive match, so
/// a step added here and nowhere else does not compile.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum Step {
    /// Everything on the fast tier, read together.
    Fast,
    /// The satellite counts.
    Satellites,
    /// Oven temperature and current, and the EFC DAC.
    Oscillator,
    /// The subgroup condition registers, and a Z3817A's reference.
    Registers,
    /// Holdover duration, prediction and present error.
    Holdover,
    /// `*IDN?`, compared with the identity read at open.
    Identity,
    /// Position and date.
    Position,
    /// Log count, the oscillator-current constant and the powerup register.
    Counters,
}

/// The steps making up each tier's pass, in the order they are run.
///
/// A scheduler takes one step per turn, so a tier costs the link its
/// longest single step rather than its whole pass.  The fast tier is
/// one step on purpose: its fields are compared against each other,
/// and a time interval from one second beside an EFC from the next is
/// a correlation nobody measured.
const fn steps(tier: Tier) -> &'static [Step] {
    match tier {
        Tier::Fast => &[Step::Fast],
        Tier::Medium => &[
            Step::Satellites,
            Step::Oscillator,
            Step::Registers,
            Step::Holdover,
        ],
        // Identity first, so a swapped receiver's position and
        // counters are never read into the old one's snapshot.
        Tier::Slow => &[Step::Identity, Step::Position, Step::Counters],
    }
}

/// How many steps a pass over this tier takes.
pub fn step_count(tier: Tier) -> usize {
    steps(tier).len()
}

/// Polling one tier's worth of fields into a snapshot.
impl<T: Transport> Device<T> {
    /// Refresh the fields belonging to `tier`.
    ///
    /// A field the receiver declines on state is set to `None` rather
    /// than left at its previous value, because a stale reading is
    /// worse than an absent one.  So is one it refuses outright, which
    /// is named in the tier's `failed` list and costs no other field
    /// ([`Device::field`]).  A transport failure or an unreadable reply
    /// aborts the tier and propagates.
    ///
    /// Only this tier's success is recorded.  The others keep the time
    /// of their own last success, so a caller can tell how old each
    /// group of fields is rather than reading one timestamp that the
    /// fastest tier keeps refreshing on everyone's behalf.
    ///
    /// This runs the tier's whole pass in one go.  A scheduler sharing
    /// the link with a faster tier wants [`Device::poll_step`] instead.
    pub fn poll(&mut self, tier: Tier, into: &mut Snapshot, now: Timestamp) -> Result<()> {
        for step in 0..step_count(tier) {
            self.poll_step(tier, step, into, now)?;
        }
        Ok(())
    }

    /// Refresh one step of `tier`'s pass.
    ///
    /// Steps are numbered from zero and are meant to be run in order.
    /// Success is recorded only on the last one: a pass is as old as
    /// its slowest field, so stamping it partway through would call
    /// fields fresh that had not been read yet.
    ///
    /// A step index past the end of the tier reads nothing and is not
    /// an error, so a caller that has lost its place cannot wedge.
    pub fn poll_step(
        &mut self,
        tier: Tier,
        step: usize,
        into: &mut Snapshot,
        now: Timestamp,
    ) -> Result<()> {
        let steps = steps(tier);
        let Some(step_kind) = steps.get(step) else {
            return Ok(());
        };
        // Left over only from a step that failed outright, whose
        // snapshot the caller discards with them.
        self.failed.clear();
        match step_kind {
            Step::Fast => self.poll_fast(into)?,
            Step::Satellites => self.poll_satellites(into)?,
            Step::Oscillator => self.poll_oscillator(into)?,
            Step::Registers => self.poll_registers(into)?,
            Step::Holdover => self.poll_holdover(into)?,
            Step::Identity => self.check_identity()?,
            Step::Position => self.poll_position(into, now)?,
            Step::Counters => self.poll_counters(into)?,
        }
        if step == 0 {
            into.polled.begin_pass(tier);
        }
        for failure in self.failed.drain(..) {
            into.polled.field_failed(tier, failure);
        }
        into.at = now;
        if step + 1 == steps.len() {
            into.polled.succeeded(tier, now);
        }
        into.settle_freshness();
        Ok(())
    }

    /// Read one field of a step, keeping a failure that concerns its
    /// query alone from costing the rest of the step.
    ///
    /// What this model or dialect does not ask, and what the receiver
    /// declines on state, is absent and no failure (see
    /// [`absent_if_unsupported`]).  Any other refusal -- -113 for a
    /// keyword the firmware lists but does not implement, or one whose
    /// code a full error queue lost -- is absent too, and recorded for
    /// [`Device::poll_step`] to file against the tier.
    ///
    /// Everything else still fails the step.  A reply that does not
    /// parse means the reader and the receiver disagree about a format,
    /// which is a defect to see rather than a field to drop, and a link
    /// failure or a swap makes everything after it untrustworthy.
    fn field<V>(
        &mut self,
        id: CommandId,
        read: impl FnOnce(&mut Self) -> Result<V>,
    ) -> Result<Option<V>> {
        let error = match absent_if_unsupported(read(self)) {
            Ok(value) => return Ok(value),
            Err(e @ (Error::Device { .. } | Error::ErrorLost { .. })) => e,
            Err(e) => return Err(e),
        };
        let code = match error {
            Error::Device { code, .. } => Some(code),
            _ => None,
        };
        self.failed.push(FieldFailure {
            scpi: self.dialect.spec(id).map_or("", |s| s.scpi).to_owned(),
            code,
            error: error.to_string(),
        });
        Ok(None)
    }

    fn poll_fast(&mut self, into: &mut Snapshot) -> Result<()> {
        into.mode = self.field(CommandId::SyncState, Self::mode)?;
        into.tfom = self.field(CommandId::Tfom, Self::tfom)?;
        into.ffom = self.field(CommandId::Ffom, Self::ffom)?;
        into.time_interval = self
            .field(CommandId::Tinterval, Self::time_interval)?
            .flatten();
        into.efc = self.field(CommandId::Efc, Self::efc)?;
        into.hardware = self.field(CommandId::HardwareCondition, Self::hardware_condition)?;
        into.holdover_waiting = self.field(CommandId::HoldoverWaiting, Self::holdover_waiting)?;
        into.time = self.field(CommandId::Time, Self::time)?;
        Ok(())
    }

    /// The satellite counts, which the status screen also prints.
    ///
    /// Two short queries, about 80 ms, against the 1.5 s the screen
    /// costs.  Reading them here is what lets the screen sit on no
    /// tier -- it is read every `--sky` seconds (300 by default) and
    /// on request -- without the counts going stale with it.
    fn poll_satellites(&mut self, into: &mut Snapshot) -> Result<()> {
        into.tracking = self
            .field(CommandId::SatTrackingCount, Self::tracking_count)?
            .map(satellite_count);
        into.visible = self
            .field(CommandId::SatVisibleCount, Self::visible_count)?
            .map(satellite_count);
        Ok(())
    }

    fn poll_oscillator(&mut self, into: &mut Snapshot) -> Result<()> {
        // Temperature and oven current sit here rather than on the fast
        // tier: they move slowly, and the fast tier is already close to
        // its budget.
        into.temperature = self
            .field(CommandId::Temperature, Self::temperature)?
            .flatten();
        into.oven_current = self
            .field(CommandId::OvenCurrent, Self::oven_current)?
            .flatten();
        into.efc_dac = self.field(CommandId::EfcAbsolute, Self::efc_dac)?.flatten();
        Ok(())
    }

    /// The subgroup condition registers.
    ///
    /// A condition register is read in real time and holds nothing, so
    /// sampling one every ten seconds misses transitions -- the event
    /// registers catch those, and reading an event register clears it,
    /// which a logger has no business doing.  What survives a poll here
    /// is the receiver's steady state, which is what the history is
    /// for.
    fn poll_registers(&mut self, into: &mut Snapshot) -> Result<()> {
        into.alarm = self.field(CommandId::Stb, Self::alarm_condition)?;
        into.operation = self.field(CommandId::OperCondition, Self::operation_condition)?;
        into.holdover_state = self.field(CommandId::HoldoverCondition, Self::holdover_condition)?;
        // What a Z3817A's reference inputs are being fed.  Here rather
        // than a step of its own, which would cost every other model a
        // scheduler turn and a logged row each pass to send nothing.
        into.reference = self.field(CommandId::ReferenceStatus, Self::reference_status)?;
        Ok(())
    }

    fn poll_holdover(&mut self, into: &mut Snapshot) -> Result<()> {
        // Absent when refused, like every other field, rather than
        // abandoning the rest of the step.
        let holdover = self.field(CommandId::HoldoverDuration, Self::holdover_duration)?;
        into.holdover_duration = holdover;
        into.holdover_predicted = self
            .field(CommandId::HoldoverUncPred, Self::holdover_predicted)?
            .flatten();
        // Only asked for while it exists.  Present holdover error is
        // refused with -230 whenever the receiver is locked, and a
        // refusal is not free: the session reads the error queue to
        // explain it, so a question we already know the answer to cost
        // two round trips of a tier that is against its wire budget,
        // on every medium poll, for the life of the daemon.  It also set
        // the command-error bit in the standard event status register
        // each time, which made that register a record of our own
        // manners rather than of the receiver's.
        into.holdover_present = if holdover.is_some_and(|h| h.active) {
            self.field(CommandId::HoldoverUncNow, Self::holdover_present)?
                .flatten()
        } else {
            None
        };
        Ok(())
    }

    /// Whether the receiver is still the one the port was opened on.
    ///
    /// `*IDN?` is otherwise read only at open, so a cable moved to
    /// another unit quickly enough that no read failed would leave the
    /// new unit's readings filed under the old one's serial.
    ///
    /// An answer that is not an identity at all -- an installer's
    /// revision string -- is not the unit attached either, and is
    /// treated as a swap, which reopens and identifies afresh.
    fn check_identity(&mut self) -> Result<()> {
        let reply = self.session.query("*IDN?")?;
        let line = reply.one_line("an identity")?;
        match parse::identity(line) {
            Ok(now) if self.identity.same_unit(&now) => Ok(()),
            Ok(now) => Err(Error::Swapped {
                was: self.identity.to_string(),
                now: now.to_string(),
            }),
            Err(_) => Err(Error::Swapped {
                was: self.identity.to_string(),
                now: line.to_owned(),
            }),
        }
    }

    /// Every one of these is wrapped, including the two that already
    /// return an Option.  A receiver that has never had a fix refuses
    /// its date with -230, and a bare `?` on that line aborts the rest
    /// of the tier: the counters below would report themselves missing
    /// when they were merely unreached.
    fn poll_position(&mut self, into: &mut Snapshot, now: Timestamp) -> Result<()> {
        into.position = self
            .field(CommandId::PositionAvg, Self::position)?
            .flatten();
        let today = now.to_zoned(jiff::tz::TimeZone::UTC).date();
        into.date = self.field(CommandId::Date, |device| device.date(today))?;
        Ok(())
    }

    fn poll_counters(&mut self, into: &mut Snapshot) -> Result<()> {
        into.log_count = self.field(CommandId::LogCount, Self::log_count)?;
        into.oven_tempco = self
            .field(CommandId::OvenTempco, Self::oven_tempco)?
            .flatten();
        // All three of its bits are set during startup and then stay,
        // so asking on every medium poll spent a round trip to be told
        // what it said last time.  The medium tier is the one against
        // its budget; this belongs with the other things that barely
        // move.
        into.powerup = self.field(CommandId::PowerupCondition, Self::powerup_condition)?;
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::Step;
    use super::absent_if_unsupported;
    use super::steps;
    use crate::command::CommandId;
    use crate::snapshot::Tier;

    use super::asked;
    use super::datum_for;
    use super::dialect_for;
    use super::spec_for;
    use crate::command::COMPLETE_MODELS;
    use crate::command::Dialect;
    use crate::types::Datum;

    #[test]
    fn an_audited_model_is_asked_only_what_its_lists_name() {
        assert!(COMPLETE_MODELS.contains(&"Z3817A"));
        let z3817a = |id| spec_for(Dialect::Z3801, "Z3817A", id).is_some();
        // Its stub answers -113, and costs a round trip and a queued
        // error on every medium pass.
        assert!(!z3817a(CommandId::OvenCurrent));
        assert!(z3817a(CommandId::Efc));
        assert!(z3817a(CommandId::EfcAbsolute));
        // Compared without case, as dialect_for compares.
        assert!(spec_for(Dialect::Z3801, "z3817a", CommandId::Efc).is_some());
        assert!(spec_for(Dialect::Z3801, "z3817a", CommandId::OvenCurrent).is_none());
    }

    #[test]
    fn a_model_nobody_has_audited_is_asked_what_it_was_asked_before() {
        // Every z3801 query names the Z3801A or the Z3805A, or both,
        // and neither is in COMPLETE_MODELS, so each is asked of
        // both, and of a Z3815A the table names nowhere -- the lists
        // were documentation until now, and binding them for a model
        // whose lists were never audited would silently starve it.
        for model in ["Z3801A", "Z3805A", "Z3815A", "Z3816A"] {
            for spec in Dialect::Z3801.specs() {
                let theirs_alone = spec.models.iter().all(|m| COMPLETE_MODELS.contains(m));
                assert_eq!(asked(spec, model), !theirs_alone, "{model} {:?}", spec.id);
            }
        }
        for model in ["58503A", "58503B", "59551A", "58540A"] {
            for spec in Dialect::Hp58503.specs() {
                assert!(asked(spec, model), "{model} {:?}", spec.id);
            }
        }
    }

    #[test]
    fn every_step_belongs_to_exactly_one_tier() {
        // A missing match arm is a compile error; a variant that no
        // tier schedules is not, and would read nothing forever.
        let scheduled: Vec<Step> = Tier::ALL.iter().flat_map(|t| steps(*t)).copied().collect();
        // The screen is read on request, not on a schedule.
        for step in [
            Step::Fast,
            Step::Satellites,
            Step::Oscillator,
            Step::Registers,
            Step::Holdover,
            Step::Identity,
            Step::Position,
            Step::Counters,
        ] {
            assert_eq!(
                scheduled.iter().filter(|s| **s == step).count(),
                1,
                "{step:?}"
            );
        }
        assert_eq!(scheduled.len(), 8);
    }

    #[test]
    fn the_model_chooses_the_command_tree() {
        assert_eq!(dialect_for("58503A"), Dialect::Hp58503);
        assert_eq!(dialect_for("58503B"), Dialect::Hp58503);
        assert_eq!(dialect_for("59551A"), Dialect::Hp58503);
        assert_eq!(dialect_for("Z3801A"), Dialect::Z3801);
        assert_eq!(dialect_for("z3816a"), Dialect::Z3801);
        // An unknown model gets the tree we can actually test against.
        assert_eq!(dialect_for("58540A"), Dialect::Hp58503);
    }

    #[test]
    fn the_model_chooses_the_vertical_datum() {
        assert_eq!(datum_for("58503A"), Datum::MeanSeaLevel);
        assert_eq!(datum_for("59551A"), Datum::MeanSeaLevel);
        assert_eq!(datum_for("58503B"), Datum::Ellipsoid);
    }
    #[test]
    fn a_command_this_model_lacks_is_absent_rather_than_a_failure() {
        // A Z3805A has no TFOM in the Z3801 tree.  Before this, the `?`
        // on that one line abandoned the rest of the fast tier: the
        // snapshot was never stamped and never filled, so a hundred and
        // twenty-eight rows arrived carrying one frozen timestamp and
        // no readings, while FFOM, EFC and the rest were there for the
        // asking.
        let missing: crate::error::Result<u8> = Err(crate::error::Error::Unsupported {
            dialect: "z3801",
            operation: CommandId::Tfom,
        });
        assert_eq!(
            absent_if_unsupported(missing).expect("must not propagate"),
            None
        );

        // A refusal means the same to a reader and is treated the same.
        let refused: crate::error::Result<u8> = Err(crate::error::Error::Device {
            code: -230,
            message: "Data corrupt or stale".to_owned(),
        });
        assert_eq!(absent_if_unsupported(refused).expect("nor this"), None);

        // Anything else still is a failure: a receiver that stopped
        // answering must not read as a receiver without the feature.
        let broken: crate::error::Result<u8> = Err(crate::error::Error::Parse {
            reply: "nonsense".to_owned(),
            expected: "a number",
        });
        assert!(absent_if_unsupported(broken).is_err());

        assert_eq!(absent_if_unsupported(Ok(3u8)).expect("a value"), Some(3));
    }
}
