//! The trace: what the CPU does at the board, a record a line on
//! standard error for a script to read (docs/emulator.md, "Tracing").
//! Each line is `smartclock-emu: `, the record's kind, and its fields
//! as `key=value`, ASCII, separated by single spaces; a time is
//! `cycle=`, the board's clock in CPU cycles, and an address or a
//! register is hexadecimal with `0x`.  The lamps' changes are records
//! of the same form (`indicators`).  The lines go to standard error
//! unless the host gives them a sink of its own (`set_sink`), as a
//! browser, which has none, does.

use std::fmt;
use std::io::Write;
use std::sync::OnceLock;

use crate::board::Reset;
use crate::board::time::CPU_HZ;
use crate::board::time::Cycles;

/// What a run traces.
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub(crate) struct Tracing {
    /// Where the CPU is, every `POSITION_EVERY`.
    pub(crate) position: bool,
    /// Every device access.
    pub(crate) devices: bool,
    /// Every exception taken, interrupts among them.
    pub(crate) exceptions: bool,
}

/// How often a position record is made: every tenth of a second.
pub(crate) const POSITION_EVERY: Cycles = Cycles(CPU_HZ / 10);

/// Writes `record` as a trace line.
pub(crate) fn emit(record: &dyn fmt::Display) {
    note(format_args!("{record}"));
}

/// Writes `text` as a line of the emulator's own on standard error,
/// after its prefix.
pub fn note(text: fmt::Arguments<'_>) {
    line(format_args!("smartclock-emu: {text}"));
}

/// Where the trace's lines go in place of standard error, a line at a
/// time, its end not included.
pub type Sink = Box<dyn Fn(fmt::Arguments<'_>) + Send + Sync>;

/// The host's sink, once it gives one.
static SINK: OnceLock<Sink> = OnceLock::new();

/// The trace's lines, and the emulator's own, given to `sink` from now
/// on, in place of standard error: once, before the run, for a host
/// with no standard error.  A second sink is refused and given back.
///
/// # Errors
///
/// `sink`, if a sink was given before.
pub fn set_sink(sink: Sink) -> Result<(), Sink> {
    SINK.set(sink)
}

/// Writes `text` as a line on standard error, or to the host's sink.  A
/// failure to write, a closed standard error, is ignored rather than
/// ending the run, as `eprintln!` would by panicking: the run's output
/// is its ports'.
pub(crate) fn line(text: fmt::Arguments<'_>) {
    match SINK.get() {
        Some(sink) => sink(text),
        // nothing to report a failure to
        None => {
            let _ = writeln!(std::io::stderr().lock(), "{text}");
        }
    }
}

/// A device access.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) struct Access {
    /// When: the end of the block of code making it, as the core counts
    /// its time a block at a time.
    pub(crate) at: Cycles,
    /// The instruction's address.
    pub(crate) pc: u32,
    /// A write, else a read.
    pub(crate) write: bool,
    /// The address.
    pub(crate) address: u32,
    /// Its size in bytes: 1, 2 or 4.
    pub(crate) size: usize,
    /// The value read or written.
    pub(crate) value: u32,
}

impl fmt::Display for Access {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            formatter,
            "device cycle={} pc={:#010x} access={} address={:#010x} size={} value={:#0width$x}",
            self.at.0,
            self.pc,
            if self.write { "write" } else { "read" },
            self.address,
            self.size,
            self.value,
            width = 2 + 2 * self.size,
        )
    }
}

/// An exception taken.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) struct Exception {
    /// When.
    pub(crate) at: Cycles,
    /// Its vector number.
    pub(crate) vector: u8,
    /// Where it was taken: the instruction's address for a trap or a
    /// fault, the next instruction's for an interrupt.
    pub(crate) pc: u32,
    /// The status register before it.
    pub(crate) sr: u16,
    /// An interrupt's level.
    pub(crate) level: Option<u8>,
}

impl fmt::Display for Exception {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            formatter,
            "exception cycle={} vector={} pc={:#010x} sr={:#06x}",
            self.at.0, self.vector, self.pc, self.sr
        )?;
        if let Some(level) = self.level {
            write!(formatter, " level={level}")?;
        }
        Ok(())
    }
}

/// Where the CPU is.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) struct Position {
    /// When.
    pub(crate) at: Cycles,
    /// The program counter.
    pub(crate) pc: u32,
    /// The status register.
    pub(crate) sr: u32,
    /// The vector base.
    pub(crate) vbr: u32,
    /// The stack pointer.
    pub(crate) a7: u32,
    /// The instructions run so far, when counted.
    pub(crate) instructions: Option<u64>,
}

/// `instructions=N`, when counted.
fn instructions(formatter: &mut fmt::Formatter<'_>, count: Option<u64>) -> fmt::Result {
    match count {
        Some(count) => write!(formatter, " instructions={count}"),
        None => Ok(()),
    }
}

impl fmt::Display for Position {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            formatter,
            "position cycle={} pc={:#010x} sr={:#06x} vbr={:#010x} a7={:#010x}",
            self.at.0, self.pc, self.sr, self.vbr, self.a7
        )?;
        instructions(formatter, self.instructions)
    }
}

/// A watched instruction about to run, with the registers and the top
/// of the stack.
#[derive(Debug, Clone, PartialEq, Eq)]
pub(crate) struct Code {
    /// When.
    pub(crate) at: Cycles,
    /// Its address.
    pub(crate) pc: u32,
    /// The status register.
    pub(crate) sr: u16,
    /// D0 to D7.
    pub(crate) data: [u32; 8],
    /// A0 to A7.
    pub(crate) address: [u32; 8],
    /// The top eight longs of the stack.
    pub(crate) stack: [u32; 8],
}

/// Longs as hexadecimal, comma-separated.
fn longs(values: &[u32]) -> String {
    values
        .iter()
        .map(|value| format!("{value:08x}"))
        .collect::<Vec<_>>()
        .join(",")
}

impl fmt::Display for Code {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            formatter,
            "code cycle={} pc={:#010x} sr={:#06x} d={} a={} stack={}",
            self.at.0,
            self.pc,
            self.sr,
            longs(&self.data),
            longs(&self.address),
            longs(&self.stack)
        )
    }
}

/// The run's end, at `--stop-after`.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) struct Stopped {
    /// When.
    pub(crate) at: Cycles,
    /// The instructions run, when counted.
    pub(crate) instructions: Option<u64>,
}

impl fmt::Display for Stopped {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(formatter, "stopped cycle={}", self.at.0)?;
        instructions(formatter, self.instructions)
    }
}

/// A reset of the CPU the board asked for, its cause named as
/// mc683xx's `ResetCause` names it, so the two cores' records compare.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) struct Restart {
    /// When.
    pub(crate) at: Cycles,
    /// Why.
    pub(crate) cause: Reset,
}

impl fmt::Display for Restart {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            formatter,
            "reset cycle={} cause={:?}",
            self.at.0, self.cause
        )
    }
}

#[cfg(test)]
mod tests {
    use std::sync::Mutex;

    use super::Access;
    use super::Code;
    use super::Exception;
    use super::Position;
    use super::Restart;
    use super::Stopped;
    use crate::board::Reset;
    use crate::board::time::Cycles;

    /// The lines the sink was given, in the test that gives it.
    static SUNK: Mutex<Vec<String>> = Mutex::new(Vec::new());

    /// A sink the host gives takes the lines in place of standard error;
    /// the other tests' lines go there too, once it is given.
    #[test]
    fn the_hosts_sink_takes_the_lines() {
        let sink = |text: std::fmt::Arguments<'_>| {
            SUNK.lock().expect("the lines").push(text.to_string());
        };
        assert!(super::set_sink(Box::new(sink)).is_ok(), "the first sink");
        assert!(super::set_sink(Box::new(|_| {})).is_err(), "a second");
        super::note(format_args!("sunk"));
        assert!(
            SUNK.lock()
                .expect("the lines")
                .iter()
                .any(|line| line == "smartclock-emu: sunk")
        );
    }

    #[test]
    fn an_access_shows_its_value_at_its_size() {
        let access = Access {
            at: Cycles(42),
            pc: 0x1_5f88,
            write: false,
            address: 0x20_0003,
            size: 1,
            value: 4,
        };
        assert_eq!(
            access.to_string(),
            "device cycle=42 pc=0x00015f88 access=read address=0x00200003 size=1 value=0x04"
        );
        let access = Access {
            write: true,
            size: 2,
            value: 0x542,
            ..access
        };
        assert!(
            access
                .to_string()
                .ends_with("access=write address=0x00200003 size=2 value=0x0542")
        );
    }

    #[test]
    fn an_interrupt_shows_its_level_and_a_trap_none() {
        let trap = Exception {
            at: Cycles(7),
            vector: 32,
            pc: 0x100,
            sr: 0x2000,
            level: None,
        };
        assert_eq!(
            trap.to_string(),
            "exception cycle=7 vector=32 pc=0x00000100 sr=0x2000"
        );
        let tick = Exception {
            vector: 0x42,
            level: Some(5),
            ..trap
        };
        assert!(
            tick.to_string()
                .ends_with("vector=66 pc=0x00000100 sr=0x2000 level=5")
        );
    }

    #[test]
    fn the_count_shows_only_when_counted() {
        let position = Position {
            at: Cycles(1),
            pc: 2,
            sr: 0x2700,
            vbr: 0,
            a7: 0x10_fffc,
            instructions: None,
        };
        assert!(!position.to_string().contains("instructions"));
        let stopped = Stopped {
            at: Cycles(3),
            instructions: Some(9),
        };
        assert_eq!(stopped.to_string(), "stopped cycle=3 instructions=9");
    }

    #[test]
    fn a_reset_is_named_as_mc683xx_names_it() {
        let reset = Restart {
            at: Cycles(5),
            cause: Reset::Watchdog,
        };
        assert_eq!(reset.to_string(), "reset cycle=5 cause=Watchdog");
    }

    #[test]
    fn a_watched_instruction_is_one_line() {
        let code = Code {
            at: Cycles(0),
            pc: 0x1dee6,
            sr: 0x2000,
            data: [0; 8],
            address: [1; 8],
            stack: [2; 8],
        };
        let line = code.to_string();
        assert!(!line.contains('\n'), "{line}");
        assert!(line.contains(" a=00000001,00000001,"), "{line}");
    }
}
