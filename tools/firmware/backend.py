"""The CPU the harness runs the image on, mc683xx's bare CPU32
(docs/firmware/README.md, "The CPU").

new_cpu gives a Mc683xx, which answers the API the harness and its
checks are written against -- mem_map, mem_read, mem_write,
mem_protect, reg_read, reg_write, hook_add, hook_del, emu_start,
emu_stop, the UC_ constants and UcError -- on mc683xx.Machine, a CPU32
on flat memory with 32 address lines (mc683xx's docs/python.md, "Bare
CPU").  What the API means, where the Machine alone would differ:

- Memory.  A region that a read or write hook added before the first
  run covers is a device of the harness's own, its bytes kept here, a
  hook covering a cycle there called before the cycle, a read reading
  what it left: the devices, the EEPROM and the installer's flash.  Any
  other region is the Machine's memory, as is every region where the
  memory hooks only watch (watch_only).
- Hooks.  A code or block hook runs before its instruction, free to
  read and write the CPU, a write of PC redirecting.  A memory hook's
  callback runs once its instruction has ended, given an InRun in place
  of the CPU, which reads the registers as the instruction began; it is
  not called for the writes of an exception's frame.  A long operand is
  one access, at a device and to a hook alike.  An interrupt hook is
  called from mc683xx's intercept, before anything is stacked, PC at
  the instruction that raised the exception, but for the vectors
  native_vectors lets the CPU take itself.
- Faults.  A bus error, nothing mapped or a page's permissions refusing
  the cycle, stops the run (set_stop_on_fault), the registers as its
  instruction began: the invalid access hooks are called, and emu_start
  raises UcError for it; for an address error where something is
  mapped, the error of an unaligned access.
- Alignment and tracing.  The Machine is the CPU32 as it is: a word or
  long at an odd address is an address error (CPU32RM 6.2.3), and the
  trace bits trace (6.2.10).
"""

import bisect
from collections.abc import Callable
from dataclasses import dataclass

# The API's names and values, as the harness and its checks use them,
# and the scripts' reports and known differences quote them: a hook's
# kind (hook_add) ...
UC_HOOK_INTR = 1
UC_HOOK_CODE = 4
UC_HOOK_BLOCK = 8
UC_HOOK_MEM_INVALID = 1008
UC_HOOK_MEM_READ = 1024
UC_HOOK_MEM_WRITE = 2048
# ... the access a memory hook is given ...
UC_MEM_READ = 16
UC_MEM_WRITE = 17
UC_MEM_FETCH = 18
UC_MEM_READ_UNMAPPED = 19
UC_MEM_WRITE_UNMAPPED = 20
UC_MEM_FETCH_UNMAPPED = 21
UC_MEM_WRITE_PROT = 22
UC_MEM_READ_PROT = 23
UC_MEM_FETCH_PROT = 24
# ... a region's permissions (mem_map, mem_protect) ...
UC_PROT_READ = 1
UC_PROT_WRITE = 2
UC_PROT_EXEC = 4
UC_PROT_ALL = 7
# ... an error's code (UcError.errno) ...
UC_ERR_READ_UNMAPPED = 6
UC_ERR_WRITE_UNMAPPED = 7
UC_ERR_FETCH_UNMAPPED = 8
UC_ERR_WRITE_PROT = 12
UC_ERR_READ_PROT = 13
UC_ERR_FETCH_PROT = 14
UC_ERR_READ_UNALIGNED = 16
UC_ERR_WRITE_UNALIGNED = 17
UC_ERR_FETCH_UNALIGNED = 18
UC_ERR_EXCEPTION = 21
# ... a register (reg_read, reg_write) ...
UC_M68K_REG_A0 = 1
UC_M68K_REG_A1 = 2
UC_M68K_REG_A2 = 3
UC_M68K_REG_A3 = 4
UC_M68K_REG_A4 = 5
UC_M68K_REG_A5 = 6
UC_M68K_REG_A6 = 7
UC_M68K_REG_A7 = 8
UC_M68K_REG_D0 = 9
UC_M68K_REG_D1 = 10
UC_M68K_REG_D2 = 11
UC_M68K_REG_D3 = 12
UC_M68K_REG_D4 = 13
UC_M68K_REG_D5 = 14
UC_M68K_REG_D6 = 15
UC_M68K_REG_D7 = 16
UC_M68K_REG_SR = 17
UC_M68K_REG_PC = 18
UC_M68K_REG_CR_VBR = 21

# ... and each error's message, str(UcError).
ERROR_MESSAGES = {
    UC_ERR_READ_UNMAPPED: "Invalid memory read (UC_ERR_READ_UNMAPPED)",
    UC_ERR_WRITE_UNMAPPED: "Invalid memory write (UC_ERR_WRITE_UNMAPPED)",
    UC_ERR_FETCH_UNMAPPED: "Invalid memory fetch (UC_ERR_FETCH_UNMAPPED)",
    UC_ERR_WRITE_PROT: "Write to write-protected memory (UC_ERR_WRITE_PROT)",
    UC_ERR_READ_PROT: "Read from non-readable memory (UC_ERR_READ_PROT)",
    UC_ERR_FETCH_PROT: "Fetch from non-executable memory (UC_ERR_FETCH_PROT)",
    UC_ERR_READ_UNALIGNED: "Read from unaligned memory (UC_ERR_READ_UNALIGNED)",
    UC_ERR_WRITE_UNALIGNED: "Write to unaligned memory (UC_ERR_WRITE_UNALIGNED)",
    UC_ERR_FETCH_UNALIGNED: "Fetch from unaligned memory (UC_ERR_FETCH_UNALIGNED)",
    UC_ERR_EXCEPTION: "Unhandled CPU exception (UC_ERR_EXCEPTION)",
}


class UcError(Exception):
    """A fault that stopped a run, or an access of nothing mapped: its
    code, one of the UC_ERR_ constants."""

    def __init__(self, errno: int):
        super().__init__()
        self.errno = errno

    def __str__(self) -> str:
        return ERROR_MESSAGES[self.errno]


def new_cpu() -> "Mc683xx":
    """A CPU, nothing mapped."""
    return Mc683xx()


# The registers by mc683xx's names.
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
# The permissions as mc683xx's letters.
PERMISSION_LETTERS = ((UC_PROT_READ, "r"), (UC_PROT_WRITE, "w"), (UC_PROT_EXEC, "x"))
# The invalid access kinds and errors, by mc683xx's cycle kind (its
# CycleKind's member, `cycle_kind`: fetch, read, write) and whether
# anything is mapped there; and for an address error where something is,
# an unaligned access's.
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


def cycle_kind(mc683xx, cycle) -> int:
    """mc683xx's CycleKind `cycle` as this module's FETCH, READ or WRITE,
    by member: its `int` is outside mc683xx's semver."""
    return {mc683xx.CycleKind.FETCH: FETCH, mc683xx.CycleKind.READ: READ, mc683xx.CycleKind.WRITE: WRITE}[cycle]


def permissions(perms: int) -> str:
    """The permission bits as mc683xx's letters."""
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

    def mem_read(self, address: int, size: int) -> bytearray:
        return self.cpu.mem_read(address, size)

    def mem_write(self, address: int, data: bytes) -> None:
        self.cpu.mem_write(address, data)

    def emu_stop(self) -> None:
        self.cpu.emu_stop()


class Device:
    """A region of the harness's own, as mc683xx's Machine reaches it: each
    cycle a read or write of its bytes, a read hook covering it run
    before a read and a write hook before a write; an exception's frame
    written as the CPU stacks it, shown no hook; a cycle its permissions
    refuse, a bus error."""

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
    """The API the harness uses, on mc683xx's bare CPU32:
    the regions mapped, and placed in the Machine or kept as devices at the
    first run (place); native_vectors the exceptions the CPU takes
    itself, which no interrupt hook is called for; watch_only, set where
    the memory hooks only note what is read or written, that they make
    no device."""

    def __init__(self):
        import mc683xx

        self.cpu = mc683xx.Machine()
        self.stop_request = self.cpu.stop_request()
        self.cpu.set_stop_on_fault(True)
        self.regions: list[Region] = []
        self.starts: list[int] = []
        self.hooks: list[HookEntry] = []
        self.native_vectors: set[int] = set()
        self.watch_only = False
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
        hooked = [
            hook
            for hook in self.hooks
            if hook.kind in (UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE) and not self.watch_only
        ]
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

    # hooks

    def hook_add(self, kind: int, callback: Callable, user_data: object = None, begin: int = 1, end: int = 0):
        """A hook of a kind on the addresses begin to end, begin past end
        for every address."""
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
        """A device's cycle: the hooks of kind covering it, run before the
        access."""
        self.in_device = True
        try:
            for hook in self.hooks:
                if hook.kind == kind and hook.covers(address):
                    hook.callback(InRun(self, None), access, address, size, value, hook.data)
        finally:
            self.in_device = False

    def intercepted(self, interception) -> bool:
        """An exception the CPU raised, before anything is stacked: to
        the interrupt hooks, in place of taking it; with none, the run
        stops."""
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
            fault = end.fault
            mapped = self.region(fault.address) is not None
            cycle = cycle_kind(mc683xx, fault.cycle)
            if mapped and fault.cause == mc683xx.FaultCause.ADDRESS_ERROR:
                kind, error = UNALIGNED[cycle]
            else:
                kind, error = INVALID[(cycle, mapped)]
            for hook in self.hooks:
                if hook.kind & UC_HOOK_MEM_INVALID and hook.covers(fault.address):
                    hook.callback(self, kind, fault.address, fault.size, 0, hook.data)
            raise UcError(error)
        if isinstance(end, mc683xx.End.Stop) and isinstance(end.stop, mc683xx.Stop.Halted):
            raise UcError(UC_ERR_EXCEPTION)
