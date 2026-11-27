//! A design's netlist executed on the board: the LCA model (lca.rs)
//! behind `Logic`'s interface (logic.rs), its pins wired as the board
//! wires the part (docs/firmware/fpga-codegen.md).
//!
//! - **Time.**  The model's picoseconds are the board's oscillator's
//!   own: a tick is its `Wiring`'s `tick` of them (pins.rs), from the
//!   tick the design was made on, and the model drives the design's
//!   clocks itself, as its `Wiring` has them: P7 rising on each tick and
//!   falling half way, say, so a steered oscillator steers the design by
//!   its phase as it does the hand model.  Where the real board's PLL
//!   locks the design's clock to the 10 MHz through a divider of the
//!   design's that its sync field steps, the PLL is not modeled: the
//!   board drives the clock ideally, and the model's time moves a tick
//!   ahead of the board's or behind it at each step inferred from the
//!   field's writes (`Steps`), as the PLL would move the clock.
//!   Between the bus's changes the model is moved on by
//!   fast-forward (`lca/fast_forward.rs`), as if every edge were clocked;
//!   a board file asks for this model (`design = "lca"`) and the hand
//!   model stays the default.
//! - **A bus access is a bus cycle.**  The select Low for chip select
//!   5's cycle, `BUS_CYCLE`, and a write's data and R/W held
//!   `WRITE_HOLD` past it, are pin changes in the model's own time, in
//!   order with the clock's edges and the GPS 1 PPS.  A read must answer
//!   at once, sampling the data bus as its cycle ends.  Where the board
//!   says nothing can reach the part before then (mc683xx's
//!   `PeripheralAccess::horizon`), the model itself runs through the
//!   cycle and answers from its own sample, exactly for the netlist, the
//!   interpolator's byte as of the cycle's start, keeping what it saw
//!   past the board's time until the board gets there.  Otherwise a
//!   copy of the model runs through it and answers, while the model
//!   makes the same pin changes in order, so that an input landing
//!   within the cycle still reaches the model at its time; the answer
//!   is the same either way unless one does.
//! - **The internal 1 PPS and the interval** are what the model's pins
//!   do: P83's rise, on the rising edge before it, and P81's Low pulse,
//!   the start's interval the interpolator measures.  The board needs
//!   the pulse's tick before it comes (`next_event`), so the model runs
//!   a copy of itself ahead, fast-forwarded, to the pulse or its
//!   horizon at most; while nothing drives a pin the copy is the
//!   model's future, which the model takes over once it gets there.
//!   The horizon starts short when a pin is driven and grows each time
//!   the model takes the copy over, to `AHEAD_TICKS`, so that an access
//!   throws away little of the copy's run and a quiet second costs
//!   few runs.

use std::cell::RefCell;
use std::collections::VecDeque;
use std::sync::Arc;
use std::sync::Mutex;

use super::super::clock::IDEAL_HZ;
use super::super::clock::Phase;
use super::super::clock::Tick;
use super::super::time::CPU_HZ;
use super::lca::Drive;
use super::lca::Lca;
use super::lca::Netlist;
use super::lca::Picoseconds;
use super::lca::Pin;
use super::lca::Probe;
use super::lca::Transition;
use super::lca::fast_forward::FastForward;
use super::logic::Ran;
use super::pins::ADDRESS;
use super::pins::BYTE;
use super::pins::CHIP_SELECT;
use super::pins::DATA;
use super::pins::GPS;
use super::pins::INTERVAL;
use super::pins::PPS;
use super::pins::READ_WRITE;
use super::pins::Steps;
use super::pins::TICK;
use super::pins::Wiring;

/// Picoseconds a second.
const PS_PER_SECOND: u64 = 1_000_000_000_000;
/// Chip select 5's bus cycle: a byte access's three clocks of the
/// MC68331's, and its 12 wait states (chip-selects.md), about 0.9 us,
/// as `z3805a_tb.v` reckons it.  A write's data and R/W are held a
/// quarter of a cycle past the select, as `z3805a_stimulus.v` holds them.
const BUS_CLOCKS: u64 = 3;
const CS5_WAIT_STATES: u64 = 12;
const BUS_CYCLE: u64 = (BUS_CLOCKS + CS5_WAIT_STATES) * PS_PER_SECOND / CPU_HZ;
const WRITE_HOLD: u64 = BUS_CYCLE / 4;
/// The shortest Low of P81 that is an interval, not a glitch, as the
/// differential test takes it (lca/tests.rs).
const GLITCH: u64 = 5_000;
/// How far the copy runs ahead of the model, ticks, when no pulse comes
/// first: how often the board comes back to the design, and the most of
/// a copy's run an access throws away.  Fast-forwarded, a copy's run
/// costs its landings, not its ticks, but on a design whose groups wake
/// often (the Z3816A's) its landings are many a second, and its
/// firmware writes the port about a hundred times a second: so the
/// horizon after an access is `FIRST_AHEAD_TICKS`, and it grows by
/// `AHEAD_GROWTH` each time the model takes the copy over, to a second.
const AHEAD_TICKS: u64 = IDEAL_HZ;
const FIRST_AHEAD_TICKS: u64 = AHEAD_TICKS / 64;
const AHEAD_GROWTH: u64 = 4;

/// A netlist's fast-forward, shared.
type Shared = Arc<Mutex<FastForward<'static>>>;

thread_local! {
    /// The fast-forward of each netlist run on this thread: what one run
    /// learns of a netlist serves every run of it after, the design
    /// reset or configured again, the board booted again, as it serves
    /// a run's copies.  Its tables are the netlist's, whatever the run.
    static FAST_FORWARDS: RefCell<Vec<(&'static Netlist, Shared)>> =
        const { RefCell::new(Vec::new()) };
}

/// `netlist`'s fast-forward on this thread, made the first time.
fn fast_forward(netlist: &'static Netlist) -> Shared {
    FAST_FORWARDS.with_borrow_mut(|known| {
        if let Some((_, fast)) = known
            .iter()
            .find(|(known, _)| std::ptr::eq(*known, netlist))
        {
            return Arc::clone(fast);
        }
        let fast = Arc::new(Mutex::new(FastForward::new(netlist)));
        known.push((netlist, Arc::clone(&fast)));
        fast
    })
}

/// A design's netlist and how the board wires its own pins.
#[derive(Debug, Clone, Copy)]
pub(super) struct Wired {
    pub(super) netlist: &'static Netlist,
    pub(super) wiring: &'static Wiring,
}

impl Wired {
    /// Every pin the board wires.
    fn pins(self) -> impl Iterator<Item = Pin> {
        [GPS, PPS, INTERVAL, CHIP_SELECT, READ_WRITE]
            .into_iter()
            .chain(ADDRESS)
            .chain(DATA)
            .chain(BYTE)
            .chain(self.wiring.clocks.iter().map(|clock| clock.pin))
            .chain(self.wiring.held.iter().map(|(pin, _)| *pin))
    }

    /// A pin of the board's that the netlist bonds to no I/O block, if
    /// any: the design cannot be wired.
    pub(super) fn unwired(self) -> Option<Pin> {
        let lca = Lca::new(self.netlist);
        self.pins().find(|pin| lca.pin(*pin).is_err())
    }
}

fn level(high: bool) -> Drive {
    Drive::Strong(high)
}

/// What the bus does at a time of the model's.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum Change {
    Drive(Pin, Drive),
    /// The data bus read, as a read's cycle ends.
    Sample,
}

/// The netlist running, and what the board has still to do to its pins.
#[derive(Debug, Clone)]
struct Run {
    lca: Lca<'static>,
    /// What moves it on, shared with its copies and every run of the
    /// netlist on the thread (`FAST_FORWARDS`): what it has learned of
    /// the netlist serves them all.
    fast: Shared,
    /// The bus's changes to come, in time order.
    scheduled: VecDeque<(Picoseconds, Change)>,
    /// P83's and P81's changes since the run was made or copied.
    seen: Vec<Transition>,
    /// The data bus as the last read's cycle ended.
    sampled: u8,
    /// Everything done to the netlist, in order, for a test to do again
    /// edge by edge.
    #[cfg(test)]
    log: Vec<(Picoseconds, Logged)>,
    /// `BUS_CYCLE` and `WRITE_HOLD` in the model's picoseconds.
    bus_cycle: u64,
    write_hold: u64,
}

impl Run {
    /// `wired`'s netlist configured, RESET Low, the board's pins at
    /// their idle levels: the select and R/W High, the data bus pulled
    /// High, the inputs held as the wiring holds them, the rest Low
    /// (`z3805a_stimulus.v`'s); and the wiring's clocks running.
    fn new(wired: Wired) -> Self {
        let mut run = Self {
            lca: Lca::new(wired.netlist),
            fast: fast_forward(wired.netlist),
            scheduled: VecDeque::new(),
            seen: Vec::new(),
            sampled: 0,
            #[cfg(test)]
            log: Vec::new(),
            bus_cycle: BUS_CYCLE * wired.wiring.tick / TICK,
            write_hold: WRITE_HOLD * wired.wiring.tick / TICK,
        };
        for probe in [PPS, INTERVAL] {
            run.lca
                .watch(Probe::Pin(probe))
                .expect("a wired pin, checked at the configuration");
        }
        let idle = [
            (CHIP_SELECT, level(true)),
            (READ_WRITE, level(true)),
            (GPS, level(false)),
        ];
        let held = wired
            .wiring
            .held
            .iter()
            .map(|(pin, high)| (*pin, level(*high)));
        let low = ADDRESS.iter().chain(&BYTE).map(|pin| (*pin, level(false)));
        let pulled = DATA.iter().map(|pin| (*pin, Drive::Pull(true)));
        for (pin, drive) in idle.into_iter().chain(held).chain(low).chain(pulled) {
            run.drive(pin, drive);
        }
        for clock in wired.wiring.clocks {
            run.lca
                .add_clock(*clock)
                .expect("a wired pin, checked at the configuration");
        }
        #[cfg(test)]
        run.log.push((run.now(), Logged::Clocks));
        run
    }

    fn now(&self) -> Picoseconds {
        self.lca.now()
    }

    fn drive(&mut self, pin: Pin, drive: Drive) {
        #[cfg(test)]
        self.log.push((self.now(), Logged::Drive(pin, drive)));
        self.lca
            .drive(pin, drive)
            .expect("a wired pin, checked at the configuration");
    }

    /// RESET High: the design let go.
    fn release(&mut self) {
        #[cfg(test)]
        self.log.push((self.now(), Logged::Release));
        self.lca.set_reset(true);
    }

    /// Time moved on to `until`: the clock's edges and the bus's
    /// changes due by then, the clock first at an instant both act on.
    fn step(&mut self, until: Picoseconds) {
        self.advance(until, false);
    }

    /// Time moved on toward `until` as `step` moves it, stopping once
    /// P83 or P81 has changed, if one does first, past the change by as
    /// many periods as `FastForward::advance_to_change` runs past it.
    fn step_to_change(&mut self, until: Picoseconds) {
        self.advance(until, true);
    }

    fn advance(&mut self, until: Picoseconds, stop: bool) {
        let fast = Arc::clone(&self.fast);
        let mut fast = fast.lock().expect("fast-forward is never poisoned");
        loop {
            let next = self
                .scheduled
                .front()
                .copied()
                .filter(|(at, _)| *at <= until);
            let to = next.map_or(until, |(at, _)| at);
            if stop {
                fast.advance_to_change(&mut self.lca, to);
                if self.lca.changed() {
                    break;
                }
            } else {
                fast.advance(&mut self.lca, to);
            }
            let Some((_, change)) = next else {
                break;
            };
            self.scheduled.pop_front();
            self.apply(change);
        }
        self.seen.extend(self.lca.take_transitions());
    }

    fn apply(&mut self, change: Change) {
        match change {
            Change::Drive(pin, drive) => self.drive(pin, drive),
            Change::Sample => {
                self.sampled = DATA.iter().enumerate().fold(0, |byte, (bit, pin)| {
                    let high = self
                        .lca
                        .pin(*pin)
                        .expect("a wired pin, checked at the configuration");
                    byte | u8::from(high) << bit
                });
                #[cfg(test)]
                self.log.push((self.now(), Logged::Sample(self.sampled)));
            }
        }
    }

    /// A bus cycle, `starts` its changes as it starts, as soon as the
    /// last one's are done: at once, but for a cycle that follows one
    /// still running, as the CPU's next access would.  The select goes
    /// Low as it starts, `ends` are its changes as it ends, the select
    /// High after them, and `released` a quarter cycle on.  Returns when
    /// it ends.
    fn bus_cycle(
        &mut self,
        starts: impl IntoIterator<Item = Change>,
        ends: &[Change],
        released: &[Change],
    ) -> Picoseconds {
        let start = self
            .scheduled
            .back()
            .map_or(self.now(), |(at, _)| *at)
            .max(self.now());
        let end = Picoseconds(start.0 + self.bus_cycle);
        let release = Picoseconds(end.0 + self.write_hold);
        let select = |low: bool| Change::Drive(CHIP_SELECT, level(!low));
        let starts = starts
            .into_iter()
            .chain([select(true)])
            .map(|change| (start, change));
        let ends = ends
            .iter()
            .copied()
            .chain([select(false)])
            .map(|change| (end, change));
        let released = released.iter().map(|change| (release, *change));
        self.scheduled.extend(starts.chain(ends).chain(released));
        end
    }

    /// A copy to run ahead, nothing seen yet.
    fn ahead(&self) -> Self {
        Self {
            seen: Vec::new(),
            ..self.clone()
        }
    }
}

/// What a test does again to a fresh netlist (`Run::log`).
#[cfg(test)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum Logged {
    Drive(Pin, Drive),
    /// The wiring's clocks set running.
    Clocks,
    /// RESET High.
    Release,
    /// The data bus read as a read's cycle ended, and what it read.
    Sample(u8),
}

/// How a run's reads were answered: by the model's own sample, the
/// board's horizon past it, or by a copy's.
#[cfg(test)]
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
struct Reads {
    exact: u64,
    copied: u64,
}

/// A netlist running as the board's design.
#[derive(Debug, Clone)]
pub(super) struct Executed {
    /// The tick the model's time starts on.
    origin: Tick,
    /// The model's picoseconds a tick (`Wiring`).
    tick: u64,
    /// The sync field the board's PLL follows, if it has one (`Wiring`),
    /// the field as last written, and how far, the model's picoseconds,
    /// its steps have moved the model's time ahead of the board's.
    steps: Option<Steps>,
    field: u8,
    lead: i64,
    /// When RESET let the design go, if it has: AH's pulses from then on
    /// are intervals.
    released: Option<Picoseconds>,
    /// The model, at the board's time, `reached`, or past it.
    run: Run,
    /// The board's time in the model's, as far as it has come.
    reached: Picoseconds,
    /// A copy run ahead, while nothing has driven a pin since it was
    /// made.
    ahead: Option<Run>,
    /// How far, ticks, the copy runs ahead of the model at most.
    horizon: u64,
    /// The last internal 1 PPS reported, its rise.
    pulsed: Option<Picoseconds>,
    /// P81's last fall, an interval's start.
    started: Option<Picoseconds>,
    /// The interpolator's byte as last driven on P3 to P11.
    byte: u8,
    #[cfg(test)]
    reads: Reads,
}

impl Executed {
    /// `wired`'s design held in reset at the phase `at`, RESET Low.
    pub(super) fn held(wired: Wired, at: Phase) -> Self {
        let mut design = Self {
            origin: at.tick,
            tick: wired.wiring.tick,
            steps: wired.wiring.steps,
            field: 0,
            lead: 0,
            released: None,
            run: Run::new(wired),
            reached: Picoseconds::default(),
            ahead: None,
            horizon: AHEAD_TICKS,
            pulsed: None,
            started: None,
            byte: 0,
            #[cfg(test)]
            reads: Reads::default(),
        };
        design.catch_up(design.picoseconds(at));
        design.run_ahead();
        design
    }

    /// `wired`'s design as its reset at the phase `at` leaves it.
    pub(super) fn reset(wired: Wired, at: Phase) -> Self {
        let mut design = Self::held(wired, at);
        design.ahead = None;
        design.run.release();
        design.released = Some(design.run.now());
        design.run_ahead();
        design
    }

    /// The phase `at` in the model's time.
    fn picoseconds(&self, at: Phase) -> Picoseconds {
        let ticks = at.tick.0.saturating_sub(self.origin.0);
        // within the tick, to the picosecond
        let within = ((at.fraction * self.tick as f64).round() as u64).min(self.tick - 1);
        // saturating: a horizon may be far off
        Picoseconds(
            ticks
                .saturating_mul(self.tick)
                .saturating_add(within)
                .saturating_add_signed(self.lead),
        )
    }

    /// The tick of the model's time `at`: the rising edge at or before it.
    fn tick(&self, at: Picoseconds) -> Tick {
        self.origin + at.0.saturating_add_signed(-self.lead) / self.tick
    }

    /// A span of the model's time, `span` of its picoseconds, in seconds
    /// of the board's oscillator.
    fn seconds(&self, span: u64) -> f64 {
        span as f64 * TICK as f64 / self.tick as f64 / PS_PER_SECOND as f64
    }

    /// The next internal 1 PPS the model has seen past the board's time,
    /// or else the copy ahead, and the board has not been told of, if
    /// any.
    fn pulse_ahead(&self) -> Option<Picoseconds> {
        let model = self
            .run
            .seen
            .iter()
            .filter(|transition| transition.at > self.reached);
        let ahead = self.ahead.iter().flat_map(|ahead| &ahead.seen);
        model.chain(ahead).find_map(|transition| {
            (transition.probe == Probe::Pin(PPS)
                && transition.high
                && self.pulsed.is_none_or(|pulsed| transition.at > pulsed))
            .then_some(transition.at)
        })
    }

    /// The next tick the design acts on by itself: its next internal
    /// 1 PPS, if the copy ahead has seen it, or else where the copy has
    /// run to, from which it runs on.
    pub(super) fn next_event(&self) -> Option<Tick> {
        let ahead = self.ahead.as_ref()?;
        Some(self.tick(self.pulse_ahead().unwrap_or(ahead.now())))
    }

    /// The copy ahead run on, a tick at a time, until it sees a pulse
    /// the board has not been told of or is its horizon past the model.
    fn run_ahead(&mut self) {
        let limit = Picoseconds((self.reached.0 / self.tick + self.horizon) * self.tick);
        if self.ahead.is_none() {
            self.ahead = Some(self.run.ahead());
        }
        while self.pulse_ahead().is_none()
            && let Some(ahead) = self.ahead.as_mut()
            && ahead.now() < limit
        {
            ahead.step_to_change(limit);
        }
    }

    /// The board's time moved on to `at`, and the model with it unless
    /// it is there already: the copy ahead taken over if it has not run
    /// past, the horizon grown, else the model run itself, what the copy
    /// saw up to there seen by the model.
    fn catch_up(&mut self, at: Picoseconds) {
        if at <= self.reached {
            return;
        }
        self.reached = at;
        if at <= self.run.now() {
            return;
        }
        match self.ahead.take() {
            Some(ahead) if ahead.now() <= at => {
                let seen = std::mem::replace(&mut self.run, ahead).seen;
                self.run.seen.splice(0..0, seen);
                self.horizon = (self.horizon * AHEAD_GROWTH).min(AHEAD_TICKS);
            }
            ahead => {
                self.ahead = ahead.map(|mut ahead| {
                    ahead.seen.retain(|transition| transition.at > at);
                    ahead
                });
            }
        }
        self.run.step(at);
    }

    /// The design moved on to the phase `now`: the pulses and intervals
    /// on the way, and a pulse seen ahead on `now`'s tick, its rise a
    /// nanosecond or two past the edge.  What the model saw past the
    /// board's time it keeps until the board gets there.
    pub(super) fn run_to(&mut self, now: Phase) -> Ran {
        self.catch_up(self.picoseconds(now));
        let mut ran = Ran::default();
        let reached = self.reached;
        let (past, ahead) = std::mem::take(&mut self.run.seen)
            .into_iter()
            .partition(|transition| transition.at <= reached);
        self.run.seen = ahead;
        for transition in past {
            match (transition.probe, transition.high) {
                (Probe::Pin(PPS), true) => self.pulse(transition.at, &mut ran),
                (Probe::Pin(INTERVAL), false) => self.started = Some(transition.at),
                (Probe::Pin(INTERVAL), true) => {
                    let start = self.started.take();
                    if let Some(start) = start
                        && self.released.is_some_and(|released| start > released)
                        && transition.at.0 - start.0 > GLITCH
                    {
                        ran.interval = Some(self.seconds(transition.at.0 - start.0));
                    }
                }
                _ => {}
            }
        }
        if let Some(pulse) = self.pulse_ahead()
            && self.tick(pulse) <= now.tick
        {
            self.pulse(pulse, &mut ran);
        }
        self.run_ahead();
        ran
    }

    /// The pulse rising at `at`, unless the board has been told of it.
    fn pulse(&mut self, at: Picoseconds, ran: &mut Ran) {
        if self.pulsed.is_none_or(|pulsed| at > pulsed) {
            self.pulsed = Some(at);
            ran.pulse = Some(self.tick(at));
        }
    }

    /// The model at the phase `at`, for a pin to be driven: the copy
    /// ahead no longer its future.
    fn driven_at(&mut self, at: Phase) {
        self.catch_up(self.picoseconds(at));
        self.ahead = None;
        self.horizon = FIRST_AHEAD_TICKS;
    }

    /// The GPS 1 PPS rising at the phase `at`.  Its interval comes as
    /// the model runs.
    pub(super) fn gps_pps(&mut self, at: Phase) -> Option<f64> {
        self.gps_level(at, true);
        None
    }

    /// The GPS 1 PPS falling at the phase `at`.
    pub(super) fn gps_pps_fall(&mut self, at: Phase) {
        self.gps_level(at, false);
    }

    fn gps_level(&mut self, at: Phase, high: bool) {
        self.driven_at(at);
        self.run.drive(GPS, level(high));
        self.run_ahead();
    }

    /// The port's read of `register` at the phase `now`, with the
    /// interpolator's byte `pins` on P3 to P11, nothing reaching the part
    /// before the phase `horizon` if the board knows one.  The data bus
    /// is sampled as the read's cycle ends: by the model itself, if that
    /// is before `horizon`, else by a copy run through the cycle, which
    /// is then the copy ahead.
    pub(super) fn read(
        &mut self,
        register: u32,
        now: Phase,
        pins: u8,
        horizon: Option<Phase>,
    ) -> u8 {
        self.driven_at(now);
        let byte = (pins != self.byte).then(|| bits(&BYTE, u32::from(pins)));
        self.byte = pins;
        let starts = byte
            .into_iter()
            .flatten()
            .chain(bits(&ADDRESS, register))
            .chain([Change::Drive(READ_WRITE, level(true))]);
        let end = self.run.bus_cycle(starts, &[Change::Sample], &[]);
        let read = if horizon.is_some_and(|horizon| self.before(end, horizon)) {
            #[cfg(test)]
            {
                self.reads.exact += 1;
            }
            self.run.step(end);
            self.run.sampled
        } else {
            #[cfg(test)]
            {
                self.reads.copied += 1;
            }
            let mut ahead = self.run.ahead();
            ahead.step(end);
            let read = ahead.sampled;
            self.ahead = Some(ahead);
            read
        };
        self.run_ahead();
        read
    }

    /// Whether the model's time `at` is surely before the board's phase
    /// `horizon`, whatever comes after: by a tick's margin, since a sync
    /// step written later moves the board's time in the model's back a
    /// tick (`follow`), and the phase's picosecond is rounded.
    fn before(&self, at: Picoseconds, horizon: Phase) -> bool {
        at.0 + self.tick < self.picoseconds(horizon).0
    }

    /// `value` written to the register `register` at the phase `now`.
    /// Its interval, if it starts one, comes as the model runs.
    pub(super) fn write(&mut self, register: u32, value: u8, now: Phase) -> Option<f64> {
        self.driven_at(now);
        let starts = bits(&ADDRESS, register)
            .chain([Change::Drive(READ_WRITE, level(false))])
            .chain(bits(&DATA, u32::from(value)));
        let released: Vec<Change> = DATA
            .iter()
            .map(|pin| Change::Drive(*pin, Drive::Pull(true)))
            .chain([Change::Drive(READ_WRITE, level(true))])
            .collect();
        self.run.bus_cycle(starts, &[], &released);
        self.follow(register, value);
        self.run_ahead();
        None
    }

    /// The PLL following a step of the divider it locks to, if `value`
    /// written to `register` makes one: the model's time a tick ahead of
    /// the board's for a step earlier, a tick behind for one later, where
    /// it stands still until the board's time comes.
    fn follow(&mut self, register: u32, value: u8) {
        let Some(steps) = self.steps.filter(|steps| steps.register == register) else {
            return;
        };
        let field = value & steps.mask;
        if std::mem::replace(&mut self.field, field) != 0 {
            return;
        }
        let tick = i64::try_from(self.tick).expect("a tick within i64");
        if field == steps.earlier {
            self.lead += tick;
        } else if field == steps.later {
            self.lead -= tick;
        }
    }
}

/// `pins` driven to `value`, bit 0 on the first.
fn bits(pins: &'static [Pin], value: u32) -> impl Iterator<Item = Change> {
    pins.iter()
        .enumerate()
        .map(move |(bit, pin)| Change::Drive(*pin, level(value >> bit & 1 != 0)))
}

#[cfg(test)]
mod tests {
    use std::sync::LazyLock;

    use super::super::super::clock::Clock;
    use super::super::super::clock::Ideal;
    use super::super::super::clock::Phase;
    use super::super::super::clock::Tick;
    use super::super::super::time::CPU_HZ;
    use super::super::super::time::Cycles;
    use super::super::fixture::Case;
    use super::super::fixture::cases;
    use super::super::fixture::hex;
    use super::super::lca::Lca;
    use super::super::lca::Netlist;
    use super::super::pins::ACLK_ALONE;
    use super::super::pins::DATA;
    use super::super::pins::TEN_MHZ_ALONE;
    use super::super::pins::Wiring;
    use super::Executed;
    use super::Logged;
    use super::Reads;
    use super::Wired;
    use crate::run::SLICE;

    /// The Z3816A's netlist, whose wiring follows its sync field's steps.
    static Z3816A: LazyLock<Netlist> = LazyLock::new(|| {
        Netlist::parse(include_str!("z3816a.netlist")).expect("the Z3816A's netlist")
    });
    static Z3805A: LazyLock<Netlist> = LazyLock::new(|| {
        Netlist::parse(include_str!("z3805a.netlist")).expect("the Z3805A's netlist")
    });
    static FPGA_58503B: LazyLock<Netlist> = LazyLock::new(|| {
        Netlist::parse(include_str!("58503b.netlist")).expect("the 58503B's netlist")
    });

    /// A design as a board wires it, and the fixture whose cases drive it.
    struct Bench {
        netlist: &'static LazyLock<Netlist>,
        wiring: &'static Wiring,
        fixture: &'static str,
    }

    impl Bench {
        fn wired(&self) -> Wired {
            Wired {
                netlist: self.netlist,
                wiring: self.wiring,
            }
        }
    }

    /// Each wiring a board gives a netlist: the 10 MHz alone, on the
    /// Z3805A's design and the 58503B's, and ACLK with the sync field's
    /// steps, the Z3816A's.
    static BENCHES: [Bench; 3] = [
        Bench {
            netlist: &Z3805A,
            wiring: &TEN_MHZ_ALONE,
            fixture: include_str!("z3805a.fixture"),
        },
        Bench {
            netlist: &FPGA_58503B,
            wiring: &TEN_MHZ_ALONE,
            fixture: include_str!("58503b.fixture"),
        },
        Bench {
            netlist: &Z3816A,
            wiring: &ACLK_ALONE,
            fixture: include_str!("z3816a.fixture"),
        },
    ];

    /// The cases short enough to run unoptimized, as lca/tests.rs's
    /// `SHORT_CASES`: the register port, the read multiplexer and the
    /// preset, and mode 3's references.
    const CASES: [&str; 2] = ["registers", "references"];

    /// differential.py's GPS 1 PPS width, a tenth of a second.
    const GPS_WIDTH: Cycles = Cycles(CPU_HZ / 10);

    /// Where the board cuts its runs, cycles, as the horizon then has
    /// them: from a few of the bus's cycles, through the host's slice, to
    /// never; and first no horizon at all, every read from a copy.
    const QUANTA: [Option<u64>; 4] = [None, Some(64), Some(SLICE.0), Some(u64::MAX)];

    /// What a case's stimulus does to the design, as the board makes it.
    #[derive(Debug, Clone, Copy, PartialEq)]
    enum Stimulus {
        Release,
        Pins(u8),
        Write(u32, u8),
        Read,
        GpsRise,
        GpsFall,
    }

    /// `case`'s stimulus in time order, cycles of the board's.
    fn stimulus(case: &Case) -> Vec<(Cycles, Stimulus)> {
        let mut stimulus = Vec::new();
        for (at, words) in &case.operations {
            let at = Cycles(*at);
            let words: Vec<&str> = words.iter().map(String::as_str).collect();
            match words.as_slice() {
                ["reset"] => stimulus.push((at, Stimulus::Release)),
                ["pins", value] => stimulus.push((at, Stimulus::Pins(hex(value)))),
                ["write", register, value] => stimulus.push((
                    at,
                    Stimulus::Write(register.parse().expect("a register"), hex(value)),
                )),
                ["read", _] => stimulus.push((at, Stimulus::Read)),
                ["gps"] => {
                    stimulus.push((at, Stimulus::GpsRise));
                    stimulus.push((Cycles(at.0 + GPS_WIDTH.0), Stimulus::GpsFall));
                }
                _ => {}
            }
        }
        // stable: an instant's in the case's order
        stimulus.sort_by_key(|(at, _)| *at);
        stimulus
    }

    /// What the board saw of the design: each read, each pulse and
    /// interval as it ran, and its next event after each step.
    #[derive(Debug, Default, PartialEq)]
    struct Seen {
        reads: Vec<u8>,
        ran: Vec<(Cycles, String)>,
        events: Vec<Option<Tick>>,
    }

    /// `case` on `bench` as a board makes it, the run cut every `quantum`
    /// cycles: each read's horizon the next cut or the next GPS edge,
    /// whichever comes first, or none.  The design as it ends, and what
    /// the board saw.
    fn board(bench: &Bench, case: &Case, quantum: Option<u64>) -> (Executed, Seen) {
        let stimulus = stimulus(case);
        let mut seen = Seen::default();
        let mut design: Option<Executed> = None;
        let mut pins = 0;
        let mut called = Cycles(0);
        for (index, (at, change)) in stimulus.iter().copied().enumerate() {
            let Some(running) = design.as_mut() else {
                match change {
                    Stimulus::Release => {
                        design = Some(Executed::reset(bench.wired(), Ideal.phase(at)))
                    }
                    Stimulus::Pins(value) => pins = value,
                    _ => {}
                }
                continue;
            };
            // the board's run to the design's events on the way
            while let Some(next) = running.next_event().map(|tick| Ideal.reaching(tick))
                && next > called
                && next <= at
            {
                called = next;
                let ran = running.run_to(Ideal.phase(next));
                seen.ran.push((next, format!("{ran:?}")));
                seen.events.push(running.next_event());
            }
            let ran = running.run_to(Ideal.phase(at));
            seen.ran.push((at, format!("{ran:?}")));
            let now = Ideal.phase(at);
            match change {
                Stimulus::Release => panic!("{}: a second reset", case.name),
                Stimulus::Pins(value) => pins = value,
                Stimulus::Write(register, value) => {
                    running.write(register, value, now);
                }
                Stimulus::Read => {
                    let edge = stimulus[index + 1..]
                        .iter()
                        .find(|(_, later)| matches!(later, Stimulus::GpsRise | Stimulus::GpsFall))
                        .map(|(edge, _)| edge.0);
                    let horizon = quantum.map(|quantum| {
                        let cut = (at.0 / quantum).saturating_add(1).saturating_mul(quantum);
                        let horizon = edge.map_or(cut, |edge| edge.min(cut));
                        // the last cycle before it, as the part takes it
                        Ideal.phase(Cycles(horizon.saturating_sub(1)))
                    });
                    seen.reads.push(running.read(0, now, pins, horizon));
                }
                Stimulus::GpsRise => {
                    running.gps_pps(now);
                }
                Stimulus::GpsFall => running.gps_pps_fall(now),
            }
            let ran = running.run_to(now);
            seen.ran.push((at, format!("{ran:?}")));
            seen.events.push(running.next_event());
            called = called.max(at);
        }
        let mut design = design.unwrap_or_else(|| panic!("{}: no reset", case.name));
        let end = stimulus.last().map_or(Cycles(0), |(at, _)| *at);
        design.run_to(Ideal.phase(end));
        (design, seen)
    }

    /// The bench's case `name`.
    fn case(bench: &Bench, name: &str) -> Case {
        cases(bench.fixture)
            .into_iter()
            .find(|case| case.name == name)
            .unwrap_or_else(|| panic!("no case {name}"))
    }

    /// `case` answered alike however the run is cut, with no horizon
    /// too, and with no horizon every read from a copy.
    fn alike(bench: &Bench, case: &Case) -> Vec<Reads> {
        let runs: Vec<(Executed, Seen)> = QUANTA
            .iter()
            .map(|quantum| board(bench, case, *quantum))
            .collect();
        let (copied, reference) = &runs[0];
        assert_eq!(
            copied.reads.exact, 0,
            "{}: no horizon, no exact read",
            case.name
        );
        for ((design, seen), quantum) in runs.iter().zip(QUANTA) {
            assert_eq!(seen, reference, "{}: cut every {quantum:?}", case.name);
            assert!(
                design.run.lca.snapshot() == copied.run.lca.snapshot(),
                "{}: cut every {quantum:?}, the netlist's state",
                case.name
            );
        }
        runs.iter().map(|(design, _)| design.reads).collect()
    }

    /// The same reads, pulses, intervals and next events whatever the
    /// run's cuts, with every read sampled exactly where nothing comes
    /// first, and from a copy where something might.
    #[test]
    fn a_reads_answer_is_the_same_wherever_the_run_is_cut() {
        std::thread::scope(|scope| {
            for bench in &BENCHES {
                for name in CASES {
                    scope.spawn(move || {
                        let reads = alike(bench, &case(bench, name));
                        let uncut = reads.last().expect("a run never cut");
                        assert!(uncut.exact > 0, "{name}: some read exact");
                        assert!(reads[1].copied > 0, "{name}: some read past a short cut");
                    });
                }
            }
        });
    }

    /// Every case, the GPS 1 PPS's among them, optimized.
    #[test]
    #[ignore = "seconds of every case on every wiring: minutes, optimized"]
    fn a_reads_answer_is_the_same_wherever_the_run_is_cut_in_every_case() {
        std::thread::scope(|scope| {
            for bench in &BENCHES {
                for case in cases(bench.fixture) {
                    scope.spawn(move || alike(bench, &case));
                }
            }
        });
    }

    /// What the model did to the netlist, done again to a fresh one
    /// clocked edge by edge: each sample as the model's own, and every
    /// read sampled exactly answered as the model sampled.
    fn exact_as_clocked(bench: &Bench, case: &Case) {
        let (design, seen) = board(bench, case, Some(u64::MAX));
        let mut lca = Lca::new(bench.netlist);
        let mut samples = Vec::new();
        for (at, logged) in &design.run.log {
            if *at > lca.now() {
                lca.run_clocked(*at);
            }
            match *logged {
                Logged::Drive(pin, drive) => lca.drive(pin, drive).expect("a wired pin"),
                Logged::Clocks => {
                    for clock in bench.wiring.clocks {
                        lca.add_clock(*clock).expect("a clock's pin");
                    }
                }
                Logged::Release => lca.set_reset(true),
                Logged::Sample(sampled) => {
                    let clocked = DATA.iter().enumerate().fold(0u8, |byte, (bit, pin)| {
                        byte | u8::from(lca.pin(*pin).expect("a data pin")) << bit
                    });
                    assert_eq!(clocked, sampled, "{}: at {at:?}", case.name);
                    samples.push(sampled);
                }
            }
        }
        assert_eq!(
            seen.reads, samples,
            "{}: the reads, the model's samples",
            case.name
        );
    }

    #[test]
    fn exact_sampling_is_the_netlist_clocked() {
        for bench in &BENCHES {
            for name in CASES {
                exact_as_clocked(bench, &case(bench, name));
            }
        }
    }

    /// Every case, the GPS 1 PPS's among them, optimized.
    #[test]
    #[ignore = "every clock edge of seconds of every case: minutes, optimized"]
    fn exact_sampling_is_the_netlist_clocked_in_every_case() {
        std::thread::scope(|scope| {
            for bench in &BENCHES {
                for case in cases(bench.fixture) {
                    scope.spawn(move || exact_as_clocked(bench, &case));
                }
            }
        });
    }

    /// The tick `tick` on its edge.
    fn on(tick: u64) -> Phase {
        Phase {
            tick: Tick(tick),
            fraction: 0.0,
        }
    }

    /// A write of the sync field from 0 to 1 moves the model's time a
    /// tick ahead of the board's, from 0 to 2 a tick behind, once each:
    /// a field written again, or to 3, or another register, does not.
    #[test]
    fn a_sync_field_step_moves_the_models_time_a_tick() {
        const START: u64 = 1000;
        let wired = Wired {
            netlist: &Z3816A,
            wiring: &ACLK_ALONE,
        };
        let mut design = Executed::held(wired, on(START));
        let unmoved = |design: &Executed, tick: u64| {
            design.picoseconds(on(tick)).0 - (tick - START) * design.tick
        };
        let writes: [(u32, u8, i64); 9] = [
            (3, 0x00, 0),
            (3, 0x01, 1),
            (3, 0x01, 1),
            (3, 0x00, 1),
            (2, 0x01, 1),
            (3, 0x02, 0),
            (3, 0x00, 0),
            (3, 0x03, 0),
            (3, 0x00, 0),
        ];
        for (index, (register, value, ticks)) in writes.into_iter().enumerate() {
            let tick = START + 10 * (index as u64 + 1);
            design.write(register, value, on(tick));
            let after = tick + 5;
            assert_eq!(
                unmoved(&design, after) as i64,
                ticks * design.tick as i64,
                "after writing {value:#04x} to {register}"
            );
            assert_eq!(design.tick(design.picoseconds(on(after))), Tick(after));
        }
        design.write(3, 0x02, on(START + 200));
        assert_eq!(design.lead, -(design.tick as i64));
    }
}
