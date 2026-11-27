//! The Unicorn core, behind a trait: an MC68331's CPU32, on
//! smartclock's own map of the board.  The `unicorn` feature builds
//! it, for development and as the oracle the default core, mc683xx,
//! which runs the board file instead (`mc683xx`), is checked
//! against.
//!
//! A core is a whole CPU: it executes instructions and takes its own
//! exceptions, traps and `rte` among them, as the CPU32 does
//! (MC68331UM, section 5; CPU32RM, exception processing).  What it
//! cannot do alone is reach the devices, which it does through the
//! board's `Bus` for the regions mapped as devices, and know when a
//! device interrupts, which the machine tells it between runs.

pub(crate) mod frame;

use std::cell::RefCell;
use std::rc::Rc;

use crate::board::Bus;
use crate::board::time::Cycles;
use crate::trace::Tracing;

/// A register of the CPU32's programming model.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) enum Register {
    /// An address register, 0 to 7; A7 is the stack pointer of the
    /// mode the status register selects.
    Address(u8),
    /// The program counter.
    ProgramCounter,
    /// The status register.
    Status,
    /// The vector base register.
    VectorBase,
}

/// How a run ended.
#[derive(Debug, Clone, PartialEq, Eq)]
pub(crate) enum Stop {
    /// It used its budget.
    Ran,
    /// The CPU executed STOP and waits for an interrupt.
    Stopped,
    /// The CPU cannot go on: a double fault, an access to no memory
    /// the core can take an exception for, or a fault of the core.
    Halted(String),
}

/// A run's outcome: its cycles and how it ended.
#[derive(Debug, Clone, PartialEq, Eq)]
pub(crate) struct Run {
    /// The cycles it took, an estimate where the core does not count
    /// them exactly.
    pub(crate) cycles: Cycles,
    /// How it ended.
    pub(crate) stop: Stop,
}

/// What can go wrong driving a core.
#[derive(Debug, thiserror::Error)]
pub(crate) enum Error {
    /// This build has no Unicorn core.
    #[cfg(not(feature = "unicorn"))]
    #[error(
        "smartclock-emu was built without its Unicorn core; build it with the `unicorn` \
         feature (`make emulator-unicorn`), or run the default core, mc683xx"
    )]
    NoCore,
    /// The core refused a request.
    #[error("the CPU core: {0}")]
    Core(String),
}

/// A CPU32 core.
pub(crate) trait Cpu {
    /// A register.
    fn register(&mut self, register: Register) -> Result<u32, Error>;
    /// Writes memory at `at`, whatever its protection.
    fn write(&mut self, at: u32, bytes: &[u8]) -> Result<(), Error>;
    /// Maps `size` bytes of memory at `at`, writable or not by the
    /// program; it executes from either.
    fn map_memory(&mut self, at: u32, size: u32, writable: bool) -> Result<(), Error>;
    /// Maps `size` bytes at `at` to the bus: every access there is a
    /// device's.
    fn map_device(&mut self, at: u32, size: u32) -> Result<(), Error>;
    /// Traces what `tracing` asks of the core: every device access,
    /// every exception it takes itself (`trace`).
    fn trace(&mut self, tracing: Tracing);
    /// Traces each device access that touches `at`.
    fn watch(&mut self, at: u32);
    /// Traces each time the instruction at `at` runs, with the
    /// registers and the top of the stack.
    fn watch_code(&mut self, at: u32) -> Result<(), Error>;
    /// Counts the instructions run from now on.
    fn count_instructions(&mut self) -> Result<(), Error>;
    /// The instructions run, if counted.
    fn instructions(&self) -> Option<u64>;
    /// Patches over the core's own faults the code from `at`, `bytes`
    /// long, needs: instructions it gets wrong, which it then does
    /// right wherever this code holds them.
    fn prepare_code(&mut self, at: u32, bytes: &[u8]) -> Result<(), Error>;
    /// Resets the CPU: the supervisor stack and the program counter
    /// from the vectors at address zero, the status register at
    /// interrupt level 7 in supervisor mode, the vector base zero.
    fn reset(&mut self) -> Result<(), Error>;
    /// Asks for an interrupt at `level` through `vector`: taken, and
    /// true, if the interrupt mask admits it.
    fn interrupt(&mut self, level: u8, vector: u8) -> Result<bool, Error>;
    /// Runs for about `budget` cycles from `now` on the board's clock,
    /// which the trace's records are stamped with.
    fn run(&mut self, now: Cycles, budget: Cycles) -> Result<Run, Error>;
}

/// The core this build has: none.
#[cfg(not(feature = "unicorn"))]
pub(crate) fn core(bus: Rc<RefCell<dyn Bus>>) -> Result<Box<dyn Cpu>, Error> {
    drop(bus);
    Err(Error::NoCore)
}
