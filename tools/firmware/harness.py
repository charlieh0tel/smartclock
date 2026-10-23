"""The emulator harness the rebuild's checks share: the image with the
rebuilt image's code loaded beside it in Unicorn (libc_check.Machine),
and calls into either from the same RAM and registers.  A machine runs
one image's profile (Profile): the primary's, or the installer's, whose
RAM starts as the boot code unpacks it and whose own vectors, kernel
and status set-up stand where the primary's do below.

A call starts in supervisor mode with interrupts masked, as the
firmware runs, every register a distinct sentinel but A6, which holds a
caller's frame, and its own stack at STACK, filled and never compared;
RAM zeroed but the kernel's tables, its data area as kernel_init makes
it, with the exchanges root_task creates, what the image's own
INITIALIZERS make, the running task's record and the exchanges' ids; the EEPROM erased and the pointees filled, each
compared after it as RAM is; the devices' registers zeroed but the
status registers the routines poll, ready after a case's busy polls,
the input port's case-varying reads and the EEPROM's write cycle
(DeviceCase); the installer's flash a model of its parts' commands
(Flash), compared after it; a routine of the harness's own for
each parameter that points at one, whose calls are effects; and the
image's own object for a parameter whose callers pass one.  The harness
stands in for what lies outside memory, the same for both calls: traps
#1 and #7 run the image's own handlers, and rtr, which Unicorn lacks,
is done in its place; the kernel's traps, #3 and #4, are answered by a
model of each service, and in a pForth console session the console's
reads by its lines (READ); a trap that never returns, a zero divide, a
write to code, a branch to itself, the KERNEL_CALL_LIMIT'th kernel
call and a machine's counter's count (COUNTED) end a call; a case may
take an interrupt part way, after an event of its own instructions
(Interrupt); and the kernel calls and device reads and writes a call
makes, in order, are its effects, compared as RAM is.
Unicorn has no CPU32; the 68020 stands in, and the image uses none of
the instructions either has alone (PLAN.md, "A rebuild that runs").
"""

import bisect
import re
import struct
import zlib
from dataclasses import dataclass

from unicorn import (
    UC_HOOK_BLOCK,
    UC_HOOK_CODE,
    UC_HOOK_INTR,
    UC_HOOK_MEM_INVALID,
    UC_HOOK_MEM_READ,
    UC_HOOK_MEM_WRITE,
    UC_MEM_WRITE_PROT,
    UC_PROT_EXEC,
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

from decompiled_check import installer_records
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
# The registers the m68k C convention has a callee keep, which the
# original's compiler and gcc both save where a function uses them; a
# sidecar convention's preserves names others a routine keeps too
# (float_raise's A0 and A1, which its movem at 0x5e940 saves for the
# soft-float routines that call it), or, as PRESERVES_ALL, every one.
# A routine of another convention keeps only those its preserves
# names.  An unknown convention is a C function's the decompiler laid
# out.
CALLEE_SAVED = ("D2", "D3", "D4", "D5", "D6", "D7", "A2", "A3", "A4", "A5", "A6")
C_CONVENTIONS = {"c", "unknown"}
PRESERVES_ALL = "all"
# The status register a call starts with: supervisor, interrupts masked;
# and its supervisor bit.
SUPERVISOR = 0x2700
SUPERVISOR_BIT = 0x2000
# The status register's system byte, its trace, supervisor and master
# bits and interrupt mask, which a call leaves its caller, compared
# after it.
SYSTEM_BYTE = 0xFF00
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
# The supervisor stack a call started in user mode takes its exceptions
# on, well below its own stack in the same memory.
SUPERVISOR_STACK = STACK + STACK_SIZE // 2
# link.w An,#d: its opcode, with the register's bits masked, and its
# bytes with the displacement.  Each frame it makes starts as
# CLEAN_STACK, so that a local read before it is written does not read
# what an earlier callee left there, which lies where each copy's
# frames put it.
LINK = 0x4E50
LINK_MASK = 0xFFF8
LINK_INSTRUCTION = struct.Struct(">Hh")
# A6 at a call's start: a caller's frame above the call's stack, its
# saved frame pointer 0, its return address RETURN and its first
# argument a name, CALLER_NAME.  The image's code may walk its caller's
# frame (float_raise reads the return address there, and psos_pack_name,
# 0x16f50, the name its callers, the task and exchange creators, take
# first); gcc's copy, called with the same A6, finds the same frame.
# The name is root_task's
# first exchange's, "gpsx", which it passes psos_create_x (ROOT_EXCHANGES).
FRAME = STACK_TOP
CALLER_NAME = 0x131F7
FRAME_CONTENTS = struct.Struct(">III")
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
# A call whose loop never ends and makes no kernel call, root_task's
# idle loop, ends where the machine's counter, a variable the loop
# counts in, is written with its count, rather than at STEP_LIMIT,
# which the two copies, their code apart, reach at different counts;
# compared up to there, both calls alike.
COUNTED = "counted"
COUNTER = struct.Struct(">I")
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
# rtr, which pops the condition codes and then the return address
# (MC68331UM, table 5-1): QEMU, under Unicorn, takes it for an illegal
# instruction, vector 4, at it, and the harness does it in its place.
# pForth's code words end with it (forth_compile_code, 0x19338).
ILLEGAL_INSTRUCTION = 4
RETURN_AND_RESTORE = bytes.fromhex("4e77")
# move (SP)+,SR, which pops the status register from the supervisor
# stack (M68000PM, MOVE to SR: the source is read, then the status
# register written): QEMU, under Unicorn, increments A7 after the write,
# so that where it clears the supervisor bit, a task's move back after
# trap #7 (set_gps_utc_offset, 0x139c6), the user stack is the one
# incremented; the harness does it in its place, wherever the image's
# code, in ROM or as RAM starts, holds it.
STATUS_POP = bytes.fromhex("46df")
STATUS_SIZE = 2
RESTORED = struct.Struct(">HI")
CONDITION_CODES = 0xFF


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
    ALLOCATIONS; a task it identifies, as (name, pointer) registers,
    returned as its record, a segment of TASK_RECORD_SIZE allocated the
    first time a call names it; a block it reads through a pointer,
    unless null, as a word count and that many words after it, recorded
    by its contents; and whether it returns to its caller in supervisor
    mode, the old status register in D0's low word; and the low byte
    its result registers return beside KERNEL_RESULT's, where a caller
    uses it.  An argument read only in part names its width, D1.b for
    the low byte, and is recorded so."""

    arguments: tuple[str, ...] = ()
    results: tuple[str, ...] = ()
    reads: tuple[str, str | None] | None = None
    fills: tuple[str, str] | None = None
    none_done: str | None = None
    text: tuple[str, str] | None = None
    allocates: tuple[str, str] | None = None
    identifies: tuple[str, str] | None = None
    counted: str | None = None
    supervisor: bool = False
    result_low: int = 0


# A counted block's word count, and each of its words.
COUNT = struct.Struct(">H")
# An argument's width suffix, and the bits of the register it reads.
WIDTHS = {".b": 0xFF, ".w": 0xFFFF}


def service_argument(m: "RebuiltMachine", name: str) -> tuple[str, int]:
    """An argument's name and the bits of its register a service reads,
    as the image's copy would pass it (RebuiltMachine.as_image)."""
    register, width = name[:2], name[2:]
    value = m.uc.reg_read(REGISTERS[register]) & WIDTHS.get(width, 0xFFFFFFFF)
    return name, m.as_image(value)


# The kernel's services, trap #3, by number; and the old priority the
# priority service returns, a task's in the middle of the range.
OLD_PRIORITY = 0x40
KERNEL_SERVICES = {
    0: Service(),
    # create: the name, the time slice and the priority from D1's and
    # D2's low bytes, the two stack sizes in D3's words, and through A0,
    # unless null, a word count and that many words for the new stack
    # (kernel_create, 0x14dac: 0x14e2e, 0x14db0, 0x14dc8, 0x14ddc)
    1: Service(("D0", "D1.b", "D2.b", "D3"), ("A0",), counted="A0"),
    2: Service(("A0", "A1")),
    3: Service(("D0",), ("D0",), identifies=("D0", "A0")),
    4: Service(("A0",)),
    5: Service(("A0",)),
    6: Service(("A0",)),
    # priority: the task and the step, the old priority in D0's low byte
    # (docs/firmware/psos.md, "Calls"), which task_set_priority steps
    # from (0x1339e)
    7: Service(("A0", "D0"), ("D0",), result_low=OLD_PRIORITY),
    8: Service(("D0", "D1", "D2", "D3"), allocates=("D1", "A0")),
    9: Service(("A0",)),
    10: Service(("A0", "A1")),
    **{number: Service() for number in (11, 12, 13, 14, 15, 17, 34)},
    16: Service(("A0", "A1")),
    # broadcast: the exchange, the message through A1, its five longs
    # from +4 as send_x reads them, and the number of copies in D0's low
    # byte; the tasks woken in D1's low byte (kernel_broadcast_x, 0x154ae:
    # 0x154fe, 0x15510)
    18: Service(("A0", "D0.b"), ("D0", "D1"), reads=("A1", None)),
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
    # set the time of day: the date, the time, whose top byte it
    # ignores, and the ticks in D2's low word (kernel_set_time,
    # 0x1599e: 0x159b6, 0x159f4)
    31: Service(("D0", "D1", "D2.w")),
    # mode: the new mode and the mask in D0's and D1's low bytes
    # (kernel_mode, 0x158da: 0x158e2)
    32: Service(("D0.b", "D1.b"), ("D0",)),
    # supervisor: the status register the trap returns to gets its
    # supervisor bit, the old in D0's low word (kernel_supervisor,
    # 0x158f6), which klok_task and spoo_task open with
    33: Service(supervisor=True),
    # create a partition: the area, the block size and the count of
    # blocks in D1's low word (kernel_create_partition, 0x15a10: 0x15a32)
    35: Service(("A0", "D0", "D1.w"), ("A0",)),
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
# A pForth console session (RebuiltMachine.console_input) models the two
# devices the console reads, by major number, as their drivers read:
# DUART channel B, the console's port, gives the session's next line,
# as drtb_read (0x1ddd6) gives what drtb_gets read, a line without its
# CR or LF, at most the length less one, then a NUL, its count in D2
# and its end in A1, with no error; and the console's string device
# gives the string its control function was last given in D2
# (string_device_control, 0x1af8e), as console_read_from_string
# (0x1af98) copies it, up to D2 bytes or its NUL, leaving its end in A1
# and in D2.  Either with nothing left to give, which would leave the
# console waiting (drtb_getc's wait_v, 0x1d74a; the suspend at 0x1afce),
# and the console's task deleting itself (service 4, A0 0, the halt
# hook's last call, console.md "Leaving it"), end the session.
READ = 3
CONTROL = 5
CONSOLE_PORT_DEVICE = 0
CONSOLE_STRING_DEVICE = 2
DELETE = 4
SELF = 0
INPUT_ENDED = "input ended"
TASK_DELETED = "task deleted"
# The console's user area, the bytes below A5 (console_task's link,
# 0x1aef4), and the offset below A5 of its cell holding the data
# stack's base (console.md, "The kernel's registers").
USER_AREA_SIZE = 0x28
USER_STACK_BASE = 8
# The traps the harness answers for the kernel, by vector.  Each takes
# its service or function in D7's low byte and returns its error there.
KERNEL_TRAPS = {TRAP_VECTORS + 3: KERNEL_SERVICES, IO_TRAP: IO_FUNCTIONS}
SERVICE_REGISTER = "D7"
SERVICE_MASK = 0xFF
# What a modeled service returns: no error, and in each result register
# a value no mapped memory lies at, so that one used as an address
# faults.  Every other register it leaves as it was, as the kernel does:
# the image's own code keeps values across the trap in registers the
# binding's convention lists among its results, mutex_acquire its lock
# in A1 across ident (0x10442 to 0x1044a), so that code calling the
# binding in asm must name as changed each register the service
# returns in, and may name more.
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
SAMPLE_INTEGER = 0x334DE
RECEIVED = {
    SPOO_EXCHANGE: struct.pack(">5I", 0, 0x29FCE, 0x19, 0, 0),
    PLLC_EXCHANGE: struct.pack(">IIBBBBII", 0, SAMPLE_INTEGER, 0, 0, 0, 0, 7, 0),
}
FIRST_EXCHANGE_ID = KERNEL_RESULT + 1
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
# What ident returns for a name no task has, where a machine's
# ident_fails has it fail: error 1 and the last record its search
# reached, none (kernel_ident, 0x14eda to 0x14eec).
IDENT_NOT_FOUND = 1
NO_TASK = 0
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
# And the exchanges root_task (0x1304a) creates, gpsx, plla and pllc,
# as (name, flags, queue limit), psos_create_x's arguments
# (docs/firmware/structures.md, "The tasks and exchanges"), each made by
# the kernel's own code for service 20, kernel_create_x (0x1537e), run
# to its rts, KERNEL_CREATE_END, as kernel_dispatch_service (0x14b26)
# calls it: the data area in A6, the name packed as psos_pack_name packs
# it, a character a byte, the first highest, and the flags and the
# limit in D0, D1 and D2.  It takes each from the free list at the data
# area's +0x18 and puts it at the head of the list at +0x14, linked
# through its +0x1c, its queue of waiting tasks empty, pointing at
# itself, which os_report_exchanges (0x1665e) walks.
KERNEL_CREATE = 0x1537E
KERNEL_CREATE_END = 0x153DE
ROOT_EXCHANGES = [(b"gpsx", 0, 0), (b"plla", 0, 0), (b"pllc", 2, 8)]
EXCHANGE_LIST = 0x14
EXCHANGE_NEXT = 0x1C
# The image's own set-up past the kernel's, which RAM starts each case
# with too: routines root_task (0x1304a) calls before it creates the
# tasks, each run once as a call is, its kernel calls answered by the
# model and the segments it allocates kept, from ALLOCATIONS: status_init
# (0x2f480), which builds status_object and its registers, through
# status_create, and sets status_ready (docs/firmware/structures.md, "The
# status system").
INITIALIZERS = [0x2F480]
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


@dataclass(frozen=True)
class ConsoleLine:
    """A line a console session's port gave the pForth console, None at
    the end of its input, and the console's state as it asked for it,
    after the line before: the dictionary pointer, A4, the data stack's
    pointer, A6, its user area and its data stack's cells, from A6 to
    the base its user area holds (empty if A6 is not below the base)."""

    text: bytes | None
    dictionary: int
    stack_pointer: int
    user_area: bytes
    stack: bytes

    def __str__(self) -> str:
        return (
            ("end of console input" if self.text is None else f"console line {self.text!r}")
            + f" after A4={self.dictionary:x} A6={self.stack_pointer:x} "
            f"user area {self.user_area.hex()} stack {self.stack.hex()}"
        )


# The devices' chip selects and the MC68331's own modules, whose
# registers the firmware reads and writes (docs/firmware/README.md and
# restart.md, "The switch byte at 0x302000"), the QSPI's RAM and the
# FPGA's ports behind it among them: each mapped, page by page, as
# memory zeroed at a call's start, so that a read finds zero or what the
# call last wrote there, but for the registers modeled below; and each
# read and each write is a call's effect, in order with its kernel
# calls, so that a routine's polls, how many and in what order, are
# compared, not only what it leaves.
DEVICES = [
    (0x200000, 0x1000),  # the DUART
    (0x300000, 0x10000),  # the latch, the switch byte and the registers at 0x304000 and 0x306000
    (0x500000, 0x1000),
    (0xFFF000, 0x1000),  # the GPT, SIM and QSM, and the chip select at 0xfff800
]


@dataclass(frozen=True)
class Status:
    """A device register a routine polls, which reads as the bits its
    data sheet says the device shows by what it has done, whatever was
    written there: its address and bytes; the bits a read gives as last
    written; the bits the device sets once ready, after the case's busy
    polls of the register (DeviceCase.busy) since the call's start or the
    last write to one of triggers, which starts the device again, and,
    while it is busy, the bits a read gives as written in place of kept;
    and the bits it shows while data waits, for the case's first waiting
    reads of it, with errors beside them where the case has a receive
    error."""

    address: int
    size: int
    kept: int
    ready: int
    triggers: tuple[int, ...]
    busy_kept: int = 0
    waiting: int = 0
    errors: int = 0


# The registers the routines poll, read alike in both calls.
STATUS_REGISTERS = [
    # the DUART's SRA and SRB: TxRDY and TxEMT, which a write of the
    # channel's TB clears until the character has gone, RxRDY while a
    # character waits, and the framing error beside it (MC68681, "Channel
    # A status register (SRA) and channel B status register (SRB)");
    # duartb_putc_polled waits for TxRDY, duartb_isr reads while RxRDY;
    # a write there is to CSRA or CSRB
    Status(0x200003, 1, 0, 0x0C, (0x200007,), waiting=0x01, errors=0x40),
    Status(0x200013, 1, 0, 0x0C, (0x200017,), waiting=0x01, errors=0x40),
    # the SCI's SCSR: TDRE and TC, which a write of SCDR clears, RDRF and
    # the framing error (MC68331UM, D.4.6), which sci_putc_polled waits
    # for
    Status(0xFFFC0C, 2, 0, 0x0180, (0xFFFC0E,), waiting=0x0040, errors=0x0002),
    # the QSPI's SPCR1, whose SPE the QSPI clears at the end of its queue
    # (6.3.5.1), and SPSR, whose SPIF it then sets (D.4.13), each queue
    # started by a write of SPCR1: qspi_send waits for both
    Status(0xFFFC1A, 2, 0x7FFF, 0, (0xFFFC1A,), busy_kept=0xFFFF),
    Status(0xFFFC1F, 1, 0, 0x80, (0xFFFC1A,)),
]
STATUS_AT = {status.address: status for status in STATUS_REGISTERS}
# The DUART's input port, IP (MC68681, "Input port"), which
# duart_input_debounced reads until ten reads agree: each read gives the
# case's next input (DeviceCase.inputs), the last repeated.
INPUT_PORT = 0x20001B


@dataclass(frozen=True)
class DeviceCase:
    """How a case's devices answer, the same for both calls: the polls
    of a status register, the EEPROM's cell being written or a busy
    flash part before it is ready; the reads a status register with data
    waiting shows it for, and whether the data comes with a receive
    error; the input port's successive values; and whether an EEPROM
    write or a flash part's program or erase fails, the cell never
    taking its value and the part showing its time-out (Flash)."""

    busy: int
    waiting: int
    errors: bool
    inputs: tuple[int, ...]
    fails: bool = False


# The cases' devices, by the case: the first as the devices were before
# any was modeled, ready at once with nothing waiting; then busy for a
# poll and more, characters waiting, with and without an error, and an
# input that bounces before it settles, high or low; and failures.
DEVICE_CASES = [
    DeviceCase(0, 0, False, (0,)),
    DeviceCase(1, 1, False, (0xFF,)),
    DeviceCase(3, 0, False, (0, 0xFF, 0, 0)),
    DeviceCase(2, 2, True, (0xFF, 0, 0xFF, 0xFF, 0xFF)),
    DeviceCase(1, 0, False, (0,), fails=True),
    DeviceCase(5, 3, False, (0xFF, 0xFF, 0, 0xFF)),
]


@dataclass(frozen=True)
class DeviceRead:
    """A read of a device: its address and its bytes."""

    address: int
    size: int

    def __str__(self) -> str:
        return f"read of {self.size} bytes at {self.address:x}"


# The EEPROM's write cycle, as the harness models it: the part is not
# identified (docs/firmware/README.md), so a generic byte-write
# EEPROM's, polled by reading the cell back: after a write, the case's
# busy reads of the cell give its value's complement and the next its
# value, or, where the case fails, every read the complement.  Its
# writes are effects, and its reads from its first write on, where the
# routines poll it (eeprom_write_byte, eeprom_write_page).
BYTE_MASK = 0xFF


class Flash:
    """The flash, as the installer writes it (docs/firmware/restart.md,
    "Erase and download"): two pairs of AM29F010s, 128 KB each, at 0 and
    PAIR_SIZE, a pair's parts the high and low bytes of each word, and
    so a part's address the bus's offset in its pair halved.  Writes
    reach it through chip select 6, write only over the boot ROM's range
    (CSBAR6 0x0006, CSOR6 0x7070; docs/firmware/README.md).  Each part
    is a command state machine, as AMD's Am29F010B data sheet gives it
    (third_party/amd/Am29F010B.pdf): two unlock cycles, 0xaa at 0x5555 and 0x55 at
    0x2aaa, then the command at 0x5555: program, 0xa0, the next write
    the byte, which can only clear bits; erase, 0x80, a second unlock
    and then chip erase, 0x10, at 0x5555, or sector erase, 0x30, at a
    sector, more sectors' 0x30 following without unlocks until the first
    read ends the window; and read/reset, 0xf0, alone or after the
    unlocks, which also clears a time-out; any other write puts the part
    back to reading.  While a program or erase runs, each read of the
    part gives its status: DQ7 the complement of the byte's top bit, 0
    for an erase; DQ6 toggling; DQ3 for an erase; and DQ5 once it has
    timed out; after the case's busy reads (DeviceCase.busy) the next
    finds it done and reads the array, or, where the case fails, DQ5
    set from then on until a reset.  Its contents, a part's bytes, are
    compared after a call as RAM is, and its reads and writes are
    effects; a write no part takes, a stray pointer's, ends the call
    (RebuiltMachine.flash_write)."""

    def __init__(self, image: bytes):
        # the image's bytes, and what a call starts with, the image's
        # unless a case gives its own
        self.image = image[ROM : ROM + FLASH_SIZE]
        self.start = self.image
        self.parts = [FlashPart() for _ in range(PAIRS * LANES)]
        self.reset(self.image, DEVICE_CASES[0])

    def reset(self, contents: bytes, case: DeviceCase) -> None:
        """Each part holds its bytes of contents and reads its array."""
        for index, part in enumerate(self.parts):
            pair, lane = divmod(index, LANES)
            part.reset(contents[pair * PAIR_SIZE + lane : (pair + 1) * PAIR_SIZE : LANES], case)

    def contents(self) -> bytes:
        """The flash's bytes as the bus reads its arrays."""
        found = bytearray(FLASH_SIZE)
        for index, part in enumerate(self.parts):
            pair, lane = divmod(index, LANES)
            found[pair * PAIR_SIZE + lane : (pair + 1) * PAIR_SIZE : LANES] = part.array
        return bytes(found)

    @staticmethod
    def locate(address: int) -> tuple[int, int]:
        """The part a bus byte is in and its address in the part."""
        pair, offset = divmod(address - ROM, PAIR_SIZE)
        return pair * LANES + offset % LANES, offset // LANES

    def write(self, address: int, size: int, value: int) -> bool:
        """Gives a write to its parts; whether one of them took it as a
        command's cycle, not a write a reading part ignores."""
        taken = False
        for i in range(size):
            part, at = Flash.locate(address + i)
            taken |= self.parts[part].write(at, value >> 8 * (size - 1 - i) & BYTE_MASK)
        return taken

    def read(self, address: int, size: int) -> bytes:
        return bytes(self.parts[part].read(at) for part, at in map(Flash.locate, range(address, address + size)))

    def arrays(self, address: int, size: int) -> bytes:
        """The arrays' bytes at an address, as memory holds them between
        reads."""
        return bytes(self.parts[part].array[at] for part, at in map(Flash.locate, range(address, address + size)))


# The flash's layout and the parts' commands and status bits (Flash).
FLASH_SIZE = 0x80000
PAIRS = 2
LANES = 2
PAIR_SIZE = FLASH_SIZE // PAIRS
PART_SIZE = PAIR_SIZE // LANES
PART_SECTOR = 0x4000
UNLOCK_FIRST = (0x5555, 0xAA)
UNLOCK_SECOND = (0x2AAA, 0x55)
PROGRAM = 0xA0
ERASE = 0x80
CHIP_ERASE = 0x10
SECTOR_ERASE = 0x30
READ_RESET = 0xF0
DQ7 = 0x80
DQ6 = 0x40
DQ5 = 0x20
DQ3 = 0x08
ERASED_BYTE = 0xFF
# A part's states (FlashPart): reading its array; after one unlock
# cycle, or two, of a command or of an erase's second unlock; a
# program's byte awaited; sector erase's window, open to more sectors;
# a program or erase running; and timed out.
(
    READING,
    UNLOCKED_ONCE,
    UNLOCKED,
    PROGRAMMING_NEXT,
    ERASE_UNLOCKING,
    ERASE_UNLOCKED_ONCE,
    ERASE_UNLOCKED,
    SECTORS_OPEN,
    RUNNING,
    TIMED_OUT,
) = range(10)


class FlashPart:
    """One AM29F010 (Flash): its array; its state; the case's busy
    reads and whether it fails; a running program's byte and address,
    None for an erase, and the sectors an erase clears; the reads it has
    answered with status since it started; and DQ6's toggle."""

    def reset(self, contents: bytes, case: DeviceCase) -> None:
        self.array = bytearray(contents)
        self.state = READING
        self.busy = case.busy
        self.fails = case.fails
        self.programming: tuple[int, int] | None = None
        self.sectors: set[int] = set()
        self.polls = 0
        self.toggle = 0

    def write(self, at: int, data: int) -> bool:
        """A write's cycle; whether the part took it, not a write it
        ignores while reading its array."""
        state = self.state
        if state == RUNNING:
            return True
        if data == READ_RESET and state in (READING, UNLOCKED, TIMED_OUT):
            self.state = READING
            return True
        if state == TIMED_OUT:
            return True
        if state == READING and (at, data) != UNLOCK_FIRST:
            return False
        if state in (READING, ERASE_UNLOCKING) and (at, data) == UNLOCK_FIRST:
            self.state = UNLOCKED_ONCE if state == READING else ERASE_UNLOCKED_ONCE
        elif state in (UNLOCKED_ONCE, ERASE_UNLOCKED_ONCE) and (at, data) == UNLOCK_SECOND:
            self.state = UNLOCKED if state == UNLOCKED_ONCE else ERASE_UNLOCKED
        elif state == UNLOCKED and at == UNLOCK_FIRST[0] and data in (PROGRAM, ERASE):
            self.state = PROGRAMMING_NEXT if data == PROGRAM else ERASE_UNLOCKING
        elif state == PROGRAMMING_NEXT:
            self.start((at, data), set())
        elif state == ERASE_UNLOCKED and at == UNLOCK_FIRST[0] and data == CHIP_ERASE:
            self.start(None, set(range(PART_SIZE // PART_SECTOR)))
        elif state in (ERASE_UNLOCKED, SECTORS_OPEN) and data == SECTOR_ERASE:
            self.sectors.add(at // PART_SECTOR)
            self.state = SECTORS_OPEN
        else:
            self.state = READING
            self.sectors = set()
        return True

    def start(self, programming: tuple[int, int] | None, sectors: set[int]) -> None:
        self.programming = programming
        self.sectors = sectors
        self.polls = 0
        self.state = RUNNING

    def read(self, at: int) -> int:
        if self.state == SECTORS_OPEN:
            self.start(None, self.sectors)
        if self.state not in (RUNNING, TIMED_OUT):
            return self.array[at]
        if self.state == RUNNING:
            self.polls += 1
            if self.polls > self.busy and not self.fails:
                self.finish()
                return self.array[at]
            if self.polls > self.busy:
                self.state = TIMED_OUT
        self.toggle ^= DQ6
        top = ~self.programming[1] & DQ7 if self.programming else DQ3
        return top | self.toggle | (DQ5 if self.state == TIMED_OUT else 0)

    def finish(self) -> None:
        if self.programming:
            at, data = self.programming
            self.array[at] &= data
        for sector in self.sectors:
            self.array[sector * PART_SECTOR : (sector + 1) * PART_SECTOR] = bytes([ERASED_BYTE]) * PART_SECTOR
        self.state = READING
        self.sectors = set()


class Devices:
    """What the modeled devices have done in a call, from its start
    (reset): each status register's reads since the device last
    started, and all its reads; the value last written to each; the
    input port's reads; the EEPROM's cell being written and its value,
    and the reads of it since, and whether the call has written the
    EEPROM; and the case it answers as."""

    def reset(self, case: DeviceCase) -> None:
        self.case = case
        self.since_start = {address: 0 for address in STATUS_AT}
        self.reads = {address: 0 for address in STATUS_AT}
        self.written = {address: 0 for address in STATUS_AT}
        self.inputs = 0
        self.eeprom_cell: tuple[int, int] | None = None
        self.eeprom_polls = 0
        self.eeprom_written = False

    def status_value(self, status: Status) -> int:
        """What a status register reads as now, counting the read."""
        written = self.written[status.address]
        ready = self.since_start[status.address] >= self.case.busy
        waiting = self.reads[status.address] < self.case.waiting
        self.since_start[status.address] += 1
        self.reads[status.address] += 1
        value = written & status.kept | status.ready if ready else written & status.busy_kept
        if waiting:
            value |= status.waiting | (status.errors if self.case.errors else 0)
        return value

    def make_ready(self, address: int) -> None:
        """Has a status register's device finish, as its interrupt says."""
        self.since_start[address] = max(self.since_start[address], self.case.busy)

    def written_to(self, address: int, size: int, value: int) -> None:
        """Notes a device write: a status register's value, or a write
        that starts a device again."""
        for status in STATUS_REGISTERS:
            if address <= status.address < address + size:
                shift = 8 * (address + size - status.address - status.size)
                self.written[status.address] = value >> shift & ((1 << 8 * status.size) - 1)
            if any(address <= trigger < address + size for trigger in status.triggers):
                self.since_start[status.address] = 0

    def next_input(self) -> int:
        inputs = self.case.inputs
        value = inputs[min(self.inputs, len(inputs) - 1)]
        self.inputs += 1
        return value


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
# constants, and an overlay's code in RAM (Profile.code).
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
# in the rest one of POINTER_FILLS each, whose longs are pointers, so
# that a routine given a record that holds pointers, or a pointer to
# one, runs on.
PLAIN_CASES = 6
# What a pointer case's buffers hold, and the pointees, the harness's
# own memory, a POINTEES_SIZE page at each address a long at an even
# offset in a fill points into but for NULL and ROM, filled with the
# case's fill, the first in a plain case.  Each fill is 16 bytes over,
# text and then a NULL, which ends a list, and the long after the NULL,
# 0x3130, is in ROM.  In the first, "1010" over, every long in the text
# is 0x31303130, which holds itself, so that a list linked through any
# of a record's first three longs never ends; in the second,
# "10141018101<", the first three longs point at the next one's place
# in a 16 bytes and the third at the NULL's, so that such a list ends,
# and the longs between, "14" "10" and the like, point into the
# pointees as well.  The pointees are filled at a call's start and
# compared after it, as RAM is.
NULL_LONG = b"\0\0\0\0"
POINTER_FILLS = [b"10" * 6 + NULL_LONG, b"10141018101<" + NULL_LONG]
POINTER_CASES = len(POINTER_FILLS)
CASES = PLAIN_CASES + POINTER_CASES
POINTEES_SIZE = 0x10000
POINTEES = sorted(
    {
        pointer & -POINTEES_SIZE
        for fill in POINTER_FILLS
        for at in range(0, len(fill), POINTER_ALIGNMENT)
        if (pointer := int.from_bytes((fill * 2)[at : at + POINTER_SIZE], "big")) >= ROM + ROM_SIZE
    }
)
# Memory beside RAM that a call starts with the same contents in and
# that is compared after it as RAM is: by name, its address and its
# bytes.  None holds code, so that a jump into one faults.
COMPARED = {
    "EEPROM": (EEPROM, EEPROM_SIZE),
    **{f"pointee memory at {start:x}": (start, POINTEES_SIZE) for start in POINTEES},
}
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
# jsr to the image's vsprintf; lea (12,sp),sp; and an rts, at which it
# records.
RETURN_INSTRUCTION = bytes.fromhex("4e75")
FORMATTING_START = bytes.fromhex("486f00082f2f00084879") + struct.pack(">I", ROUTINE_TEXT) + bytes.fromhex("4eb9")
FORMATTING_END = bytes.fromhex("4fef000c") + RETURN_INSTRUCTION
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
# The objects of the image's own a parameter is given, by the name its
# prototype gives it: an SCPI formatter's output, which
# scpi_format_response's callers pass, and the SCPI lexer's input, the
# console port's object, which scpi_console_port (0x29b82) returns:
# its read routine at +0, which the lexer calls, and its methods from
# the pointer at +4 (docs/firmware/structures.md).
CONSOLE_PORT = 0x29E18
OUTPUT = "output"
INPUT = "input"
# And an SCPI header path, a fresh buffer, as scpi_path_reset (0x27efe)
# sets one back: the long at +0 a list of the tree, the main tree's root
# list, scpi_main_tree (0x53034), which scpi_interpret is given, or, for
# a header's lookup, the case's (header_target), and the word at +4, the
# suffixes it holds, 0 (docs/scpi/README.md); and a child list, the
# case's.
PATH = "path"
CHILD_LIST = "scpi_child_list *"
SCPI_MAIN_TREE = 0x53034
RESET_PATH_SUFFIXES = struct.pack(">H", 0)
RESET_PATH_LIST = 0
# And a keyword token, scpi_keyword: its length at +0 and its text from
# +2 (smartclock.h).  A header's, a lookup's keyword, the case's
# (header_target); a parameter's, a token, by the case one of KEYWORDS:
# of the main tree's root list, short and long, of :SOURce's, which the
# root's default child reaches, and of neither.
KEYWORD = "scpi_keyword *"
HEADER_KEYWORD = "keyword"
KEYWORDS = [b"SYST", b"STATUS", b"PTIM", b"GPSYSTEM", b"DIAG", b"OUTP", b"CALA", b"NONE"]
KEYWORD_LENGTH = struct.Struct(">h")
# And by its type: the status model status_init builds (INITIALIZERS),
# which status_object keeps and every caller passes, one of its
# registers, by the case, from the pointers at its +0x12, as many as
# the word at its +0 counts, and its error queue, from the pointer at
# its +6 (structures.md, "The status system"), which the status model's
# routines pass the queue's, the installer's copies too.
STATUS_MODEL = "status_model *"
STATUS_REGISTER = "status_register *"
ERROR_QUEUE = "status_error_queue *"
STATUS_OBJECT = 0x102EB8
STATUS_REGISTER_COUNT = 0
STATUS_MODEL_QUEUE = 6
STATUS_MODEL_REGISTERS = 0x12
# And the variables whose address the callers of a parameter's type
# pass: aging_curve (0x101fd8), which aging_fit (0x417c6) hands each fit
# routine, or a copy of it on its stack (structures.md, "The aging fit's
# curve"), and status_screen_snapshot (0x10142c), which status_screen
# and its lines are given to print.
VARIABLES = {"aging_curve_state *": 0x101FD8, "status_snapshot_record *": 0x10142C}
# And by its function and position, an exchange on the kernel's list,
# the case'th of ROOT_EXCHANGES, as os_report_exchanges passes each to
# os_report_exchange.
EXCHANGE_PARAMETERS = {"os_report_exchange": 0}
# And a numeric value's step, which validate_num (0x42c10) passes its
# descriptor's validate from the pointer at the descriptor's +0x28, one
# for each value: the first of :SAMPle:INTeger's, the descriptor of the
# request RECEIVED from pllc_exchange.
SCPI_STEP = "scpi_step *"
DESCRIPTOR_STEPS = 0x28
# What scpi_call_handler (0x29642) gives a handler, named for its path
# (SmartClockSetup.java), from the call record scpi_interpret (0x292d6)
# builds on its stack: its parameters, which scpi_header_resolve
# (0x27d80) takes from the arena and starts with the word at +0x22 of
# each node of the header that holds one, 0xffff for none, as
# scpi_header_step (0x27e9e) keeps them in the path, the parsed
# parameters after them; its response; and the call record, with the
# node at +0, the handler's index at +4, 0 for the setter at the node's
# +8 and 1 for the query at its +0x12, 10 bytes each, the arena at +6,
# the parameters at +0xa, the response at +0xe, scpi_interpret's input
# and output at +0x12 and +0x16, its status model at +0x1a, and the main
# tree's root list and the common commands' at +0x22 and +0x26, as
# scpi_task (0x2a04a) passes them.  A node's keyword pair is at its +0,
# its child list at +4 and its response's items at +0x1c; a child list's
# count is at +2, its nodes' pointers from +6 (docs/scpi/README.md).  An
# arena, as arena_init (0x29770) makes one of a buffer, holds its next
# free byte at +0, from +8, and its last at +4.  The parsed parameters
# are the handler's in order, from its entry's list at +4, each linked
# at +0 to the next and its descriptor at +4 (smartclock.h,
# scpi_parameter), as scpi_parameter_match (0x278f4) lays them: a kind
# word, the index of the descriptor's converter at +0xc that took it,
# then the value, as many bytes as the descriptor's +0 says.  A choice's
# value, a word, an index into the keywords of the choice list at the
# descriptor's +8, to a NULL, is in range, by the case, as the parser
# gives it (scpi_parse_choice, 0x26460): every handler's choices, of
# which :DIAGnostic:TEST?'s picks its test; so too, by HANDLER_VALUES, a
# value a handler's switch takes, the installer's :DIAGnostic:TEST?'s
# test, installer_test's five (smartclock.h).  The installer's handlers
# get their records as the primary's do, from its tree.
HANDLER = re.compile(r"(?:installer_)?scpi_(query|set)_(\w+)$")
HANDLER_ENTRIES = 8
HANDLER_ENTRY_SIZE = 10
ENTRY_PARAMETERS = 4
PARAMETER_NEXT = 0
PARAMETER_KINDS = 4
KINDS_SIZE = 0
KINDS_CHOICES = 8
KINDS_CONVERTERS = 0xC
CONVERTERS = 6
WORD = struct.Struct(">H")
HANDLER_VALUES = {"installer_scpi_query_DIAGnostic_TEST": 5}
HANDLER_NAMES = ["parameters", "response", "call"]
QUERY = "query"
COMMON_PREFIX = "common_"
SCPI_COMMON_LIST = 0x526F6
KEYWORD_SEPARATOR = "_"
NODE_CHILDREN = 4
NODE_ITEMS = 0x1C
NODE_ID = 0x22
NO_ID = 0xFFFF
LIST_COUNT = 2
LIST_NODES = 6
CALL_NODE = 0
CALL_INDEX = 4
CALL_ARENA = 6
CALL_PARAMETERS = 0xA
CALL_RESPONSE = 0xE
CALL_INPUT = 0x12
CALL_OUTPUT = 0x16
CALL_STATUS = 0x1A
CALL_MAIN_TREE = 0x22
CALL_COMMON_LIST = 0x26
ARENA_START = 8
# The parameters of the parser's own routines named for what
# scpi_interpret gives them: a call record, a fresh one for SAMPLE_CALL,
# the query of :STATus:OPERation:HARDware:ENABle?; the response's items,
# that node's (scpi_format_response); the record's response slot, where
# scpi_call_handler leaves the response it takes; and the parser's
# context, as scpi_context_init (0x26a80) fills it: the input at +0,
# a word 0 at +4, the path at +6, as scpi_path_reset sets it, the call record
# at +0xa, the arena at +0xe, and the main tree and common list at +0x12
# and +0x16.
CALL = "call"
ITEMS = "items"
RESPONSE_SLOT = "response"
CONTEXT = "context"
SAMPLE_CALL = (True, "STATus_OPERation_HARDware_ENABle")
# The installer's (docs/scpi/README.md, "As C"): its console port,
# installer_console_port (installer::0x1076d0), its main tree's root
# list, installer_scpi_list_main (installer::0x108606), and its common
# commands', installer_scpi_list_common (installer::0x1085d8), as
# installer_scpi_trees (installer::0x108930) holds them; keywords of
# its root list, short and long, of :SYSTem's, and of neither, the root
# having no default child; and the query of :DIAGnostic:ERASe?, a short.
INSTALLER_CONSOLE_PORT = 0x1076D0
INSTALLER_SCPI_MAIN_TREE = 0x108606
INSTALLER_SCPI_COMMON_LIST = 0x1085D8
INSTALLER_KEYWORDS = (b"SYST", b"DIAGNOSTIC", b"ERR", b"NONE")
INSTALLER_SAMPLE_CALL = (True, "DIAGnostic_ERASe")


@dataclass(frozen=True)
class ScpiObjects:
    """The SCPI parser's objects a profile's routines are given: its
    console port, its main tree's root list, its common commands' list,
    the keywords a keyword token holds, by the case, and the path of the
    node whose query a fresh call record is for."""

    console_port: int
    main_tree: int
    common_list: int
    keywords: tuple[bytes, ...]
    sample_call: tuple[bool, str]


PRIMARY_SCPI = ScpiObjects(CONSOLE_PORT, SCPI_MAIN_TREE, SCPI_COMMON_LIST, tuple(KEYWORDS), SAMPLE_CALL)
INSTALLER_SCPI = ScpiObjects(
    INSTALLER_CONSOLE_PORT,
    INSTALLER_SCPI_MAIN_TREE,
    INSTALLER_SCPI_COMMON_LIST,
    INSTALLER_KEYWORDS,
    INSTALLER_SAMPLE_CALL,
)
CONTEXT_INPUT = 0
CONTEXT_WORD = 4
CONTEXT_WORD_SIZE = 2
CONTEXT_PATH = 6
CONTEXT_CALL = 0xA
CONTEXT_ARENA = 0xE
CONTEXT_MAIN_TREE = 0x12
CONTEXT_COMMON_LIST = 0x16
# The sizes of the base types the decompiled C's prototypes use; the
# rest are pointers or are asserted in the C.
BASE_SIZES = {
    "char": 1, "byte": 1, "uchar": 1, "undefined1": 1, "uint8_t": 1, "int8_t": 1, "bool": 1,
    "short": 2, "ushort": 2, "undefined2": 2, "uint16_t": 2, "int16_t": 2,
    "int": 4, "uint": 4, "long": 4, "ulong": 4, "undefined4": 4, "uint32_t": 4, "int32_t": 4,
    "float": 4,
    "double": 8, "undefined8": 8, "longlong": 8, "ulonglong": 8, "uint64_t": 8, "int64_t": 8,
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


@dataclass(frozen=True)
class Profile:
    """An image's own code and state, which a call runs on and starts
    with: the primary's, in ROM, or an overlay's, which the boot code
    unpacks into RAM.  overlay is its name in the sidecar's addresses,
    None for the primary; ram what RAM holds before its kernel's set-up;
    code the overlay's image in RAM, as (start, bytes), which a check
    keeps read-only and where its literals lie, as the ROM is; vectors
    its vector table; kernel_tables to initializers its kernel's tables,
    data area and set-up, as KERNEL_TABLES, PSOS_CONFIG, KERNEL_INIT,
    KERNEL_CREATE and KERNEL_CREATE_END, ROOT_EXCHANGES and INITIALIZERS
    are the primary's; received the messages its exchanges give, as
    RECEIVED; string_devices as STRING_DEVICES; caller_name the name in
    a call's caller's frame, as CALLER_NAME; vsprintf its own, as
    VSPRINTF; buffers its arguments' buffers, as (start, bytes), in RAM
    or apart from it; status_object where its status set-up keeps the
    status model, as STATUS_OBJECT; objects whether its parameters
    are given the primary's other objects, VARIABLES to
    EXCHANGE_PARAMETERS and an SCPI handler's; and scpi the SCPI
    parser's objects its parameters are given, its own tree's, as
    PRIMARY_SCPI and INSTALLER_SCPI."""

    overlay: str | None
    ram: bytes
    code: tuple[int, int] | None
    vectors: int
    kernel_tables: tuple[tuple[int, int, int], ...]
    psos_config: int
    kernel_init: tuple[tuple[int, int], ...]
    kernel_create: tuple[int, int]
    root_exchanges: tuple[tuple[bytes, int, int], ...]
    initializers: tuple[int, ...]
    received: dict[int, bytes]
    string_devices: frozenset[int]
    caller_name: int
    vsprintf: int
    buffers: tuple[int, int]
    status_object: int
    objects: bool
    scpi: ScpiObjects

    def exchange_ids(self) -> dict[int, int]:
        """A distinct id for each cell of received, as its creates leave one."""
        return {cell: FIRST_EXCHANGE_ID + i for i, cell in enumerate(self.received)}

    def buffers_apart(self) -> bool:
        """Whether the arguments' buffers lie outside RAM, in memory of
        their own."""
        return not RAM <= self.buffers[0] < RAM + RAM_SIZE


# The sidecar writes an overlay's address as its name, OVERLAY_SEPARATOR
# and the address.
OVERLAY_SEPARATOR = "::"
PRIMARY = Profile(
    overlay=None,
    ram=bytes(RAM_SIZE),
    code=None,
    vectors=VECTORS,
    kernel_tables=tuple(KERNEL_TABLES),
    psos_config=PSOS_CONFIG,
    kernel_init=tuple(KERNEL_INIT),
    kernel_create=(KERNEL_CREATE, KERNEL_CREATE_END),
    root_exchanges=tuple(ROOT_EXCHANGES),
    initializers=tuple(INITIALIZERS),
    received=RECEIVED,
    string_devices=frozenset(STRING_DEVICES),
    caller_name=CALLER_NAME,
    vsprintf=VSPRINTF,
    buffers=(BUFFER, ALLOCATIONS - BUFFER),
    status_object=STATUS_OBJECT,
    objects=True,
    scpi=PRIMARY_SCPI,
)
# The installer (docs/firmware/restart.md, "Forced installer entry with
# an unusable primary"): the entry at 0xa58 unpacks its records into RAM
# (boot_unpack_installer, 0xaa0), copies the boot vector table, ROM's
# first VECTOR_TABLE_SIZE bytes, to RAM's start, makes that the vector
# base and jumps to installer_entry (0x10135c).  Its vectors take trap #1
# to installer_trap_1 (0x108466) and trap #7 to installer_trap_7
# (0x100f32).  installer_psos_config_init (0x1083b6) copies its
# configuration and I/O table from 0x1084da and 0x108578 to 0x10ab84 and
# 0x10ab60: a data area from 0x10b0c8 and one device, DUART channel B,
# whose write, 0x10380a, is drtb_write's code (docs/firmware/structures.md,
# "The installer's RAM").  installer_kernel_init (0x1026de) and
# installer_kernel_create_x (0x101d84) are kernel_init's and
# kernel_create_x's bytes, 0xeca06 higher, so that they run to the same
# places.  The installer's root task (0x100d40) creates no exchange
# itself and sets up the status system with 0x107a00, which passes
# INSTALLER_STATUS_OBJECT's address to status_create's copy (0x105d98),
# before it starts its spool (0x103efe), which creates "spoo"
# (0x104088), passing the name to ident, psos_create_x and
# psos_create_t, and keeps its id at 0x10afbe; the root task then sends the spool the SCPI task's creator,
# 0x107886, and priority 0x19 (0x100d20), as scpi_task_start does.  Its
# arguments' buffers lie outside RAM, which its code fills, at
# INSTALLER_BUFFERS, compared as RAM is.
INSTALLER = "installer"
VECTOR_TABLE_SIZE = 0x400
INSTALLER_PSOS_CONFIG = 0x10AB84
INSTALLER_SPOO_EXCHANGE = 0x10AFBE
INSTALLER_STATUS_OBJECT = 0x10AD06
INSTALLER_BUFFERS = 0x900000


def installer_profile(image: bytes) -> Profile:
    """The installer's profile, its RAM as the boot code leaves it."""
    ram = bytearray(image[ROM : ROM + VECTOR_TABLE_SIZE].ljust(RAM_SIZE, b"\0"))
    records = installer_records(image)
    for destination, data in records:
        ram[destination - RAM : destination - RAM + len(data)] = data
    start = min(destination for destination, _ in records)
    end = max(destination + len(data) for destination, data in records)
    return Profile(
        overlay=INSTALLER,
        ram=bytes(ram),
        code=(start, end - start),
        vectors=RAM,
        kernel_tables=((0x1084DA, INSTALLER_PSOS_CONFIG, 0x62), (0x108578, 0x10AB60, 0x24)),
        psos_config=INSTALLER_PSOS_CONFIG,
        kernel_init=((0x1026DE, 0x102840), (0x102862, 0x102870)),
        kernel_create=(0x101D84, 0x101DE4),
        root_exchanges=(),
        initializers=(0x107A00,),
        received={INSTALLER_SPOO_EXCHANGE: struct.pack(">5I", 0, 0x107886, 0x19, 0, 0)},
        string_devices=frozenset({0}),
        caller_name=0x104088,
        vsprintf=0x10AAA6,
        buffers=(INSTALLER_BUFFERS, PRIMARY.buffers[1]),
        status_object=INSTALLER_STATUS_OBJECT,
        objects=False,
        scpi=INSTALLER_SCPI,
    )


def overlay(convention: dict) -> str | None:
    """The overlay a sidecar row's routine is in; None for the primary."""
    address = convention["address"]
    return address.split(OVERLAY_SEPARATOR)[0] if OVERLAY_SEPARATOR in address else None


class Twins:
    """The objects gcc's C defines that have a twin in the image: a table
    the hand-written C keeps in place of the image's, and every record of
    the generated SCPI tree (docs/firmware/README.md, "Hand-written C";
    docs/scpi/README.md, "As C").  A pointer into a C object at an offset
    is the same as one into its twin at that offset, in a call's result,
    in the memory compared after it and in its effects; and where the
    harness gives a call one of the image's objects, gcc's copy is given
    its twin.  A pointer to an object's end is to neither."""

    def __init__(self, objects: list[tuple[int, int, int]] | None = None):
        # each object as (its start, its size, its twin's start), sorted,
        # once by the C's address and once by the image's, and their
        # starts, which a lookup bisects
        self.by_c = sorted(objects or [])
        self.by_image = sorted((image, size, c) for c, size, image in self.by_c)
        self.c_starts = [start for start, _, _ in self.by_c]
        self.image_starts = [start for start, _, _ in self.by_image]

    @staticmethod
    def find(objects: list[tuple[int, int, int]], starts: list[int], at: int) -> int | None:
        """Where an address in one of the objects lies in its twin; None
        if it is in none."""
        i = bisect.bisect_right(starts, at) - 1
        if i < 0:
            return None
        start, size, twin = objects[i]
        return twin + at - start if at < start + size else None

    def image_address(self, at: int) -> int | None:
        """The image's address for one in a C object; None if none."""
        return Twins.find(self.by_c, self.c_starts, at)

    def c_address(self, at: int) -> int | None:
        """The C's address for one in the image's twin; None if none."""
        return Twins.find(self.by_image, self.image_starts, at)


def image_labels(paths: list[str]) -> dict[str, str]:
    """Each label row's name and address, as the names files write it,
    OVERLAY_SEPARATOR and all; none for a name given two."""
    labels, ambiguous = {}, set()
    for path in paths:
        for line in open(path):
            fields = line.rstrip("\n").split("\t")
            if line.startswith("#") or len(fields) < 3 or fields[1] != "label":
                continue
            if labels.setdefault(fields[2], fields[0]) != fields[0]:
                ambiguous.add(fields[2])
    return {name: address for name, address in labels.items() if name not in ambiguous}


def twins(objects: dict[str, tuple[int, int]], labels: dict[str, str], profile: "Profile") -> Twins:
    """The Twins of a profile: each object gcc's link puts among its
    constants, by name its (address, size), that the names files label
    where the profile's literals lie, in ROM or its overlay's code."""
    found = []
    for name, (at, size) in objects.items():
        label = labels.get(name)
        if label is None or not REBUILT <= at < REBUILT_DATA or not size:
            continue
        space, _, address = label.rpartition(OVERLAY_SEPARATOR)
        image_at = int(address, 16)
        literals = [(ROM, ROM_SIZE)] + ([profile.code] if profile.code else [])
        if (space or None) == profile.overlay and any(start <= image_at < start + n for start, n in literals):
            found.append((at, size, image_at))
    return Twins(found)


class RebuiltMachine(Machine):
    """The image with the rebuilt image's code, run as a profile has it,
    and the effects the call running has had on what lies outside
    memory, in order: kernel calls and device reads and writes."""

    def __init__(self, image: bytes, profile: Profile):
        super().__init__(image)
        self.profile = profile
        # where literals lie, and whether the code there is kept read-only
        self.literals = LITERALS + ([profile.code] if profile.code else [])
        self.protected = False
        self.effects: list[KernelCall | DeviceRead | DeviceWrite | RoutineCall | ConsoleLine] = []
        # in a pForth console session, the lines its port has yet to
        # give, else None, and the string its string device gives
        # (READ)
        self.console_input: list[bytes] | None = None
        self.string_cursor = 0
        # each C function's two copies, gcc's address and the image's
        self.copies: set[tuple[int, int]] = set()
        # and the image's copy by gcc's, which a kernel call records in
        # its place, a task's entry that psos_start passes
        self.image_copies: dict[int, int] = {}
        # the C's objects with a twin in the image; and the cells of the
        # memory a call starts with where the harness gave it one of the
        # image's objects that has one, by address, holding the image's,
        # where gcc's copy is given its twin (give)
        self.twins = Twins()
        self.given: dict[int, int] = {}
        # where a call's first segment of service 8 starts, past those
        # INITIALIZERS keep, and its next; and the task records service 3
        # has returned, by name
        self.first_allocation = ALLOCATIONS
        self.next_allocation = ALLOCATIONS
        self.tasks: dict[int, int] = {}
        # whether ident finds no task of any name but the running
        # task's, so that a creator that asks first goes on to create
        # one; and the counter whose write of its count ends a call, as
        # (address, count), COUNTED
        self.ident_fails = False
        # the status register a call starts with: SUPERVISOR, or a
        # task's in user mode, whose exceptions take SUPERVISOR_STACK
        self.starting_status = SUPERVISOR
        # what each frame a link makes starts as in place of CLEAN_STACK,
        # if not None: a pattern repeated down from the frame pointer, so
        # that a local the image reads unset holds it, wherever each
        # copy puts the local
        self.frame_fill: bytes | None = None
        self.counter: tuple[int, int] | None = None
        # the interrupts a call takes, each with its watch's address
        # (Interrupt); and whether its arguments end at the stack's edge
        self.interrupts: list[tuple[Interrupt, int]] = []
        self.stack_edge = False
        # what each page of the pointees holds at a call's start
        self.pointees = b""
        # RAM as the profile has it but the kernel's tables, its data area
        # and what its initializers make, as its kernel_init and they
        # leave them
        self.initialized_ram = profile.ram
        # the routines at ROUTINES, by the address each records at
        self.stubs: dict[int, Stub] = {}
        # the child lists of the profile's main SCPI tree, as the image
        # holds them (tree_lists)
        self.tree: list[TreeList] = []
        self.uc.mem_map(ROUTINES, ROUTINES_SIZE)
        self.uc.hook_add(UC_HOOK_CODE, self.routine_call, begin=ROUTINES, end=ROUTINES + ROUTINES_SIZE - 1)
        for base, contents in ((ROM, image), (RAM, profile.ram)):
            for at in range(0, len(contents) - 1, len(STATUS_POP)):
                if contents[at : at + len(STATUS_POP)] == STATUS_POP:
                    self.uc.hook_add(UC_HOOK_CODE, self.status_pop, begin=base + at, end=base + at)
        # the devices' state in the running call, and the flash, which
        # only the installer writes (Flash), None for the primary
        self.devices = Devices()
        self.devices.reset(DEVICE_CASES[0])
        self.device_case = DEVICE_CASES[0]
        self.flash = Flash(image) if profile.overlay == INSTALLER else None
        # where the call's writes and status reads have left other than
        # the arrays in memory, and whether it has written the flash
        self.flash_written: list[tuple[int, int]] = []
        self.flash_touched = False
        # whether the call has written the flash where no part takes it
        self.stray_write = False
        for start, size in DEVICES:
            self.uc.mem_map(start, size)
            self.uc.hook_add(UC_HOOK_MEM_WRITE, self.device_write, begin=start, end=start + size - 1)
            self.uc.hook_add(UC_HOOK_MEM_READ, self.device_read, begin=start, end=start + size - 1)
        if self.flash is not None:
            self.uc.hook_add(UC_HOOK_MEM_WRITE, self.flash_write, begin=ROM, end=ROM + FLASH_SIZE - 1)
            self.uc.hook_add(UC_HOOK_MEM_READ, self.flash_read, begin=ROM, end=ROM + FLASH_SIZE - 1)
            self.uc.hook_add(UC_HOOK_BLOCK, self.flash_restore, begin=ROM, end=ROM + FLASH_SIZE - 1)
        self.uc.hook_add(UC_HOOK_MEM_WRITE, self.eeprom_write, begin=EEPROM, end=EEPROM + EEPROM_SIZE - 1)
        self.uc.hook_add(UC_HOOK_MEM_READ, self.eeprom_read, begin=EEPROM, end=EEPROM + EEPROM_SIZE - 1)

    def as_image(self, value: int) -> int:
        """A value as the image's copy would have it: a C function's gcc
        copy as its image copy, and a pointer into a C object with a twin
        as one into the twin; any other as it is."""
        if value in self.image_copies:
            return self.image_copies[value]
        twin = self.twins.image_address(value)
        return value if twin is None else twin

    def device_read(self, uc: Uc, access: int, address: int, size: int, value: int, _) -> None:
        """Records a device read and, before it, sets each modeled
        register it covers to what it reads as."""
        self.effects.append(DeviceRead(address, size))
        for at in range(address, address + size):
            status = STATUS_AT.get(at)
            if status is not None:
                uc.mem_write(at, self.devices.status_value(status).to_bytes(status.size, "big"))
            elif at == INPUT_PORT:
                uc.mem_write(at, bytes([self.devices.next_input()]))

    def device_write(self, uc: Uc, access: int, address: int, size: int, value: int, _) -> None:
        value &= (1 << 8 * size) - 1
        self.effects.append(DeviceWrite(address, size, value))
        self.devices.written_to(address, size, value)

    def eeprom_write(self, uc: Uc, access: int, address: int, size: int, value: int, _) -> None:
        """Records an EEPROM write and starts the write cycle of its last
        byte, finishing the cell written before it."""
        value &= (1 << 8 * size) - 1
        self.effects.append(DeviceWrite(address, size, value))
        devices = self.devices
        if devices.eeprom_cell is not None:
            uc.mem_write(devices.eeprom_cell[0], bytes([devices.eeprom_cell[1]]))
        devices.eeprom_cell = (address + size - 1, value & BYTE_MASK)
        devices.eeprom_polls = 0
        devices.eeprom_written = True

    def eeprom_read(self, uc: Uc, access: int, address: int, size: int, value: int, _) -> None:
        """Records an EEPROM read from the call's first write on, and gives
        the cell being written its complement until its cycle is done."""
        devices = self.devices
        if not devices.eeprom_written:
            return
        self.effects.append(DeviceRead(address, size))
        cell = devices.eeprom_cell
        if cell is None or not address <= cell[0] < address + size:
            return
        devices.eeprom_polls += 1
        done = devices.eeprom_polls > devices.case.busy and not devices.case.fails
        uc.mem_write(cell[0], bytes([cell[1] if done else ~cell[1] & BYTE_MASK]))
        if done:
            devices.eeprom_cell = None

    def flash_write(self, uc: Uc, access: int, address: int, size: int, value: int, _) -> None:
        """Records a write to the flash and gives it to its parts.  The
        write lands in memory after the hook; the arrays are put back
        there before the next read of the flash or block of code run from
        it (flash_restore)."""
        value &= (1 << 8 * size) - 1
        self.effects.append(DeviceWrite(address, size, value))
        self.flash_written.append((address, size))
        self.flash_touched = True
        if not self.flash.write(address, size, value):
            # a write the parts ignore, through a stray pointer: the call
            # ends there, as one to the primary's ROM does (BUS_ERROR)
            self.stray_write = True
            uc.emu_stop()

    def flash_restore(self, uc: Uc, *_) -> None:
        """Puts the arrays back where writes landed in memory."""
        for address, size in self.flash_written:
            uc.mem_write(address, self.flash.arrays(address, size))
        self.flash_written = []

    def flash_read(self, uc: Uc, access: int, address: int, size: int, value: int, _) -> None:
        """Sets what a read of the flash reads as, before it, and records
        it from the call's first write of the flash on, where the
        installer polls it."""
        self.flash_restore(uc)
        if self.flash_touched:
            self.effects.append(DeviceRead(address, size))
        uc.mem_write(address, self.flash.read(address, size))
        self.flash_written.append((address, size))

    def status_pop(self, uc: Uc, address: int, size: int, _) -> None:
        """Does a move (SP)+,SR, STATUS_POP, in supervisor mode, as the
        processor does; in user mode it is left to raise its privilege
        violation."""
        sr = uc.reg_read(UC_M68K_REG_SR)
        if not sr & SUPERVISOR_BIT:
            return
        sp = uc.reg_read(UC_M68K_REG_A7)
        popped = int.from_bytes(uc.mem_read(sp, STATUS_SIZE), "big")
        uc.reg_write(UC_M68K_REG_A7, sp + STATUS_SIZE)
        uc.reg_write(UC_M68K_REG_SR, popped)
        uc.reg_write(UC_M68K_REG_PC, address + len(STATUS_POP))

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
            arguments.append(self.as_image(value) if contents is None else contents)
        self.effects.append(RoutineCall(stub.position, tuple(arguments)))
        uc.reg_write(UC_M68K_REG_D0, ROUTINE_RESULT)


def rebuilt_machine(
    image: bytes,
    linked: list[Section],
    linked_symbols: dict[str, int],
    profile: Profile = PRIMARY,
    objects: Twins | None = None,
) -> RebuiltMachine:
    """The image, with the rebuilt image's code at REBUILT, a stack, the
    EEPROM and the devices, which of the link's symbols name a C
    function's copies, gcc's and the image's, the C's objects with a
    twin in the image (twins), and the profile's kernel's RAM."""
    m = RebuiltMachine(image, profile)
    m.twins = objects or Twins()
    m.copies = {
        (address, linked_symbols[IMAGE_PREFIX + name])
        for name, address in linked_symbols.items()
        if IMAGE_PREFIX + name in linked_symbols
    }
    m.image_copies = dict(m.copies)
    m.uc.mem_map(REBUILT, REBUILT_SIZE)
    for section in linked:
        if section.name in LOADED:
            m.uc.mem_write(section.address, section.data)
    m.uc.mem_map(STACK, STACK_SIZE)
    for start, size in COMPARED.values():
        m.uc.mem_map(start, size, UC_PROT_READ | UC_PROT_WRITE)
    if profile.buffers_apart():
        m.uc.mem_map(*profile.buffers, UC_PROT_READ | UC_PROT_WRITE)
    m.uc.mem_write(RAM, profile.ram)
    for source, destination, size in profile.kernel_tables:
        m.uc.mem_write(destination, m.read(source, size))
    m.uc.reg_write(UC_M68K_REG_SR, SUPERVISOR)
    for start, end in profile.kernel_init:
        m.uc.emu_start(start, end, count=STEP_LIMIT)
    for name, flags, limit in profile.root_exchanges:
        m.uc.reg_write(UC_M68K_REG_A6, data_area(m))
        for register, value in zip(("D0", "D1", "D2"), (int.from_bytes(name, "big"), flags, limit)):
            m.uc.reg_write(REGISTERS[register], value)
        m.uc.emu_start(*profile.kernel_create, count=STEP_LIMIT)
    m.initialized_ram = m.read(RAM, RAM_SIZE)
    for routine in profile.initializers:
        fresh_ram(m)
        reset(m, snapshot(m))
        run = Run(m, routine)
        if run.stopped is not None or run.ended is not None:
            raise RuntimeError(f"initializer {routine:x}: {run.stopped or run.end()}")
        m.first_allocation = m.next_allocation
        m.initialized_ram = run.memory[RAM]
    fresh_ram(m)
    m.tree = tree_lists(m, profile.scpi.main_tree)
    return m


def protect_code(m: RebuiltMachine) -> None:
    """Keeps the image's code read-only, ROM's, gcc's and the overlay's,
    so that a stray write to it faults, rather than changing later
    cases; but for the installer's, whose writes to ROM's range are to
    the flash (Flash), reset for each call."""
    if m.flash is None:
        m.uc.mem_protect(ROM, ROM_SIZE, UC_PROT_READ | UC_PROT_EXEC)
    m.uc.mem_protect(REBUILT, REBUILT_DATA - REBUILT, UC_PROT_READ | UC_PROT_EXEC)
    m.protected = True


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
        formatting = FORMATTING_START + struct.pack(">I", m.profile.vsprintf) + FORMATTING_END
        m.uc.mem_write(start, formatting)
        m.stubs[start + len(formatting) - len(RETURN_INSTRUCTION)] = Stub(position, None)
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


def argument(
    m: RebuiltMachine, types: Types, kind: str, case: int, fill: bytes | None = None, position: int = 0
) -> bytes:
    """The case'th argument of a kind, as its bytes: a routine at
    ROUTINES for a pointer to a routine type, or a pointer to a fresh
    buffer of what it points at, a pointer case's fill but for text and
    reals."""
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
            else fill * (BUFFER_SPACING // len(fill))
            if fill
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


def arguments(
    m: RebuiltMachine,
    types: Types,
    kinds: list[str],
    case: int,
    names: list[str] | None = None,
    function: str | None = None,
    target: "HeaderTarget | None" = None,
) -> list[bytes]:
    """A case's arguments, each position taking a different value, or,
    where the profile gives them, the image's object for its name or
    type, a header's lookup target's, the case's unless given; an SCPI
    handler's as scpi_call_handler gives them."""
    fill = pointer_fill(case) if case >= PLAIN_CASES else None
    objects = m.profile.objects
    target = target or header_target(m, function or "", case)
    values = []
    for i, (kind, name) in enumerate(zip(kinds, names or [None] * len(kinds))):
        at = (
            exchange(m, case)
            if objects and EXCHANGE_PARAMETERS.get(function) == i
            else image_object(m, kind, name, case + POSITION_STRIDE * i, target)
        )
        values.append(
            argument(m, types, kind, case + POSITION_STRIDE * i, fill, i) if at is None else struct.pack(">I", at)
        )
    handler = HANDLER.match(function or "")
    if handler and names == HANDLER_NAMES:
        parameters, response, call = (struct.unpack(">I", value)[0] for value in values)
        query = handler.group(1) == QUERY
        ids = call_record(m, query, handler.group(2), parameters, response, call)
        if ids is not None:
            parsed_parameters(m, query, call, parameters + WORD.size * ids, HANDLER_VALUES.get(function), case)
    elif names and (CALL in names or CONTEXT in names):
        record = sample_call(m)
        node = pointer_at(m, record + CALL_NODE)
        given = {
            CALL: record,
            ITEMS: pointer_at(m, node + NODE_ITEMS),
            RESPONSE_SLOT: record + CALL_RESPONSE,
            CONTEXT: parser_context(m, record),
        }
        values = [struct.pack(">I", given[name]) if name in given else value for name, value in zip(names, values)]
    return values


def call_record(m: RebuiltMachine, query: bool, path: str, parameters: int, response: int, record: int) -> int | None:
    """Makes record, a buffer, what scpi_interpret's call record holds for
    a handler's path, and parameters what scpi_header_resolve starts
    them with; how many words those are, or None, leaving them, for a
    path not in the tree."""
    scpi = m.profile.scpi
    common = path.startswith(COMMON_PREFIX)
    nodes = header_nodes(m, scpi.common_list if common else scpi.main_tree, path.removeprefix(COMMON_PREFIX))
    if not nodes:
        return None
    ids = [i for i in (struct.unpack(">H", m.read(node + NODE_ID, 2))[0] for node in nodes) if i != NO_ID]
    m.uc.mem_write(parameters, struct.pack(f">{len(ids)}H", *ids))
    for offset, value in (
        (CALL_NODE, nodes[-1]),
        (CALL_ARENA, arena(m)),
        (CALL_PARAMETERS, parameters),
        (CALL_RESPONSE, response),
        (CALL_INPUT, scpi.console_port),
        (CALL_OUTPUT, scpi.console_port),
        (CALL_STATUS, pointer_at(m, m.profile.status_object)),
        (CALL_MAIN_TREE, scpi.main_tree),
        (CALL_COMMON_LIST, scpi.common_list),
    ):
        give(m, record + offset, value)
    m.uc.mem_write(record + CALL_INDEX, struct.pack(">H", int(query)))
    return len(ids)


def parsed_parameters(m: RebuiltMachine, query: bool, record: int, at: int, values: int | None, case: int) -> None:
    """Gives a call record's handler, its parameters from at, its
    choices, and its values where values counts them, the case'th in
    range."""
    entry = pointer_at(m, record + CALL_NODE) + HANDLER_ENTRIES + HANDLER_ENTRY_SIZE * query
    parameter = pointer_at(m, entry + ENTRY_PARAMETERS)
    while parameter:
        kinds = pointer_at(m, parameter + PARAMETER_KINDS)
        size = struct.unpack(">h", m.read(kinds + KINDS_SIZE, WORD.size))[0]
        choices = pointer_at(m, kinds + KINDS_CHOICES)
        converters = struct.unpack(f">{CONVERTERS}I", m.read(kinds + KINDS_CONVERTERS, CONVERTERS * POINTER_SIZE))
        kind = next((i for i, converter in enumerate(converters) if converter), 0)
        count = choice_count(m, choices) if choices else values
        if count:
            m.uc.mem_write(at, WORD.pack(kind) + WORD.pack(case % count))
        at += WORD.size + size
        parameter = pointer_at(m, parameter + PARAMETER_NEXT)


def choice_count(m: Machine, choices: int) -> int:
    """How many keywords a choice list has, to its NULL."""
    keywords, count = pointer_at(m, choices), 0
    while pointer_at(m, keywords + POINTER_SIZE * count):
        count += 1
    return count


def sample_call(m: RebuiltMachine) -> int:
    """A fresh call record for the profile's sample call, as SAMPLE_CALL,
    with fresh parameters and response."""
    record = m.buffer()
    if call_record(m, *m.profile.scpi.sample_call, m.buffer(), m.buffer(), record) is None:
        raise RuntimeError(f"no node for {m.profile.scpi.sample_call}")
    return record


def arena(m: RebuiltMachine) -> int:
    """An arena of a fresh buffer, as arena_init makes it."""
    at = m.buffer()
    m.uc.mem_write(at, struct.pack(">II", at + ARENA_START, at + BUFFER_SPACING - 1))
    return at


def parser_context(m: RebuiltMachine, record: int) -> int:
    """A fresh parser context, as scpi_interpret has scpi_context_init fill it."""
    context = m.buffer()
    scpi = m.profile.scpi
    for offset, value in (
        (CONTEXT_INPUT, scpi.console_port),
        (CONTEXT_PATH, reset_path(m)),
        (CONTEXT_CALL, record),
        (CONTEXT_ARENA, pointer_at(m, record + CALL_ARENA)),
        (CONTEXT_MAIN_TREE, scpi.main_tree),
        (CONTEXT_COMMON_LIST, scpi.common_list),
    ):
        give(m, context + offset, value)
    m.uc.mem_write(context + CONTEXT_WORD, bytes(CONTEXT_WORD_SIZE))
    return context


def reset_path(m: RebuiltMachine, at: int | None = None) -> int:
    """A fresh header path, as scpi_path_reset sets one back, to the main
    tree's root list or another."""
    at = m.profile.scpi.main_tree if at is None else at
    path = m.buffer(struct.pack(">I", at) + RESET_PATH_SUFFIXES)
    give(m, path + RESET_PATH_LIST, at)
    return path


def give(m: RebuiltMachine, at: int, pointer: int) -> None:
    """Gives a call a pointer at a cell of the memory it starts with,
    noting the cell if what it points at has a twin among the C's
    objects, for give_twins."""
    m.uc.mem_write(at, struct.pack(">I", pointer))
    if m.twins.c_address(pointer) is not None:
        m.given[at] = pointer


def give_twins(m: RebuiltMachine) -> None:
    """Gives gcc's copy, in place of each image object the harness gave
    a call, its twin among the C's objects."""
    for at, pointer in m.given.items():
        m.uc.mem_write(at, struct.pack(">I", m.twins.c_address(pointer)))


def twin_arguments(m: RebuiltMachine, types: Types, kinds: list[str], values: list[bytes]) -> list[bytes]:
    """A call's arguments as gcc's copy is given them: a pointer to an
    image object that has a twin among the C's objects, the twin."""
    given = []
    for kind, value in zip(kinds, values):
        twin = m.twins.c_address(struct.unpack(">I", value)[0]) if types.is_pointer(kind) else None
        given.append(value if twin is None else struct.pack(">I", twin))
    return given


def header_nodes(m: Machine, root: int, path: str) -> list[int]:
    """The nodes a header's keywords, separated by KEYWORD_SEPARATOR,
    name from a child list down, each matched whole against its keyword
    pair's two forms joined; none if one is not found."""
    nodes, children = [], root
    for keyword in path.split(KEYWORD_SEPARATOR):
        if children == 0:
            return []
        count = struct.unpack(">H", m.read(children + LIST_COUNT, 2))[0]
        pointers = struct.unpack(f">{count}I", m.read(children + LIST_NODES, count * POINTER_SIZE))
        found = [node for node in pointers if keyword_text(m, node).upper() == keyword.upper()]
        if not found:
            return []
        nodes.append(found[0])
        children = pointer_at(m, found[0] + NODE_CHILDREN)
    return nodes


def keyword_text(m: Machine, node: int) -> str:
    """A node's keyword, its short form and the rest of its long form."""
    short = m.string(pointer_at(m, node))
    rest = m.string(pointer_at(m, node) + len(short) + 1)
    return (short + rest).decode("ascii", "replace")


@dataclass(frozen=True)
class TreeList:
    """A child list of a profile's SCPI tree, as the image holds it: its
    address, the nodes of the shortest header that reaches it from the
    root list, the last the node whose list it is, none for the root
    list's own; its nodes; and the index of its default child, NO_DEFAULT
    for none (smartclock.h, scpi_child_list)."""

    address: int
    path: tuple[int, ...]
    nodes: tuple[int, ...]
    default: int


NO_DEFAULT = -1


def tree_lists(m: Machine, root: int) -> list[TreeList]:
    """Every child list the tree from a root list reaches, each once,
    the shallowest first, each header's lists in their nodes' order."""
    found: dict[int, TreeList] = {}
    pending = [(root, ())]
    while pending:
        at, path = pending.pop(0)
        if at in found:
            continue
        count, default = struct.unpack(">hh", m.read(at + LIST_COUNT, 4))
        nodes = struct.unpack(f">{count}I", m.read(at + LIST_NODES, count * POINTER_SIZE))
        found[at] = TreeList(at, path, nodes, default)
        pending += [(children, path + (node,)) for node in nodes if (children := pointer_at(m, node + NODE_CHILDREN))]
    return list(found.values())


def keyword_forms(m: Machine, node: int) -> tuple[bytes, bytes]:
    """A node's keyword, short and long, in upper case, as a header
    gives either."""
    text = keyword_text(m, node).upper().encode("ascii")
    return text[: len(m.string(pointer_at(m, node)))], text


def header_text(m: Machine, nodes: tuple[int, ...], long: bool) -> bytes:
    """An absolute header naming nodes, each by its short or long form."""
    return b"".join(b":" + keyword_forms(m, node)[long] for node in nodes)


# A keyword token's choice among a tree's lists, by the case, for a
# lookup in one (header_target): a node of the list's, short or long; a
# node of the list of its default child's, which a lookup reaches
# through the default; and a keyword of none.  Each case looks in a list
# of the tree, drawn by the function and the case, below the root as
# much as at it, so that a child list the C's tree points at wrongly
# shows.
SHORT_FORM, LONG_FORM, THROUGH_DEFAULT, NO_NODE = range(4)
HEADER_VARIANTS = 4
NO_KEYWORD = b"NONE"


@dataclass(frozen=True)
class HeaderTarget:
    """A lookup a case gives: the list, and the keyword looked up in it."""

    list: int
    keyword: bytes


def tree_steps(m: RebuiltMachine) -> list[HeaderTarget]:
    """A step down to each node's children, of every node of the
    profile's tree that has them: its list and its keyword, short and
    long in turn, so that a case of each, of a routine that steps into
    a node's children (scpi_header_step), reads every node's child
    list pointer."""
    return [
        HeaderTarget(tree.address, keyword_forms(m, node)[i % 2 == 1])
        for i, (tree, node) in enumerate(
            (tree, node) for tree in m.tree for node in tree.nodes if pointer_at(m, node + NODE_CHILDREN)
        )
    ]


def header_target(m: RebuiltMachine, function: str, case: int) -> HeaderTarget:
    """The list and keyword a function's case looks up, drawn from the
    profile's tree (tree_lists) by the function's name and the case."""
    lists = m.tree
    seed = zlib.crc32(function.encode())
    variant = case % HEADER_VARIANTS
    through = [
        tree
        for tree in lists
        if tree.default != NO_DEFAULT and pointer_at(m, tree.nodes[tree.default] + NODE_CHILDREN)
    ]
    if variant == THROUGH_DEFAULT and through:
        chosen = through[(seed + case) % len(through)]
        children = next(t for t in lists if t.address == pointer_at(m, chosen.nodes[chosen.default] + NODE_CHILDREN))
        node = children.nodes[(seed + case) % len(children.nodes)]
        return HeaderTarget(chosen.address, keyword_forms(m, node)[True])
    chosen = lists[(seed + case) % len(lists)]
    if variant in (SHORT_FORM, LONG_FORM):
        node = chosen.nodes[(seed // len(lists) + case) % len(chosen.nodes)]
        return HeaderTarget(chosen.address, keyword_forms(m, node)[variant == LONG_FORM])
    return HeaderTarget(chosen.address, NO_KEYWORD)


def image_object(m: RebuiltMachine, kind: str, name: str | None, case: int, target: HeaderTarget) -> int | None:
    """The image's object a parameter of a name and type is given, the
    case'th of those it may be, a header's lookup's the case's target;
    None if it is given none.  Every profile gives its status model's
    and its SCPI parser's; only one with objects the rest."""
    scpi = m.profile.scpi
    if name in (OUTPUT, INPUT):
        return scpi.console_port
    if name == PATH:
        return reset_path(m, target.list)
    if kind == CHILD_LIST:
        return target.list
    if kind == KEYWORD:
        text = target.keyword if name == HEADER_KEYWORD else scpi.keywords[case % len(scpi.keywords)]
        return m.buffer(KEYWORD_LENGTH.pack(len(text)) + text + b"\0")
    if m.profile.objects:
        if kind in VARIABLES:
            return VARIABLES[kind]
        if kind == SCPI_STEP:
            return pointer_at(m, SAMPLE_INTEGER + DESCRIPTOR_STEPS)
    if kind not in (STATUS_MODEL, STATUS_REGISTER, ERROR_QUEUE):
        return None
    status = pointer_at(m, m.profile.status_object)
    if kind == STATUS_MODEL:
        return status
    if kind == ERROR_QUEUE:
        return pointer_at(m, status + STATUS_MODEL_QUEUE)
    count = struct.unpack(">h", m.read(status + STATUS_REGISTER_COUNT, 2))[0]
    return pointer_at(m, status + STATUS_MODEL_REGISTERS + case % count * POINTER_SIZE)


def pointer_at(m: Machine, address: int) -> int:
    return struct.unpack(">I", m.read(address, POINTER_SIZE))[0]


def data_area(m: RebuiltMachine) -> int:
    """The kernel's data area, the configuration's first long."""
    return pointer_at(m, m.profile.psos_config)


def exchange(m: RebuiltMachine, case: int) -> int:
    """The case'th of the exchanges on the kernel's list, from its head."""
    exchanges = []
    at = pointer_at(m, data_area(m) + EXCHANGE_LIST)
    while at:
        exchanges.append(at)
        at = pointer_at(m, at + EXCHANGE_NEXT)
    return exchanges[case % len(exchanges)]


def gcc_slots(values: list[bytes]) -> bytes:
    """Arguments as gcc pushes them: a char or short promoted to a whole
    int in a 4-byte slot."""
    return b"".join(
        value if len(value) >= 4 else int.from_bytes(value, "big", signed=True).to_bytes(4, "big", signed=True)
        for value in values
    )


def pointer_fill(case: int) -> bytes:
    """The fill of a case's pointees, and of its buffers in a pointer
    case."""
    return POINTER_FILLS[max(case - PLAIN_CASES, 0)]


def fresh_ram(m: RebuiltMachine, case: int = 0) -> None:
    """RAM as the profile has it but what its kernel's tables,
    kernel_init and initializers make, and the exchanges' ids; the
    buffers' allocator, their memory zeroed where it lies apart, and the
    pointees' fill: a case's starting point."""
    start, size = m.profile.buffers
    m.next_buffer = start
    m.given = {}
    m.device_case = DEVICE_CASES[case % len(DEVICE_CASES)]
    if m.profile.buffers_apart():
        m.uc.mem_write(start, bytes(size))
    m.pointees = pointer_fill(case) * (POINTEES_SIZE // len(pointer_fill(case)))
    m.uc.mem_write(RAM, m.initialized_ram)
    for cell, exchange in m.profile.exchange_ids().items():
        m.uc.mem_write(cell, struct.pack(">I", exchange))


def memory_compared(m: RebuiltMachine) -> list[tuple[int, int]]:
    """The memory a call compares as RAM, pointers to the same literal or
    function the same: RAM and, where they lie apart, the arguments'
    buffers, as (start, bytes)."""
    return [(RAM, RAM_SIZE)] + ([m.profile.buffers] if m.profile.buffers_apart() else [])


def snapshot(m: RebuiltMachine) -> dict[int, bytes]:
    """What the memory_compared holds, by its start, for reset."""
    return {start: m.read(start, size) for start, size in memory_compared(m)}


def reset(m: RebuiltMachine, memory: dict[int, bytes]) -> None:
    for start, contents in memory.items():
        m.uc.mem_write(start, contents)
    m.next_allocation = m.first_allocation
    m.tasks = {RUNNING_TASK: allocate(m, TASK_RECORD_SIZE)}
    m.uc.mem_write(data_area(m), struct.pack(">I", m.tasks[RUNNING_TASK]))
    m.uc.mem_write(EEPROM, ERASED)
    for start in POINTEES:
        m.uc.mem_write(start, m.pointees)
    m.uc.mem_write(STACK, CLEAN_STACK)
    for start, size in DEVICES:
        m.uc.mem_write(start, bytes(size))
    m.devices.reset(m.device_case)
    m.string_cursor = 0
    if m.flash is not None:
        m.flash.reset(m.flash.start, m.device_case)
        m.uc.mem_write(ROM, m.flash.start)
        m.flash_written = []
        m.flash_touched = False
        m.stray_write = False
    m.uc.reg_write(UC_M68K_REG_SR, SUPERVISOR)
    for name, register in REGISTERS.items():
        m.uc.reg_write(register, STARTING[name])


def take_exception(uc: Uc, vectors: int, vector: int) -> None:
    """Enters a vector's handler as the processor does: a format 0 frame
    on the supervisor stack, which setting the supervisor bit makes A7
    from a task's user stack, returning past the trap, and the handler's
    address from a vector table.  Unicorn calls the interrupt hook in
    place of doing this, with PC at the trap."""
    sr = uc.reg_read(UC_M68K_REG_SR)
    frame = struct.pack(FRAME_FORMAT, sr, uc.reg_read(UC_M68K_REG_PC) + TRAP_SIZE, vector * VECTOR_SIZE)
    uc.reg_write(UC_M68K_REG_SR, sr | SUPERVISOR_BIT)
    sp = uc.reg_read(UC_M68K_REG_A7) - len(frame)
    uc.mem_write(sp, frame)
    uc.reg_write(UC_M68K_REG_A7, sp)
    uc.reg_write(UC_M68K_REG_PC, struct.unpack(">I", uc.mem_read(vectors + vector * VECTOR_SIZE, VECTOR_SIZE))[0])


@dataclass(frozen=True)
class Interrupt:
    """An interrupt a case takes part way through its call, in both
    copies at the same point of each one's own instructions: after the
    first'th read, or write, of a variable or register, its watch, an
    lvalue of the exported C or an address, by the routine itself, not
    by a handler, and every every'th after (0 for once), at most times
    in all, each taken before the next instruction once the interrupt
    mask lets its level in.  The device's vector and level, as the
    firmware sets them up (startup.h, boot.c); the status registers it
    makes ready (Devices.make_ready); and the registers it presents to
    its handler, as each firing's (address, bytes, value), the last
    repeated."""

    vector: int
    level: int
    watch: str | int
    first: int
    write: bool = False
    every: int = 0
    times: int = 1
    ready: tuple[int, ...] = ()
    presents: tuple[tuple[tuple[int, int, int], ...], ...] = ()


# The status register's interrupt mask, bits 10 to 8, and its trace
# bits; level 7, which the mask never keeps out (MC68331UM, 5.8.2,
# "Interrupts"); and trap #5, the kernel's return from interrupt, which
# the firmware's handlers end with (docs/firmware/psos.md).
MASK_SHIFT = 8
LEVEL_MASK = 7
TRACE_BITS = 0xC000
NONMASKABLE = 7
RETURN_FROM_INTERRUPT = TRAP_VECTORS + 5
# Where A6 points in a case at the stack's edge
# (RebuiltMachine.stack_edge), at no mapped memory, so that a read of
# the caller's frame faults: the value a modeled service returns.
UNMAPPED_FRAME = KERNEL_RESULT


def take_interrupt(uc: Uc, vectors: int, interrupt: Interrupt, pc: int) -> int:
    """Enters an interrupt's handler as the CPU32 does (MC68331UM, 5.8.2
    and "Exception Stack Frames"): the status register copied, then
    supervisor mode, trace off and the mask at the interrupt's level; a
    four-word frame of format 0, the status register, the address of the
    next instruction and the vector's offset, on the supervisor stack;
    and the handler's address from the vector table at the vector base.
    Returns the frame's address."""
    sr = uc.reg_read(UC_M68K_REG_SR)
    frame = struct.pack(FRAME_FORMAT, sr, pc, interrupt.vector * VECTOR_SIZE)
    mask = LEVEL_MASK << MASK_SHIFT
    uc.reg_write(UC_M68K_REG_SR, (sr | SUPERVISOR_BIT) & ~TRACE_BITS & ~mask | interrupt.level << MASK_SHIFT)
    sp = uc.reg_read(UC_M68K_REG_A7) - len(frame)
    uc.mem_write(sp, frame)
    uc.reg_write(UC_M68K_REG_A7, sp)
    handler = struct.unpack(">I", uc.mem_read(vectors + interrupt.vector * VECTOR_SIZE, VECTOR_SIZE))[0]
    uc.reg_write(UC_M68K_REG_PC, handler)
    return sp


def return_and_restore(uc: Uc) -> bool:
    """Does an rtr at PC as the processor does; False, nothing done, if
    the instruction there is another."""
    pc = uc.reg_read(UC_M68K_REG_PC)
    if bytes(uc.mem_read(pc, len(RETURN_AND_RESTORE))) != RETURN_AND_RESTORE:
        return False
    sp = uc.reg_read(UC_M68K_REG_A7)
    ccr, address = RESTORED.unpack(uc.mem_read(sp, RESTORED.size))
    uc.reg_write(UC_M68K_REG_A7, sp + RESTORED.size)
    uc.reg_write(UC_M68K_REG_SR, uc.reg_read(UC_M68K_REG_SR) & ~CONDITION_CODES | ccr & CONDITION_CODES)
    uc.reg_write(UC_M68K_REG_PC, address)
    return True


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


def console_read(m: RebuiltMachine, vector: int) -> int | None:
    """In a console session, the device a trap reads that the session
    models (READ), else None."""
    if m.console_input is None or vector != IO_TRAP:
        return None
    uc = m.uc
    device = uc.reg_read(UC_M68K_REG_D0) >> DEVICE_SHIFT & DEVICE_MASK
    if uc.reg_read(REGISTERS[SERVICE_REGISTER]) & SERVICE_MASK != READ:
        return None
    return device if device in (CONSOLE_PORT_DEVICE, CONSOLE_STRING_DEVICE) else None


def console_state(m: RebuiltMachine, text: bytes | None) -> ConsoleLine:
    """The console's state as it reads its port, and the line given."""
    uc = m.uc
    a5, a6 = uc.reg_read(UC_M68K_REG_A5), uc.reg_read(UC_M68K_REG_A6)
    user = m.read(a5 - USER_AREA_SIZE, USER_AREA_SIZE)
    base = pointer_at(m, a5 - USER_STACK_BASE)
    stack = m.read(a6, base - a6) if a6 < base and base - a6 <= LONGEST_BUFFER else b""
    return ConsoleLine(text, uc.reg_read(UC_M68K_REG_A4), a6, user, stack)


def console_session_ended(m: RebuiltMachine, vector: int) -> bool:
    """Whether a trap ends a console session: a read of a device with
    nothing left to give (READ)."""
    device = console_read(m, vector)
    if device == CONSOLE_PORT_DEVICE:
        return not m.console_input
    return device == CONSOLE_STRING_DEVICE and not (m.string_cursor and m.read(m.string_cursor, 1) != b"\0")


def answer_console_read(m: RebuiltMachine, device: int) -> ConsoleLine | KernelCall:
    """Answers a console session's read of a device as READ models it,
    returning past the trap."""
    uc = m.uc
    buffer, length = uc.reg_read(UC_M68K_REG_A1), uc.reg_read(REGISTERS["D2"])
    if device == CONSOLE_PORT_DEVICE:
        line = m.console_input.pop(0)
        given, rest = line[: length - 1], line[length - 1 :]
        if rest:
            m.console_input.insert(0, rest)
        uc.mem_write(buffer, given + b"\0")
        end, count = buffer + len(given), len(given)
        call = console_state(m, given)
    else:
        text = m.read(m.string_cursor, length)
        given = text[: text.index(0) + 1] if 0 in text else text
        m.string_cursor += len(given)
        uc.mem_write(buffer, given)
        end = count = buffer + len(given)
        call = KernelCall(IO_TRAP, READ, (("D0", uc.reg_read(UC_M68K_REG_D0)), ("D2", length)), given)
    uc.reg_write(UC_M68K_REG_A1, end)
    uc.reg_write(REGISTERS["D2"], count)
    uc.reg_write(REGISTERS[SERVICE_REGISTER], NO_ERROR)
    uc.reg_write(UC_M68K_REG_PC, uc.reg_read(UC_M68K_REG_PC) + TRAP_SIZE)
    return call


def answer_kernel(m: RebuiltMachine, vector: int) -> KernelCall | ConsoleLine | str:
    """Answers a kernel trap as KERNEL_TRAPS models it, or a console
    session's read as READ does, returning past the trap; or, nothing
    done, why it cannot: a service it does not model, a buffer it cannot
    read or fill, or a segment too large."""
    uc = m.uc
    read = console_read(m, vector)
    if read is not None:
        return answer_console_read(m, read)
    number = uc.reg_read(REGISTERS[SERVICE_REGISTER]) & SERVICE_MASK
    service = KERNEL_TRAPS[vector].get(number)
    device = uc.reg_read(UC_M68K_REG_D0) >> DEVICE_SHIFT & DEVICE_MASK
    if m.console_input is not None and vector == IO_TRAP and number == CONTROL and device == CONSOLE_STRING_DEVICE:
        m.string_cursor = uc.reg_read(REGISTERS["D2"])
    if vector == IO_TRAP and number == WRITE and device in m.profile.string_devices:
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
            if length is None:
                # a message's longs, a routine spoo_run sends among them
                longs = struct.unpack(f">{MESSAGE_LONGS}I", data)
                data = struct.pack(f">{MESSAGE_LONGS}I", *(m.as_image(v) for v in longs))
        if service.fills:
            buffer, exchange = (uc.reg_read(REGISTERS[name]) for name in service.fills)
            ids = m.profile.exchange_ids()
            received = {ids[cell]: message for cell, message in m.profile.received.items()}
            uc.mem_write(buffer + MESSAGE_OFFSET, received.get(exchange, MESSAGE))
        if service.text:
            data = string(uc, uc.reg_read(REGISTERS[service.text[0]]))
            if data is None:
                return out_of_reach
        if service.counted:
            block = uc.reg_read(REGISTERS[service.counted])
            if block:
                (count,) = COUNT.unpack(uc.mem_read(block, COUNT.size))
                data = bytes(uc.mem_read(block, COUNT.size * (count + 1)))
    except UcError:
        return out_of_reach
    too_large = f"service {number}, its segment out of reach"
    if service.allocates:
        size, pointer = service.allocates
        segment = allocate(m, uc.reg_read(REGISTERS[size]))
        if segment is None:
            return too_large
        uc.reg_write(REGISTERS[pointer], segment)
    error = NO_ERROR
    if service.identifies:
        name, pointer = service.identifies
        task = uc.reg_read(REGISTERS[name])
        if m.ident_fails and task != RUNNING_TASK:
            error = IDENT_NOT_FOUND
            record = NO_TASK
        else:
            if task not in m.tasks:
                m.tasks[task] = allocate(m, TASK_RECORD_SIZE)
            record = m.tasks[task]
            if record is None:
                return too_large
        uc.reg_write(REGISTERS[pointer], record)
    call = KernelCall(vector, number, tuple(service_argument(m, name) for name in service.arguments), data)
    for name in service.results:
        uc.reg_write(REGISTERS[name], KERNEL_RESULT | service.result_low)
    if service.none_done:
        uc.reg_write(REGISTERS[service.none_done], 0)
    if service.text:
        pointer, count = service.text
        uc.reg_write(REGISTERS[pointer], uc.reg_read(REGISTERS[pointer]) + len(data))
        uc.reg_write(REGISTERS[count], len(data))
    uc.reg_write(REGISTERS[SERVICE_REGISTER], error)
    if service.supervisor:
        sr = uc.reg_read(UC_M68K_REG_SR)
        uc.reg_write(UC_M68K_REG_D0, uc.reg_read(UC_M68K_REG_D0) & ~WIDTHS[".w"] | sr)
        # A7 becomes the supervisor stack, as the trap's rte leaves it
        uc.reg_write(UC_M68K_REG_SR, sr | SUPERVISOR_BIT)
    uc.reg_write(UC_M68K_REG_PC, uc.reg_read(UC_M68K_REG_PC) + TRAP_SIZE)
    return call


class Run:
    """One call: its stack arguments laid above the return address, at
    the stack's edge, or, for a task's entry, at SP alone, its registers
    and status register after, the memory_compared, the COMPARED memory
    and the flash after, its effects, the ENDING vector, HALTED,
    STILL_RUNNING, COUNTED or a console session's INPUT_ENDED or
    TASK_DELETED it ended at instead of returning, and the fault or trap
    that stopped it, if one did.  The machine's interrupts are taken as
    their events come (Interrupt), and the kernel's return from
    interrupt, trap #5, returns from one taken."""

    def __init__(self, m: RebuiltMachine, start: int, stack_arguments: bytes = b"", entry: bool = False):
        trapped, invalid, ended = [], [], []
        # writes to an overlay's code, which Unicorn protects only by
        # whole pages: each one's address and the bytes it overwrote
        written: list[tuple[int, bytes]] = []
        self.effects = m.effects = []
        kernel_calls = 0

        # the interrupts the case takes (Interrupt): each one's events
        # and firings so far; those due and not yet taken, by index; and
        # the frame of each taken and not yet returned from, innermost
        # last
        interrupts = m.interrupts
        events = [0] * len(interrupts)
        fired = [0] * len(interrupts)
        pending: list[int] = []
        frames: list[int] = []

        def watched(uc, access, address, size, value, index):
            if frames:
                return
            interrupt = interrupts[index][0]
            events[index] += 1
            after = events[index] - interrupt.first
            if fired[index] + len([i for i in pending if i == index]) < interrupt.times and (
                after == 0 or after > 0 and interrupt.every and after % interrupt.every == 0
            ):
                pending.append(index)

        def step(uc, address, size, _):
            # before each instruction, the first interrupt due, if the
            # mask lets it in
            if not pending:
                return
            index = pending[0]
            interrupt = interrupts[index][0]
            sr = uc.reg_read(UC_M68K_REG_SR)
            if interrupt.level != NONMASKABLE and interrupt.level <= sr >> MASK_SHIFT & LEVEL_MASK:
                return
            pending.pop(0)
            presented = interrupt.presents[min(fired[index], len(interrupt.presents) - 1)] if interrupt.presents else ()
            fired[index] += 1
            for at, length, shown in presented:
                uc.mem_write(at, shown.to_bytes(length, "big"))
            for at in interrupt.ready:
                m.devices.make_ready(at)
            frames.append(take_interrupt(uc, m.profile.vectors, interrupt, address))

        def trap(uc, number, _):
            nonlocal kernel_calls
            if frames and number == RETURN_FROM_INTERRUPT:
                # the kernel's return from interrupt, as it returns when
                # the interrupt readied no task to switch to: the rte
                return_from_exception(uc)
                frames.pop()
                return
            if frames and number == RETURN_FROM_EXCEPTION and uc.reg_read(UC_M68K_REG_A7) == frames[-1]:
                frames.pop()
            if number in HANDLED:
                take_exception(uc, m.profile.vectors, number)
                return
            if number in ENDING:
                ended.append(number)
                uc.emu_stop()
                return
            if number == RETURN_FROM_EXCEPTION and return_from_exception(uc):
                return
            if number == ILLEGAL_INSTRUCTION and return_and_restore(uc):
                return
            if number in KERNEL_TRAPS and kernel_calls == KERNEL_CALL_LIMIT:
                ended.append(STILL_RUNNING)
                uc.emu_stop()
                return
            if number in KERNEL_TRAPS and console_session_ended(m, number):
                if console_read(m, number) == CONSOLE_PORT_DEVICE:
                    m.effects.append(console_state(m, None))
                ended.append(INPUT_ENDED)
                uc.emu_stop()
                return
            if number in KERNEL_TRAPS:
                call = answer_kernel(m, number)
                if isinstance(call, (KernelCall, ConsoleLine)):
                    kernel_calls += 1
                    m.effects.append(call)
                    if m.console_input is not None and call == KernelCall(number, DELETE, (("A0", SELF),)):
                        ended.append(TASK_DELETED)
                        uc.emu_stop()
                    return
                trapped.append(f"{number}, {call}")
            else:
                trapped.append(number)
            uc.emu_stop()

        def fault(uc, access, address, size, value, _):
            if access == UC_MEM_WRITE_PROT and literal_region(m, uc.reg_read(UC_M68K_REG_PC)) is not None:
                ended.append(BUS_ERROR)
            invalid.append(address)
            return False

        def code_write(uc, access, address, size, value, _):
            # the write is made after the hook, and undone after the call
            written.append((address, bytes(uc.mem_read(address, size))))
            if literal_region(m, uc.reg_read(UC_M68K_REG_PC)) is not None:
                ended.append(BUS_ERROR)
            else:
                invalid.append(address)
            uc.emu_stop()

        def counted(uc, access, address, size, value, _):
            # before the write; made here too, in case it is not after
            counter, count = m.counter
            if address == counter and size == COUNTER.size and value & 0xFFFFFFFF == count:
                uc.mem_write(counter, COUNTER.pack(count))
                ended.append(COUNTED)
                uc.emu_stop()

        def clean_frame(uc, access, address, size, value, _):
            # at link's push of the frame pointer, the frame below it
            opcode, displacement = LINK_INSTRUCTION.unpack(
                uc.mem_read(uc.reg_read(UC_M68K_REG_PC), LINK_INSTRUCTION.size)
            )
            if opcode & LINK_MASK == LINK and displacement < 0 and address + displacement >= STACK:
                fill = m.frame_fill
                if fill is None:
                    uc.mem_write(address + displacement, CLEAN_STACK[:-displacement])
                else:
                    uc.mem_write(address + displacement, (fill * (-displacement // len(fill) + 1))[displacement:])

        if entry:
            # a task's entry, as the kernel starts it: its stack's words
            # at SP, no return address below them
            sp = STACK_TOP - len(stack_arguments)
            m.uc.mem_write(sp, stack_arguments)
        elif m.stack_edge:
            # the arguments end where the stack's memory does, and the
            # caller's frame is nowhere mapped (UNMAPPED_FRAME)
            sp = STACK + STACK_SIZE - 4 - len(stack_arguments)
            m.uc.mem_write(sp, struct.pack(">I", RETURN) + stack_arguments)
            m.uc.reg_write(UC_M68K_REG_A6, UNMAPPED_FRAME)
        else:
            sp = STACK_TOP - 4 - len(stack_arguments)
            frame = FRAME_CONTENTS.pack(0, RETURN, m.profile.caller_name)
            m.uc.mem_write(sp, struct.pack(">I", RETURN) + stack_arguments + frame)
        if not m.starting_status & SUPERVISOR_BIT:
            # A7 is then the user stack, the supervisor's kept apart
            m.uc.reg_write(UC_M68K_REG_A7, SUPERVISOR_STACK)
        m.uc.reg_write(UC_M68K_REG_SR, m.starting_status)
        m.uc.reg_write(UC_M68K_REG_A7, sp)
        hooks = [
            m.uc.hook_add(UC_HOOK_INTR, trap),
            m.uc.hook_add(UC_HOOK_MEM_INVALID, fault),
            m.uc.hook_add(UC_HOOK_MEM_WRITE, clean_frame, begin=STACK, end=STACK + STACK_SIZE - 1),
        ]
        for index, (interrupt, at) in enumerate(interrupts):
            kind = UC_HOOK_MEM_WRITE if interrupt.write else UC_HOOK_MEM_READ
            hooks.append(m.uc.hook_add(kind, watched, index, begin=at, end=at))
        if interrupts:
            # every instruction, which a block translated before the hook
            # would run without
            hooks.append(m.uc.hook_add(UC_HOOK_CODE, step))
            m.uc.ctl_flush_tb()
        if m.counter is not None:
            hooks.append(m.uc.hook_add(UC_HOOK_MEM_WRITE, counted, begin=m.counter[0], end=m.counter[0]))
        if m.protected and m.profile.code:
            code, size = m.profile.code
            hooks.append(m.uc.hook_add(UC_HOOK_MEM_WRITE, code_write, begin=code, end=code + size - 1))
        try:
            m.uc.emu_start(start, RETURN, count=STEP_LIMIT)
            self.stopped = f"trap {trapped[0]}" if trapped else None
        except UcError as error:
            of = f" of {invalid[0]:x}" if invalid else ""
            self.stopped = None if ended else f"{error}{of} at {m.uc.reg_read(UC_M68K_REG_PC):x}"
        finally:
            for hook in hooks:
                m.uc.hook_del(hook)
            if interrupts:
                m.uc.ctl_flush_tb()
        if m.stray_write:
            ended.append(BUS_ERROR)
            self.stopped = None
        pc = m.uc.reg_read(UC_M68K_REG_PC)
        # the flash's arrays, which memory then holds again in place of
        # what the call's commands and status reads left there
        self.flash = None if m.flash is None else m.flash.contents()
        if self.flash is not None:
            m.uc.mem_write(ROM, self.flash)
        for address, contents in reversed(written):
            m.uc.mem_write(address, contents)
        if written and not ended:
            self.stopped = f"write to code of {invalid[0]:x} at {pc:x}"
        if self.stopped is None and not ended and pc != RETURN:
            if any(m.read(pc, len(branch)) == branch for branch in SELF_BRANCHES):
                ended.append(HALTED)
            else:
                self.stopped = f"no return in {STEP_LIMIT} steps"
        self.ended = ended[0] if ended else None
        self.registers = {name: m.uc.reg_read(register) for name, register in REGISTERS.items()}
        self.status = m.uc.reg_read(UC_M68K_REG_SR)
        self.memory = snapshot(m)
        self.compared = {name: m.read(start, size) for name, (start, size) in COMPARED.items()}

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


def literal_region(m: RebuiltMachine, at: int) -> int | None:
    """The start of the region of the machine's literals, the image's
    ROM, gcc's code and constants or an overlay's code, an address lies
    in; None if none."""
    return next((start for start, size in m.literals if start <= at < start + size), None)


def preceding(m: RebuiltMachine, at: int) -> bytes | None:
    """Up to BUFFER_SPACING bytes before an address where literals lie;
    None if it is not there."""
    region = literal_region(m, at)
    if region is None:
        return None
    start = max(region, at - BUFFER_SPACING)
    return m.read(start, at - start)


def same_literal(m: RebuiltMachine, a: int, b: int) -> bool:
    """Whether two pointers, each where literals lie, name the same
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
        return text != b"" or literal_region(m, a) != literal_region(m, b)
    except (UcError, ValueError):
        return False


def same_pointer(m: RebuiltMachine, a: int, b: int) -> bool:
    """Whether two pointers name the same thing in the two copies: the
    same literal, the same C function, the image's copy and gcc's (a
    driver's echo routine it stores), or the same place in an image
    object and its twin among the C's (Twins).  A pointer into an object
    with a twin is the same only as one to its twin's place, never as a
    literal: a child list or node of the C's tree that is not the twin of
    the image's would otherwise pass as the same empty text."""
    if (a, b) in m.copies or (b, a) in m.copies:
        return True
    twinned = (m.twins.image_address(b), m.twins.image_address(a), m.twins.c_address(a), m.twins.c_address(b))
    if any(at is not None for at in twinned):
        return a == twinned[0] or b == twinned[1]
    return same_literal(m, a, b)


def pointers_difference(m: RebuiltMachine, a: bytes, b: bytes) -> int | None:
    """None if two copies of memory are the same, but for pointers, at
    even offsets as the 68k keeps a long, that name the same thing
    (same_pointer); else the offset where they first differ."""
    at = 0
    while a[at:] != b[at:]:
        first = next(i for i in range(at, len(a)) if a[i] != b[i])
        even = first - first % POINTER_ALIGNMENT
        starts = range(max(at, even - POINTER_SIZE + POINTER_ALIGNMENT), even + 1, POINTER_ALIGNMENT)
        literal = next(
            (
                start
                for start in starts
                if start + POINTER_SIZE <= len(a)
                and same_pointer(m, *(struct.unpack_from(">I", memory, start)[0] for memory in (a, b)))
            ),
            None,
        )
        if literal is None:
            return first
        at = literal + POINTER_SIZE
    return None


def ram_difference(m: RebuiltMachine, a: Run, b: Run) -> str | None:
    """None if two calls left the memory_compared and the COMPARED
    memory the same, but for pointers to the same literal, function or
    place in an object and its twin; else where it first differs."""
    for start, memory in a.memory.items():
        first = pointers_difference(m, memory, b.memory[start])
        if first is not None:
            name = "RAM" if start == RAM else "the arguments' buffers"
            return f"{name} differs from {start + first:x}"
    for name, (start, _) in COMPARED.items():
        first = pointers_difference(m, a.compared[name], b.compared[name])
        if first is not None:
            return f"{name} differs from {start + first:x}"
    if a.flash != b.flash:
        first = next(i for i in range(FLASH_SIZE) if a.flash[i] != b.flash[i])
        return f"the flash differs from {ROM + first:x}"
    return None


def preserved_registers(convention: dict) -> tuple[str, ...]:
    """The registers a routine keeps for its caller, as its sidecar
    convention says (CALLEE_SAVED), in REGISTERS' order."""
    listed = set(convention.get("preserves", []))
    if PRESERVES_ALL in listed:
        return tuple(REGISTERS)
    if convention["convention"] in C_CONVENTIONS:
        listed |= set(CALLEE_SAVED)
    return tuple(name for name in REGISTERS if name in listed)


def registers_difference(m: RebuiltMachine, a: Run, b: Run, registers: tuple[str, ...]) -> str | None:
    """None if two calls that returned left each of registers alike, a
    pointer the same as one to the same thing (same_pointer); else
    those they did not, b's and a's.  A call that ended without
    returning keeps none."""
    if a.ended is not None or b.ended is not None:
        return None
    differing = [
        f"{name} {b.registers[name]:x}, image's {a.registers[name]:x}"
        for name in registers
        if a.registers[name] != b.registers[name] and not same_pointer(m, a.registers[name], b.registers[name])
    ]
    return "register " + ", ".join(differing) if differing else None


def effects_difference(a: Run, b: Run, names: tuple[str, str]) -> str | None:
    """None if two calls made the same kernel calls and device reads and
    writes in the same order; else the first that differs."""
    for i, (first, second) in enumerate(zip(a.effects, b.effects)):
        if first != second:
            return f"effect {i} {second}, {names[0]}'s {first}"
    if len(a.effects) != len(b.effects):
        return f"effects {len(b.effects)}, {names[0]}'s {len(a.effects)}"
    return None


@dataclass(frozen=True)
class Known:
    """A difference between a routine's image copy and gcc's whose cause
    is found and is not the C's to fix: its cause, and a pattern each
    failing case's outcome matches."""

    cause: str
    outcome: str


def stopped_outcome(a: Run, b: Run, names: tuple[str, str]) -> str | None:
    """None if both calls returned or ended at the same vector; a skip if
    both stopped, what the harness does not model deciding; else the
    difference."""
    if a.stopped is None and b.stopped is None:
        return None if a.ended == b.ended else f"one ended: {names[0]} {a.end()}, {names[1]} {b.end()}"
    if a.stopped is not None and b.stopped is not None:
        return "skipped: " + a.stopped
    return f"one stopped: {names[0]} {a.stopped}, {names[1]} {b.stopped}"


def report(name: str, outcomes: list[tuple[str, str | None]], known: "Known | None" = None) -> str:
    """ok, skip, known or FAIL for a routine's cases, printing why; which
    of agreeing, skipped, known or failed it counts as.  A routine with a
    known difference whose every failing case is that difference counts
    as known; one failing otherwise fails as any does."""
    failures = [(d, o) for d, o in outcomes if o and not o.startswith("skipped")]
    skips = [o for _, o in outcomes if o and o.startswith("skipped")]
    if failures and known is not None and all(re.search(known.outcome, o) for _, o in failures):
        print(f"known {name}: {len(failures)} of {len(outcomes)} cases, {known.cause}")
        return "known"
    if failures:
        for description, outcome in failures:
            print(f"FAIL {name} {description}: {outcome}")
        return "failed"
    if len(skips) == len(outcomes):
        print(f"skip {name}: {skips[0].removeprefix('skipped: ')}")
        return "skipped"
    print(f"ok {name}: {len(outcomes) - len(skips)} of {len(outcomes)} cases run")
    return "agreeing"
