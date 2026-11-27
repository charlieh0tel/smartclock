//! The machine: a CPU core on the board, run a slice at a time, with
//! time moved on and interrupts delivered between slices.
//!
//! A slice ends at the board's next event, or the next event of what
//! runs alongside it (a script's step, the GPS engine's second), or
//! after `SLICE` cycles, whichever comes first, so a device's interrupt
//! is taken within a slice of being asked for, a polled flag changes on
//! time, and a step or a second is taken at its own time, not up to a
//! slice late.  While
//! the CPU is stopped, waiting for an interrupt, time jumps to the next
//! event instead of being spent.
//!
//! The board's time keeps to the wall clock's as `run::Pace` keeps it.

use std::cell::RefCell;
use std::rc::Rc;

use crate::board::Board;
use crate::board::DEVICE_REGIONS;
use crate::board::FLASH;
use crate::board::FLASH_SIZE;
use crate::board::RAM;
use crate::board::RAM_SIZE;
use crate::board::Reset;
use crate::board::time::Cycles;
use crate::cpu::Cpu;
use crate::cpu::Error;
use crate::cpu::Register;
use crate::cpu::Stop;
use crate::run::Alongside;
use crate::run::Alongsides;
use crate::run::Flow;
use crate::run::Halted;
use crate::run::Pace;
use crate::run::Pacing;
use crate::run::SLICE;
use crate::trace;
use crate::trace::Exception;
use crate::trace::POSITION_EVERY;
use crate::trace::Position;
use crate::trace::Restart;
use crate::trace::Stopped;
use crate::trace::Tracing;

/// The shortest, so that a run always makes headway.
const LEAST_SLICE: Cycles = Cycles(64);
/// Why the machine stopped.
#[derive(Debug, thiserror::Error)]
pub(crate) enum Halt {
    /// The core failed.
    #[error(transparent)]
    Core(#[from] Error),
    /// The CPU halted.
    #[error(transparent)]
    Halted(#[from] Halted),
    /// What runs alongside failed the run: a script's expect not met in
    /// time.
    #[error("{0}")]
    Failed(String),
}

/// The machine.
pub(crate) struct Machine {
    /// The CPU.
    cpu: Box<dyn Cpu>,
    /// The board, which the CPU's device regions reach too.
    board: Rc<RefCell<Board>>,
    /// The board's time kept to the wall clock's, or not.
    pace: Pace,
    /// What is traced.
    tracing: Tracing,
    /// When to next say where the CPU is, when tracing it.
    next_position: Option<Cycles>,
    /// What runs off the board on its clock, a serial line's far end
    /// such as the synthetic GPS engine: each is advanced to the
    /// board's time after each slice.
    alongside: Alongsides,
}

impl std::fmt::Debug for Machine {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        formatter
            .debug_struct("Machine")
            .field("board", &self.board)
            .finish_non_exhaustive()
    }
}

/// How a machine runs.
#[derive(Debug, Clone, Default)]
pub(crate) struct Options {
    /// How its time keeps to the wall clock's.
    pub(crate) pacing: Pacing,
    /// What it traces.
    pub(crate) tracing: Tracing,
    /// Device addresses whose accesses to trace.
    pub(crate) watch: Vec<u32>,
    /// Instructions to trace each time they run.
    pub(crate) watch_code: Vec<u32>,
    /// Count the instructions run.
    pub(crate) count_instructions: bool,
}

impl Machine {
    /// The machine with `cpu` on `board`, `image` in its flash, reset,
    /// running as `options` say.
    pub(crate) fn new(
        mut cpu: Box<dyn Cpu>,
        board: Rc<RefCell<Board>>,
        image: &[u8],
        options: &Options,
    ) -> Result<Self, Error> {
        cpu.map_memory(FLASH, FLASH_SIZE, false)?;
        cpu.write(FLASH, image)?;
        cpu.prepare_code(FLASH, image)?;
        cpu.map_memory(RAM, RAM_SIZE, true)?;
        for (at, size) in DEVICE_REGIONS {
            cpu.map_device(at, size)?;
        }
        cpu.trace(options.tracing);
        if options.count_instructions {
            cpu.count_instructions()?;
        }
        for at in &options.watch {
            cpu.watch(*at);
        }
        for at in &options.watch_code {
            cpu.watch_code(*at)?;
        }
        cpu.reset()?;
        trace::emit(&Restart {
            at: board.borrow().now(),
            cause: Reset::PowerOn,
        });
        Ok(Self {
            cpu,
            board,
            pace: Pace::new(options.pacing),
            tracing: options.tracing,
            next_position: options.tracing.position.then_some(Cycles(0)),
            alongside: Alongsides::default(),
        })
    }

    /// Advances `alongside` to the board's time after each slice.
    pub(crate) fn run_alongside(&mut self, alongside: Box<dyn Alongside>) {
        self.alongside.push(alongside);
    }

    /// Runs one slice: whether the run goes on.
    pub(crate) fn step(&mut self) -> Result<Flow, Halt> {
        let (now, budget) = {
            let board = self.board.borrow();
            let now = board.now();
            let limit = self
                .alongside
                .next_event()
                .map_or(SLICE, |at| now.until(at).min(SLICE));
            (now, board.until_next_event(limit).max(LEAST_SLICE))
        };
        let run = self.cpu.run(now, budget)?;
        let elapsed = match run.stop {
            Stop::Ran => run.cycles,
            Stop::Stopped => budget,
            Stop::Halted(reason) => {
                return Err(Halt::Halted(Halted {
                    pc: self.cpu.register(Register::ProgramCounter)?,
                    reason,
                }));
            }
        };
        let reset = {
            let mut board = self.board.borrow_mut();
            board.advance(elapsed);
            board.take_reset()
        };
        let now = self.board.borrow().now();
        // a failure first, then a stop
        let flow = self.alongside.advance(now);
        if let Some(cause) = reset {
            trace::emit(&Restart { at: now, cause });
            self.board.borrow_mut().reset(cause);
            self.cpu.reset()?;
        }
        let request = self.board.borrow().interrupt();
        if let Some(request) = request {
            // the CPU's state before it is taken, for the trace
            let before = if self.tracing.exceptions {
                Some((
                    self.cpu.register(Register::ProgramCounter)?,
                    self.cpu.register(Register::Status)?,
                ))
            } else {
                None
            };
            if self.cpu.interrupt(request.level, request.vector)? {
                self.board.borrow_mut().acknowledge(request);
                if let Some((pc, sr)) = before {
                    trace::emit(&Exception {
                        at: self.board.borrow().now(),
                        vector: request.vector,
                        pc,
                        // the status register is 16 bits
                        sr: sr as u16,
                        level: Some(request.level),
                    });
                }
            }
        }
        self.pace.keep(self.board.borrow().now());
        self.trace_position()?;
        Ok(flow)
    }

    /// When tracing it, says where the CPU is each `POSITION_EVERY`.
    fn trace_position(&mut self) -> Result<(), Error> {
        let Some(next) = self.next_position else {
            return Ok(());
        };
        let now = self.board.borrow().now();
        if now >= next {
            trace::emit(&Position {
                at: now,
                pc: self.cpu.register(Register::ProgramCounter)?,
                sr: self.cpu.register(Register::Status)?,
                vbr: self.cpu.register(Register::VectorBase)?,
                a7: self.cpu.register(Register::Address(7))?,
                instructions: self.cpu.instructions(),
            });
            self.next_position = Some(now + POSITION_EVERY);
        }
        Ok(())
    }

    /// Runs until the board's clock reaches `until`, or what runs
    /// alongside stops it, or forever: what it ran, at the end.
    pub(crate) fn run(&mut self, until: Option<Cycles>) -> Result<Stopped, Halt> {
        while until.is_none_or(|until| self.board.borrow().now() < until) {
            match self.step()? {
                Flow::Continue => {}
                Flow::Stop => break,
                Flow::Fail(why) => return Err(Halt::Failed(why)),
            }
        }
        Ok(Stopped {
            at: self.board.borrow().now(),
            instructions: self.cpu.instructions(),
        })
    }
}

#[cfg(test)]
mod tests {
    use std::cell::RefCell;
    use std::rc::Rc;

    use super::Halt;
    use super::Machine;
    use super::Options;
    use crate::board::Board;
    use crate::board::Bus;
    use crate::board::eeprom::Eeprom;
    use crate::board::profile::Model;
    use crate::board::time::Cycles;
    use crate::cpu::Cpu;
    use crate::cpu::Error;
    use crate::cpu::Register;
    use crate::cpu::Run;
    use crate::cpu::Stop;
    use crate::cpu::frame::admits;
    use crate::line::Line;
    use crate::run::Alongside;
    use crate::run::Flow;
    use crate::run::Halted;
    use crate::run::Pacing;
    use crate::trace::Tracing;

    /// The interrupts a fake core took, level and vector.
    type Taken = Rc<RefCell<Vec<(u8, u8)>>>;

    /// A core that spends its budget doing nothing, at interrupt mask
    /// 0, and remembers the interrupts it took; or one that stops, or
    /// halts.
    struct Fake {
        /// How each run ends.
        stop: Stop,
        /// The interrupts taken.
        taken: Taken,
    }

    impl Cpu for Fake {
        fn register(&mut self, register: Register) -> Result<u32, Error> {
            match register {
                Register::ProgramCounter => Ok(0x15f88),
                Register::Status => Ok(0x2000),
                _ => Err(Error::Core(format!("no {register:?} here"))),
            }
        }
        fn write(&mut self, _at: u32, _bytes: &[u8]) -> Result<(), Error> {
            Ok(())
        }
        fn map_memory(&mut self, _at: u32, _size: u32, _writable: bool) -> Result<(), Error> {
            Ok(())
        }
        fn map_device(&mut self, _at: u32, _size: u32) -> Result<(), Error> {
            Ok(())
        }
        fn trace(&mut self, _tracing: Tracing) {}
        fn watch(&mut self, _at: u32) {}
        fn watch_code(&mut self, _at: u32) -> Result<(), Error> {
            Ok(())
        }
        fn count_instructions(&mut self) -> Result<(), Error> {
            Ok(())
        }
        fn instructions(&self) -> Option<u64> {
            None
        }
        fn prepare_code(&mut self, _at: u32, _bytes: &[u8]) -> Result<(), Error> {
            Ok(())
        }
        fn reset(&mut self) -> Result<(), Error> {
            Ok(())
        }
        fn interrupt(&mut self, level: u8, vector: u8) -> Result<bool, Error> {
            let admitted = admits(0x2000, level);
            if admitted {
                self.taken.borrow_mut().push((level, vector));
            }
            Ok(admitted)
        }
        fn run(&mut self, _now: Cycles, budget: Cycles) -> Result<Run, Error> {
            Ok(Run {
                cycles: budget,
                stop: self.stop.clone(),
            })
        }
    }

    /// A machine on a fake core whose runs end as `stop`, the periodic
    /// timer programmed as the reset code programs it, and the
    /// interrupts it takes.
    fn machine(stop: Stop) -> (Machine, Taken) {
        let board = Rc::new(RefCell::new(Board::new(
            Model::Z3805A,
            Line::new("sci", false),
            Line::new("gps", false),
            Line::new("host", false),
            Eeprom::new(Vec::new(), None),
        )));
        board.borrow_mut().write(0xff_fa22, 2, 0x0542, Cycles(0));
        board.borrow_mut().write(0xff_fa24, 2, 0x0008, Cycles(0));
        let taken = Taken::default();
        let cpu = Box::new(Fake {
            stop,
            taken: Rc::clone(&taken),
        });
        let options = Options {
            pacing: Pacing::Free,
            ..Options::default()
        };
        let machine = Machine::new(cpu, board, &[], &options).expect("a machine");
        (machine, taken)
    }

    #[test]
    fn each_periodic_tick_is_delivered_once() {
        let (mut machine, taken) = machine(Stop::Ran);
        machine.run(Some(Cycles(3 * 16384))).expect("runs");
        assert_eq!(*taken.borrow(), [(5, 0x42); 3]);
    }

    #[test]
    fn a_stopped_cpu_still_sees_the_ticks() {
        let (mut machine, taken) = machine(Stop::Stopped);
        machine.run(Some(Cycles(2 * 16384))).expect("runs");
        assert_eq!(taken.borrow().len(), 2);
    }

    #[test]
    fn what_runs_alongside_is_advanced_to_the_board_time_each_slice() {
        let (mut machine, _) = machine(Stop::Ran);
        let seen = Rc::new(RefCell::new(Vec::new()));
        let record = Rc::clone(&seen);
        machine.run_alongside(Box::new(move |now| {
            record.borrow_mut().push(now);
            Flow::Continue
        }));
        let stopped = machine.run(Some(Cycles(2 * 16384))).expect("runs");
        let seen = seen.borrow();
        assert!(seen.len() >= 2);
        assert!(seen.windows(2).all(|pair| pair[0] < pair[1]));
        assert_eq!(seen.last(), Some(&stopped.at));
    }

    #[test]
    fn a_run_stops_at_its_end_and_says_when() {
        let (mut machine, _) = machine(Stop::Ran);
        let stopped = machine.run(Some(Cycles(100_000))).expect("runs");
        assert!(stopped.at >= Cycles(100_000), "{stopped}");
        assert!(
            stopped.at < Cycles(100_000 + 4096),
            "within a slice: {stopped}"
        );
        assert_eq!(stopped.instructions, None, "not counted");
    }

    /// Something alongside with an event of its own, and the times it
    /// was advanced to.
    struct Timed {
        at: Cycles,
        seen: Rc<RefCell<Vec<Cycles>>>,
    }

    impl Alongside for Timed {
        fn advance(&mut self, now: Cycles) -> Flow {
            self.seen.borrow_mut().push(now);
            Flow::Continue
        }

        fn next_event(&self) -> Option<Cycles> {
            Some(self.at)
        }
    }

    #[test]
    fn an_event_alongside_ends_a_slice_exactly() {
        let (mut machine, _) = machine(Stop::Ran);
        let seen = Rc::new(RefCell::new(Vec::new()));
        // not on a slice's or a periodic tick's boundary
        let at = Cycles(3 * 4096 + 1234);
        machine.run_alongside(Box::new(Timed {
            at,
            seen: Rc::clone(&seen),
        }));
        machine.run(Some(Cycles(5 * 4096))).expect("runs");
        assert!(seen.borrow().contains(&at), "{:?}", seen.borrow());
    }

    #[test]
    fn what_runs_alongside_can_fail_the_run() {
        let (mut machine, _) = machine(Stop::Ran);
        machine.run_alongside(Box::new(|_| Flow::Fail("why".to_owned())));
        let halt = machine.run(None).expect_err("fails");
        assert!(
            matches!(halt, Halt::Failed(ref why) if why == "why"),
            "{halt}"
        );
    }

    #[test]
    fn what_runs_alongside_can_stop_the_run() {
        let (mut machine, _) = machine(Stop::Ran);
        machine.run_alongside(Box::new(|now| {
            if now >= Cycles(10_000) {
                Flow::Stop
            } else {
                Flow::Continue
            }
        }));
        let stopped = machine.run(None).expect("runs");
        assert!(stopped.at >= Cycles(10_000), "{stopped}");
        assert!(stopped.at < Cycles(10_000 + 4096), "{stopped}");
    }

    #[test]
    fn a_halt_says_where() {
        let (mut machine, _) = machine(Stop::Halted("a double bus fault".to_owned()));
        let halt = machine.step().expect_err("halts");
        assert!(
            matches!(halt, Halt::Halted(Halted { pc: 0x15f88, .. })),
            "{halt}"
        );
        assert_eq!(
            halt.to_string(),
            "the CPU halted at 0x00015f88: a double bus fault",
            "as mc683xx's says it"
        );
    }
}
