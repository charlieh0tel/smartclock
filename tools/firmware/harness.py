"""The emulator harness the rebuild's checks share: the image with the
rebuilt image's code loaded beside it in Unicorn (libc_check.Machine),
and calls into either from the same RAM and registers.

A call starts in supervisor mode with interrupts masked, as the
firmware runs, every register a distinct sentinel but A6, which holds a
caller's frame, and its own stack at STACK, filled and never compared;
the EEPROM erased, and compared after it as RAM is; and the devices'
registers zeroed but the status bits the routines poll for.  The
harness stands in for what lies outside memory, the same for both
calls: traps #1 and #7 run the image's own handlers; the kernel's
traps, #3 and #4, are answered by a model of each service; a trap that
never returns, or a write to code, ends a call; and the kernel calls
and device writes a call makes, in order, are its effects, compared as
RAM is.
Unicorn has no CPU32; the 68020 stands in, and the image uses none of
the instructions either has alone (PLAN.md, "A rebuild that runs").
"""

import re
import struct
from dataclasses import dataclass

from unicorn import (
    UC_HOOK_INTR,
    UC_HOOK_MEM_INVALID,
    UC_HOOK_MEM_READ,
    UC_HOOK_MEM_WRITE,
    UC_MEM_WRITE_PROT,
    Uc,
    UcError,
)
from unicorn.m68k_const import (
    UC_M68K_REG_A0,
    UC_M68K_REG_A1,
    UC_M68K_REG_A2,
    UC_M68K_REG_A3,
    UC_M68K_REG_A4,
    UC_M68K_REG_A5,
    UC_M68K_REG_A6,
    UC_M68K_REG_A7,
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

from libc_check import BUFFER, BUFFER_SPACING, RAM, RAM_SIZE, RETURN, ROM, ROM_SIZE, STEP_LIMIT, Machine
from rebuild_link import IMAGE_PREFIX, LOADED, REBUILT, REBUILT_DATA, Section

# Every register a routine might take, by the sidecar's name.
REGISTERS = {
    "D0": UC_M68K_REG_D0,
    "D1": UC_M68K_REG_D1,
    "D2": UC_M68K_REG_D2,
    "D3": UC_M68K_REG_D3,
    "D4": UC_M68K_REG_D4,
    "D5": UC_M68K_REG_D5,
    "D6": UC_M68K_REG_D6,
    "D7": UC_M68K_REG_D7,
    "A0": UC_M68K_REG_A0,
    "A1": UC_M68K_REG_A1,
    "A2": UC_M68K_REG_A2,
    "A3": UC_M68K_REG_A3,
    "A4": UC_M68K_REG_A4,
    "A5": UC_M68K_REG_A5,
    "A6": UC_M68K_REG_A6,
}
# The status register a call starts with: supervisor, interrupts masked;
# and its supervisor bit.
SUPERVISOR = 0x2700
SUPERVISOR_BIT = 0x2000
# A trap instruction's bytes.
TRAP_SIZE = 2
# A register's value before a call, distinct for each register.
SENTINEL = 0x5A5A0000
# The calls' stack, clear of ROM, RAM, the peripherals, the return
# address and the rebuilt code.
STACK = 0x700000
STACK_SIZE = 0x10000
STACK_TOP = STACK + STACK_SIZE - 0x10
# What the stack holds at a call's start, so that a local read before
# it is written reads the same in both calls, wherever their frames lie.
CLEAN_STACK = b"\xa5" * STACK_SIZE
# link.w An,#d: its opcode, with the register's bits masked, and its
# bytes with the displacement.  Each frame it makes starts as
# CLEAN_STACK, so that a local read before it is written does not read
# what an earlier callee left there, which lies where each copy's
# frames put it.
LINK = 0x4E50
LINK_MASK = 0xFFF8
LINK_INSTRUCTION = struct.Struct(">Hh")
# A6 at a call's start: a caller's frame above the call's stack, its
# saved frame pointer 0 and its return address RETURN.  The image's code
# may walk its caller's frame (float_raise reads the return address
# there); gcc's keeps no frame pointer, so its callees find the caller's
# A6 instead, and must find a frame there too.
FRAME = STACK_TOP
FRAME_CONTENTS = struct.pack(">II", 0, RETURN)
# Each register's value at a call's start.
STARTING = {name: FRAME if name == "A6" else SENTINEL + i for i, name in enumerate(REGISTERS)}
REBUILT_SIZE = 0x100000
# The EEPROM, chip select 9's 8 KB (docs/firmware/README.md), erased at a
# call's start and compared after it as RAM is.
EEPROM = 0x400000
EEPROM_SIZE = 0x2000
ERASED = b"\xff" * EEPROM_SIZE
# The primary's vector table (docs/firmware/README.md), which the
# harness takes an exception through, and a vector's bytes in it.
VECTORS = 0x10000
VECTOR_SIZE = 4
# trap #n is vector TRAP_VECTORS + n.
TRAP_VECTORS = 32
# The vectors whose handlers the harness runs, the image's own: trap #1,
# which saves or restores a status register through A0 for setjmp and
# longjmp (primary_trap_1, 0x510e8), and trap #7, which hands back the
# caller's status register (docs/firmware/psos.md).
HANDLED = {TRAP_VECTORS + 1, TRAP_VECTORS + 7}
# The vectors whose handlers never return to the routine, which ends at
# them, the two calls then compared as if it had returned, but for its
# result: trap #5, which ends an interrupt by switching to the first
# ready task (docs/firmware/psos.md, "Inside the kernel"), and traps #11
# and #12, which shut down and restart (docs/firmware/restart.md).
ENDING = {TRAP_VECTORS + 5, TRAP_VECTORS + 11, TRAP_VECTORS + 12}
# A write to code, the image's ROM or gcc's, which the checks protect,
# made by code, not by data a call runs, ends the call the same way, at
# the bus error it would raise: the boot chip select is read only
# (CSORBT 0x6870, R/W 01), so that a write finds no DSACK, and the bus
# monitor is on (SYPCR 0xcc, BME), as boot_reset and primary_reset set
# them (MC68331UM, table 4-22 and D.3.12).
BUS_ERROR = 2
# The 68020's exception frame of format 0: the status register, the
# return address and the format and vector offset word.
FRAME_FORMAT = ">HIH"
FRAME_FORMAT_SHIFT = 12
# What QEMU, under Unicorn, raises for an rte, which the interrupt hook
# is then called with in place of the processor's return (EXCP_RTE).
RETURN_FROM_EXCEPTION = 0x100


@dataclass(frozen=True)
class Service:
    """A kernel service or I/O function as the harness models it, from
    the registers the binding's stub loads before the trap and stores
    from after it (docs/firmware/psos.md, "Calls"): the registers it
    reads, which a call records; those it returns in, which it sets to
    KERNEL_RESULT; the bytes it reads through a pointer, as (pointer,
    length) registers, the length None for a message, which a call
    records by their contents, since a caller's buffer on its stack
    lies where its own frame puts it; a buffer it fills with MESSAGE;
    a count it returns as none done; and a NUL-terminated string it
    reads, as (pointer, count) registers, recorded by its contents and
    returned as its length in the count and its end in the pointer; and
    a segment it allocates, as (size, pointer) registers, returned from
    ALLOCATIONS; and a task it identifies, as (name, pointer) registers,
    returned as its record, a segment of TASK_RECORD_SIZE allocated the
    first time a call names it."""

    arguments: tuple[str, ...] = ()
    results: tuple[str, ...] = ()
    reads: tuple[str, str | None] | None = None
    fills: str | None = None
    none_done: str | None = None
    text: tuple[str, str] | None = None
    allocates: tuple[str, str] | None = None
    identifies: tuple[str, str] | None = None


# The kernel's services, trap #3, by number.
KERNEL_SERVICES = {
    0: Service(),
    1: Service(("D0", "D1", "D2", "D3", "A0"), ("A0",)),
    2: Service(("A0", "A1")),
    3: Service(("D0",), ("D0",), identifies=("D0", "A0")),
    4: Service(("A0",)),
    5: Service(("A0",)),
    6: Service(("A0",)),
    7: Service(("A0", "D0"), ("D0",)),
    8: Service(("D0", "D1", "D2", "D3"), allocates=("D1", "A0")),
    9: Service(("A0",)),
    10: Service(("A0", "A1")),
    **{number: Service() for number in (11, 12, 13, 14, 15, 17, 34)},
    16: Service(("A0", "A1")),
    18: Service(("A0", "A1", "D0"), ("D0", "D1")),
    19: Service(("A0",), reads=("A1", None)),
    20: Service(("D0", "D1", "D2"), ("A0",)),
    21: Service(("D0",), ("A0", "D1", "D2")),
    22: Service(("A0",)),
    23: Service(("A0",), reads=("A1", None)),
    24: Service(("A0", "D0", "D1"), fills="A1"),
    25: Service(("A0", "D1")),
    26: Service(("D0", "D1", "D2"), ("D1",)),
    27: Service(("D1", "D2"), ("D1",)),
    28: Service(("D0",)),
    29: Service(),
    30: Service((), ("D0", "D1", "D2")),
    31: Service(("D0", "D1", "D2")),
    32: Service(("D0", "D1"), ("D0",)),
    33: Service(),
    35: Service(("A0", "D0", "D1"), ("A0",)),
    36: Service(("A0", "D0")),
    37: Service(("A0", "D0"), ("A1",)),
    38: Service(("A0", "A1")),
}
# The I/O functions, trap #4, by number: a read reads nothing, a write
# writes all it is given.
IO_TRAP = TRAP_VECTORS + 4
WRITE = 4
IO_FUNCTIONS = {
    0: Service(("D0",)),
    1: Service(("D0", "D1")),
    2: Service(("D0",)),
    3: Service(("D0", "D2"), none_done="D2"),
    WRITE: Service(("D0", "D2"), reads=("A0", "D2")),
    5: Service(("D0", "D2", "D3")),
}
# A device is D0's bits 8 to 15 (kernel_dispatch_io, 0x14c0a).  Those
# whose write sends the string at A0 whatever D2 holds and returns its
# length in D2 and its end in A0: DUART channel B and the console's
# string device, through drtb_write (0x1dd98), and the SCI, through
# sci_write (0x20152) (docs/firmware/structures.md, "The kernel's
# configuration"); and that write.  The QSPI's, device 1, takes D2.
DEVICE_SHIFT = 8
DEVICE_MASK = 0xFF
STRING_DEVICES = {0, 2, 3}
STRING_WRITE = Service(("D0",), text=("A0", "D2"))
# The traps the harness answers for the kernel, by vector.  Each takes
# its service or function in D7's low byte and returns its error there.
KERNEL_TRAPS = {TRAP_VECTORS + 3: KERNEL_SERVICES, IO_TRAP: IO_FUNCTIONS}
SERVICE_REGISTER = "D7"
SERVICE_MASK = 0xFF
# What a modeled service returns: no error, and in each result register
# a value no mapped memory lies at, so that one used as an address
# faults.
NO_ERROR = 0
KERNEL_RESULT = 0x6B000000
# A message's five longs from +4, which the kernel copies from a sender's
# record to a receiver's (moveml a1@(4),d3-d7 at 0x1562e and 0x156c6;
# +0 is the kernel's link); the one a service fills a buffer with, every
# long KERNEL_RESULT.
MESSAGE_OFFSET = 4
MESSAGE_LONGS = 5
MESSAGE = struct.pack(">I", KERNEL_RESULT) * MESSAGE_LONGS
MESSAGE_SIZE = len(MESSAGE)
# The most a service's buffer may hold: RAM's size.
LONGEST_BUFFER = RAM_SIZE
# Where service 8, alloc_seg, takes its segments, D1 bytes each rounded
# up to a word (kernel_allocate, 0x15272): RAM's top quarter, in the
# kernel's region 1 (docs/firmware/structures.md, "The kernel's
# configuration") and clear of the arguments' buffers, from its start
# at each call's, so that the two calls get the same segments and RAM
# compares them.
ALLOCATIONS = RAM + RAM_SIZE * 3 // 4
ALLOCATIONS_END = RAM + RAM_SIZE
SEGMENT_ALIGNMENT = 2
# A task's id is its record's address, which service 3, ident, returns
# in A0 for the name in D0, the running task's for 0 (kernel_ident,
# 0x14ed0), and a record's bytes (docs/firmware/psos.md, "Inside the
# kernel").
TASK_RECORD_SIZE = 0xB2


@dataclass(frozen=True)
class KernelCall:
    """A call the harness answered for the kernel: its trap, service, the
    registers the service reads, and the bytes it reads through a
    pointer."""

    trap: int
    service: int
    arguments: tuple[tuple[str, int], ...]
    data: bytes = b""

    def __str__(self) -> str:
        parts = [f"trap #{self.trap - TRAP_VECTORS} service {self.service}"]
        parts += [f"{name}={value:x}" for name, value in self.arguments]
        if self.data:
            parts.append(f"data {self.data.hex()}")
        return " ".join(parts)


# The devices' chip selects and the MC68331's own modules, whose
# registers the firmware reads and writes (docs/firmware/README.md and
# restart.md, "The switch byte at 0x302000"): each mapped, page by page,
# as memory zeroed at a call's start, so that a read finds zero or what
# the call last wrote there, and each write is a call's effect, compared
# as kernel calls are.
DEVICES = [
    (0x200000, 0x1000),  # the DUART
    (0x300000, 0x10000),  # the latch, the switch byte and the registers at 0x304000 and 0x306000
    (0x500000, 0x1000),
    (0xFFF000, 0x1000),  # the GPT, SIM and QSM, and the chip select at 0xfff800
]


@dataclass(frozen=True)
class Status:
    """A device register a routine polls, which reads as the bits its
    data sheet says the device sets when what the routine waits for has
    happened, whatever was written there: its address, its bytes, the
    bits a write leaves, and the bits set."""

    address: int
    size: int
    kept: int
    set: int


# The registers the routines poll, read alike in both calls; every other
# bit reads as zero, or as written where kept says.
STATUS_REGISTERS = [
    # the DUART's SRA and SRB: TxRDY (MC68681, "Channel A status register
    # (SRA) and channel B status register (SRB)"), which
    # duartb_putc_polled waits for; a write there is to CSRA or CSRB
    Status(0x200003, 1, 0, 0x04),
    Status(0x200013, 1, 0, 0x04),
    # the SCI's SCSR: TDRE (MC68331UM, D.4.6), which sci_putc_polled
    # waits for
    Status(0xFFFC0C, 2, 0, 0x0100),
    # the QSPI's SPCR1, whose SPE the QSPI clears at the end of its queue
    # (6.3.5.1), and SPSR, whose SPIF it then sets (D.4.13): qspi_send
    # waits for both
    Status(0xFFFC1A, 2, 0x7FFF, 0),
    Status(0xFFFC1F, 1, 0, 0x80),
]


@dataclass(frozen=True)
class DeviceWrite:
    """A write to a device: its address, its bytes and its value."""

    address: int
    size: int
    value: int

    def __str__(self) -> str:
        return f"write of {self.value:0{2 * self.size}x} to {self.address:x}"


# Where a literal's text may lie: the image's ROM and gcc's code and
# constants.
LITERALS = [(ROM, ROM_SIZE), (REBUILT, REBUILT_SIZE)]
# A pointer's bytes, and the alignment the 68k keeps one at in RAM.
POINTER_SIZE = 4
POINTER_ALIGNMENT = 2
# Arguments the cases cycle through, by kind.
INTEGERS = [0, 1, -1, 0x12345678, -100000, 7]
CHARS = [-1, 0x41, 0x7F, 0, -0x80, 3]
SHORTS = [-2, 0x1234, -30000, 0, 0x7FFF, 9]
REALS = [1.5, -2.25, 1e10, 3.0e-5, 0.0, 123.456]
CASES = 6
# How far apart in the lists one case's successive arguments are taken,
# so that arguments swapped or one loaded twice show.
POSITION_STRIDE = 1
# What a char pointer's buffer holds.
TEXT = b"123.456\0"
# The sizes of the base types the decompiled C's prototypes use; the
# rest are pointers or are asserted in the C.
BASE_SIZES = {
    "char": 1, "byte": 1, "uchar": 1, "undefined1": 1, "uint8_t": 1, "int8_t": 1, "bool": 1,
    "short": 2, "ushort": 2, "undefined2": 2, "uint16_t": 2, "int16_t": 2,
    "int": 4, "uint": 4, "long": 4, "ulong": 4, "undefined4": 4, "uint32_t": 4, "int32_t": 4,
    "float": 4,
    "double": 8, "undefined8": 8, "longlong": 8, "ulonglong": 8,
}  # fmt: skip
# Of those, the unsigned: their narrow results are zero-extended.
UNSIGNED = {"byte", "uchar", "undefined1", "uint8_t", "bool", "ushort", "undefined2", "uint16_t"}


class Types:
    """The sizes of the C file's types, and which name pointers."""

    def __init__(self, c: str):
        self.pointers = set(re.findall(r"^typedef [^;()]*\*\s*(\w+);$", c, re.M))
        self.sizes = dict(BASE_SIZES)
        for name, size in re.findall(r"^_Static_assert\(sizeof\((\w+)\) == (\d+),", c, re.M):
            self.sizes[name] = int(size)

    def is_pointer(self, kind: str) -> bool:
        return kind.endswith("*") or kind in self.pointers

    def size(self, kind: str) -> int | None:
        """A type's bytes; None if it is not known."""
        return 4 if self.is_pointer(kind) else self.sizes.get(kind)


class RebuiltMachine(Machine):
    """The image with the rebuilt image's code, and the effects the call
    running has had on what lies outside memory, in order: kernel calls
    and device writes."""

    def __init__(self, image: bytes):
        super().__init__(image)
        self.effects: list[KernelCall | DeviceWrite] = []
        # each C function's two copies, gcc's address and the image's
        self.copies: set[tuple[int, int]] = set()
        # where service 8's next segment starts, and the task records
        # service 3 has returned, by name
        self.next_allocation = ALLOCATIONS
        self.tasks: dict[int, int] = {}
        for start, size in DEVICES:
            self.uc.mem_map(start, size)
            self.uc.hook_add(UC_HOOK_MEM_WRITE, self.device_write, begin=start, end=start + size - 1)
        for status in STATUS_REGISTERS:
            self.uc.hook_add(
                UC_HOOK_MEM_READ, self.status_read, status, begin=status.address, end=status.address + status.size - 1
            )

    def status_read(self, uc: Uc, access: int, address: int, size: int, value: int, status: Status) -> None:
        """Sets a polled register to what it reads as, before the read."""
        written = int.from_bytes(uc.mem_read(status.address, status.size), "big")
        uc.mem_write(status.address, (written & status.kept | status.set).to_bytes(status.size, "big"))

    def device_write(self, uc: Uc, access: int, address: int, size: int, value: int, _) -> None:
        self.effects.append(DeviceWrite(address, size, value & ((1 << 8 * size) - 1)))


def rebuilt_machine(image: bytes, linked: list[Section], linked_symbols: dict[str, int]) -> RebuiltMachine:
    """The image, with the rebuilt image's code at REBUILT, a stack, the
    EEPROM and the devices, and which of the link's symbols name a C
    function's copies, gcc's and the image's."""
    m = RebuiltMachine(image)
    m.copies = {
        (address, linked_symbols[IMAGE_PREFIX + name])
        for name, address in linked_symbols.items()
        if IMAGE_PREFIX + name in linked_symbols
    }
    m.uc.mem_map(REBUILT, REBUILT_SIZE)
    for section in linked:
        if section.name in LOADED:
            m.uc.mem_write(section.address, section.data)
    m.uc.mem_map(STACK, STACK_SIZE)
    m.uc.mem_map(EEPROM, EEPROM_SIZE)
    return m


def argument(m: Machine, types: Types, kind: str, case: int) -> bytes:
    """The case'th argument of a kind, as its bytes, a pointer to a fresh
    buffer of what it points at."""
    real = REALS[case % len(REALS)]
    if types.is_pointer(kind):
        pointee = kind.removesuffix("*").strip()
        contents = (
            struct.pack(">d", real) * 4
            if pointee == "double"
            else struct.pack(">f", real) * 8
            if pointee == "float"
            else TEXT
            if pointee == "char"
            else bytes((case * 37 + i * 11) & 0xFF for i in range(BUFFER_SPACING))
        )
        return struct.pack(">I", m.buffer(contents))
    if kind == "double":
        return struct.pack(">d", real)
    if kind == "float":
        return struct.pack(">f", real)
    size = types.size(kind)
    if size == 1:
        return struct.pack(">b", CHARS[case % len(CHARS)])
    if size == 2:
        return struct.pack(">h", SHORTS[case % len(SHORTS)])
    if size == 8:
        return struct.pack(">q", INTEGERS[case % len(INTEGERS)])
    return struct.pack(">I", INTEGERS[case % len(INTEGERS)] & 0xFFFFFFFF)


def arguments(m: Machine, types: Types, kinds: list[str], case: int) -> list[bytes]:
    """A case's arguments, each position taking a different value."""
    return [argument(m, types, kind, case + POSITION_STRIDE * i) for i, kind in enumerate(kinds)]


def gcc_slots(values: list[bytes]) -> bytes:
    """Arguments as gcc pushes them: a char or short promoted to a whole
    int in a 4-byte slot."""
    return b"".join(
        value if len(value) >= 4 else int.from_bytes(value, "big", signed=True).to_bytes(4, "big", signed=True)
        for value in values
    )


def fresh_ram(m: Machine) -> None:
    """Zero RAM and the buffers' allocator: a case's starting point."""
    m.next_buffer = BUFFER
    m.uc.mem_write(RAM, bytes(RAM_SIZE))


def reset(m: RebuiltMachine, ram: bytes) -> None:
    m.uc.mem_write(RAM, ram)
    m.next_allocation = ALLOCATIONS
    m.tasks = {}
    m.uc.mem_write(EEPROM, ERASED)
    m.uc.mem_write(STACK, CLEAN_STACK)
    for start, size in DEVICES:
        m.uc.mem_write(start, bytes(size))
    m.uc.reg_write(UC_M68K_REG_SR, SUPERVISOR)
    for name, register in REGISTERS.items():
        m.uc.reg_write(register, STARTING[name])


def take_exception(uc: Uc, vector: int) -> None:
    """Enters a vector's handler as the processor does: a format 0 frame
    on the supervisor stack, returning past the trap, and the handler's
    address from the primary's table.  Unicorn calls the interrupt hook
    in place of doing this, with PC at the trap."""
    frame = struct.pack(
        FRAME_FORMAT, uc.reg_read(UC_M68K_REG_SR), uc.reg_read(UC_M68K_REG_PC) + TRAP_SIZE, vector * VECTOR_SIZE
    )
    sp = uc.reg_read(UC_M68K_REG_A7) - len(frame)
    uc.mem_write(sp, frame)
    uc.reg_write(UC_M68K_REG_A7, sp)
    uc.reg_write(UC_M68K_REG_SR, uc.reg_read(UC_M68K_REG_SR) | SUPERVISOR_BIT)
    uc.reg_write(UC_M68K_REG_PC, struct.unpack(">I", uc.mem_read(VECTORS + vector * VECTOR_SIZE, VECTOR_SIZE))[0])


def return_from_exception(uc: Uc) -> bool:
    """Returns from a format 0 frame as rte does; False, the frame left,
    if it is of another format."""
    sp = uc.reg_read(UC_M68K_REG_A7)
    sr, pc, vector_word = struct.unpack(FRAME_FORMAT, uc.mem_read(sp, struct.calcsize(FRAME_FORMAT)))
    if vector_word >> FRAME_FORMAT_SHIFT:
        return False
    uc.reg_write(UC_M68K_REG_A7, sp + struct.calcsize(FRAME_FORMAT))
    uc.reg_write(UC_M68K_REG_SR, sr)
    uc.reg_write(UC_M68K_REG_PC, pc)
    return True


def string(uc: Uc, at: int) -> bytes | None:
    """The NUL-terminated string at an address, without its NUL; None if
    it runs past LONGEST_BUFFER."""
    data = b""
    while len(data) < LONGEST_BUFFER:
        byte = bytes(uc.mem_read(at + len(data), 1))
        if byte == b"\0":
            return data
        data += byte
    return None


def allocate(m: RebuiltMachine, size: int) -> int | None:
    """A segment of a size from ALLOCATIONS; None if it does not fit."""
    segment = m.next_allocation
    end = segment + size + SEGMENT_ALIGNMENT - 1 & -SEGMENT_ALIGNMENT
    if end > ALLOCATIONS_END:
        return None
    m.next_allocation = end
    return segment


def answer_kernel(m: RebuiltMachine, vector: int) -> KernelCall | str:
    """Answers a kernel trap as KERNEL_TRAPS models it, returning past
    the trap; or, nothing done, why it cannot: a service it does not
    model, a buffer it cannot read or fill, or a segment too large."""
    uc = m.uc
    number = uc.reg_read(REGISTERS[SERVICE_REGISTER]) & SERVICE_MASK
    service = KERNEL_TRAPS[vector].get(number)
    device = uc.reg_read(UC_M68K_REG_D0) >> DEVICE_SHIFT & DEVICE_MASK
    if vector == IO_TRAP and number == WRITE and device in STRING_DEVICES:
        service = STRING_WRITE
    if service is None:
        return f"service {number}"
    out_of_reach = f"service {number}, its buffer out of reach"
    data = b""
    try:
        if service.reads:
            pointer, length = service.reads
            size = MESSAGE_SIZE if length is None else uc.reg_read(REGISTERS[length])
            if size > LONGEST_BUFFER:
                return out_of_reach
            start = uc.reg_read(REGISTERS[pointer]) + (MESSAGE_OFFSET if length is None else 0)
            data = bytes(uc.mem_read(start, size))
        if service.fills:
            uc.mem_write(uc.reg_read(REGISTERS[service.fills]) + MESSAGE_OFFSET, MESSAGE)
        if service.text:
            data = string(uc, uc.reg_read(REGISTERS[service.text[0]]))
            if data is None:
                return out_of_reach
    except UcError:
        return out_of_reach
    too_large = f"service {number}, its segment out of reach"
    if service.allocates:
        size, pointer = service.allocates
        segment = allocate(m, uc.reg_read(REGISTERS[size]))
        if segment is None:
            return too_large
        uc.reg_write(REGISTERS[pointer], segment)
    if service.identifies:
        name, pointer = service.identifies
        task = uc.reg_read(REGISTERS[name])
        if task not in m.tasks:
            m.tasks[task] = allocate(m, TASK_RECORD_SIZE)
        if m.tasks[task] is None:
            return too_large
        uc.reg_write(REGISTERS[pointer], m.tasks[task])
    call = KernelCall(vector, number, tuple((name, uc.reg_read(REGISTERS[name])) for name in service.arguments), data)
    for name in service.results:
        uc.reg_write(REGISTERS[name], KERNEL_RESULT)
    if service.none_done:
        uc.reg_write(REGISTERS[service.none_done], 0)
    if service.text:
        pointer, count = service.text
        uc.reg_write(REGISTERS[pointer], uc.reg_read(REGISTERS[pointer]) + len(data))
        uc.reg_write(REGISTERS[count], len(data))
    uc.reg_write(REGISTERS[SERVICE_REGISTER], NO_ERROR)
    uc.reg_write(UC_M68K_REG_PC, uc.reg_read(UC_M68K_REG_PC) + TRAP_SIZE)
    return call


class Run:
    """One call: its stack arguments laid above the return address, its
    registers after, RAM and the EEPROM after, its effects, the ENDING
    vector it ended at instead of returning, and the fault or trap that
    stopped it, if one did."""

    def __init__(self, m: RebuiltMachine, start: int, stack_arguments: bytes = b""):
        trapped, invalid, ended = [], [], []
        self.effects = m.effects = []

        def trap(uc, number, _):
            if number in HANDLED:
                take_exception(uc, number)
                return
            if number in ENDING:
                ended.append(number)
                uc.emu_stop()
                return
            if number == RETURN_FROM_EXCEPTION and return_from_exception(uc):
                return
            if number in KERNEL_TRAPS:
                call = answer_kernel(m, number)
                if isinstance(call, KernelCall):
                    m.effects.append(call)
                    return
                trapped.append(f"{number}, {call}")
            else:
                trapped.append(number)
            uc.emu_stop()

        def fault(uc, access, address, size, value, _):
            if access == UC_MEM_WRITE_PROT and literal_region(uc.reg_read(UC_M68K_REG_PC)) is not None:
                ended.append(BUS_ERROR)
            invalid.append(address)
            return False

        def clean_frame(uc, access, address, size, value, _):
            # at link's push of the frame pointer, the frame below it
            opcode, displacement = LINK_INSTRUCTION.unpack(
                uc.mem_read(uc.reg_read(UC_M68K_REG_PC), LINK_INSTRUCTION.size)
            )
            if opcode & LINK_MASK == LINK and displacement < 0 and address + displacement >= STACK:
                uc.mem_write(address + displacement, CLEAN_STACK[:-displacement])

        sp = STACK_TOP - 4 - len(stack_arguments)
        m.uc.mem_write(sp, struct.pack(">I", RETURN) + stack_arguments + FRAME_CONTENTS)
        m.uc.reg_write(UC_M68K_REG_A7, sp)
        hooks = [
            m.uc.hook_add(UC_HOOK_INTR, trap),
            m.uc.hook_add(UC_HOOK_MEM_INVALID, fault),
            m.uc.hook_add(UC_HOOK_MEM_WRITE, clean_frame, begin=STACK, end=STACK + STACK_SIZE - 1),
        ]
        try:
            m.uc.emu_start(start, RETURN, count=STEP_LIMIT)
            self.stopped = f"trap {trapped[0]}" if trapped else None
        except UcError as error:
            of = f" of {invalid[0]:x}" if invalid else ""
            self.stopped = None if ended else f"{error}{of} at {m.uc.reg_read(UC_M68K_REG_PC):x}"
        finally:
            for hook in hooks:
                m.uc.hook_del(hook)
        self.ended = ended[0] if ended else None
        if self.stopped is None and self.ended is None and m.uc.reg_read(UC_M68K_REG_PC) != RETURN:
            self.stopped = f"no return in {STEP_LIMIT} steps"
        self.registers = {name: m.uc.reg_read(register) for name, register in REGISTERS.items()}
        self.ram = m.read(RAM, RAM_SIZE)
        self.eeprom = m.read(EEPROM, EEPROM_SIZE)

    def end(self) -> str:
        if self.ended is None:
            return "returned"
        return f"vector {self.ended}" if self.ended < TRAP_VECTORS else f"trap #{self.ended - TRAP_VECTORS}"

    def value(self, register: str) -> int:
        """A result in a register or pair, high register first."""
        value = 0
        for part in register.split(":"):
            value = value << 32 | self.registers[part]
        return value


def literal_region(at: int) -> int | None:
    """The start of the LITERALS region, the image's ROM or gcc's code
    and constants, an address lies in; None if neither."""
    return next((start for start, size in LITERALS if start <= at < start + size), None)


def preceding(m: Machine, at: int) -> bytes | None:
    """Up to BUFFER_SPACING bytes before an address in ROM or gcc's
    constants; None if it is in neither."""
    region = literal_region(at)
    if region is None:
        return None
    start = max(region, at - BUFFER_SPACING)
    return m.read(start, at - start)


def same_literal(m: Machine, a: int, b: int) -> bool:
    """Whether two pointers, each in ROM or gcc's constants, name the same
    place in the same text: gcc keeps its own copy of a literal the
    image keeps in ROM, and a pointer may be part way along one or at
    its end.  The text runs back over the bytes, not NUL, the two have
    alike before them.  An empty text is the same only as gcc's own
    copy of it, one pointer in ROM and the other in gcc's constants:
    gcc keeps `""` as any literal's end, so nothing before it tells."""
    before = [preceding(m, p) for p in (a, b)]
    if None in before:
        return False
    back = 0
    while back < min(map(len, before)) and before[0][-1 - back] == before[1][-1 - back] != 0:
        back += 1
    try:
        text = m.string(a - back)
        if text != m.string(b - back):
            return False
        return text != b"" or literal_region(a) != literal_region(b)
    except (UcError, ValueError):
        return False


def same_pointer(m: RebuiltMachine, a: int, b: int) -> bool:
    """Whether two pointers name the same thing in the two copies: the
    same literal, or the same C function, the image's copy and gcc's (a
    driver's echo routine it stores)."""
    return (a, b) in m.copies or (b, a) in m.copies or same_literal(m, a, b)


def ram_difference(m: RebuiltMachine, a: Run, b: Run) -> str | None:
    """None if two calls left RAM the same, but for pointers, at even
    addresses as the 68k keeps a long, to the same literal or function,
    and the EEPROM the same; else where it first differs."""
    at = 0
    while a.ram[at:] != b.ram[at:]:
        first = next(i for i in range(at, len(a.ram)) if a.ram[i] != b.ram[i])
        even = first - first % POINTER_ALIGNMENT
        starts = range(max(at, even - POINTER_SIZE + POINTER_ALIGNMENT), even + 1, POINTER_ALIGNMENT)
        literal = next(
            (
                start
                for start in starts
                if start + POINTER_SIZE <= len(a.ram)
                and same_pointer(m, *(struct.unpack_from(">I", run.ram, start)[0] for run in (a, b)))
            ),
            None,
        )
        if literal is None:
            return f"RAM differs from {RAM + first:x}"
        at = literal + POINTER_SIZE
    if a.eeprom != b.eeprom:
        first = next(i for i in range(EEPROM_SIZE) if a.eeprom[i] != b.eeprom[i])
        return f"EEPROM differs from {EEPROM + first:x}"
    return None


def effects_difference(a: Run, b: Run, names: tuple[str, str]) -> str | None:
    """None if two calls made the same kernel calls and device writes in
    the same order; else the first that differs."""
    for i, (first, second) in enumerate(zip(a.effects, b.effects)):
        if first != second:
            return f"effect {i} {second}, {names[0]}'s {first}"
    if len(a.effects) != len(b.effects):
        return f"effects {len(b.effects)}, {names[0]}'s {len(a.effects)}"
    return None


def stopped_outcome(a: Run, b: Run, names: tuple[str, str]) -> str | None:
    """None if both calls returned or ended at the same vector; a skip if
    both stopped, what the harness does not model deciding; else the
    difference."""
    if a.stopped is None and b.stopped is None:
        return None if a.ended == b.ended else f"one ended: {names[0]} {a.end()}, {names[1]} {b.end()}"
    if a.stopped is not None and b.stopped is not None:
        return "skipped: " + a.stopped
    return f"one stopped: {names[0]} {a.stopped}, {names[1]} {b.stopped}"


def report(name: str, outcomes: list[tuple[str, str | None]]) -> str:
    """ok, skip or FAIL for a routine's cases, printing why; which of
    agreeing, skipped or failed it counts as."""
    failures = [(d, o) for d, o in outcomes if o and not o.startswith("skipped")]
    skips = [o for _, o in outcomes if o and o.startswith("skipped")]
    if failures:
        for description, outcome in failures:
            print(f"FAIL {name} {description}: {outcome}")
        return "failed"
    if len(skips) == len(outcomes):
        print(f"skip {name}: {skips[0].removeprefix('skipped: ')}")
        return "skipped"
    print(f"ok {name}: {len(outcomes) - len(skips)} of {len(outcomes)} cases run")
    return "agreeing"
