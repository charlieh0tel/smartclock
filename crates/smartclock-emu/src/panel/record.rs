//! What the run traces of what the page gave it: a key or a control
//! applied, a line put on the host port, each at a cycle a script's
//! steps replay (docs/emulator.md, "The panel page", "The console").

use std::fmt;
use std::fmt::Write as _;
use std::sync::mpsc::Receiver;

use crate::board::front_panel::Key;
use crate::board::time::CPU_HZ;
use crate::board::time::Cycles;
use crate::control::Control;
use crate::names::Named;
use crate::script::TERMINATOR;
use crate::trace;

/// What the page queues for the run loop by name, as a script's step
/// names it: a key, or a simulation control.
pub(super) trait Queued: Named + Send {
    /// Its record's kind, the first word of the record.
    const KIND: &'static str;
}

impl Queued for Control {
    const KIND: &'static str = "event";
}

impl Queued for Key {
    const KIND: &'static str = "key";
}

/// `at` on the run's clock as a record writes it, seconds of the chip's
/// 16.777216 MHz: a cycle count below 2^53 over a power of two, exact,
/// and printed to the shortest text that reads back the same.
fn seconds(at: Cycles) -> f64 {
    at.0 as f64 / CPU_HZ as f64
}

/// Something queued on the page, applied at the end of a slice, as its
/// record gives it:
///
///     smartclock-emu: key cycle=53608448 at=3.1953125 name=sat
///
/// `cycle` is the chip's, as the trace counts it; `at` is the run's
/// clock as a script's `at` step takes it (`seconds`), so that `at` and
/// a step of the kind replay it at the same cycle.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(super) struct Applied<T> {
    /// When, on the chip's clock.
    pub(super) cycle: Cycles,
    /// When, on the run's clock, as a script counts it.
    pub(super) at: Cycles,
    /// What.
    pub(super) what: T,
}

impl<T: Queued> fmt::Display for Applied<T> {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            formatter,
            "{} cycle={} at={} name={}",
            T::KIND,
            self.cycle.0,
            seconds(self.at),
            self.what.name()
        )
    }
}

/// What `receiver` holds, in order, each traced as applied at `cycle`,
/// `at` on the run's clock.
pub(super) fn take<T: Queued>(receiver: &Receiver<T>, cycle: Cycles, at: Cycles) -> Vec<T> {
    let taken: Vec<T> = receiver.try_iter().collect();
    for &what in &taken {
        trace::emit(&Applied { cycle, at, what });
    }
    taken
}

/// A line typed on the page, put on the port at the end of a slice, as
/// its record gives it:
///
///     smartclock-emu: console cycle=N at=S line=*IDN?
///
/// `cycle` and `at` as `Applied` has them; `line` is the line without
/// its terminator, as a script's `send` takes its TEXT: a space, a
/// backslash and anything not printable ASCII as `\xHH` or `\\`, so
/// that `at` and `send` steps replay it.
#[derive(Debug, Clone, PartialEq, Eq)]
pub(super) struct Typed {
    /// When, on the chip's clock.
    pub(super) cycle: Cycles,
    /// When, on the run's clock.
    pub(super) at: Cycles,
    /// The line.
    pub(super) line: Vec<u8>,
}

impl fmt::Display for Typed {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            formatter,
            "console cycle={} at={} line=",
            self.cycle.0,
            seconds(self.at)
        )?;
        for &byte in &self.line {
            match byte {
                b'\\' => formatter.write_str("\\\\")?,
                b'!'..=b'~' => formatter.write_char(char::from(byte))?,
                _ => write!(formatter, "\\x{byte:02x}")?,
            }
        }
        Ok(())
    }
}

/// The lines typed since the last call, in order, each with the
/// terminator handed to `put`, and traced as put on the port at `cycle`,
/// `at` on the run's clock, if `put` took it: how many it took.  One it
/// did not, a TCP client having taken the port since it was typed, is
/// noted and dropped.
pub(super) fn take_typed(
    typed: &Receiver<Vec<u8>>,
    cycle: Cycles,
    at: Cycles,
    mut put: impl FnMut(&[u8]) -> bool,
) -> usize {
    let mut taken = 0;
    for line in typed.try_iter() {
        if put(&[line.as_slice(), TERMINATOR].concat()) {
            trace::emit(&Typed { cycle, at, line });
            taken += 1;
        } else {
            trace::note(format_args!(
                "a line typed on the console dropped: the host port has been taken"
            ));
        }
    }
    taken
}
