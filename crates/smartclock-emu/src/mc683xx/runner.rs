//! The run side of the mc683xx run: the board, a power-up at a time
//! (`Powered`), run between the turns of what outlives a power cycle:
//! the lines bound to the board's endpoints, what runs alongside on the
//! run's clock (the host port's script or standard input), the keys and
//! controls given and not yet applied, the pacing and the panel page.
//!
//! The host steps it (`Step`): deterministically, a slice of `SLICE` at
//! most at a time, each ending at the next of what runs alongside, the
//! run's end and the external 1 PPS's edges (`Runner::step_until`); or
//! paced, a frame at a time, to the board time due by the host's clock,
//! in mc683xx's quanta (`Runner::step_frame`).  Either way a slice's end,
//! or a quantum's, takes its steps in one order (`Side::slice_end`).

use std::cell::RefCell;
use std::collections::VecDeque;
use std::ops::ControlFlow;
use std::rc::Rc;
use std::time::Duration;

use mc683xx_board::signal::Kind;
use mc683xx_board::time::Span;
use mc683xx_board::time::Time;
use mc683xx_cpu32::Halt;
use mc683xx_cpu32::Stop;
use smartclock::types::Framing;

use super::Error;
use super::KeptEeprom;
use super::Step;
use super::endpoints::ALARM;
use super::endpoints::Bound;
use super::endpoints::GPS_PPS;
use super::load::Loaded;
use super::page::Page;
use super::powered::Applied;
use super::powered::Live;
use super::powered::Powered;
use super::powered::trace_header;
use super::time_code::TimeCodeOff;
use crate::board::front_panel::Key;
use crate::config::GpsSource;
use crate::config::RunConfig;
use crate::control::Control;
use crate::host::Ports;
use crate::host::host_side;
use crate::host::sci_line;
use crate::host::serve;
use crate::line::Line;
use crate::names::Named as _;
use crate::panel::state::Drawing;
use crate::ports::GPS;
use crate::ports::HOST;
use crate::ports::SCI;
use crate::run::Alongsides;
use crate::run::BEHIND;
use crate::run::Flow;
use crate::run::Halted;
use crate::run::SLICE;
use crate::run::no_front_panel;
use crate::script::Heard;
use crate::script::Injected;
use crate::script::ScriptWiring;
use crate::trace;

/// Why mc683xx's CPU halted, in words, as the Unicorn core gives its
/// reasons.
fn halt_reason(halt: Halt) -> String {
    match halt {
        Halt::DoubleBusFault => "a double bus fault".to_owned(),
        // a reason this runner does not know of, as mc683xx names it
        other => other.to_string(),
    }
}

/// Picoseconds a nanosecond.
const PICOSECONDS_PER_NANOSECOND: u64 = 1000;

/// `picoseconds` of board time as the host's clock counts it, rounded up
/// to its nanosecond, so that a frame given the reading this much after
/// another's runs at least that far.
fn wall_of(picoseconds: u64) -> Duration {
    Duration::from_nanos(picoseconds.div_ceil(PICOSECONDS_PER_NANOSECOND))
}

/// The run.
#[derive(Debug)]
pub(super) struct Runner {
    /// The board, this power-up's.
    powered: Powered,
    /// What outlives it.
    side: Side,
}

/// What outlives a power cycle.
#[derive(Debug)]
struct Side {
    /// What the run is.
    config: RunConfig,
    /// The board file as the run read it at its start, which each
    /// power-up's board is built from.
    loaded: Loaded,
    /// The endpoints bound to lines.
    lines: Vec<Bound>,
    /// Whether the board has the alarm output's endpoint.
    alarm_bound: bool,
    /// What runs off the board on the run's clock: the host port's
    /// script or standard input.
    alongside: Alongsides,
    /// The keys a script pressed, for the front panel.
    keys: Rc<RefCell<Vec<Key>>>,
    /// The controls a script gave, for the run.
    controls: Rc<RefCell<Vec<Control>>>,
    /// The controls given and not yet applied: those after a power
    /// cycle wait for the board it builds.
    pending: VecDeque<Control>,
    /// What the GPS engine was sent, for a script's expect-gps steps:
    /// none without a script, which nothing would read.
    gps_heard: Option<Heard>,
    /// What a script's write-gps steps put on the GPS engine's line.
    gps_injected: Injected,
    /// The panel page, with `--panel`.
    page: Option<Page>,
    /// `--time-code-off`'s command, where it stands in this power-up.
    time_code_off: TimeCodeOff,
    /// When the run stops by itself, board time since its start, if it
    /// does: `--stop-after`'s.
    until: Option<Time>,
    /// The host's clock reading the last frame was given and the board
    /// time it ended at, a point on the board's pacing, which runs a
    /// board second a wall second from it: none before this power-up's
    /// first frame, or after a deterministic step, either of which
    /// starts the pacing again.
    paced: Option<(Duration, Time)>,
    /// Whether the board has been reported behind the host's clock.
    behind: bool,
    /// Whether the run has ended.
    ended: bool,
}

/// What the end of a slice, or of a frame's quantum, leaves the run.
#[derive(Debug, Clone, PartialEq, Eq)]
struct SliceEnd {
    /// Whether it goes on, as what runs alongside says.
    flow: Flow,
    /// Whether a control given is a power cycle, the controls after it
    /// left for the board it builds.
    power_cycle: bool,
}

impl Runner {
    /// The run of `config` on `powered`, built from `loaded`, the board's
    /// endpoints bound to the lines `config` serves, their far ends
    /// `ports`'.
    pub(super) fn new(
        config: RunConfig,
        loaded: Loaded,
        powered: Powered,
        ports: &mut dyn Ports,
    ) -> Result<Self, Error> {
        let keys = Rc::new(RefCell::new(Vec::new()));
        let controls = Rc::new(RefCell::new(Vec::new()));
        // seconds of board time, to the picosecond
        let until = config
            .until
            .map(|until| Time::from_secs_f64(until.seconds()));
        let mut runner = Self {
            powered,
            side: Side {
                lines: Vec::new(),
                alarm_bound: false,
                alongside: Alongsides::default(),
                keys: Rc::clone(&keys),
                controls: Rc::clone(&controls),
                pending: VecDeque::new(),
                gps_heard: config.scripted().then(Heard::default),
                gps_injected: Injected::default(),
                page: None,
                time_code_off: TimeCodeOff::new(config.time_code_off),
                until,
                paced: None,
                behind: false,
                ended: false,
                config,
                loaded,
            },
        };
        for external in runner.powered.live().externals() {
            let side = &mut runner.side;
            let serial = external.kind == Kind::Serial;
            match external.name.as_str() {
                HOST if serial => {
                    let host = Line::new(HOST, side.config.tap);
                    let keys = Rc::clone(&keys);
                    let controls = Rc::clone(&controls);
                    let wiring = ScriptWiring {
                        keys: Some(Box::new(move |key| keys.borrow_mut().push(key))),
                        controls: Some(Box::new(move |control| {
                            controls.borrow_mut().push(control);
                        })),
                        gps_heard: side.gps_heard.as_ref().map(Rc::clone),
                        gps_injected: Some(Rc::clone(&side.gps_injected)),
                    };
                    if let Some(alongside) =
                        host_side(&side.config, ports, side.loaded.files(), &host, wiring)?
                    {
                        side.alongside.push(alongside);
                    }
                    side.bind(HOST, host, side.config.model.host_framing());
                }
                SCI if serial => {
                    let sci = sci_line(&side.config, ports)?;
                    serve(ports, side.config.sci_address(), sci.clone())?;
                    side.bind(SCI, sci, Framing::EightNone);
                }
                GPS if serial => {
                    let gps = Line::new(GPS, side.config.tap);
                    if let GpsSource::External(address) = &side.config.gps {
                        serve(ports, address, gps.clone())?;
                    }
                    side.bind(GPS, gps, Framing::EightNone);
                }
                GPS_PPS if external.kind == Kind::Edge && external.direction.drives() => {
                    runner.powered = runner.powered.pulsed(side.config.pacing);
                }
                // a level the board drives: reported, not bound
                ALARM if external.kind == Kind::Logic && !external.direction.drives() => {
                    side.alarm_bound = true;
                }
                other => trace::note(format_args!(
                    "the board's endpoint {other}, {} {:?}, is not bound",
                    external.kind, external.direction
                )),
            }
        }
        let side = &runner.side;
        if let Some(option) = side.config.sci_asked()
            && !side.lines.iter().any(|bound| bound.name == SCI)
        {
            return Err(Error::NoSci {
                option,
                board: side.config.board.clone(),
            });
        }
        trace_header(&side.config.trace);
        Ok(runner)
    }

    /// The EEPROM as it stands, when it is in memory.
    pub(super) fn eeprom(&mut self) -> Result<Option<KeptEeprom>, Error> {
        self.powered.live().eeprom()
    }

    /// The board, for a test to look at.
    #[cfg(test)]
    pub(super) fn live(&mut self) -> Live<'_> {
        self.powered.live()
    }

    /// The run serving the panel page as `--panel` asks, by `ports`,
    /// `drawing` drawn, its console named for the run's start,
    /// `started`.
    pub(super) fn with_panel(
        mut self,
        drawing: Drawing,
        ports: &mut dyn Ports,
        started: jiff::Timestamp,
    ) -> Result<Self, Error> {
        if let Some(address) = &self.side.config.panel {
            self.side.page = Some(Page::serve(
                address,
                ports,
                &self.powered.live(),
                drawing,
                &self.side.config.host,
                self.side.host(),
                started,
            )?);
        }
        Ok(self)
    }

    /// The run deterministically, a slice at a time, to `target`, board
    /// time since the run's start, or until it ends, or forever.
    pub(super) fn step_until(mut self, target: Option<Time>) -> Result<(Self, Step), Error> {
        if self.side.ended {
            return Ok((self, Step::Ended));
        }
        self.side.paced = None;
        loop {
            let mut live = self.powered.live();
            let now = live.now();
            let until = self.side.until.map(|until| live.since_power_up(until));
            if until.is_some_and(|until| now >= until) {
                return Ok((self.end(None), Step::Ended));
            }
            let bound = target.map(|target| live.since_power_up(target));
            if bound.is_some_and(|bound| now >= bound) {
                return Ok((
                    self,
                    Step::Running {
                        behind: Duration::ZERO,
                    },
                ));
            }
            self.side.feed(&mut live)?;
            let slice = self
                .side
                .events(&live)
                .chain(bound)
                .fold(live.time(live.cycles(now) + SLICE), Time::min)
                .max(now + Span::from_picos(1));
            let outcome = live.run_until(slice)?;
            let end = self.side.slice_end(&mut live, &outcome.stop)?;
            let (runner, flow) = self.finish_slice(end)?;
            self = runner;
            match outcome.stop {
                Stop::Budget | Stop::Stopped => {}
                // a hook, a stop asked, or one this runner does not
                // know of, ends the run
                _ => return Ok((self.end(Some(outcome)), Step::Ended)),
            }
            match flow {
                Flow::Continue => {}
                Flow::Stop => return Ok((self.end(Some(outcome)), Step::Ended)),
                Flow::Fail(why) => return Err(Error::Failed(why)),
            }
        }
    }

    /// A frame of the paced run, to the board time due by the host's
    /// clock reading `wall` (`Board::run_frame_with`), no more than
    /// `most` of board time, and no further than the next of the
    /// run's own times: what runs alongside's next step, the run's end
    /// and the external 1 PPS's next edge, each of which a frame ends at
    /// so that it is taken at its time.  Before each quantum, the last
    /// one's end (`Side::slice_end`) and the lines' bytes onto the
    /// board; a power cycle given ends the frame there, for the board
    /// built again.
    pub(super) fn step_frame(mut self, wall: Duration, most: Span) -> Result<(Self, Step), Error> {
        if self.side.ended {
            return Ok((self, Step::Ended));
        }
        let given = {
            let mut live = self.powered.live();
            let now = live.now();
            let until = self.side.until.map(|until| live.since_power_up(until));
            if until.is_some_and(|until| now >= until) {
                return Ok((self.end(None), Step::Ended));
            }
            // the edges due now out first, so that the next is the next
            self.side.feed(&mut live)?;
            let event = self.side.events(&live).filter(|&at| at > now).min();
            match (self.side.paced, event) {
                (Some((at, from)), Some(event)) => {
                    wall.min(at + wall_of(event.since(from).picos()))
                }
                _ => wall,
            }
        };
        let Self { powered, side } = &mut self;
        let mut first = true;
        let mut cut: Option<Result<SliceEnd, Error>> = None;
        let outcome = powered.run_frame(given, most, |live| {
            if !first {
                match side.slice_end(live, &Stop::Budget) {
                    Ok(end) if end.power_cycle || end.flow != Flow::Continue => {
                        cut = Some(Ok(end));
                        return ControlFlow::Break(());
                    }
                    Ok(_) => side.publish(live, false),
                    Err(error) => {
                        cut = Some(Err(error));
                        return ControlFlow::Break(());
                    }
                }
            }
            first = false;
            match side.feed(live) {
                Ok(()) => ControlFlow::Continue(()),
                Err(error) => {
                    cut = Some(Err(error));
                    ControlFlow::Break(())
                }
            }
        })?;
        let cut = cut.transpose()?;
        let cut_short = cut.is_some();
        let end = match cut {
            Some(end) => end,
            None => self
                .side
                .slice_end(&mut self.powered.live(), &outcome.stop)?,
        };
        let power_cycle = end.power_cycle;
        let (mut runner, flow) = self.finish_slice(end)?;
        runner.side.paced = (!power_cycle).then(|| (given, runner.powered.live().now()));
        if !cut_short && !matches!(outcome.stop, Stop::Budget | Stop::Stopped) {
            return Ok((runner.end(Some(outcome)), Step::Ended));
        }
        match flow {
            Flow::Continue => {}
            Flow::Stop => return Ok((runner.end(Some(outcome)), Step::Ended)),
            Flow::Fail(why) => return Err(Error::Failed(why)),
        }
        let behind = wall.saturating_sub(given);
        if !runner.side.behind && behind > BEHIND {
            runner.side.behind = true;
            behind_note(behind);
        }
        Ok((runner, Step::Running { behind }))
    }

    /// A slice's end, `end`, finished: each power cycle given, the board
    /// built again, before the controls after it, then the page
    /// published when due.  Whether the run goes on, as what runs
    /// alongside said.
    fn finish_slice(self, end: SliceEnd) -> Result<(Self, Flow), Error> {
        let mut runner = self;
        let mut power_cycle = end.power_cycle;
        while power_cycle {
            runner = runner.power_cycled()?;
            power_cycle = runner.side.apply_controls(&mut runner.powered.live());
        }
        runner.side.publish(&runner.powered.live(), false);
        Ok((runner, end.flow))
    }

    /// The run ended, `outcome` how, if the board stopped it: the page
    /// published a last time and the stopped record written.
    fn end(mut self, outcome: Option<mc683xx_board::BoardRun>) -> Self {
        let live = self.powered.live();
        self.side.publish(&live, true);
        live.report_stopped(outcome);
        self.side.ended = true;
        self
    }

    /// The board powered off and on, the lines, the script and the page
    /// kept, the pacing started again with the board.
    fn power_cycled(self) -> Result<Self, Error> {
        let Self { powered, mut side } = self;
        let powered = powered.power_cycled(&side.config, &side.loaded)?;
        if let Some(page) = &mut side.page {
            page.due_now();
        }
        side.paced = None;
        Ok(Self { powered, side })
    }
}

/// Says the board is `behind` the host's clock.
fn behind_note(behind: Duration) {
    trace::note(format_args!(
        "the board is {:.1} s behind the wall clock: the host cannot \
         keep up, and the periodic ticks come late",
        behind.as_secs_f64()
    ));
}

impl Side {
    /// The endpoint `name` bound to `line`, its far end framing as
    /// `framing`.
    fn bind(&mut self, name: &'static str, line: Line, framing: Framing) {
        self.lines.push(Bound {
            name,
            line,
            framing,
        });
    }

    /// The host port's line, if the board has the endpoint.
    fn host(&self) -> Option<&Line> {
        self.lines
            .iter()
            .find(|bound| bound.name == HOST)
            .map(|bound| &bound.line)
    }

    /// The run's own times on `live`'s board time, which a slice or a
    /// frame ends at: what runs alongside's next step, the run's end,
    /// the external 1 PPS's next edge.
    fn events(&self, live: &Live<'_>) -> impl Iterator<Item = Time> {
        self.alongside
            .next_event()
            .map(|at| live.time_of_run(at))
            .into_iter()
            .chain(self.until.map(|until| live.since_power_up(until)))
            .chain(live.next_pps())
    }

    /// What the lines' far ends sent, onto the board.
    fn feed(&self, live: &mut Live<'_>) -> Result<(), Error> {
        live.feed(&self.lines)
    }

    /// A slice's end, or a quantum's, in this order, each step seeing
    /// what those before it did:
    ///
    /// 1. What the slice sent, onto the lines (the host port's onto the
    ///    page's console too), and the alarm output's changes.
    /// 2. What the firmware sent the synthetic engine, for expect-gps.
    /// 3. The trace, and the chip's events.
    ///
    /// A CPU halted in the slice, as `stop` says, ends the run here, at
    /// where this board's CPU stopped: what the slice sent and traced is
    /// out, and a power cycle given in the slice, after the halt, is
    /// not applied, as one given in a later slice would not be.
    ///
    /// 4. `--time-code-off`'s command, onto the host port as its far
    ///    end's, if what 1 brought ends the power-up's first time code
    ///    frame; waiting again after a reset 3 saw.
    /// 5. What runs alongside, advanced to the run's clock: a script's
    ///    steps due by now are taken, seeing what 1 and 2 brought,
    ///    and queue their bytes for the engine's line, their keys and
    ///    their controls.
    /// 6. The bytes those steps gave, onto the engine's line now, as the
    ///    Unicorn machine's engine sends them, not a slice later.
    /// 7. The keys those steps pressed, at their time, as the Unicorn
    ///    machine presses them, not a slice later, and those clicked on
    ///    the page since the last slice.
    /// 8. The lines typed on the page's console, onto the host port as
    ///    its far end's, which the next slice takes as it takes a
    ///    client's.
    /// 9. The controls the same way as the keys, in order, up to a
    ///    power cycle, which the run gives (`Runner::finish_slice`),
    ///    building the board again before those after it.
    /// 10. The page, published when due, the board built again or not
    ///     (`Side::publish`).
    ///
    /// Whether the run goes on, as 5 says, and whether a power cycle is
    /// due, as 9 does.
    fn slice_end(&mut self, live: &mut Live<'_>, stop: &Stop) -> Result<SliceEnd, Error> {
        let host_sent = self.drain(live)?;
        self.hear_gps(live);
        live.write_trace(&self.config.trace.watch);
        let reset = live.report_events();
        if let Stop::Halted(halt) = stop {
            return Err(Halted {
                pc: live.pc(),
                reason: halt_reason(*halt),
            }
            .into());
        }
        self.turn_time_code_off(live, &host_sent, reset);
        let flow = self.alongside.advance(live.clocks().run);
        self.inject_gps(live);
        self.press(live);
        self.type_lines(live);
        let power_cycle = self.apply_controls(live);
        Ok(SliceEnd { flow, power_cycle })
    }

    /// The page, `live`'s snapshot published when it is due or when
    /// `forced`.
    fn publish(&mut self, live: &Live<'_>, forced: bool) {
        if let Some(page) = &mut self.page {
            page.publish(live, forced);
        }
    }

    /// What reached the board's endpoints, onto the lines and the page's
    /// console, and the alarm output's changes reported: what the host
    /// port sent.
    fn drain(&self, live: &mut Live<'_>) -> Result<Vec<u8>, Error> {
        let host_sent = self.lines.iter().try_fold(Vec::new(), |host_sent, bound| {
            let sent = live.drain(bound)?;
            if bound.name != HOST {
                return Ok::<_, Error>(host_sent);
            }
            if let Some(page) = &self.page {
                page.panel.sent(&sent);
            }
            Ok(sent)
        })?;
        if self.alarm_bound {
            live.report_alarm(self.config.indicators)?;
        }
        Ok(host_sent)
    }

    /// `--time-code-off`'s command onto the host port, if `host_sent`,
    /// what the port sent in the slice, makes it due, `reset` whether
    /// the board reset in it.
    fn turn_time_code_off(&mut self, live: &Live<'_>, host_sent: &[u8], reset: bool) {
        if let Some(command) = self.time_code_off.after(host_sent, reset)
            && let Some(host) = self.host()
        {
            host.inject(command);
            trace::note(format_args!(
                "time-code-off cycle={} step=send",
                live.clocks().cycle.0
            ));
        }
    }

    /// What the GPS engine was sent, for the script, taken from the
    /// part either way, so that it does not pile up there.
    fn hear_gps(&self, live: &mut Live<'_>) {
        let bytes = live.take_gps_heard();
        if let Some(heard) = &self.gps_heard {
            heard.borrow_mut().extend_from_slice(&bytes);
        }
    }

    /// What the script's write-gps steps gave, onto the GPS engine's
    /// line.
    fn inject_gps(&self, live: &mut Live<'_>) {
        let bytes = std::mem::take(&mut *self.gps_injected.borrow_mut());
        live.inject_gps(&bytes);
    }

    /// The keys a script pressed and those clicked on the panel page, on
    /// the front panel.
    fn press(&self, live: &mut Live<'_>) {
        let mut keys = std::mem::take(&mut *self.keys.borrow_mut());
        if let Some(page) = &self.page {
            let clocks = live.clocks();
            keys.extend(page.panel.take_keys(clocks.cycle, clocks.run));
        }
        for key in keys {
            if !live.press(key) {
                no_front_panel(key);
            }
        }
    }

    /// The lines typed on the panel page's console, onto the host port
    /// as its far end's, the next slice taking them as it takes a
    /// client's.
    fn type_lines(&self, live: &Live<'_>) {
        if let (Some(page), Some(host)) = (&self.page, self.host()) {
            let clocks = live.clocks();
            page.panel.type_lines(host, clocks.cycle, clocks.run);
        }
    }

    /// The controls a script gave and those clicked on the panel page,
    /// applied in order up to a power cycle: whether one is due, the
    /// controls after it left for the board it builds.
    fn apply_controls(&mut self, live: &mut Live<'_>) -> bool {
        self.pending.extend(self.controls.borrow_mut().drain(..));
        if let Some(page) = &self.page {
            let clocks = live.clocks();
            self.pending
                .extend(page.panel.take_controls(clocks.cycle, clocks.run));
        }
        while let Some(control) = self.pending.pop_front() {
            match live.apply(control) {
                Applied::Yes => {}
                Applied::Not => trace::note(format_args!(
                    "nothing on this board takes event {}; not applied",
                    control.name()
                )),
                Applied::PowerCycle => return true,
            }
        }
        false
    }
}

#[cfg(test)]
mod tests {
    use std::rc::Rc;

    use crate::files::Disk;
    use std::path::PathBuf;
    use std::time::Duration;

    use mc683xx_board::time::Span;
    use mc683xx_cpu32::Stop;

    use super::super::carried::Carried;
    use super::super::load::Loaded;
    use super::super::powered::Powered;
    use super::super::tests::config;
    use super::super::tests::remove_eeprom;
    use super::super::tests::working_file;
    use super::PICOSECONDS_PER_NANOSECOND;
    use super::Runner;
    use super::Step;
    use super::wall_of;
    use crate::config::HostPort;
    use crate::config::StopAfter;
    use crate::host::Unserved;
    use crate::run::Flow;
    use crate::run::Pacing;

    /// A write-gps step's bytes leave the run for the engine at the end
    /// of the slice the step is taken in, not the next one's.
    #[test]
    fn a_write_gps_step_reaches_the_engine_at_the_slice_it_is_taken() {
        let script = std::env::temp_dir().join(format!(
            "smartclock-emu-runner-write-gps-{}.script",
            std::process::id()
        ));
        std::fs::write(&script, "write-gps @@x\n").expect("the script written");
        let mut config = config("third_party/firmware/images/z3805a-3543b.bin", "write-gps");
        config.host = HostPort::Script(script.clone());
        config.sci = Some("127.0.0.1:0".to_owned());
        config.pacing = Pacing::Free;
        let eeprom = working_file(&config).to_owned();
        let loaded = Loaded::new(&config, Rc::new(Disk)).expect("loaded");
        let powered = Powered::up(&config, &loaded, Carried::default()).expect("built");
        let mut runner = Runner::new(config, loaded, powered, &mut Unserved).expect("bound");
        let end = runner
            .side
            .slice_end(&mut runner.powered.live(), &Stop::Budget)
            .expect("a slice's end");
        assert_eq!(end.flow, Flow::Continue);
        assert!(
            runner.side.gps_injected.borrow().is_empty(),
            "left for a slice"
        );
        drop(runner);
        remove_eeprom(&eeprom);
        let _ = std::fs::remove_file(&script);
    }

    /// A script that steps through what a run's host gives it: a power
    /// cycle, then a reset, a key and the antenna pulled, each at a time
    /// of its own between the slices' and the quanta's ends, so that
    /// each step's time shows in the state after it.
    const STEPS: &str = concat!(
        "at 0.3\nevent power-cycle\n",
        "at 0.71\nevent reset\n",
        "at 1.05\nkey sat\n",
        "after 0.4\nevent antenna-off\n",
    );

    /// Seconds of board time the runs of `STEPS` take.
    const STEPS_SECONDS: f64 = 2.0;

    /// How far the host's clock goes between a paced run's frames: no
    /// multiple of a quantum or of a slice.
    const FRAME: Duration = Duration::from_micros(7_321);

    /// The 58503A run of `STEPS`, paced as `pacing` says, under `name`:
    /// its runner and its EEPROM, its script.
    fn stepped(name: &str, pacing: Pacing) -> (Runner, PathBuf, PathBuf) {
        let script = std::env::temp_dir().join(format!(
            "smartclock-emu-runner-{name}-{}.script",
            std::process::id()
        ));
        std::fs::write(&script, STEPS).expect("the script written");
        let mut config = config("third_party/firmware/images/58503a-3704.bin", name);
        config.host = HostPort::Script(script.clone());
        config.pacing = pacing;
        config.until = Some(StopAfter::new(STEPS_SECONDS).expect("a span"));
        let eeprom = working_file(&config).to_owned();
        let loaded = Loaded::new(&config, Rc::new(Disk)).expect("loaded");
        let powered = Powered::up(&config, &loaded, Carried::default()).expect("built");
        let runner = Runner::new(config, loaded, powered, &mut Unserved).expect("bound");
        (runner, eeprom, script)
    }

    /// A paced run takes a script's steps at their times, as a
    /// deterministic run does, though its frames end where the host's
    /// clock had come to and its quanta are mc683xx's 5 ms, not the
    /// slices': the two runs of one script, a power cycle among its
    /// steps, end in the same state, at the same time on the run's
    /// clock.  A frame ends at a step's time on the host's clock, a
    /// nanosecond's resolution, so the power cycle comes within a
    /// nanosecond after the deterministic run's.
    #[test]
    fn a_paced_run_takes_a_scripts_steps_at_their_times() {
        let (free, free_eeprom, free_script) = stepped("steps-free", Pacing::Free);
        let (mut free, step) = free.step_until(None).expect("run");
        assert_eq!(step, Step::Ended);
        let (mut paced, paced_eeprom, paced_script) = stepped("steps-paced", Pacing::Wall);
        let mut wall = Duration::ZERO;
        loop {
            let (runner, step) = paced.step_frame(wall, Span::MAX).expect("a frame");
            paced = runner;
            if step == Step::Ended {
                break;
            }
            wall += FRAME;
        }
        {
            let (paced_live, free_live) = (paced.powered.live(), free.powered.live());
            assert_eq!(paced_live.fingerprint(), free_live.fingerprint());
            assert_eq!(paced_live.run_time(), free_live.run_time());
            let late = (free_live.now() - paced_live.now()).picos();
            assert!(late < PICOSECONDS_PER_NANOSECOND, "{late} ps");
        }
        drop((free, paced));
        for (eeprom, script) in [(free_eeprom, free_script), (paced_eeprom, paced_script)] {
            remove_eeprom(&eeprom);
            let _ = std::fs::remove_file(&script);
        }
    }

    /// The deterministic Z3805A run of the script `steps`, under `name`,
    /// with `--skip-warm-up` if `skip_warm_up`, to `STEPS_SECONDS`
    /// ended: its runner, its EEPROM and its script.
    fn warmed(name: &str, steps: &str, skip_warm_up: bool) -> (Runner, PathBuf, PathBuf) {
        let script = std::env::temp_dir().join(format!(
            "smartclock-emu-runner-{name}-{}.script",
            std::process::id()
        ));
        std::fs::write(&script, steps).expect("the script written");
        let mut config = config("third_party/firmware/images/z3805a-3543b.bin", name);
        config.host = HostPort::Script(script.clone());
        config.pacing = Pacing::Free;
        config.until = Some(StopAfter::new(STEPS_SECONDS).expect("a span"));
        config.skip_warm_up = skip_warm_up;
        let eeprom = working_file(&config).to_owned();
        let loaded = Loaded::new(&config, Rc::new(Disk)).expect("loaded");
        let powered = Powered::up(&config, &loaded, Carried::default()).expect("built");
        let runner = Runner::new(config, loaded, powered, &mut Unserved).expect("bound");
        let (runner, step) = runner.step_until(None).expect("run");
        assert_eq!(step, Step::Ended);
        (runner, eeprom, script)
    }

    /// A deterministic run with `--skip-warm-up` ends in the state of
    /// one whose script takes the skip-warm-up control at 0.
    #[test]
    fn skip_warm_up_runs_as_the_control_taken_at_the_start() {
        let (mut option, option_eeprom, option_script) = warmed("skip-option", "", true);
        let (mut control, control_eeprom, control_script) =
            warmed("skip-control", "at 0\nevent skip-warm-up\n", false);
        assert_eq!(
            option.powered.live().fingerprint(),
            control.powered.live().fingerprint()
        );
        drop((option, control));
        for (eeprom, script) in [
            (option_eeprom, option_script),
            (control_eeprom, control_script),
        ] {
            remove_eeprom(&eeprom);
            let _ = std::fs::remove_file(&script);
        }
    }

    #[test]
    fn board_time_on_the_hosts_clock_is_rounded_up_to_its_nanosecond() {
        assert_eq!(wall_of(0).as_nanos(), 0);
        assert_eq!(wall_of(1).as_nanos(), 1);
        assert_eq!(wall_of(1000).as_nanos(), 1);
        assert_eq!(wall_of(1001).as_nanos(), 2);
    }
}
