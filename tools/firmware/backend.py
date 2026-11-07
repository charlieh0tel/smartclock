"""The CPU the harness runs the image on: Unicorn's 68020, the default,
or mc683xx's bare CPU32, chosen by SMARTCLOCK_CPU (BACKEND_VARIABLE),
and the one seam between them (docs/firmware/README.md, "The CPU
backend").

new_cpu gives a Unicorn Uc, or a Mc683xx, which answers the part of
Unicorn's API the harness and its checks use -- mem_map, mem_read,
mem_write, mem_protect, reg_read, reg_write, context_save, hook_add,
hook_del, emu_start, emu_stop, ctl_flush_tb -- on mc683xx.Machine, a CPU32
on flat memory with 32 address lines (mc683xx's docs/python.md, "Bare
CPU"), so that both run from the same code.  Where the two differ,
Mc683xx keeps Unicorn's meaning:

- Memory.  A region that a read or write hook added before the first
  run covers is a device of the harness's own, its bytes kept here, a
  hook covering a cycle there called before the cycle, as Unicorn's
  are, a read reading what it left: the devices, the EEPROM and the
  installer's flash.  Any other region is the Machine's memory.
- Hooks.  A code or block hook is Unicorn's: before its instruction,
  free to read and write the CPU, a write of PC redirecting.  A memory
  hook's callback runs once its instruction has ended, given an InRun
  in place of the CPU, which reads the registers as the instruction
  began, as Unicorn's do from part way through it; it is not called
  for the writes of an exception's frame, which Unicorn never makes.
  A long operand is one access, as Unicorn's, at a device and to a
  hook alike.  An interrupt hook is called from mc683xx's intercept,
  before anything is stacked, PC at the instruction that raised the
  exception, but for the vectors native_vectors lets the CPU take
  itself.
- Faults.  A bus error, nothing mapped or a page's permissions
  refusing the cycle, stops the run (set_stop_on_fault), the registers as
  its instruction began: the invalid access hooks are called, and
  emu_start raises Unicorn's UcError for it; for an address error where
  something is mapped, Unicorn's for an unaligned access, which it
  never raises.
- Unicorn's 68020 reads and writes words and longs at odd addresses
  and does not trace, which the CPU32 would refuse (an address error,
  CPU32RM 6.2.3) and do (6.2.10): the Machine's parity deviations,
  misaligned and untraced, make it do the same, unless
  SMARTCLOCK_CPU_PARITY (PARITY_VARIABLE) is 0.
"""

import bisect
import os
from collections.abc import Callable
from dataclasses import dataclass

from unicorn import (
    UC_ARCH_M68K,
    UC_ERR_EXCEPTION,
    UC_ERR_FETCH_PROT,
    UC_ERR_FETCH_UNALIGNED,
    UC_ERR_FETCH_UNMAPPED,
    UC_ERR_READ_PROT,
    UC_ERR_READ_UNALIGNED,
    UC_ERR_READ_UNMAPPED,
    UC_ERR_WRITE_PROT,
    UC_ERR_WRITE_UNALIGNED,
    UC_ERR_WRITE_UNMAPPED,
    UC_HOOK_BLOCK,
    UC_HOOK_CODE,
    UC_HOOK_INTR,
    UC_HOOK_MEM_INVALID,
    UC_HOOK_MEM_READ,
    UC_HOOK_MEM_WRITE,
    UC_MEM_FETCH,
    UC_MEM_FETCH_PROT,
    UC_MEM_FETCH_UNMAPPED,
    UC_MEM_READ,
    UC_MEM_READ_PROT,
    UC_MEM_READ_UNMAPPED,
    UC_MEM_WRITE,
    UC_MEM_WRITE_PROT,
    UC_MEM_WRITE_UNMAPPED,
    UC_MODE_BIG_ENDIAN,
    UC_PROT_ALL,
    UC_PROT_EXEC,
    UC_PROT_READ,
    UC_PROT_WRITE,
    Uc,
    UcError,
)
from unicorn.m68k_const import (
    UC_CPU_M68K_M68020,
    UC_M68K_REG_A0,
    UC_M68K_REG_A1,
    UC_M68K_REG_A2,
    UC_M68K_REG_A3,
    UC_M68K_REG_A4,
    UC_M68K_REG_A5,
    UC_M68K_REG_A6,
    UC_M68K_REG_A7,
    UC_M68K_REG_CR_VBR,
    UC_M68K_REG_D0,
    UC_M68K_REG_D1,
    UC_M68K_REG_D2,
    UC_M68K_REG_D3,
    UC_M68K_REG_D4,
    UC_M68K_REG_D5,
    UC_M68K_REG_D6,
    UC_M68K_REG_D7,
    UC_M68K_REG_PC,
    UC_M68K_REG_SR,
)

# The environment variable that chooses the backend, and its values;
# Unicorn when it is unset.
BACKEND_VARIABLE = "SMARTCLOCK_CPU"
UNICORN = "unicorn"
MC683XX = "mc683xx"
BACKEND = os.environ.get(BACKEND_VARIABLE, UNICORN)
if BACKEND not in (UNICORN, MC683XX):
    raise SystemExit(f"{BACKEND_VARIABLE}={BACKEND}: not {UNICORN} or {MC683XX}")
ON_MC683XX = BACKEND == MC683XX
# The environment variable that, 0, has mc683xx run as the CPU32 does
# rather than as Unicorn's 68020 (Mc683xx).
PARITY_VARIABLE = "SMARTCLOCK_CPU_PARITY"
PARITY = os.environ.get(PARITY_VARIABLE, "1") != "0"


# A long's store, move.l d0,(LONG_PROBE).l, and its address: the probe
# of whether a hook sees a long once (check_module).
LONG_PROBE = 0x1000
LONG_STORE = bytes.fromhex("23c0") + LONG_PROBE.to_bytes(4, "big")
LONG_SIZE = 4
# Whether check_module has found the module new enough.
checked: list[bool] = []


def check_module(mc683xx) -> None:
    """Stops with a message if mc683xx's module is older than the harness
    needs: a bare Machine (mc683xx's known-good-6), and memory hooks that
    see a long once, as one access of 4, as Unicorn's do."""
    if checked:
        return
    stale = f"{mc683xx.__file__}: older than the harness needs"
    if not hasattr(mc683xx, "Machine"):
        raise SystemExit(f"{stale} (no bare Machine)")
    probe = mc683xx.Machine()
    probe.map(0, 2 * LONG_PROBE, contents=LONG_STORE)
    sizes: list[int] = []
    probe.add_write_hook(lambda access: sizes.append(access.size), LONG_PROBE, LONG_PROBE + LONG_SIZE - 1)
    probe.run_limited(begin=0, instructions=1)
    if sizes != [LONG_SIZE]:
        raise SystemExit(f"{stale} (a hook sees a long as {sizes})")
    checked.append(True)


def new_cpu() -> "Uc | Mc683xx":
    """A CPU of the chosen backend, nothing mapped.  Unicorn has no
    CPU32; its 68020 stands in (PLAN.md, "A rebuild that runs")."""
    if ON_MC683XX:
        return Mc683xx()
    uc = Uc(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN)
    uc.ctl_set_cpu_model(UC_CPU_M68K_M68020)
    return uc


# Unicorn's registers by mc683xx's names.
REGISTER_NAMES = {
    UC_M68K_REG_D0: "D0",
    UC_M68K_REG_D1: "D1",
    UC_M68K_REG_D2: "D2",
    UC_M68K_REG_D3: "D3",
    UC_M68K_REG_D4: "D4",
    UC_M68K_REG_D5: "D5",
    UC_M68K_REG_D6: "D6",
    UC_M68K_REG_D7: "D7",
    UC_M68K_REG_A0: "A0",
    UC_M68K_REG_A1: "A1",
    UC_M68K_REG_A2: "A2",
    UC_M68K_REG_A3: "A3",
    UC_M68K_REG_A4: "A4",
    UC_M68K_REG_A5: "A5",
    UC_M68K_REG_A6: "A6",
    UC_M68K_REG_A7: "A7",
    UC_M68K_REG_PC: "PC",
    UC_M68K_REG_SR: "SR",
    UC_M68K_REG_CR_VBR: "VBR",
}
LONG_MASK = 0xFFFFFFFF
# The exception vectors (CPU32RM 6.1.3); those not native_vectors are
# intercepted.
VECTORS = 256
# Function codes of program space, user and supervisor (CPU32RM 3.4):
# a fetch.
PROGRAM_SPACES = (2, 6)
# Unicorn's permissions as mc683xx's letters.
PERMISSION_LETTERS = ((UC_PROT_READ, "r"), (UC_PROT_WRITE, "w"), (UC_PROT_EXEC, "x"))
# Unicorn's invalid access kinds and errors, by mc683xx's cycle kind
# (its CycleKind's number: fetch, read, write) and whether anything is
# mapped there; and for an address error where something is, Unicorn's
# for an unaligned access, which it never raises.
FETCH, READ, WRITE = range(3)
UNALIGNED = {
    FETCH: (UC_MEM_FETCH, UC_ERR_FETCH_UNALIGNED),
    READ: (UC_MEM_READ, UC_ERR_READ_UNALIGNED),
    WRITE: (UC_MEM_WRITE, UC_ERR_WRITE_UNALIGNED),
}
INVALID = {
    (FETCH, False): (UC_MEM_FETCH_UNMAPPED, UC_ERR_FETCH_UNMAPPED),
    (READ, False): (UC_MEM_READ_UNMAPPED, UC_ERR_READ_UNMAPPED),
    (WRITE, False): (UC_MEM_WRITE_UNMAPPED, UC_ERR_WRITE_UNMAPPED),
    (FETCH, True): (UC_MEM_FETCH_PROT, UC_ERR_FETCH_PROT),
    (READ, True): (UC_MEM_READ_PROT, UC_ERR_READ_PROT),
    (WRITE, True): (UC_MEM_WRITE_PROT, UC_ERR_WRITE_PROT),
}


def permissions(perms: int) -> str:
    """Unicorn's permission bits as mc683xx's letters."""
    return "".join(letter for bit, letter in PERMISSION_LETTERS if perms & bit)


@dataclass
class Region:
    """A mem_map: its start and bytes, its permissions, and, for one the
    Machine does not hold, a device's or one mapped before the first run,
    its bytes here."""

    start: int
    size: int
    perms: int
    data: bytearray | None

    def end(self) -> int:
        return self.start + self.size


@dataclass
class HookEntry:
    """A hook_add: its kind, callback, user data and range, and the
    mc683xx hook it installed, if one."""

    kind: int
    callback: Callable
    data: object
    begin: int
    end: int
    native: object = None

    def covers(self, address: int) -> bool:
        return self.begin <= address <= self.end


class Context:
    """context_save's: the registers, readable as Unicorn's are."""

    def __init__(self, registers: dict[str, int]):
        self.registers = registers

    def reg_read(self, register: int) -> int:
        return self.registers[REGISTER_NAMES[register]]


class InRun:
    """The CPU as a memory hook's callback sees it: the registers as the
    access's instruction began, else the CPU's own; memory and
    emu_stop."""

    def __init__(self, cpu: "Mc683xx", registers: dict[str, int] | None):
        self.cpu = cpu
        self.registers = registers

    def reg_read(self, register: int) -> int:
        if self.registers is None:
            return self.cpu.reg_read(register)
        return self.registers[REGISTER_NAMES[register]]

    def context_save(self) -> Context:
        return Context(self.registers) if self.registers is not None else self.cpu.context_save()

    def mem_read(self, address: int, size: int) -> bytearray:
        return self.cpu.mem_read(address, size)

    def mem_write(self, address: int, data: bytes) -> None:
        self.cpu.mem_write(address, data)

    def emu_stop(self) -> None:
        self.cpu.emu_stop()


class Device:
    """A region of the harness's own, as mc683xx's Machine reaches it: each
    cycle a read or write of its bytes, a read hook covering it run
    before a read and a write hook before a write, as Unicorn's are; an
    exception's frame written as the CPU stacks it, shown no hook, as
    Unicorn makes none; a cycle its permissions refuse, a bus error."""

    def __init__(self, cpu: "Mc683xx", region: Region):
        self.cpu = cpu
        self.region = region

    def permit(self, bit: int) -> None:
        import mc683xx

        if not self.region.perms & bit:
            raise mc683xx.BusError()

    def read(self, cycle) -> int:
        self.permit(UC_PROT_EXEC if cycle.function_code in PROGRAM_SPACES else UC_PROT_READ)
        if not cycle.stacking:
            self.cpu.before(UC_HOOK_MEM_READ, UC_MEM_READ, cycle.address, cycle.size, 0)
        at = cycle.address - self.region.start
        return int.from_bytes(self.region.data[at : at + cycle.size], "big")

    def write(self, cycle, value: int) -> None:
        self.permit(UC_PROT_WRITE)
        if not cycle.stacking:
            self.cpu.before(UC_HOOK_MEM_WRITE, UC_MEM_WRITE, cycle.address, cycle.size, value)
        at = cycle.address - self.region.start
        self.region.data[at : at + cycle.size] = value.to_bytes(cycle.size, "big")


class Mc683xx:
    """Unicorn's API, as the harness uses it, on mc683xx's bare CPU32:
    the regions mapped, and placed in the Machine or kept as devices at the
    first run (place); native_vectors the exceptions the CPU takes
    itself, which no interrupt hook is called for."""

    def __init__(self):
        import mc683xx

        check_module(mc683xx)
        self.cpu = mc683xx.Machine()
        self.stop_request = self.cpu.stop_request()
        self.cpu.set_stop_on_fault(True)
        self.cpu.set_deviations(mc683xx.Deviations(misaligned=PARITY, untraced=PARITY))
        self.regions: list[Region] = []
        self.starts: list[int] = []
        self.hooks: list[HookEntry] = []
        self.native_vectors: set[int] = set()
        # whether the regions are placed; whether a device's cycle is
        # running hooks, which cannot call into the Machine (the run holds
        # it), and whether one asked to stop; and the exception
        # intercepted with no interrupt hook to give it to
        self.placed = False
        self.in_device = False
        self.stopping = False
        self.unhooked: int | None = None

    # memory

    def mem_map(self, address: int, size: int, perms: int = UC_PROT_ALL) -> None:
        region = Region(address, size, perms, bytearray(size))
        index = bisect.bisect(self.starts, address)
        self.regions.insert(index, region)
        self.starts.insert(index, address)
        if self.placed:
            self.cpu.map(address, size, permissions=permissions(perms))
            region.data = None

    def region(self, address: int) -> Region | None:
        index = bisect.bisect(self.starts, address) - 1
        if index < 0:
            return None
        region = self.regions[index]
        return region if address < region.end() else None

    def spans(self, address: int, size: int, error: int):
        """(region, offset, bytes) for each region a range lies in."""
        while size > 0:
            region = self.region(address)
            if region is None:
                raise UcError(error)
            count = min(size, region.end() - address)
            yield region, address - region.start, count
            address += count
            size -= count

    def mem_read(self, address: int, size: int) -> bytearray:
        found = bytearray()
        for region, offset, count in self.spans(address, size, UC_ERR_READ_UNMAPPED):
            if region.data is not None:
                found += region.data[offset : offset + count]
            else:
                found += self.cpu.read(region.start + offset, count)
        return found

    def mem_write(self, address: int, data: bytes) -> None:
        done = 0
        for region, offset, count in self.spans(address, len(data), UC_ERR_WRITE_UNMAPPED):
            part = data[done : done + count]
            if region.data is not None:
                region.data[offset : offset + count] = part
            else:
                self.cpu.write(region.start + offset, part)
            done += count

    def mem_protect(self, address: int, size: int, perms: int = UC_PROT_ALL) -> None:
        if self.placed:
            self.cpu.protect(address, size, permissions(perms))
            return
        for region, offset, count in self.spans(address, size, UC_ERR_WRITE_UNMAPPED):
            if offset or count != region.size:
                raise RuntimeError(f"{address:x}: protecting part of a region before the first run")
            region.perms = perms

    def place(self) -> None:
        """The regions in the Machine, and the devices among them, at the
        first use of the CPU."""
        if self.placed:
            return
        self.placed = True
        hooked = [hook for hook in self.hooks if hook.kind in (UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE)]
        for region in self.regions:
            if any(hook.begin < region.end() and region.start <= hook.end for hook in hooked):
                last = region.end() - 1
                self.cpu.add_peripheral(region.start, last, Device(self, region))
                # a device's hook stops the run once the cycle's
                # instruction has ended
                self.cpu.add_read_hook(lambda _: self.stopping, region.start, last)
                self.cpu.add_write_hook(lambda _: self.stopping, region.start, last)
            else:
                self.cpu.map(region.start, region.size, permissions=permissions(region.perms), contents=region.data)
                region.data = None
        for vector in range(VECTORS):
            self.cpu.set_intercepted(vector, vector not in self.native_vectors)
        self.cpu.set_intercept_callback(self.intercepted)
        for hook in self.hooks:
            self.install(hook)

    # registers

    def reg_read(self, register: int) -> int:
        self.place()
        return self.cpu.read_register(REGISTER_NAMES[register])

    def reg_write(self, register: int, value: int) -> None:
        self.place()
        self.cpu.write_register(REGISTER_NAMES[register], value & LONG_MASK)

    def context_save(self) -> Context:
        self.place()
        return Context(self.cpu.registers())

    def ctl_flush_tb(self) -> None:
        """Unicorn's translation blocks; mc683xx has none."""

    # hooks

    def hook_add(self, kind: int, callback: Callable, user_data: object = None, begin: int = 1, end: int = 0):
        """Unicorn's hook_add, begin past end for every address."""
        if begin > end:
            begin, end = 0, LONG_MASK
        hook = HookEntry(kind, callback, user_data, begin, end)
        self.hooks.append(hook)
        if self.placed:
            self.install(hook)
        return hook

    def install(self, hook: HookEntry) -> None:
        cpu = self.cpu
        if hook.kind in (UC_HOOK_CODE, UC_HOOK_BLOCK):
            add = cpu.add_code_hook if hook.kind == UC_HOOK_CODE else cpu.add_block_hook
            hook.native = add(
                lambda instruction: hook.callback(self, instruction.pc, 0, hook.data), hook.begin, hook.end
            )
        elif hook.kind in (UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE):
            reading = hook.kind == UC_HOOK_MEM_READ
            add, access = (cpu.add_read_hook, UC_MEM_READ) if reading else (cpu.add_write_hook, UC_MEM_WRITE)

            def memory(seen):
                region = self.region(seen.address)
                if seen.stacking or region is not None and region.data is not None:
                    return
                shown = InRun(self, seen.registers)
                hook.callback(shown, access, seen.address, seen.size, seen.value, hook.data)

            hook.native = add(memory, hook.begin, hook.end)

    def hook_del(self, hook: HookEntry) -> None:
        self.hooks.remove(hook)
        if hook.native is not None:
            self.cpu.remove_hook(hook.native)

    def before(self, kind: int, access: int, address: int, size: int, value: int) -> None:
        """A device's cycle: the hooks of kind covering it, as Unicorn's
        run before the access."""
        self.in_device = True
        try:
            for hook in self.hooks:
                if hook.kind == kind and hook.covers(address):
                    hook.callback(InRun(self, None), access, address, size, value, hook.data)
        finally:
            self.in_device = False

    def intercepted(self, interception) -> bool:
        """An exception the CPU raised, before anything is stacked: to
        the interrupt hooks, as Unicorn gives it them in place of taking
        it; with none, the run stops, as Unicorn's does."""
        interrupts = [hook for hook in self.hooks if hook.kind == UC_HOOK_INTR]
        if not interrupts:
            self.unhooked = interception.vector
            self.stop_request.request()
            return True
        for hook in interrupts:
            hook.callback(self, interception.vector, hook.data)
        return True

    def emu_stop(self) -> None:
        if self.placed and not self.in_device:
            self.stop_request.request()
        self.stopping = True

    def emu_start(self, begin: int, until: int, timeout: int = 0, count: int = 0) -> None:
        import mc683xx

        self.place()
        self.stopping = False
        self.unhooked = None
        end = self.cpu.run_limited(begin=begin & LONG_MASK, until=until & LONG_MASK, instructions=count or None).end
        if self.unhooked is not None:
            raise UcError(UC_ERR_EXCEPTION)
        if isinstance(end, mc683xx.End.Fault):
            (fault,) = end
            mapped = self.region(fault.address) is not None
            if mapped and fault.cause == mc683xx.FaultCause.ADDRESS_ERROR:
                kind, error = UNALIGNED[int(fault.cycle)]
            else:
                kind, error = INVALID[(int(fault.cycle), mapped)]
            for hook in self.hooks:
                if hook.kind & UC_HOOK_MEM_INVALID and hook.covers(fault.address):
                    hook.callback(self, kind, fault.address, fault.size, 0, hook.data)
            raise UcError(error)
        if isinstance(end, mc683xx.End.Stop) and isinstance(end[0], mc683xx.Stop.Halted):
            raise UcError(UC_ERR_EXCEPTION)
