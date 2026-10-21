"""The emulator harness the rebuild's checks share: the image with the
rebuilt image's code loaded beside it in Unicorn (libc_check.Machine),
and calls into either from the same RAM and registers.

A call starts in supervisor mode with interrupts masked, as the
firmware runs, every register a distinct sentinel but A6, which holds a
caller's frame, and its own stack at STACK, which is never compared;
the EEPROM erased, and compared after it as RAM is.
Unicorn has no CPU32; the 68020 stands in, and the image uses none of
the instructions either has alone (PLAN.md, "A rebuild that runs").
"""

import re
import struct

from unicorn import UC_HOOK_INTR, UcError
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
from rebuild_link import REBUILT, REBUILT_DATA, Section

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
# The status register a call starts with: supervisor, interrupts masked.
SUPERVISOR = 0x2700
# A register's value before a call, distinct for each register.
SENTINEL = 0x5A5A0000
# The calls' stack, clear of ROM, RAM, the peripherals, the return
# address and the rebuilt code.
STACK = 0x700000
STACK_SIZE = 0x10000
STACK_TOP = STACK + STACK_SIZE - 0x10
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


def rebuilt_machine(image: bytes, linked: list[Section]) -> Machine:
    """The image, with the rebuilt image's code at REBUILT and a stack."""
    m = Machine(image)
    m.uc.mem_map(REBUILT, REBUILT_SIZE)
    for section in linked:
        if section.name in (".rebuilt", ".rebuilt_data"):
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


def reset(m: Machine, ram: bytes) -> None:
    m.uc.mem_write(RAM, ram)
    m.uc.mem_write(EEPROM, ERASED)
    m.uc.reg_write(UC_M68K_REG_SR, SUPERVISOR)
    for name, register in REGISTERS.items():
        m.uc.reg_write(register, STARTING[name])


class Run:
    """One call: its stack arguments laid above the return address, its
    registers after, RAM and the EEPROM after, and the fault or trap that
    stopped it, if one did."""

    def __init__(self, m: Machine, start: int, stack_arguments: bytes = b""):
        trapped = []

        def trap(uc, number, _):
            trapped.append(number)
            uc.emu_stop()

        sp = STACK_TOP - 4 - len(stack_arguments)
        m.uc.mem_write(sp, struct.pack(">I", RETURN) + stack_arguments + FRAME_CONTENTS)
        m.uc.reg_write(UC_M68K_REG_A7, sp)
        hook = m.uc.hook_add(UC_HOOK_INTR, trap)
        try:
            m.uc.emu_start(start, RETURN, count=STEP_LIMIT)
            self.stopped = f"trap {trapped[0]}" if trapped else None
        except UcError as error:
            self.stopped = f"{error} at {m.uc.reg_read(UC_M68K_REG_PC):x}"
        finally:
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


def ram_difference(m: Machine, a: Run, b: Run) -> str | None:
    """None if two calls left RAM the same, but for pointers, at even
    addresses as the 68k keeps a long, to the same literal, and the
    EEPROM the same; else where it first differs."""
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
                and same_literal(m, *(struct.unpack_from(">I", run.ram, start)[0] for run in (a, b)))
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


def stopped_outcome(a: Run, b: Run, names: tuple[str, str]) -> str | None:
    """None if both calls returned; a skip if both stopped, the kernel or
    a peripheral deciding; else the difference."""
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
