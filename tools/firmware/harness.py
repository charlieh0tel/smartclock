"""The emulator harness the rebuild's checks share: the image with the
rebuilt image's code loaded beside it in Unicorn (libc_check.Machine),
and calls into either from the same RAM and registers.

A call starts in supervisor mode with interrupts masked, as the
firmware runs, every register a distinct sentinel but A6, which holds a
caller's frame, and its own stack at STACK, filled and never compared;
RAM zeroed but the kernel's tables, its data area as kernel_init makes
it, the running task's record and the exchanges' ids; the EEPROM
erased and the pointees filled, each compared after it as RAM is; the
devices' registers zeroed but the status bits the routines poll for;
and a routine of the harness's own for each parameter that points at
one, whose calls are effects.  The harness stands in for what
lies outside memory, the same for both calls: traps #1 and #7 run the
image's own handlers; the kernel's traps, #3 and #4, are answered by a
model of each service; a trap that never returns, a zero divide, a
write to code, a branch to itself and the KERNEL_CALL_LIMIT'th kernel
call end a call; and the kernel calls and device writes a call makes,
in order, are its effects, compared as RAM is.
Unicorn has no CPU32; the 68020 stands in, and the image uses none of
the instructions either has alone (PLAN.md, "A rebuild that runs").
"""

import re
import struct
from dataclasses import dataclass

from unicorn import (
    UC_HOOK_CODE,
    UC_HOOK_INTR,
    UC_HOOK_MEM_INVALID,
    UC_HOOK_MEM_READ,
    UC_HOOK_MEM_WRITE,
    UC_MEM_WRITE_PROT,
    UC_PROT_READ,
    UC_PROT_WRITE,
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
# and #12, which shut down and restart (docs/firmware/restart.md); and
# a zero divide, vector 5, whose handler, the common entry at 0x11924,
# finds no task's handler in the harness's RAM, records the exception,
# masks interrupts and spins (restart.md, "Both end in the watchdog").
ZERO_DIVIDE = 5
ENDING = {ZERO_DIVIDE, TRAP_VECTORS + 5, TRAP_VECTORS + 11, TRAP_VECTORS + 12}
# A call that reaches a branch to itself, bra.s or bra.w, has halted:
# with no interrupt in the harness it never leaves, and ends there, as
# fatal_error's spin does.
SELF_BRANCHES = [bytes.fromhex("60fe"), bytes.fromhex("6000fffe")]
HALTED = "halted"
# A call still running at its KERNEL_CALL_LIMIT'th kernel call, a
# task's loop waiting for its next message or tick, ends there, before
# making it, and is compared up to there, both calls alike.
KERNEL_CALL_LIMIT = 1000
STILL_RUNNING = "still running"
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
    lies where its own frame puts it; a buffer it fills, as (buffer,
    exchange) registers, with the exchange's RECEIVED message or else
    MESSAGE;
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
    fills: tuple[str, str] | None = None
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
    24: Service(("A0", "D0", "D1"), fills=("A1", "A0")),
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
# The exchanges whose receivers' messages docs/firmware/structures.md
# documents, by the cell that keeps each one's id, and the message,
# its MESSAGE_SIZE bytes from +4, that request_x receives from it in
# place of MESSAGE.  A case starts with a distinct id in each cell, as
# root_task's creates leave one, an address nothing is mapped at.  To
# spoo, a spoo_request: scpi_task_create (0x29fce) and its priority,
# 0x19, as scpi_task_start sends it (structures.md, spoo_run); to pllc,
# a pllc_request: :SAMPle:INTeger's descriptor (0x334de), index 0,
# source 0 and values[0] 7, in its range of 0 to 256 ("The pllc task's
# SCPI requests" and "Numeric SCPI parameters").
SPOO_EXCHANGE = 0x1032F8
PLLC_EXCHANGE = 0x103804
RECEIVED = {
    SPOO_EXCHANGE: struct.pack(">5I", 0, 0x29FCE, 0x19, 0, 0),
    PLLC_EXCHANGE: struct.pack(">IIBBBBII", 0, 0x334DE, 0, 0, 0, 0, 7, 0),
}
FIRST_EXCHANGE_ID = KERNEL_RESULT + 1
EXCHANGE_IDS = {cell: FIRST_EXCHANGE_ID + i for i, cell in enumerate(RECEIVED)}
RECEIVED_BY_ID = {EXCHANGE_IDS[cell]: message for cell, message in RECEIVED.items()}
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
# The kernel's configuration, psos_config, and its I/O table, which
# psos_config_init (0x51038) copies from ROM at start-up, as (from, to,
# bytes) (docs/firmware/structures.md, "The kernel's configuration"),
# and which RAM starts each case with.
PSOS_CONFIG = 0x10264C
KERNEL_TABLES = [(0x5122A, PSOS_CONFIG, 0x62), (0x5171A, 0x1025BC, 0x90)]
# What kernel_init (0x15cd8), trap #6, makes of the data area, which RAM
# starts each case with, from the image's own code run on the tables:
# from its start to its loop calling each device's function 0, and from
# after that loop to its create of ROOT (0x15e6a).  It carves the area
# into the supervisor stack, the exchanges, the message buffers and the
# task records, each kind in a free list; makes its own record at +0x8c
# the running one, which a call's start replaces with RUNNING_TASK's;
# and sets up regions 0 and 1, the segment headers and
# the empty queue of tasks waiting on a region at +0x64, pointing at
# itself (kernel_region_init, 0x15ece), which os_report_regions walks.
KERNEL_INIT = [(0x15CD8, 0x15E3A), (0x15E5C, 0x15E6A)]
# The configuration's first long is the data area's address, and the
# data area's first long the running task's record, which most of the
# kernel's handlers load (psos.md, "Inside the kernel") and the
# routines that count into it read.  A call starts with that record
# from ALLOCATIONS, which ident returns for the name 0.
RUNNING_TASK = 0


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
class Stub:
    """A routine at ROUTINES: the parameter it is given as, and where
    it reads each argument above its return address, as (offset, bytes,
    whether it is text), or None if it formats them."""

    position: int
    layout: tuple[tuple[int, int, bool], ...] | None


@dataclass(frozen=True)
class RoutineCall:
    """A call of a routine at ROUTINES: the parameter it was given as,
    and its arguments, each a value or a text, or the text it
    formatted."""

    position: int
    arguments: tuple[int | bytes, ...]

    def __str__(self) -> str:
        parts = [f"call of parameter {self.position}'s routine"]
        parts += [repr(a) if isinstance(a, bytes) else f"{a:x}" for a in self.arguments]
        return " ".join(parts)


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
# The cases: in the first PLAIN_CASES a pointer's buffer holds bytes,
# in the rest POINTER_FILL, whose longs are pointers, so that a routine
# given a record that holds pointers, or a pointer to one, runs on.
PLAIN_CASES = 6
POINTER_CASES = 2
CASES = PLAIN_CASES + POINTER_CASES
# What a pointer case's buffers hold, and POINTEES, the harness's own
# memory, which every long whose high word is "10" lies in, filled
# with it too: a long at an even offset in it is a pointer into
# POINTEES, "1010..." as text, but one in each 16 bytes that is NULL,
# which ends a list, and the next, 0x3130, in ROM.  POINTEES is filled
# at a call's start and compared after it, as RAM is.
POINTER_FILL = b"10" * 6 + b"\0\0\0\0"
POINTEES = 0x31300000
POINTEES_SIZE = 0x10000
FILLED = POINTER_FILL * (POINTEES_SIZE // len(POINTER_FILL))
# Memory beside RAM that a call starts with the same contents in and
# that is compared after it as RAM is: by name, its address and those
# contents.  Neither holds code, so that a jump into either faults.
COMPARED = {"EEPROM": (EEPROM, ERASED), "pointee memory": (POINTEES, FILLED)}
# What a parameter that points at a routine, a function typedef of the
# C's with its parameters declared, is given: a routine of the
# harness's own at ROUTINES, one for each position, ROUTINE_SPACING
# apart, the same in both calls, which records its call as an effect,
# compared as kernel calls are, and returns.  One whose type takes a
# format and a variable list, printf's, formats them as printf does
# (docs/firmware/libc.md): it calls the image's own vsprintf, at
# VSPRINTF, into ROUTINE_TEXT and returns the length, and records the
# text.  Any other records each argument where the image's routines
# read it, as SmartClockSetup.java lays a prototype out: a char or
# short in a 2-byte slot, its value at the slot's start, and a
# char_slot so, its char first and the rest unread (smartclock.h); a
# char pointer's by its text, as a service's string, and the rest by
# their bytes; and returns ROUTINE_RESULT, in D0, as a service does.
ROUTINES = 0x31400000
ROUTINES_SIZE = 0x1000
ROUTINE_SPACING = 0x20
ROUTINE_TEXT = ROUTINES + ROUTINES_SIZE // 2
ROUTINE_RESULT = KERNEL_RESULT
VSPRINTF = 0x608FE
# The routines' code: an rts, and printf's: pea (8,sp), the arguments
# after the format; move.l (8,sp),-(sp), the format; pea ROUTINE_TEXT;
# jsr VSPRINTF; lea (12,sp),sp; and an rts, at which it records.
RETURN_INSTRUCTION = bytes.fromhex("4e75")
FORMATTING = (
    bytes.fromhex("486f00082f2f00084879")
    + struct.pack(">I", ROUTINE_TEXT)
    + bytes.fromhex("4eb9")
    + struct.pack(">I", VSPRINTF)
    + bytes.fromhex("4fef000c")
    + RETURN_INSTRUCTION
)
VARIABLE = "..."
FORMAT = "char *"
CHAR_SLOT = "char_slot"
# A stack argument's offset above the stack pointer at entry, past the
# return address, and the narrowest slot the image's routines read one
# from.
FIRST_ARGUMENT = 4
STACK_SLOT_MINIMUM = 2
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


@dataclass(frozen=True)
class Routine:
    """A function typedef of the C's: its result and parameter types,
    VARIABLE last for a variable list."""

    result: str
    parameters: tuple[str, ...]


class Types:
    """The sizes of the C file's types, which name pointers, and its
    routine types, by name, those whose parameters it declares."""

    def __init__(self, c: str):
        self.pointers = set(re.findall(r"^typedef [^;()]*\*\s*(\w+);$", c, re.M))
        self.sizes = dict(BASE_SIZES)
        for name, size in re.findall(r"^_Static_assert\(sizeof\((\w+)\) == (\d+),", c, re.M):
            self.sizes[name] = int(size)
        self.routines = {
            name: Routine(result, tuple(p.strip().removeprefix("struct ") for p in parameters.split(",")))
            for result, name, parameters in re.findall(r"^typedef ([^;()]+?) ?\((\w+)\)\(([^()]+)\);$", c, re.M)
        }

    def routine(self, kind: str) -> Routine | None:
        """The routine type a pointer type points at; None if it is not one."""
        return self.routines.get(kind.removesuffix("*").strip()) if kind.endswith("*") else None

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
        self.effects: list[KernelCall | DeviceWrite | RoutineCall] = []
        # each C function's two copies, gcc's address and the image's
        self.copies: set[tuple[int, int]] = set()
        # where service 8's next segment starts, and the task records
        # service 3 has returned, by name
        self.next_allocation = ALLOCATIONS
        self.tasks: dict[int, int] = {}
        # RAM zeroed but the kernel's tables and its data area, as
        # KERNEL_INIT leaves them
        self.kernel_ram = bytes(RAM_SIZE)
        # the routines at ROUTINES, by the address each records at
        self.stubs: dict[int, Stub] = {}
        self.uc.mem_map(ROUTINES, ROUTINES_SIZE)
        self.uc.hook_add(UC_HOOK_CODE, self.routine_call, begin=ROUTINES, end=ROUTINES + ROUTINES_SIZE - 1)
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

    def routine_call(self, uc: Uc, address: int, size: int, _) -> None:
        """Records a call of a routine at ROUTINES, at the instruction it
        records at, and returns its result."""
        stub = self.stubs.get(address)
        if stub is None:
            return
        if stub.layout is None:
            self.effects.append(RoutineCall(stub.position, (string(uc, ROUTINE_TEXT),)))
            return
        sp = uc.reg_read(UC_M68K_REG_A7)
        arguments = []
        for offset, length, text in stub.layout:
            value = int.from_bytes(uc.mem_read(sp + offset, length), "big")
            try:
                contents = string(uc, value) if text else None
            except UcError:
                contents = None
            arguments.append(value if contents is None else contents)
        self.effects.append(RoutineCall(stub.position, tuple(arguments)))
        uc.reg_write(UC_M68K_REG_D0, ROUTINE_RESULT)


def rebuilt_machine(image: bytes, linked: list[Section], linked_symbols: dict[str, int]) -> RebuiltMachine:
    """The image, with the rebuilt image's code at REBUILT, a stack, the
    EEPROM and the devices, which of the link's symbols name a C
    function's copies, gcc's and the image's, and the kernel's RAM."""
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
    for start, contents in COMPARED.values():
        m.uc.mem_map(start, len(contents), UC_PROT_READ | UC_PROT_WRITE)
    m.uc.mem_write(RAM, bytes(RAM_SIZE))
    for source, destination, size in KERNEL_TABLES:
        m.uc.mem_write(destination, m.read(source, size))
    m.uc.reg_write(UC_M68K_REG_SR, SUPERVISOR)
    for start, end in KERNEL_INIT:
        m.uc.emu_start(start, end, count=STEP_LIMIT)
    m.kernel_ram = m.read(RAM, RAM_SIZE)
    return m


def stub(m: RebuiltMachine, types: Types, routine: Routine, position: int) -> int | None:
    """The routine at ROUTINES for a parameter's position, of a routine
    type; None if the harness has none for it, a variable list without
    a format or an argument of no known size."""
    start = ROUTINES + position * ROUTINE_SPACING
    for address in range(start, start + ROUTINE_SPACING):
        m.stubs.pop(address, None)
    if routine.parameters[-1] == VARIABLE:
        if routine.parameters[0] != FORMAT:
            return None
        m.uc.mem_write(start, FORMATTING)
        m.stubs[start + len(FORMATTING) - len(RETURN_INSTRUCTION)] = Stub(position, None)
        return start
    layout, offset = [], FIRST_ARGUMENT
    for kind in routine.parameters:
        if kind == "void":
            continue
        size = 1 if kind == CHAR_SLOT else types.size(kind)
        if size is None:
            return None
        layout.append((offset, size, kind == FORMAT))
        slot = max(size, STACK_SLOT_MINIMUM)
        offset += slot + slot % 2
    m.uc.mem_write(start, RETURN_INSTRUCTION)
    m.stubs[start] = Stub(position, tuple(layout))
    return start


def argument(m: RebuiltMachine, types: Types, kind: str, case: int, pointing: bool = False, position: int = 0) -> bytes:
    """The case'th argument of a kind, as its bytes: a routine at
    ROUTINES for a pointer to a routine type, or a pointer to a fresh
    buffer of what it points at, POINTER_FILL where pointing but for
    text and reals."""
    real = REALS[case % len(REALS)]
    routine = types.routine(kind)
    routine_at = None if routine is None else stub(m, types, routine, position)
    if routine_at is not None:
        return struct.pack(">I", routine_at)
    if types.is_pointer(kind):
        pointee = kind.removesuffix("*").strip()
        contents = (
            struct.pack(">d", real) * 4
            if pointee == "double"
            else struct.pack(">f", real) * 8
            if pointee == "float"
            else TEXT
            if pointee == "char"
            else POINTER_FILL * (BUFFER_SPACING // len(POINTER_FILL))
            if pointing
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


def arguments(m: RebuiltMachine, types: Types, kinds: list[str], case: int) -> list[bytes]:
    """A case's arguments, each position taking a different value."""
    pointing = case >= PLAIN_CASES
    return [argument(m, types, kind, case + POSITION_STRIDE * i, pointing, i) for i, kind in enumerate(kinds)]


def gcc_slots(values: list[bytes]) -> bytes:
    """Arguments as gcc pushes them: a char or short promoted to a whole
    int in a 4-byte slot."""
    return b"".join(
        value if len(value) >= 4 else int.from_bytes(value, "big", signed=True).to_bytes(4, "big", signed=True)
        for value in values
    )


def fresh_ram(m: RebuiltMachine) -> None:
    """RAM zeroed but the kernel's tables, its data area as kernel_init
    makes it and the exchanges' ids, and the buffers' allocator: a
    case's starting point."""
    m.next_buffer = BUFFER
    m.uc.mem_write(RAM, m.kernel_ram)
    for cell, exchange in EXCHANGE_IDS.items():
        m.uc.mem_write(cell, struct.pack(">I", exchange))


def reset(m: RebuiltMachine, ram: bytes) -> None:
    m.uc.mem_write(RAM, ram)
    m.next_allocation = ALLOCATIONS
    m.tasks = {RUNNING_TASK: allocate(m, TASK_RECORD_SIZE)}
    data_area = struct.unpack(">I", m.read(PSOS_CONFIG, POINTER_SIZE))[0]
    m.uc.mem_write(data_area, struct.pack(">I", m.tasks[RUNNING_TASK]))
    for start, contents in COMPARED.values():
        m.uc.mem_write(start, contents)
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
            buffer, exchange = (uc.reg_read(REGISTERS[name]) for name in service.fills)
            uc.mem_write(buffer + MESSAGE_OFFSET, RECEIVED_BY_ID.get(exchange, MESSAGE))
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
    registers after, RAM and the COMPARED memory after, its effects,
    the ENDING vector, HALTED or STILL_RUNNING it ended at instead of
    returning, and the fault or trap that stopped it, if one did."""

    def __init__(self, m: RebuiltMachine, start: int, stack_arguments: bytes = b""):
        trapped, invalid, ended = [], [], []
        self.effects = m.effects = []
        kernel_calls = 0

        def trap(uc, number, _):
            nonlocal kernel_calls
            if number in HANDLED:
                take_exception(uc, number)
                return
            if number in ENDING:
                ended.append(number)
                uc.emu_stop()
                return
            if number == RETURN_FROM_EXCEPTION and return_from_exception(uc):
                return
            if number in KERNEL_TRAPS and kernel_calls == KERNEL_CALL_LIMIT:
                ended.append(STILL_RUNNING)
                uc.emu_stop()
                return
            if number in KERNEL_TRAPS:
                call = answer_kernel(m, number)
                if isinstance(call, KernelCall):
                    kernel_calls += 1
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
        pc = m.uc.reg_read(UC_M68K_REG_PC)
        if self.stopped is None and not ended and pc != RETURN:
            if any(m.read(pc, len(branch)) == branch for branch in SELF_BRANCHES):
                ended.append(HALTED)
            else:
                self.stopped = f"no return in {STEP_LIMIT} steps"
        self.ended = ended[0] if ended else None
        self.registers = {name: m.uc.reg_read(register) for name, register in REGISTERS.items()}
        self.ram = m.read(RAM, RAM_SIZE)
        self.compared = {name: m.read(start, len(contents)) for name, (start, contents) in COMPARED.items()}

    def end(self) -> str:
        if self.ended is None:
            return "returned"
        if isinstance(self.ended, str):
            return self.ended
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
    for name, (start, _) in COMPARED.items():
        if a.compared[name] != b.compared[name]:
            first = next(i for i, (x, y) in enumerate(zip(a.compared[name], b.compared[name])) if x != y)
            return f"{name} differs from {start + first:x}"
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
