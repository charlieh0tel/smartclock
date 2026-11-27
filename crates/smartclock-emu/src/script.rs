//! A script on the host port, in place of a client (`--script`): input
//! timed by the board's clock, so that a session replays the same each
//! time, to the cycle, as a deterministic boot does (docs/emulator.md,
//! "Scripts").
//!
//! A script is a file of steps, one a line, run in order, each once
//! what it waits for has come; a blank line or one starting `#` is
//! nothing:
//!
//! ```text
//! at SECONDS      board time since the run's start, across a
//!                 power cycle
//! after SECONDS   board time since the step before
//! expect TEXT     the port to have sent TEXT since the last expect
//! expect TEXT within SECONDS
//!                 the same, failing the run if it has not within
//!                 SECONDS of board time since the step before
//! expect-gps TEXT [within SECONDS]
//!                 the same of what the firmware sent the synthetic
//!                 GPS engine since the last expect-gps
//! send TEXT       TEXT and CR LF, as the library's session sends
//! write TEXT      TEXT alone
//! write-gps TEXT  TEXT to the firmware on the GPS engine's line, as
//!                 though the synthetic engine had sent it
//! key NAME        a front-panel key pressed: time, long, lat, alt,
//!                 sat, status, serial or shift (the 58503A's)
//! event NAME      a simulation control applied: antenna-off,
//!                 antenna-on, skip-warm-up, power-cycle or reset
//! stop            the end of the run
//! ```
//!
//! TEXT is the rest of the line, spaces and all, with `\r`, `\n`, `\t`,
//! `\\` and `\xHH` for what a line cannot hold; an expect's TEXT ends
//! before its last ` within `, if what follows is a number of seconds.
//! A step is looked at after each of the machine's slices, and a step's
//! time ends a slice (`Alongside::next_event`), so it is taken at its
//! time, or past it by the block of code the core was running then; a
//! slice's ends are the board's events', never the wall clock's, so
//! they are the same each run.  An expect not met within its time ends
//! the run as failed.
//! What the port sends goes to standard output as it comes, and each
//! step taken is a record on standard error:
//!
//! ```text
//! smartclock-emu: script cycle=17825792 line=3 step=send
//! ```

use std::cell::RefCell;
use std::fmt;
use std::io::Read;
use std::io::Write;
use std::path::Path;
use std::rc::Rc;

use crate::board::front_panel::Key;
use crate::board::time::CPU_HZ;
use crate::board::time::Cycles;
use crate::control::Control;
use crate::files::Files;
use crate::files::read_text;
use crate::line::FarEnd;
use crate::names::Named as _;
use crate::run::Alongside;
use crate::run::Flow;
use crate::trace;

/// What `send` ends its text with, as `smartclock::session` does.
pub(crate) const TERMINATOR: &[u8] = b"\r\n";

/// What puts a time limit on an expect.
const WITHIN: &str = " within ";

/// What the firmware sent the synthetic GPS engine, for `expect-gps`:
/// whoever runs the engine appends to it, and the script takes it.
pub(crate) type Heard = Rc<RefCell<Vec<u8>>>;

/// What a script's `write-gps` steps put on the GPS engine's line to
/// the firmware: the script appends to it, and whoever runs the engine
/// sends it after what the engine has already sent.
pub(crate) type Injected = Rc<RefCell<Vec<u8>>>;

/// What an expect looks in.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum Source {
    /// What the host port sent.
    Host,
    /// What the firmware sent the synthetic GPS engine.
    Gps,
}

/// What goes wrong reading a script.
#[derive(Debug, thiserror::Error)]
pub enum Error {
    /// It could not be read.
    #[error("reading the script {path}: {source}")]
    Read {
        /// The file.
        path: String,
        /// Why.
        source: std::io::Error,
    },
    /// A line is not a step.
    #[error("the script's line {line}: {message}")]
    Step {
        /// The line, from 1.
        line: usize,
        /// What is wrong with it.
        message: String,
    },
}

/// What a step does, or waits for.
#[derive(Debug, Clone, PartialEq)]
enum Action {
    /// Waits for the run's clock, the board's time since the run's
    /// start, to reach a time.
    At(Cycles),
    /// Waits for a span of the board's time since the step before.
    After(Cycles),
    /// Waits for the port, or the firmware to the GPS engine, to send
    /// this, failing the run if it has not within a span of the
    /// board's time since the step before.
    Expect {
        /// Where.
        source: Source,
        /// The text.
        text: Vec<u8>,
        /// The span.
        within: Option<Cycles>,
    },
    /// Sends this to the port.
    Write(Vec<u8>),
    /// Sends this to the firmware on the GPS engine's line, as though
    /// the engine had.
    WriteGps(Vec<u8>),
    /// Presses a front-panel key.
    Key(Key),
    /// Applies a simulation control.
    Event(Control),
    /// Ends the run.
    Stop,
}

impl Action {
    /// Its name, as the script and the records give it.
    fn name(&self) -> &'static str {
        match self {
            Self::At(_) => "at",
            Self::After(_) => "after",
            Self::Expect {
                source: Source::Host,
                ..
            } => "expect",
            Self::Expect {
                source: Source::Gps,
                ..
            } => "expect-gps",
            Self::Write(_) => "write",
            Self::WriteGps(_) => "write-gps",
            Self::Key(_) => "key",
            Self::Event(_) => "event",
            Self::Stop => "stop",
        }
    }
}

/// What a script's `key` step presses the key through: the board's
/// front panel.
pub(crate) type KeyHook = Box<dyn FnMut(Key)>;

/// A key step's argument: the key's name.
fn key(argument: &str) -> Result<Action, String> {
    Key::from_name(argument.trim())
        .map(Action::Key)
        .ok_or_else(|| format!("{argument:?} is no key; the keys are {}", Key::names()))
}

/// What a script's `event` step applies its control through: the run.
pub(crate) type ControlHook = Box<dyn FnMut(Control)>;

/// What a script reaches beyond the host port, each through whoever
/// runs it: without one, the steps that would reach it do nothing, an
/// expect-gps is never met.
#[derive(Default)]
pub(crate) struct ScriptWiring {
    /// What a key step presses its key through: the front panel.
    pub(crate) keys: Option<KeyHook>,
    /// What an event step applies its control through: the run.
    pub(crate) controls: Option<ControlHook>,
    /// What the firmware sent the GPS engine, which expect-gps steps
    /// look in.
    pub(crate) gps_heard: Option<Heard>,
    /// Where write-gps steps put their bytes.
    pub(crate) gps_injected: Option<Injected>,
}

/// An event step's argument: the control's name.
fn event(argument: &str) -> Result<Action, String> {
    Control::from_name(argument.trim())
        .map(Action::Event)
        .ok_or_else(|| {
            format!(
                "{argument:?} is no control; the controls are {}",
                Control::names()
            )
        })
}

/// A step: what it does, and the line of the script it is on.
#[derive(Debug, Clone, PartialEq)]
struct Step {
    /// Its line, from 1.
    line: usize,
    /// What it does.
    action: Action,
    /// How the script names it: `send` is a `write` with the terminator.
    name: &'static str,
}

/// A step taken, as its record gives it.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
struct Taken {
    /// When, on the board's clock.
    at: Cycles,
    /// Its line.
    line: usize,
    /// Its name.
    name: &'static str,
}

impl fmt::Display for Taken {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            formatter,
            "script cycle={} line={} step={}",
            self.at.0, self.line, self.name
        )
    }
}

/// TEXT as a step gives it: its escapes undone.
pub(crate) fn unescape(text: &str) -> Result<Vec<u8>, String> {
    let mut bytes = Vec::new();
    let mut chars = text.chars();
    while let Some(next) = chars.next() {
        if next != '\\' {
            let mut buffer = [0; 4];
            bytes.extend_from_slice(next.encode_utf8(&mut buffer).as_bytes());
            continue;
        }
        match chars.next() {
            Some('r') => bytes.push(b'\r'),
            Some('n') => bytes.push(b'\n'),
            Some('t') => bytes.push(b'\t'),
            Some('\\') => bytes.push(b'\\'),
            Some('x') => {
                let digits: String = chars.by_ref().take(2).collect();
                let byte = u8::from_str_radix(&digits, 16)
                    .ok()
                    .filter(|_| digits.len() == 2)
                    .ok_or_else(|| format!("\\x{digits} is not \\x and two hexadecimal digits"))?;
                bytes.push(byte);
            }
            Some(other) => return Err(format!("\\{other} is no escape")),
            None => return Err("a \\ ends the line".to_owned()),
        }
    }
    Ok(bytes)
}

/// SECONDS as a step gives it.
fn seconds(text: &str) -> Result<Cycles, String> {
    text.trim()
        .parse::<f64>()
        .ok()
        .and_then(Cycles::from_seconds)
        .ok_or_else(|| format!("{text:?} is not a number of seconds"))
}

/// An expect's argument, of what `source` sent: TEXT, or TEXT within
/// SECONDS.
fn expect(argument: &str, source: Source) -> Result<Action, String> {
    let (text, within) = match argument.rsplit_once(WITHIN) {
        Some((text, span)) => match seconds(span) {
            Ok(span) => (text, Some(span)),
            Err(_) => (argument, None),
        },
        None => (argument, None),
    };
    let text = unescape(text)?;
    if text.is_empty() {
        return Err("expect wants text".to_owned());
    }
    Ok(Action::Expect {
        source,
        text,
        within,
    })
}

/// The steps of the script `text`.
fn parse(text: &str) -> Result<Vec<Step>, Error> {
    let mut steps = Vec::new();
    for (index, content) in text.lines().enumerate() {
        let line = index + 1;
        if content.trim().is_empty() || content.trim_start().starts_with('#') {
            continue;
        }
        let (keyword, argument) = content.split_once(' ').unwrap_or((content, ""));
        let action = match keyword {
            "at" => seconds(argument).map(Action::At),
            "after" => seconds(argument).map(Action::After),
            "expect" => expect(argument, Source::Host),
            "expect-gps" => expect(argument, Source::Gps),
            "send" => unescape(argument).map(|text| Action::Write([&text, TERMINATOR].concat())),
            "write" => unescape(argument).map(Action::Write),
            "write-gps" => unescape(argument).map(Action::WriteGps),
            "key" => key(argument),
            "event" => event(argument),
            "stop" if argument.trim().is_empty() => Ok(Action::Stop),
            "stop" => Err("stop takes nothing".to_owned()),
            _ => Err(format!("{keyword:?} is no step")),
        }
        .map_err(|message| Error::Step { line, message })?;
        let name = if keyword == "send" {
            "send"
        } else {
            action.name()
        };
        steps.push(Step { line, action, name });
    }
    Ok(steps)
}

/// A script running on the host port.
pub(crate) struct Script {
    /// Its end of the host port.
    far: FarEnd,
    /// Its steps.
    steps: Vec<Step>,
    /// The next to take.
    next: usize,
    /// When the step before was taken.
    since: Cycles,
    /// What the port sent since the last expect was met.
    heard: Vec<u8>,
    /// What the firmware sent the GPS engine since the last
    /// expect-gps was met.
    gps_heard: Vec<u8>,
    /// What it reaches beyond the port.
    wiring: ScriptWiring,
    /// How far into its text the expect being taken has looked without
    /// finding it: a match can start no earlier.
    scanned: usize,
    /// The last expect step's index for each source, the host's and the
    /// engine's: past it, what that source sends is kept for nothing.
    last_expects: [Option<usize>; 2],
}

impl std::fmt::Debug for Script {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        formatter
            .debug_struct("Script")
            .field("next", &self.next)
            .field("since", &self.since)
            .finish_non_exhaustive()
    }
}

impl Script {
    /// The script in the file `path`, read from `files`, on `far`,
    /// reaching nothing beyond it.
    pub(crate) fn open(path: &Path, files: &dyn Files, far: FarEnd) -> Result<Self, Error> {
        let text = read_text(files, path).map_err(|source| Error::Read {
            path: path.display().to_string(),
            source,
        })?;
        Ok(Self::new(parse(&text)?, far))
    }

    fn new(steps: Vec<Step>, far: FarEnd) -> Self {
        let last_expect = |wanted: Source| {
            steps.iter().rposition(
                |step| matches!(step.action, Action::Expect { source, .. } if source == wanted),
            )
        };
        let last_expects = [last_expect(Source::Host), last_expect(Source::Gps)];
        Self {
            far,
            steps,
            next: 0,
            since: Cycles(0),
            heard: Vec::new(),
            gps_heard: Vec::new(),
            wiring: ScriptWiring::default(),
            scanned: 0,
            last_expects,
        }
    }

    /// Whether an expect on `source` is still to be taken.
    fn expects(&self, source: Source) -> bool {
        self.last_expects[source as usize].is_some_and(|last| last >= self.next)
    }

    /// The script reaching beyond the port through `wiring`.
    pub(crate) fn wired(mut self, wiring: ScriptWiring) -> Self {
        self.wiring = wiring;
        self
    }

    /// Takes in what the port sent: to standard output, and to what the
    /// next expect looks in; and what the GPS engine was sent.
    fn listen(&mut self) {
        if let Some(gps) = &self.wiring.gps_heard {
            self.gps_heard.append(&mut gps.borrow_mut());
        }
        let mut buffer = [0; 256];
        while let Ok(count @ 1..) = self.far.read(&mut buffer) {
            let mut stdout = std::io::stdout().lock();
            // a transcript nobody reads is no reason to stop
            let _ = stdout.write_all(&buffer[..count]);
            let _ = stdout.flush();
            self.heard.extend_from_slice(&buffer[..count]);
        }
        // what no expect will look in
        if !self.expects(Source::Host) {
            self.heard.clear();
        }
        if !self.expects(Source::Gps) {
            self.gps_heard.clear();
        }
    }

    /// Time moves on to `now`: each step whose wait is over is taken, in
    /// order, until one waits on or the script stops or fails the run.
    fn take(&mut self, now: Cycles) -> Flow {
        self.listen();
        while let Some(step) = self.steps.get(self.next) {
            match &step.action {
                Action::At(at) if now < *at => break,
                Action::After(span) if now < self.since.saturating_add(*span) => break,
                Action::At(_) | Action::After(_) => {}
                Action::Expect {
                    source,
                    text,
                    within,
                } => {
                    let heard = match source {
                        Source::Host => &mut self.heard,
                        Source::Gps => &mut self.gps_heard,
                    };
                    let from = self.scanned.min(heard.len());
                    let Some(found) = heard[from..]
                        .windows(text.len())
                        .position(|window| window == text.as_slice())
                        .map(|at| from + at)
                    else {
                        // a match may yet start in its last bytes but one
                        self.scanned = heard.len().saturating_sub(text.len() - 1);
                        if let Some(within) = within
                            && now >= self.since.saturating_add(*within)
                        {
                            return Flow::Fail(format!(
                                "the script's line {}: {:?} not sent{} within {} s",
                                step.line,
                                String::from_utf8_lossy(text),
                                match source {
                                    Source::Host => "",
                                    Source::Gps => " to the GPS engine",
                                },
                                within.0 as f64 / CPU_HZ as f64
                            ));
                        }
                        break;
                    };
                    heard.drain(..found + text.len());
                }
                Action::Write(bytes) => {
                    // the line takes everything; a write cannot fail
                    let _ = self.far.write_all(bytes);
                }
                Action::WriteGps(bytes) => {
                    if let Some(injected) = &self.wiring.gps_injected {
                        injected.borrow_mut().extend_from_slice(bytes);
                    }
                }
                Action::Key(key) => {
                    if let Some(press) = &mut self.wiring.keys {
                        press(*key);
                    }
                }
                Action::Event(control) => {
                    if let Some(apply) = &mut self.wiring.controls {
                        apply(*control);
                    }
                }
                Action::Stop => {}
            }
            trace::emit(&Taken {
                at: now,
                line: step.line,
                name: step.name,
            });
            self.since = now;
            self.next += 1;
            self.scanned = 0;
            if step.action == Action::Stop {
                return Flow::Stop;
            }
        }
        Flow::Continue
    }
}

impl Alongside for Script {
    fn advance(&mut self, now: Cycles) -> Flow {
        self.take(now)
    }

    /// The next step's time, if it waits for one.
    fn next_event(&self) -> Option<Cycles> {
        match &self.steps.get(self.next)?.action {
            Action::At(at) => Some(*at),
            Action::After(span) => Some(self.since.saturating_add(*span)),
            Action::Expect { within, .. } => within.map(|within| self.since.saturating_add(within)),
            Action::Write(_)
            | Action::WriteGps(_)
            | Action::Key(_)
            | Action::Event(_)
            | Action::Stop => None,
        }
    }
}

#[cfg(test)]
mod tests {
    use std::cell::RefCell;
    use std::rc::Rc;

    use super::Action;
    use super::Error;
    use super::Script;
    use super::ScriptWiring;
    use super::Source;
    use super::parse;
    use crate::board::front_panel::Key;
    use crate::board::time::CPU_HZ;
    use crate::board::time::Cycles;
    use crate::control::Control;
    use crate::line::Line;
    use crate::run::Alongside;
    use crate::run::Flow;

    fn actions(text: &str) -> Vec<Action> {
        parse(text)
            .expect("a script")
            .into_iter()
            .map(|step| step.action)
            .collect()
    }

    #[test]
    fn each_step_reads_as_its_action() {
        assert_eq!(
            actions(
                "# a session\n\nat 1.5\nafter 0.25\nexpect scpi >\\x20\nsend *IDN?\nwrite a\\tb\\\\\nstop\n"
            ),
            [
                Action::At(Cycles(CPU_HZ * 3 / 2)),
                Action::After(Cycles(CPU_HZ / 4)),
                Action::Expect {
                    source: Source::Host,
                    text: b"scpi > ".to_vec(),
                    within: None,
                },
                Action::Write(b"*IDN?\r\n".to_vec()),
                Action::Write(b"a\tb\\".to_vec()),
                Action::Stop,
            ]
        );
    }

    #[test]
    fn a_line_that_is_no_step_is_refused_with_its_number() {
        for (text, line) in [
            ("at 1\nwait 2\n", 2),
            ("after -1\n", 1),
            ("\nsend \\q\n", 2),
            ("write \\x4\n", 1),
            ("expect\n", 1),
            ("stop now\n", 1),
            ("key enter\n", 1),
            ("event antenna\n", 1),
        ] {
            let error = parse(text).expect_err(text);
            assert!(
                matches!(error, Error::Step { line: at, .. } if at == line),
                "{text:?}: {error}"
            );
        }
    }

    #[test]
    fn steps_wait_for_their_time_and_their_text_in_order() {
        let line = Line::new("host", false);
        let script = parse("expect >\nafter 1\nsend x\nstop\n").expect("a script");
        let mut script = Script::new(script, line.attach());
        assert_eq!(script.advance(Cycles(10)), Flow::Continue);
        assert_eq!(line.receive(), None, "waiting for the prompt");
        for byte in b"ok >" {
            line.transmit(*byte);
        }
        assert_eq!(script.advance(Cycles(20)), Flow::Continue);
        assert_eq!(line.receive(), None, "a second after the prompt");
        assert_eq!(script.advance(Cycles(20 + CPU_HZ)), Flow::Stop);
        let sent: Vec<u8> = std::iter::from_fn(|| line.receive()).collect();
        assert_eq!(sent, b"x\r\n");
    }

    #[test]
    fn an_expect_within_takes_its_limit_and_keeps_text_that_is_no_limit() {
        assert_eq!(
            actions("expect scpi > within 2.5\nexpect-gps @@Bk\nexpect up within reach\n"),
            [
                Action::Expect {
                    source: Source::Host,
                    text: b"scpi >".to_vec(),
                    within: Some(Cycles(CPU_HZ * 5 / 2)),
                },
                Action::Expect {
                    source: Source::Gps,
                    text: b"@@Bk".to_vec(),
                    within: None,
                },
                Action::Expect {
                    source: Source::Host,
                    text: b"up within reach".to_vec(),
                    within: None,
                },
            ]
        );
    }

    #[test]
    fn an_expect_not_met_within_its_time_fails_the_run_then() {
        let line = Line::new("host", false);
        let script = parse("after 1\nexpect > within 2\nstop\n").expect("a script");
        let mut script = Script::new(script, line.attach());
        assert_eq!(script.next_event(), Some(Cycles(CPU_HZ)), "the after's");
        assert_eq!(script.advance(Cycles(CPU_HZ)), Flow::Continue);
        assert_eq!(script.next_event(), Some(Cycles(3 * CPU_HZ)), "its limit");
        assert_eq!(script.advance(Cycles(3 * CPU_HZ - 1)), Flow::Continue);
        let Flow::Fail(why) = script.advance(Cycles(3 * CPU_HZ)) else {
            panic!("not failed");
        };
        assert!(why.contains("line 2"), "{why}");
    }

    /// A wait or a limit past what the board's clock counts is never
    /// over: its end is the clock's last cycle, not one wrapped round.
    #[test]
    fn a_wait_or_a_limit_past_the_clocks_end_never_ends() {
        let line = Line::new("host", false);
        let script = parse("after 1\nafter 1e30\nstop\n").expect("a script");
        let mut script = Script::new(script, line.attach());
        assert_eq!(script.advance(Cycles(CPU_HZ)), Flow::Continue);
        assert_eq!(script.next_event(), Some(Cycles(u64::MAX)));
        assert_eq!(script.advance(Cycles(2 * CPU_HZ)), Flow::Continue);
        let script = parse("after 1\nexpect > within 1e30\n").expect("a script");
        let mut script = Script::new(script, line.attach());
        assert_eq!(script.advance(Cycles(CPU_HZ)), Flow::Continue);
        assert_eq!(script.next_event(), Some(Cycles(u64::MAX)));
        assert_eq!(script.advance(Cycles(2 * CPU_HZ)), Flow::Continue);
    }

    #[test]
    fn an_expect_met_within_its_time_goes_on() {
        let line = Line::new("host", false);
        let script = parse("expect > within 2\nstop\n").expect("a script");
        let mut script = Script::new(script, line.attach());
        line.transmit(b'>');
        assert_eq!(script.advance(Cycles(2 * CPU_HZ)), Flow::Stop);
    }

    #[test]
    fn a_key_step_presses_its_key_through_the_hook() {
        let line = Line::new("host", false);
        let script = parse("key sat\nkey shift\nstop\n").expect("a script");
        let pressed = Rc::new(RefCell::new(Vec::new()));
        let into = Rc::clone(&pressed);
        let mut script = Script::new(script, line.attach()).wired(ScriptWiring {
            keys: Some(Box::new(move |key| into.borrow_mut().push(key))),
            ..ScriptWiring::default()
        });
        assert_eq!(script.advance(Cycles(1)), Flow::Stop);
        assert_eq!(*pressed.borrow(), [Key::Sat, Key::Shift]);
    }

    #[test]
    fn an_event_step_applies_its_control_through_the_hook() {
        let line = Line::new("host", false);
        let script = parse("event antenna-off\nevent power-cycle\nstop\n").expect("a script");
        let applied = Rc::new(RefCell::new(Vec::new()));
        let into = Rc::clone(&applied);
        let mut script = Script::new(script, line.attach()).wired(ScriptWiring {
            controls: Some(Box::new(move |control| into.borrow_mut().push(control))),
            ..ScriptWiring::default()
        });
        assert_eq!(script.advance(Cycles(1)), Flow::Stop);
        assert_eq!(
            *applied.borrow(),
            [Control::AntennaOff, Control::PowerCycle]
        );
    }

    #[test]
    fn an_expect_takes_what_it_found_and_no_more() {
        let line = Line::new("host", false);
        let script = parse("expect >\nexpect >\nwrite y\n").expect("a script");
        let mut script = Script::new(script, line.attach());
        line.transmit(b'>');
        script.advance(Cycles(1));
        assert_eq!(line.receive(), None, "the second waits for another");
        line.transmit(b'>');
        script.advance(Cycles(2));
        assert_eq!(line.receive(), Some(b'y'));
    }

    /// An expect looks again only at what it has not looked at, but for
    /// the bytes a match could still start in, so a text split across
    /// two reads is still found.
    #[test]
    fn an_expect_looks_on_from_where_it_left_off() {
        let line = Line::new("host", false);
        let script = parse("expect abc\nwrite y\n").expect("a script");
        let mut script = Script::new(script, line.attach());
        for byte in b"xxxxab" {
            line.transmit(*byte);
        }
        script.advance(Cycles(1));
        assert_eq!(script.scanned, 4, "the last two bytes may start it");
        line.transmit(b'c');
        script.advance(Cycles(2));
        assert_eq!(line.receive(), Some(b'y'));
        assert_eq!(script.scanned, 0, "the next step starts afresh");
    }

    /// Once no expect is left on a source, what it sends is not kept.
    #[test]
    fn what_no_expect_will_look_in_is_not_kept() {
        let line = Line::new("host", false);
        let script = parse("expect >\nsend x\n").expect("a script");
        let gps = Rc::new(RefCell::new(Vec::new()));
        let mut script = Script::new(script, line.attach()).wired(ScriptWiring {
            gps_heard: Some(Rc::clone(&gps)),
            ..ScriptWiring::default()
        });
        gps.borrow_mut().extend_from_slice(b"@@Bk");
        for byte in b"pending" {
            line.transmit(*byte);
        }
        script.advance(Cycles(1));
        assert_eq!(script.heard, b"pending", "the expect waits on it");
        assert!(script.gps_heard.is_empty(), "no expect-gps at all");
        line.transmit(b'>');
        script.advance(Cycles(2));
        for byte in b"after" {
            line.transmit(*byte);
        }
        script.advance(Cycles(3));
        assert!(script.heard.is_empty(), "past the last expect");
    }

    #[test]
    fn an_expect_gps_looks_in_what_the_engine_was_sent_alone() {
        let line = Line::new("host", false);
        let script =
            parse("expect-gps @@Bk within 2\nexpect-gps @@Cg\nwrite y\n").expect("a script");
        let gps = Rc::new(RefCell::new(Vec::new()));
        let mut script = Script::new(script, line.attach()).wired(ScriptWiring {
            gps_heard: Some(Rc::clone(&gps)),
            ..ScriptWiring::default()
        });
        for byte in b"@@Bk" {
            line.transmit(*byte);
        }
        assert_eq!(script.advance(Cycles(1)), Flow::Continue);
        assert_eq!(
            line.receive(),
            None,
            "the host port's text is not the engine's"
        );
        gps.borrow_mut().extend_from_slice(b"@@Bk\x00)\r\n@@C");
        assert_eq!(script.advance(Cycles(2)), Flow::Continue);
        assert_eq!(line.receive(), None, "the second waits for the rest");
        gps.borrow_mut().extend_from_slice(b"g\x01%\r\n");
        script.advance(Cycles(3));
        assert_eq!(line.receive(), Some(b'y'));
    }

    #[test]
    fn a_write_gps_step_puts_its_bytes_where_the_engine_takes_them() {
        let line = Line::new("host", false);
        let script = parse("write-gps @@Ci\\x01\\x2b\\r\\n\nstop\n").expect("a script");
        let injected = Rc::new(RefCell::new(Vec::new()));
        let mut script = Script::new(script, line.attach()).wired(ScriptWiring {
            gps_injected: Some(Rc::clone(&injected)),
            ..ScriptWiring::default()
        });
        assert_eq!(script.advance(Cycles(1)), Flow::Stop);
        assert_eq!(*injected.borrow(), b"@@Ci\x01\x2b\r\n");
        assert_eq!(line.receive(), None, "nothing on the host port");
    }

    #[test]
    fn an_expect_gps_not_met_within_its_time_fails_the_run_naming_the_engine() {
        let line = Line::new("host", false);
        let script = parse("expect-gps @@Bk within 1\n").expect("a script");
        let mut script = Script::new(script, line.attach());
        let Flow::Fail(why) = script.advance(Cycles(CPU_HZ)) else {
            panic!("not failed");
        };
        assert!(why.contains("to the GPS engine"), "{why}");
    }
}
