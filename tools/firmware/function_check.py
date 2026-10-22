# /// script
# requires-python = ">=3.11"
# dependencies = ["unicorn>=2.1"]
# ///
"""Runs each decompiled C function, as gcc compiled it, against the
image's own: in the rebuilt image (rebuild_link.py), the image's copy is
called with its arguments where the .conventions.jsonl sidecar says the
original reads them, as Ghidra's storage has them, and gcc's copy as
gcc's callers lay them, in 4-byte slots.  From the same RAM, the two must give
the same result, leave RAM the same, and make the same kernel calls and
device writes in the same order (harness.py), each starting with what
its callers set where it reads that (ESTABLISHED, SET_UP_REQUESTS,
CALLED_FIRST).  After the generated cases come a function's own, the
arguments its callers pass where the generated ones never reach them
(GIVEN_CASES); and where no declared argument lies, both copies find
the same pointers (UNDECLARED), so that a body reading an argument its
prototype leaves out differs.  A function may start from states its
zeroed RAM never reaches, drawn for each case, with routines it calls
answered in their place and their calls compared (STATES); find no
task by name (IDENT_FAILS); start in user mode, as a task
(STARTING_STATUS); end its endless loop at a count (COUNTED_LOOPS);
and, for the stack report, find the running task's stacks
(TASK_STACKS).  Both copies must leave the status register's system
byte the same.

    DECOMPILED_CFLAGS='...' uv run tools/firmware/function_check.py \\
        third_party/firmware/decompiled/z3805a-3543b.c \\
        third_party/firmware/z3805a-3543b.bin [NAME...]

With NAMEs, only those.  gcc's copy calls gcc's C and, through their
names and the bridges, the assembly; so a difference may lie in a
callee.  A call that ends at a trap that never returns, a zero divide,
a write to code, a branch to itself, its KERNEL_CALL_LIMIT'th kernel
call or its counter's count is compared up to there, but for its
result.  A function that
reaches beyond what the harness maps, calls a kernel service the
harness does not model, or traps the same way both times is skipped.  A function whose prototype has an
argument of no known size, a structure passed whole, a variable
argument list, or an argument not on the stack is not run.  The
installer's functions run on a machine of its own, whose RAM is the
installer as the boot code unpacks it (harness.installer_profile), and
are counted apart.  This is the rebuild's measure, not a gate: it exits
zero whatever it finds.
"""

import json
import os
import random
import re
import shlex
import struct
import sys
import tempfile
from dataclasses import dataclass

from unicorn import UC_HOOK_CODE, UcError
from unicorn.m68k_const import UC_M68K_REG_A7, UC_M68K_REG_D0, UC_M68K_REG_PC

from harness import (
    CASES,
    FIRST_ARGUMENT,
    MESSAGE_OFFSET,
    NULL_LONG,
    PLLC_EXCHANGE,
    POINTER_ALIGNMENT,
    POINTER_FILLS,
    POINTER_SIZE,
    PRIMARY,
    RECEIVED,
    SUPERVISOR,
    SYSTEM_BYTE,
    UNSIGNED,
    Known,
    Machine,
    Run,
    Types,
    arguments,
    effects_difference,
    fresh_ram,
    gcc_slots,
    installer_profile,
    overlay,
    pointer_at,
    protect_code,
    ram_difference,
    rebuilt_machine,
    report,
    reset,
    snapshot,
    stopped_outcome,
)
from libc_check import BUFFER_SPACING
from rebuild_link import IMAGE_PREFIX, REBUILT, Section, link, symbols

# The widest argument or result the harness lays out.
WIDEST = 8
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
# status_register_setup's arguments as status_init passes them
# (0x2f4b8 to 0x2f50a): a status model, its first argument, whose
# seven register pointers point into the pointee memory, and the
# console port's object (0x29e18) for the Status Byte, register 0,
# which has no parent; the others report into register 0 or 2, at a
# bit, with no port.
SETUP_REGISTERS = struct.pack(">7I", *(0x31300000 + 0x10 * index for index in range(7)))
SETUP_MODEL = struct.pack(">hIIII", 7, 0, 0, 0, 0) + SETUP_REGISTERS
CONSOLE_PORT = 0x29E18
SETUP_CASES = [
    (0, 0, 0, 0, 0, CONSOLE_PORT),
    (1, 0x0010FFFE, 0x0010FFFE, 0, 0x20, 0),
    (3, 0xFFFF0002, 0x12345678, 2, 8, 0),
    (6, 0x13FF, 0, 2, 1, 0),
]
# scpi_format_resolution_array_record's records, by kind (0x26822): a
# block, a resolution array and a kind of neither, each at eight zero
# bytes in ROM (0x2f76e, an empty block and an empty array), with
# return_zero (0x29ada) as the release routine at +6, which it calls
# whatever the kind.
EMPTY_IN_ROM = 0x2F76E
RETURN_ZERO = 0x29ADA
ARRAY_RECORD_KINDS = [0, 1, 2, -1]
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
GIVEN_CASES = {
    "vsprintf": [{1: text, 2: arguments} for text, arguments in FORMATS],
    "installer_vsprintf": [{1: text, 2: arguments} for text, arguments in FORMATS],
    "clamp_double_to_step_and_limits": [
        {4: struct.pack(">dI", step, LOAD_DOUBLE)} for step in DOUBLE_STEPS
    ],
    "clamp_to_step_and_limits": [{4: struct.pack(">II", step, LOAD_LONG)} for step in LONG_STEPS],
    **{
        name: [{4: struct.pack(">fI", step, LOAD_LONG)} for step in FLOAT_STEPS]
        for name in ("clamp_float_to_step_and_limits", "clamp_float_to_step_double")
    },
    "pforth_define_word": [{2: arguments, 3: results} for arguments, results in WORD_COUNTS],
    "bytes_equal_n": [dict(enumerate(arguments)) for arguments in SAME_LENGTH],
    "double_to_digits": [{1: count, 5: mode} for count, mode in DIGITS],
    **{name: [{0: value} for value in SPECIAL_DOUBLES] for name in ("frexp", "modf", "sqrt", "log", "fabs")},
    "pow": [{0: base, 1: exponent} for base, exponent in POWERS],
    **{
        name: [{0: value} for value in EXPONENT_DOUBLES]
        for name in ("double_decimal_exponent", "double_decimal_exponent_rounded")
    },
    "status_register_setup": [
        {0: SETUP_MODEL, **{position + 1: value for position, value in enumerate(values)}} for values in SETUP_CASES
    ],
    "scpi_format_resolution_array_record": [
        {1: struct.pack(">bBII", kind, 0, EMPTY_IN_ROM, RETURN_ZERO)} for kind in ARRAY_RECORD_KINDS
    ],
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

# Arguments a routine indexes a table by unchecked, which every caller
# passes in range, by position: how many values each takes, the case
# picking one.  fpga_set_bit's bit and fpga_set_field's field, an
# fpga_bit_index, are one of the six entries of fpga_bit_table
# (0x41310), which the hand-written fpga.c keeps as fpga_bits; what
# follows the image's table is not the C's.  scpi_char_upper's
# character is a port's value, 0 to 0x105, which indexes the lexer's
# classes (0x277ec), as scpi_engine_class keeps them.
IN_RANGE = {
    **{name: {0: 6} for name in ("fpga_set_bit", "fpga_set_field")},
    "scpi_char_upper": {0: 0x106},
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


@dataclass(frozen=True)
class HookedCall:
    """A call of a Hook's routine: its name and its arguments' values."""

    routine: str
    arguments: tuple[int, ...]

    def __str__(self) -> str:
        return " ".join([f"call of {self.routine}"] + [f"{a:x}" for a in self.arguments])


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
STATES = {
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
}
# The functions whose cases find no task by name (harness.IDENT_NOT_FOUND),
# by the generated cases that do so, the others finding one: those that
# ask for their task before they create it, which, finding it, end in
# fatal_error; and root_task, in every case, which then goes on to
# create and start every task.
CREATORS_IDENT_FAILS = range(1, CASES, 2)
IDENT_FAILS = {
    "root_task": range(CASES),
    **{
        name: CREATORS_IDENT_FAILS
        for name in (
            "spoo_task_create",
            "klok_task_create",
            "scpi_task_create",
            "installer_spoo_task_create",
            "installer_scpi_task_create",
        )
    },
}
# The functions whose loop never ends and makes no kernel call, by the
# variable it counts in and the count a call ends at (harness.COUNTED):
# root_task's idle loop (0x131dc), counting in idle_loops.  The count is
# odd, so that a loop counting by more than one passes it and runs to
# STEP_LIMIT, which the image's does not.
IDLE_LOOPS = 101
COUNTED_LOOPS = {"root_task": ("idle_loops", IDLE_LOOPS)}
# The functions that start as a task does, in user mode, its interrupts
# unmasked, by that status register, rather than harness.SUPERVISOR:
# root_task, which kernel_init creates in mode 0 (0x15e70), enters
# supervisor mode with trap #7 to enable the periodic interrupt and
# moves its status register back (0x13082 to 0x13098).
TASK_STATUS = 0
STARTING_STATUS = {"root_task": TASK_STATUS}


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
SYSTEM_STACK_SIZE = "psos_config.supervisor_stack_size"
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
    size_at, size = located[SYSTEM_STACK_SIZE]
    system = pointer_at(m, m.profile.psos_config) + SYSTEM_STACK
    length = int.from_bytes(m.read(size_at, size), "big")
    unused = length - stacks.system_used
    m.uc.mem_write(system, bytes(unused) + USED_STACK * (stacks.system_used // len(USED_STACK)))


def targets() -> list[str]:
    """Every Preset's target and the task stacks' fields, each once, in
    a fixed order."""
    found = [*TASK_FIELDS.values(), SEGMENT_SIZE, SYSTEM_STACK_SIZE]
    for states in STATES.values():
        for preset in states.presets + tuple(p for hook in states.hooks for p in hook.leaves):
            if preset.target not in found:
                found.append(preset.target)
    return found


def probes() -> str:
    """The C that has gcc find each target's address and size."""
    return (
        "".join(
            f"\nvoid *const {PROBE_ADDRESS.format(i)} = (void *)&({target});"
            f"\nconst uint32_t {PROBE_SIZE.format(i)} = sizeof({target});"
            for i, target in enumerate(targets())
        )
        + "\n"
    )


def probed(linked: list[Section], linked_symbols: dict[str, int]) -> dict[str, tuple[int, int]]:
    """Each target's address and size, from the probes in the link."""

    def long_at(name: str) -> int:
        address = linked_symbols[name]
        section = next(s for s in linked if s.address <= address < s.address + len(s.data))
        return struct.unpack_from(">I", section.data, address - section.address)[0]

    return {
        target: (long_at(PROBE_ADDRESS.format(i)), long_at(PROBE_SIZE.format(i))) for i, target in enumerate(targets())
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
    narrower value in a slot's last bytes (gcc_slots)."""
    sp = m.uc.reg_read(UC_M68K_REG_A7)
    found, slot = [], FIRST_ARGUMENT
    for offset, kind in zip(routine.offsets, routine.parameters):
        size = types.size(kind)
        at = sp + (slot + max(size, GCC_SLOT) - size if gcc else offset)
        found.append(int.from_bytes(m.uc.mem_read(at, size), "big"))
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
# "A rebuild that runs"), each counted as known while every failing case
# is that difference, so that another in the same routine still fails.
# The firmware's own: os_report_processes holds
# the address of "%s" in D5 across its calls (0x16768), which
# os_report_process prints as a task's stack share.  And what C cannot
# say: setjmp reads its caller's registers; log_erase checksums a
# pointer's bytes.
D5_SHARE = Known(
    "os_report_process prints D5, which os_report_processes leaves holding the address of \"%s\" (0x16768)",
    r"^effect \d+ trap #4 service 4 ",
)
KNOWN = {
    **{name: D5_SHARE for name in ("os_process_report", "ps_word", "scpi_query_DIAGnostic_OS_PROCess")},
    "setjmp": Known(
        "setjmp reads its caller's registers, which C has no name for",
        r"^one stopped: image None, gcc Invalid memory read \(UC_ERR_READ_UNMAPPED\) of fffff000 ",
    ),
    "log_erase": Known("log_erase checksums a pointer's bytes", r"^RAM differs from 101d50$"),
    "installer_set_interrupt_mask": Known(
        "the status register it returns holds its caller's condition codes, "
        "which gcc's copy of its narrow argument sets first",
        r"^result ([0-9a-f]*?)[0-9a-f]{2}, image's \1[0-9a-f]{2}$",
    ),
}


@dataclass
class Function:
    """A C function: its prototype's parameter and result types, each
    parameter's offset in the original's stack, and its parameters'
    names."""

    name: str
    result: str
    parameters: list[str]
    offsets: list[int]
    names: list[str]


def functions(c: str, types: Types, conventions: dict, image: str | None) -> tuple[list[Function], int]:
    """Every C function of an image, the primary's or an overlay's, whose
    prototype the harness can lay out, and how many it cannot."""
    found, unrunnable = [], 0
    for result, name, parameters in re.findall(r"^([A-Za-z_][\w \*]*?)\n?\b(\w+)\n?\s*\(([^()]*)\);$", c, re.M):
        convention = conventions.get(name)
        if parameters == "" or convention is None or overlay(convention) != image:
            continue
        declared = [] if parameters == "void" else [p.strip() for p in parameters.split(",")]
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
        ):
            unrunnable += 1
            continue
        names = [re.search(r"\w*$", p).group() for p in declared]
        found.append(Function(name, result, kinds, [item["offset"] for item in stack], names))
    return found, unrunnable


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


def call_first(m: Machine, linked_symbols: dict[str, int], routine: str, stack_arguments: bytes) -> None:
    """Runs the image's own routine, a C function's copy or assembly, on
    its stack arguments, keeping what it leaves in RAM."""
    reset(m, snapshot(m))
    run = Run(m, linked_symbols.get(IMAGE_PREFIX + routine, linked_symbols.get(routine)), stack_arguments)
    if run.stopped is not None or run.ended is not None:
        raise RuntimeError(f"{routine}: {run.stopped or run.end()}")


# system_init's battery-backed region (preserved_region, 0x94e bytes: a
# checksum word, the restore word, then the rest), which RAM's zeros fail
# the checksum of, so that only the cold start after a bad region would
# run: by case, a good checksum with restore 1, the warm start, or 0, the
# cold start that clears it, and a bad one (0x12c74 to 0x12d34).
PRESERVED_LENGTH = 0x94E


def preserved_region(case: int) -> bytes:
    """preserved_region's bytes for a case of system_init."""
    body = bytearray((i * 7 + case * 13 + 1) & 0xFF for i in range(PRESERVED_LENGTH - 2))
    body[0:2] = struct.pack(">H", 0 if case % 4 == 1 else 1)
    total = sum(body) & 0xFF
    checksum = total << 8 | (~total & 0xFF)
    if case % 4 == 3:
        checksum ^= 1
    return struct.pack(">H", checksum) + bytes(body)


REGIONS = {"system_init": ("preserved_region", preserved_region)}


def establish(m: Machine, function: Function, linked_symbols: dict[str, int], case: int) -> None:
    """Sets the variables ESTABLISHED says a function's callers set, runs
    SET_UP on its request for each of SET_UP_REQUESTS, the case'th of
    those it has, the routine CALLED_FIRST names, and a region's bytes
    by REGIONS."""
    for variable, value in ESTABLISHED.get(function.name, {}).items():
        if isinstance(value, str):
            value = linked_symbols[IMAGE_PREFIX + value]
        m.uc.mem_write(linked_symbols[variable], struct.pack(">I", value))
    if function.name in SET_UP_REQUESTS:
        requests = SET_UP_REQUESTS[function.name]
        if isinstance(requests, bytes):
            requests = (requests,)
        request = m.buffer(bytes(MESSAGE_OFFSET) + requests[case % len(requests)])
        call_first(m, linked_symbols, SET_UP, struct.pack(">I", request))
    if function.name in CALLED_FIRST:
        routine, longs = CALLED_FIRST[function.name]
        call_first(m, linked_symbols, routine, struct.pack(f">{len(longs)}I", *longs))
    if function.name in REGIONS:
        variable, contents = REGIONS[function.name]
        m.uc.mem_write(linked_symbols[variable], contents(case))


def given(m: Machine, types: Types, kind: str, value: int | float | bytes) -> bytes:
    """A value of GIVEN_CASES as a parameter of a type has it."""
    if isinstance(value, bytes):
        return struct.pack(">I", m.buffer(value))
    if kind in ("double", "float"):
        return struct.pack(">d" if kind == "double" else ">f", value)
    size = types.size(kind)
    return (value & ((1 << 8 * size) - 1)).to_bytes(size, "big")


def cases(function: Function) -> list[tuple[str, int, dict[int, int | float | bytes]]]:
    """Each case's description, the generated case it starts from and the
    arguments GIVEN_CASES gives it."""
    own = GIVEN_CASES.get(function.name, [])
    states = STATES.get(function.name, States())
    return (
        [(f"case {case}", case, {}) for case in range(CASES)]
        + [(f"given case {i}", i % CASES, values) for i, values in enumerate(own)]
        + [(f"state case {i}", i % CASES, {}) for i in range(states.cases)]
    )


@dataclass
class Context:
    """What a check finds beyond the function: the link's symbols, each
    Preset target's address and size, and the runnable functions, by
    name, for a Hook's prototype."""

    linked_symbols: dict[str, int]
    located: dict[str, tuple[int, int]]
    by_name: dict[str, Function]


def install_hooks(m: Machine, types: Types, states: States, context: Context, drawn: list[Drawn]) -> list[int]:
    """Hooks each of the States' routines, the image's copy and gcc's, to
    answer as the current case drew (drawn's one entry); the hooks."""
    handles = []
    for hook in states.hooks:
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


def check(m: Machine, types: Types, function: Function, image: int, rebuilt: int, context: Context):
    """Yields (description, outcome) for each case: outcome None if gcc's
    copy agrees with the image's, else what differs."""
    linked_symbols = context.linked_symbols
    states = STATES.get(function.name, States())
    drawn: list[Drawn] = []
    handles = install_hooks(m, types, states, context, drawn)
    counter = COUNTED_LOOPS.get(function.name)
    m.counter = (linked_symbols[counter[0]], counter[1]) if counter else None
    m.starting_status = STARTING_STATUS.get(function.name, SUPERVISOR)
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
    for description, case, values_given in cases(function):
        drawn[:] = [draw(states, context.located, function.name, description)]
        m.ident_fails = case in IDENT_FAILS.get(function.name, ())
        m.frame_fill = None
        fresh_ram(m, case)
        establish(m, function, linked_symbols, case)
        if function.name in TASK_STACKS:
            task_stacks(m, context.located, case)
        for address, contents in drawn[0].presets.items():
            m.uc.mem_write(address, contents)
        values = arguments(m, types, function.parameters, case, function.names, function.name)
        for position, count in IN_RANGE.get(function.name, {}).items():
            values[position] = given(m, types, function.parameters[position], case % count)
        for position, value in values_given.items():
            values[position] = given(m, types, function.parameters[position], value)
        memory = snapshot(m)

        reset(m, memory)
        original = Run(m, image, original_layout(function, values))
        reset(m, memory)
        gcc = Run(m, rebuilt, gcc_slots(values) + UNDECLARED)

        stopped = stopped_outcome(original, gcc, ("image", "gcc"))
        if stopped:
            yield description, stopped
            continue
        differences = []
        wanted, got = result(function, types, original), result(function, types, gcc)
        if wanted != got and not same_text(m, function, wanted, got):
            differences.append(f"result {got:x}, image's {wanted:x}")
        if original.status & SYSTEM_BYTE != gcc.status & SYSTEM_BYTE:
            differences.append(f"status register {gcc.status:04x}, image's {original.status:04x}")
        ram_differs = ram_difference(m, original, gcc)
        if ram_differs:
            differences.append(ram_differs)
        effects_differ = effects_difference(original, gcc, ("image", "gcc"))
        if effects_differ:
            differences.append(effects_differ)
        yield description, "; ".join(differences) or None


def main() -> int:
    if len(sys.argv) < 3 or "DECOMPILED_CFLAGS" not in os.environ:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    c_path, image_path, names = sys.argv[1], sys.argv[2], set(sys.argv[3:])
    c = open(c_path).read()
    with tempfile.TemporaryDirectory() as work:
        elf = work + "/rebuilt.elf"
        linked = link(c_path, shlex.split(os.environ["DECOMPILED_CFLAGS"]), work, elf, probes())
        linked_symbols = symbols(elf)
    located = probed(linked, linked_symbols)
    image_bytes = open(image_path, "rb").read()
    types = Types(c)
    conventions = {}
    for line in open(c_path.removesuffix(".c") + ".conventions.jsonl"):
        function = json.loads(line)
        conventions[function["name"]] = function
    for profile in (PRIMARY, installer_profile(image_bytes)):
        machine = rebuilt_machine(image_bytes, linked, linked_symbols, profile)
        protect_code(machine)
        runnable, unrunnable = functions(c, types, conventions, profile.overlay)
        context = Context(linked_symbols, located, {function.name: function for function in runnable})
        counts = {"agreeing": 0, "skipped": 0, "known": 0, "failed": 0}
        for function in runnable:
            image = linked_symbols.get(IMAGE_PREFIX + function.name)
            rebuilt = linked_symbols.get(function.name)
            if names and function.name not in names or image is None or rebuilt is None or rebuilt < REBUILT:
                continue
            outcomes = list(check(machine, types, function, image, rebuilt, context))
            counts[report(function.name, outcomes, KNOWN.get(function.name))] += 1
        label = f"{profile.overlay} " if profile.overlay else ""
        print(
            f"{label}functions agreeing: {counts['agreeing']}, skipped: {counts['skipped']}, "
            f"known: {counts['known']}, failed: {counts['failed']}; prototypes not laid out: {unrunnable}"
        )
    return 0


if __name__ == "__main__":
    sys.exit(main())
