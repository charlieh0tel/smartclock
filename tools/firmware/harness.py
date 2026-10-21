"""The emulator harness the rebuild's checks share: the image with the
rebuilt image's code loaded beside it in Unicorn (libc_check.Machine),
and calls into either from the same RAM and registers.

A call starts in supervisor mode with interrupts masked, as the
firmware runs, every register a distinct sentinel but A6, which holds a
caller's frame, and its own stack at STACK, filled and never compared;
the EEPROM erased, and compared after it as RAM is; and the devices'
registers zeroed.  The harness stands in for what lies outside memory,
the same for both calls: trap #7 runs the image's own handler; the
kernel's traps, #3 and #4, are answered by a model of each service; and
the kernel calls and device writes a call makes, in order, are its
effects, compared as RAM is.
Unicorn has no CPU32; the 68020 stands in, and the image uses none of
the instructions either has alone (PLAN.md, "A rebuild that runs").
"""

import re
import struct
from dataclasses import dataclass

from unicorn import UC_HOOK_INTR, UC_HOOK_MEM_INVALID, UC_HOOK_MEM_WRITE, Uc, UcError
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
# The vectors whose handlers the harness runs, the image's own: trap #7,
# which hands back the caller's status register (docs/firmware/psos.md).
HANDLED = {TRAP_VECTORS + 7}
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
    and a count it returns as none done."""

    arguments: tuple[str, ...] = ()
    results: tuple[str, ...] = ()
    reads: tuple[str, str | None] | None = None
    fills: str | None = None
    none_done: str | None = None


# The kernel's services, trap #3, by number.
KERNEL_SERVICES = {
    0: Service(),
    1: Service(("D0", "D1", "D2", "D3", "A0"), ("A0",)),
    2: Service(("A0", "A1")),
    3: Service(("D0",), ("A0", "D0")),
    4: Service(("A0",)),
    5: Service(("A0",)),
    6: Service(("A0",)),
    7: Service(("A0", "D0"), ("D0",)),
    8: Service(("D0", "D1", "D2", "D3"), ("A0",)),
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
IO_FUNCTIONS = {
    0: Service(("D0",)),
    1: Service(("D0", "D1")),
    2: Service(("D0",)),
    3: Service(("D0", "D2"), none_done="D2"),
    4: Service(("D0", "D2"), reads=("A0", "D2")),
    5: Service(("D0", "D2", "D3")),
}
# The traps the harness answers for the kernel, by vector.  Each takes
# its service or function in D7's low byte and returns its error there.
KERNEL_TRAPS = {TRAP_VECTORS + 3: KERNEL_SERVICES, TRAP_VECTORS + 4: IO_FUNCTIONS}
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
        for start, size in DEVICES:
            self.uc.mem_map(start, size)
            self.uc.hook_add(UC_HOOK_MEM_WRITE, self.device_write, begin=start, end=start + size - 1)

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


def answer_kernel(uc: Uc, vector: int) -> KernelCall | str:
    """Answers a kernel trap as KERNEL_TRAPS models it, returning past
    the trap; or, nothing done, why it cannot: a service it does not
    model, or a buffer it cannot read or fill."""
    number = uc.reg_read(REGISTERS[SERVICE_REGISTER]) & SERVICE_MASK
    service = KERNEL_TRAPS[vector].get(number)
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
    except UcError:
        return out_of_reach
    call = KernelCall(vector, number, tuple((name, uc.reg_read(REGISTERS[name])) for name in service.arguments), data)
    for name in service.results:
        uc.reg_write(REGISTERS[name], KERNEL_RESULT)
    if service.none_done:
        uc.reg_write(REGISTERS[service.none_done], 0)
    uc.reg_write(REGISTERS[SERVICE_REGISTER], NO_ERROR)
    uc.reg_write(UC_M68K_REG_PC, uc.reg_read(UC_M68K_REG_PC) + TRAP_SIZE)
    return call


class Run:
    """One call: its stack arguments laid above the return address, its
    registers after, RAM and the EEPROM after, its effects, and the fault
    or trap that stopped it, if one did."""

    def __init__(self, m: RebuiltMachine, start: int, stack_arguments: bytes = b""):
        trapped, invalid = [], []
        self.effects = m.effects = []

        def trap(uc, number, _):
            if number in HANDLED:
                take_exception(uc, number)
                return
            if number == RETURN_FROM_EXCEPTION and return_from_exception(uc):
                return
            if number in KERNEL_TRAPS:
                call = answer_kernel(uc, number)
                if isinstance(call, KernelCall):
                    m.effects.append(call)
                    return
                trapped.append(f"{number}, {call}")
            else:
                trapped.append(number)
            uc.emu_stop()

        def fault(uc, access, address, size, value, _):
            invalid.append(address)
            return False

        sp = STACK_TOP - 4 - len(stack_arguments)
        m.uc.mem_write(sp, struct.pack(">I", RETURN) + stack_arguments + FRAME_CONTENTS)
        m.uc.reg_write(UC_M68K_REG_A7, sp)
        hooks = [m.uc.hook_add(UC_HOOK_INTR, trap), m.uc.hook_add(UC_HOOK_MEM_INVALID, fault)]
        try:
            m.uc.emu_start(start, RETURN, count=STEP_LIMIT)
            self.stopped = f"trap {trapped[0]}" if trapped else None
        except UcError as error:
            of = f" of {invalid[0]:x}" if invalid else ""
            self.stopped = f"{error}{of} at {m.uc.reg_read(UC_M68K_REG_PC):x}"
        finally:
            for hook in hooks:
                m.uc.hook_del(hook)
        if self.stopped is None and m.uc.reg_read(UC_M68K_REG_PC) != RETURN:
            self.stopped = f"no return in {STEP_LIMIT} steps"
        self.registers = {name: m.uc.reg_read(register) for name, register in REGISTERS.items()}
        self.ram = m.read(RAM, RAM_SIZE)
        self.eeprom = m.read(EEPROM, EEPROM_SIZE)

    def value(self, register: str) -> int:
        """A result in a register or pair, high register first."""
        value = 0
        for part in register.split(":"):
            value = value << 32 | self.registers[part]
        return value


def preceding(m: Machine, at: int) -> bytes | None:
    """Up to BUFFER_SPACING bytes before an address in ROM or gcc's
    constants; None if it is in neither."""
    region = next((start for start, size in LITERALS if start <= at < start + size), None)
    if region is None:
        return None
    start = max(region, at - BUFFER_SPACING)
    return m.read(start, at - start)


def same_literal(m: Machine, a: int, b: int) -> bool:
    """Whether two pointers, each in ROM or gcc's constants, name the same
    place in the same text: gcc keeps its own copy of a literal the
    image keeps in ROM, and a pointer may be part way along one or at
    its end.  The text runs back over the bytes, not NUL, the two have
    alike before them, and must not be empty."""
    before = [preceding(m, p) for p in (a, b)]
    if None in before:
        return False
    back = 0
    while back < min(map(len, before)) and before[0][-1 - back] == before[1][-1 - back] != 0:
        back += 1
    try:
        text = m.string(a - back)
        return text != b"" and text == m.string(b - back)
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
    """None if both calls returned; a skip if both stopped, what the
    harness does not model deciding; else the difference."""
    if a.stopped is None and b.stopped is None:
        return None
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
