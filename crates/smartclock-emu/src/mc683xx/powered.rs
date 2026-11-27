//! The board side of the run: one power-up's board, what it reaches by
//! name, what it carries to the next, and what is the power-up's own
//! (the alarm's level, the external 1 PPS's phase, the position trace's
//! next record).  A power cycle drops it and builds the next.  The run
//! side, the runner (`runner.rs`), keeps what outlives a power cycle.

use std::fmt::Display;
use std::ops::ControlFlow;
use std::time::Duration;

use mc683xx_board::Board;
use mc683xx_board::BoardRun;
use mc683xx_board::BoardTracing;
use mc683xx_board::Chip;
use mc683xx_board::External;
use mc683xx_board::pacing::Pacing as BoardPacing;
use mc683xx_board::signal::Serial;
use mc683xx_board::signal::Signal;
use mc683xx_board::time::Span;
use mc683xx_board::time::Time;
use mc683xx_board::trace::Line as TraceLine;
use mc683xx_board::trace::Record;
use mc683xx_board::trace::header;
use mc683xx_cpu32::Stop;
use mc683xx_mc68331::Event;
use mc683xx_mc68331::Mc68331;
use mc683xx_modules::qsm::sci::Frame;
use mc683xx_modules::sim::ResetCause;
use mc683xx_target::Address;

use super::Error;
use super::KeptEeprom;
use super::carried::Carried;
use super::eeprom;
use super::endpoints::ALARM;
use super::endpoints::ALARM_BANK;
use super::endpoints::Bound;
use super::endpoints::GPS_PPS;
use super::endpoints::frame_bits;
use super::endpoints::frame_byte;
use super::load::Loaded;
use super::overrides::Parts;
use super::pps::PpsTrain;
use crate::board::analog::Warmth;
use crate::board::front_panel::Key;
use crate::board::participants::efc_monitor;
use crate::board::participants::front_panel;
use crate::board::participants::gps;
use crate::board::participants::ocxo;
use crate::board::participants::sources;
use crate::board::registry;
use crate::board::system_clock::board_cycles;
use crate::board::system_clock::mc683xx_cycle;
use crate::board::time::Cycles;
use crate::config::RunConfig;
use crate::config::TraceConfig;
use crate::config::Traced;
use crate::control::Control;
use crate::gps::external::PulseClock;
use crate::indicators::Change;
use crate::panel::state::Controls;
use crate::panel::state::Drawing;
use crate::panel::state::Snapshot;
use crate::ports::GPS;
use crate::run::Pacing;
use crate::trace;
use crate::trace::POSITION_EVERY;

/// Board time `at` on the two clocks the run's records count: the
/// chip's system clocks since the power-up, and since the run's start.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(super) struct Clocks {
    /// Since the power-up, as the models count it.
    pub(super) cycle: Cycles,
    /// Since the run's start, as a script counts it.
    pub(super) run: Cycles,
}

/// What a control did to the board.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(super) enum Applied {
    /// It was applied.
    Yes,
    /// Nothing on the board takes it.
    Not,
    /// It is a power cycle, which only the run can give: the board is
    /// built again.
    PowerCycle,
}

/// One power-up's board.
#[derive(Debug)]
pub(super) struct Powered {
    /// The board.
    board: Board<Mc68331>,
    /// The rest of the power-up.
    up: PowerUp,
}

/// What a power-up has beside its board.
#[derive(Debug)]
struct PowerUp {
    /// The parts the board reaches by name.
    parts: Parts,
    /// What it carries to the next, as it stands.
    carried: Carried,
    /// The alarm output's level as last reported, reported with
    /// `--indicators`.
    alarm: Option<bool>,
    /// The external engine's 1 PPS, when the board has the `gps_pps`
    /// endpoint.
    gps_pps: Option<PpsTrain>,
    /// Where the CPU is, traced every `POSITION_EVERY` from this.
    next_position: Option<Time>,
}

/// A power-up's board and the rest of it, borrowed apart: the board as
/// a frame's quanta are handed it (`Board::run_frame_with`), the rest
/// as the power-up has it.
#[derive(Debug)]
pub(super) struct Live<'a> {
    /// The board.
    board: &'a mut Board<Mc68331>,
    /// The rest.
    up: &'a mut PowerUp,
}

impl Powered {
    /// The board `config` asks for, built from `loaded`, powered up
    /// with what `carried` carries, its tracing as `config` asks, paced
    /// to the host's clock unless `--deterministic`; with
    /// `--skip-warm-up` its warm-up at the curve's settled end.
    pub(super) fn up(config: &RunConfig, loaded: &Loaded, carried: Carried) -> Result<Self, Error> {
        let carried = if config.skip_warm_up {
            Carried {
                warmth: Warmth::Settled,
                ..carried
            }
        } else {
            carried
        };
        let (board, parts) = loaded.build(config, &carried)?;
        if config.pacing == Pacing::Wall {
            board.control().set_pacing(BoardPacing::RealTime);
        }
        let mut powered = Self {
            board,
            up: PowerUp {
                parts,
                carried,
                alarm: None,
                gps_pps: None,
                next_position: None,
            },
        };
        powered.live().trace(&config.trace);
        Ok(powered)
    }

    /// The board powered off and on: built again from `loaded`, its file
    /// as the run read it at its start, with what it carries (the
    /// antenna, the warm-up, the oscillator's crystal, the run's clock,
    /// the EEPROM's cells and protection when it is in memory).  The old board goes
    /// first, its EEPROM's write cycle in flight lost as a power cut
    /// loses it, and its lock on the working file with it.
    pub(super) fn power_cycled(self, config: &RunConfig, loaded: &Loaded) -> Result<Self, Error> {
        let carried = self
            .up
            .carried
            .clone()
            .through_power_cycle(&self.board, &self.up.parts)?;
        let pulsed = self.up.gps_pps.is_some();
        drop(self);
        let powered = Self::up(config, loaded, carried)?;
        Ok(if pulsed {
            powered.pulsed(config.pacing)
        } else {
            powered
        })
    }

    /// The board with the external engine's 1 PPS, for its `gps_pps`
    /// endpoint: on the run's whole seconds, or in a run paced to the
    /// wall clock, the host's (`gps::external`).
    pub(super) fn pulsed(mut self, pacing: Pacing) -> Self {
        let (now, run) = {
            let live = self.live();
            (live.now(), live.run_time())
        };
        self.up.gps_pps = Some(PpsTrain::new(PulseClock::of(pacing), now, run));
        self
    }

    /// The board and the rest, borrowed apart.
    pub(super) fn live(&mut self) -> Live<'_> {
        Live {
            board: &mut self.board,
            up: &mut self.up,
        }
    }

    /// A frame of the board's run (`Board::run_frame_with`): to the board
    /// time due by the host's clock reading `wall`, no more than
    /// `most`, `before` given the power-up before each quantum.
    /// A part that stalls it ends the run, naming it.
    pub(super) fn run_frame(
        &mut self,
        wall: Duration,
        most: Span,
        mut before: impl FnMut(&mut Live<'_>) -> ControlFlow<()>,
    ) -> Result<BoardRun, Error> {
        let Self { board, up } = self;
        board
            .run_frame_with(wall, most, |board| before(&mut Live { board, up }))
            .map_err(Error::Stalled)
    }
}

impl Live<'_> {
    /// The board's run-time endpoints.
    pub(super) fn externals(&self) -> Vec<External> {
        self.board.externals()
    }

    /// Board time now, since the power-up.
    pub(super) fn now(&self) -> Time {
        self.board.now()
    }

    /// Board time `at` as the models count it: the chip's system clocks
    /// since the board's power-up.
    pub(super) fn cycles(&self, at: Time) -> Cycles {
        Cycles(self.board.system_clock().cycle(at).clocks())
    }

    /// Board time `at` on the run's clock, as a script counts it: the
    /// chip's system clocks since the run's start.
    pub(super) fn run_cycles(&self, at: Time) -> Cycles {
        self.up.carried.epoch.cycles + self.cycles(at)
    }

    /// Now on both clocks.
    pub(super) fn clocks(&self) -> Clocks {
        let now = self.now();
        Clocks {
            cycle: self.cycles(now),
            run: self.run_cycles(now),
        }
    }

    /// The models' `at` on board time.
    pub(super) fn time(&self, at: Cycles) -> Time {
        self.board.system_clock().time(mc683xx_cycle(at))
    }

    /// The run's time `at`, the chip's clocks since the run's start, on
    /// this power-up's board time: its start if it is before it.
    pub(super) fn time_of_run(&self, at: Cycles) -> Time {
        self.time(Cycles(at.0.saturating_sub(self.up.carried.epoch.cycles.0)))
    }

    /// Board time since the run's start, `at`, on this power-up's.
    pub(super) fn since_power_up(&self, at: Time) -> Time {
        Time::ZERO + (at - self.up.carried.epoch.time)
    }

    /// Board time now since the run's start.
    pub(super) fn run_time(&self) -> Time {
        self.now() + self.up.carried.epoch.time.since_power_on()
    }

    /// The external 1 PPS's next edge, if it has one due.
    pub(super) fn next_pps(&self) -> Option<Time> {
        self.up.gps_pps.as_ref().and_then(PpsTrain::next)
    }

    /// The board run to `target`: a part that stalls it ends the run,
    /// naming it.
    pub(super) fn run_until(&mut self, target: Time) -> Result<BoardRun, Error> {
        self.board.run_until(target).map_err(Error::Stalled)
    }

    /// Where the CPU is.
    pub(super) fn pc(&self) -> u32 {
        self.board.chip().cpu().registers.pc.0
    }

    /// The stopped record of `outcome`, or of the board's time if it
    /// stopped between runs, on standard error.
    pub(super) fn report_stopped(&self, outcome: Option<BoardRun>) {
        let stopped = outcome.unwrap_or(BoardRun::new(self.board.now(), Stop::Budget));
        trace_line(&self.board.stopped_record(stopped));
    }

    /// What the lines' far ends sent, onto the board's endpoints, and
    /// the external engine's 1 PPS.
    pub(super) fn feed(&mut self, lines: &[Bound]) -> Result<(), Error> {
        let now = self.board.now();
        for bound in lines {
            while let Some(byte) = bound.line.receive() {
                self.board
                    .send(
                        bound.name,
                        Signal::Serial(Serial::untimed(Frame::Data(frame_bits(
                            bound.framing,
                            byte,
                        )))),
                    )
                    .map_err(|source| Error::Send {
                        name: bound.name,
                        source,
                    })?;
            }
        }
        if let Some(train) = &mut self.up.gps_pps {
            for high in train.edges(now) {
                self.board
                    .send(GPS_PPS, Signal::Level(high))
                    .map_err(|source| Error::Send {
                        name: GPS_PPS,
                        source,
                    })?;
            }
        }
        Ok(())
    }

    /// What reached the board's endpoint of `bound`, onto its line: the
    /// bytes sent.
    pub(super) fn drain(&mut self, bound: &Bound) -> Result<Vec<u8>, Error> {
        let mut sent = Vec::new();
        for (_, signal) in self.board.take_received(bound.name)? {
            if let Signal::Serial(Serial {
                frame: Frame::Data(data),
                ..
            }) = signal
            {
                let byte = frame_byte(bound.framing, data);
                bound.line.transmit(byte);
                sent.push(byte);
            }
        }
        if bound.name == GPS
            && let Some(train) = &mut self.up.gps_pps
        {
            train.heard(&sent);
        }
        Ok(sent)
    }

    /// The alarm output's changes, reported when `indicators`.
    pub(super) fn report_alarm(&mut self, indicators: bool) -> Result<(), Error> {
        for (at, signal) in self.board.take_received(ALARM)? {
            if let Signal::Level(level) = signal
                && self.up.alarm != Some(level)
            {
                // low while the alarm is on (58503a-3704.md)
                let lit = !level;
                if indicators && (self.up.alarm.is_some() || lit) {
                    trace::emit(&Change {
                        at: self.cycles(at),
                        bank: ALARM_BANK,
                        name: ALARM,
                        lit,
                    });
                }
                self.up.alarm = Some(level);
            }
        }
        Ok(())
    }

    /// The chip's events worth a line: a reset, and a device's DSACK
    /// pins disagreeing with PEPAR or its select (the board file's
    /// `dsack`).  Whether the chip reset.
    pub(super) fn report_events(&mut self) -> bool {
        self.board
            .take_events()
            .into_iter()
            .fold(false, |reset, event| match event {
                Event::Reset { now, cause, .. } => {
                    trace::note(format_args!("reset cycle={} cause={cause:?}", now.clocks()));
                    true
                }
                Event::Dsack { now, fault, .. } => {
                    trace::note(format_args!("dsack cycle={} fault={fault:?}", now.clocks()));
                    reset
                }
                _ => reset,
            })
    }

    /// What the firmware sent the synthetic GPS engine since the last
    /// call, taken from the part either way, so that it does not pile up
    /// there.
    pub(super) fn take_gps_heard(&mut self) -> Vec<u8> {
        self.up
            .parts
            .gps
            .as_deref()
            .map(|name| gps::take_heard(self.board, name))
            .unwrap_or_default()
    }

    /// `bytes` onto the synthetic GPS engine's line, after what it has
    /// sent.
    pub(super) fn inject_gps(&mut self, bytes: &[u8]) {
        if let Some(name) = &self.up.parts.gps
            && !bytes.is_empty()
        {
            gps::inject(self.board, name, bytes);
        }
    }

    /// The EEPROM as it stands, when it is in memory.
    pub(super) fn eeprom(&self) -> Result<Option<KeptEeprom>, Error> {
        self.up
            .parts
            .eeprom
            .as_ref()
            .map(|memory| eeprom::kept(self.board, &memory.name))
            .transpose()
    }

    /// `key` pressed on the front panel: whether the board has one.
    pub(super) fn press(&mut self, key: Key) -> bool {
        self.up
            .parts
            .front_panel
            .as_deref()
            .is_some_and(|name| front_panel::press(self.board, name, key))
    }

    /// `control` applied to the board, but for a power cycle, which
    /// the run gives; what it carries kept as the control leaves it.
    pub(super) fn apply(&mut self, control: Control) -> Applied {
        let applied = match control {
            Control::AntennaOff | Control::AntennaOn => {
                let connected = control == Control::AntennaOn;
                let applied = self
                    .up
                    .parts
                    .gps
                    .as_deref()
                    .is_some_and(|name| gps::set_antenna(self.board, name, connected));
                if applied {
                    self.up.carried.antenna = connected;
                }
                applied
            }
            Control::SkipWarmUp => {
                let ocxo = self
                    .up
                    .parts
                    .ocxo
                    .as_deref()
                    .is_some_and(|name| ocxo::settle(self.board, name));
                let temperature = self
                    .up
                    .parts
                    .temperature
                    .as_deref()
                    .is_some_and(|name| sources::settle_temperature(self.board, name));
                let efc_monitor = self
                    .up
                    .parts
                    .efc_monitor
                    .as_deref()
                    .is_some_and(|name| efc_monitor::settle(self.board, name));
                let settled = ocxo || temperature || efc_monitor;
                if settled {
                    self.up.carried.warmth = Warmth::Settled;
                }
                settled
            }
            Control::Reset => self.reset(),
            Control::PowerCycle => return Applied::PowerCycle,
        };
        if applied { Applied::Yes } else { Applied::Not }
    }

    /// A warm restart, as the SIM's software watchdog's time-out gives
    /// one (restart.md, "The watchdog"): RSR SW, the modules it resets
    /// reset, RESET driven for its 512 clocks.  While the board holds
    /// RESET low the chip is in reset already and this does nothing.
    /// Every board has the chip, so it always applies.
    fn reset(&mut self) -> bool {
        self.board.reset_for(ResetCause::Watchdog);
        true
    }

    /// What the panel shows now, drawn as `drawing`.
    pub(super) fn snapshot(&self, drawing: Drawing) -> Snapshot {
        let now = self.board.now();
        let cycle = self.cycles(now);
        let indicators = self
            .up
            .parts
            .indicators
            .iter()
            .filter_map(|name| registry::indicators(self.board, name, cycle))
            .flatten()
            .collect();
        Snapshot {
            drawing,
            cycle,
            seconds: self.run_time().secs_f64(),
            indicators,
            controls: Controls {
                antenna: self
                    .up
                    .parts
                    .gps
                    .is_some()
                    .then_some(self.up.carried.antenna),
                warming: self
                    .up
                    .parts
                    .ocxo
                    .is_some()
                    .then(|| self.up.carried.warmth.warming(board_cycles(now))),
            },
            display: self
                .up
                .parts
                .front_panel
                .as_deref()
                .and_then(|name| front_panel::display(self.board, name)),
        }
    }

    /// What a test compares of two runs' boards: the CPU's registers
    /// and the RAM.
    #[cfg(test)]
    pub(super) fn fingerprint(&self) -> String {
        format!(
            "{:?} {:?}",
            self.board.chip().cpu().registers,
            self.board.memory("ram")
        )
    }

    /// The board, for a test to look at.
    #[cfg(test)]
    pub(super) fn board(&self) -> &Board<Mc68331> {
        self.board
    }

    /// Whether the board has a front panel.
    pub(super) fn has_front_panel(&self) -> bool {
        self.up.parts.front_panel.is_some()
    }

    /// Sets the board's tracing as `trace` asks.
    fn trace(&mut self, trace: &TraceConfig) {
        let mut tracing = BoardTracing::default();
        tracing.bus_cycles = trace.traces(Traced::Devices) || !trace.watch.is_empty();
        tracing.exceptions = trace.traces(Traced::Exceptions);
        tracing.qspi = trace.traces(Traced::Qspi);
        tracing.pins = trace.traces(Traced::Pins);
        self.board.set_tracing(tracing);
        for &pc in &trace.watch_code {
            self.board.watch_code(Address(pc));
        }
        if trace.traces(Traced::Position) {
            self.up.next_position = Some(self.board.now());
        }
    }

    /// What the board traced, on standard error, its device accesses
    /// only at `watch`'s addresses if it names any, and where the CPU
    /// is, when traced.
    pub(super) fn write_trace(&mut self, watch: &[u32]) {
        let trace = self.board.take_trace();
        for record in trace.records {
            let watched = match &record {
                Record::BusCycle(device) => {
                    watch.is_empty()
                        || watch.iter().any(|&at| {
                            (device.address.0..device.address.0 + device.size.bytes()).contains(&at)
                        })
                }
                _ => true,
            };
            if watched {
                trace_line(&record);
            }
        }
        if let Some(at) = self.up.next_position
            && self.board.now() >= at
        {
            trace_line(&self.board.position_record());
            let next = self.cycles(self.board.now()) + POSITION_EVERY;
            self.up.next_position = Some(self.time(next));
        }
    }
}

/// The trace's header line, before its first record, when anything is
/// traced.
pub(super) fn trace_header(trace: &TraceConfig) {
    if trace.any() {
        trace::line(format_args!("{}", header(Mc68331::TYPE)));
    }
}

/// `record`'s line on standard error, prefixed with the chip's type.
fn trace_line(record: &impl Display) {
    trace::line(format_args!("{}", TraceLine::new(Mc68331::TYPE, record)));
}
