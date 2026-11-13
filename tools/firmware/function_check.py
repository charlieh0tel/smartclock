# /// script
# requires-python = ">=3.11"
# dependencies = ["mc683xx==0.2.0", "unicorn==2.1.4"]
# ///
"""Runs each decompiled C function, as gcc compiled it, against the
image's own: in the rebuilt image (rebuild_link.py), the image's copy is
called with its arguments where the .conventions.jsonl sidecar says the
original reads them, as Ghidra's storage has them, and gcc's copy as
gcc's callers lay them, in 4-byte slots.  From the same RAM, the two must give
the same result, leave RAM the same, and make the same kernel calls and
device writes in the same order (harness.py), a pointer into an object
the C defines in place of the image's, the generated SCPI tree's
records, the same as one into the image's (harness.Twins), each starting with what
its callers set where it reads that (ESTABLISHED, SET_UP_REQUESTS,
CALLED_FIRST).  After the generated cases come a function's own, the
arguments its callers pass where the generated ones never reach them
(GIVEN_CASES); and where no declared argument lies, both copies find
the same pointers (UNDECLARED), so that a body reading an argument its
prototype leaves out differs.  A function may start from states its
zeroed RAM never reaches, drawn for each case, with routines it calls
answered in their place and their calls compared (STATES); find no
task by name (IDENT_FAILS); start in user mode, as a task, if it runs
in one, every case, or every other if supervisor code runs it too
(TASK_STATUS, task_context); end its endless loop at a count
(COUNTED_LOOPS); for the stack report, find the running task's stacks
(TASK_STACKS); and, in a given case, take an interrupt part way
(INTERRUPTS), start with variables set (PRESETS), a value or one
made for the machine, the installer's flash erased (FLASH), or its
arguments at the stack's edge (STACK_EDGE).  Each case's devices answer as harness.DEVICE_CASES
says, by the case.  Both copies must leave the status register's
system byte the same, its condition codes too for a function whose
convention returns in them (returns=ccr), and the registers the
function's convention keeps for its caller (harness.preserved_registers).

    DECOMPILED_CFLAGS='...' uv run tools/firmware/function_check.py \\
        third_party/firmware/decompiled/z3805a-3543b/z3805a-3543b.c \\
        third_party/firmware/images/z3805a-3543b.bin [NAME...]

With NAMEs, only those.  gcc's copy calls gcc's C and, through their
names, by the bindings' asm statements, the assembly; so a difference may lie in a
callee.  A call that ends at a trap that never returns, a zero divide,
a write to code, a branch to itself, its KERNEL_CALL_LIMIT'th kernel
call or its counter's count is compared up to there, but for its
result.  A function that
reaches beyond what the harness maps, calls a kernel service the
harness does not model, or traps the same way both times is skipped.  A function whose prototype has an
argument of no known size, a structure passed whole, or an argument
not on the stack is not run; a variadic one runs on its callers' lists
(VARIADIC_CASES), the parser's loop on SCPI lines a session's
console reads (SCPI_SESSIONS), and console_init, then the pForth
console's task it creates, on lines typed at the console
(CONSOLE_SESSIONS).  The
installer's functions run on a machine of its own, whose RAM is the
installer as the boot code unpacks it (harness.installer_profile), and
are counted apart.  Exits nonzero if any function fails, in either
image.
"""

import bisect
import json
import os
import random
import re
import shlex
import struct
import sys
import tempfile
from dataclasses import dataclass, field

from unicorn import UC_HOOK_CODE, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE, UcError
from unicorn.m68k_const import UC_M68K_REG_A7, UC_M68K_REG_D0, UC_M68K_REG_PC

from backend import ON_MC683XX, PARITY
from harness import (
    ALLOCATIONS,
    BYTE_MASK,
    CALL,
    CALL_INPUT,
    CASES,
    CONDITION_CODES,
    COUNT,
    EEPROM,
    ERASED,
    DeviceCase,
    REGISTERS,
    STARTING,
    TRAP_VECTORS,
    QSPI_LEVEL,
    QSPI_VECTOR,
    SPSR,
    UNMASKED,
    Interrupt,
    FIRST_ARGUMENT,
    HANDLER_ENTRIES,
    HANDLER_ENTRY_SIZE,
    HookedCall,
    KernelCall,
    MESSAGE_OFFSET,
    NULL_LONG,
    NUMERIC_DESCRIPTOR_58503A,
    NUMERIC_DESCRIPTOR_Z3815A,
    OVERLAY_SEPARATOR,
    PLLC_EXCHANGE,
    POINTER_ALIGNMENT,
    POINTER_FILLS,
    POINTER_SIZE,
    RECEIVED,
    SAMPLE_INTEGER_Z3801A,
    STACK_END,
    SUPERVISOR,
    SYSTEM_BYTE,
    TASK_RECORD_SIZE,
    TRACE_BITS,
    UNSIGNED,
    USER_STACK_BASE,
    Known,
    Machine,
    Run,
    Types,
    arguments,
    calls_differences,
    effects_differences,
    fresh_ram,
    gcc_slots,
    give,
    give_twins,
    header_text,
    image_labels,
    image_name,
    image_profiles,
    literal_region,
    overlay,
    pointer_at,
    preserved_registers,
    protect_code,
    ram_differences,
    rebuilt_machine,
    report,
    registers_difference,
    reset,
    snapshot,
    stopped_outcome,
    stub,
    tree_steps,
    twin_arguments,
    twins,
)
from event_log import EEPROM_BASE, LOG_ENTRIES, LOG_PAGE_CHECKSUM, entry_argument_offsets, page_offsets
from libc_check import BUFFER_SPACING, RAM, RAM_SIZE, RETURN
from rebuild_link import IMAGE_PREFIX, REBUILT, Section, code_symbols, link, objects, symbols
from report_figures import STRICT

# The widest argument or result the harness lays out.
WIDEST = 8
# gcc's inlining of a callee the C does not declare inline, off in the
# check's link, on in every other: at -O1 gcc puts a static function
# called once, and a callee smaller than its call, in its caller, where
# a hook at the callee's entry (install_hooks, record_calls) never sees
# the call.  So each such call is one in gcc's copy as in the image's,
# which calls each of the C's functions it calls.  A function declared
# inline, a binding's asm or always_inline's, stays in its caller: a
# binding's asm statement is its clobbers' check (bridge_check.py), which
# a call of its own would hide.  A helper the C defines static, which
# the image has no copy of, makes its calls for its caller
# (RebuiltCode.owner).
NOT_INLINED = ("-fno-inline-functions-called-once", "-fno-inline-small-functions")
# What both copies find on the stack where no declared argument lies,
# between the original's and after the last: the first pointer fill's
# text, whose every long at an even offset, 0x31303130, points into the
# pointees, compared after a call.  A function whose prototype leaves
# out an argument its body reads, which the decompiler then names
# in_stack_N, a local gcc never sets, reads it in the image's copy and
# not in gcc's (validate_or_doaction's second, 0x42d40; robin_store's
# second and third, 0x29aa0 and 0x29aac), so that the two differ.
UNDECLARED = POINTER_FILLS[0].removesuffix(NULL_LONG)
# What a function's callers set before they call it, which each case
# starts with: by function, each variable's name and its value, a name
# for the image's copy of that C function.  qsort stores its element
# size and comparator for qsort_range and its swaps (docs/firmware/libc.md),
# and its one call, satellite_status_list's (0x497c8), sorts the
# satellites' 2-byte entries with satellite_compare.
ESTABLISHED_QSORT = {"qsort_size": 2, "qsort_compare": "satellite_compare"}
ESTABLISHED = {name: ESTABLISHED_QSORT for name in ("qsort_range", "qsort_swap", "qsort_rotate")}
# The functions that read the pllc task's request from the variables
# get_setup_vars (0x431f4) stores it in, which pll_scpi_command
# (0x432bc) calls first, and the request, its message from +4, that
# each case starts with as the image's own get_setup_vars stores it:
# for those the numeric request's path through validate_num reaches,
# and pll_scpi_complete, which finishes a request the loop took, the one
# the harness's request_x receives from pllc_exchange (harness.RECEIVED);
# for pll_scpi_do_action, an action's, *TST?'s: its descriptor
# (0x41348), index 0 and source 0 (docs/firmware/structures.md, "The
# SCPI action descriptors").  validate_or_doaction, which
# pll_scpi_command calls on every request (0x432dc), takes each in
# turn, by the case: the action's path passes its second argument on
# to pll_scpi_do_action (0x42d40).
SET_UP = "get_setup_vars"
NUMERIC_REQUEST = RECEIVED[PLLC_EXCHANGE]
ACTION_REQUEST = struct.pack(">IIBBBBII", 0, 0x41348, 0, 0, 0, 0, 0, 0)
SET_UP_REQUESTS = {
    "validate_or_doaction": (NUMERIC_REQUEST, ACTION_REQUEST),
    "validate_num": NUMERIC_REQUEST,
    "pll_scpi_needs_loop": NUMERIC_REQUEST,
    "pll_scpi_send_to_loop": NUMERIC_REQUEST,
    "scpi_param_saved_in_nv": NUMERIC_REQUEST,
    "scpi_param_saved_in_record": NUMERIC_REQUEST,
    "finish_scpi_cmd": NUMERIC_REQUEST,
    "pll_scpi_complete": NUMERIC_REQUEST,
    "pll_scpi_do_action": ACTION_REQUEST,
}
# The routines a function's callers call first, which each case starts
# with as the image's own leaves what they set: by function, the
# routine's name and its stack arguments, longs.  console_init
# (0x12faa) allocates the pForth dictionary, pforth_dictionary_init
# (0x1ad28) through alloc_seg, before it defines a word; with
# pforth_dictionary_next null, as a fresh RAM has it,
# pforth_define_word returns at once (0x1ad8a), and so does every
# definition diag_words_define (0x1c18e) makes.
DICTIONARY_SIZE = 0x1000
DICTIONARY_INIT = ("pforth_dictionary_init", (DICTIONARY_SIZE,))
CALLED_FIRST = {name: DICTIONARY_INIT for name in ("pforth_define_word", "diag_words_define")}
# Cases of a function's own, after the generated ones, where those never
# give an argument what its callers do, or a value its body treats
# apart: by function, each case's arguments by position -- an int or a
# float, laid out as its parameter's type has it, or bytes, a pointer
# to a buffer of them -- the other positions keeping the generated
# case's, which cycle.
#
# pforth_define_word's argument and result counts, which console_init
# passes as 0 and 0 for ps, mem_rep and s_rep and diag_words_define
# from the table at 0x1c8ac, +0x1e and +0x22 of each 0x26-byte entry:
# adc_read 1 and 1, hdac_write 2 and 0, wr_eeprom 2 and 1, hdac_all and
# set_time 6 and 0.  The generated counts, 0, 1, 7 and the like, never
# reached its loop's second pass while the dictionary was null, and 2 is
# what found the loop's count read back from a register nothing set.
WORD_COUNTS = [(0, 0), (1, 1), (2, 0), (2, 1), (6, 0)]
# bytes_equal_n's two strings of one length, which gpsm_task (0x4c3e8)
# and gps_response_match (0x4fd78) compare: the generated lengths are
# never equal, and its loop never ran.  Equal, and differing in the last
# byte.
SAME_LENGTH = [(b"@@Ea", 4, b"@@Ea", 4), (b"@@Ea", 4, b"@@Eb", 4)]
# double_to_digits' digit count and mode as doprnt passes them: at most
# 17 digits in mode 1 for %e and %g (0x600f2, 0x602c4), and at most 60
# in mode 0 for %f (0x60436), 6 the default precision.
DIGITS = [(7, 1), (17, 1), (1, 1), (6, 0), (0, 0), (60, 0)]
# And the doubles the generated reals never are, which the libc
# routines take apart: a subnormal, which frexp normalizes (0x609a6, to
# 0x60962), and an infinity and a NaN, which frexp and modf raise on
# (0x609ac, 0x60dae) and modf then stores through iptr, float_raise
# keeping A0 (0x5e940, 0x60e62).
SPECIAL_DOUBLES = [5e-324, 1e-310, float("inf"), float("nan")]
# The magnitudes the decimal exponents' bisections (0x1258a, 0x122f6)
# find in their tables or past their ends: entries exactly, the image's
# 1e23 and 5e22 an ulp high among them, a negative, and beyond 1e-30 and
# 5e30.
EXPONENT_DOUBLES = [1.0, 5.0, -100.0, 1e30, 5e30, 1e31, 1e-31, 1e23, 5e22, 1.0000000000000001e23, 5.0000000000000004e22, 0.0]
# pow's operands the generated reals never are: a negative base to an
# integer power, odd and even, which pow takes the sign of apart
# (double_is_integer, 0x60e68), and a fraction.
POWERS = [(-2.0, 3.0), (-2.0, 2.0), (-0.5, 3.0), (2.0, 0.5), (0.0, 2.0)]
# vsprintf's formats, which the generated text never makes, each with
# its arguments: an unsigned with its top bit set, whose last digit
# doprnt takes from next_decimal_digit (0x5fd90) before its signed loop,
# as unsigned, signed and hex; then each conversion and flag doprnt's
# switch takes (0x5fb90), as the firmware's own formats use them.
TOP_BIT_SET = struct.pack(">II", 0xFFFFFFFF, 0x80000000)
FORMATS = [
    (b"%u\0", TOP_BIT_SET),
    (b"%d\0", TOP_BIT_SET),
    (b"%x\0", TOP_BIT_SET),
    (b"%10u|%-5u\0", TOP_BIT_SET),
    (b"%+d % d %05d %-4d|%ld %hd\0", struct.pack(">iiiiii", 42, 42, -42, 7, -123456789, 0x12345)),
    (b"%o %#o %X %#x %08x %p\0", struct.pack(">IIIIII", 8, 8, 0xBEEF, 0xBEEF, 0xBEEF, 0x1234)),
    (b"%c%c|%%|%5c\0", struct.pack(">iii", ord("A"), ord("z"), ord("q"))),
    (b"%f %.3f %10.2f %-8.1f|\0", struct.pack(">dddd", 3.14159, -2.5, 1e6, 0.05)),
    (b"%e %.2E %g %G\0", struct.pack(">dddd", 12345.678, -0.000123, 1e-5, 6.02e23)),
    (b"%g %g %.0f %#.0f\0", struct.pack(">dddd", 100000.0, 1234567.0, 2.5, 2.5)),
    # A string from ROM, "scpi: error opening stream\n" (0x2a16f), whole,
    # cut short by a precision, padded past the 20 a run of spaces holds,
    # and as %S; widths and precisions from the arguments, one negative;
    # zero padding past 20; the alternate forms of 0 and of %g; an
    # exponent of three digits; and a character doprnt does not convert.
    (b"%s|%.4s|%-30.3s|%.40S|%25s\0", struct.pack(">IIIII", *[0x2A16F] * 5)),
    (b"%*d|%-*d|%.*f|%*.*e\0", struct.pack(">iiiiidiid", 6, 42, -6, 42, 2, 0.25, 12, 3, 1.5)),
    (b"%030d|%-030d|%.0d|%#o|%#x|%#X|%.5d|%.5x\0", struct.pack(">iiiiiiii", -7, 7, 0, 0, 0, 255, -3, 0x1F)),
    (b"%#g|%#.3g|%e|%E|%.0e|%G\0", struct.pack(">dddddd", 1.0, 0.0001, 1.5e-300, -2.5e300, 9.99, 1e-10)),
    (b"%y|%5|%hu|%hx|%P|%#p\0", struct.pack(">iiii", 0x12345, 0x1BEEF, 0xABC, 0xABC)),
    (b"%f|%.20f|%.70f|%f\0", struct.pack(">dddd", 1e20, 1.0 / 3, 0.1, -0.0000001)),
]
# Double step records as clamp_double_to_step_and_limits reads them
# (scpi_double_step): a step, then load_double (0x322aa) as the routine,
# which returns the record's own step; one so fine the steps pass 1e9,
# which coarsens it (0x32612).
LOAD_DOUBLE = 0x322AA
DOUBLE_STEPS = [0.5, 0.001, 1e-12, 100.0]
# And the long and float step records the other validators read
# (scpi_step), load_long (0x32e2e) the routine, which returns the
# record's own step: a long's steps, 1 among them, which
# clamp_to_step_and_limits leaves the value at (0x3235c); a float's
# bits, the fine one coarsened as the double's is (0x324fc).
LOAD_LONG = 0x32E2E
LONG_STEPS = [1, 3, 1000, 0x10000]
FLOAT_STEPS = [0.5, 0.001, 1e-12, 100.0]


def validator_cases(load_double: int, load_long: int, load_long_unit_step: int | None = None) -> dict[str, list]:
    """The validators' given cases, each step record's routine an image's
    load_double or load_long; and clamp_to_limits', on load_long_unit_step,
    for an image that gives it."""
    cases = {
        "clamp_double_to_step_and_limits": [{4: struct.pack(">dI", step, load_double)} for step in DOUBLE_STEPS],
        "clamp_to_step_and_limits": [{4: struct.pack(">II", step, load_long)} for step in LONG_STEPS],
        **{
            name: [{4: struct.pack(">fI", step, load_long)} for step in FLOAT_STEPS]
            for name in ("clamp_float_to_step_and_limits", "clamp_float_to_step_double")
        },
    }
    if load_long_unit_step is not None:
        cases["clamp_to_limits"] = [{4: struct.pack(">II", step, load_long_unit_step)} for step in LONG_STEPS]
    return cases


# status_register_setup's and its installer copy's arguments as
# status_init passes them
# (0x2f4b8 to 0x2f50a): a status model, its first argument, whose
# seven register pointers point into the pointee memory, and the
# console port's object (0x29e18, the profile's) for the Status Byte,
# register 0, which has no parent; the others report into register 0
# or 2, at a bit, with no port.
SETUP_REGISTERS = struct.pack(">7I", *(0x31300000 + 0x10 * index for index in range(7)))
SETUP_MODEL = struct.pack(">hIIII", 7, 0, 0, 0, 0) + SETUP_REGISTERS


def console_port(m: Machine) -> int:
    return m.profile.scpi.console_port


SETUP_CASES = [
    (0, 0, 0, 0, 0, console_port),
    (1, 0x0010FFFE, 0x0010FFFE, 0, 0x20, 0),
    (3, 0xFFFF0002, 0x12345678, 2, 8, 0),
    (6, 0x13FF, 0, 2, 1, 0),
]
# scpi_format_resolution_array_record's records, by kind (0x26822): a
# block, a resolution array and a kind of neither, each at eight zero
# bytes in ROM (0x2f76e, an empty block and an empty array), with
# scpi_input_source (0x29ada) as the release routine at +6, which it calls
# whatever the kind.
EMPTY_IN_ROM = 0x2F76E
RETURN_ZERO = 0x29ADA
ARRAY_RECORD_KINDS = [0, 1, 2, -1]


def array_record_cases(empty: int, release: int) -> list[dict]:
    """scpi_format_resolution_array_record's given cases, each kind's
    record at an image's eight zero bytes in ROM with its release routine."""
    return [{1: struct.pack(">bBII", kind, 0, empty, release)} for kind in ARRAY_RECORD_KINDS]


# scpi_real_to_text's arguments, a double, its significant digits, its
# exponent's digits, the resolution and the point, as its callers pass
# them (scpi_format_double 0x26870: 15, 3, -308 and 1; format_si
# 0x49b88: 2 and 0) and around them, which the generated doubles, two
# longs, never are: positive and negative (0x255c8), digits copied
# (0x25612) and one digit with and without the point (0x25600,
# 0x255fc); a resolution that rounds and cuts the digits (0x25594,
# 0x255b2), one above the double's exponent (0x2557e) and one it rounds
# up to (0x255a6); zero converted at -308 and written at any other
# (0x2568a), with and without the point; a negative exponent (0x25624),
# a width that takes three digits, one that refuses the hundreds'
# (0x25668), a width of 4 that skips none (0x2565c); hundreds from 100,
# which read past decimal_digit_pairs (0x25640, quirks.md), and -32768,
# whose divu.w overflows (0x2562e); no digits (0x25562), and an
# infinity, which scpi_double_to_decimal refuses (0x2556e).
REAL_TEXT_CASES = [
    (1.5, 15, 3, -308, 1),
    (123.456, 6, 3, -308, 1),
    (-2.25, 6, 3, -308, 1),
    (5.0, 1, 3, -308, 0),
    (5.0, 1, 3, -308, 1),
    (123.456, 15, 3, -2, 1),
    (0.004, 15, 3, -2, 1),
    (0.006, 15, 3, -2, 1),
    (0.0, 15, 3, -308, 1),
    (0.0, 15, 3, -3, 1),
    (0.0, 6, 2, 0, 0),
    (1e-5, 15, 3, -308, 1),
    (1e300, 15, 3, -308, 1),
    (12.345, 4, 2, -3, 0),
    (1.5, 15, 1, -308, 1),
    (1e100, 15, 1, -308, 1),
    (1e100, 15, 4, -308, 1),
    (0.0, 6, 3, 12000, 1),
    (0.0, 6, 4, -32767, 0),
    (0.0, 6, 4, -32768, 0),
    (1.5, 0, 3, -308, 1),
    (float("inf"), 15, 3, -308, 1),
]
# qspi_queue_setup's devices, each written and read, the generated ones
# reaching only 0, 3 and those above 12 (0x205c0's table); an FPGA
# block's word counts, none, some and more than the queue's 16
# (0x206d6), and the readback's bits, fewer than 8, some and more than
# 16 (0x2072c).
QSPI_DEVICES = range(14)
QSPI_FPGA_BLOCK = 6
QSPI_FPGA_READBACK = 0xC
QSPI_COUNTS = [0, 3, 10, 20]
# scpi_parse_text's string tokens, scpi_token_string, a length and the
# text, here a ROM string's, "scpi: error opening stream\n" (0x2a16f):
# the length its NUL ends it at, one past, which the NUL then shortens,
# its last five, and a length whose word is negative (0x25d60).
ROM_TEXT = 0x2A16F
ROM_TEXT_LENGTH = 27
TEXT_TOKENS = [
    struct.pack(">HI", length, text)
    for length, text in (
        (ROM_TEXT_LENGTH, ROM_TEXT),
        (ROM_TEXT_LENGTH + 1, ROM_TEXT),
        (5, ROM_TEXT + ROM_TEXT_LENGTH - 5),
        (0x8000 | ROM_TEXT_LENGTH, ROM_TEXT),
    )
]


@dataclass(frozen=True)
class Pointed:
    """A buffer a given argument points at, its parts in order: bytes as
    they are, and another Pointed as a pointer to a buffer of its own."""

    parts: tuple["bytes | Pointed", ...]


# What a given case may set beside its arguments, by key: the interrupts
# it takes (harness.Interrupt); the status register it starts with, in
# place of the generated case's; variables at values, lvalues of the C
# as a Preset's target, a value being a number, one made for the
# machine, or a routine's name, its image copy's address; the flash's contents, the installer's; and
# whether its stack arguments end at the stack's edge, the caller's
# frame unmapped (harness.UNMAPPED_FRAME), so that a read past what the
# image reads faults; the registers it starts with, by name, a value or
# one made for the machine, where its routine reads them as its caller
# left them (harness.RebuiltMachine.starting_registers); and an SCPI
# handler's call record's input port, made for the machine, in place of
# the console port's object.  Every case of a routine that restores the
# status register from its buffer, given or generated, starts with that
# word's trace bits clear (JUMP_FUNCTIONS); and the results its States'
# hooks give and the variables they leave, by routine, in place of those
# drawn for it.
INTERRUPTS = "interrupts"
STARTING_STATUS = "status"
PRESETS = "presets"
FLASH = "flash"
DEVICES = "devices"
RECORDS = "records"
STACK_EDGE = "edge"
CALLERS_REGISTERS = "registers"
INPUT_PORT = "input port"
HOOK_RESULTS = "hook results"
HOOK_LEAVES = "hook leaves"
# The QSPI's end of a queue (harness.QSPI_VECTOR), taken after the
# first, second or third read of qspi.running in qspi_start's wait
# (0x20866).


def qspi_end_of_queue(first: int) -> Interrupt:
    return Interrupt(QSPI_VECTOR, QSPI_LEVEL, "qspi.running", first, ready=(SPSR,))


# The DUART's interrupt: vector 67, which duart_ivr_test leaves in IVR
# (0x43, duart.c) and whose handler, primary_user_67 (0x1eaf4), runs a
# channel's loopback receive and transmit while its loopback_test is
# set, as ISR shows RxRDY and TxRDY under the shadow IMR; its level, not
# set by software, at 2, the mask the firmware raises around its DUART
# accesses (duart_b_putc_polled, duart.h).  Taken after each read of
# loopback_expect_next in duart_loopback_test's wait (0x1e398): the
# first firing shows TxRDY alone, which sends the first character, and
# each after RxRDY as well, which takes back the character sent before,
# TB and RB one register in the harness's memory, as local loopback has
# them, until the text, "DUART test string", is all back.
DUART_VECTOR = 0x43
DUART_LEVEL = 2
DUART_ISR = 0x20000B
LOOPBACK_FIRINGS = 20


def duart_loopback(receive: int, transmit: int) -> Interrupt:
    return Interrupt(
        DUART_VECTOR,
        DUART_LEVEL,
        "loopback_expect_next",
        1,
        every=1,
        times=LOOPBACK_FIRINGS,
        presents=(((DUART_ISR, 1, transmit),), ((DUART_ISR, 1, receive | transmit),)),
    )


# The periodic interrupt, primary_user_66 (0x11f82): vector 66 at level
# 5, as periodic_interrupt_enable sets PICR (startup.h), taken after
# interval_wait_reading sets interval_tick_wait, with ticks_since_pps one
# short of the 0x199 at which the tick signals the wait's event, and a
# 1PPS dead, which takes it to the wait.
PERIODIC = Interrupt(0x42, 5, "interval_tick_wait", 1, write=True)
TICK_WAIT_PRESETS = {"gps_pps_dead": 1, "ticks_since_pps": 0x198}
QSPI_WORDS = struct.pack(">3H", 0x123, 0x456, 0x789)
INTERRUPT_CASES = {
    "qspi_start": [
        {0: device, 1: QSPI_WORDS, 2: 3, 3: 0, INTERRUPTS: (qspi_end_of_queue(first),), STARTING_STATUS: UNMASKED}
        for device in (2, 7)
        for first in (1, 2, 3)
    ],
    "duart_loopback_test_a": [
        {INTERRUPTS: (duart_loopback(0x02, 0x01),), STARTING_STATUS: status} for status in (UNMASKED, 0)
    ],
    "duart_loopback_test_b": [
        {INTERRUPTS: (duart_loopback(0x20, 0x10),), STARTING_STATUS: status} for status in (UNMASKED, 0)
    ],
    "interval_wait_reading": [{PRESETS: TICK_WAIT_PRESETS, INTERRUPTS: (PERIODIC,), STARTING_STATUS: UNMASKED}],
}
# The flash's cases, the installer's, each from the device case its
# generated case gives (harness.DEVICE_CASES), busy for some polls, or
# failing: installer_flash_program_word's addresses, the writable
# range's ends, each pair's, and past each end, and words, one that only
# clears bits and one the erased flash takes whole; :ERASe?'s flash
# erased, wholly and all but its last word, which the range's end
# reads; and :DOWNload's records (FLASH_RECORDS).
FLASH_ADDRESSES = (0x10000, 0x3FFFE, 0x40000, 0x7FFFE, 0xFFFE, 0x80000)
FLASH_WORDS = (0x1234, 0x0000)
FLASH_START = 0x10000
FLASH_END = 0x80000


def erased_flash(image: bytes, last: bytes = b"", start: int = FLASH_START) -> bytes:
    """The image's flash with its writable range, from start, erased, its
    last bytes last."""
    erased = image[:start] + b"\xff" * (FLASH_END - start)
    return erased[: len(erased) - len(last)] + last


def s_record(kind: int, address: int, data: bytes, address_bytes: int, checksum: int | None = None) -> bytes:
    """An S-record's text, its count the address's, data's and checksum's
    bytes and its checksum their sum's ones' complement, or checksum."""
    body = bytes([address_bytes + len(data) + 1]) + address.to_bytes(address_bytes, "big") + data
    total = ~sum(body) & 0xFF if checksum is None else checksum
    return b"S%d" % kind + (body + bytes([total])).hex().upper().encode()


# :DOWNload's parameter, a scpi_text_parameter, its kind and its text
# (scpi.h): an S1, whose 16-bit address is always below the range, an
# S2 and S3 of data at the range's start, an S2 of an
# odd count, which an erased byte pads, one across the second pair's
# start, one at the range's end that runs past it, one below it, an S0
# and an S5, which do nothing, an S9, which programs its data as an S1,
# and, each else an S2 the range takes, a bad checksum, an S4 and a
# text that is no record, "X2..." and "s2...".
FLASH_RECORDS = [
    s_record(1, 0x1000, b"\x12\x34", 2),
    s_record(2, 0x010000, bytes(range(8)), 3),
    s_record(3, 0x00010000, b"\xa5\x5a\x00\xff", 4),
    s_record(2, 0x020000, b"\x01\x02\x03", 3),
    s_record(2, 0x03FFFC, bytes(range(8)), 3),
    s_record(2, 0x07FFFC, bytes(range(8)), 3),
    s_record(2, 0x00FFFE, b"\x11\x22", 3),
    s_record(0, 0, b"HDR", 2),
    s_record(5, 3, b"", 2),
    s_record(9, 0x1000, b"", 2),
    s_record(2, 0x010000, b"\x12\x34", 3, checksum=0),
    *(kind + s_record(2, 0x010000, b"\x12\x34", 3)[2:] for kind in (b"S4", b"X2", b"s2")),
]


def text_parameter(text: bytes) -> Pointed:
    """A scpi_text_parameter of a text: its kind word, 0, and its pointer."""
    return Pointed((bytes(2), Pointed((text + b"\0",))))


# The SCPI suffix matchers' units, the generated tree's tables in ROM,
# which both copies read: :OUTPut:PIN1:FREQuency's MHZ at 6 and HZ,
# which takes a multiplier (0x53d6e), and :OUTPut:PIN1:DELay's S, which
# does too (0x53d4c); and suffixes, in upper case as the lexer keeps
# them (smartclock.h, scpi_number): each unit whole, with each length of
# multiplier, one and two letters, known and not, and with three, too
# many; one the units do not end, and none.
FREQUENCY_UNITS = 0x53D6E
SECONDS_UNITS = 0x53D4C
SUFFIXES = [
    (FREQUENCY_UNITS, b"MHZ"),
    (FREQUENCY_UNITS, b"HZ"),
    (FREQUENCY_UNITS, b"KHZ"),
    (FREQUENCY_UNITS, b"MAHZ"),
    (FREQUENCY_UNITS, b"EXHZ"),
    (FREQUENCY_UNITS, b"XHZ"),
    (FREQUENCY_UNITS, b"XYHZ"),
    (FREQUENCY_UNITS, b"KMAHZ"),
    (FREQUENCY_UNITS, b"S"),
    (SECONDS_UNITS, b"S"),
    (SECONDS_UNITS, b"MS"),
    (SECONDS_UNITS, b"US"),
    (SECONDS_UNITS, b"PES"),
    (SECONDS_UNITS, b"MAS"),
    (SECONDS_UNITS, b"SS"),
    (SECONDS_UNITS, b""),
]
# A scpi_number's digits and suffix (smartclock.h), and the word its
# scale and unit start as, apart from what a match leaves.
SCPI_NUMBER_DIGITS = 16
SUFFIX_ROOM = 12
UNSET_WORD = 0x5A5A


def scpi_number(text: str, suffix: bytes = b"") -> bytes:
    """A scpi_number of a decimal text, '-' first for a negative and a
    point where it has one, and a suffix, as the lexer leaves it
    (smartclock.h): its digits from the first nonzero, the exponent such
    that its integer part is the digits before the point, its places at
    scale 0 (scpi_engine_places, scpi_convert.h), and its scale and unit
    set apart from what a match leaves."""
    negative = text.startswith("-")
    whole, _, fraction = text.lstrip("-").partition(".")
    digits = (whole + fraction).lstrip("0")
    exponent = len(whole.lstrip("0")) - SCPI_NUMBER_DIGITS
    laid = [int(d) for d in digits[:SCPI_NUMBER_DIGITS]]
    return struct.pack(
        f">h{SCPI_NUMBER_DIGITS}bhh{SUFFIX_ROOM}shh",
        int(negative),
        *laid + [0] * (SCPI_NUMBER_DIGITS - len(laid)),
        exponent,
        len(suffix),
        suffix,
        UNSET_WORD,
        UNSET_WORD,
    )


# The converters' numbers about each type's limits: below, at and past
# the most, positive and negative, one that rounds over it, those whose
# integer part's tenths pass the limit the converter tests them against
# (MOST_TENTHS, scpi_convert.h), past what the type's arithmetic then
# wraps to, and one with a digit too many, and zero.
SHORT_NUMBERS = [
    *("32767", "32768", "-32768", "-32769", "32767.5", "32770"),
    *("65535", "65536", "65539", "-65537", "65540", "99999", "123456", "0", "-5.5"),
]
LONG_NUMBERS = [
    "2147483647",
    "2147483648",
    "-2147483648",
    "-2147483649",
    "4294967295",
    "4294967296",
    "4294967295.5",
    "4294967300",
    "9999999999",
    "42949672960",
    "1.25",
]
CONVERTER_CASES = {
    f"{prefix}scpi_number_to_{kind}": [{0: scpi_number(text), 2: 0} for text in numbers]
    for prefix in ("", "installer_")
    for kind, numbers in (
        ("short", SHORT_NUMBERS),
        ("unsigned_short", SHORT_NUMBERS),
        ("long", LONG_NUMBERS),
        ("unsigned_long", LONG_NUMBERS),
    )
}
# The double converter's numbers: some that convert, one with more
# digits than a scpi_number keeps, zero, one past a double's range,
# which scpi_bcd_to_double refuses (Z set, 0x25258), and one whose
# exponent is past 309, which scpi_number_to_bcd refuses (0x2550e).
DOUBLE_NUMBERS = ["1.5", "-2.25", "123456789012345678", "0", "2" + "0" * 308, "1" + "0" * 400]
CONVERTER_CASES["scpi_number_to_double"] = [{0: scpi_number(text), 2: 0} for text in DOUBLE_NUMBERS]
# The numbers the parameter parsers are given, each with a suffix and
# units of SUFFIXES', some of which take it and some not, and with a
# suffix and no units.
PARSED_NUMBERS = ["12", "-7", "40000", "3000000000", "0.5"]
SUFFIX_CASES = {
    **{
        name: [{0: len(suffix), 1: suffix + b"\0", 2: units, 3: bytes(2)} for units, suffix in SUFFIXES]
        for name in ("scpi_match_suffix", "installer_scpi_match_suffix")
    },
    **{
        name: [{0: scpi_number("1", suffix), 1: units} for units, suffix in SUFFIXES]
        + [{0: scpi_number("1", b"MHZ"), 1: 0}, {0: scpi_number("1"), 1: 0}]
        for name in ("scpi_apply_suffix", "installer_scpi_apply_suffix")
    },
    **{
        f"{prefix}scpi_parse_{kind}": [
            {0: scpi_number(text, suffix), 3: units}
            for i, text in enumerate(PARSED_NUMBERS)
            for units, suffix in SUFFIXES[i % 3 :: 3] + [(0, b"MS")]
        ]
        for prefix in ("", "installer_")
        for kind in ("short", "unsigned_short", "long", "unsigned_long")
    },
    "installer_scpi_parse_text": [{0: token} for token in TEXT_TOKENS],
}


def status_register(event: int, parent: "Pointed | None", parent_bit: int) -> Pointed:
    """A status_register record (smartclock.h) with an event word, no
    enable, and its parent, none for the top register."""
    return Pointed((struct.pack(">5H", 0, 0, 0, 0, event), parent or NULL_LONG, struct.pack(">h", parent_bit)))


def two_register_model(m: Machine) -> Pointed:
    """A status_model (smartclock.h) of two registers, neither the top
    one, which status_clear_events reads from the last down to the
    second, each reporting into a top register of its own, whose status
    byte is the profile's console port's."""
    registers = [status_register(event, status_register(0, None, 0), bit) for event, bit in ((0x55, 4), (0x33, 8))]
    return Pointed(
        (struct.pack(">hIIIi", len(registers), m.profile.scpi.console_port, 0, 0, 0), *registers)
        + (NULL_LONG,) * (STATUS_MODEL_REGISTERS - len(registers))
    )


# An error queue's entries, a status_error_entry each, and the queue's
# states, as (head, tail): one entry, two, the tail at the last entry,
# which then wraps to the first, the head there too, and empty; each
# reporting into the status model status_init builds.
STATUS_MODEL_REGISTERS = 7
QUEUE_SIZE = 4
QUEUE_STATES = [(1, 0), (2, 0), (0, 3), (1, 3), (3, 3), (0, 0)]


def error_queue(head: int, tail: int):
    """A maker of a status_error_queue of QUEUE_SIZE distinct entries in
    a state (smartclock.h)."""

    def make(m: Machine) -> bytes:
        status = pointer_at(m, m.profile.status_object)
        entries = b"".join(struct.pack(">hI", -100 - i, 0x1000 + i) for i in range(QUEUE_SIZE))
        return struct.pack(">hhIh", head, tail, status, QUEUE_SIZE) + entries

    return make


STATUS_CASES = {
    **{name: [{0: two_register_model}] for name in ("status_clear_events", "installer_status_clear_events")},
    **{
        name: [{0: error_queue(head, tail)} for head, tail in QUEUE_STATES]
        for name in ("error_queue_pop", "installer_error_queue_pop")
    },
}
# :SYSTem:LANGuage's texts, which it puts in upper case in place: each
# language, in lower and mixed case and as given, of the primary and the
# installer; another; and the longest a byte index reaches, 127
# characters (0x30680, installer::0x107f92).
LANGUAGES = [b"pforth", b"Install", b"primary", b"PRIMARY", b"INSTALL", b"bogus", b"a" * 127]
# :DIAGnostic:TEST?'s choices, a choice word after its kind (0x2bc2e):
# each of the primary's 14 and one past them, which the parser never
# gives; the installer's test, a short, each of its five, one past and
# a negative (installer::0x108088), with the boot code's results each
# its own, so that each test's word shows.
PRIMARY_TESTS = 14
INSTALLER_TESTS = 5
BOOT_RESULTS = {
    "boot_selftest.checksum": 2,
    "boot_selftest.cpu": 1,
    "boot_selftest.ram": 3,
    "boot_selftest.ram_data_bits": 0x8001,
    "boot_selftest.ram_address_lines": 0x40020,
    "boot_selftest.uart": 0x10,
}
# :DIAGnostic:QUERy:RESPonse?'s from a port not the console's, which
# copies the count at A3 of the text at A4 to A2 as its caller left them
# (0x2b91a; docs/firmware/quirks.md): another port's object, a buffer
# the handler only compares; each count, none, part and all of the
# text, its bytes apart from the copy's fill; and the three registers
# each a buffer of its own, so that one read for another shows.
QUERY_ECHO = b"+0,\"No error\"\r\n"
QUERY_ECHO_COUNTS = (0, 5, len(QUERY_ECHO))
QUERY_RESPONSE_CASES = [
    {
        INPUT_PORT: lambda m: m.buffer(),
        CALLERS_REGISTERS: {
            "A2": lambda m: m.buffer(),
            "A3": lambda m, count=count: m.buffer(bytes([count])),
            "A4": lambda m: m.buffer(QUERY_ECHO),
        },
    }
    for count in QUERY_ECHO_COUNTS
]
# os_report_process's: a zeroed task record, its segments none, with D5
# and D6 as its caller left them, the stack share it prints for a task
# whose supervisor stack is in no segment and the memory it adds the
# task's segments to (0x1632e), a kilobyte and more apart, so that its
# kB column shows the one read for the other.
REPORT_REGISTERS = [{"D5": 0x400, "D6": 0x20000}, {"D5": 0x20000, "D6": 0x400}]
REPORT_CASES = [{0: bytes(TASK_RECORD_SIZE), CALLERS_REGISTERS: registers} for registers in REPORT_REGISTERS]
HANDLER_CASES = {
    "scpi_query_DIAGnostic_QUERy_RESPonse": QUERY_RESPONSE_CASES,
    **{
        name: [{0: text_parameter(text)} for text in LANGUAGES]
        for name in ("scpi_set_SYSTem_LANGuage", "installer_scpi_set_SYSTem_LANGuage")
    },
    "scpi_query_DIAGnostic_TEST": [{0: struct.pack(">hH", 0, choice)} for choice in range(PRIMARY_TESTS + 1)],
    "installer_scpi_query_DIAGnostic_TEST": [
        {0: struct.pack(">hhh", 0, test, 0), PRESETS: BOOT_RESULTS} for test in (*range(INSTALLER_TESTS + 1), -1)
    ],
}
# longjmp's buffer, a jmp_buf as setjmp fills it (libc.h,
# libc_jump_buffer): D1 to D7 and A1 to A5, each distinct, A0, the
# frame, the return address the harness's own, so that the call ends
# there, the stack pointer at it, in the harness's stack below its
# frames, and the status register, which trap #1 restores for a value
# but 0; and the values, 0, which it saves the status register for
# again and returns as 0, and others.
JUMP_STACK = 0x708000
JUMP_BUFFER = struct.pack(
    ">12IIIIIH", *(0x11110000 + i for i in range(12)), 0x2222AAAA, 0x3333BBBB, RETURN, JUMP_STACK, 0x2704
)
JUMP_VALUES = [0, 1, -1, 0x12345678]
# The routines that restore a status register from their buffer, the
# first argument, at JUMP_STATUS (libc_jump_buffer's sr), and the
# offset: in every case of theirs, the generated ones' too, the word's
# trace bits are cleared before the run, on both backends, since
# setjmp, which fills the buffer, runs untraced.  A generated case's
# buffer may set them (case 0's 0xc0, case 1's 0xe5), which mc683xx's
# strict CPU32 then takes: each instruction after the restore traces to
# the handler, which prints its PC.
JUMP_FUNCTIONS = ("longjmp", "installer_longjmp")
JUMP_STATUS = 0x40
# atexit's table, its count at the most and one short, given a NULL and
# a routine (smartclock.h, atexit_functions[32]).
ATEXIT_MOST = 32
# sign_extend_bits' values and widths: the byte and word widths it takes
# apart, every other from 1 to 31, and those it leaves the value at.
SIGN_VALUES = [0x00000010, 0xFFFFFFF0, 0x0000001F, 0x00000800, 0x7FFFFFFF, 0x80000001]
SIGN_WIDTHS = [0, 1, 4, 5, 8, 11, 12, 16, 17, 24, 31, 32, 33, 255]
# console_port_read's read errors, DUART channel B's as console_read
# gives them (stdio.c): none, each bit, and all; with the line read, its
# next past its end, so that the port reads the console.
READ_ERRORS = [0, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0xFF]
# atoi's texts with a high-bit character, which it indexes ctype_table
# by sign extended (0x5ed0a), reading the 128 bytes before the table,
# atoi's own code: 0x90, whose byte there has the space bit, before a
# number, 0x95, the digit's, in one and after one, and 0xa0, both, and
# 0xff, the EOF entry, neither.
ATOI_TEXTS = [b"\x9012\0", b"1\x952\0", b"\x95\0", b"\xa0-3\0", b"\xff4\0", b"7\x90\0"]
LIBRARY_CASES = {
    "atoi": [{0: text} for text in ATOI_TEXTS],
    **{name: [{0: JUMP_BUFFER, 1: value} for value in JUMP_VALUES] for name in ("longjmp", "installer_longjmp")},
    **{
        f"{prefix}atexit": [{0: 0}, {0: 0, PRESETS: {f"{prefix}atexit_count": ATEXIT_MOST}}]
        + [{PRESETS: {f"{prefix}atexit_count": count}} for count in (ATEXIT_MOST, ATEXIT_MOST - 1)]
        for prefix in ("", "installer_")
    },
    **{
        name: [{0: value, 1: width} for value in SIGN_VALUES for width in SIGN_WIDTHS]
        for name in ("sign_extend_bits", "installer_sign_extend_bits")
    },
    **{
        f"{prefix}console_port_read": [
            {
                PRESETS: {
                    f"{prefix}scpi_input_next": 2,
                    f"{prefix}scpi_input_end": 1,
                    f"{prefix}duartb.read_errors": errors,
                }
            }
            for errors in READ_ERRORS
        ]
        for prefix in ("", "installer_")
    },
}
# The line readers' echoes as a routine of the image's reads its
# character, at the start of a 2-byte slot, as drtb_putc, sci_putc and
# installer_drtb_putc do (0x1ddfa, 0x201b4, installer::0x10386c), and
# the Z3815A 4010's installer_sci_putc, its installer's host port's, which
# the image's reconfiguration stores there (0x1dd54): a routine at
# harness.ROUTINES, whose calls are compared, its parameter's position
# past the reader's two.  And the echo off, as the image's
# reconfiguration leaves it, its echo_none routine, a bare rts, by name
# (the Z3805A's at 0x1d570 and 0x1f6c8, the 58503A's at 0x1d3d0 and
# 0x1f328), which the installer has none of, never setting its echo;
# and 0, as RAM starts, which the reader calls as it calls any echo, not
# testing for none (0x1d8ca).  By reader, its port's echo and its
# echo_none; each in four cases, reading a line into a buffer of 32
# bytes.
ECHO_POSITION = 2
ECHO_CASES = 4
ECHO_LINE = 32
IMAGE_ECHOES = {
    "drtb_gets": ("duartb.echo", "duartb_echo_none"),
    "sci_gets": ("sci.echo", "sci_echo_none"),
    "installer_drtb_gets": ("installer_duartb.echo", None),
    "installer_sci_gets": ("installer_sci.echo", None),
}


def echo_routine(m: Machine, types: Types) -> int:
    """A routine at harness.ROUTINES of the echoes' type, putc_function."""
    return stub(m, types, types.routine("putc_function *"), ECHO_POSITION)


# gps_request_build's requests with their arguments, each of the GPS
# task's 49 messages, GPS_ARGUMENTS_SET, the 0 mode (smartclock.h),
# which runs the message's encoders, the image's own routines, taking an
# index in a 2-byte slot (0x4f9c6); with eight distinct arguments and
# room for the longest message.
GPS_MESSAGES = 49
GPS_ARGUMENTS = struct.pack(">8I", *(0x01020304 * (i + 1) for i in range(8)))
GPS_MESSAGE_ROOM = 128


def gps_request_build_cases(messages: int) -> list[dict[int, int | bytes]]:
    """gps_request_build's given cases, one for each of an image's messages."""
    return [{0: message, 1: 0, 2: GPS_ARGUMENTS, 3: bytes(GPS_MESSAGE_ROOM)} for message in range(messages)]


# The codes the 58503A 3704's, the Z3815A 4010's and the Z3817A 3752's
# log_event takes, each a given case, from 0 to one past the last its
# table has a text for (LOG_SYSTEM_PRESET, 0x1b in each), so that each code's text, which the call of
# log_write it makes passes (record_calls), is compared: the generated
# cases reach only a few.  log_event, the Z3805A's and the Z3801A's,
# writes its entry itself, in RAM and the EEPROM, which compare it.
LOG_EVENT_CODES = 0x1C
LOG_EVENT_CASES = [{0: code} for code in range(LOG_EVENT_CODES + 1)]
# counter_read's, counter_calibrate's and counter_arm's reading with no
# error, as counter_arm leaves it, so that they reach the counter and the
# interpolator (COUNTER_STATES): the generated cases' readings all have
# one.  Each is given configurations of its own: counter_read the
# generated one, each byte apart, so that a copy reading another cell of
# it than the image's reads another value; counter_calibrate and
# counter_arm ones whatever the record's layout, so that the work each
# does shows in the cells it writes: of every byte
# COUNTER_CALIBRATE_ALWAYS (3), calibrating, or COUNTER_SYNC_WHEN_WANTED
# and COUNTER_WORK_WANTED (2), syncing, and of 1 and 2 by turns, which
# every mode and state pair at an even offset, every image's, reads as
# when wanted and wanted: COUNTER_REGISTERS_WHEN_WANTED,
# COUNTER_CALIBRATE_WHEN_WANTED and the Z3815A 4010's
# COUNTER_START_SOURCE_WHEN_WANTED.
COUNTER_READING = bytes(16)
COUNTER_CONFIGURATION_SIZE = 0x40
COUNTER_ALWAYS = bytes([3]) * COUNTER_CONFIGURATION_SIZE
COUNTER_SYNC_WANTED = bytes([2]) * COUNTER_CONFIGURATION_SIZE
COUNTER_PAIRS_WANTED = bytes([1, 2]) * (COUNTER_CONFIGURATION_SIZE // 2)
COUNTER_CASES = 4
COUNTER_GIVEN_CASES = {
    "counter_read": [{0: COUNTER_READING}] * COUNTER_CASES,
    **{
        name: [{0: COUNTER_READING, 1: configuration} for configuration in configurations for _ in range(COUNTER_CASES)]
        for name, configurations in (
            ("counter_calibrate", (COUNTER_ALWAYS, COUNTER_PAIRS_WANTED)),
            ("counter_arm", (COUNTER_SYNC_WANTED, COUNTER_PAIRS_WANTED)),
        )
    },
}
# The EEPROM's records' readers, the primary's and the installer's, by
# the record each reads, each with the record good and with it bad,
# where the erased EEPROM every other case starts with gives none
# (eeprom_records): a good record's fields are read, each a byte its
# address makes, so that a field read for another differs; the
# calibration block by its address, which only some images' C names.
CALIBRATION_BLOCK = "*((calibration_block *)0x400080)"
RECORD_READERS = {
    **{
        name: "eeprom_parameter_record"
        for name in ("parameter_record_read", "installer_parameter_record_read")
    },
    **{
        name: "eeprom_identification_record"
        for name in (
            "identification_record_read", "identification_record_write", "installer_identification_record_read"
        )
    },
    **{
        name: CALIBRATION_BLOCK
        for name in (
            "calibration_block_read", "efc_calibration_read", "save_calibration_block"
        )
    },
    "lifetime_record_read": "eeprom_lifetime_record",
}
RECORD_CASES = {name: [{RECORDS: {record: good}} for good in (True, False)] for name, record in RECORD_READERS.items()}
# The functions of the base's given cases whose copy in another image
# differs and is given none of them: the 58503A 3704's and the Z3815A
# 4010's save_calibration_block, which compares the block with the time
# offset and TC before it writes, and installer_console_port_read, the
# primary's console_port_read on the installer's state, which prompts
# while installer_scpi_prompt_enabled is set.
NOT_GIVEN = {name: [] for name in ("save_calibration_block", "installer_console_port_read")}
# And the Z3815A's interval_wait_reading, which waits on its own tick
# (INTERRUPT_CASES' are the base's).
NOT_GIVEN_Z3815A = {**NOT_GIVEN, "interval_wait_reading": []}
GIVEN_CASES = {
    **COUNTER_GIVEN_CASES,
    **RECORD_CASES,
    **INTERRUPT_CASES,
    **STATUS_CASES,
    **HANDLER_CASES,
    **LIBRARY_CASES,
    **SUFFIX_CASES,
    **CONVERTER_CASES,
    "installer_flash_program_word": [{0: address, 1: word} for address in FLASH_ADDRESSES for word in FLASH_WORDS],
    "installer_scpi_query_DIAGnostic_ERASe": [
        {FLASH: erased_flash},
        {FLASH: lambda image: erased_flash(image, b"\x00\x01")},
    ],
    "installer_scpi_set_DIAGnostic_DOWNload": [
        {0: text_parameter(record), FLASH: erased_flash} for record in FLASH_RECORDS
    ],
    "scpi_parse_text": [{0: token} for token in TEXT_TOKENS],
    "os_report_process": REPORT_CASES,
    **{
        name: [{CALLERS_REGISTERS: registers} for registers in REPORT_REGISTERS]
        for name in ("os_process_report", "ps_word", "scpi_query_DIAGnostic_OS_PROCess")
    },
    **{
        reader: [
            {0: bytes(ECHO_LINE), 1: ECHO_LINE, PRESETS: {echo: routine}}
            for routine in (echo_routine, echo_none, 0)
            if routine is not None
            for _ in range(ECHO_CASES)
        ]
        for reader, (echo, echo_none) in IMAGE_ECHOES.items()
    },
    "gps_request_build": gps_request_build_cases(GPS_MESSAGES),
    "vsprintf": [{1: text, 2: arguments} for text, arguments in FORMATS],
    "installer_vsprintf": [{1: text, 2: arguments} for text, arguments in FORMATS],
    **validator_cases(LOAD_DOUBLE, LOAD_LONG),
    "pforth_define_word": [{2: arguments, 3: results} for arguments, results in WORD_COUNTS],
    "bytes_equal_n": [dict(enumerate(arguments)) for arguments in SAME_LENGTH],
    "double_to_digits": [{1: count, 5: mode} for count, mode in DIGITS],
    **{name: [{0: value} for value in SPECIAL_DOUBLES] for name in ("frexp", "modf", "sqrt", "log", "fabs")},
    "pow": [{0: base, 1: exponent} for base, exponent in POWERS],
    **{
        name: [{0: value} for value in EXPONENT_DOUBLES]
        for name in ("double_decimal_exponent", "double_decimal_exponent_rounded")
    },
    **{
        name: [
            {0: SETUP_MODEL, **{position + 1: value for position, value in enumerate(values)}}
            for values in SETUP_CASES
        ]
        for name in ("status_register_setup", "installer_status_register_setup")
    },
    "scpi_format_resolution_array_record": array_record_cases(EMPTY_IN_ROM, RETURN_ZERO),
    "scpi_real_to_text": [
        {
            **dict(enumerate(struct.unpack(">ii", struct.pack(">d", value)))),
            3: digits,
            4: width,
            5: resolution,
            6: point,
        }
        for value, digits, width, resolution, point in REAL_TEXT_CASES
    ],
    "qspi_queue_setup": [
        {0: device, 2: count, 3: read}
        for device in QSPI_DEVICES
        for count in (QSPI_COUNTS if device in (QSPI_FPGA_BLOCK, QSPI_FPGA_READBACK) else [3])
        for read in (0, 1)
    ],
}

# A variadic function's own cases, the arguments after its last declared
# one as its callers push them, laid out alike for both copies (each
# declared argument a 4-byte slot, so that the list starts where the
# image's code and gcc's va_start find it), by function: each case's
# declared arguments by position, as GIVEN_CASES gives them, and the
# list's bytes.  The generated cases come first, their list empty, so
# that UNDECLARED follows the declared arguments as for any function.
# The printf family formats through vsprintf, as its callers do
# (exception reports, debug and diagnostic output), into a buffer on its
# stack, 160 bytes for printf, printf_polled and sci_printf and 80 for
# the debug and SCI polled routines, then writes it: through the kernel
# to device 0 or the SCI, compared as a kernel call's text, or by
# polling the DUART or the SCI, compared as device writes.  So each is
# given vsprintf's FORMATS, and those whose output 80 bytes holds,
# without the long strings', zero padding's and %f's; sprintf and
# debug_sprintf into their buffer argument, compared as RAM.
VARIADIC = "..."
LONG_OUTPUTS = (b"%s|", b"%030d|", b"%f|%.20f|")
SHORT_FORMATS = [(text, listed) for text, listed in FORMATS if not text.startswith(LONG_OUTPUTS)]
PRINTF_CASES = [({0: text}, listed) for text, listed in FORMATS]
SHORT_PRINTF_CASES = [({0: text}, listed) for text, listed in SHORT_FORMATS]
SPRINTF_CASES = [({1: text}, listed) for text, listed in FORMATS]
# log_write's: an entry's format and the two longs log_append (0x31c42)
# keeps, as its callers pass them: an int (console.c's "Log test
# message %d", 0x13540), two (log_first_satellite's date and time,
# 0x31ba6), a double in both (the supplies' "... value: %.2f", 0x1b2a2)
# and none (log_event's, 0x319a2), which reads the caller's fill.
LOG_CASES = [
    ({0: b"Log test message %d\0"}, struct.pack(">i", 42)),
    ({0: b"%08x %08x\0"}, struct.pack(">II", 0x19970114, 0x12345678)),
    ({0: b"value: %.2f\0"}, struct.pack(">d", 4.875)),
    ({0: b"Power on\0"}, b""),
]
# float_raise's: a code, its high word the routine and its low word the
# cause (docs/firmware/libc.md, "Exception codes"; libc.h's
# LIBC_RAISE_CODE), with the operands its callers push after it.  The
# routine's bit 0 says a float's operand (one long), else a double's
# (two); its bit 7 the math library's, whose report gives the address
# pushed after the operands rather than its caller's caller's return
# (0x5e990 to 0x5e9a0), and which sets errno (0x5eafe).  An
# underflow's operands are an exponent's shift count and the mantissa,
# a double's in three longs and a float's in two (0x5e9a0, 0x5ea7c),
# with counts below 32, at 32, between, past 55 and, for a float, past
# 24, negative ones, which shift by their low 6 bits (0x5e9c4), and
# halfway cases that round to even, by the low long's last bit, not the
# high's (0x5ea44).  A NaN's operand
# with its quiet bit clear and set (0x5ec00, 0x5ec3e).  A code of no
# cause, and one with only the precision lost bit (0x5ec4c).
RAISE_ROUTINES = (0x0007, 0x0008, 0x0086, 0x0087, 0x0080)
RAISE_CAUSES = (0x4000, 0x2000, 0x1000, 0x0400, 0x0200, 0x0080, 0x0000)
RAISE_OPERANDS = struct.pack(">IIII", 0x7FF12345, 0x6789ABCD, 0x0FEDCBA9, 0x31400040)
RAISE_QUIET = struct.pack(">IIII", 0x7FFC0000, 0x00000001, 0x7FC00001, 0x31400040)
UNDERFLOW_COUNTS = (0, 1, 5, 31, 32, 33, 40, 55, 56, 24, 25, -1, -8)
UNDERFLOW_MANTISSAS = (
    (0x000FFFFF, 0xFFFFFFFF, 0x80000000),
    (0x00012345, 0x6789ABCD, 0x00000000),
    (0x00080000, 0x00000001, 0x80000001),
    (0x00000002, 0x00000001, 0x80000000),
    (0x00000001, 0x80000000, 0x00000000),
)
RAISE_CASES = [
    *(
        ({0: routine << 16 | cause}, operands)
        for routine in RAISE_ROUTINES
        for cause in RAISE_CAUSES
        for operands in (RAISE_OPERANDS, RAISE_QUIET)
    ),
    *(
        ({0: (routine | math) << 16 | 0x0800}, struct.pack(">i3I", count, *mantissa))
        for routine in (0x0007, 0x0008)
        for math in (0, 0x80)
        for count in UNDERFLOW_COUNTS
        for mantissa in UNDERFLOW_MANTISSAS
    ),
]
# And float_raise's at the stack's edge (STACK_EDGE), its caller's frame
# unmapped, each with only the operands the image reads, so that a read
# of another, or of the frame, faults: a float's quiet NaN, of two
# (0x5ebf8, 0x5ebfc), and a double's, of three (0x5ec34, 0x5ec3a), each
# returned at once; a code of no cause, of none (0x5e986); an operand
# error, an overflow and a divide by zero of the arithmetic, of their
# report address alone, which read no frame (0x5eb16, 0x5eb50, 0x5eba6);
# the math library's inexact and precision lost, likewise
# (0x5ebdc, 0x5ec4c); and the math library's float underflow, of its
# count, its mantissa and the half long its report address straddles
# into (0x5ea6c).
EDGE = {STACK_EDGE: True}
RAISE_EDGE_CASES = [
    ({0: 0x00074000, **EDGE}, struct.pack(">II", 0x7FC00001, 0x31400040)),
    ({0: 0x00084000, **EDGE}, struct.pack(">III", 0x7FF80000, 1, 0x31400040)),
    ({0: 0x00070000, **EDGE}, b""),
    *(({0: 0x0007 << 16 | cause, **EDGE}, struct.pack(">I", 0x31400040)) for cause in (0x2000, 0x1000, 0x0400)),
    *(({0: 0x0087 << 16 | cause, **EDGE}, struct.pack(">I", 0x31400040)) for cause in (0x0200, 0x0080)),
    ({0: 0x00870800, **EDGE}, struct.pack(">iIIH", 5, 0x00012345, 0x6789ABCD, 0x3140)),
]
RAISE_CASES = RAISE_CASES + RAISE_EDGE_CASES
VARIADIC_CASES = {
    **{name: PRINTF_CASES for name in ("printf", "printf_polled", "sci_printf", "installer_printf", "installer_printf_polled")},
    **{name: SHORT_PRINTF_CASES for name in ("debug_printf", "debug_printf_polled", "sci_printf_polled")},
    **{name: SPRINTF_CASES for name in ("sprintf", "debug_sprintf", "installer_sprintf")},
    "log_write": LOG_CASES,
    **{name: RAISE_CASES for name in ("float_raise", "installer_float_raise")},
}


def variadic_cases(function: "Function") -> list[tuple[str, int, dict]]:
    """A variadic function's own cases, as cases gives them, each with its
    list's bytes under VARIADIC."""
    return [
        (f"variadic case {i}", i % CASES, {**declared, VARIADIC: listed})
        for i, (declared, listed) in enumerate(VARIADIC_CASES.get(function.name, []) if function.variadic else [])
    ]


@dataclass(frozen=True)
class ScpiSession:
    """What the parser's loop is given in an SCPI session, as its image's
    SCPI task gives it (scpi_task, 0x2a04a; the installer's, 0x107902):
    the console port's object, as both its input and its output, the
    port's methods its table at the object's +4 (docs/firmware/
    structures.md, "The SCPI session"), the profile's; the pair of
    trees, main and common, whose two pointers it passes, the C's pair
    by name or, None, the profile's root lists; the arena's size, a segment
    of which it takes from the kernel; the console's line buffer and
    its two pointers, the next character and the line's end, which the
    port reads from before it reads the console; the language whose
    :SYSTem:LANGuage ends the session, its handler's -60; and the lines
    of its own, after SCPI_LINES."""

    trees: str | None
    arena: int
    line: str
    next: str
    end: str
    language: bytes
    lines: tuple[bytes, ...]


# The sessions' lines, each read whole as the console's one line, then
# its end, the line's NUL, and from then on an empty read of the
# console: the line starts at its buffer's second byte, the first a NUL,
# which console_port_read (0x29bce) gives again for each empty read, so
# that the input ends, -102, and the loop reads on.  Most end the
# session with :SYSTem:LANGuage, its handler's -60, which returns
# (0x29622): "PFORTH" the primary's (0x30680) and "PRIMARY" the
# installer's (installer::0x107f92), its session's language; one runs
# on to the kernel call limit.  A common query and a command, an error
# query, a bad header, a query with parameters, a compound message with
# a relative header, a query whose response queues and one in a message
# that errs part way.  Each session's own then: the primary's
# "PRIMARY", which its handler takes and does nothing for, a query
# whose handler fails, :LED:ALARm:MINor?'s undefined header
# (scpi_led.c), and the messages after it, and :DIAGnostic:TEST? on a
# choice; the installer's "PFORTH", an execution error, and its
# :DIAGnostic:TEST? on a test and past the last, alone and with units
# after it in its message, which its failure skips.  High-bit
# characters, which console_port_read gives sign extended (0x29c4e) and
# the lexer looks up so, before its class table (0x26f02): 0xfe, -2,
# whose byte there, 0x4e, is no class, 0xf8, -8, whose is a letter's,
# and 0xf9, -7, whose, 0xdf, a keyword reads as a signed class, below
# the letters' (0x26fc0): alone, in a keyword and after a number.  And
# from the session's tree, lines that reach every child list
# (tree_lines).
# LANGUAGE in a line is its session's language; ENDING the message
# that ends it.
LANGUAGE = b"{language}"
ENDING = b":SYST:LANG " + LANGUAGE + b"\n"
SCPI_LINES = [
    b"*IDN?\n" + ENDING,
    b":SYST:ERR?;*CLS\n" + ENDING,
    b":NOPE:HEADER 3\n:SYST:ERR?\n" + ENDING,
    b"*ESE 16;*ESE?;:STAT:OPER:ENAB 4;ENAB?\n" + ENDING,
    b":SYST:ERR?;:SYST:ERR?;*OPC?\n" + ENDING,
    b":SYST:LANG " + LANGUAGE + b" junk\n" + ENDING,
    b"*IDN?;\n",
    b"\xfe\n:SYST:ERR?\n" + ENDING,
    b"\xf8\n:SYST:ERR?\n" + ENDING,
    b"*ES\xf8E?\n:SYST:ERR?;*ES\xfeE?\n:SYST:ERR?;*ESE\xf9?\n:SYST:ERR?\n" + ENDING,
    b"*ESE 1\xfe;*ESE 2\xf8\n:SYST:ERR?\n" + ENDING,
]
SCPI_SESSIONS = {
    "scpi_interpret": ScpiSession(
        None,
        2000,
        "scpi_input_line",
        "scpi_input_next",
        "scpi_input_end",
        b'"PFORTH"',
        (
            b':SYST:LANG "PRIMARY";:SYST:LANG?\n:SYST:ERR?\n' + ENDING,
            b":LED:ALAR:MIN?;*IDN?\n:SYST:ERR?;:LED:ALAR:USER?\n:SYST:ERR?\n" + ENDING,
            b":DIAG:TEST? EEPR;:DIAG:TEST:RES?\n:SYST:ERR?\n" + ENDING,
        ),
    ),
    "installer_scpi_interpret": ScpiSession(
        "installer_scpi_trees",
        1500,
        "installer_scpi_input_line",
        "installer_scpi_input_next",
        "installer_scpi_input_end",
        b'"PRIMARY"',
        (
            b':SYST:LANG "PFORTH"\n:SYST:ERR?;:SYST:LANG?\n' + ENDING,
            b":DIAG:TEST? 3\n:DIAG:TEST? 9\n:SYST:ERR?\n" + ENDING,
            b":DIAG:TEST? 9;:SYST:ERR?;*IDN?\n:SYST:ERR?\n" + ENDING,
        ),
    ),
}
# The lines that reach every child list of a session's tree below its
# root: for each, the header of the shortest path to it and one of its
# nodes, one with a query handler, at the node's +0x12, where it has
# one, which a header resolved otherwise does not reach, the query, then
# the error query, each a message of its own, whose
# response the session writes, so that a header the C's tree resolves
# otherwise shows in what it writes; short forms, and every
# TREE_LONG_EVERY'th long, each line as many as the console's line
# buffer holds (scpi_input_line, 256 bytes with its two NULs).
TREE_QUERY = b"?\n:SYST:ERR?\n"
QUERY_HANDLER = HANDLER_ENTRIES + HANDLER_ENTRY_SIZE
TREE_LONG_EVERY = 3
LINE_BUFFER = 256
LINE_ROOM = LINE_BUFFER - 2
# A session case's line's index, among its arguments; and the
# positions of the loop's arguments a session gives.
SCPI_LINE = "line"
SESSION_INPUT, SESSION_OUTPUT, SESSION_ARENA, SESSION_SIZE, SESSION_MAIN, SESSION_COMMON = range(6)
SESSION_UNREAD = (7, 8)


def tree_lines(m: Machine) -> list[bytes]:
    """The lines that reach each child list of the machine's tree below
    its root, each ended by ENDING."""
    longest = ENDING.replace(LANGUAGE, max((s.language for s in SCPI_SESSIONS.values()), key=len))
    messages = []
    for i, tree in enumerate(t for t in m.tree if t.path):
        queried = [node for node in tree.nodes if pointer_at(m, node + QUERY_HANDLER)] or tree.nodes
        node = queried[i % len(queried)]
        messages.append(header_text(m, tree.path + (node,), i % TREE_LONG_EVERY == 0) + TREE_QUERY)
    lines, line = [], b""
    for message in messages:
        if line and len(line) + len(message) + len(longest) > LINE_ROOM:
            lines.append(line + ENDING)
            line = b""
        line += message
    return lines + ([line + ENDING] if line else [])


def session_lines(m: Machine, function: "Function") -> list[bytes]:
    """A session's lines: SCPI_LINES, its own and its tree's, in each
    its language for LANGUAGE."""
    session = SCPI_SESSIONS[function.name]
    return [line.replace(LANGUAGE, session.language) for line in SCPI_LINES + list(session.lines) + tree_lines(m)]


def session_cases(m: Machine, function: "Function", linked_symbols: dict[str, int]) -> list[tuple[str, int, dict]]:
    """The parser's loop's session cases, as cases gives them, each with
    its line's index under SCPI_LINE; none where the image's C names
    none of its console's line buffer and pointers, or of its pair of
    trees, as an image not yet labeled so may not."""
    session = SCPI_SESSIONS.get(function.name)
    if session is None:
        return []
    named = (session.line, session.next, session.end) + (() if session.trees is None else (session.trees,))
    if not all(name in linked_symbols for name in named):
        return []
    return [(f"session case {i}", i % CASES, {SCPI_LINE: i}) for i in range(len(session_lines(m, function)))]


# The pForth console's sessions, as console_init (0x12faa) starts the
# console: console_init, which sets up the dictionary and its words
# (pforth_define_word, diag_words_define) and creates the console's
# task, then that task, console_task (0x1aedc), entered as the kernel
# starts it, its stack the words the create call gave it, in user mode,
# from registers as a call starts with them.  The console reads the
# phrase console_init gives its string device, then, once `0 !iodev`
# has made it so, its port, DUART channel B (console.md, "Getting to
# it"), each a line of the session's in turn (harness.READ); the
# session ends when its lines run out or its task deletes itself, as
# halt has it do.  Each line's read records the console's dictionary
# pointer, data stack and user area and the port's echo of the line
# (harness.ConsoleLine), and its output is the trap #4 writes, so that
# both are compared as effects,
# and the dictionary, in RAM, as RAM.  By function, the task's entry.
CONSOLE_SESSIONS = {"console_init": "console_task"}
# Service 1, create, whose counted block is the new task's stack.
KERNEL_TRAP = TRAP_VECTORS + 3
CREATE = 1
# Each session's lines: arithmetic and printing, the stack left; a
# definition, used and leaving its result; an unknown word part way
# along a line, which aborts and empties the stack, then a word with
# the stack empty; numbers in hex, one negative; diagnostic words of
# each argument and result count that touches neither the GPS engine,
# a reset nor a halt: adc_read (1 -- 1), hdac_all (6 -- 0), then
# hdac_write (2 -- 0), a value and a channel, after it, so that the
# DAC's shadow it leaves shows in RAM as well as in the QSPI write's
# transfers (harness.qspi_write_data), dmes_all (1 -- 0), pr_efc (0 -- 0),
# mem_rep,
# console_init's own (0 -- 0), and wr_eeprom (2 -- 1), whose write the
# harness's EEPROM takes; the word list; and halt, after which the
# session's last line is never read.
CONSOLE_LINES = [
    (b"1 2 + .", b"hex 10 . ff .", b"decimal -7 3 * . 7 2 /mod . .", b"2 3 .s"),
    (b": sq dup * ;", b"3 sq .", b"5 sq"),
    (b"1 2 nope 3", b"."),
    (b"hex 1a2b 10 + .", b"-ff . decimal"),
    (b"3 adc_read .", b"1 2 3 4 5 6 hdac_all", b"9 2 hdac_write", b"1 dmes_all 0 dmes_all", b"pr_efc mem_rep"),
    (b"hex 400100 5a wr_eeprom . decimal",),
    (b"words",),
    (b"1 2", b"halt", b"3 ."),
]
CONSOLE_SESSION = "console session"


def console_session_cases(function: "Function") -> list[tuple[str, int, dict]]:
    """A console session's cases, as cases gives them, each with its
    session's index under CONSOLE_SESSION."""
    if function.name not in CONSOLE_SESSIONS:
        return []
    return [(f"console session {i}", i % CASES, {CONSOLE_SESSION: i}) for i in range(len(CONSOLE_LINES))]


def console_task_run(m: Machine, first: Run, task: int) -> Run:
    """A console session's task's run after the call that created it,
    the two calls' effects as one; the call alone if it did not return
    or created no task."""
    if first.stopped is not None or first.ended is not None:
        return first
    create = next(
        (e for e in first.effects if isinstance(e, KernelCall) and e.trap == KERNEL_TRAP and e.service == CREATE),
        None,
    )
    if create is None:
        return first
    for name, register in REGISTERS.items():
        m.uc.reg_write(register, STARTING[name])
    starting_status, m.starting_status = m.starting_status, TASK_STATUS
    run = Run(m, task, create.data[COUNT.size :], entry=True)
    run.floor = pointer_at(m, run.registers["A5"] - USER_STACK_BASE)
    m.starting_status = starting_status
    run.effects = first.effects + run.effects
    return run


# The routines that step into a node's children, which run a case for
# every node of their profile's tree that has them (harness.tree_steps),
# by the index of its step among their arguments.
STEPPERS = {"scpi_header_step", "installer_scpi_header_step"}
TREE_STEP = "step"


def step_cases(m: Machine, function: "Function") -> list[tuple[str, int, dict]]:
    """A stepper's cases, as cases gives them, each with its step's index
    under TREE_STEP."""
    if function.name not in STEPPERS:
        return []
    return [(f"step case {i}", i % CASES, {TREE_STEP: i}) for i in range(len(tree_steps(m)))]


def scpi_session(m: Machine, function: "Function", linked_symbols: dict[str, int], line: int) -> dict[int, int]:
    """Puts a session's line in the console's buffer, its pointers at it;
    the loop's arguments, by position, the status model's left as the
    generated case's."""
    session = SCPI_SESSIONS[function.name]
    text = session_lines(m, function)[line]
    buffer = linked_symbols[session.line]
    m.uc.mem_write(buffer, b"\0" + text + b"\0")
    m.uc.mem_write(linked_symbols[session.next], struct.pack(">I", buffer + 1))
    m.uc.mem_write(linked_symbols[session.end], struct.pack(">I", buffer + 1 + len(text)))
    scpi = m.profile.scpi
    trees = (scpi.main_tree, scpi.common_list)
    if session.trees is not None:
        # the image's scpi_tree_pair, the C's twin, gcc's copy given the
        # C's lists: the common list first, then the main tree
        at = m.twins.image_address(linked_symbols[session.trees])
        trees = (pointer_at(m, at + POINTER_SIZE), pointer_at(m, at))
    return {
        SESSION_INPUT: scpi.console_port,
        SESSION_OUTPUT: scpi.console_port,
        SESSION_ARENA: area(m, bytes(session.arena)),
        SESSION_SIZE: session.arena,
        SESSION_MAIN: trees[0],
        SESSION_COMMON: trees[1],
        **{position: 0 for position in SESSION_UNREAD},
    }


# Arguments a routine indexes a table by unchecked, which every caller
# passes in range, by position: how many values each takes, the case
# picking one.  fpga_set_bit's bit and fpga_set_field's field, an
# fpga_bit_index, are one of the six entries of fpga_bit_table
# (0x41310), which the hand-written fpga.c keeps as fpga_bits; what
# follows the image's table is not the C's.  scpi_char_upper's
# character is a port's value, 0 to 0x105, which indexes the lexer's
# classes (0x277ec), as scpi_engine_class keeps them.  The header
# resolvers' query, 0 the setter or 1 the query, indexes the node's two
# handlers (scpi_header_resolve, 0x27d80; scpi_common_header_resolve,
# 0x27f10), and the installer's copies': past them lies what follows the
# node, the image's or the generated tree's (docs/scpi/README.md, "As C").
IN_RANGE = {
    **{name: {0: 6} for name in ("fpga_set_bit", "fpga_set_field")},
    "scpi_char_upper": {0: 0x106},
    "scpi_header_resolve": {6: 2},
    "scpi_common_header_resolve": {5: 2},
    "installer_scpi_header_resolve": {6: 2},
    "installer_scpi_common_header_resolve": {5: 2},
}
# Another image's own counts for IN_RANGE's arguments: the Z3817A 3752's
# fpga_bit_table has five entries (0x45d7a), past which its fpga_set_bit
# and fpga_set_field read the code after it.
IMAGE_IN_RANGE = {
    "z3817a-3752": {name: {0: 5} for name in ("fpga_set_bit", "fpga_set_field")},
}


@dataclass(frozen=True)
class Preset:
    """A variable whose value a case draws from a set: an lvalue of the
    exported C, a global or a field of one (`pll.fine_state`), whose
    address and size gcc finds (probes), and the values, each a float
    for a float or double, else an int laid out in the lvalue's bytes."""

    target: str
    values: tuple[int | float, ...]


@dataclass(frozen=True)
class Hook:
    """A routine whose calls, in both copies, the harness answers in its
    place, so that a case chooses what it gives back: its name, the
    variables it leaves at each call, with values drawn once for a case,
    D0's values, drawn so (none, D0 left), and whether each call's
    arguments, as its prototype lays them, are recorded as an effect,
    compared in order with the kernel calls."""

    routine: str
    leaves: tuple[Preset, ...] = ()
    results: tuple[int, ...] = ()
    recorded: bool = True


@dataclass(frozen=True)
class States:
    """A function's states, which the zeroed RAM a generated case starts
    with never reaches: the variables every case of it starts with at a
    value drawn for the case, the routines hooked in each, and how many
    cases of its own it runs past the generated ones, each from a
    generated case in turn."""

    presets: tuple[Preset, ...] = ()
    hooks: tuple[Hook, ...] = ()
    cases: int = 0


# The pll stage functions' states (applications/pll, docs/firmware/pll.md):
# small time intervals and frequency offsets, in seconds, as the
# counter's readings and the fit's sums hold them, and the sums of the
# fit's counts over fine's 50 readings, 1275 and 42925 for its squares;
# a stage's coded states and values past its last, which its switch
# takes as none.
SMALL = (0.0, 1e-8, -2e-7, 5e-9, -1e-10, 3e-7, 1.5e-9, -2.5e-9)
READINGS = (0.0, 1e-8, 2.5e-7, -1.5e-7, 5e-7, -4e-7, 1.2e-7, -5e-8)
FIT_SUMS = SMALL + (10.0, 1275.0, 42925.0)
# counter_read (0x40d34) answered with a reading drawn for the case, its
# validity mostly set, which interval_wait_reading takes from
# interval_reading; and log_event (0x319a2), whose code each call
# records, not run, so that what is compared is the code, not the log.
COUNTER_READ = Hook(
    "counter_read",
    leaves=(Preset("interval_reading.value", READINGS), Preset("interval_reading.valid", (0, 1, 1, 1))),
)
LOG_EVENT = Hook("log_event")
PLL_HOOKS = (COUNTER_READ, LOG_EVENT)
PLL_CASES = 2000
# The last functions' states: for the variadic (VARIADIC_CASES),
# whether debug output is on, which the debug routines test first, and
# gps_debug_status's debug_sprintf (EMPTY_PART);
# float_raise's trap enables, float_control's high byte's bits 1 to 5 by
# cause and its low byte's bit 7 (0x5eae2 to 0x5ec70), none, one and
# all; and the event log's cursor, which log_append (0x31c42) writes
# through, at log_write_page's entries (0x101d4e, +4), its first, its
# second and its last, after which it moves to the next page, with the
# log's lock set up, its exchange an id as a create leaves one, or not,
# which mutex_acquire fails (smartclock.h, mutex): log_write's and
# log_event's, whose entry keeps its format's address, which its page
# takes to the EEPROM, where effects compare bytes, not pointers, so
# that the C must log the image's own text (log.c).  And the
# SCI's flow, whose XOFF bits sci_transmit_allowed tests (0x2031c), the
# one received and the one sent (duart.h).
DEBUG_ENABLED = Preset("debug_enabled", (0, 1))
FLOAT_CONTROLS = (0, 0, 0x0200, 0x0400, 0x0800, 0x1000, 0x2000, 0x0080, 0x3E80)
LOG_CURSORS = (0x101D52, 0x101D66, 0x101D7A)
# The Z3801A 3543's, at its log_write_page's entries (0x101d4c, +4).
LOG_CURSORS_Z3801A = (0x101D50, 0x101D64, 0x101D78)
# The Z3817A 3752's, at its log_write_page's entries (0x10226a, +4).
LOG_CURSORS_Z3817A = (0x10226E, 0x102282, 0x102296)
LOCK_EXCHANGES = (0x6B000010, 0x6B000010, 0)
SCI_FLOWS = (0, 0x20, 0x80, 0xA0)
STRAGGLER_STATES = {
    **{
        name: States(presets=(DEBUG_ENABLED,))
        for name in ("debug_printf", "debug_printf_polled", "debug_sprintf", "gps_debug_status")
    },
    **{
        name: States(presets=(Preset("log_cursor", LOG_CURSORS), Preset("log_mutex.exchange", LOCK_EXCHANGES)))
        for name in ("log_write", "log_event")
    },
    "sci_transmit_allowed": States(presets=(Preset("sci.flow", SCI_FLOWS),)),
    "float_raise": States(presets=(Preset("float_control", FLOAT_CONTROLS),)),
    "installer_float_raise": States(presets=(Preset("installer_float_control", FLOAT_CONTROLS),)),
}
# counter_read, counter_calibrate and counter_arm (ti.c) armed, as counter_init
# leaves the reading (COUNTER_CASES), the counter ready or not and the
# interpolator calibrated or not.
COUNTER_STATES = States(
    presets=(
        Preset("interval_reading.armed", (1, 1, 0)),
        Preset("interpolator_calibration_state.calibrated", (1, 0)),
    ),
    hooks=(
        Hook("counter_wait_ready", results=(1, 1, 0)),
        Hook("interpolator_calibrate", results=(1, 0)),
        Hook("interpolator_verify", results=(1, 0)),
    ),
)
# interpolator_prepare's configuration, counter_configuration, asking
# for its registers or not (counter_arm's are COUNTER_GIVEN_CASES').
INTERPOLATOR_PREPARE_STATES = States(
    presets=(
        Preset("counter_configuration.registers_mode", (1, 0)),
        Preset("counter_configuration.registers_state", (2, 2, 1)),
    ),
)
STATES = {
    **{name: COUNTER_STATES for name in ("counter_read", "counter_calibrate", "counter_arm")},
    "interpolator_prepare": INTERPOLATOR_PREPARE_STATES,
    "fine": States(
        presets=(
            Preset("pll.stage", (1, 2, 3, 4)),
            Preset("pll.fine_state", tuple(range(11)) + (0x80, 0xFF)),
            Preset("pll.fine_index", (0, 1, 2, 3, 4, 49, 50, 51, 200)),
            Preset("pll.fine_retries", (0, 5, 8, 9, 10)),
            Preset("pll.fine_d", SMALL),
            Preset("pll.sync_requested", (0, 0, 1)),
            Preset("pll.holdover_duration", (0, 10, 100000)),
            Preset("pll.frequency_tolerance", (1e-8, 0.0, 1e-9)),
            Preset("pll.efc", (0.0, 12345.0, -500.0)),
            Preset("interval_reading.value", READINGS),
            Preset("interval_reading.valid", (0, 1, 1)),
            Preset("pll_debug_enabled", (0, 1)),
            Preset("nv_settings.holdover_limit_threshold", (1e-6, 1e-9)),
            Preset("nv_settings.holdover_limit_state", (0, 1)),
            Preset("nv_settings.holdover_measurement_threshold", (0, 50, 1000000)),
            Preset("efc_gains.reciprocal_fine", (1e10, -3e9)),
            *(
                Preset(name, FIT_SUMS)
                for name in (
                    "fine_sum_i",
                    "fine_sum_i_squared",
                    "fine_sum_ti",
                    "fine_sum_i_ti",
                    "fine_y0",
                    "fine_x0",
                    "fine_ti_max",
                )
            ),
            *(Preset(f"fine_readings[{index}]", SMALL) for index in range(3)),
        ),
        hooks=PLL_HOOKS,
        cases=PLL_CASES,
    ),
    "enter_holdover": States(
        presets=(
            Preset("pll.stage", (0, 1, 2, 3, 4)),
            Preset("pll.hold_causes", (0, 1, 2, 3, 4, 8, 0x10, 0x20, 0x40, 0x80, 0xC0, 0x22, 0x60)),
            Preset("pll.holdover_duration", (0, 0, 7)),
            Preset("gps.status_reported", tuple(range(11)) + (0x80, 0xFC)),
            Preset("gps_pps_dead", (0, 1)),
            Preset("holdover_recovery_auto", (0, 1)),
        ),
        hooks=PLL_HOOKS,
        cases=PLL_CASES,
    ),
    # root_task's choice of the SCPI task or the pForth console (0x131ca)
    "root_task": States(presets=(Preset("nv_settings.pforth_at_startup", (0, 1)),)),
    **STRAGGLER_STATES,
}
# The Z3815A 4010's loop stages and readings (its pll_stages.c and
# pll_reading.c): each stage's states and one past, its counts about
# their bounds, the hold causes, the GPS reference good, questionable
# and not, each 1PPS dead and not, a reading replaced or not, the TI
# limit on and off, overridden or not, and DAT_0010218b, which outside
# the n3 lock pll stage (6) replaces the readings, set and clear; and,
# for enter_holdover, the reference external or not and each GPS
# reference state, and the cells output_inactive_request_bit_11 tests; the uptime
# about the start-up loop's 7200 s and the aging fit's first entry; and
# startup_pll and powerup, each
# state and one past.  fine keeps STATES' scenario with this image's
# cells beside it.
# counter_read answered as for the other images, and its readings out to
# 20 us too, which fine's sync tests at 10 us; and log_event, as
# LOG_EVENT.
Z3815A_COUNTER_READ = Hook(
    "counter_read",
    leaves=(
        Preset("interval_reading.value", READINGS + (5e-6, -2e-5)),
        Preset("interval_reading.valid", (0, 1, 1, 1)),
    ),
)
Z3815A_PLL_HOOKS = (Z3815A_COUNTER_READ, LOG_EVENT)
Z3815A_COMMON = (
    Preset("pll.stage", (3, 4, 5, 6, 7, 1)),
    Preset("pll.hold_causes", (0, 0, 2, 4, 8, 0x10, 0x80, 0x81)),
    Preset("gps_reference_good", (0, 1, 1)),
    Preset("gps_reference_questionable", (0, 0, 1)),
    Preset("gps_pps_dead", (0, 0, 1)),
    Preset("internal_pps_dead", (0, 0, 1)),
    Preset("interval_reading_replaced", (0, 1)),
    Preset("interval_armed_without_pps", (0, 1)),
    Preset("DAT_0010218b", (0, 0, 0, 1)),
    Preset("pll_debug_enabled", (0, 1)),
    Preset("debug_enabled", (0, 1)),
    Preset("nv_settings.holdover_limit_threshold", (1e-6, 1e-9)),
    Preset("nv_settings.holdover_limit_state", (0, 1)),
    Preset("nv_settings.holdover_measurement_threshold", (0, 50, 1000000)),
    Preset("pll.ti_limit_override", (0, 1)),
    Preset("pll.frequency_tolerance", (1e-8, 0.0, 1e-9)),
    Preset("pll.efc", (0.0, 12345.0, 524288.0)),
    Preset("clock_gps_mismatch", (0, 1)),
    Preset("switch_word_bit_0", (0, 1)),
    Preset("switch_word_bit_11", (0, 1)),
    Preset("switch_word_bit_7", (0, 1)),
)
Z3815A_LOOP_STATES = {
    name: States(presets=Z3815A_COMMON + presets, hooks=Z3815A_PLL_HOOKS, cases=PLL_CASES)
    for name, presets in {
        "holdover": (
            Preset("pll.holdover_state", (0, 1, 1, 2, 0x80)),
            Preset("pll.holdover_duration", (0, 10, 100000)),
            Preset("holdover_steering.seconds", (0, 8, 9)),
            Preset("holdover_steering.readings", (0, 5, 9)),
            Preset("holdover_steering.ti_largest", SMALL),
            Preset("pll.manual", (0, 1)),
            Preset("pll.frequency_adjusted", (0, 1)),
            Preset("pll.aging_state", (0, 3)),
            Preset("pll.efc_history.stop_ptr", (0, 31, 5)),
            Preset("pll.efc_history.fit_a", (0.0, 500000.0)),
            Preset("pll.efc_history.fit_b", (0.0, 3.0)),
            Preset("pll.efc_history.fit_c", (0.0, -2.0)),
            Preset("aging_curve.a", (0.0, 500000.0)),
            Preset("aging_curve.b", (0.0, 3.0)),
            Preset("aging_curve.c", (0.0, -2.0)),
            Preset("clock.uptime", (0, 2700, 100000)),
            Preset("pll.predicted_uncertainty", (1e-9, 0.0)),
            Preset("gps.command_monitor_failed", (0, 1)),
        ),
        "holdover_recovery": (
            Preset("pll.recovery_state", (0, 1, 2, 3, 4, 5, 6, 0x80)),
            Preset("pll.recovery_countdown", (0, 1, 2)),
            Preset("holdover_steering.seconds", (0, 5, 6)),
            Preset("pll.fine_state", (0, 9, 6)),
            Preset("pll.fine_index", (0, 1)),
            Preset("pll.leap_ack_again", (0, 1)),
            Preset("pll.leap_ack_requested", (0, 1)),
            Preset("pll.leap_ack_done", (0, 1)),
            Preset("pll.holdover_duration", (0, 250, 251, 100000)),
            Preset("gps.command_monitor_failed", (0, 1)),
        ),
        "pll_normal": (
            Preset("pll.normal_state", (0, 1, 1, 2, 0x80)),
            Preset("loop_steering.seconds", (0, 8, 9)),
            Preset("loop_steering.readings", (0, 3)),
            Preset("loop_steering.measurement_failed", (0, 1)),
            Preset("loop_steering.field_0f", (0, 1)),
            Preset("loop_steering.ti_sum", SMALL),
            Preset("loop_steering.integral", (0.0, 25.0)),
            Preset("loop_steering.proportional_gain", (1e9, 3e8)),
            Preset("loop_steering.integral_gain", (1e6, 2e5)),
            Preset("loop_steering.filter_weight", (0.03, 0.2)),
            Preset("pll.loop_time_constant", (150.0, 1000.0)),
            Preset("pll.loop_efc_step", (0.0, 5e-3, -1.0)),
            Preset("aging_curve.b", (0.0, 1e-3, -2e-3)),
            Preset("aging_curve.c", (0.0, -1e-3)),
            Preset("pll.ti_filtered", SMALL),
            Preset("efc_gains.step_limit", (1e-3, 0.5)),
            Preset("max_loop_time", (1000.0, 300.0)),
            Preset("ti_setpoint", (0.0, 1e-8)),
        ),
        # startup_pll and powerup, by their names here
        "startup_pll": (
            Preset("pll.startup_state", (0, 1, 2, 3, 3, 4, 0x80)),
            Preset("loop_steering.settled_blocks", (0, 14, 15)),
            Preset("clock.uptime", (0, 7200, 7201, 100000)),
            Preset("loop_steering.seconds", (0, 8, 9)),
            Preset("loop_steering.readings", (0, 3)),
            Preset("loop_steering.ti_sum", SMALL + (6e-7, -6e-7)),
            Preset("pll.loop_time_constant", (150.0, 995.0, 1000.0)),
            Preset("pll.aging_state", (0, 3)),
            Preset("max_loop_time", (1000.0, 300.0)),
            Preset("efc_gains.step_limit", (1e-3, 0.5)),
        ),
        "powerup": (
            Preset("pll.powerup_state", (0, 1, 2, 3, 4, 5, 6, 7, 8, 0x80)),
            Preset("pll.powerup_countdown", (0, 1, 2)),
            Preset("pll.powerup_reference_lost", (0, 1)),
            Preset("pll.gps_init_state", (0, 15, 16)),
            Preset("pll.coarse_state", (3, 4)),
            Preset("pll.fine_state", (0, 9)),
            Preset("pll.fine_index", (0, 1)),
            Preset("pll.leap_ack_requested", (0, 1)),
            Preset("pll.leap_ack_done", (0, 1)),
            Preset("pll.external_reference", (0, 1)),
            Preset("gps.references[0].valid", (0, 1)),
            Preset("first_satellite_logged", (0, 1)),
            Preset("gps_reference_logged", (0, 1)),
            # set, so that powerup sets no clock: clock_set and
            # lifetime_update fail cases of their own
            Preset("DAT_0010218b", (1,)),
        ),
        "coarse": (
            Preset("pll.coarse_state", (0, 1, 2, 3, 4, 4, 5, 0x80)),
            Preset("pll.fine_retries", (0, 9, 10)),
            Preset("pll.fine_d", SMALL),
        ),
        "interval_wait_reading": (),
        "interval_next_reading": (),
        "gps_reference_update": (Preset("gps.references[0].valid", (0, 1, 2, 3)),),
        "holdover_within_limit": (
            Preset("pll.holdover_duration", (0, 50, 51, 100000)),
            Preset("pll.frequency_adjusted", (0, 1)),
        ),
        "holdover_duration_count": (
            Preset("pll.holdover_duration", (0, 99, 100, 101)),
            Preset("nv_settings.holdover_status_threshold", (100, 0)),
            Preset("holdover_duration_exceeded", (0, 1)),
        ),
        "enter_holdover": (
            Preset("pll.holdover_duration", (0, 0, 7)),
            Preset("pll.external_reference", (0, 1)),
            Preset("gps.references[0].state", (0, 1, 2, 3, 4, 5, 0x80)),
            Preset("holdover_recovery_auto", (0, 1)),
        ),
        "efc_mode_done": (Preset("diag_efc_mode", (0, 1, 2, 3)), Preset("pll.efc_control_passes", (0, 601))),
        "efc_mode_expire": (Preset("diag_efc_mode", (0, 1, 2, 3)), Preset("pll.efc_control_passes", (0, 599, 600, 601))),
    }.items()
} | {
    "fine": States(
        presets=STATES["fine"].presets + Z3815A_COMMON[2:],
        hooks=Z3815A_PLL_HOOKS,
        cases=STATES["fine"].cases,
    ),
}
# Another image's own States in place of STATES' of the same name, each
# preset's values that image's.
# The Z3815A 4010's pllp_task: started afresh or after a reboot, in
# power-up past waiting for GPS or not, with the switch word's bit 4
# copy, a self-test, a restart, the normal loop's mean and an awaited
# acknowledgment drawn, and each stage's routine answered with a result
# drawn for the case, enter_holdover leaving the holdover stage, so that
# the passes walk the stages and their exits, the settings server
# answered, whose message the kernel's answers leave a pointer the
# harness does not map, and power-up's self-tests, whose GPS self-test
# the harness never answers, so that a power-up never reached the loop;
# and the stage's readers in each stage and one
# past, with the flags they test, standby_from_unlock's standby answered and
# the log's event recorded; the clock's check
# against GPS with its flags, the clock set's lifetime_update, which
# faults in gcc's copy (the figures' failures), answered.
PLLP_STAGE_HOOKS_Z3815A = (
    *(Hook(name, results=(0, 1, 2)) for name in ("powerup", "pll_reference_check", "holdover_recovery", "startup_pll")),
    Hook("holdover", results=(0, 1)),
    Hook("pll_normal", results=(0, 0, 2)),
    Hook("n3_lock_pll", results=(0, 0, 2)),
    Hook("gps_diag_sequence", results=(0, 0, 1)),
    Hook("enter_holdover", leaves=(Preset("pll.stage", (3,)),)),
    Hook("pll_scpi_receive"),
    Hook("selftest_print_failure"),
)
PLLP_TASK_STATES_Z3815A = States(
    presets=(
        Preset("pll.stage", (0, 1, 2, 7)),
        Preset("pll.powerup_state", (0, 3, 4, 5)),
        Preset("switch_word_bit_4_copy", (0, 0, 1)),
        Preset("preserved_region.restore", (0, 1)),
        Preset("selftest_requested", (0, 0, 0, 1)),
        Preset("pll_restart_requested", (0, 0, 1)),
        Preset("interval_mean_flag", (0, 1)),
        Preset("interval_mean", (0.0, 1e-6, -1e-6, 1e-7)),
        Preset("gps_ack_awaited", (0, 1, 5)),
        Preset("gps_ack_ready", (0, 1)),
        Preset("nv_settings.douglas_state_powerup", (0, 1)),
    ),
    hooks=PLLP_STAGE_HOOKS_Z3815A,
    cases=200,
)
# pllp_task's walk through the stages, each case a path its routines'
# results, fixed, make, from power-up or power-up recovery, every
# transition pllp_step_stage makes taken in one: power-up done, failed
# and on to n3 lock pll; power-up recovery done to holdover recovery or
# n3 lock pll, and failed through enter_holdover to holdover; holdover
# done; holdover recovery done and failed; start-up done and failed;
# normal failed and back to start-up past 500 ns either way, its mean
# flag left set by pll_normal, which sets it; n3 lock pll failed to
# holdover recovery; and each steering or holding stage to n3 lock pll
# by the switch word's bit 4 copy, each run to its PLLP_PASSES_Z3815A-th
# pass (IMAGE_COUNTED_LOOPS).  A routine not named runs on
# (PLL_STAGE_RESULT_RUNNING).  pllp_task clears the self-test's and the
# restart's requests and the mean's flag as it starts, so that no
# preset of them reaches its loop.
PLLP_WALK_SET_UP_Z3815A = {
    "pll.powerup_state": 3, "switch_word_bit_4_copy": 0, "preserved_region.restore": 0, "gps_ack_awaited": 0,
    "gps_ack_ready": 0, "nv_settings.douglas_state_powerup": 0,
}
PLLP_WALK_ROUTINES_Z3815A = (
    "powerup", "pll_reference_check", "holdover", "holdover_recovery", "startup_pll", "pll_normal", "n3_lock_pll",
)
PLLP_WALK_MEAN_Z3815A = 1e-6
PLLP_WALKS_Z3815A = (
    ({"pll.stage": 1}, {"powerup": 1, "startup_pll": 1}, {}),
    ({"pll.stage": 1}, {"powerup": 1, "startup_pll": 1},
     {"interval_mean_flag": 1, "interval_mean": PLLP_WALK_MEAN_Z3815A}),
    ({"pll.stage": 1}, {"powerup": 1, "startup_pll": 1},
     {"interval_mean_flag": 1, "interval_mean": -PLLP_WALK_MEAN_Z3815A}),
    ({"pll.stage": 1}, {"powerup": 1, "startup_pll": 1, "pll_normal": 2, "holdover": 1}, {}),
    ({"pll.stage": 1}, {"powerup": 2}, {}),
    ({"pll.stage": 1, "pll.powerup_state": 4, "switch_word_bit_4_copy": 1}, {}, {}),
    ({"pll.stage": 1}, {"powerup": 1, "startup_pll": 2, "holdover": 1, "holdover_recovery": 1}, {}),
    ({"pll.stage": 2}, {"pll_reference_check": 1, "holdover_recovery": 1, "startup_pll": 1}, {}),
    ({"pll.stage": 2}, {"pll_reference_check": 1, "holdover_recovery": 2, "holdover": 1}, {}),
    ({"pll.stage": 2}, {"pll_reference_check": 2, "holdover": 1, "holdover_recovery": 1}, {}),
    ({"pll.stage": 2, "switch_word_bit_4_copy": 1}, {"pll_reference_check": 1, "n3_lock_pll": 2}, {}),
    ({"pll.stage": 2, "switch_word_bit_4_copy": 1}, {"pll_reference_check": 2}, {}),
    ({"pll.stage": 1, "switch_word_bit_4_copy": 1}, {"powerup": 1}, {}),
)
PLLP_WALK_CASES_Z3815A = [
    {PRESETS: {**PLLP_WALK_SET_UP_Z3815A, **presets},
     HOOK_RESULTS: {**{name: 0 for name in PLLP_WALK_ROUTINES_Z3815A}, **results},
     HOOK_LEAVES: {"pll_normal": normal}}
    for presets, results, normal in PLLP_WALKS_Z3815A
]
PLLP_STAGE_STATES_Z3815A = States(
    presets=(
        Preset("pll.stage", tuple(range(12))),
        Preset("pll.manual", (0, 1)),
        Preset("pll.led_gps_lock", (0, 1)),
        Preset("pll.led_holdover", (0, 1)),
        Preset("pll.external_reference", (0, 1)),
        Preset("pll.hold_causes", (0, 2, 4, 8, 0x10, 0x0A)),
        Preset("pll.recovery_state", (0, 1, 2)),
        Preset("pll.fine_state", (0, 6, 7, 9, 10)),
        Preset("switch_word_bit_11", (0, 1)),
    ),
    hooks=(Hook("standby_from_unlock", results=(0, 1)), LOG_EVENT),
    cases=96,
)
CLOCK_CHECK_STATES_Z3815A = States(
    presets=(
        Preset("DAT_0010218b", (0, 0, 1)),
        Preset("DAT_00102c75", (0, 1)),
        Preset("clock_gps_mismatch", (0, 1)),
        Preset("gps.status.time.second", (0, 1, 2)),
        Preset("pll.reference_time.second", (0, 1)),
    ),
    hooks=(Hook("lifetime_update"),),
    cases=64,
)
# gps_periodic_send's (0x57cd8) record, which the generated cases leave
# zeroed, a query of code 0, whose descriptor both copies call through
# at an instruction (gps_request_descriptor): each stage and one past
# them, no query (GPS_REQUEST_NONE) or the query of a setting, the mask
# angle's, rrs's, which gps_time_raim_supported refuses without time
# RAIM, clr's, which has none, and an Oncore code's, by its translation;
# its queries and sends about their bounds; and time RAIM supported or
# not (GPS_FIELD_592_TIME_RAIM).  tps's commits, which call it first,
# take the same.
GPS_PERIODIC_STATES_Z3815A = States(
    presets=(
        Preset("gps_periodic.stage", (0, 1, 2, 3, 0xFF)),
        Preset("gps_periodic.query_code", (0xFF, 0xFF, 0x39, 0x40, 0x41, 0x4A, 0x34)),
        Preset("gps_periodic.queries", (0, 1, 3, 4, 5)),
        Preset("gps_periodic.sends", (0, 1, 58, 59, 60)),
        Preset("gps.field_592", (0, 0x20)),
    ),
    cases=96,
)
# The Z3815A 4010's print_exception with debug output on, which prints
# through sci_printf_polled to the console, and with the kernel running,
# which names the task and pauses; the generated cases leave both off.
PRINT_EXCEPTION_STATES_Z3815A = States(
    presets=(Preset("debug_enabled", (0, 1, 1)), Preset("kernel_running", (0, 1))),
    cases=12,
)
# The Z3815A 4010's ES message: es_build binary and as text, its time in
# GPS seconds and as digits, through the time code's masks; es_send with
# periods that pass at once, wrap or not.
ES_BUILD_STATES_Z3815A = States(
    presets=(
        Preset("es_ascii", (0, 1)),
        Preset("nv_settings.gps_utc", (0, 0, 1)),
        Preset("tcode_status_and_mask", (0xFF, 0x0F)),
        Preset("tcode_status_or_mask", (0, 0x80)),
        Preset("tcode_error_and_mask", (0xFF, 0)),
        Preset("tcode_error_or_mask", (0, 1)),
        Preset("es_error", (0, 0x5A)),
    ),
    cases=32,
)
# The Z3815A 4010's output requests and readers: the controller's state
# in and out of the two that take an :OUTPut:ACTive, the switch word's
# decoded bits the inactive requests test, each set and clear, and the
# output's status bits and inactive causes, each and one past.
OUTPUT_REQUEST_STATES_Z3815A = States(
    presets=(
        Preset("output_control_state", (0, 2, 3, 4)),
        Preset("switch_word_bit_0", (0, 1, 1)),
        Preset("switch_word_bit_5", (0, 1, 1)),
        Preset("switch_word_bit_7", (0, 0, 1)),
        Preset("switch_word_bit_11", (0, 1, 1)),
    ),
    cases=24,
)
OUTPUT_STATE_STATES_Z3815A = States(
    presets=(
        Preset("output_status", (0, 1, 2, 4, 8, 0xC, 0x40, 0x43, 0x80)),
        Preset("output_inactive_cause", tuple(range(8))),
    ),
    cases=24,
)
ES_SEND_STATES_Z3815A = States(presets=(Preset("es_period", (0x147AE, 0, 0x10000, 0x2FFFF)),), cases=16)
# The Z3815A 4010's output controller (monitors.c, backp_task's): its
# states, each and one past its jump table, and what each tests, the
# extractor's bit, the two inputs, the requests and the decoded switch
# word's bits, set and clear, the loop's stage before, in and past its
# serving ones, a holdover past its threshold and not, and the routines
# it asks, the hardware's update and the switchover and standby checks,
# answered either way.
OUTPUT_CONTROL_STATES_Z3815A = States(
    presets=(
        Preset("output_control_state", (*range(11), 0x80, 2, 3, 3, 3, 3, 4, 7, 7, 9, 9)),
        Preset("porte_extractor_bit", (8, 8, 8, 0)),
        Preset("output_warmup_forced", (0, 1)),
        Preset("duart_input_0_state", (0, 1)),
        Preset("duart_input_1_state", (0, 1, 1)),
        Preset("output_diagnostic_on", (0, 0, 1)),
        Preset("output_diagnostic_off", (0, 0, 1)),
        Preset("output_active_disabled", (0, 0, 1)),
        Preset("output_active_enabled", (0, 0, 1)),
        Preset("output_inactive_requested", (0, 1)),
        Preset("output_inactive_cause", (0, 2, 3)),
        Preset("output_status", (0, 2)),
        Preset("switch_word_bit_0", (0, 1)),
        Preset("switch_word_bit_5", (0, 0x20)),
        Preset("switch_word_bit_7", (0, 0, 0x80)),
        Preset("switch_word_bit_8", (0, 1)),
        Preset("switch_word_bit_11", (0, 8)),
        Preset("pll.stage", (0, 2, 3, 3, 3, 5, 6, 7, 8, 0x80)),
        Preset("pll.holdover_duration", (0, 600, 601, 5000)),
        Preset("nv_settings.active_holdover_threshold", (600, 600, 3600)),
        Preset("redundancy_change_uptime", (0, 1, 0x7FFFFFF0)),
        Preset("clock.uptime", (0, 60, 61, 0x7FFFFFFF)),
    ),
    hooks=(
        Hook("alarm_hardware_update", results=(0, 0, 0, 1)),
        Hook("switchover_check", results=(0,)),
        Hook("standby_from_unlock", results=(0, 1)),
    ),
    cases=320,
)
# Its status: the test mode, the controller's states, the pp test's
# states and its blink, the switch word's bits and the requests, the
# stage and the GPS reference, and the routines it asks answered.
OUTPUT_STATUS_STATES_Z3815A = States(
    presets=(
        Preset("s3_test_mode", (0, 0, 0, 1)),
        Preset("s3_output", (0x1234,)),
        Preset("output_control_state", (*range(11), 0x80)),
        Preset("switch_word_bit_0", (0, 1)),
        Preset("switch_word_bit_1", (0, 2)),
        Preset("power_ok", (0, 1)),
        Preset("ptest_blink", (0, 1)),
        Preset("ptest_blink_uptime", (0, 0x7FFFFFF0)),
        Preset("output_inactive_requested", (0, 1)),
        Preset("output_active_requested", (0, 1)),
        Preset("output_active_request_uptime", (0, 0x7FFFFFF0)),
        Preset("pll.stage", (0, 3, 4, 5, 6, 7, 8)),
        Preset("gps.references[0].valid", (0, 1)),
        Preset("ptest.state", (0, 0, 1, 3, 4)),
        Preset("holdover_duration_exceeded", (0, 0, 1)),
        Preset("clock.uptime", (0, 1000, 0x7FFFFFF1)),
        Preset("unlock_timer_start", (0, 600)),
        Preset("unlock_timer_stopped", (0, 1)),
    ),
    hooks=(Hook("standby_from_unlock", results=(0, 1)), Hook("status_hardware_condition", results=(0, 0x40))),
    cases=64,
)
# Its alarms' updates: the loop running or not, the switch word's bit 3,
# the alarms already set or not, the stage about the PP2S's bound, and
# the routines they read answered: each condition bit, the puck alive
# or not, the antenna and the seating.  The hardware condition is
# status_hardware_condition's, which the image calls; the C's own
# static status_ready_condition, its body, has no copy in the image to
# hook (MEANT_ELSEWHERE).
ALARM_UPDATE_STATES_Z3815A = States(
    presets=(
        Preset("pll.loop_running", (0, 1)),
        Preset("switch_word_bit_3", (0, 8)),
        Preset("alarm_hardware", (0, 0, 0x1FF, 0x80)),
        Preset("pll.stage", (0, 2, 3, 7, 0x80)),
        Preset("holdover_duration_exceeded", (0, 1)),
        Preset("puck.alive", (0, 1)),
        Preset("n3_lock.seating_error", (0, 0, 1)),
    ),
    hooks=(
        Hook("status_hardware_condition", results=(0, 1, 2, 0x2000, 0x40, 0x80, 0x100, 0x200, 0x800, 0x1000)),
        Hook("hmon_check_antenna_current", results=(0, 1)),
    ),
    cases=64,
)
# alarm_hardware_update with the loop running, the PAL's bit set and no
# alarm yet, past powerup recovery, on the DUART's inputs that fail
# none, each clock and the PP2S alone, and the PAL and the PP2S both,
# so that the order it logs them in shows (harness.calls_order).
ALARM_INPUTS_Z3815A = (0x3C, 0x38, 0x34, 0x1C, 0x2C, 0x00)
ALARM_HARDWARE_CASES_Z3815A = [
    {
        DEVICES: DeviceCase(0, 0, False, (inputs,)),
        PRESETS: {"pll.loop_running": 1, "switch_word_bit_3": 8, "alarm_hardware": 0, "pll.stage": 3},
    }
    for inputs in ALARM_INPUTS_Z3815A
]
# hmon_check_antenna_current with its alarm on and off, the average
# below, at and between its bounds and past the upper, the switch word's
# bit 13 and PORTF0's bit 1 as the cases' devices give them.  The
# average's call is not compared: the image makes it first, gcc's code,
# taking it to have no effect, only with the alarm on.
ANTENNA_CURRENTS_Z3815A = (5.0, 10.0, 40.0, 70.0, 80.0)
ANTENNA_STATES_Z3815A = States(
    presets=(Preset("nv_settings.antenna_current_alarm", (0, 1, 1, 1)),),
    hooks=(
        Hook(
            "adc_average",
            results=tuple(struct.unpack(">I", struct.pack(">f", v))[0] for v in ANTENNA_CURRENTS_Z3815A),
            recorded=False,
        ),
    ),
    cases=60,
)
# The switchover check: the switch word's bit 5, the stage, the lock's
# and the bit's ticks against the uptime, at, before and past their
# bounds, the uncertainty, valid or not, past its bound or not, armed or
# not, and the bits the inactive request it makes tests.
SWITCHOVER_STATES_Z3815A = States(
    presets=(
        Preset("switch_word_bit_5", (0, 0x20)),
        Preset("pll.stage", (3, 5, 7, 7)),
        Preset("locked_ticks", (0, 13600, 13599, 100000)),
        Preset("standby_unlock_ticks", (0, 99700, 99699, 100000)),
        Preset("pll.measured_uncertainty_valid", (0, 1, 1)),
        Preset("pll.measured_uncertainty", (0.0, 1e-4, 3e-5)),
        Preset("switchover_armed", (0, 1)),
        Preset("clock.uptime", (100000,)),
        Preset("switch_word_bit_0", (0, 1)),
        Preset("switch_word_bit_7", (0, 0x80)),
    ),
    cases=160,
)
# The standby check: the n3 lock state and stage in and out, the count
# about its 700, the result kept, the interval's mean within, between
# and past its bounds, either sign, and debugging on and off.
STANDBY_STATES_Z3815A = States(
    presets=(
        Preset("pll.n3_lock_state", (12, 12, 0)),
        Preset("pll.stage", (6, 6, 7)),
        Preset("standby_from_unlock_count", (0, 699, 700, 701)),
        Preset("standby_from_unlock_ready", (0, 1)),
        Preset("interval_mean", (0.0, 3e-9, -3e-9, 4.5e-9, -4.5e-9, 6e-9, -1e-8)),
        Preset("debug_enabled", (0, 1)),
    ),
    cases=48,
)
# The unlock timer: its start against the uptime about its 300 s,
# logged and stopped or not.
UNLOCK_TIMER_STATES_Z3815A = States(
    presets=(
        Preset("unlock_timer_start", (0, 1000)),
        Preset("unlock_timer_logged", (0, 1)),
        Preset("unlock_timer_stopped", (0, 1)),
        Preset("clock.uptime", (0, 1299, 1300, 100000)),
    ),
    cases=24,
)
# The switch word's decode and the OCXO's warmth: the word and the
# DUART's inputs as the case's devices give them (harness.SWITCH_WORD),
# the clock valid or not, the oven current's sample about 0.9 and the
# puck warm or not.
SWITCH_DECODE_STATES_Z3815A = States(
    presets=(Preset("clock.set", (0, 1)),),
    cases=24,
)
OCXO_WARM_STATES_Z3815A = States(
    presets=(
        Preset("adc_monitors[6].samples[2]", (0.5, 0.9, 0.95)),
    ),
    hooks=(Hook("puck_warm", results=(0, 1)),),
    cases=16,
)
# The Z3815A 4010's latch writers with :DIAGnostic:TMODe:STATe on and
# off, which write diag_tmode_data in place of selftest.latch (0x236d2,
# 0x2370c, 0x2374c, 0x23770), and the lamps' writer, which writes nothing
# while it is on (0x36c1a).
LATCH_STATES_Z3815A = States(presets=(Preset("diag_tmode_state", (0, 1)), Preset("diag_tmode_data", (0, 0x5AC3))), cases=8)
# es_error_update with the :STATus:OPERation:HARDware condition's bit 6,
# the EFC near its limit, set and clear (0x23f74).
ES_ERROR_STATES_Z3815A = States(
    hooks=(Hook("status_hardware_condition", results=(0, 0x40, 0xFFBF), recorded=False),), cases=8
)
# sci_status_happening with the SCI session's status model ready and
# not, status_clear_bit's arguments recorded, for its given conditions'
# bytes past 0x7f, which it sign-extends (0x43530).
SCI_STATUS_HAPPENING_STATES_Z3815A = States(
    presets=(Preset("sci_status_ready", (0, 1, 1)),), hooks=(Hook("status_clear_bit"),), cases=12
)
# The Z3815A 4010's receiver status update for each receiver and every
# state its switch takes and one past each end, under the GPS's states
# that qualify it, that pass a fault the hysteresis rides out and that
# disqualify it for each reason, the tracking log on and off, and the
# reference's hysteresis none, cleared and not, and its override each
# way, at an uptime 1000 against a validation from 10 to 50 s before,
# 35 s, the GPS's assessment, among them, where the clearing and the
# hysteresis end; a satellite for position on channel 3; and the
# hardware condition's 1PPS bit, hooked, clear or set.
STATUS_GOOD_Z3815A = {
    "gps.fix_mode": 1, "gps.status_received": 2, "gps.tps.field_00": 1, "gps.position_hold": 1,
    "gps.field_641": 1, "gps.channel_count": 6, "gps.status.channels[2].satellite": 7,
    "gps.status.channels[2].mode": 8,
}
STATUS_VARIATIONS_Z3815A = (
    {},
    STATUS_GOOD_Z3815A,
    {**STATUS_GOOD_Z3815A, "gps.raim_assessment": 3},
    {**STATUS_GOOD_Z3815A, "gps.status_received": 1},
    {**STATUS_GOOD_Z3815A, "gps.tps.field_00": 0},
    {**STATUS_GOOD_Z3815A, "gps.status_monitor_failed": 1},
    {**STATUS_GOOD_Z3815A, "gps.field_641": 0},
    {**STATUS_GOOD_Z3815A, "gps.position_hold": 0, "gps.surveying": 1},
    {**STATUS_GOOD_Z3815A, "gps.position_hold": 0},
    {**STATUS_GOOD_Z3815A, "gps.pps_check_disabled": 1},
    {**STATUS_GOOD_Z3815A, "gps.status.channels[2].mode": 0},
)
STATUS_REFERENCES_Z3815A = (
    {"hysteresis": 0, "hysteresis_margin": 0, "validation_start": 990, "field_0c": 0},
    {"hysteresis": 60, "hysteresis_margin": 3, "validation_start": 950, "field_0c": 30},
    {"hysteresis": 60, "hysteresis_margin": 3, "validation_start": 990, "field_0c": 9, "override": 1},
    {"hysteresis": 20, "hysteresis_margin": 0, "validation_start": 960, "override": 0xFF},
    {"hysteresis": 60, "hysteresis_margin": 3, "validation_start": 965, "field_0c": 32},
    {"hysteresis": 38, "hysteresis_margin": 3, "validation_start": 965, "field_0c": 0},
)
STATUS_STATES_Z3815A = (*range(11), 0xFF)
STATUS_UPDATE_CASES_Z3815A = [
    {
        0: receiver,
        1: alternate,
        PRESETS: {
            "clock.uptime": 1000,
            "gps.tracking_log": 1,
            "gps.tracking_log_active": (state + index) % 2,
            **variation,
            f"gps.references[{receiver}].state": state,
            **{f"gps.references[{receiver}].{field}": value for field, value in reference.items()},
        },
    }
    for receiver, alternates, variations in ((0, (0,), STATUS_VARIATIONS_Z3815A), (1, (0, 1), ({},)))
    for alternate in alternates
    for index, variation in enumerate(variations)
    for state in STATUS_STATES_Z3815A
    for reference in STATUS_REFERENCES_Z3815A
]
# nmea_leap_second_due's dates and times, each a long as its slot holds
# it, a date's month, day and year, a time's hour, minute and second:
# a leap at a month's end and on a 1st, the time before it in its month,
# the month before and the year before, within the two hours and past.
def nmea_date(year: int, month: int, day: int) -> int:
    return int.from_bytes(struct.pack(">BBH", month, day, year), "big")


def nmea_time(hour: int, minute: int = 0, second: int = 0) -> int:
    return int.from_bytes(struct.pack(">BBBB", 0, hour, minute, second), "big")


LEAP_DUE_CASES_Z3815A = [
    {0: nmea_date(*now), 1: nmea_time(hour), 2: nmea_date(*leap), 3: nmea_time(*leap_time)}
    for now, hour, leap, leap_time in (
        ((2026, 12, 31), 23, (2027, 1, 1), (0,)),
        ((2027, 1, 1), 1, (2027, 1, 1), (0,)),
        ((2027, 1, 1), 3, (2027, 1, 1), (0,)),
        ((2026, 12, 10), 5, (2027, 1, 1), (0,)),
        ((2026, 11, 30), 5, (2027, 1, 1), (0,)),
        ((2026, 6, 10), 5, (2026, 6, 30), (23, 59, 59)),
        ((2026, 5, 31), 5, (2026, 6, 30), (23, 59, 59)),
        ((2026, 7, 1), 0, (2026, 6, 30), (23, 59, 59)),
        ((2025, 6, 10), 5, (2026, 6, 30), (23, 59, 59)),
        ((2026, 6, 15), 5, (2026, 7, 1), (0,)),
        ((2026, 7, 15), 5, (2026, 7, 1), (0,)),
    )
]
# nmea_position_message's survey: running or held, its count against
# the duration or none, the fix 3D or not, the longitude and the average
# on each side of the date line and about a quarter and a half turn, and
# the height's type; the status update and the status print hooked.
POSITION_MESSAGE_STATES_Z3815A = States(
    presets=(
        Preset("gps.position_hold", (0, 0, 0, 1)),
        Preset("nv_settings.douglas_duration_infinite", (0, 1)),
        Preset("nv_settings.douglas_duration", (0, 5, 100)),
        Preset("gps.average_count", (0, 3, 10)),
        Preset("gps.status.receiver_status", (0x20, 0x20, 0)),
        Preset("gps.status.longitude", (0, 400000000, -400000000, 640000000, -640000000, 324000001, -324000001)),
        Preset("gps.average_longitude", (0.0, 500000000.0, -500000000.0, 647000000.0, -647000000.0, -300000000.0)),
        Preset("gps.field_592", (0, 0x40)),
        Preset("gps_status_print_interval", (0, 2)),
        Preset("gps_status_print_count", (0, 1, 2)),
    ),
    hooks=(Hook("gps_receiver_status_update"), Hook("gps_debug_status")),
    cases=96,
)
# gps_print_instrument_state's leap second pending each way and past,
# tps' sign of one, the time scale, a leap date or none, and ignored
# satellites none, the first and the last, and all.
INSTRUMENT_STATE_STATES_Z3815A = States(
    presets=(
        Preset("gps.leap_second_pending", (0, 1, 2, 3, 0xFF)),
        Preset("gps.tps.leap_second", (1, -1, 0)),
        Preset("gps.utc_time", (0, 1)),
        Preset("gps.tps.leap_date.year", (0, 2026)),
        Preset("gps.ignored_satellites", (0, 0x80000001, 0xFFFFFFFF, 0x10)),
        Preset("gps.position_hold", (0, 1)),
    ),
    cases=48,
)
# The Z3815A 4010's event log ring (log.c): log_append at each entry of
# log_write_page (0x10240c, +4), full or not, wrapping or not, on the
# first, the next to last and the last page, the newest one before the
# oldest, so that a used entry moves it, at its last and not, and the
# count about its report and its most; log_init with log_lap
# either way and the page read answered good, bad or failed, its
# entries free or used and of either lap, so that it stops at a free
# entry, the oldest, a bad page or a failure, or reads all 74.  Both
# hook log_page_read, which reads the EEPROM, erased in every case.
LOG_PAGE_ENTRY_FORMATS_Z3815A = tuple(
    Preset(f"log_write_page.entries[{entry}].format", (1, 1, 1, 0)) for entry in range(3)
)
LOG_PAGE_ENTRY_LAPS_Z3815A = tuple(
    Preset(f"log_write_page.entries[{entry}].time", (0, 0, 0x01000000)) for entry in range(3)
)
LOG_APPEND_STATES_Z3815A = States(
    presets=(
        Preset("log_cursor", (0x102410, 0x102424, 0x102438)),
        Preset("log_full", (0, 1)),
        Preset("nv_settings.log_wrap", (0, 1)),
        Preset("log_write_page_number", (0, 72, 73)),
        Preset("log_newest", (5, 220, 221)),
        Preset("log_oldest", (6, 221, 0)),
        Preset("log_entries", (0, 177, 222, 176, 221)),
        Preset("log_lap", (0, 1)),
    ),
    hooks=(Hook("log_page_read", leaves=LOG_PAGE_ENTRY_FORMATS_Z3815A, results=(0,)),),
    cases=64,
)
LOG_INIT_STATES_Z3815A = States(
    presets=(Preset("log_full", (0, 1)),),
    hooks=(
        Hook("mutex_create", results=(0, 0, 0, 0, 0, 0, 0, 5), recorded=False),
        Hook("mutex_lock", results=(0,)),
        Hook("mutex_unlock", results=(0,)),
        Hook("log_first_lap", results=(0, 1)),
        Hook(
            "log_page_read",
            leaves=LOG_PAGE_ENTRY_FORMATS_Z3815A + LOG_PAGE_ENTRY_LAPS_Z3815A,
            results=(0, 0, 0, 0, 0xFF, 3),
        ),
    ),
    cases=96,
)
# The 58503A 3704's, the Z3815A 4010's and the Z3817A 3752's log_event,
# which logs by a code, not the base's, which writes its entry itself, starts from none
# of STRAGGLER_STATES' log cursors.
LOG_EVENT_BY_CODE_STATES = {"log_event": States()}
IMAGE_STATES = {
    "58503a-3704": LOG_EVENT_BY_CODE_STATES,
    "z3801a-3543": {
        name: States(presets=(Preset("log_cursor", LOG_CURSORS_Z3801A), Preset("log_mutex.exchange", LOCK_EXCHANGES)))
        for name in ("log_write", "log_event")
    },
    "z3815a-4010": {
        **Z3815A_LOOP_STATES,
        **LOG_EVENT_BY_CODE_STATES,
        "pllp_task": PLLP_TASK_STATES_Z3815A,
        "clock_check_against_gps": CLOCK_CHECK_STATES_Z3815A,
        "print_exception": PRINT_EXCEPTION_STATES_Z3815A,
        "es_build": ES_BUILD_STATES_Z3815A,
        "es_send": ES_SEND_STATES_Z3815A,
        "es_error_update": ES_ERROR_STATES_Z3815A,
        "sci_status_happening": SCI_STATUS_HAPPENING_STATES_Z3815A,
        **{
            name: LATCH_STATES_Z3815A
            for name in ("latch_bits_set", "latch_bits_write", "latch_lamps_write",
                         "latch_rewrite", "lamps_write")
        },
        "log_append": LOG_APPEND_STATES_Z3815A,
        "log_init": LOG_INIT_STATES_Z3815A,
        "nmea_position_message": POSITION_MESSAGE_STATES_Z3815A,
        "gps_receiver_status_update": States(hooks=(Hook("status_hardware_condition", results=(0, 0x100)),)),
        "gps_print_instrument_state": INSTRUMENT_STATE_STATES_Z3815A,
        "gps_leap_ack": States(
            presets=(Preset("pll.leap_ack_requested", (0, 1)), Preset("pll.leap_ack_state", (0, 3, 7))), cases=12
        ),
        **{
            name: OUTPUT_REQUEST_STATES_Z3815A
            for name in ("output_active_request", "output_inactive_request", "output_inactive_request_bit_11")
        },
        **{
            name: OUTPUT_STATE_STATES_Z3815A
            for name in ("output_state_get", "scpi_query_OUTPut_STATe", "scpi_query_OUTPut_INACtive")
        },
        "output_control_step": OUTPUT_CONTROL_STATES_Z3815A,
        "output_status_update": OUTPUT_STATUS_STATES_Z3815A,
        **{name: ALARM_UPDATE_STATES_Z3815A for name in ("alarm_hardware_update", "alarm_operation_update")},
        "hmon_check_antenna_current": ANTENNA_STATES_Z3815A,
        "switchover_check": SWITCHOVER_STATES_Z3815A,
        "standby_from_unlock": STANDBY_STATES_Z3815A,
        "unlock_timer_check": UNLOCK_TIMER_STATES_Z3815A,
        **{name: SWITCH_DECODE_STATES_Z3815A for name in ("switch_word_decode", "input_data_get")},
        "ocxo_warm": OCXO_WARM_STATES_Z3815A,
        **{
            name: GPS_PERIODIC_STATES_Z3815A
            for name in ("gps_periodic_send", "nmea_tps_fields_commit", "nmea_tps_commit")
        },
        **{
            name: PLLP_STAGE_STATES_Z3815A
            for name in (
                "pll_mode",
                "get_ffom",
                "kenneth_present_get",
                "kenneth_predicted_get",
                "pll_stage_status",
                "recovery_suffix_index",
                "holdover_waiting_reason",
            )
        },
    },
    # The Z3817A 3752's counter, which has no interpolator
    # (docs/firmware/z3817a-3752.md, "How it differs from the Z3815A"):
    # COUNTER_STATES less the interpolator's preset and hooks; and
    # enter_holdover's STATES with the GPS reference's state, which it
    # switches on for its log code (0x48a8a), as the Z3815A's does,
    # where the Z3805A's reads gps.status_reported; log_write from its own
    # log cursors, and log_event, the 58503A's by a code, from none.
    "z3817a-3752": {
        **{
            name: States(
                presets=(Preset("interval_reading.armed", (1, 1, 0)),),
                hooks=(Hook("counter_wait_ready", results=(1, 1, 0)),),
            )
            for name in ("counter_read", "counter_arm")
        },
        "enter_holdover": States(
            presets=tuple(
                Preset("gps.references[0].state", preset.values) if preset.target == "gps.status_reported" else preset
                for preset in STATES["enter_holdover"].presets
            ),
            hooks=STATES["enter_holdover"].hooks,
            cases=STATES["enter_holdover"].cases,
        ),
        "log_write": States(
            presets=(Preset("log_cursor", LOG_CURSORS_Z3817A), Preset("log_mutex.exchange", LOCK_EXCHANGES))
        ),
        # the external record, which gps_reference_update tests first
        # (0x4a14e), each validity of it as of the GPS record's
        "gps_reference_update": States(
            presets=(
                Preset("gps.references[0].valid", (0, 1, 2, 3)),
                Preset("gps.references[1].valid", (0, 1, 2)),
                Preset("pll.stage", (5, 6, 3)),
            )
        ),
        **LOG_EVENT_BY_CODE_STATES,
    },
}
# The functions whose cases find no task by name (harness.IDENT_NOT_FOUND),
# by the generated cases that do so, the others finding one: a creator,
# a function that asks the kernel for a task by name and creates one
# (CREATOR_CALLS, an installer's binding's alike), whose odd cases do so,
# so that its create and start, name, entry and all, are compared, and
# which, finding the task, returns; and root_task, in every case, which
# then goes on to create and start every task.  A creator is found by
# its calls, not named, so that a twin of one an image adds, the Z3815A
# 4010's sci_scpi_task_create, is not left finding its task in every
# case, its create never run (creators_ident_fails).
CREATORS_IDENT_FAILS = range(1, CASES, 2)
CREATOR_CALLS = {"psos_ident", "psos_create"}
INSTALLER_BINDING_PREFIX = "installer_"
IDENT_FAILS = {"root_task": range(CASES)}


def creators_ident_fails(conventions: dict) -> dict[str, range]:
    """IDENT_FAILS with every creator's cases: each function whose calls
    include CREATOR_CALLS, or the installer's bindings of them."""
    creators = {
        name: CREATORS_IDENT_FAILS
        for name, convention in conventions.items()
        if CREATOR_CALLS <= {call.removeprefix(INSTALLER_BINDING_PREFIX) for call in convention.get("calls", [])}
    }
    return {**creators, **IDENT_FAILS}


# The functions whose loop never ends and makes no kernel call, by the
# variable it counts in and the count a call ends at (harness.COUNTED):
# root_task's idle loop (0x131dc), counting in idle_loops.  The count is
# odd, so that a loop counting by more than one passes it and runs to
# STEP_LIMIT, which the image's does not.
IDLE_LOOPS = 101
COUNTED_LOOPS = {"root_task": ("idle_loops", IDLE_LOOPS)}
# An image's own, with COUNTED_LOOPS: the Z3815A 4010's pllp_task,
# whose loop counts its passes in watchdog_pll after two counts of its
# set-up, ended at its PLLP_PASSES_Z3815A-th pass, so that a case's walk
# through the stages (PLLP_TASK_STATES_Z3815A) is compared where it has
# got to, rather than run to STEP_LIMIT and skipped.
PLLP_SET_UP_COUNTS_Z3815A = 2
PLLP_PASSES_Z3815A = 9
IMAGE_COUNTED_LOOPS = {
    "z3815a-4010": {"pllp_task": ("watchdog_pll", PLLP_SET_UP_COUNTS_Z3815A + PLLP_PASSES_Z3815A)},
}
# The status register a function starts with that runs only in a task,
# as a task does, in user mode, its interrupts unmasked, rather than
# harness.SUPERVISOR.  Every task starts with the status register in its
# record's +0x44, which kernel_create clears (0x14e5c); its bit 12 marks
# a stack of the task's own, and the switch clears it before it pushes
# the word as the status register (0x16046, 0x1605a).  Only IDLE's is
# set, to supervisor mode (kernel_init, 0x15eb4); service 2, the start,
# reads the task and its entry and no mode (kernel_start, 0x14ea0).
# root_task enters supervisor mode with trap #7 to enable the periodic
# interrupt and moves its status register back (0x13082 to 0x13098).
# Which functions run in a task in user mode is found from the export's
# calls (task_context): one that only such a task reaches runs its cases
# so, and one that code in supervisor mode reaches too runs every other
# case so, the first among them, and the rest in supervisor mode.  A
# case whose two copies both stop at a privilege violation, an
# instruction a caller in supervisor mode runs it for, is run again in
# supervisor mode.
TASK_STATUS = 0
PRIVILEGE_VIOLATION = 8
# The task entries: what kernel_init starts as ROOT, the configuration's
# root_entry (psos_configuration, +0x2c), what a task's start, service
# 2, is given (psos_start, 0x16fae, and the installer's copy), and the
# entries the pForth code starts its tasks at with service 2 in line,
# which C cannot say, each in user mode as every task starts:
# console_task (0x1aedc), which console_task_create starts (A1 from
# 0x1aebe, the trap at 0x1aec6), and forth_spawned_task (0x19f42), at
# which spawn, forth_spawn, starts a task (A1 from 0x1a00a, service 2
# through 0x19ed0 at 0x1a012).  A task that opens with service 33 runs
# on in supervisor mode, as klok_task (0x10b1e), spoo_task (0x20e5e) and
# the installer's spoo_task do; so does a function spoo_run (0x20d66)
# is given, which it calls itself, in its caller's mode, before the
# spool task starts, and after sends to the spool task, which calls it
# in supervisor mode.
ROOT_ENTRY = "((psos_configuration *)0)->root_entry"
TASK_STARTER = r"(?:installer_)?psos_start"
TASK_START = re.compile(rf"\b{TASK_STARTER}\s*\(\s*\w+\s*,\s*(\w+)\s*\)")
FORTH_TASK_ENTRIES = {"console_task", "forth_spawned_task"}
SUPERVISOR_SERVICES = {"psos_supervisor", "installer_psos_supervisor"}
SPOOLER = r"(?:installer_)?spoo_run"
SPOOLED = re.compile(rf"\b{SPOOLER}\s*\(\s*(?:\([^()]*\)\s*)?(\w+)")
# A function's block in the export, from the line giving its address to
# the next; a direct call in its C or in its assembly (for a function
# written by hand, those of the decompiler's reading of the image's,
# which the sidecar lists instead, "calls" and "passed"); a thunk's
# target; and a comment, whose text is not a call.
EXPORT_BLOCK = re.compile(r"^/\* ((?:\w+::)?[0-9a-f]{8}) \*/$", re.M)
C_CALL = re.compile(r"\b(\w+)\s*\(")
ASSEMBLY_CALL = re.compile(r"\b(?:jsr|jmp|bsr\S*|bra\S*)\s+\(?(\w+)")
THUNK = re.compile(r'\.set (\w+), (\w+)"')
COMMENT = re.compile(r"/\*.*?\*/", re.S)
# A prototype, its result, name and parameters, a long one wrapped; and
# an SCPI handler's parameters (scpi_handler_function), with which the
# engine calls it through the command tree, in the SCPI task: the
# image's only references to a handler are its node's setter and query
# slots, which only scpi_call_handler (0x29642) calls through, from
# scpi_interpret (0x293d4), from scpi_task (0x2a0de).
PROTOTYPE = re.compile(r"^([A-Za-z_][\w \*]*?)\n?\b(\w+)\n?\s*\(([^()]*)\);$", re.M)
SCPI_HANDLER = re.compile(r"^void \*\s*\w*,\s*void \*\s*\w*,\s*scpi_call \*\s*\w*$")


@dataclass(frozen=True)
class TaskStacks:
    """The running task's stacks a case of the stack report starts with
    (task_stack_info, 0x16bb6; structures.md, "The tasks' stacks"): each
    stack's bytes, used and unused, the unused filled with UNUSED_STACK;
    whether the task has a supervisor stack, below its user stack, or
    only a user stack, which task_stack_info takes from two locals it
    never sets (docs/firmware/quirks.md); how far below each stack's top
    its saved pointer lies, as a fraction of its size; and the bytes in
    use at the top of the kernel's own supervisor stack, the rest zero,
    which the report measures its use against (0x16dd4)."""

    user: int
    user_used: int
    supervisor: int
    supervisor_used: int
    pointer_at: float
    system_used: int


# The fill the stack report measures a stack's use against (0x16cea), a
# used word's value, and the bytes between a task's supervisor stack's
# top and its user stack, which task_stack_info leaves out of the user
# stack's size (0x16c08), and between a segment's start and its stack's
# top, which it leaves out of the segment's (0x16c36).
UNUSED_STACK = struct.pack(">H", 0xABCD)
USED_STACK = struct.pack(">H", 0x1234)
USER_STACK_GAP = 4
SEGMENT_OVERHEAD = 0x12
# The cases' stacks, in turn: both stacks, part used, the pointers in
# their used parts, then below them, the kernel's stack's use then the
# task's supervisor stack's, all its words zero (0x16df2); a user stack
# alone in a segment, part used; and an empty one, so that no task is
# printed and the kernel's stack's use is D2 as the report's caller
# left it (0x16e06).
TASK_STACK_CASES = [
    TaskStacks(0x80, 0x40, 0x40, 0x30, 0.25, 0x800),
    TaskStacks(0x80, 0x10, 0x40, 0x10, 0.75, 0),
    TaskStacks(0x60, 0x20, 0, 0, 0.25, 0x10),
    TaskStacks(0, 0, 0, 0, 0.5, 0),
]
TASK_STACKS = {"task_stack_report", "scpi_query_DIAGnostic_OS_STACk", "s_rep_word"}
# The fields TaskStacks sets, as lvalues of a record at 0 whose
# addresses are their offsets, probed as a Preset's target is; and the
# kernel's own supervisor stack, from 200 bytes into its data area
# (0x16dbe), and its size.
TASK_FIELDS = {
    name: f"((psos_task *)0)->{name}"
    for name in ("stack_bottom", "supervisor_top", "user_top", "supervisor_pointer", "user_pointer")
}
SEGMENT_SIZE = "((psos_segment *)0)->size"
SYSTEM_STACK = 200
SYSTEM_STACK_SIZE = "((psos_configuration *)0)->supervisor_stack_size"
# The names of the probes gcc compiles for each Preset's target and
# each of those, by its index: its address and its size.
PROBE_ADDRESS = "function_check_address_{}"
PROBE_SIZE = "function_check_size_{}"
# A slot of gcc's stack arguments (gcc_slots).
GCC_SLOT = 4


def stack_bytes(size: int, used: int) -> bytes:
    """A stack of a size, its top used bytes in use."""
    return UNUSED_STACK * ((size - used) // len(UNUSED_STACK)) + USED_STACK * (used // len(USED_STACK))


def area(m: Machine, contents: bytes) -> int:
    """contents among the arguments' buffers, however many they take."""
    at = m.next_buffer
    m.next_buffer += -(-len(contents) // BUFFER_SPACING) * BUFFER_SPACING
    m.uc.mem_write(at, contents)
    return at


def task_stacks(m: Machine, located: dict[str, tuple[int, int]], case: int) -> None:
    """The running task's record and stacks, and the kernel's supervisor
    stack, as a case of TASK_STACK_CASES has them; for a task with a
    user stack alone, its segment, which each frame's locals point at,
    task_stack_info's two unset ones among them."""
    stacks = TASK_STACK_CASES[case % len(TASK_STACK_CASES)]
    record = m.first_allocation

    def field(name: str, value: int) -> None:
        offset, size = located[TASK_FIELDS[name]]
        m.uc.mem_write(record + offset, preset_bytes(value, size))

    def into(top: int, size: int) -> int:
        return top - int(size * stacks.pointer_at) & -len(UNUSED_STACK)

    user = stack_bytes(stacks.user, stacks.user_used)
    if stacks.supervisor:
        bottom = area(m, stack_bytes(stacks.supervisor, stacks.supervisor_used) + bytes(USER_STACK_GAP) + user)
        supervisor_top = bottom + stacks.supervisor
        user_top = supervisor_top + USER_STACK_GAP + stacks.user
        field("stack_bottom", bottom)
        field("supervisor_top", supervisor_top)
        field("user_top", user_top)
        field("supervisor_pointer", into(supervisor_top, stacks.supervisor))
        field("user_pointer", into(user_top, stacks.user))
    else:
        offset, size = located[SEGMENT_SIZE]
        segment = bytearray(SEGMENT_OVERHEAD)
        segment[offset : offset + size] = preset_bytes(stacks.user + SEGMENT_OVERHEAD, size)
        top = area(m, user + bytes(segment)) + stacks.user
        field("supervisor_pointer", into(top, stacks.user))
        m.frame_fill = struct.pack(">I", top)
    offset, size = located[SYSTEM_STACK_SIZE]
    size_at = m.profile.psos_config + offset
    system = pointer_at(m, m.profile.psos_config) + SYSTEM_STACK
    length = int.from_bytes(m.read(size_at, size), "big")
    unused = length - stacks.system_used
    m.uc.mem_write(system, bytes(unused) + USED_STACK * (stacks.system_used // len(USED_STACK)))


# A target's leading cast of a null pointer, which only a type's
# declaration makes probeable; and its root, the variable it names first,
# which the image's C declares as the export does a global, by its .set.
NULL_CAST = re.compile(r"\(\([^()]*\)0\)")
# an object at an address, of a type the C defines, as the EEPROM's
# calibration block, which some images' C declares no object for
ADDRESS_CAST = re.compile(r"\*\(\((\w+) \*\)0x[0-9a-f]+\)$")
ROOT = re.compile(r"[A-Za-z_]\w*")
# a field a target names after its root, as gps.status_reported's
MEMBER = re.compile(r"\.([A-Za-z_]\w*)")


def probeable(c: str, target: str) -> bool:
    """Whether the C has what a target names: a structure's field, an
    object of a type it defines at an address (ADDRESS_CAST), or a
    global the export sets and each field of it the target names, which
    another image's C may not have."""
    if NULL_CAST.match(target) is not None:
        return True
    cast = ADDRESS_CAST.match(target)
    if cast is not None:
        kind = cast.group(1)
        return f"typedef struct {kind} {kind}" in c or f"}} {kind};" in c
    root = ROOT.match(target).group(0)
    declared = re.search(rf"(?m)^extern (?:volatile )?(\w+) {root}\b", c)
    # a field of a global the export leaves untyped, as the Z3815A's pll
    if MEMBER.search(target) and declared is not None and f"\nstruct {declared.group(1)} {{" not in c:
        return False
    if f'__asm__(".set {root}, ' not in c:
        return False
    # each field in its structure, where the C defines that structure, as
    # an image whose record lacks a field another's has may define it in
    # another structure; elsewhere in any
    kind = declared.group(1) if declared is not None else None
    for field in MEMBER.findall(target):
        body = re.search(rf"(?ms)^(?:struct|union) {kind} \{{\n(.*?)^\}};", c) if kind else None
        line = rf"(?m)^\s+(?:(?:struct|union) )?(\w+)[\w \*]*?\b{field}\s*(?:\[[^\]]*\]\s*)*;"
        found = re.search(line, body.group(1) if body else c)
        if found is None:
            return False
        kind = found.group(1) if body else None
    return True


def targets(c: str) -> list[str]:
    """Every Preset's target, STATES' and each image's IMAGE_STATES', a
    given case's and its hooks' leaves', every image's, the task stacks' fields, the regions and
    the counted loops' counters, each once, in a fixed order, that the C
    has (probeable)."""
    found = [*TASK_FIELDS.values(), SEGMENT_SIZE, SYSTEM_STACK_SIZE, ROOT_ENTRY]
    found += [variable for variable, _ in REGIONS.values()]
    for loops in [COUNTED_LOOPS, *IMAGE_COUNTED_LOOPS.values()]:
        found += [variable for variable, _ in loops.values() if variable not in found]
    for states in [*STATES.values(), *(states for image in IMAGE_STATES.values() for states in image.values())]:
        for preset in states.presets + tuple(p for hook in states.hooks for p in hook.leaves):
            if preset.target not in found:
                found.append(preset.target)
    for own in [*GIVEN_CASES.values(), *(own for image in IMAGE_GIVEN_CASES.values() for own in image.values())]:
        for values in own:
            watches = [i.watch for i in values.get(INTERRUPTS, ()) if isinstance(i.watch, str)]
            leaves = [target for leaves in values.get(HOOK_LEAVES, {}).values() for target in leaves]
            for target in [*values.get(PRESETS, {}), *watches, *leaves, *values.get(RECORDS, {})]:
                if target not in found:
                    found.append(target)
    return [target for target in found if probeable(c, target)]


def probes(c: str) -> str:
    """The C that has gcc find each target's address and size."""
    return (
        "".join(
            f"\nvoid *const {PROBE_ADDRESS.format(i)} = (void *)&({target});"
            f"\nconst uint32_t {PROBE_SIZE.format(i)} = sizeof({target});"
            for i, target in enumerate(targets(c))
        )
        + "\n"
    )


def probed(c: str, linked: list[Section], linked_symbols: dict[str, int]) -> dict[str, tuple[int, int]]:
    """Each target's address and size, from the probes in the link."""

    def long_at(name: str) -> int:
        address = linked_symbols[name]
        section = next(s for s in linked if s.address <= address < s.address + len(s.data))
        return struct.unpack_from(">I", section.data, address - section.address)[0]

    return {
        target: (long_at(PROBE_ADDRESS.format(i)), long_at(PROBE_SIZE.format(i)))
        for i, target in enumerate(targets(c))
    }


def preset_bytes(value: int | float, size: int) -> bytes:
    """A Preset's value in its target's bytes."""
    if isinstance(value, float):
        return struct.pack(">f" if size == 4 else ">d", value)
    return (value & ((1 << 8 * size) - 1)).to_bytes(size, "big")


@dataclass
class Drawn:
    """What a case drew for a function's States: each preset's bytes, by
    address; and by hooked routine, the bytes it leaves, by address, and
    its D0, None to leave it."""

    presets: dict[int, bytes]
    leaves: dict[str, dict[int, bytes]]
    results: dict[str, int | None]


def draw(states: States, located: dict[str, tuple[int, int]], name: str, description: str) -> Drawn:
    """A case's draws, the same each run: its generator seeded by the
    function and the case."""
    generator = random.Random(f"{name} {description}")

    def values(presets: tuple[Preset, ...]) -> dict[int, bytes]:
        drawn = {}
        for preset in presets:
            if preset.target not in located:
                continue
            address, size = located[preset.target]
            drawn[address] = preset_bytes(generator.choice(preset.values), size)
        return drawn

    return Drawn(
        values(states.presets),
        {hook.routine: values(hook.leaves) for hook in states.hooks},
        {hook.routine: generator.choice(hook.results) if hook.results else None for hook in states.hooks},
    )


def hook_arguments(m: Machine, types: Types, routine: "Function", gcc: bool) -> tuple[int, ...]:
    """A hooked routine's arguments at its entry, where its copy reads
    them: the image's at the original's offsets, gcc's in its slots, a
    narrower value in a slot's last bytes (gcc_slots); each as the
    image's copy would pass it (RebuiltMachine.as_image)."""
    sp = m.uc.reg_read(UC_M68K_REG_A7)
    found, slot = [], FIRST_ARGUMENT
    for offset, kind in zip(routine.offsets, routine.parameters):
        size = types.size(kind)
        at = sp + (slot + max(size, GCC_SLOT) - size if gcc else offset)
        found.append(m.as_image(int.from_bytes(m.uc.mem_read(at, size), "big")))
        slot += max(size, GCC_SLOT)
    return tuple(found)


def return_from_hook(m: Machine, result: int | None) -> None:
    """Returns from a hooked routine at its entry, D0 its result unless
    None."""
    sp = m.uc.reg_read(UC_M68K_REG_A7)
    if result is not None:
        m.uc.reg_write(UC_M68K_REG_D0, result & 0xFFFFFFFF)
    m.uc.reg_write(UC_M68K_REG_PC, int.from_bytes(m.uc.mem_read(sp, POINTER_SIZE), "big"))
    m.uc.reg_write(UC_M68K_REG_A7, sp + POINTER_SIZE)


# The differences whose cause is found and is not the C's (PLAN.md,
# "A rebuild that runs"), each counted as known while every difference
# of every failing case is that difference, so that another in the same
# routine, in the same case or another, still fails.
# The firmware's own: os_report_processes holds
# the address of "%s" in D5 across its calls (0x16768), which
# os_report_process prints as a task's stack share.
# Only that column of the line, the "%2d%%" after the memory column's
# "kB " (the format at 0x513b8, printed at 0x1646c), differs: the rest,
# the memory column that D6 starts (REPORT_REGISTERS) among it, is
# compared as any effect is.
SHARE_LINE = (
    r"trap #4 service 4 (?P<registers>[^,]*)data (?P<head>(?:[0-9a-f]{2})*?6b4220)(?:3[0-9]|2d|20)+"
    r"(?P<tail>2520(?:[0-9a-f]{2})*)"
)
D5_SHARE = Known(
    "os_report_process prints D5, which os_report_processes leaves holding the address of \"%s\" (0x16768)",
    rf"effect \d+ {SHARE_LINE}, image's trap #4 service 4 (?P=registers)data (?P=head)(?:3[0-9]|2d|20)+(?P=tail)",
)
# And gps_debug_status, which cuts the last comma from its last
# channel's text by clearing the byte before the text's end (0x49384):
# with debug output off, debug_sprintf leaves the text empty, and the
# byte cleared is the one below the buffer, the lowest of the A4 its
# movem saved (0x49214), which it returns with.
EMPTY_PART = Known(
    "gps_debug_status clears the byte below an empty part, its saved A4's lowest (0x49384)",
    r"register A4 5a5a000c, image's 5a5a0000",
)
# And satellite_status_list, whose list of the satellites visible and
# not in the set holds 12 (0x4985e): a visibility record with more
# overflows it, in the image over its end pointer (-0xc(a6)), so that
# its blank entries run to the end of RAM (0x499de), in gcc's frame
# over whatever gcc put above it.
OTHERS_OVERFLOW = Known(
    "satellite_status_list overflows its 12 satellites on more, each copy's frame its own (0x4985e)",
    r"stopped differently: image Invalid memory write \(UC_ERR_WRITE_UNMAPPED\) of 110000 at 499de, gcc .*",
)
# And what C cannot say: log_event pushes only its message's format
# (0x31a0a), so log_write keeps as the entry's two longs whatever lies
# above it, log_event's return address and the long holding its char,
# each copy's own; they differ, with the page's checksum, in the RAM's
# pages, log_write_page (0x101d4e) and log_page_buffer (0x101d94), and
# the EEPROM's (0x400d40, 64 bytes each), and nowhere else: the format,
# the image's text in each, does not (log.c).
LOG_ENTRY_ARGUMENTS = [at for entry in range(LOG_ENTRIES) for argument in entry_argument_offsets(entry) for at in argument]


def log_event_fill(pages: list[int], log_event: int) -> Known:
    """LOG_EVENT_FILL for an image whose RAM pages, log_write_page and
    log_page_buffer, are at pages, and whose log_event pushes its format
    at log_event."""
    fill = "|".join(
        f"{page + at:x}"
        for page in pages + [EEPROM_BASE + at for at in page_offsets()]
        for at in [*LOG_PAGE_CHECKSUM, *LOG_ENTRY_ARGUMENTS]
    )
    return Known(
        f"log_event's entry keeps log_event's own stack as its arguments, each copy's ({log_event:#x})",
        rf"(?:RAM|EEPROM) differs from (?:{fill})"
        rf"|effect \d+ write of \w+ to ({fill}), image's write of \w+ to \1",
    )


LOG_EVENT_FILL = log_event_fill([0x101D4E, 0x101D94], 0x31A0A)
# The image the tables of this file were written for, whose ROM their
# addresses are: another image's function check runs every case with
# them but KNOWN's differences, which are its failures until a wave
# finds their causes in its own code (IMAGE_KNOWN), and
# SET_UP_REQUESTS', whose descriptors are the base's.
BASE_IMAGE = "z3805a-3543b"
REPORT_FUNCTIONS = ("os_process_report", "ps_word", "scpi_query_DIAGnostic_OS_PROCess")
KNOWN = {
    **{name: D5_SHARE for name in REPORT_FUNCTIONS},
    "gps_debug_status": EMPTY_PART,
    "satellite_status_list": OTHERS_OVERFLOW,
    "log_event": LOG_EVENT_FILL,
}
# Each image's known differences, each found in its own code: the
# 58503A 3704's os_report_processes holds "%s"'s address, 0x540c4, in
# D5 as the Z3805A's does (0x167c2), and its os_report_process is the
# Z3805A's (0x1632e).  Its primary's session reaches :DIAG:OS:PROC?
# (tree_lines), where the first line's memory column also starts from
# D6 as each copy of scpi_interpret leaves it, which the given cases
# set (REPORT_REGISTERS) and a session does not.
D5_SHARE_58503A = (
    "os_report_process prints D5, which os_report_processes leaves holding the address of \"%s\" (0x167c2)"
)
SESSION_SHARE_LINE = (
    r"trap #4 service 4 (?P<registers>[^,]*)data (?P<head>(?:[0-9a-f]{2})*?)(?:3[0-9]|20)+6b4220(?:3[0-9]|2d|20)+"
    r"(?P<tail>2520(?:[0-9a-f]{2})*)"
)
# The Z3801A 3543's, the Z3805A's own in its code: os_report_processes
# holds "%s"'s address in D5 (0x16470); gps_debug_status clears the
# byte below an empty part (0x49342); satellite_status_list overflows
# its list (0x4981c), the image's blank entries running to the end of
# RAM at 0x4999c; and log_event pushes only its format (0x30842), its
# RAM pages at log_write_page (0x101d4c) and log_page_buffer (0x101d92).
D5_SHARE_Z3801A = (
    "os_report_process prints D5, which os_report_processes leaves holding the address of \"%s\" (0x16470)"
)
# The Z3815A 4010's, in its own code: os_report_processes holds "%s"'s
# address, 0x5ee62, in D5 as the Z3805A's does (0x26dd4), the routine
# and os_report_process relocated from the Z3805A's; its sci_puts leaves
# sci.transmit_next at the end of what it sent, where the Z3805A's sets
# it to "" (0x2fe00), and sci_putc sends from its own frame (0x30888), so
# that the pointer left is into each copy's stack, whose argument slots
# differ, a char's 2 bytes against gcc's 4: the pointer's low byte; and
# the installer's copies the same (installer::0x103230, 0x103c4e).
D5_SHARE_Z3815A = (
    "os_report_process prints D5, which os_report_processes leaves holding the address of \"%s\" (0x26dd4)"
)
TRANSMIT_FRAME_Z3815A = (
    "sci_puts leaves sci.transmit_next pointing into sci_putc's frame, each copy's stack its own (0x2fe00)"
)
# The Z3817A 3752's, in its own code: its process report is the 58503A's
# byte for byte, os_report_processes holding "%s"'s address, 0x58f26, in
# D5 (0x26760), which os_report_process prints for a task the kernel
# finds no segment of (0x26354); its sci_puts leaves sci.transmit_next as
# the Z3815A's does (0x2e804), its sci_putc sending from its own frame
# (0x2f27e); its installer is the Z3815A's.
D5_SHARE_Z3817A = (
    "os_report_process prints D5, which os_report_processes leaves holding the address of \"%s\" (0x26760)"
)
TRANSMIT_FRAME_Z3817A = (
    "sci_puts leaves sci.transmit_next pointing into sci_putc's frame, each copy's stack its own (0x2e804)"
)
INSTALLER_TRANSMIT_FRAME = Known(
    "the installer's copy of sci_puts leaves its transmit_next pointing into installer_sci_putc's frame, "
    "each copy's stack its own (installer::0x103230)",
    r"RAM differs from 10b009",
)
# A session's line of :DIAG:OS:PROC?, its memory and share columns
# apart, as scpi_interpret's KNOWN gives it.
SESSION_SHARE = (
    rf"effect \d+ {SESSION_SHARE_LINE}, image's trap #4 service 4 (?P=registers)data (?P=head)"
    r"(?:3[0-9]|20)+6b4220(?:3[0-9]|2d|20)+(?P=tail)"
)
IMAGE_KNOWN = {
    BASE_IMAGE: KNOWN,
    "z3815a-4010": {
        **{name: Known(D5_SHARE_Z3815A, D5_SHARE.outcome) for name in REPORT_FUNCTIONS},
        "scpi_interpret": Known(
            D5_SHARE_Z3815A + ", and adds D6, which scpi_interpret leaves (0x2699a)", SESSION_SHARE
        ),
        "sci_putc": Known(TRANSMIT_FRAME_Z3815A, r"RAM differs from 103dff"),
        "installer_sci_putc": INSTALLER_TRANSMIT_FRAME,
    },
    "z3817a-3752": {
        **{name: Known(D5_SHARE_Z3817A, D5_SHARE.outcome) for name in REPORT_FUNCTIONS},
        "sci_putc": Known(TRANSMIT_FRAME_Z3817A, r"RAM differs from 10394b"),
        "installer_sci_putc": INSTALLER_TRANSMIT_FRAME,
    },
    "z3801a-3543": {
        **{name: Known(D5_SHARE_Z3801A, D5_SHARE.outcome) for name in REPORT_FUNCTIONS},
        "gps_debug_status": Known(
            "gps_debug_status clears the byte below an empty part, its saved A4's lowest (0x49342)",
            EMPTY_PART.outcome,
        ),
        "satellite_status_list": Known(
            "satellite_status_list overflows its 12 satellites on more, each copy's frame its own (0x4981c)",
            r"stopped differently: image Invalid memory write \(UC_ERR_WRITE_UNMAPPED\) of 110000 at 4999c, gcc .*",
        ),
        "log_event": log_event_fill([0x101D4C, 0x101D92], 0x30842),
    },
    "58503a-3704": {
        **{name: Known(D5_SHARE_58503A, D5_SHARE.outcome) for name in REPORT_FUNCTIONS},
        "scpi_interpret": Known(D5_SHARE_58503A + ", and adds D6, which scpi_interpret leaves", SESSION_SHARE),
    },
}
# Another image's own given cases, in place of the base's of the same
# function: the 58503A 3704's 56 messages (oncore_messages, 0x51178);
# its step records' routines, load_double (0x3220e) and load_long
# (0x32e3a), where the base's are; clamp_to_limits on a long's steps,
# as the descriptors that validate with it give them (0x33bd8's, load_long_unit_step
# 0x32e32); the send filters' messages, the six- and eight-channel
# ones and one of neither, on six and eight channels (gps.md, "Six or
# eight channels"), with an encoder of the table's, long_array_get_byte
# (0x4fe5a), and gps.field_592's Time RAIM bit set and clear;
# scpi_task_run and installer_scpi_task_run on port 2, the one their
# port layers serve, and on another.
GPS_MESSAGES_58503A = 56
GPS_MESSAGES_Z3817A = 57
LOAD_DOUBLE_58503A = 0x3220E
LOAD_LONG_58503A = 0x32E3A
LOAD_LONG_UNIT_STEP_58503A = 0x32E32
# scpi_format_resolution_array_record's records, this image's eight
# zero bytes, those before status_register_layouts (0x2f44c), and
# scpi_input_source (0x29246).
ARRAY_RECORD_CASES_58503A = array_record_cases(0x2F44C, 0x29246)
FILTERED_MESSAGES = (0x1E, 0x27, 0x29, 0x32, 0x33, 0x34, 0x2A)
CHANNEL_COUNTS = (6, 8)
FILTER_ENCODER_58503A = 0x4FE5A
FILTER_ARGUMENTS = (struct.pack(">I", 0), struct.pack(">I", 1))
TIME_RAIM = 0x40
SCPI_TASK_PORTS = (2, 0)
FILTER_CASES_58503A = [
    {0: message, 1: FILTER_ENCODER_58503A, 2: arguments,
     PRESETS: {"gps.channel_count": channels, "gps.field_592": TIME_RAIM}}
    for message in FILTERED_MESSAGES
    for channels in CHANNEL_COUNTS
    for arguments in FILTER_ARGUMENTS
]
# The 58503A 3704's numeric request, its pllc_exchange's
# (harness.RECEIVED's shape, NUMERIC_DESCRIPTOR_58503A), and *TST?'s
# action, its descriptor at 0x413e2, for SET_UP_REQUESTS' functions and
# pll_scpi_command, which passes validate_or_doaction what
# get_setup_vars stored (0x4333a), as validate_or_doaction's callers
# do: each of the two in turn.
NUMERIC_REQUEST_58503A = struct.pack(">IIBBBBII", 0, NUMERIC_DESCRIPTOR_58503A, 0, 0, 0, 0, 60, 0)
ACTION_REQUEST_58503A = struct.pack(">IIBBBBII", 0, 0x413E2, 0, 0, 0, 0, 0, 0)
# The 58503A 3704's Primary oven current check, 1 while pll.stage is
# holdover to normal, 3 to 6, which it compares as a signed byte
# (0x221c4): each stage from powerup recovery to diag, the bounds' either
# side, and the bytes a signed compare takes as negative; and the
# health monitor's step at that channel (ADC_OVEN_CURRENT), whose report
# the check gates (0x2220a), the oven warm and not: the Oven channel's
# monitor (ADC_OVEN) without a value, its nominal 4.0 not below -2.0,
# and with one below it (double_oven_warmed_up, adc_average).
PLL_CHECK_STAGES = (*range(2, 8), 0x80, 0xFF)
ADC_OVEN = 3
ADC_OVEN_CURRENT = 6
# The antenna current's, its last (smartclock.h, adc_channel).
ADC_ANTENNA_CURRENT = 7
WARM_OVEN = {f"adc_monitors[{ADC_OVEN}].valid": 1, f"adc_monitors[{ADC_OVEN}].value": -3.0}
OVEN_CHECK_CASES = [{PRESETS: {"pll.stage": stage}} for stage in PLL_CHECK_STAGES]


def nmea_frame(sentence: str) -> bytes:
    """A sentence as the Z3815A 4010's NMEA framer leaves it (nmea_frame,
    its types/smartclock.h): a NUL for each comma, a null cursor, its
    fields and each one's offset, the first 1, past the '$'."""
    text = sentence.encode().replace(b",", b"\0")
    starts = [1] + [i + 1 for i, c in enumerate(sentence) if c == ","]
    return (text.ljust(0x52, b"\0") + struct.pack(">IBB", 0, len(starts), 2)
            + bytes(starts).ljust(0x14, b"\0"))


# The Z3815A 4010's gps_response_match, for each match kind a
# sentence sent and responses that answer it, differ from it and are
# another's, set and queried, the position held and not
# (gps_sentence_match_kind): GLL by GGA, a latitude 12 and 18 mas off
# and at the pole; rrs by rsd; set,H and set,M by ssd, a height 10 and
# 20 cm off; srq, rrq and tst by theirs; clr; and ZDA, which awaits none.
GLL_SENT = "$GPGLL,3723.4567,N,12201.2345,W\r\n"
GGA = "$GPGGA,123456,%s,N,12201.2345,W,1,08"
MATCH_EXCHANGES_Z3815A = (
    (0x3B, GLL_SENT, (GGA % "3723.4567", GGA % "3723.4569", GGA % "3723.4570", "$GPGSA,A,3")),
    (0x3B, "$GPGLL,9000.0000,N,12201.2345,W\r\n",
     ("$GPGGA,123456,9000.0000,N,00000.0000,E,1,08", GGA % "3723.4567")),
    (0x40, "$PFEC,GPrrs,1,2,3\r\n", ("$PFEC,GPrsd,1,2,3", "$PFEC,GPrsd,1,2,4", "$PFEC,GPssd,1")),
    (0x3C, "$PFEC,GPset,H123.4\r\n", ("$PFEC,GPssd,M05,H123.4", "$PFEC,GPssd,M05,H123.5",
                                      "$PFEC,GPssd,M05,H123.6", "$PFEC,GPssd,M05", "$PFEC,GPrsd,1")),
    (0x39, "$PFEC,GPset,M10\r\n", ("$PFEC,GPssd,M10,H123.4", "$PFEC,GPssd,M05,H123.4")),
    (0x4A, "$PFEC,GPsrq\r\n", ("$PFEC,GPssd,M10", "$PFEC,GPrsd,1")),
    (0x4B, "$PFEC,GPrrq\r\n", ("$PFEC,GPrsd,1", "$PFEC,GPssd,1")),
    (0x4C, "$PFEC,GPint,tst00\r\n", ("$PFEC,GPtst,1", "$PFEC,GPssd,1")),
    (0x41, "$PFEC,GPclr\r\n", ("$PFEC,GPtst,1",)),
    (0x3A, "$GPZDA,123456,01,02,2026\r\n", ("$PFEC,GPtst,1",)),
)
MATCH_CASES_Z3815A = [
    {0: message, 1: sent.encode(), 2: len(sent), 3: mode, 4: message, 5: nmea_frame(response),
     6: len(response), PRESETS: {"gps.position_hold": held}}
    for message, sent, responses in MATCH_EXCHANGES_Z3815A
    for response in responses
    for mode in (0, 1)
    for held in (0, 1)
]
# The Z3815A 4010's gps_strength_bits about its thresholds, 43, 47 and
# 51; and command_monitor's status monitor watching, the status fresh or
# not, rrm fresh or not, a time new or repeated, the repeats and the
# statuses without rrm at their bounds' either side, and rrm due or not
# (field_592's Time RAIM bit, rrm received before), and its command monitor in each state,
# enabled, a command failed.
STRENGTHS_Z3815A = (0, 42, 43, 46, 47, 50, 51, 255)
STATUS_MONITOR_CASES_Z3815A = [
    {PRESETS: {"gps.status_monitor_state": 1, "gps.status_monitor_enabled": 1, "gps.status_rate": 1,
               "gps_status_fresh": fresh, "gps_raim_fresh": raim_fresh, "gps.status.time.hour": hour,
               "gps.status_repeat_count": repeats, "status_without_traim_count": without,
               "gps.field_592": raim_bit, "gps.raim_received": received, "gps.raim.raim_on": 1,
               "gps.raim.rate": 1}}
    for fresh in (0, 1)
    for raim_fresh in (0, 1)
    for hour in (0, 1)
    for repeats in (45, 46)
    for without in (19, 20)
    for raim_bit in (0, 0x20)
    for received in (0, 1)
]
COMMAND_MONITOR_CASES_Z3815A = [
    {PRESETS: {"gps.command_monitor_state": state, "gps.command_monitor_enabled": 1,
               "gps.command_monitor_error": 1}}
    for state in range(4)
]
# The Z3815A 4010's gps_print_command on command_log (0x101da8)
# holding one entry, set,M's, received as @@Ag's and translated or not,
# set and queried, its time the clock's or not.
COMMAND_LOG_Z3815A = 0x101DA8
COMMAND_LOG_ENTRY_Z3815A = 0x16
PRINT_COMMAND_CASES_Z3815A = [
    {0: COMMAND_LOG_Z3815A, 1: bytes(62),
     PRESETS: {"command_log.read": COMMAND_LOG_Z3815A,
               "command_log.write": COMMAND_LOG_Z3815A + COMMAND_LOG_ENTRY_Z3815A,
               "command_log.entries[0].sender": 2, "command_log.entries[0].untranslated_code": received,
               "command_log.entries[0].code": 0x39, "command_log.entries[0].message": 0x39,
               "command_log.entries[0].query": query, "command_log.entries[0].from_clock": from_clock}}
    for received in (0x39, 0x06)
    for query in (0, 1)
    for from_clock in (0, 1)
]
# The Z3815A 4010's satellite_track_class on satellite 5's record, its
# health at +8, each health and one past, in Time RAIM's set and not;
# and gps_console_request on each of its steps, from the setting's
# senders.
TRACK_CLASS_CASES_Z3815A = [
    {0: bytes([5, 0, 0, 0, 0, 0, 0, 0, health, 0]), 1: isolated}
    for health in range(5)
    for isolated in (0, 1 << 4)
]
CONSOLE_REQUEST_CASES_Z3815A = [
    {0: code, 1: struct.pack(">IIII", 1, 0, 0, 0), 2: sender}
    for code in range(0x4D, 0x6A)
    for sender in (2, 3)
]
# The Z3815A 4010's port E link (puck_link, 0x102374): puck_efc_correct
# about each of its tests, the error in range and not, efc_compensation
# off, on and its 1, the correction's restart, the frames since the EFC
# was set about its bounds, in n3 lock pll's pll state and not, and the
# error logged or not; and puck_byte_done on a 0x55 frame, its cursor
# short of the last byte and at it, its sum right and wrong.
PUCK_Z3815A = 0x102374
PUCK_FRAME_Z3815A = PUCK_Z3815A + 0x18
PUCK_CORRECT_CASES_Z3815A = [
    {PRESETS: {"puck.efc_adc_expected": 1000 + error, "efc_adc_reading": 1000, "efc_compensation": compensation,
               "puck.correction_restart": restart, "puck.frames_since_efc": frames, "pll.stage": 6,
               "pll.n3_lock_state": state, "puck.adc_error_logged": logged,
               "scpi_value_DIAGnostic_ROSCillator_EFControl_DADC": 16.0, "efc_compensation_debug": 1}}
    for error in (40, 1677721)
    for compensation in (0, 1, 2)
    for restart in (0, 1)
    for frames in (0, 1, 2, 3)
    for state in (12, 11)
    for logged in (0, 1)
]
PUCK_FRAME_55_Z3815A = (0x55, 0x12, 0x34, 0x56, 0x01, 0x55 + 0x12 + 0x34 + 0x56 + 0x01 & 0xFF)
PUCK_BYTE_CASES_Z3815A = [
    {PRESETS: {**{f"puck.frame[{i}]": byte for i, byte in enumerate(PUCK_FRAME_55_Z3815A[:5])},
               "puck.frame[5]": PUCK_FRAME_55_Z3815A[5] ^ wrong, "puck.frame_length": 6,
               "puck.cursor": PUCK_FRAME_Z3815A + at, "puck.framing": 1}}
    for at in (4, 5)
    for wrong in (0, 1)
]
# The Z3815A 4010's n3_lock_pll in each of its states and one past,
# running and starting, its debugging on and off, and in its pll state
# about the loop's passes' end, averaging and not, the seating count at
# its bound; and efc_calibration_measure in each of its steps and one
# past, its point and settling about their bounds, and its check with a,
# b and :DADC below, within and above their bounds.
N3_LOCK_CASES_Z3815A = [
    {0: running, PRESETS: {"pll.n3_lock_state": state, "switch_word_bit_4_copy": 1, "pll_debug_enabled": debug,
                           "n3_lock.pll_passes": 60, "n3_lock.phase_passes": 1, "n3_lock.pass": 0}}
    for state in range(16)
    for running in (0, 1)
    for debug in (0, 1)
] + [
    {0: 1, PRESETS: {"pll.n3_lock_state": 12, "switch_word_bit_4_copy": copy, "pll_debug_enabled": 1,
                     "n3_lock.pll_passes": 60, "n3_lock.phase_passes": 1, "n3_lock.pass": at,
                     "n3_lock.averaging": averaging, "n3_lock.seating_count": 299, "n3_lock.ti_average": 2e-09,
                     "n3_lock.phase_offset": offset}}
    for at in (59, 60, 61, 62)
    for averaging in (0, 1)
    for copy in (0, 1)
    for offset in (0.0, 1e-08)
]
EFC_CALIBRATION_CASES_Z3815A = [
    {0: 1, PRESETS: {"efc_calibration.step": step, "efc_calibration.point": point, "efc_calibration.settle": 11,
                     "pll_debug_enabled": 1, "gps.references[0].valid": 1}}
    for step in range(10)
    for point in (0, 9, 10)
] + [
    {0: 1, PRESETS: {"efc_calibration.step": 8, "scpi_value_DIAGnostic_ROSCillator_EFControl_ASLOPe": a,
                     "scpi_value_DIAGnostic_ROSCillator_EFControl_BSLOPe": b,
                     "scpi_value_DIAGnostic_ROSCillator_EFControl_DADC": dadc}}
    for a in (1e-16, 1e-12, 1e-10)
    for b in (1e-31, 1e-24, 1e-21)
    for dadc in (14.0, 16.0, 18.0)
]
# The Z3815A 4010's :DIAGnostic:ROSCillator:PTESt time uncertainty test
# (ptest, 0x1026fe): ptest_check running about each of its bounds, its
# passes about :COUNt, its hours about each :TTHR, predicted and
# measured, valid and not, the uncertainty within the first :PTHR and
# not, and in each state; ptest_update armed and running, its
# uncertainty valid or not, within the second :PTHR or not;
# ptest_mode_done and ptest_type_done in each state, the mode off and
# on; ptest_duration_hours in each state about an hour's rounding;
# ptest_log by type, valid and not, about its clamps and rounding, its
# entries logged, but an armed test's, whose entry keeps the caller's
# return address as its second long (quirks.md), each copy's own; ptest_efc_average
# about its clamps; and measured_uncertainty_compute and
# measured_uncertainty_update over a history whose span holds and does
# not, its first entry weighted and not, debugging on and off.  The
# uptime is 0, so that a running test's hours are its start's negation.
PTEST_HOUR_Z3815A = 3600
# The event log's cursor at log_write_page's first entry (0x10240c, +4)
# and its lock's exchange an id, so that the test's log entries are
# written and compared (LOG_CURSORS).
PTEST_LOG_Z3815A = {"log_cursor": 0x102410, "log_mutex.exchange": LOCK_EXCHANGES[0]}
PTEST_SETTINGS_Z3815A = {
    "scpi_value_DIAGnostic_ROSCillator_PTESt_MODE": 1,
    "scpi_value_DIAGnostic_ROSCillator_PTESt_COUNt": 3,
    "scpi_value_DIAGnostic_ROSCillator_PTESt_TTHR[0]": 2,
    "scpi_value_DIAGnostic_ROSCillator_PTESt_TTHR[1]": 5,
    "scpi_value_DIAGnostic_ROSCillator_PTESt_PTHR[0]": 1e-06,
    "scpi_value_DIAGnostic_ROSCillator_PTESt_PTHR[1]": 8e-07,
}
PTEST_CHECK_CASES_Z3815A = [
    {PRESETS: {**PTEST_SETTINGS_Z3815A, "ptest.state": 2, "scpi_value_DIAGnostic_ROSCillator_PTESt_TYPE": kind,
               "ptest.passes": passes, "ptest.start_uptime": -hours * PTEST_HOUR_Z3815A & 0xFFFFFFFF,
               "pll.measured_uncertainty_valid": valid, "pll.predicted_uncertainty": uncertainty,
               "pll.measured_uncertainty": -uncertainty}}
    for kind in (0, 1)
    for passes in (2, 3)
    for hours in (1, 2, 4, 5)
    for valid in (0, 1)
    for uncertainty in (5e-07, 1e-06, 2e-06)
] + [{PRESETS: {**PTEST_SETTINGS_Z3815A, "ptest.state": state}} for state in range(6)]
PTEST_UPDATE_CASES_Z3815A = [
    {PRESETS: {**PTEST_SETTINGS_Z3815A, **(PTEST_LOG_Z3815A if state != 1 else {}), "ptest.state": state,
               "scpi_value_DIAGnostic_ROSCillator_PTESt_TYPE": kind, "ptest.passes": 1, "pll.measured_uncertainty_valid": valid, "pll.predicted_uncertainty": uncertainty,
               "pll.measured_uncertainty": uncertainty}}
    for state in range(4)
    for kind in (0, 1)
    for valid in (0, 1)
    for uncertainty in (5e-07, -5e-07, 9e-07, 2e-06)
]
PTEST_MODE_CASES_Z3815A = [
    {PRESETS: {**PTEST_SETTINGS_Z3815A, "scpi_value_DIAGnostic_ROSCillator_PTESt_MODE": mode, "ptest.state": state,
               "ptest.passes": 3}}
    for mode in (0, 1)
    for state in range(6)
]
PTEST_DURATION_CASES_Z3815A = [
    {PRESETS: {"ptest.state": state, "ptest.start_uptime": -elapsed & 0xFFFFFFFF, "ptest.end_uptime": 0}}
    for state in range(6)
    for elapsed in (0, 899, 900, 4499, 4500)
]
PTEST_LOG_CASES_Z3815A = [
    {0: uncertainty, 1: valid, PRESETS: {**PTEST_LOG_Z3815A, "scpi_value_DIAGnostic_ROSCillator_PTESt_TYPE": kind}}
    for kind in (0, 1)
    for valid in (0, 1)
    for uncertainty in (1e-07, -1e-07, 0.0, 2.0, -2.0, 2.4e-10, -2.6e-10)
]
PTEST_AVERAGE_CASES_Z3815A = [
    {PRESETS: {"pll.efc_history.stop_ptr": stop, f"pll.efc_history.averages[{stop - 1 & 31}]": average}}
    for stop in (0, 5)
    for average in (-3.0, -0.4, 100.4, 2e09)
]
PTEST_NEWEST_Z3815A = 10
PTEST_HISTORY_Z3815A = {
    "aging_curve.newest_index": PTEST_NEWEST_Z3815A,
    "pll.efc_history.replaced_time": 100.0 + PTEST_NEWEST_Z3815A - 32,
    "scpi_value_DIAGnostic_ROSCillator_EFControl_ASLOPe": 1e-12,
    "scpi_value_DIAGnostic_ROSCillator_EFControl_BSLOPe": 3e-24,
    "aging_curve.a": 2.0, "aging_curve.b": 0.02, "aging_curve.c": 0.25,
    **{f"pll.efc_history.times[{i}]": 100.0 + i for i in range(32)},
    **{f"pll.efc_history.averages[{i}]": 1.0 + 0.03 * i for i in range(32)},
    **{f"pll.efc_history.weights[{i}]": 1 for i in range(32)},
    **{f"pll.efc_history.fit_a_history[{i}]": 1.0 for i in range(32)},
    **{f"pll.efc_history.fit_b_history[{i}]": 0.01 for i in range(32)},
    **{f"pll.efc_history.fit_c_history[{i}]": 0.5 for i in range(32)},
}
MEASURED_UNCERTAINTY_CASES_Z3815A = [
    {0: entries, PRESETS: {**PTEST_HISTORY_Z3815A, "pll_debug_enabled": debug,
                           f"pll.efc_history.times[{PTEST_NEWEST_Z3815A - entries & 31}]":
                               100.0 + PTEST_NEWEST_Z3815A - entries + shift,
                           f"pll.efc_history.weights[{PTEST_NEWEST_Z3815A - entries & 31}]": weight,
                           "pll.efc_history.replaced_weight": weight,
                           **({"pll.efc_history.replaced_time": 100.0 + PTEST_NEWEST_Z3815A - 32 - shift}
                              if entries == 32 else {})}}
    for entries in (8, 32)
    for shift in (0.0, 0.4, 0.6, -0.6)
    for weight in (0, 2)
    for debug in (0, 1)
]
MEASURED_UPDATE_CASES_Z3815A = [
    {PRESETS: {**PTEST_HISTORY_Z3815A, "pll.efc_history.replaced_weight": weight}} for weight in (0, 3)
]
# The Z3815A 4010's EFC history (pll.efc_history, 0x1028cc, 32 entries)
# and aging fit.  efc_history_update in each aging state, settled and
# not; collecting, in each stage that sums and one that does not, with
# the switch word's bit 11 and standby_from_unlock's standby (its 1 in n3 lock
# pll's pll state, the interval mean between 4 and 5 ns, while
# standby_from_unlock_ready is set), nothing summed and some, an entry not yet due; and an entry due with each weight's bounds of summed samples and
# uptime, its debugging on and off, and its ring wrapping and full.
# aging_fit with no, 16 and 17 entries well weighted, the oldest before
# half a day and after; fit_quality in each mode, its span wrapping or
# not; fit_linear_halves, fit_constant and linear_regression over a
# history whose weights leave none, two, three and all of its entries,
# and linear_regression's x all one; fit_three_point on three points,
# two x the same, in line, the outer y equal, and a curvature too small
# for a float, the outer y equal and not; and kenneth_measured_get valid
# and not.
EFC_HISTORY_Z3815A = 0x1028CC
EFC_HISTORY_AVERAGES_Z3815A = EFC_HISTORY_Z3815A + 0x0A
EFC_HISTORY_WEIGHTS_Z3815A = EFC_HISTORY_Z3815A + 0x8E
EFC_HISTORY_TIMES_Z3815A = EFC_HISTORY_Z3815A + 0xB0
EFC_HISTORY_ENTRIES_Z3815A = 32


def efc_history_z3815a(weights: list[int], first_time: float = 3.0, shift: int = 0) -> dict:
    """Presets of the history's entries: the weights given, each time
    first_time on from its entry shift places back, the averages about a
    line in time, the log terms rising."""
    presets = {}
    for i, weight in enumerate(weights):
        time = first_time + (i + shift) % EFC_HISTORY_ENTRIES_Z3815A
        presets[f"pll.efc_history.weights[{i}]"] = weight
        presets[f"pll.efc_history.times[{i}]"] = time
        presets[f"pll.efc_history.averages[{i}]"] = 524288.0 + 0.25 * time + 0.5 * (i % 3)
        presets[f"pll.efc_history.logs[{i}]"] = 0.05 * i
    return presets


def efc_weights_z3815a(well: int, partial: int) -> list[int]:
    """well entries of weight 2 or 3 in turn, then partial of 1, the rest
    empty."""
    weights = [2 + i % 2 for i in range(well)] + [1] * partial
    return weights + [0] * (EFC_HISTORY_ENTRIES_Z3815A - len(weights))


EFC_CALIBRATED_Z3815A = {
    "pll.efc": 524288.0,
    "scpi_value_DIAGnostic_ROSCillator_EFControl_ASLOPe": 5e-13,
    "scpi_value_DIAGnostic_ROSCillator_EFControl_BSLOPe": 3.3e-24,
}
EFC_HISTORY_COLLECTING_Z3815A = {
    "pll.aging_state": 3, "pll.efc": 524288.5, "scpi_value_DIAGnostic_ROSCillator_TCOefficient": 1000.0,
    "efc_history_base": 524000.25, "pll.efc_history.efc_sum": 1.5e6,
}
EFC_HISTORY_CASES_Z3815A = [
    {PRESETS: {"pll.aging_state": state, "pll.efc_history.loop_settled": settled, "clock.uptime": 5000}}
    for state in range(5)
    for settled in (0, 1)
] + [
    {PRESETS: {**EFC_HISTORY_COLLECTING_Z3815A, "pll.stage": stage, "pll.startup_state": startup,
               "switch_word_bit_11": bit_11, "pll.efc_history.summed_count": summed,
               "pll.efc_history.next_entry_uptime": 0xFFFFFFFF, "clock.uptime": 5000,
               "pll.n3_lock_state": 12, "interval_mean": 4.5e-09, "standby_from_unlock_ready": standby}}
    for stage in (5, 6, 7)
    for startup in (0, 1)
    for bit_11 in (0, 1)
    for summed in (0, 5)
    for standby in (0, 1)
] + [
    {PRESETS: {**EFC_HISTORY_COLLECTING_Z3815A, **efc_history_z3815a(efc_weights_z3815a(20, 4)),
               "pll.stage": stage, "pll.efc_history.summed_count": summed, "pll.efc_history.next_entry_uptime": 0,
               "clock.uptime": uptime, "pll_debug_enabled": debug, "pll.efc_history.stop_ptr": stop,
               "pll.efc_history.start_ptr": start, "pll.tfom": 1e-07}}
    for stage in (6, 7)
    for summed in (0, 1, 674, 675, 2699, 2700)
    for uptime in (2700, 2701, 35099, 35100)
    for debug in (0, 1)
    for stop, start in ((31, 0), (5, 9))
]
AGING_FIT_CASES_Z3815A = [
    {PRESETS: {**EFC_CALIBRATED_Z3815A, **efc_history_z3815a(efc_weights_z3815a(well, 4), first_time),
               "pll_debug_enabled": debug}}
    for well in (0, 3, 16, 17, 28)
    for first_time in (10.0, 20.0)
    for debug in (0, 1)
]
AGING_CURVE_FORMAT_Z3815A = ">BBIIiiffffff"
FIT_QUALITY_CASES_Z3815A = [
    {0: struct.pack(AGING_CURVE_FORMAT_Z3815A, mode, 2, 3, 34, oldest, newest, 524289.0, 0.25, 0.1, 0.0, 0.5, 0.0),
     PRESETS: {**EFC_CALIBRATED_Z3815A, **efc_history_z3815a(efc_weights_z3815a(32, 0))}}
    for mode in range(6)
    for oldest, newest in ((0, 31), (20, 5), (7, 7))
]
FIT_HISTORY_CASES_Z3815A = [
    {0: struct.pack(AGING_CURVE_FORMAT_Z3815A, 0, minimum, 0, 0, 0, 0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0),
     PRESETS: efc_history_z3815a(efc_weights_z3815a(well, partial), shift=shift)}
    for minimum in (1, 2)
    for well, partial in ((0, 0), (0, 2), (2, 1), (3, 0), (32, 0))
    for shift in (0, 9)
]
LINEAR_REGRESSION_CASES_Z3815A = [
    {0: struct.pack(">BBIIIfff", minimum, 0, EFC_HISTORY_TIMES_Z3815A, EFC_HISTORY_AVERAGES_Z3815A,
                    EFC_HISTORY_WEIGHTS_Z3815A, 0.0, 0.0, 0.0),
     1: slope,
     PRESETS: {**efc_history_z3815a(efc_weights_z3815a(well, partial)),
               **({f"pll.efc_history.times[{i}]": 5.0 for i in range(EFC_HISTORY_ENTRIES_Z3815A)} if same else {})}}
    for minimum in (1, 2)
    for well, partial in ((0, 0), (2, 0), (2, 1), (3, 0), (32, 0))
    for slope in (0, 1)
    for same in (False, True)
]
THREE_POINT_CASES_Z3815A = [
    {0: struct.pack(">IIff", EFC_HISTORY_TIMES_Z3815A, EFC_HISTORY_AVERAGES_Z3815A, 0.0, 0.0),
     PRESETS: {**{f"pll.efc_history.times[{i}]": x for i, x in enumerate(xs)},
               **{f"pll.efc_history.averages[{i}]": y for i, y in enumerate(ys)}}}
    for xs, ys in (
        ((1.0, 2.0, 3.0), (1.0, 0.0, 2.0)),
        ((1.0, 2.0, 3.0), (1.0, 0.0, 1.0)),
        ((1.0, 1.0, 3.0), (1.0, 0.0, 2.0)),
        ((1.0, 3.0, 3.0), (1.0, 0.0, 2.0)),
        ((3.0, 2.0, 3.0), (1.0, 0.0, 2.0)),
        ((1.0, 2.0, 3.0), (1.0, 2.0, 3.0)),
        ((1e12, 2e12, 4e12), (1e-30, 0.0, 0.0)),
        ((1e12, 2e12, 4e12), (0.0, 1e-30, 0.0)),
    )
]
KENNETH_MEASURED_CASES_Z3815A = [
    {0: bytes(4), 1: bytes(1), PRESETS: {"pll.measured_uncertainty": 2.5e-07, "pll.measured_uncertainty_valid": valid}}
    for valid in (0, 1, 2)
]
# The Z3815A 4010's n3_lock_slew about its bounds: TIs within 100 ns,
# within the stopping TI and past it either way, the offset within a step,
# past it and at its bound either way, steps and scales, and its debug
# line; fine_sync_adjust and holdover_within_limit on TIs about 90 ns,
# 100 ns and 300 ns either way; and interval_next_reading in
# each arm mode, its States drawn for each.
SLEW_TIS_Z3815A = (0.0, 5e-08, -5e-08, 2e-07, -2e-07, 7e-07, -7e-07, 1e-05, -1e-05)
SLEW_CASES_Z3815A = [
    {0: ti, 1: most, 2: step, 3: scale, PRESETS: {"n3_lock.slew_offset": offset, "pll_debug_enabled": debug}}
    for ti in SLEW_TIS_Z3815A
    for offset in (0.0, 0.5, -3.0, 12.0, -12.0, 2000.0)
    for most, step, scale in ((10, 1.0, 1), (1000, 0.25, 10))
    for debug in (0, 1)
]
# The Z3815A 4010's port layer on each of its two ports, the SCI's (1)
# and DUART channel B's (2), and on others (0x3c670 to 0x3c80e), and
# scpi_task_run on both and another; scpi_input_source on each session's
# port object (0x3c310, 0x3c61a); status_report_from on each source and
# past them (0x3fadc), each session's status ready; and
# status_prompt_refresh on each session's status model and another; and
# :PTIMe:TDATe:DEFault? on each of its nodes' path words, the clock set,
# its GPS-UTC offset known, and keeping either timescale.
SCPI_PORTS_Z3815A = (0, 1, 2, 3)
SCPI_PORT_OBJECTS_Z3815A = (0x3C310, 0x3C61A)
SCPI_MODELS_Z3815A = {"scpi_status_model": 0x104000, "sci_scpi_status_model": 0x104100}
TDATE_CLOCK_Z3815A = {"clock.set": 1, "clock.gps_utc_offset": 13, "clock.gps_utc_offset_valid": 1}
SCPI_PORT_CASES_Z3815A = {
    **{name: [{0: port} for port in SCPI_PORTS_Z3815A]
       for name in ("port_status_report", "port_report_errors", "port_prompt_get", "port_prompt_set",
                    "port_session_init", "port_object")},
    "port_update_rqs": [{2: port} for port in SCPI_PORTS_Z3815A],
    "scpi_task_run": [{0: port} for port in (1, 2, 0)],
    "scpi_input_source": [{0: port} for port in SCPI_PORT_OBJECTS_Z3815A],
    "status_report_from": [
        {0: code, 1: source, PRESETS: {"status_ready": 1, "sci_status_ready": 1}}
        for source in range(6)
        for code in (-100, 3)
    ],
    "scpi_query_SOURce_PTIMe_TDATe_DEFault": [
        {0: struct.pack(">h", path), PRESETS: {**TDATE_CLOCK_Z3815A, "nv_settings.gps_utc": utc}}
        for path in (-1, 1, 2)
        for utc in (0, 1)
    ],
    "status_prompt_refresh": [
        {0: model, 1: error, PRESETS: SCPI_MODELS_Z3815A}
        for model in (*SCPI_MODELS_Z3815A.values(), 0x104200)
        for error in (0, -113)
    ],
}
# The Z3815A 4010's SCPI handlers that take either port or answer by the
# session asking: the ALARm port's :FDUPlex, :RECeive:PACE and :PRESet on
# each port and past them (0x44204, 0x4462a, 0x44350), and
# :SYSTem:LANGuage?, :DIAGnostic:ADC?, :LOG:READ:ALL? and :TEST:FLASh? on
# each session's port object; :DIAGnostic:ROSCillator:PTESt:STATe? on
# each of the test's states and one past them (0x3f232), and
# :DIAGnostic:GPSystem:ACURrent:STATe? with the antenna current's alarm
# on, its average normal and past its most (0x3eb46); :KENneth:MEASured?
# with an uncertainty and without, its resolution negative (0x43e54);
# and :DIAGnostic:LOG:WRAP? and :ROSCillator:PTESt:MODE? on a byte past
# 0x7f, which they sign-extend (0x3ea22, 0x3f06c).
SCPI_EITHER_PORT_CASES_Z3815A = {
    **{name: [{0: struct.pack(">hhH", port, 0, value)} for port in SCPI_PORTS_Z3815A for value in (0, 1)]
       for name in ("scpi_set_SYSTem_COMMunicate_ALARm_FDUPlex", "scpi_set_SYSTem_COMMunicate_ALARm_RECeive_PACE",
                    "scpi_set_SYSTem_COMMunicate_ALARm_PRESet")},
    **{name: [{INPUT_PORT: lambda m, port=port: port} for port in SCPI_PORT_OBJECTS_Z3815A]
       for name in ("scpi_query_SYSTem_LANGuage", "scpi_query_DIAGnostic_ADC", "scpi_query_DIAGnostic_LOG_READ_ALL",
                    "scpi_query_DIAGnostic_TEST_FLASh")},
    "scpi_query_DIAGnostic_ROSCillator_PTESt_STATe": [
        {PRESETS: {"ptest.state": state, "ptest.passes": state + 1}} for state in range(6)
    ],
    "scpi_query_DIAGnostic_GPSystem_ACURrent_STATe": [
        {PRESETS: {"nv_settings.antenna_current_alarm": 1, f"adc_monitors[{ADC_ANTENNA_CURRENT}].valid": 1,
                   f"adc_monitors[{ADC_ANTENNA_CURRENT}].value": value}}
        for value in (40.0, 100.0)
    ],
    "scpi_query_KENneth_MEASured": [
        {PRESETS: {"pll.measured_uncertainty": 2.5e-07, "pll.measured_uncertainty_valid": valid}} for valid in (0, 1)
    ],
    "scpi_query_DIAGnostic_LOG_WRAP": [{PRESETS: {"nv_settings.log_wrap": value}} for value in (1, 0xFF)],
    "scpi_query_DIAGnostic_ROSCillator_PTESt_MODE": [
        {PRESETS: {"scpi_value_DIAGnostic_ROSCillator_PTESt_MODE": value}} for value in (1, 0xFF)
    ],
}
# es_status for a date with a leap second pending on it and on the day
# before, its minute 61 seconds and 59, and none pending, in holdover,
# its recovery and normal pll (0x23fdc to 0x24016): a packed date,
# 2008-12-31's.
ES_LEAP_DATE_Z3815A = 0x07D80C1F
ES_STATUS_CASES_Z3815A = [
    {
        0: ES_LEAP_DATE_Z3815A,
        PRESETS: {"nv_settings.leap_pending": pending, "nv_settings.leap_date": date,
                  "nv_settings.leap_minute_length": length, "pll.stage": stage},
    }
    for pending, date, length in ((1, ES_LEAP_DATE_Z3815A, 61), (1, ES_LEAP_DATE_Z3815A, 59),
                                  (1, ES_LEAP_DATE_Z3815A - 1, 61), (0, ES_LEAP_DATE_Z3815A, 61))
    for stage in (3, 4, 7)
]
# The pForth word pr_pll on each of the loop's stages and one past, and
# each sub-state of powerup, holdover recovery, n3 lock pll and diag and
# one past, by the jump tables at 0x2c1d8, 0x2c224, 0x2c328, 0x2c3d6 and
# 0x2c526.
PR_PLL_CASES_Z3815A = [
    {PRESETS: {"pll.stage": stage, field: state}}
    for stage, field, states in ((1, "pll.powerup_state", 9), (4, "pll.recovery_state", 7),
                                 (6, "pll.n3_lock_state", 16), (8, "pll.gps_diag_state", 7))
    for state in range(states)
] + [{PRESETS: {"pll.stage": stage}} for stage in (0, 2, 3, 5, 7, 9, 10, 11, 0xFF)]
# holdover_recovery's clock check passing, every cause gone, about its
# 250 s of holdover: the counter not read (DAT_0010218b), so that the
# reading is valid and the clock's check passes, the leap second's
# acknowledgment idle and the GPS-UTC offset valid.
RECOVERY_DONE_Z3815A = {
    "pll.recovery_state": 5, "DAT_0010218b": 1, "pll.stage": 4, "pll.hold_causes": 0, "internal_pps_dead": 0,
    "gps_pps_dead": 0, "pll.leap_ack_state": 0, "pll.leap_ack_requested": 0, "clock.gps_utc_offset_valid": 1,
}
# The Z3815A 4010's flash cases, its installer's over its writable range,
# 0x20000 to 0x7ffff, one 16-bit part (harness.WordFlash), as FLASH's
# are the others': installer_flash_program_word's addresses, the range's
# ends, each block's, and past each end; installer_flash_erase_block's
# blocks, each of the range's, the primary's test block and one below
# the range, in no modeled block; :ERASe?'s flash erased from the range's
# start, wholly, all but its last word and all but its first, the boot
# code's 128 KB below it kept, which a range from 0x10000 reads; and
# :DOWNload's records, as FLASH_RECORDS over this range: an S1, always
# below it, an S2 and S3 at its start, an odd count, one across the
# second block's start, one running past the range's end, one below it,
# an S0, an S5 and an S9, a bad checksum and texts that are no record.
FLASH_START_Z3815A = 0x20000
FLASH_ADDRESSES_Z3815A = (0x20000, 0x3FFFE, 0x40000, 0x7FFFE, 0x1FFFE, 0x80000)
FLASH_BLOCKS_Z3815A = (0x20000, 0x40000, 0x60000, 0x4000, 0x10000)
FLASH_RECORDS_Z3815A = [
    s_record(1, 0x1000, b"\x12\x34", 2),
    s_record(2, 0x020000, bytes(range(8)), 3),
    s_record(3, 0x00020000, b"\xa5\x5a\x00\xff", 4),
    s_record(2, 0x020000, b"\x01\x02\x03", 3),
    s_record(2, 0x03FFFC, bytes(range(8)), 3),
    s_record(2, 0x07FFFC, bytes(range(8)), 3),
    s_record(2, 0x01FFFE, b"\x11\x22", 3),
    s_record(0, 0, b"HDR", 2),
    s_record(5, 3, b"", 2),
    s_record(9, 0x1000, b"", 2),
    s_record(2, 0x020000, b"\x12\x34", 3, checksum=0),
    *(kind + s_record(2, 0x020000, b"\x12\x34", 3)[2:] for kind in (b"S4", b"X2", b"s2")),
]


def erased_flash_z3815a(image: bytes, last: bytes = b"", first: bytes = b"") -> bytes:
    """The Z3815A 4010's flash with its writable range erased, its first
    bytes first and its last bytes last."""
    erased = erased_flash(image, last, FLASH_START_Z3815A)
    return erased[:FLASH_START_Z3815A] + first + erased[FLASH_START_Z3815A + len(first) :]


FLASH_CASES_Z3815A = {
    "installer_flash_program_word": [
        {0: address, 1: word} for address in FLASH_ADDRESSES_Z3815A for word in FLASH_WORDS
    ],
    "installer_flash_erase_block": [{0: block} for block in FLASH_BLOCKS_Z3815A],
    "installer_scpi_query_DIAGnostic_ERASe": [
        {FLASH: erased_flash_z3815a},
        {FLASH: lambda image: erased_flash_z3815a(image, last=b"\x00\x01")},
        {FLASH: lambda image: erased_flash_z3815a(image, first=b"\x7f\xff")},
    ],
    "installer_scpi_set_DIAGnostic_DOWNload": [
        {0: text_parameter(record), FLASH: erased_flash_z3815a} for record in FLASH_RECORDS_Z3815A
    ],
}
SYNC_TIS_Z3815A = (0.0, 8.9e-08, -8.9e-08, 9.5e-08, -9.5e-08, 1e-07, -1e-07, 2.5e-07, -2.5e-07, 3e-07, -3e-07, 1e-06)
# The Z3815A 4010's C library's own (stdlib.c, string.c, math.c):
# strtol's, strtoul's and installer_strtol's, whose body strtol shares
# (libc.h, libc_strtol), texts, white space and signs, C's prefixes in
# either case and without a digit after, the longs' and unsigned longs'
# limits and one past, a value that wraps an unsigned long past what it
# was and one that does not, letters, nothing, a high-bit character,
# each in the bases C's prefixes, the decimal, the hexadecimal, the most
# and one past, with and without end; strstr's texts, found at the
# start, after a false start and not, an empty one in each place; and
# exp's arguments, zeros, below 2^-54, within ln 2 / 2 and past it of
# either sign, about the overflow and the underflow, the results that
# underflow its exponent field, the infinities and a NaN.
STRTOL_TEXTS_Z3815A = [
    b"  -123\0", b"+77\0", b"0x1fZ\0", b"0X1f\0", b"017\0", b"0\0", b"0xg\0", b"\t-0x80000000\0",
    b"2147483647\0", b"2147483648\0", b"-2147483648\0", b"-2147483649\0", b"4294967295\0", b"4294967296\0",
    b"99999999999\0", b"-4000000000\0", b"zZ9\0", b"\0", b"-\0", b"\x9512\0",
]
STRTOL_BASES_Z3815A = (0, 10, 16, 36, 37)
STRTOL_CASES_Z3815A = [
    {0: text, 1: end, 2: base} for text in STRTOL_TEXTS_Z3815A for base in STRTOL_BASES_Z3815A for end in (0, bytes(4))
]
STRSTR_CASES_Z3815A = [
    {0: text, 1: pattern}
    for text, pattern in (
        (b"abcabd\0", b"abd\0"), (b"abd\0", b"abd\0"), (b"abc\0", b"\0"), (b"\0", b"a\0"),
        (b"\0", b"\0"), (b"aab\0", b"ab\0"), (b"abc\0", b"abcd\0"), (b"abc\0", b"x\0"),
    )
]
# gps_script_lookup's codes, each from one before GPS_SCRIPT_INIT_DEFAULTS
# to one past GPS_SCRIPT_LONGITUDE_SET, and the word's ends; and
# oncore_hold_heights_match's heights, sent and answered, four bytes high
# first: about the least and the most the Oncore takes, within 2 cm and
# past, either way.
SCRIPT_CODES_Z3815A = (*range(0x6A, 0x82), 0, 0xFFFF)
# gps_request_build's codes, each with a descriptor, set with
# GPS_ARGUMENTS and queried: the GT-74's sentences, GPS_NMEA_MASK_ANGLE
# to GPS_NMEA_SELFTEST, whose descriptors gps_sentence_descriptors holds
# (0x5e3cc), and the Oncore requests gps_request_translations (0x5e6d4)
# takes to one of them; a code past them, or one the table takes to
# none, reads a descriptor beyond its end (gps_request_descriptor,
# 0x5c0f6), as the generated codes do; each with time RAIM supported
# and not (GPS_FIELD_592_TIME_RAIM), which rrs', int,rrm's and zzz's
# rrq's test, gps_time_raim_supported (0x5c08e), refuses them without.
# The cable delay's, set,t and the Oncore's @@Az, which
# nmea_encode_cable_delay (0x5c4b6) formats "%+07d" into an 8-byte
# field, are set with delays that fit it, from :GPS:ADELay's range, 0 to
# 999999 either way: GPS_ARGUMENTS' first overruns it and the frame
# pointer saved above it.
GPS_SENTENCE_CODES_Z3815A = (*range(0x39, 0x4D), 0x06, 0x0C, 0x05, 0x12, 0x13, 0x19)
GPS_CABLE_DELAY_CODES_Z3815A = (0x3F, 0x19)
GPS_CABLE_DELAYS = (0, 1234, 999999, -999999)
GPS_ARGUMENT_MODES = (0, 1)
TIME_RAIM_Z3815A = 0x20
GPS_REQUEST_BUILD_CASES_Z3815A = [
    {0: code, 1: mode, 2: arguments, 3: bytes(GPS_MESSAGE_ROOM), PRESETS: {"gps.field_592": raim}}
    for code in GPS_SENTENCE_CODES_Z3815A
    for mode in GPS_ARGUMENT_MODES
    for arguments in (
        [struct.pack(">i", delay) + GPS_ARGUMENTS[4:] for delay in GPS_CABLE_DELAYS]
        if code in GPS_CABLE_DELAY_CODES_Z3815A and mode == 0
        else [GPS_ARGUMENTS]
    )
    for raim in (0, TIME_RAIM_Z3815A)
]
# The Z3815A 4010's validators' step routines, load_double (0x46970),
# load_long (0x475a4) and load_long_unit_step (0x4759c);
# scpi_format_resolution_array_record's eight zero bytes, those before
# status_register_layouts (0x435d6), and scpi_input_source (0x3c84e);
# and its numeric request, its pllc_exchange's (harness), and *TST?'s
# action, scpi_action_self_test (0x49ee8), for SET_UP_REQUESTS'
# functions and pll_scpi_command, as the 58503A's.
VALIDATOR_CASES_Z3815A = validator_cases(0x46970, 0x475A4, 0x4759C)
ARRAY_RECORD_CASES_Z3815A = array_record_cases(0x435D6, 0x3C84E)
NUMERIC_REQUEST_Z3815A = struct.pack(">IIBBBBII", 0, NUMERIC_DESCRIPTOR_Z3815A, 0, 0, 0, 0, 60, 0)
ACTION_REQUEST_Z3815A = struct.pack(">IIBBBBII", 0, 0x49EE8, 0, 0, 0, 0, 0, 0)
# The Z3801A 3543's validators' step routines, load_double (0x310ae),
# load_long (0x31c32) and load_long_unit_step (0x31c2a); and its
# numeric request, its pllc_exchange's (harness), :SAMPle:INTeger's at
# this image's address, and *TST?'s action, scpi_action_self_test
# (0x4137a), for SET_UP_REQUESTS' functions and pll_scpi_command, as the
# 58503A's.
VALIDATOR_CASES_Z3801A = validator_cases(0x310AE, 0x31C32, 0x31C2A)
NUMERIC_REQUEST_Z3801A = struct.pack(">IIBBBBII", 0, SAMPLE_INTEGER_Z3801A, 0, 0, 0, 0, 7, 0)
ACTION_REQUEST_Z3801A = struct.pack(">IIBBBBII", 0, 0x4137A, 0, 0, 0, 0, 0, 0)
HOLD_HEIGHTS_Z3815A = (
    (-100001, -100001), (-100000, -99998), (-100000, -99997), (1800000, 1800002), (1800001, 1800001),
    (0, 3), (0, -2), (5, 7), (5, 2),
)
# efc_for_frequency_change's slopes, B zero of either sign, and of each
# sign against A's, so that Cardano's D is positive, with h on either side
# of its root, or not, at EFCs of either sign, and frequency changes of
# either sign and none.
EFC_CHANGE_CASES_Z3815A = [
    {
        0: delta,
        PRESETS: {
            "scpi_value_DIAGnostic_ROSCillator_EFControl_ASLOPe": a,
            "scpi_value_DIAGnostic_ROSCillator_EFControl_BSLOPe": b,
            "pll.efc": efc,
        },
    }
    for a in (1e-11, -1e-11, 2e-11)
    for b in (0.0, -0.0, 1e-22, -1e-22, 1e-20, -1e-24)
    for efc in (500000.0, 0.0, 100.0, -300000.0)
    for delta in (1e-9, -1e-9, 5e-8, 0.0)
]
EXP_ARGUMENTS_Z3815A = (
    0.0, -0.0, 1e-20, -1e-20, 0.25, -0.3, 0.5, 1.0, -1.0, 2.5, 10.0, -10.0, 100.0, 700.0, 709.7, 709.8,
    -700.0, -708.5, -740.0, -744.0, -745.0, float("inf"), float("-inf"), float("nan"),
)
IMAGE_GIVEN_CASES = {
    "z3815a-4010": {
        **{name: STRTOL_CASES_Z3815A for name in ("strtol", "strtoul", "installer_strtol")},
        "alarm_hardware_update": ALARM_HARDWARE_CASES_Z3815A,
        "strstr": STRSTR_CASES_Z3815A,
        "exp": [{0: value} for value in EXP_ARGUMENTS_Z3815A],
        "gps_script_lookup": [{0: code} for code in SCRIPT_CODES_Z3815A],
        "gps_receiver_status_update": STATUS_UPDATE_CASES_Z3815A,
        "nmea_leap_second_due": LEAP_DUE_CASES_Z3815A,
        "efc_for_frequency_change": EFC_CHANGE_CASES_Z3815A,
        "oncore_hold_heights_match": [
            {0: struct.pack(">i", sent), 1: struct.pack(">i", answered)} for sent, answered in HOLD_HEIGHTS_Z3815A
        ],
        **FLASH_CASES_Z3815A,
        "efc_history_update": EFC_HISTORY_CASES_Z3815A,
        "pllp_task": PLLP_WALK_CASES_Z3815A,
        "aging_fit": AGING_FIT_CASES_Z3815A,
        "fit_quality": FIT_QUALITY_CASES_Z3815A,
        "fit_linear_halves": FIT_HISTORY_CASES_Z3815A,
        "fit_constant": FIT_HISTORY_CASES_Z3815A,
        "fit_linear": FIT_HISTORY_CASES_Z3815A,
        "linear_regression": LINEAR_REGRESSION_CASES_Z3815A,
        "fit_three_point": THREE_POINT_CASES_Z3815A,
        "kenneth_measured_get": KENNETH_MEASURED_CASES_Z3815A,
        "n3_lock_slew": SLEW_CASES_Z3815A,
        "fine_sync_adjust": [{0: ti} for ti in SYNC_TIS_Z3815A],
        "holdover_within_limit": [{0: ti} for ti in SYNC_TIS_Z3815A] + [
            {0: ti, PRESETS: {"pll.holdover_duration": 0, "nv_settings.holdover_limit_threshold": abs(ti),
                              "nv_settings.holdover_measurement_threshold": 10, "pll.frequency_adjusted": 0}}
            for ti in (1e-07, -1e-07, 2e-07)
        ],
        "holdover_recovery": [
            {0: 1, PRESETS: {**RECOVERY_DONE_Z3815A, "pll.holdover_duration": duration}}
            for duration in (249, 250, 251, 300, 351)
        ],
        "startup_pll": [
            {0: 1, PRESETS: {"pll.startup_state": 2, "loop_steering.seconds": 9, "loop_steering.readings": 3,
                             "clock.uptime": uptime}}
            for uptime in range(7196, 7204)
        ],
        "enter_holdover": [
            {PRESETS: {"pll.stage": 3, "pll.holdover_duration": 0, "pll.hold_causes": 2, "pll.external_reference": 1,
                       "gps_pps_dead": dead}}
            for dead in (0, 1)
        ],
        "interval_next_reading": [{0: mode} for mode in (0, 1, 2) for _ in range(8)],
        "n3_lock_pll": N3_LOCK_CASES_Z3815A,
        "ptest_check": PTEST_CHECK_CASES_Z3815A,
        "ptest_update": PTEST_UPDATE_CASES_Z3815A,
        "ptest_mode_done": PTEST_MODE_CASES_Z3815A,
        "ptest_type_done": PTEST_MODE_CASES_Z3815A,
        "ptest_duration_hours": PTEST_DURATION_CASES_Z3815A,
        "ptest_log": PTEST_LOG_CASES_Z3815A,
        "ptest_efc_average": PTEST_AVERAGE_CASES_Z3815A,
        "measured_uncertainty_compute": MEASURED_UNCERTAINTY_CASES_Z3815A,
        "measured_uncertainty_update": MEASURED_UPDATE_CASES_Z3815A,
        "efc_calibration_measure": EFC_CALIBRATION_CASES_Z3815A,
        "puck_efc_correct": PUCK_CORRECT_CASES_Z3815A,
        "puck_byte_done": PUCK_BYTE_CASES_Z3815A,
        "satellite_track_class": TRACK_CLASS_CASES_Z3815A,
        "gps_console_request": CONSOLE_REQUEST_CASES_Z3815A,
        "gps_print_command": PRINT_COMMAND_CASES_Z3815A,
        "gps_response_match": MATCH_CASES_Z3815A,
        "gps_strength_bits": [{0: strength} for strength in STRENGTHS_Z3815A],
        "command_monitor": STATUS_MONITOR_CASES_Z3815A + COMMAND_MONITOR_CASES_Z3815A,
        **SCPI_PORT_CASES_Z3815A,
        **SCPI_EITHER_PORT_CASES_Z3815A,
        "es_status": ES_STATUS_CASES_Z3815A,
        "diag_pr_pll": PR_PLL_CASES_Z3815A,
        # record_word_or_default on an erased word and others (0x450d8).
        "record_word_or_default": [{0: word, 1: 0x1234} for word in (0xFFFF, 0, 0x7FFF, 0xFFFE)],
        "sci_status_happening": [{0: condition} for condition in (0x19, 0x90, 0xFF)],
        "log_event": LOG_EVENT_CASES,
        "gps_request_build": GPS_REQUEST_BUILD_CASES_Z3815A,
        **VALIDATOR_CASES_Z3815A,
        "scpi_format_resolution_array_record": ARRAY_RECORD_CASES_Z3815A,
        **NOT_GIVEN_Z3815A,
    },
    # the Z3801A 3543's eight zero bytes and scpi_input_source for
    # scpi_format_resolution_array_record's records (ARRAY_RECORD_KINDS):
    # the status presets' tables (0x2e674) and 0x2899e
    "z3801a-3543": {
        "scpi_format_resolution_array_record": array_record_cases(0x2E674, 0x2899E),
        **VALIDATOR_CASES_Z3801A,
    },
    "58503a-3704": {
        "hmon_check_pll_stage": OVEN_CHECK_CASES,
        "hmon_channel_step": [
            {PRESETS: {**case[PRESETS], "hmon_channel_index": ADC_OVEN_CURRENT, **oven}}
            for case in OVEN_CHECK_CASES
            for oven in ({}, WARM_OVEN)
        ],
        "gps_request_build": gps_request_build_cases(GPS_MESSAGES_58503A),
        **validator_cases(LOAD_DOUBLE_58503A, LOAD_LONG_58503A, LOAD_LONG_UNIT_STEP_58503A),
        "scpi_format_resolution_array_record": ARRAY_RECORD_CASES_58503A,
        "oncore_filter_channels": FILTER_CASES_58503A,
        "oncore_filter_traim": FILTER_CASES_58503A
        + [{**case, PRESETS: {**case[PRESETS], "gps.field_592": 0}} for case in FILTER_CASES_58503A],
        **{name: [{0: port} for port in SCPI_TASK_PORTS] for name in ("scpi_task_run", "installer_scpi_task_run")},
        "log_event": LOG_EVENT_CASES,
        **NOT_GIVEN,
    },
    # gps_request_build on each of its 57 messages (oncore_messages,
    # 0x575d8), and log_event on each of its codes, as the 58503A's;
    # console_port_read on the SCI, whose device read, sci_read, gives
    # no error (its D7, 0x2f278), so that READ_ERRORS have nowhere to
    # go: the line read, its next past its end, so that the port reads
    # the console; and its installer's copy none of the base's, as the
    # Z3815A's, whose installer it is byte for byte
    "z3817a-3752": {
        "console_port_read": [{PRESETS: {"scpi_input_next": 2, "scpi_input_end": 1}}],
        "installer_console_port_read": NOT_GIVEN["installer_console_port_read"],
        "gps_request_build": gps_request_build_cases(GPS_MESSAGES_Z3817A),
        "log_event": LOG_EVENT_CASES,
    },
}
# Another image's own requests for SET_UP_REQUESTS' functions, the
# 58503A 3704's (IMAGE_SET_UP_REQUESTS above).
def image_set_up_requests(numeric: bytes, action: bytes) -> dict[str, tuple[bytes, ...]]:
    """SET_UP_REQUESTS with an image's own numeric and action requests in
    place of the base's, and pll_scpi_command's, each of the two."""
    return {
        name: tuple(
            {NUMERIC_REQUEST: numeric, ACTION_REQUEST: action}[request]
            for request in (requests if isinstance(requests, tuple) else (requests,))
        )
        for name, requests in SET_UP_REQUESTS.items()
    } | {"pll_scpi_command": (numeric, action)}


IMAGE_SET_UP_REQUESTS = {
    "58503a-3704": image_set_up_requests(NUMERIC_REQUEST_58503A, ACTION_REQUEST_58503A),
    "z3815a-4010": image_set_up_requests(NUMERIC_REQUEST_Z3815A, ACTION_REQUEST_Z3815A),
    "z3801a-3543": image_set_up_requests(NUMERIC_REQUEST_Z3801A, ACTION_REQUEST_Z3801A),
}


# The registers a function leaves as it found them beside those its
# convention keeps, compared after it as those are: setjmp's, which
# changes only D0 (0x5112a to 0x51156: A0 put back from the buffer, A1
# and D1 untouched), so that the C's restores show.
SETJMP_KEPT = ("D1", "A0", "A1")
KEPT_BESIDE = {"setjmp": SETJMP_KEPT, "installer_setjmp": SETJMP_KEPT}
# A sidecar convention's returns for a function whose result is in the
# condition codes, which are compared after it as its result is; no
# other function's are (setjmp's flags, which trap #1 leaves, are not).
RETURNS_CONDITION_CODES = "ccr"


@dataclass
class Function:
    """A C function: its prototype's parameter and result types, each
    parameter's offset in the original's stack, its parameters' names,
    whether a variable list follows them (VARIADIC_CASES), and the
    registers its convention keeps for its caller
    (harness.preserved_registers), and whether it returns in the
    condition codes, which are then compared after it."""

    name: str
    result: str
    parameters: list[str]
    offsets: list[int]
    names: list[str]
    variadic: bool = False
    preserves: tuple[str, ...] = ()
    condition_codes: bool = False


def functions(c: str, types: Types, conventions: dict, image: str | None) -> tuple[list[Function], int]:
    """Every C function of an image, the primary's or an overlay's, whose
    prototype the harness can lay out, and how many it cannot."""
    found, unrunnable = [], 0
    for result, name, parameters in PROTOTYPE.findall(c):
        convention = conventions.get(name)
        if parameters == "" or convention is None or overlay(convention) != image:
            continue
        declared = [] if parameters == "void" else [p.strip() for p in parameters.split(",")]
        variadic = declared[-1:] == [VARIADIC]
        declared = declared[:-1] if variadic else declared
        kinds = [re.sub(r"\w+$", "", p).strip() for p in declared]
        result = result.strip()
        sizes = [types.size(kind) for kind in kinds]
        result_size = 0 if result == "void" else types.size(result)
        stack = convention.get("stack", [])
        if (
            "..." in kinds
            or None in sizes
            or result_size is None
            or max(sizes + [result_size]) > WIDEST
            or [item["size"] for item in stack] != sizes
            or variadic and not variadic_layable(stack)
        ):
            unrunnable += 1
            continue
        names = [re.search(r"\w*$", p).group() for p in declared]
        offsets = [item["offset"] for item in stack]
        preserves = preserved_registers(convention) + KEPT_BESIDE.get(name, ())
        condition_codes = convention.get("returns") == RETURNS_CONDITION_CODES
        found.append(Function(name, result, kinds, offsets, names, variadic, preserves, condition_codes))
    return found, unrunnable


def variadic_layable(stack: list[dict]) -> bool:
    """Whether a variadic function's declared arguments lie in 4-byte
    slots from the first, as gcc's do, so that its list starts at the
    same offset in both copies."""
    return [(item["offset"], item["size"]) for item in stack] == [
        (FIRST_ARGUMENT + GCC_SLOT * i, GCC_SLOT) for i in range(len(stack))
    ]


def task_context(c: str, conventions: dict, roots: set[str]) -> tuple[set[str], set[str]]:
    """The functions that run in a task in user mode, those a task entry,
    one of roots, an entry psos_start is given, one of
    FORTH_TASK_ENTRIES, or an SCPI handler reaches by direct calls, as
    two sets: those that only such a task reaches, and those that a
    function nobody calls (an interrupt handler, the reset, a console
    word, a routine reached only through a table), a task entry that
    enters supervisor mode for good (SUPERVISOR_SERVICES) or a function
    the spool task runs (SPOOLED) reaches too.  A function spoo_run is
    given is its caller's callee as well."""
    names = {convention["address"]: name for name, convention in conventions.items()}
    calls: dict[str, set[str]] = {name: set() for name in conventions}
    spooled: set[str] = set()
    blocks = EXPORT_BLOCK.split(c)
    for address, block in zip(blocks[1::2], blocks[2::2]):
        caller = names.get(address)
        if caller is None:
            continue
        text = COMMENT.sub("", block)
        callees = set(C_CALL.findall(text)) | set(ASSEMBLY_CALL.findall(text))
        callees |= {target for _, target in THUNK.findall(block)}
        spooled |= set(SPOOLED.findall(text)) & calls.keys()
        callees |= set(SPOOLED.findall(text))
        calls[caller] |= {name for name in callees if name in calls and name != caller}
    started = set(TASK_START.findall(c))
    for caller, convention in conventions.items():
        passed = convention.get("passed", [])
        started |= {function for callee, function in passed if re.fullmatch(TASK_STARTER, callee)}
        spools = {function for callee, function in passed if re.fullmatch(SPOOLER, callee)}
        spooled |= spools & calls.keys()
        callees = set(convention.get("calls", [])) | spools
        calls[caller] |= {name for name in callees if name in calls and name != caller}
    handlers = {name for _, name, parameters in PROTOTYPE.findall(c) if SCPI_HANDLER.match(parameters)}
    entries = (roots | started | FORTH_TASK_ENTRIES | handlers) & calls.keys()
    called = set().union(*calls.values())

    def reached(starts: set[str]) -> set[str]:
        found, pending = set(starts), list(starts)
        while pending:
            for callee in calls[pending.pop()] - found:
                found.add(callee)
                pending.append(callee)
        return found

    supervisor = {entry for entry in entries if calls[entry] & SUPERVISOR_SERVICES}
    user = reached(entries - supervisor)
    both = user & reached((calls.keys() - called - entries) | supervisor | spooled)
    return user - both, both


def root_function(m: Machine, located: dict[str, tuple[int, int]], conventions: dict) -> str | None:
    """The function kernel_init starts as ROOT on a machine, from its
    configuration in RAM; None if it is none of conventions'."""
    fresh_ram(m)
    entry = pointer_at(m, m.profile.psos_config + located[ROOT_ENTRY][0])
    address = f"{entry:08x}" if m.profile.overlay is None else f"{m.profile.overlay}{OVERLAY_SEPARATOR}{entry:08x}"
    return next((name for name, convention in conventions.items() if convention["address"] == address), None)


def original_layout(function: Function, values: list[bytes]) -> bytes:
    """Arguments where the original reads them, above the return address,
    UNDECLARED between and after them."""
    end = max((offset + len(value) for offset, value in zip(function.offsets, values)), default=FIRST_ARGUMENT)
    end += (end - FIRST_ARGUMENT) % POINTER_ALIGNMENT
    layout = bytearray((UNDECLARED * (end // len(UNDECLARED) + 1))[: end - FIRST_ARGUMENT])
    for offset, value in zip(function.offsets, values):
        layout[offset - FIRST_ARGUMENT : offset - FIRST_ARGUMENT + len(value)] = value
    return bytes(layout) + UNDECLARED


def result(function: Function, types: Types, run: Run) -> int | None:
    """The function's result as its type has it, from D0 or D0:D1; None
    if it has none or ended without returning."""
    if function.result == "void" or run.ended is not None:
        return None
    size = types.size(function.result)
    if size == WIDEST:
        return run.value("D0:D1")
    value = run.value("D0") & ((1 << 8 * size) - 1)
    if size < 4 and function.result not in UNSIGNED and value >> (8 * size - 1):
        value -= 1 << 8 * size
    return value


def same_text(m: Machine, function: Function, a: int, b: int) -> bool:
    """Whether two char pointer results point at the same text: gcc's copy
    of a function returns its own copy of a literal, where the image's
    returns the ROM's."""
    if function.result != "char *":
        return False
    try:
        return m.string(a) == m.string(b)
    except (UcError, ValueError):
        return False


def same_object(m: Machine, function: Function, types: Types, a: int, b: int) -> bool:
    """Whether two pointer results point at the same place in an image
    object and its twin among the C's (harness.Twins)."""
    return types.is_pointer(function.result) and a == m.twins.image_address(b)


def call_first(m: Machine, linked_symbols: dict[str, int], routine: str, stack_arguments: bytes) -> None:
    """Runs the image's own routine, a C function's copy or assembly, on
    its stack arguments, keeping what it leaves in RAM."""
    reset(m, snapshot(m))
    run = Run(m, linked_symbols.get(IMAGE_PREFIX + routine, linked_symbols.get(routine)), stack_arguments)
    if run.stopped is not None or run.ended is not None:
        raise RuntimeError(f"{routine}: {run.stopped or run.end()}")


# system_init's battery-backed region (preserved_region, 0x94e bytes in
# the Z3805A 3543B, its size the C's: a checksum word, the restore word,
# then the rest), which RAM's zeros fail the checksum of, so that only
# the cold start after a bad region would run: by case, a good checksum
# with restore 1, the warm start, or 0, the cold start that clears it,
# and a bad one (0x12c74 to 0x12d34).
def preserved_region(case: int, length: int) -> bytes:
    """preserved_region's bytes, of its length, for a case of system_init."""
    body = bytearray((i * 7 + case * 13 + 1) & 0xFF for i in range(length - 2))
    body[0:2] = struct.pack(">H", 0 if case % 4 == 1 else 1)
    total = sum(body) & 0xFF
    checksum = total << 8 | (~total & 0xFF)
    if case % 4 == 3:
        checksum ^= 1
    return struct.pack(">H", checksum) + bytes(body)


REGIONS = {"system_init": ("preserved_region", preserved_region)}


def establish(m: Machine, function: Function, context: "Context", case: int) -> None:
    """Sets the variables ESTABLISHED says a function's callers set, runs
    SET_UP on its request for each of SET_UP_REQUESTS, the case'th of
    those it has, the routine CALLED_FIRST names, and a region's bytes
    by REGIONS."""
    linked_symbols, located = context.linked_symbols, context.located
    for variable, value in ESTABLISHED.get(function.name, {}).items():
        # what an image's C does not name is left as RAM starts it
        if variable not in linked_symbols or isinstance(value, str) and IMAGE_PREFIX + value not in linked_symbols:
            continue
        if isinstance(value, str):
            value = linked_symbols[IMAGE_PREFIX + value]
        m.uc.mem_write(linked_symbols[variable], struct.pack(">I", value))
    if function.name in context.set_up_requests:
        requests = context.set_up_requests[function.name]
        if isinstance(requests, bytes):
            requests = (requests,)
        request = m.buffer(bytes(MESSAGE_OFFSET) + requests[case % len(requests)])
        call_first(m, linked_symbols, SET_UP, struct.pack(">I", request))
    if function.name in CALLED_FIRST:
        routine, longs = CALLED_FIRST[function.name]
        call_first(m, linked_symbols, routine, struct.pack(f">{len(longs)}I", *longs))
    if function.name in REGIONS and REGIONS[function.name][0] in located:
        variable, contents = REGIONS[function.name]
        m.uc.mem_write(linked_symbols[variable], contents(case, located[variable][1]))


def pointed(m: Machine, value: Pointed) -> int:
    """A Pointed's buffer, among the arguments' buffers, its own pointed
    buffers first."""
    laid = b"".join(part if isinstance(part, bytes) else struct.pack(">I", pointed(m, part)) for part in value.parts)
    return area(m, laid)


def given(m: Machine, types: Types, kind: str, value: int | float | bytes) -> bytes:
    """A value of GIVEN_CASES as a parameter of a type has it; one made
    for the machine, the value its maker gives."""
    if callable(value):
        value = value(m)
    if isinstance(value, bytes):
        return struct.pack(">I", m.buffer(value))
    if isinstance(value, Pointed):
        return struct.pack(">I", pointed(m, value))
    if kind in ("double", "float"):
        return struct.pack(">d" if kind == "double" else ">f", value)
    size = types.size(kind)
    return (value & ((1 << 8 * size) - 1)).to_bytes(size, "big")


def give_input_port(m: Machine, function: Function, values: list[bytes], port: int) -> None:
    """Gives an SCPI handler's call record, its argument named CALL,
    port as its input in place of the console port's object, and gcc's
    copy the same, not the object's twin."""
    at = struct.unpack(">I", values[function.names.index(CALL)])[0] + CALL_INPUT
    m.given.pop(at, None)
    give(m, at, port)


def cases(
    m: Machine, function: Function, linked_symbols: dict[str, int], given_cases: dict[str, list], states: States
) -> list[tuple[str, int, dict[int, int | float | bytes]]]:
    """Each case's description, the generated case it starts from and the
    arguments given_cases, GIVEN_CASES with the image's IMAGE_GIVEN_CASES,
    gives it."""
    own = given_cases.get(function.name, [])
    return (
        [(f"case {case}", case, {}) for case in range(CASES)]
        + [(f"given case {i}", i % CASES, values) for i, values in enumerate(own)]
        + variadic_cases(function)
        + session_cases(m, function, linked_symbols)
        + console_session_cases(function)
        + step_cases(m, function)
        + [(f"state case {i}", i % CASES, {}) for i in range(states.cases)]
    )


# The stack's headroom: how much deeper than the image's copy gcc's may
# write on a stack in a case, its own or the supervisor stack a task's
# exceptions take (harness.Run.deepest), before the case differs; and
# how many functions the report lists, the deepest beyond the image's,
# beside the cases with the least room above a floor (harness.Run.floor).
# The image's tasks were sized for its own frames, so a deeper copy may
# overrun a stack whose use the image leaves close to full, as the
# pForth console's printing did its data stack: 1 KB of return stack,
# of which the image's mem_rep leaves 58 bytes (docs/firmware/README.md,
# "Hand-written C").  gcc's frames and saves go deeper than the image's
# in most cases, at most the 58503A 3704's float_raise's 204 bytes, its
# soft-float frames, in the check's link, whose callees are not inlined
# (NOT_INLINED), 28 more than the 176 of the link the image is built
# from; the margin is the next 64 bytes up, so that a frame grown past
# what gcc's code takes now differs.  Above a floor the image's own room is
# what the stack was sized to, so gcc's may take only FLOOR_MARGIN more,
# a few saved registers, and never pass the floor where the image's
# does not: doprnt called, not in line, leaves 4 bytes of mem_rep's 58
# under gcc 15 and reached the data stack under gcc 13.
STACK_MARGIN = 256
FLOOR_MARGIN = 16
WORST_STACKS = 10


@dataclass(frozen=True)
class StackDepth:
    """A case's use of one stack, by the top of its memory: the bytes
    below where the call started it that gcc's copy and the image's
    wrote, 0 for a copy that never wrote there; gcc's lowest write; and
    the floor below the stack, if any, with the image's lowest write."""

    function: str
    case: str
    top: int
    gcc: int
    image: int
    lowest: int
    floor: int | None
    image_lowest: int

    def deeper(self) -> int:
        """How many bytes deeper gcc's copy wrote than the image's."""
        return self.gcc - self.image


def stack_depths(name: str, description: str, original: Run, gcc: Run) -> list[StackDepth]:
    """A case's StackDepth on each stack gcc's copy wrote, its own stack's
    floor the run's.  Each copy's depth is from where its own call
    started the stack, so that gcc's arguments, each in a 4-byte slot, do
    not count as its use."""
    depths = []
    for top, lowest in sorted(gcc.deepest.items()):
        image_lowest = original.deepest.get(top, original.stack_starts[top])
        depths.append(
            StackDepth(
                name,
                description,
                top,
                gcc.stack_starts[top] - lowest,
                original.stack_starts[top] - image_lowest,
                lowest,
                gcc.floor if top == STACK_END else None,
                image_lowest,
            )
        )
    return depths


def stack_difference(depths: list[StackDepth]) -> str | None:
    """None if gcc's copy kept within STACK_MARGIN of the image's on each
    stack, FLOOR_MARGIN on one with a floor, and above each floor the
    image's kept above; else how not."""
    for depth in depths:
        if depth.deeper() > (STACK_MARGIN if depth.floor is None else FLOOR_MARGIN):
            return f"stack {depth.gcc} bytes deep, {depth.deeper()} deeper than the image's, to {depth.lowest:x}"
        if depth.floor is not None and depth.lowest < depth.floor <= depth.image_lowest:
            return f"stack to {depth.lowest:x}, below {depth.floor:x}, image's to {depth.image_lowest:x}"
    return None


def print_stacks(label: str, depths: list[StackDepth]) -> None:
    """The stack's figures: the cases whose gcc copy went deeper than the
    image's, the deepest case of each of the WORST_STACKS functions
    deepest beyond the image's, and the least room left above a floor."""
    deeper = sorted((d for d in depths if d.deeper() > 0), key=lambda d: -d.deeper())
    print(
        f"{label}stack deeper than the image's: {len(deeper)} of {len(depths)} cases, "
        f"margin {STACK_MARGIN} bytes, {FLOOR_MARGIN} above a floor"
    )
    worst = {}
    for depth in deeper:
        worst.setdefault(depth.function, depth)
    for depth in list(worst.values())[:WORST_STACKS]:
        print(
            f"  {depth.function} {depth.case}: {depth.gcc} bytes, {depth.deeper()} deeper than the image's, "
            f"of the stack below {depth.top:x}"
        )
    floored = sorted((d for d in depths if d.floor is not None), key=lambda d: d.lowest - d.floor)
    for depth in floored[:WORST_STACKS]:
        print(
            f"  {depth.function} {depth.case}: {depth.lowest - depth.floor} bytes above {depth.floor:x}, "
            f"image's {depth.image_lowest - depth.floor}"
        )


@dataclass
class Context:
    """What a check finds beyond the function: the link's symbols, each
    Preset target's address and size, the runnable functions, by name,
    for a Hook's prototype, the functions that run in a task in user
    mode, only so and in both modes (task_context), each case's
    StackDepth so far, the image's SET_UP_REQUESTS (BASE_IMAGE), its
    given cases (cases) and its States, STATES with its IMAGE_STATES,
    its counted loops, the cases ident fails in, by function
    (creators_ident_fails), each function's calls, by its sidecar, and
    where the functions' image copies and gcc's start, each sorted,
    where a copy's code ends, gcc's functions, the sidecar's and the
    C's own helpers, whose calls are their callers' (record_calls)."""

    linked_symbols: dict[str, int]
    located: dict[str, tuple[int, int]]
    by_name: dict[str, Function]
    in_task: set[str]
    both_modes: set[str]
    depths: list[StackDepth]
    set_up_requests: dict[str, bytes | tuple[bytes, ...]]
    given_cases: dict[str, list]
    states: dict[str, States]
    counted_loops: dict[str, tuple[str, int]]
    ident_fails: dict[str, range]
    calls: dict[str, list[str]]
    starts: tuple[list[int], list[int]]
    rebuilt_code: "RebuiltCode"
    in_range: dict[str, dict[int, int]] = field(default_factory=lambda: IN_RANGE)


def install_hooks(
    m: Machine, types: Types, name: str, states: States, context: Context, drawn: list[Drawn]
) -> list[int]:
    """Hooks each of the States' routines, the image's copy and gcc's, to
    answer as the current case drew (drawn's one entry), and each other
    C function the image's copy of a function calls (Context.calls), to
    record its call's arguments and run on (record_calls); the hooks.  A
    routine the image's C does not have under its name runs unhooked."""
    handles = record_calls(m, types, name, states, context)
    for hook in states.hooks:
        if hook.routine not in context.by_name:
            continue
        routine = context.by_name[hook.routine]
        image = context.linked_symbols[IMAGE_PREFIX + hook.routine]

        def answer(uc, address, size, hook_data):
            hook, routine, gcc = hook_data
            case = drawn[0]
            if hook.recorded:
                m.effects.append(HookedCall(hook.routine, hook_arguments(m, types, routine, gcc)))
            for at, contents in case.leaves[hook.routine].items():
                uc.mem_write(at, contents)
            return_from_hook(m, case.results[hook.routine])

        for address, gcc in ((image, False), (context.linked_symbols[hook.routine], True)):
            handles.append(m.uc.hook_add(UC_HOOK_CODE, answer, (hook, routine, gcc), begin=address, end=address))
    # a block translated before its hook was added runs without it
    m.uc.ctl_flush_tb()
    return handles


# What a function's States and given cases name that the image's C
# lacks, and so would be left out unseen: a preset's or a hook's
# leaves' target the C has no lvalue for (probeable), a routine hooked
# or given results that the image's C has no function of or the States
# do not hook, an interrupt's watch.  Each fails the function, so that a
# name the C changed does not leave its cases at zeroed RAM's value, as
# standby_from_unlock_ready's did in the Z3815A 4010's efc_history_update
# and kenneth_predicted_get under the export's name, DAT_001021b3, and a
# hook of a static helper, status_ready_condition, did in its alarms'
# updates.  But for those meant for another image's C, in States shared
# by the images (STATES, GIVEN_CASES): by image, each (function, name).
MEANT_ELSEWHERE: dict[str, frozenset[tuple[str, str]]] = {
    # the Z3805A's fine's readings and GPS status, which the 58503A's C
    # names otherwise
    "58503a-3704": frozenset(
        {
            ("enter_holdover", "gps.status_reported"),
            *(("fine", f"fine_readings[{index}]") for index in range(3)),
        }
    ),
}


def unresolved(name: str, states: States, given: list, context: Context) -> list[tuple[str, str]]:
    """What a function's States and given cases name that the image's C
    lacks (MEANT_ELSEWHERE), as (what it is, its name)."""
    missing = []
    hooked = {hook.routine for hook in states.hooks}

    def target(kind: str, lvalue: str) -> None:
        if lvalue not in context.located:
            missing.append((kind, lvalue))

    for preset in states.presets:
        target("preset", preset.target)
    for hook in states.hooks:
        if hook.routine not in context.by_name or IMAGE_PREFIX + hook.routine not in context.linked_symbols:
            missing.append(("hook", hook.routine))
        for preset in hook.leaves:
            target("hook's leaves", preset.target)
    for values in given:
        for lvalue, value in values.get(PRESETS, {}).items():
            target("preset", lvalue)
            if isinstance(value, str) and IMAGE_PREFIX + value not in context.linked_symbols:
                missing.append(("preset's function", value))
        for routine, leaves in values.get(HOOK_LEAVES, {}).items():
            if routine not in hooked:
                missing.append(("unhooked routine's leaves", routine))
            for lvalue in leaves:
                target("hook's leaves", lvalue)
        for routine in values.get(HOOK_RESULTS, {}):
            if routine not in hooked:
                missing.append(("unhooked routine's results", routine))
        for interrupt in values.get(INTERRUPTS, ()):
            if isinstance(interrupt.watch, str):
                target("interrupt's watch", interrupt.watch)
        for lvalue in values.get(RECORDS, {}):
            target("EEPROM record", lvalue)
    return sorted(set(missing))


# Past the end of any copy's code: the last function's copy's end.
REBUILT_END = 0x1_0000_0000
# How a frame gcc's code links, -fno-omit-frame-pointer's: the caller's
# frame pointer at A6, its return address above it.
SAVED_FRAME = 0
SAVED_RETURN = 4
# The most frames of the C's own helpers a call is traced back through.
HELPER_DEPTH = 16


@dataclass(frozen=True)
class RebuiltCode:
    """gcc's functions in the link, from REBUILT: each one's start,
    sorted, and its name; and the sidecar's functions, whose own code
    their calls are, where any other, a helper the C defines static,
    makes its calls for whatever sidecar function called it (owner)."""

    starts: list[int]
    names: list[str]
    sidecar: frozenset[str]

    def function_at(self, at: int) -> str | None:
        """The name of gcc's function whose code holds an address."""
        index = bisect.bisect_right(self.starts, at) - 1
        return self.names[index] if index >= 0 and at >= REBUILT else None

    def owner(self, m: Machine, returning: int) -> str | None:
        """The sidecar function whose gcc copy makes a call that returns
        to an address: the function holding it or, from a helper of the
        C's own, the first sidecar function back through the helpers'
        frames, each linked, -fno-omit-frame-pointer, at A6."""
        frame = m.uc.reg_read(REGISTERS["A6"])
        for _ in range(HELPER_DEPTH):
            name = self.function_at(returning)
            if name is None or name in self.sidecar:
                return name
            try:
                returning = pointer_at(m, frame + SAVED_RETURN)
                frame = pointer_at(m, frame + SAVED_FRAME)
            except UcError:
                return None
        return None


def rebuilt_code(code: dict[str, int], conventions: dict) -> RebuiltCode:
    """gcc's functions in the link, of its code symbols."""
    ordered = sorted((address, name) for name, address in code.items() if address >= REBUILT)
    return RebuiltCode([a for a, _ in ordered], [n for _, n in ordered], frozenset(conventions))


def function_starts(linked_symbols: dict[str, int], conventions: dict) -> tuple[list[int], list[int]]:
    """Where the sidecar's functions' image copies start in the link,
    sorted, and their gcc copies."""
    return tuple(
        sorted({linked_symbols[prefix + name] for name in conventions if prefix + name in linked_symbols})
        for prefix in (IMAGE_PREFIX, "")
    )


# A parameter's kind that is a pointer, and what a recorded call keeps
# for an argument of another kind, which lies where no literal does: an
# integer, a mask or a length, may be a ROM address's value, and two
# copies may pass it with bits the routine does not read.
POINTER_KIND = "*"
NOT_POINTER = -1


def record_calls(m: Machine, types: Types, name: str, states: States, context: Context) -> list[int]:
    """Hooks each C function the image's copy of a function calls, by
    its sidecar, that the States do not answer, the image's copy and
    gcc's, to record the arguments of each call the function's own code
    makes (Run.calls), its return address within the image's copy, or
    gcc's copy or a helper of the C's own it calls (RebuiltCode.owner),
    or, for a jump to it in the function's place, the call's own, whose pointer
    arguments to literals are compared (harness.calls_differences): an
    argument that reaches nothing the call leaves behind, a text a log
    entry is not written with, is compared so.  A callee's own calls
    are not recorded: a callee's two copies may reach a routine by other
    paths, the image's through a binding gcc's C calls through another."""
    answered = {hook.routine for hook in states.hooks}
    handles = []
    copies = [
        (start, starts[bisect.bisect_right(starts, start)] if start < starts[-1] else REBUILT_END)
        for start, starts in zip(
            (context.linked_symbols[IMAGE_PREFIX + name], context.linked_symbols[name]), context.starts
        )
    ]

    def record(uc, address, size, hook_data):
        routine, gcc = hook_data
        start, end = copies[gcc]
        sp = uc.reg_read(UC_M68K_REG_A7)
        returning = int.from_bytes(uc.mem_read(sp, POINTER_SIZE), "big")
        own = context.rebuilt_code.owner(m, returning) == name if gcc else start <= returning < end
        if not own and returning != RETURN:
            return
        arguments = hook_arguments(m, types, routine, gcc)
        m.calls.append(
            HookedCall(
                routine.name,
                tuple(a if POINTER_KIND in kind else NOT_POINTER for a, kind in zip(arguments, routine.parameters)),
            )
        )

    for callee in dict.fromkeys(context.calls.get(name, ())):
        image = context.linked_symbols.get(IMAGE_PREFIX + callee)
        rebuilt = context.linked_symbols.get(callee)
        if callee in answered or callee not in context.by_name or image is None or rebuilt is None:
            continue
        for address, gcc in ((image, False), (rebuilt, True)):
            handles.append(m.uc.hook_add(UC_HOOK_CODE, record, (context.by_name[callee], gcc), begin=address, end=address))
    return handles


def check(m: Machine, types: Types, function: Function, image: int, rebuilt: int, context: Context):
    """Yields (description, outcome) for each case: outcome empty if gcc's
    copy agrees with the image's, else each thing that differs."""
    linked_symbols = context.linked_symbols
    states = context.states.get(function.name, States())
    drawn: list[Drawn] = []
    handles = install_hooks(m, types, function.name, states, context, drawn)
    counter = context.counted_loops.get(function.name)
    m.counter = (*context.located[counter[0]], counter[1]) if counter and counter[0] in context.located else None
    try:
        yield from check_cases(m, types, function, image, rebuilt, context, states, drawn)
    finally:
        for handle in handles:
            m.uc.hook_del(handle)
        m.uc.ctl_flush_tb()
        m.counter = None
        m.ident_fails = False
        m.starting_status = SUPERVISOR
        m.frame_fill = None
        m.interrupts = []
        m.stack_edge = False
        m.starting_registers = {}
        m.eeprom = ERASED
        if m.flash is not None:
            m.flash.start = m.flash.image


def both_runs(
    m: Machine,
    types: Types,
    function: Function,
    image: int,
    rebuilt: int,
    values: list[bytes],
    memory,
    listed: bytes = b"",
    session: tuple[tuple[bytes, ...], int, int] | None = None,
    accesses: "Accesses | None" = None,
) -> tuple[Run, Run]:
    """The image's copy and gcc's, each run from memory on a case's
    arguments, a variable list's bytes after the declared ones, gcc's
    given the C's twins of the image's objects; UNDECLARED after them
    but at the stack's edge.  In a console session, given as its lines
    and its task's two copies, the image's and gcc's, each then runs its
    own copy of the task (console_task_run).  Given accesses, each run's
    RAM accesses are its copy's (record_accesses)."""
    after = b"" if m.stack_edge else UNDECLARED
    if accesses is not None:
        accesses.copy = 0
    reset(m, memory)
    m.console_input = None if session is None else list(session[0])
    original = Run(m, image, original_layout(function, values).removesuffix(UNDECLARED) + listed + after)
    if session is not None:
        original = console_task_run(m, original, session[1])
    reset(m, memory)
    give_twins(m)
    if accesses is not None:
        accesses.copy = 1
    m.console_input = None if session is None else list(session[0])
    gcc_arguments = gcc_slots(twin_arguments(m, types, function.parameters, values))
    gcc = Run(m, rebuilt, gcc_arguments + listed + after)
    if session is not None:
        gcc = console_task_run(m, gcc, session[2])
    m.console_input = None
    return original, gcc


def check_cases(
    m: Machine,
    types: Types,
    function: Function,
    image: int,
    rebuilt: int,
    context: Context,
    states: States,
    drawn: list[Drawn],
):
    """check's cases, with its hooks in place, each drawing its States
    into drawn."""
    linked_symbols = context.linked_symbols
    planned = cases(m, function, linked_symbols, context.given_cases, states)
    for ordinal, (description, case, values_given), poisoned in (
        (ordinal, planned_case, poisoned)
        for ordinal, planned_case in enumerate(planned)
        for poisoned in (False, True)
    ):
        in_task = function.name in context.in_task or function.name in context.both_modes and ordinal % 2 == 0
        m.starting_status = TASK_STATUS if in_task else SUPERVISOR
        values_given = dict(values_given)
        listed = values_given.pop(VARIADIC, b"")
        line = values_given.pop(SCPI_LINE, None)
        console = values_given.pop(CONSOLE_SESSION, None)
        session = None
        if console is not None:
            task = CONSOLE_SESSIONS[function.name]
            session = (CONSOLE_LINES[console], linked_symbols[IMAGE_PREFIX + task], linked_symbols[task])
        step = values_given.pop(TREE_STEP, None)
        status = values_given.pop(STARTING_STATUS, None)
        if status is not None:
            m.starting_status = status
        presets = values_given.pop(PRESETS, {})
        flash = values_given.pop(FLASH, None)
        if m.flash is not None:
            m.flash.start = m.flash.image if flash is None else flash(m.flash.image)
        m.stack_edge = values_given.pop(STACK_EDGE, False)
        registers = values_given.pop(CALLERS_REGISTERS, {})
        input_port = values_given.pop(INPUT_PORT, None)
        # an interrupt, a preset or a region whose target the image's C
        # does not have (probeable) is left out
        m.interrupts = [
            (interrupt, context.located[interrupt.watch][0] if isinstance(interrupt.watch, str) else interrupt.watch)
            for interrupt in values_given.pop(INTERRUPTS, ())
            if not isinstance(interrupt.watch, str) or interrupt.watch in context.located
        ]
        drawn[:] = [draw(states, context.located, function.name, description)]
        drawn[0].results.update(values_given.pop(HOOK_RESULTS, {}))
        for routine, leaves in values_given.pop(HOOK_LEAVES, {}).items():
            drawn[0].leaves[routine] = {
                context.located[target][0]: preset_bytes(value, context.located[target][1])
                for target, value in leaves.items()
                if target in context.located
            }
        m.ident_fails = case in context.ident_fails.get(function.name, ())
        m.frame_fill = None
        devices = values_given.pop(DEVICES, None)
        records = values_given.pop(RECORDS, {})
        fresh_ram(m, case)
        if devices is not None:
            m.device_case = devices
        m.eeprom = eeprom_records(context.located, records)
        establish(m, function, context, case)
        if line is not None:
            values_given = {**scpi_session(m, function, linked_symbols, line), **values_given}
        if function.name in TASK_STACKS:
            task_stacks(m, context.located, case)
        for address, contents in drawn[0].presets.items():
            m.uc.mem_write(address, contents)
        for target, value in presets.items():
            if target not in context.located:
                continue
            address, size = context.located[target]
            if callable(value):
                value = value(m, types)
            elif isinstance(value, str):
                value = context.linked_symbols[IMAGE_PREFIX + value]
            m.uc.mem_write(address, preset_bytes(value, size))
        target = None if step is None else tree_steps(m)[step]
        values = arguments(m, types, function.parameters, case, function.names, function.name, target)
        for position, count in context.in_range.get(function.name, {}).items():
            values[position] = given(m, types, function.parameters[position], case % count)
        for position, value in values_given.items():
            values[position] = given(m, types, function.parameters[position], value)
        m.starting_registers = {name: value(m) if callable(value) else value for name, value in registers.items()}
        if input_port is not None:
            give_input_port(m, function, values, input_port(m))
        if function.name in JUMP_FUNCTIONS:
            untraced_jump(m, struct.unpack(">I", values[0])[0])
        if not poisoned:
            # the RAM the case's runs read and wrote, each copy's
            accesses = Accesses()
        elif not poison(m, accesses):
            continue
        memory = snapshot(m)

        recording = not poisoned
        handles = record_accesses(m, accesses) if recording else []
        original, gcc = both_runs(
            m, types, function, image, rebuilt, values, memory, listed, session, accesses if recording else None
        )
        privileged = f"trap {PRIVILEGE_VIOLATION}"
        if original.stopped == gcc.stopped == privileged and m.starting_status != SUPERVISOR:
            starting_status, m.starting_status = m.starting_status, SUPERVISOR
            original, gcc = both_runs(
                m, types, function, image, rebuilt, values, memory, listed, session, accesses if recording else None
            )
            m.starting_status = starting_status
        for handle in handles:
            m.uc.hook_del(handle)
        if handles:
            m.uc.ctl_flush_tb()
        description += POISONED if poisoned else ""

        stopped = stopped_outcome(original, gcc, ("image", "gcc"))
        if stopped:
            yield description, (stopped,)
            continue
        differences = []
        wanted, got = result(function, types, original), result(function, types, gcc)
        if (
            wanted != got
            and not same_text(m, function, wanted, got)
            and not same_object(m, function, types, wanted, got)
        ):
            differences.append(f"result {got:x}, image's {wanted:x}")
        if original.status & SYSTEM_BYTE != gcc.status & SYSTEM_BYTE:
            differences.append(f"status register {gcc.status:04x}, image's {original.status:04x}")
        if (
            function.condition_codes
            and original.ended is None
            and gcc.ended is None
            and original.status & CONDITION_CODES != gcc.status & CONDITION_CODES
        ):
            differences.append(
                f"condition codes {gcc.status & CONDITION_CODES:02x}, "
                f"image's {original.status & CONDITION_CODES:02x}"
            )
        registers_differ = registers_difference(m, original, gcc, function.preserves)
        if registers_differ:
            differences.append(registers_differ)
        differences += ram_differences(m, original, gcc)
        differences += effects_differences(original, gcc, ("image", "gcc"))
        differences += calls_differences(m, original, gcc)
        depths = stack_depths(function.name, description, original, gcc)
        context.depths += depths
        stack_differs = stack_difference(depths)
        if stack_differs:
            differences.append(stack_differs)
        yield description, tuple(differences)


# The first case run again with bytes of RAM poisoned, set to
# poison_byte's value where they are zero as the case starts: those
# either copy wrote in its first run and neither read, and those one
# copy read and the other did not.  A copy writing or reading another
# cell than the other's, which zeroed RAM gives the same 0, then
# differs: a flag cleared at another image's address leaves the right
# cell's poison in place.  No byte a copy read in both is poisoned, so
# that two copies alike take the same path as in the first case: a
# poisoned value both read would lead them where the harness's
# addresses, a frame's or a code address in a message, differ.  So a
# wrong cell read goes unseen only where the other copy reads it too.
# The case's description, with this.
POISONED = ", poisoned"
POISON_STEP = 0x9D


@dataclass
class Accesses:
    """The bytes of RAM each copy's run of a case read and wrote, the
    image's first, and which copy is running (both_runs)."""

    reads: tuple[set[int], set[int]] = field(default_factory=lambda: (set(), set()))
    writes: tuple[set[int], set[int]] = field(default_factory=lambda: (set(), set()))
    copy: int = 0

    def poisoned(self) -> set[int]:
        """The bytes a poisoned case poisons, if zero (POISONED)."""
        read = self.reads[0] | self.reads[1]
        return (self.writes[0] | self.writes[1]) - read | self.reads[0] ^ self.reads[1]


def poison_byte(at: int) -> int:
    """The nonzero byte a poisoned case starts with at an address,
    neighbors differing, so that a long read across two cells, or a cell
    read for another near it, differs."""
    return at * POISON_STEP & BYTE_MASK or POISON_STEP


def record_accesses(m: Machine, accesses: Accesses) -> list[int]:
    """Hooks RAM's reads and writes to add each byte they touch to the
    running copy's accesses; the hooks."""

    def touched(uc, access, address, size, value, kind):
        (accesses.reads if kind == UC_HOOK_MEM_READ else accesses.writes)[accesses.copy].update(
            range(address, address + size)
        )

    handles = [
        m.uc.hook_add(kind, touched, kind, begin=RAM, end=RAM + RAM_SIZE - 1)
        for kind in (UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE)
    ]
    m.uc.ctl_flush_tb()
    return handles


def poison(m: Machine, accesses: Accesses) -> bool:
    """Sets each byte of RAM accesses poisons, in a long that is zero,
    and not where literals lie nor among the kernel's segments
    (ALLOCATIONS), which the harness lays out, to its poison_byte
    (POISONED): a zero byte of a long that is not, a pointer's or a
    count's high byte, is part of a value set up, which poisoned would
    no longer be.  Whether any byte was."""
    start = m.read(RAM, RAM_SIZE)
    ram = bytearray(start)
    for at in accesses.poisoned():
        offset = at - RAM
        cell = offset - offset % POINTER_SIZE
        if not any(start[cell : cell + POINTER_SIZE]) and literal_region(m, at) is None and at < ALLOCATIONS:
            ram[offset] = poison_byte(at)
    m.uc.mem_write(RAM, bytes(ram))
    return ram != start


# A record's checksum, its first word, as records_checksum makes it:
# the sum of the bytes after it, then its complement.
RECORD_CHECKSUM = 2


def eeprom_records(located: dict[str, tuple[int, int]], records: dict[str, bool]) -> bytes:
    """The EEPROM a case starts with: erased, but each of its records,
    by the lvalue the C names it by, holding bytes that differ by
    address (poison_byte), so that a field read for another differs, and
    a checksum that holds or, for a record given False, its complement
    byte flipped, does not."""
    eeprom = bytearray(ERASED)
    for target, good in records.items():
        if target not in located:
            continue
        address, size = located[target]
        offset = address - EEPROM
        body = bytes(poison_byte(at) for at in range(address + RECORD_CHECKSUM, address + size))
        total = sum(body) & BYTE_MASK
        checksum = total << 8 | ~total & BYTE_MASK
        eeprom[offset : offset + size] = struct.pack(">H", checksum if good else checksum ^ BYTE_MASK) + body
    return bytes(eeprom)


def untraced_jump(m: Machine, buffer: int) -> None:
    """Clears the trace bits of the status register a jump buffer holds
    (JUMP_FUNCTIONS)."""
    at = buffer + JUMP_STATUS
    status = int.from_bytes(m.uc.mem_read(at, 2), "big")
    m.uc.mem_write(at, (status & ~TRACE_BITS).to_bytes(2, "big"))


# The names files, which label the image's objects, in the image's
# analysis/ folder, named as the C file's decompiled/ folder is: the
# names file and the generated SCPI trees' rows.
NAMES_FILES = ("names.tsv", "scpi-tree.tsv", "installer-scpi-tree.tsv")


def names_files(c_path: str) -> list[str]:
    """The names files of the image a C file is of."""
    decompiled = os.path.dirname(os.path.abspath(c_path))
    folder = os.path.join(os.path.dirname(os.path.dirname(decompiled)), "analysis", os.path.basename(decompiled))
    paths = [os.path.join(folder, name) for name in NAMES_FILES]
    return [path for path in paths if os.path.exists(path)]


def main() -> int:
    if len(sys.argv) < 3 or "DECOMPILED_CFLAGS" not in os.environ:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    c_path, image_path, names = sys.argv[1], sys.argv[2], set(sys.argv[3:])
    c = open(c_path).read()
    with tempfile.TemporaryDirectory() as work:
        elf = work + "/rebuilt.elf"
        linked = link(c_path, shlex.split(os.environ["DECOMPILED_CFLAGS"]) + list(NOT_INLINED), work, elf, probes(c))
        linked_symbols = symbols(elf)
        linked_objects = objects(elf)
        linked_code = code_symbols(elf)
    located = probed(c, linked, linked_symbols)
    image_bytes = open(image_path, "rb").read()
    types = Types(c)
    conventions = {}
    for line in open(c_path.removesuffix(".c") + ".conventions.jsonl"):
        function = json.loads(line)
        conventions[function["name"]] = function
    labels = image_labels(names_files(c_path))
    failed = 0
    base = image_name(image_path) == BASE_IMAGE
    known = IMAGE_KNOWN.get(image_name(image_path), {})
    elsewhere = MEANT_ELSEWHERE.get(image_name(image_path), frozenset())
    # an image's own States, given cases and known differences, each
    # for a function of its sidecar
    for table in (IMAGE_STATES, IMAGE_GIVEN_CASES, IMAGE_KNOWN):
        for name in table.get(image_name(image_path), {}):
            if name not in conventions and (not names or name in names):
                print(f"FAIL {name}: the image's own cases name no function of its sidecar")
                failed += 1
    given_cases = {**GIVEN_CASES, **IMAGE_GIVEN_CASES.get(image_name(image_path), {})}
    states = {**STATES, **IMAGE_STATES.get(image_name(image_path), {})}
    counted_loops = {**COUNTED_LOOPS, **IMAGE_COUNTED_LOOPS.get(image_name(image_path), {})}
    set_up_requests = SET_UP_REQUESTS if base else IMAGE_SET_UP_REQUESTS.get(image_name(image_path), {})
    for profile in image_profiles(image_path, image_bytes):
        machine = rebuilt_machine(
            image_bytes, linked, linked_symbols, profile, twins(linked_objects, labels, profile)
        )
        protect_code(machine)
        runnable, unrunnable = functions(c, types, conventions, profile.overlay)
        in_task, both_modes = task_context(c, conventions, {root_function(machine, located, conventions)})
        by_name = {function.name: function for function in runnable}
        context = Context(
            linked_symbols, located, by_name, in_task, both_modes, [], set_up_requests, given_cases, states,
            counted_loops, creators_ident_fails(conventions),
            {name: convention.get("calls", []) for name, convention in conventions.items()},
            function_starts(linked_symbols, conventions),
            rebuilt_code(linked_code, conventions),
            {**IN_RANGE, **IMAGE_IN_RANGE.get(image_name(image_path), {})},
        )
        counts = {"agreeing": 0, "skipped": 0, "known": 0, "failed": 0}
        modes = {"user mode alone": 0, "both modes": 0}
        condition_codes_compared = 0
        sessions = {"agreeing": 0, "run": 0}
        for function in runnable:
            image = linked_symbols.get(IMAGE_PREFIX + function.name)
            rebuilt = linked_symbols.get(function.name)
            if names and function.name not in names or image is None or rebuilt is None or rebuilt < REBUILT:
                continue
            outcomes = list(check(machine, types, function, image, rebuilt, context))
            counted = report(function.name, outcomes, known.get(function.name))
            missing = [
                (kind, what)
                for kind, what in unresolved(
                    function.name, context.states.get(function.name, States()),
                    context.given_cases.get(function.name, []), context,
                )
                if (function.name, what) not in elsewhere
            ]
            for kind, what in missing:
                print(f"FAIL {function.name}: its {kind} {what} names nothing in the image's C")
            counts["failed" if missing else counted] += 1
            modes["user mode alone"] += function.name in in_task
            modes["both modes"] += function.name in both_modes
            condition_codes_compared += function.condition_codes
            for description, outcome in outcomes:
                if description.startswith(CONSOLE_SESSION) and not description.endswith(POISONED):
                    sessions["run"] += 1
                    sessions["agreeing"] += not outcome
        label = f"{profile.overlay} " if profile.overlay else ""
        print(
            f"{label}functions agreeing: {counts['agreeing']}, skipped: {counts['skipped']}, "
            f"known: {counts['known']}, failed: {counts['failed']}; prototypes not laid out: {unrunnable}"
        )
        # the figures docs/firmware/README.md gives (report_figures.py)
        print(
            f"{label}functions run in "
            + ", ".join(f"{what}: {count}" for what, count in modes.items())
            + f"; condition codes compared (returns=ccr): {condition_codes_compared}"
        )
        # the pForth console's sessions, console_init's cases and the
        # poisoned runs apart
        if sessions["run"]:
            print(f"{label}console sessions agreeing: {sessions['agreeing']} of {sessions['run']}")
        print_stacks(label, context.depths)
        failed += counts["failed"]
    # a strict run's cases are not the documented ones
    # (report_figures.py)
    if ON_MC683XX and not PARITY:
        print(STRICT)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
