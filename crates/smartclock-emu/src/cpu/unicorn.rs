//! The Unicorn core: QEMU's m68k through the `unicorn-engine` crate,
//! as a 68020, which runs the CPU32's instructions (the harness's
//! choice, `tools/firmware/libc_check.py`).
//!
//! Unicorn executes instructions but takes no exceptions: it hands each
//! one to its interrupt hook, the program counter at the instruction,
//! and goes on wherever the hook leaves it.  This core takes them as
//! the CPU32 does, with `frame`: `trap #n` and the faults vector
//! through the table at the vector base, and `rte` pops its frame.
//! Unicorn cannot read the vector base either, so the core follows it,
//! from a code hook on each `movec` to VBR in the image.
//!
//! What QEMU, under Unicorn, gets wrong, the core does in its place, as
//! the harness does (tools/firmware/harness.py): `rtr`, which it takes
//! for an illegal instruction; `move (sp)+,sr`, which it pops after
//! switching to the user stack; and `stop`, which it runs past rather
//! than waiting for an interrupt.
//!
//! Nor can Unicorn read the status register without spoiling it: its
//! m68k register read (`qemu/target/m68k/unicorn.c`, `UC_M68K_REG_SR`)
//! marks QEMU's lazy condition codes as already computed before it
//! computes them, so it returns, and leaves the CPU to go on with,
//! whatever operands the last flag-setting instruction left, wrong
//! codes between a compare and its branch.  The core reads the system
//! byte from a copy of the CPU's state, which the read spoils instead,
//! and the codes with a probe of its own, `move ccr`, which QEMU
//! computes rightly; and since the probe runs the CPU, which a hook
//! cannot, it takes an exception Unicorn raises once the run has
//! stopped, outside the hook.  The probe's page is the core's, where
//! the board has nothing: the program's reaching it, but for the probe
//! run between the machine's slices, is an invalid access.
//!
//! Time is an estimate: two cycles a byte of each block of code run,
//! about the CPU32's average (CPU32RM, appendix on instruction timing),
//! counted by a block hook as the block starts.  The hook ends the run
//! at the first block to start with the budget spent; a block it ends
//! the run at does not run (Unicorn calls the hook before the block's
//! check for a stop), and is not counted.  A run's first block always
//! runs, whatever it costs, so that a run makes headway on a budget
//! smaller than its block.

use std::cell::RefCell;
use std::rc::Rc;

use unicorn_engine::Arch;
use unicorn_engine::HookType;
use unicorn_engine::M68kCpuModel;
use unicorn_engine::MemType;
use unicorn_engine::Mode;
use unicorn_engine::Prot;
use unicorn_engine::RegisterM68K;
use unicorn_engine::Unicorn;
use unicorn_engine::uc_error;

use super::Cpu;
use super::Error;
use super::Register;
use super::Run;
use super::Stop;
use super::frame::Format;
use super::frame::SUPERVISOR;
use super::frame::VECTOR_SIZE;
use super::frame::admits;
use super::frame::entered;
use super::frame::frame;
use super::frame::popped;
use crate::board::Bus;
use crate::board::time::Cycles;
use crate::trace;
use crate::trace::Access;
use crate::trace::Code;
use crate::trace::Exception;
use crate::trace::Tracing;

/// Cycles a byte of code run, the estimate.
const CYCLES_PER_BYTE: u64 = 2;

/// Where a run would end by address: nowhere a 32-bit PC reaches.
const NO_END: u64 = u64::MAX;

/// QEMU's exception numbers (target/m68k/cpu.h), which Unicorn hands
/// the interrupt hook: the CPU32's vector numbers, and `rte`.
const EXCEPTION_ACCESS: u32 = 2;
const EXCEPTION_ADDRESS: u32 = 3;
const EXCEPTION_ILLEGAL: u32 = 4;
const EXCEPTION_ZERO_DIVIDE: u32 = 5;
const EXCEPTION_CHK: u32 = 6;
const EXCEPTION_TRAPCC: u32 = 7;
const EXCEPTION_PRIVILEGE: u32 = 8;
const EXCEPTION_LINE_A: u32 = 10;
const EXCEPTION_LINE_F: u32 = 11;
const EXCEPTION_TRAP_FIRST: u32 = 32;
const EXCEPTION_TRAP_LAST: u32 = 47;
const EXCEPTION_RTE: u32 = 0x100;

/// The instructions the core does in QEMU's place, and how long each
/// is.
const RTR: u16 = 0x4e77;
const STATUS_POP: u16 = 0x46df;
const STOP: u16 = 0x4e72;
const STOP_SIZE: u32 = 4;
const TRAP_SIZE: u32 = 2;
const WORD: u32 = 2;
/// `movec Rn,VBR`: the opcode, and in its extension word the control
/// register's field, VBR's number, and the general register's field.
const MOVEC_TO: u16 = 0x4e7b;
const MOVEC_CONTROL_MASK: u16 = 0x0fff;
const MOVEC_VBR: u16 = 0x0801;
const MOVEC_REGISTER_SHIFT: u16 = 12;
const MOVEC_ADDRESS_BIT: u16 = 0x8;
const MOVEC_NUMBER_MASK: u16 = 0x7;

/// The condition codes, the status register's low byte, which `rtr`
/// restores.
const CONDITION_CODES: u16 = 0x00ff;
/// The status register at reset: supervisor, level 7.
const RESET_STATUS: u16 = 0x2700;

/// The core's own page, where the board has nothing (`board`'s memory
/// map), holding the probe that reads the condition codes: executable
/// only, so that the program's reading or writing it is an invalid
/// access, and its running it is one too while no probe runs.
const PROBE: u32 = 0x00f0_0000;
const PROBE_SIZE: u32 = 0x1000;
/// The probe, `move.w ccr,d0`, one word: D0 is saved and put back
/// around it.
const MOVE_FROM_CCR_TO_D0: u16 = 0x42c0;
const PROBE_LENGTH: u32 = 2;

/// The registers by number.
const DATA: [RegisterM68K; 8] = [
    RegisterM68K::D0,
    RegisterM68K::D1,
    RegisterM68K::D2,
    RegisterM68K::D3,
    RegisterM68K::D4,
    RegisterM68K::D5,
    RegisterM68K::D6,
    RegisterM68K::D7,
];
const ADDRESS: [RegisterM68K; 8] = [
    RegisterM68K::A0,
    RegisterM68K::A1,
    RegisterM68K::A2,
    RegisterM68K::A3,
    RegisterM68K::A4,
    RegisterM68K::A5,
    RegisterM68K::A6,
    RegisterM68K::A7,
];

/// What the core's hooks share with it.
struct Shared {
    /// The devices.
    bus: Rc<RefCell<dyn Bus>>,
    /// The vector base, as the last `movec` to it set it.
    vector_base: u32,
    /// The board's clock when this run started, the cycles spent in
    /// it, and its budget.
    started: u64,
    spent: u64,
    budget: u64,
    /// STOP ran: the CPU waits for an interrupt.
    stopped: bool,
    /// Why the CPU cannot go on, once it cannot.
    halted: Option<String>,
    /// The exception the interrupt hook stopped the run to have taken.
    pending: Option<Pending>,
    /// What is traced.
    tracing: Tracing,
    /// Device addresses whose accesses are traced.
    watched: Vec<u32>,
    /// The instructions run, once counted.
    instructions: Option<u64>,
    /// The probe runs: the program is not in its page.
    probing: bool,
}

impl Shared {
    /// Now: the board's clock at the end of the block running.
    fn now(&self) -> Cycles {
        Cycles(self.started + self.spent)
    }
}

/// Traces an access of `size` bytes at `address`, if every access is or
/// it touches a watched address.
fn traced(engine: &Unicorn<'_, Shared>, address: u32, size: usize, write: bool, value: u32) {
    let shared = engine.get_data();
    let end = address + size as u32;
    if shared.tracing.devices
        || shared
            .watched
            .iter()
            .any(|watched| (address..end).contains(watched))
    {
        trace::emit(&Access {
            at: shared.now(),
            pc: get(engine, RegisterM68K::PC).unwrap_or(0),
            write,
            address,
            size,
            value,
        });
    }
}

/// Traces an exception about to be taken through `vector` at `at`,
/// from status `sr`.
fn trace_exception(engine: &Unicorn<'_, Shared>, vector: u8, at: u32, sr: u16) {
    let shared = engine.get_data();
    if shared.tracing.exceptions {
        trace::emit(&Exception {
            at: shared.now(),
            vector,
            pc: at,
            sr,
            level: None,
        });
    }
}

/// The Unicorn core.
pub(super) struct Core {
    /// The engine, its hooks' state its data.
    engine: Unicorn<'static, Shared>,
}

impl std::fmt::Debug for Core {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        formatter.debug_struct("Core").finish_non_exhaustive()
    }
}

/// A Unicorn error as the core's.
fn failed(what: &str) -> impl FnOnce(uc_error) -> Error + '_ {
    move |error| Error::Core(format!("{what}: {error:?}"))
}

/// A register of the engine, 32 bits.
fn get(engine: &Unicorn<'_, Shared>, register: RegisterM68K) -> Result<u32, uc_error> {
    // the m68k's registers are 32 bits; the engine widens them
    engine.reg_read(register).map(|value| value as u32)
}

fn set(
    engine: &mut Unicorn<'_, Shared>,
    register: RegisterM68K,
    value: u32,
) -> Result<(), uc_error> {
    engine.reg_write(register, u64::from(value))
}

fn read_word(engine: &Unicorn<'_, Shared>, at: u32) -> Result<u16, uc_error> {
    let mut bytes = [0; 2];
    engine.mem_read(u64::from(at), &mut bytes)?;
    Ok(u16::from_be_bytes(bytes))
}

fn read_long(engine: &Unicorn<'_, Shared>, at: u32) -> Result<u32, uc_error> {
    let mut bytes = [0; 4];
    engine.mem_read(u64::from(at), &mut bytes)?;
    Ok(u32::from_be_bytes(bytes))
}

/// The status register's system byte, its condition codes clear: read
/// from a copy of the CPU's state, which Unicorn's read spoils in place
/// of the CPU's own (the module's comment); so safe in a hook.
fn system_status(engine: &Unicorn<'_, Shared>) -> Result<u16, uc_error> {
    let copy = engine.context_init()?;
    // the status register is 16 bits; the engine widens it
    copy.reg_read(RegisterM68K::SR)
        .map(|value| value as u16 & !CONDITION_CODES)
}

/// The condition codes, as the probe reads them; outside a hook, since
/// it runs the CPU.  The probe leaves the codes and every register but
/// the program counter, which is put back, as they were, and is not
/// counted among the run's cycles or instructions.
fn condition_codes(engine: &mut Unicorn<'_, Shared>) -> Result<u16, uc_error> {
    let pc = get(engine, RegisterM68K::PC)?;
    let d0 = get(engine, RegisterM68K::D0)?;
    let (spent, budget, instructions) = {
        let shared = engine.get_data_mut();
        let run = (shared.spent, shared.budget, shared.instructions);
        shared.budget = u64::MAX;
        shared.probing = true;
        run
    };
    let probed = engine.emu_start(u64::from(PROBE), u64::from(PROBE + PROBE_LENGTH), 0, 0);
    let shared = engine.get_data_mut();
    shared.spent = spent;
    shared.budget = budget;
    shared.instructions = instructions;
    shared.probing = false;
    probed?;
    // the codes are the low byte of the word moved
    let codes = get(engine, RegisterM68K::D0)? as u16 & CONDITION_CODES;
    set(engine, RegisterM68K::D0, d0)?;
    set(engine, RegisterM68K::PC, pc)?;
    Ok(codes)
}

/// The status register, 16 bits, its condition codes the probe's;
/// outside a hook.
fn status(engine: &mut Unicorn<'_, Shared>) -> Result<u16, uc_error> {
    Ok(system_status(engine)? | condition_codes(engine)?)
}

/// An exception Unicorn raised, which the core takes once the run has
/// stopped (the module's comment): its vector, the instruction it was
/// raised at, where it returns to, and its frame.
#[derive(Debug, Clone, Copy)]
struct Pending {
    vector: u8,
    at: u32,
    return_to: u32,
    format: Format,
}

/// Takes an exception through `vector` from status `was`: the frame on
/// the supervisor stack, the handler from the vector table.
fn take(
    engine: &mut Unicorn<'_, Shared>,
    was: u16,
    vector: u8,
    return_to: u32,
    format: Format,
    level: Option<u8>,
) -> Result<(), uc_error> {
    // the stacks switch as the supervisor bit sets
    set(engine, RegisterM68K::SR, u32::from(entered(was, level)))?;
    let pushed = frame(was, return_to, vector, format);
    let stack = get(engine, RegisterM68K::A7)? - pushed.len() as u32;
    engine.mem_write(u64::from(stack), &pushed)?;
    set(engine, RegisterM68K::A7, stack)?;
    let table = engine.get_data().vector_base;
    let handler = read_long(engine, table + u32::from(vector) * VECTOR_SIZE)?;
    set(engine, RegisterM68K::PC, handler)
}

/// `rte`: pops its frame, the stack pointer first, so that the status
/// register switches the stacks after.
fn return_from_exception(engine: &mut Unicorn<'_, Shared>) -> Result<Option<String>, uc_error> {
    let stack = get(engine, RegisterM68K::A7)?;
    let mut top = [0; 8];
    engine.mem_read(u64::from(stack), &mut top)?;
    let Some(popped) = popped(top) else {
        return Ok(Some(format!(
            "rte of a frame of format {:#x} at {stack:#08x}",
            top[6] >> 4
        )));
    };
    set(engine, RegisterM68K::A7, stack + popped.size)?;
    set(engine, RegisterM68K::SR, u32::from(popped.status))?;
    set(engine, RegisterM68K::PC, popped.return_to)?;
    Ok(None)
}

/// `rtr`: pops the condition codes, then the return address.
fn return_and_restore(engine: &mut Unicorn<'_, Shared>) -> Result<(), uc_error> {
    let stack = get(engine, RegisterM68K::A7)?;
    let codes = read_word(engine, stack)?;
    let return_to = read_long(engine, stack + WORD)?;
    set(engine, RegisterM68K::A7, stack + WORD + 4)?;
    let was = system_status(engine)?;
    set(
        engine,
        RegisterM68K::SR,
        u32::from(was | codes & CONDITION_CODES),
    )?;
    set(engine, RegisterM68K::PC, return_to)
}

/// Stops the run to have `pending` taken once it has.
fn defer(engine: &mut Unicorn<'_, Shared>, pending: Pending) -> Result<(), uc_error> {
    engine.get_data_mut().pending = Some(pending);
    engine.emu_stop()
}

/// The interrupt hook: an exception Unicorn raised, number `number`,
/// the program counter at the instruction.
fn exception(engine: &mut Unicorn<'_, Shared>, number: u32) {
    let result = (|| {
        let at = get(engine, RegisterM68K::PC)?;
        // the vector numbers fit a byte, the match says which
        let vector = number as u8;
        let pending = |return_to, format| Pending {
            vector,
            at,
            return_to,
            format,
        };
        match number {
            EXCEPTION_RTE => return return_from_exception(engine),
            EXCEPTION_ILLEGAL if read_word(engine, at)? == RTR => {
                // a hook cannot run the probe, so the condition codes
                // are left out
                trace_exception(engine, vector, at, system_status(engine)?);
                return_and_restore(engine)?;
            }
            EXCEPTION_TRAP_FIRST..=EXCEPTION_TRAP_LAST => {
                defer(engine, pending(at + TRAP_SIZE, Format::Short))?;
            }
            EXCEPTION_ILLEGAL | EXCEPTION_PRIVILEGE | EXCEPTION_LINE_A | EXCEPTION_LINE_F => {
                trace::note(format_args!("exception {number} at {at:#08x}"));
                defer(engine, pending(at, Format::Short))?;
            }
            EXCEPTION_ZERO_DIVIDE | EXCEPTION_CHK | EXCEPTION_TRAPCC => {
                trace::note(format_args!("exception {number} at {at:#08x}"));
                defer(engine, pending(at, Format::Instruction(at)))?;
            }
            EXCEPTION_ACCESS | EXCEPTION_ADDRESS => {
                trace_exception(engine, vector, at, system_status(engine)?);
                return Ok(Some(format!(
                    "a bus or address error, exception {number}, at {at:#08x}"
                )));
            }
            _ => {
                trace_exception(engine, vector, at, system_status(engine)?);
                return Ok(Some(format!("exception {number} at {at:#08x}")));
            }
        }
        Ok(None)
    })();
    let halted = match result {
        Ok(halted) => halted,
        Err(error) => Some(format!("taking exception {number}: {error:?}")),
    };
    if let Some(reason) = halted {
        engine.get_data_mut().halted = Some(reason);
        let _ = engine.emu_stop();
    }
}

/// An instruction the core does in QEMU's place, given its address.
type Instruction = fn(&mut Unicorn<'_, Shared>, u32) -> Result<(), uc_error>;

/// A code hook's work failed: the CPU halts.
fn hook_failed(engine: &mut Unicorn<'_, Shared>, what: &str, error: uc_error) {
    engine.get_data_mut().halted = Some(format!("{what}: {error:?}"));
    let _ = engine.emu_stop();
}

/// `move (sp)+,sr` at `at`, in supervisor mode; in user mode it is left
/// to raise its privilege violation.
fn status_pop(engine: &mut Unicorn<'_, Shared>, at: u32) -> Result<(), uc_error> {
    if system_status(engine)? & SUPERVISOR == 0 {
        return Ok(());
    }
    let stack = get(engine, RegisterM68K::A7)?;
    let popped = read_word(engine, stack)?;
    set(engine, RegisterM68K::A7, stack + WORD)?;
    set(engine, RegisterM68K::SR, u32::from(popped))?;
    set(engine, RegisterM68K::PC, at + WORD)
}

/// `stop #imm` at `at`, in supervisor mode: the status register takes
/// the immediate and the CPU waits for an interrupt, past the
/// instruction.
fn stop(engine: &mut Unicorn<'_, Shared>, at: u32) -> Result<(), uc_error> {
    if system_status(engine)? & SUPERVISOR == 0 {
        return Ok(());
    }
    let immediate = read_word(engine, at + WORD)?;
    set(engine, RegisterM68K::SR, u32::from(immediate))?;
    set(engine, RegisterM68K::PC, at + STOP_SIZE)?;
    engine.get_data_mut().stopped = true;
    engine.emu_stop()
}

/// `movec Rn,VBR` at `at`, about to run: the register it moves.
fn vector_base_set(engine: &mut Unicorn<'_, Shared>, at: u32) -> Result<(), uc_error> {
    let extension = read_word(engine, at + WORD)?;
    let field = extension >> MOVEC_REGISTER_SHIFT;
    let number = usize::from(field & MOVEC_NUMBER_MASK);
    let register = if field & MOVEC_ADDRESS_BIT != 0 {
        ADDRESS[number]
    } else {
        DATA[number]
    };
    let base = get(engine, register)?;
    engine.get_data_mut().vector_base = base;
    Ok(())
}

impl Core {
    /// A core reaching `bus` for its device regions.
    pub(super) fn new(bus: Rc<RefCell<dyn Bus>>) -> Result<Self, Error> {
        let shared = Shared {
            bus,
            vector_base: 0,
            started: 0,
            spent: 0,
            budget: 0,
            stopped: false,
            halted: None,
            pending: None,
            tracing: Tracing::default(),
            watched: Vec::new(),
            instructions: None,
            probing: false,
        };
        let mut engine = Unicorn::new_with_data(Arch::M68K, Mode::BIG_ENDIAN, shared)
            .map_err(failed("opening Unicorn"))?;
        engine
            .ctl_set_cpu_model(M68kCpuModel::M68020 as i32)
            .map_err(failed("choosing the 68020"))?;
        engine
            .add_block_hook(1, 0, |engine, _at, size| {
                let shared = engine.get_data_mut();
                if shared.spent > 0 && shared.spent >= shared.budget {
                    let _ = engine.emu_stop();
                } else {
                    shared.spent += u64::from(size) * CYCLES_PER_BYTE;
                }
            })
            .map_err(failed("hooking blocks"))?;
        engine
            .add_intr_hook(exception)
            .map_err(failed("hooking exceptions"))?;
        engine
            .add_mem_hook(
                HookType::MEM_INVALID,
                1,
                0,
                |engine, access: MemType, at, size, value| {
                    engine.get_data_mut().halted = Some(format!(
                        "{access:?} of {size} bytes at {at:#08x}{}, where nothing is",
                        if access == MemType::WRITE_UNMAPPED || access == MemType::WRITE_PROT {
                            format!(" of {value:#x}")
                        } else {
                            String::new()
                        }
                    ));
                    false
                },
            )
            .map_err(failed("hooking invalid accesses"))?;
        engine
            .mem_map(u64::from(PROBE), u64::from(PROBE_SIZE), Prot::EXEC)
            .map_err(failed("mapping the probe"))?;
        engine
            .mem_write(u64::from(PROBE), &MOVE_FROM_CCR_TO_D0.to_be_bytes())
            .map_err(failed("writing the probe"))?;
        // running the probe's page but for the probe: a hook on its
        // page alone, which costs the rest of the code nothing
        engine
            .add_code_hook(
                u64::from(PROBE),
                u64::from(PROBE + PROBE_SIZE - 1),
                |engine, at, _size| {
                    if !engine.get_data().probing {
                        engine.get_data_mut().halted =
                            Some(format!("a fetch at {at:#08x}, where nothing is"));
                        let _ = engine.emu_stop();
                    }
                },
            )
            .map_err(failed("hooking the probe's page"))?;
        Ok(Self { engine })
    }
}

impl Cpu for Core {
    fn register(&mut self, register: Register) -> Result<u32, Error> {
        let engine = &mut self.engine;
        let read = |engine: &Unicorn<'_, Shared>, register| {
            get(engine, register).map_err(failed("reading a register"))
        };
        match register {
            Register::Address(number) => read(engine, ADDRESS[usize::from(number & 7)]),
            Register::ProgramCounter => read(engine, RegisterM68K::PC),
            Register::Status => status(engine)
                .map(u32::from)
                .map_err(failed("reading the status register")),
            Register::VectorBase => Ok(engine.get_data().vector_base),
        }
    }

    fn write(&mut self, at: u32, bytes: &[u8]) -> Result<(), Error> {
        self.engine
            .mem_write(u64::from(at), bytes)
            .map_err(failed("writing memory"))
    }

    fn map_memory(&mut self, at: u32, size: u32, writable: bool) -> Result<(), Error> {
        let access = if writable {
            Prot::ALL
        } else {
            Prot::READ | Prot::EXEC
        };
        self.engine
            .mem_map(u64::from(at), u64::from(size), access)
            .map_err(failed("mapping memory"))
    }

    fn map_device(&mut self, at: u32, size: u32) -> Result<(), Error> {
        self.engine
            .mmio_map(
                u64::from(at),
                u64::from(size),
                Some(
                    move |engine: &mut Unicorn<'_, Shared>, offset: u64, size: usize| {
                        let bus = Rc::clone(&engine.get_data().bus);
                        // offsets are within the region, sizes 1, 2 or 4
                        let address = at + offset as u32;
                        let now = engine.get_data().now();
                        let value = bus.borrow_mut().read(address, size as u32, now);
                        traced(engine, address, size, false, value);
                        u64::from(value)
                    },
                ),
                Some(
                    move |engine: &mut Unicorn<'_, Shared>,
                          offset: u64,
                          size: usize,
                          value: u64| {
                        let bus = Rc::clone(&engine.get_data().bus);
                        let address = at + offset as u32;
                        traced(engine, address, size, true, value as u32);
                        let now = engine.get_data().now();
                        bus.borrow_mut()
                            .write(address, size as u32, value as u32, now);
                    },
                ),
            )
            .map_err(failed("mapping a device"))
    }

    fn trace(&mut self, tracing: Tracing) {
        self.engine.get_data_mut().tracing = tracing;
    }

    fn watch(&mut self, at: u32) {
        self.engine.get_data_mut().watched.push(at);
    }

    fn watch_code(&mut self, at: u32) -> Result<(), Error> {
        self.engine
            .add_code_hook(u64::from(at), u64::from(at), |engine, address, _size| {
                let registers =
                    |names: &[RegisterM68K; 8]| names.map(|name| get(engine, name).unwrap_or(0));
                let stack = get(engine, RegisterM68K::A7).unwrap_or(0);
                let mut top = [0; 32];
                let _ = engine.mem_read(u64::from(stack), &mut top);
                let (longs, _) = top.as_chunks::<4>();
                trace::emit(&Code {
                    at: engine.get_data().now(),
                    // a 32-bit core's addresses
                    pc: address as u32,
                    // a hook cannot run the probe, so the condition
                    // codes are left out
                    sr: system_status(engine).unwrap_or(0),
                    data: registers(&DATA),
                    address: registers(&ADDRESS),
                    stack: std::array::from_fn(|index| u32::from_be_bytes(longs[index])),
                });
            })
            .map(drop)
            .map_err(failed("hooking an instruction"))
    }

    fn count_instructions(&mut self) -> Result<(), Error> {
        if self.engine.get_data().instructions.is_some() {
            return Ok(());
        }
        self.engine.get_data_mut().instructions = Some(0);
        // a hook on every instruction, begin past end: the cost of
        // counting, which is why it is asked for
        self.engine
            .add_code_hook(1, 0, |engine, _address, _size| {
                if let Some(count) = &mut engine.get_data_mut().instructions {
                    *count += 1;
                }
            })
            .map(drop)
            .map_err(failed("hooking every instruction"))
    }

    fn instructions(&self) -> Option<u64> {
        self.engine.get_data().instructions
    }

    fn prepare_code(&mut self, at: u32, bytes: &[u8]) -> Result<(), Error> {
        let words: Vec<u16> = bytes
            .as_chunks::<2>()
            .0
            .iter()
            .map(|pair| u16::from_be_bytes(*pair))
            .collect();
        for (index, word) in words.iter().enumerate() {
            let extension = words.get(index + 1).copied();
            let hook: Option<(&'static str, Instruction)> = match *word {
                STATUS_POP => Some(("move (sp)+,sr", status_pop)),
                STOP => Some(("stop", stop)),
                MOVEC_TO
                    if extension
                        .is_some_and(|extension| extension & MOVEC_CONTROL_MASK == MOVEC_VBR) =>
                {
                    Some(("movec to vbr", vector_base_set))
                }
                _ => None,
            };
            let Some((name, hook)) = hook else {
                continue;
            };
            // an image is at most the flash's 512 KiB
            let address = u64::from(at) + 2 * index as u64;
            self.engine
                .add_code_hook(address, address, move |engine, address, _size| {
                    if let Err(error) = hook(engine, address as u32) {
                        hook_failed(engine, name, error);
                    }
                })
                .map_err(failed("hooking an instruction"))?;
        }
        Ok(())
    }

    fn reset(&mut self) -> Result<(), Error> {
        let engine = &mut self.engine;
        let stack = read_long(engine, 0).map_err(failed("reading the reset vector"))?;
        let start = read_long(engine, 4).map_err(failed("reading the reset vector"))?;
        set(engine, RegisterM68K::SR, u32::from(RESET_STATUS)).map_err(failed("resetting"))?;
        set(engine, RegisterM68K::A7, stack).map_err(failed("resetting"))?;
        set(engine, RegisterM68K::PC, start).map_err(failed("resetting"))?;
        let shared = engine.get_data_mut();
        shared.vector_base = 0;
        shared.stopped = false;
        shared.halted = None;
        shared.pending = None;
        Ok(())
    }

    fn interrupt(&mut self, level: u8, vector: u8) -> Result<bool, Error> {
        let engine = &mut self.engine;
        let was = status(engine).map_err(failed("reading the status register"))?;
        if !admits(was, level) {
            return Ok(false);
        }
        let at = get(engine, RegisterM68K::PC).map_err(failed("reading the program counter"))?;
        take(engine, was, vector, at, Format::Short, Some(level))
            .map_err(failed("taking an interrupt"))?;
        engine.get_data_mut().stopped = false;
        Ok(true)
    }

    fn run(&mut self, now: Cycles, budget: Cycles) -> Result<Run, Error> {
        if self.engine.get_data().stopped {
            return Ok(Run {
                cycles: budget,
                stop: Stop::Stopped,
            });
        }
        {
            let shared = self.engine.get_data_mut();
            shared.started = now.0;
            shared.spent = 0;
            shared.budget = budget.0;
        }
        let outcome = loop {
            let engine = &mut self.engine;
            let at =
                get(engine, RegisterM68K::PC).map_err(failed("reading the program counter"))?;
            let outcome = engine.emu_start(u64::from(at), NO_END, 0, 0);
            let Some(pending) = engine.get_data_mut().pending.take() else {
                break outcome;
            };
            if outcome.is_err() || engine.get_data().halted.is_some() {
                break outcome;
            }
            let taken = status(engine).and_then(|was| {
                trace_exception(engine, pending.vector, pending.at, was);
                take(
                    engine,
                    was,
                    pending.vector,
                    pending.return_to,
                    pending.format,
                    None,
                )
            });
            if let Err(error) = taken {
                engine.get_data_mut().halted =
                    Some(format!("taking exception {}: {error:?}", pending.vector));
                break Ok(());
            }
            let shared = engine.get_data();
            if shared.spent >= shared.budget {
                break Ok(());
            }
        };
        let shared = self.engine.get_data_mut();
        let cycles = Cycles(shared.spent);
        let stop = match (shared.halted.take(), outcome) {
            (Some(reason), _) => Stop::Halted(reason),
            (None, Err(error)) => Stop::Halted(format!("Unicorn stopped: {error:?}")),
            (None, Ok(())) if shared.stopped => Stop::Stopped,
            (None, Ok(())) => Stop::Ran,
        };
        Ok(Run { cycles, stop })
    }
}

#[cfg(test)]
mod tests {
    use std::cell::RefCell;
    use std::rc::Rc;

    use unicorn_engine::RegisterM68K;

    use super::Core;
    use super::get;
    use crate::board::Bus;
    use crate::board::time::Cycles;
    use crate::cpu::Cpu;
    use crate::cpu::Register;
    use crate::cpu::Stop;

    /// A bus with nothing on it: the tests run from memory alone.
    struct NoDevices;

    impl Bus for NoDevices {
        fn read(&mut self, _at: u32, _size: u32, _now: Cycles) -> u32 {
            0
        }
        fn write(&mut self, _at: u32, _size: u32, _value: u32, _now: Cycles) {}
    }

    /// Where the tests' code starts, and the handler every vector they
    /// take goes to.
    const START: u32 = 0x1000;
    const HANDLER: u32 = 0x2000;
    /// The vectors the tests take: `trap #0`'s, and a level 7
    /// interrupt's.
    const TRAP_0: u8 = 32;
    const LEVEL_7_VECTOR: u8 = 64;
    /// `rte`, the handler.
    const RTE: [u8; 2] = [0x4e, 0x73];
    /// D2 as the branch leaves it when not taken.
    const NOT_TAKEN: u32 = 2;

    /// A core on 64 KiB of memory, reset, its supervisor stack at
    /// `0x8000`, `code` at `START` and an `rte` at `HANDLER` for every
    /// vector the tests take.
    fn core(code: &[u16]) -> Core {
        let mut core = Core::new(Rc::new(RefCell::new(NoDevices))).expect("a core");
        core.map_memory(0, 0x1_0000, true).expect("memory");
        core.write(0, &0x8000_u32.to_be_bytes()).expect("the stack");
        core.write(4, &START.to_be_bytes()).expect("the start");
        for vector in [TRAP_0, LEVEL_7_VECTOR] {
            core.write(u32::from(vector) * 4, &HANDLER.to_be_bytes())
                .expect("a vector");
        }
        core.write(HANDLER, &RTE).expect("the handler");
        let bytes: Vec<u8> = code.iter().copied().flat_map(u16::to_be_bytes).collect();
        core.write(START, &bytes).expect("the code");
        core.reset().expect("reset");
        core
    }

    /// 1 compared with 2, which leaves N and C set and Z clear in
    /// QEMU's lazy form, after Z set in its computed form, where the
    /// compare leaves it to be read as set by a read that takes the lazy
    /// form for the computed: `move #4,ccr; moveq #1,d0; moveq #0,d2;
    /// cmpi.l #2,d0`.
    const COMPARE: [u16; 7] = [0x44fc, 0x0004, 0x7001, 0x7400, 0x0c80, 0x0000, 0x0002];
    /// Where the code after `COMPARE` starts.
    const AFTER_COMPARE: u32 = START + 2 * COMPARE.len() as u32;
    /// `beq.s` past the next two words: D2 2 when not taken, 1 when
    /// taken; each way then waits in a `bra.s` to itself.
    const BRANCH: [u16; 5] = [0x6704, 0x7402, 0x60fe, 0x7401, 0x60fe];
    /// `trap #0`.
    const TRAP: u16 = 0x4e40;
    /// The condition codes 1 compared with 2 leaves: N and C.
    const LESS: u32 = 0x09;

    /// Runs the code from `START` to the branch after `COMPARE`.
    fn run_to_the_branch(core: &mut Core) {
        core.engine.get_data_mut().budget = u64::MAX;
        core.engine
            .emu_start(u64::from(START), u64::from(AFTER_COMPARE), 0, 0)
            .expect("runs to the branch");
        assert_eq!(
            get(&core.engine, RegisterM68K::PC).expect("the program counter"),
            AFTER_COMPARE
        );
    }

    /// Where the exception's frame lies, below the stack's start, and
    /// the condition codes in its status register.
    const FRAME: u32 = 0x8000 - 8;
    fn pushed_codes(core: &Core) -> u32 {
        u32::from(super::read_word(&core.engine, FRAME).expect("the frame") & 0xff)
    }

    /// D2, which the branch leaves.
    fn d2(core: &Core) -> u32 {
        get(&core.engine, RegisterM68K::D2).expect("D2")
    }

    #[test]
    fn reading_the_status_register_reads_and_keeps_the_condition_codes() {
        let mut core = core(&[COMPARE.as_slice(), &BRANCH].concat());
        run_to_the_branch(&mut core);
        let status = core
            .register(Register::Status)
            .expect("the status register");
        assert_eq!(status & 0xff, LESS, "{status:#06x}");
        core.run(Cycles(0), Cycles(64)).expect("runs on");
        assert_eq!(d2(&core), NOT_TAKEN, "1 is not 2");
    }

    #[test]
    fn a_trap_keeps_the_condition_codes_in_its_frame() {
        let mut core = core(&[COMPARE.as_slice(), &[TRAP], &BRANCH].concat());
        core.run(Cycles(0), Cycles(400)).expect("runs");
        assert_eq!(d2(&core), NOT_TAKEN, "1 is not 2");
        assert_eq!(pushed_codes(&core), LESS);
    }

    /// `nop`, forty of which make a block costlier than the least
    /// slice.
    const NOP: u16 = 0x4e71;
    const NOPS: usize = 40;
    /// `moveq #1,d2`, the branch's taken way's mark.
    const MARK: u16 = 0x7401;
    /// `bra.s` to itself.
    const WAIT: u16 = 0x60fe;

    #[test]
    fn a_run_always_runs_its_first_block_however_small_its_budget() {
        let mut core = core(&[[NOP; NOPS].as_slice(), &[MARK, WAIT]].concat());
        let budget = Cycles(64);
        let run = core.run(Cycles(0), budget).expect("runs");
        assert_eq!(d2(&core), 1, "the block ran");
        let block = super::CYCLES_PER_BYTE * 2 * (NOPS as u64 + 2);
        assert!(run.cycles.0 >= block, "{run:?}");
    }

    #[test]
    fn a_block_is_counted_once_when_it_runs() {
        // the waiting branch's block, one instruction of two bytes,
        // again and again, past a budget that is not a whole number of
        // them
        let mut core = core(&[WAIT]);
        core.count_instructions().expect("counted");
        let run = core.run(Cycles(0), Cycles(65)).expect("runs");
        let ran = core.instructions().expect("counted");
        assert_eq!(run.cycles.0, ran * 2 * super::CYCLES_PER_BYTE, "{run:?}");
        assert!(run.cycles >= Cycles(65), "{run:?}");
    }

    /// The probe's page, which the program reaches only as nothing.
    const PROBE_PAGE: [u16; 2] = [0x00f0, 0x0000];

    #[test]
    fn the_program_reading_the_probes_page_is_an_invalid_access() {
        // move.w (0xf00000).l,d0
        let mut core = core(&[[0x3039].as_slice(), &PROBE_PAGE, &[WAIT]].concat());
        let run = core.run(Cycles(0), Cycles(64)).expect("runs");
        assert!(matches!(run.stop, Stop::Halted(_)), "{run:?}");
    }

    #[test]
    fn the_program_writing_the_probes_page_is_an_invalid_access() {
        // move.w d0,(0xf00000).l
        let mut core = core(&[[0x33c0].as_slice(), &PROBE_PAGE, &[WAIT]].concat());
        let run = core.run(Cycles(0), Cycles(64)).expect("runs");
        assert!(matches!(run.stop, Stop::Halted(_)), "{run:?}");
    }

    #[test]
    fn the_program_running_the_probe_is_an_invalid_access() {
        // jmp (0xf00000).l
        let mut core = core(&[[0x4ef9].as_slice(), &PROBE_PAGE].concat());
        let run = core.run(Cycles(0), Cycles(64)).expect("runs");
        assert!(matches!(run.stop, Stop::Halted(_)), "{run:?}");
        assert_eq!(
            get(&core.engine, RegisterM68K::PC).expect("the program counter"),
            0x00f0_0000
        );
    }

    #[test]
    fn an_interrupt_keeps_the_condition_codes_in_its_frame() {
        let mut core = core(&[COMPARE.as_slice(), &BRANCH].concat());
        run_to_the_branch(&mut core);
        assert!(core.interrupt(7, LEVEL_7_VECTOR).expect("asked for"));
        core.run(Cycles(0), Cycles(400)).expect("runs on");
        assert_eq!(d2(&core), NOT_TAKEN, "1 is not 2");
        assert_eq!(pushed_codes(&core), LESS);
    }
}

/// The two Unicorn m68k bugs the core works around (the module's
/// comment; docs/emulator.md, "The condition codes"),
/// pinned by what they do as the `unicorn-engine` crate's Unicorn has
/// them (the crate 2.1.5 bundles Unicorn 2.1.4, its `unicorn.h`'s
/// `UC_API_*`): each case fails if a bug no longer shows as recorded, so
/// that the workarounds are revisited, or if a workaround gives the
/// wrong codes.  The version is not checked: Unicorn's `dev` still
/// calls itself 2.1.4.
///
/// 1. A read of SR (`qemu/target/m68k/unicorn.c`, `reg_read`) marks
///    QEMU's lazy condition codes computed before computing them, so it
///    returns the last flag-setting instruction's operands read as
///    flags and leaves the CPU to go on with them.
/// 2. A write of PC from a code hook part way through a translated
///    block loses the lazy condition codes.
///
/// Each case runs a compare, 2 with 2 (Z) and 1 with 2 (N, C), then
/// `scs d1; seq d3`, which record the codes the CPU goes on with.
#[cfg(test)]
mod recorded_bugs {
    use std::cell::RefCell;
    use std::rc::Rc;

    use unicorn_engine::Arch;
    use unicorn_engine::M68kCpuModel;
    use unicorn_engine::Mode;
    use unicorn_engine::Prot;
    use unicorn_engine::RegisterM68K;
    use unicorn_engine::Unicorn;

    use super::CONDITION_CODES;
    use super::Core;
    use super::RESET_STATUS;
    use super::status;
    use crate::board::Bus;
    use crate::board::time::Cycles;
    use crate::cpu::Cpu;

    /// The code's memory, where it starts, and the stack's top.
    const MEMORY: u64 = 0x1_0000;
    const START: u32 = 0x1000;
    const STACK_TOP: u32 = 0x8000;

    /// `move #0,ccr; moveq #2,d0; cmpi.l #2,d0` and the same with 1
    /// (M68000PM).
    const EQUAL: [u16; 5] = [0x44fc, 0x0000, 0x7002, 0x0c80, 0x0000];
    const LESS: [u16; 5] = [0x44fc, 0x0000, 0x7001, 0x0c80, 0x0000];
    /// The compare's immediate's low word.
    const TWO: u16 = 0x0002;
    /// `scs d1; seq d3`: each `0xff` if its condition holds, else 0.
    const RECORD: [u16; 2] = [0x55c1, 0x57c3];
    /// `trap #0`, which the cases' interrupt hooks step past.
    const TRAP: u16 = 0x4e40;
    const SET: u32 = 0xff;
    const BYTE: u32 = 0xff;

    /// A compare, the codes it leaves, and what `RECORD` leaves after.
    struct Compare {
        name: &'static str,
        code: [u16; 6],
        codes: u16,
        recorded: (u32, u32),
    }

    /// Z for 2 with 2; N and C for 1 with 2.
    fn compares() -> [Compare; 2] {
        let with_two = |code: [u16; 5]| [code[0], code[1], code[2], code[3], code[4], TWO];
        [
            Compare {
                name: "2 cmp 2",
                code: with_two(EQUAL),
                codes: 0x04,
                recorded: (0, SET),
            },
            Compare {
                name: "1 cmp 2",
                code: with_two(LESS),
                codes: 0x09,
                recorded: (SET, 0),
            },
        ]
    }

    /// What the bugs give: the read's codes, the move to CCR's cleared
    /// ones, and the codes the CPU goes on with, in either compare.
    const BUGGY_CODES: u16 = 0x00;
    const BUGGY_RECORDED: (u32, u32) = (0, 0);

    /// What the symptoms give: an SR read mid-block's codes, the move to
    /// CCR's cleared ones, the compare's not yet computed; and what
    /// `RECORD` records after a hook's SR write mid-block, alone or
    /// before a fault: the written flags taken as the compare's
    /// operands, 0 with 0, Z alone, whatever was written.
    /// `WRITTEN_CODES`, C and Z, are what the hooks write.
    const STALE_CODES: u16 = 0x00;
    const WRITTEN_CODES: u16 = 0x05;
    const MISREAD_RECORDED: (u32, u32) = (0, SET);
    /// `move.l (0xf00000).l,d0`, a read where nothing is mapped.
    const UNMAPPED_READ: [u16; 3] = [0x2039, 0x00f0, 0x0000];

    fn bytes(words: &[u16]) -> Vec<u8> {
        words.iter().copied().flat_map(u16::to_be_bytes).collect()
    }

    /// The code's end, from `START`.
    fn end(words: &[u16]) -> u64 {
        u64::from(START) + 2 * words.len() as u64
    }

    /// A bare 68020, the code at `START`, supervisor at level 7.
    fn machine(code: &[u16]) -> Unicorn<'static, ()> {
        let mut uc = Unicorn::new(Arch::M68K, Mode::BIG_ENDIAN).expect("Unicorn");
        uc.ctl_set_cpu_model(M68kCpuModel::M68020 as i32)
            .expect("a 68020");
        uc.mem_map(0, MEMORY, Prot::ALL).expect("memory");
        uc.mem_write(u64::from(START), &bytes(code)).expect("code");
        uc.reg_write(RegisterM68K::SR, u64::from(RESET_STATUS))
            .expect("SR");
        uc.reg_write(RegisterM68K::A7, u64::from(STACK_TOP))
            .expect("A7");
        uc
    }

    fn recorded<D>(uc: &Unicorn<'_, D>) -> (u32, u32) {
        let byte = |register| uc.reg_read(register).expect("a register") as u32 & BYTE;
        (byte(RegisterM68K::D1), byte(RegisterM68K::D3))
    }

    /// A bus with nothing on it, for the core.
    struct NoDevices;

    impl Bus for NoDevices {
        fn read(&mut self, _at: u32, _size: u32, _now: Cycles) -> u32 {
            0
        }
        fn write(&mut self, _at: u32, _size: u32, _value: u32, _now: Cycles) {}
    }

    /// The core on the same memory and code, supervisor at level 7.
    fn core(code: &[u16]) -> Core {
        let mut core = Core::new(Rc::new(RefCell::new(NoDevices))).expect("a core");
        core.map_memory(0, MEMORY as u32, true).expect("memory");
        core.write(START, &bytes(code)).expect("code");
        core.engine
            .reg_write(RegisterM68K::SR, u64::from(RESET_STATUS))
            .expect("SR");
        core.engine
            .reg_write(RegisterM68K::A7, u64::from(STACK_TOP))
            .expect("A7");
        core.engine.get_data_mut().budget = u64::MAX;
        core
    }

    /// Bug 1: SR read after a run stops between the compare and
    /// `RECORD`, and the run then goes on; the workaround, the core's
    /// `status`, reads a copy's system byte and probes the codes.
    #[test]
    fn a_status_read_after_a_stop_spoils_the_codes_and_status_does_not() {
        for compare in compares() {
            let code = [compare.code.as_slice(), &RECORD].concat();
            let after = end(&compare.code);
            let mut uc = machine(&code);
            uc.emu_start(u64::from(START), after, 0, 0).expect("runs");
            let read = uc.reg_read(RegisterM68K::SR).expect("SR") as u16;
            uc.emu_start(after, end(&code), 0, 0).expect("runs on");
            assert_eq!(
                read & CONDITION_CODES,
                BUGGY_CODES,
                "{}: the bug",
                compare.name
            );
            assert_eq!(recorded(&uc), BUGGY_RECORDED, "{}: the bug", compare.name);

            let mut core = core(&code);
            core.engine
                .emu_start(u64::from(START), after, 0, 0)
                .expect("runs");
            let read = status(&mut core.engine).expect("the status");
            core.engine
                .emu_start(after, end(&code), 0, 0)
                .expect("runs on");
            assert_eq!(read, RESET_STATUS | compare.codes, "{}", compare.name);
            assert_eq!(recorded(&core.engine), compare.recorded, "{}", compare.name);
        }
    }

    /// Bug 1 in a trap's interrupt hook, the trap stepped past; the
    /// core takes the trap after the run stops instead, its frame's
    /// codes the probe's (`a_trap_keeps_the_condition_codes_in_its_frame`
    /// for the core's side).
    #[test]
    fn a_status_read_in_a_trap_hook_spoils_the_codes() {
        for compare in compares() {
            let code = [compare.code.as_slice(), &[TRAP], &RECORD].concat();
            let mut uc = machine(&code);
            let seen = Rc::new(RefCell::new(None));
            let record = Rc::clone(&seen);
            uc.add_intr_hook(move |uc, _number| {
                let pc = uc.reg_read(RegisterM68K::PC).expect("PC");
                *record.borrow_mut() = Some(uc.reg_read(RegisterM68K::SR).expect("SR") as u16);
                uc.reg_write(RegisterM68K::PC, pc + 2).expect("PC");
            })
            .expect("hooked");
            uc.emu_start(u64::from(START), end(&code), 0, 0)
                .expect("runs");
            let read = seen.borrow().expect("the trap taken");
            assert_eq!(
                read & CONDITION_CODES,
                BUGGY_CODES,
                "{}: the bug",
                compare.name
            );
            assert_eq!(recorded(&uc), BUGGY_RECORDED, "{}: the bug", compare.name);
        }
    }

    /// Bug 2: a code hook at `RECORD`, in the compare's block, writes PC
    /// (to `RECORD` again); the workaround, as the core takes an
    /// exception, stops the run there and goes on with a run of its own.
    #[test]
    fn a_pc_write_in_a_code_hook_loses_the_codes_and_a_stop_does_not() {
        for compare in compares() {
            let code = [compare.code.as_slice(), &RECORD].concat();
            let after = end(&compare.code);
            for workaround in [false, true] {
                let mut uc = machine(&code);
                let done = Rc::new(RefCell::new(false));
                let once = Rc::clone(&done);
                uc.add_code_hook(after, after, move |uc, address, _size| {
                    if once.replace(true) {
                        return;
                    }
                    if workaround {
                        uc.emu_stop().expect("stopped");
                    } else {
                        uc.reg_write(RegisterM68K::PC, address).expect("PC");
                    }
                })
                .expect("hooked");
                uc.emu_start(u64::from(START), end(&code), 0, 0)
                    .expect("runs");
                if workaround {
                    let pc = uc.reg_read(RegisterM68K::PC).expect("PC");
                    uc.emu_start(pc, end(&code), 0, 0).expect("runs on");
                    assert_eq!(recorded(&uc), compare.recorded, "{}", compare.name);
                } else {
                    assert_eq!(recorded(&uc), BUGGY_RECORDED, "{}: the bug", compare.name);
                }
                assert!(*done.borrow(), "{}: the hook ran", compare.name);
            }
        }
    }

    /// Bug 2's cause, the lazy codes kept apart from what a hook sees
    /// part way through a block, shows three more ways on Unicorn's
    /// development line, pinned here by behavior as the crate's Unicorn
    /// has them: an SR read from a code hook mid-block gives stale
    /// codes, and an SR write from one is ignored by the rest of the
    /// block, its flags read as the operands of the instruction that set
    /// the codes; and an SR write from a code hook on an instruction
    /// that then faults is undone.  The
    /// core's code hooks read only the system byte, from a context's
    /// copy (`system_status`: `move (sp)+,sr`, `stop`, `--watch-code`),
    /// and write SR only with PC after it (`move (sp)+,sr`, `stop`),
    /// which ends the block before the hooked instruction runs, so that
    /// it can neither fault nor run on stale codes (the next test); the
    /// codes it reads only between runs, through the probe.
    #[test]
    fn a_status_read_in_a_code_hook_mid_block_is_stale() {
        for compare in compares() {
            let code = [compare.code.as_slice(), &RECORD].concat();
            let after = end(&compare.code);
            let mut uc = machine(&code);
            let seen = Rc::new(RefCell::new(None));
            let record = Rc::clone(&seen);
            uc.add_code_hook(after, after, move |uc, _address, _size| {
                *record.borrow_mut() = Some(uc.reg_read(RegisterM68K::SR).expect("SR") as u16);
            })
            .expect("hooked");
            uc.emu_start(u64::from(START), end(&code), 0, 0)
                .expect("runs");
            let read = seen.borrow().expect("the hook ran");
            assert_eq!(
                read & CONDITION_CODES,
                STALE_CODES,
                "{}: the bug",
                compare.name
            );
        }
    }

    #[test]
    fn a_status_write_in_a_code_hook_mid_block_is_ignored() {
        for compare in compares() {
            let code = [compare.code.as_slice(), &RECORD].concat();
            let after = end(&compare.code);
            let mut uc = machine(&code);
            uc.add_code_hook(after, after, move |uc, _address, _size| {
                uc.reg_write(RegisterM68K::SR, u64::from(RESET_STATUS | WRITTEN_CODES))
                    .expect("SR");
            })
            .expect("hooked");
            uc.emu_start(u64::from(START), end(&code), 0, 0)
                .expect("runs");
            assert_eq!(recorded(&uc), MISREAD_RECORDED, "{}: the bug", compare.name);
        }
    }

    /// What the core's `move (sp)+,sr` and `stop` hooks rely on: an SR
    /// write from a code hook mid-block with a PC write after it ends
    /// the block there, and the code from the new PC sees the status
    /// written, codes and all.
    #[test]
    fn a_status_write_with_a_pc_write_in_a_code_hook_is_seen_after() {
        for compare in compares() {
            let code = [compare.code.as_slice(), &RECORD].concat();
            let after = end(&compare.code);
            let mut uc = machine(&code);
            let done = Rc::new(RefCell::new(false));
            let once = Rc::clone(&done);
            uc.add_code_hook(after, after, move |uc, address, _size| {
                if once.replace(true) {
                    return;
                }
                uc.reg_write(RegisterM68K::SR, u64::from(RESET_STATUS | WRITTEN_CODES))
                    .expect("SR");
                uc.reg_write(RegisterM68K::PC, address).expect("PC");
            })
            .expect("hooked");
            uc.emu_start(u64::from(START), end(&code), 0, 0)
                .expect("runs");
            assert_eq!(recorded(&uc), (SET, SET), "{}", compare.name);
        }
    }

    /// A code hook on a load that faults, after the compare in its
    /// block, writes SR, its codes clear; a run from past the load then
    /// records the codes the fault left: QEMU's `restore_state_to_opc()`
    /// puts the block's lazy codes back over the write, and the written
    /// flags are read as the compare's operands, 0 with 0, Z.  The core does not
    /// do this: its SR writes in code hooks come with a PC write, so the
    /// hooked instruction never runs to fault.
    #[test]
    fn a_status_write_in_a_code_hook_before_a_fault_is_undone() {
        for compare in compares() {
            let code = [compare.code.as_slice(), &UNMAPPED_READ, &RECORD].concat();
            let at = end(&compare.code);
            let mut uc = machine(&code);
            uc.add_code_hook(at, at, |uc, _address, _size| {
                uc.reg_write(RegisterM68K::SR, u64::from(RESET_STATUS))
                    .expect("SR");
            })
            .expect("hooked");
            let faulted = uc.emu_start(u64::from(START), end(&code), 0, 0);
            assert!(faulted.is_err(), "{}: the load faults", compare.name);
            let past = at + 2 * UNMAPPED_READ.len() as u64;
            uc.emu_start(past, end(&code), 0, 0).expect("runs on");
            assert_eq!(recorded(&uc), MISREAD_RECORDED, "{}: the bug", compare.name);
        }
    }
}
