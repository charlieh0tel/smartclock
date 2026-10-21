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
prototype leaves out differs.

    DECOMPILED_CFLAGS='...' uv run tools/firmware/function_check.py \\
        third_party/firmware/decompiled/z3805a-3543b.c \\
        third_party/firmware/z3805a-3543b.bin [NAME...]

With NAMEs, only those.  gcc's copy calls gcc's C and, through their
names and the bridges, the assembly; so a difference may lie in a
callee.  A call that ends at a trap that never returns, a zero divide,
a write to code, a branch to itself or its KERNEL_CALL_LIMIT'th kernel
call is compared up to there, but for its result.  A function that
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
import re
import shlex
import struct
import sys
import tempfile
from dataclasses import dataclass

from unicorn import UcError

from harness import (
    CASES,
    FIRST_ARGUMENT,
    MESSAGE_OFFSET,
    NULL_LONG,
    PLLC_EXCHANGE,
    POINTER_ALIGNMENT,
    POINTER_FILLS,
    PRIMARY,
    RECEIVED,
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
    protect_code,
    ram_difference,
    rebuilt_machine,
    report,
    reset,
    snapshot,
    stopped_outcome,
)
from rebuild_link import IMAGE_PREFIX, REBUILT, link, symbols

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
# pow's operands the generated reals never are: a negative base to an
# integer power, odd and even, which pow takes the sign of apart
# (double_is_integer, 0x60e68), and a fraction.
POWERS = [(-2.0, 3.0), (-2.0, 2.0), (-0.5, 3.0), (2.0, 0.5), (0.0, 2.0)]
# Double step records as clamp_double_to_step_and_limits reads them
# (scpi_double_step): a step, then load_double (0x322aa) as the routine,
# which returns the record's own step; one so fine the steps pass 1e9,
# which coarsens it (0x32612).
LOAD_DOUBLE = 0x322AA
DOUBLE_STEPS = [0.5, 0.001, 1e-12, 100.0]
GIVEN_CASES = {
    "clamp_double_to_step_and_limits": [
        {4: struct.pack(">dI", step, LOAD_DOUBLE)} for step in DOUBLE_STEPS
    ],
    "pforth_define_word": [{2: arguments, 3: results} for arguments, results in WORD_COUNTS],
    "bytes_equal_n": [dict(enumerate(arguments)) for arguments in SAME_LENGTH],
    "double_to_digits": [{1: count, 5: mode} for count, mode in DIGITS],
    **{name: [{0: value} for value in SPECIAL_DOUBLES] for name in ("frexp", "modf", "sqrt", "log", "fabs")},
    "pow": [{0: base, 1: exponent} for base, exponent in POWERS],
}


# The differences whose cause is found and is not the C's (PLAN.md,
# "A rebuild that runs"), each counted as known while every failing case
# is that difference, so that another in the same routine still fails.
# The firmware's own: task_stack_info reads two locals it never sets
# ("no system stack found", 0x16c28 to 0x16c32), which gcc takes as 0
# where the image faults on the filled frame; os_report_processes holds
# the address of "%s" in D5 across its calls (0x16768), which
# os_report_process prints as a task's stack share.  And what C cannot
# say: setjmp reads its caller's registers; int64_divide and
# uint64_divide, on a quotient too wide for 32 bits, return the
# dividend's low long, as divs.l leaves it; log_erase checksums a
# pointer's bytes.
UNSET_LOCALS = Known(
    "task_stack_info reads two locals it never sets (0x16c28 to 0x16c32), the image faulting on the filled frame",
    r"^one stopped: image Invalid memory read .* at 16c32, gcc None$",
)
D5_SHARE = Known(
    "os_report_process prints D5, which os_report_processes leaves holding the address of \"%s\" (0x16768)",
    r"^effect \d+ trap #4 service 4 ",
)
WIDE_QUOTIENT = Known(
    "a quotient too wide for 32 bits: the image returns the dividend's low long, as divs.l leaves it",
    r"^result [0-9a-f]+, image's [0-9a-f]+$",
)
KNOWN = {
    **{name: UNSET_LOCALS for name in ("task_stack_report", "s_rep_word", "scpi_query_DIAGnostic_OS_STACk")},
    **{name: D5_SHARE for name in ("os_process_report", "ps_word", "scpi_query_DIAGnostic_OS_PROCess")},
    "int64_divide": WIDE_QUOTIENT,
    "uint64_divide": WIDE_QUOTIENT,
    "setjmp": Known(
        "setjmp reads its caller's registers, which C has no name for",
        r"^one stopped: image None, gcc Invalid memory read \(UC_ERR_READ_UNMAPPED\) of fffff000 ",
    ),
    "log_erase": Known("log_erase checksums a pointer's bytes", r"^RAM differs from 101d50$"),
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


def establish(m: Machine, function: Function, linked_symbols: dict[str, int], case: int) -> None:
    """Sets the variables ESTABLISHED says a function's callers set, runs
    SET_UP on its request for each of SET_UP_REQUESTS, the case'th of
    those it has, and the routine CALLED_FIRST names."""
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
    return [(f"case {case}", case, {}) for case in range(CASES)] + [
        (f"given case {i}", i % CASES, values) for i, values in enumerate(own)
    ]


def check(m: Machine, types: Types, function: Function, image: int, rebuilt: int, linked_symbols: dict[str, int]):
    """Yields (description, outcome) for each case: outcome None if gcc's
    copy agrees with the image's, else what differs."""
    for description, case, values_given in cases(function):
        fresh_ram(m, case)
        establish(m, function, linked_symbols, case)
        values = arguments(m, types, function.parameters, case, function.names, function.name)
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
        linked = link(c_path, shlex.split(os.environ["DECOMPILED_CFLAGS"]), work, elf)
        linked_symbols = symbols(elf)
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
        counts = {"agreeing": 0, "skipped": 0, "known": 0, "failed": 0}
        for function in runnable:
            image = linked_symbols.get(IMAGE_PREFIX + function.name)
            rebuilt = linked_symbols.get(function.name)
            if names and function.name not in names or image is None or rebuilt is None or rebuilt < REBUILT:
                continue
            outcomes = list(check(machine, types, function, image, rebuilt, linked_symbols))
            counts[report(function.name, outcomes, KNOWN.get(function.name))] += 1
        label = f"{profile.overlay} " if profile.overlay else ""
        print(
            f"{label}functions agreeing: {counts['agreeing']}, skipped: {counts['skipped']}, "
            f"known: {counts['known']}, failed: {counts['failed']}; prototypes not laid out: {unrunnable}"
        )
    return 0


if __name__ == "__main__":
    sys.exit(main())
