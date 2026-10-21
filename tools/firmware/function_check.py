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
its callers set where it reads that (ESTABLISHED, SET_UP_REQUESTS).

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
argument list, or an argument not on the stack is not run, nor is the
installer's, whose image copy the boot code unpacks into RAM.  This is the rebuild's measure, not a gate:
it exits zero whatever it finds.
"""

import json
import os
import re
import shlex
import struct
import sys
import tempfile
from dataclasses import dataclass

from unicorn import UC_PROT_EXEC, UC_PROT_READ, UcError

from harness import (
    CASES,
    FIRST_ARGUMENT,
    MESSAGE_OFFSET,
    PLLC_EXCHANGE,
    RECEIVED,
    UNSIGNED,
    Machine,
    Run,
    Types,
    arguments,
    effects_difference,
    fresh_ram,
    gcc_slots,
    ram_difference,
    rebuilt_machine,
    report,
    reset,
    stopped_outcome,
)
from libc_check import RAM, RAM_SIZE, ROM, ROM_SIZE
from rebuild_link import IMAGE_PREFIX, REBUILT, REBUILT_DATA, link, symbols

# The widest argument or result the harness lays out.
WIDEST = 8
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
# SCPI action descriptors").
SET_UP = "get_setup_vars"
NUMERIC_REQUEST = RECEIVED[PLLC_EXCHANGE]
ACTION_REQUEST = struct.pack(">IIBBBBII", 0, 0x41348, 0, 0, 0, 0, 0, 0)
SET_UP_REQUESTS = {
    "validate_or_doaction": NUMERIC_REQUEST,
    "validate_num": NUMERIC_REQUEST,
    "pll_scpi_needs_loop": NUMERIC_REQUEST,
    "pll_scpi_send_to_loop": NUMERIC_REQUEST,
    "scpi_param_saved_in_nv": NUMERIC_REQUEST,
    "scpi_param_saved_in_record": NUMERIC_REQUEST,
    "finish_scpi_cmd": NUMERIC_REQUEST,
    "pll_scpi_complete": NUMERIC_REQUEST,
    "pll_scpi_do_action": ACTION_REQUEST,
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


def functions(c: str, types: Types, conventions: dict) -> tuple[list[Function], int]:
    """Every C function in ROM whose prototype the harness can lay out,
    and how many it cannot."""
    found, unrunnable = [], 0
    for result, name, parameters in re.findall(r"^([A-Za-z_][\w \*]*?)\b(\w+)\(([^()]*)\);$", c, re.M):
        convention = conventions.get(name)
        if parameters == "" or convention is None or convention["address"].startswith("installer"):
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
    """Arguments where the original reads them, above the return address."""
    end = max((offset + len(value) for offset, value in zip(function.offsets, values)), default=FIRST_ARGUMENT)
    layout = bytearray(end - FIRST_ARGUMENT)
    for offset, value in zip(function.offsets, values):
        layout[offset - FIRST_ARGUMENT : offset - FIRST_ARGUMENT + len(value)] = value
    return bytes(layout)


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


def establish(m: Machine, function: Function, linked_symbols: dict[str, int]) -> None:
    """Sets the variables ESTABLISHED says a function's callers set, and
    runs SET_UP on its request for each of SET_UP_REQUESTS."""
    for variable, value in ESTABLISHED.get(function.name, {}).items():
        if isinstance(value, str):
            value = linked_symbols[IMAGE_PREFIX + value]
        m.uc.mem_write(linked_symbols[variable], struct.pack(">I", value))
    if function.name in SET_UP_REQUESTS:
        request = m.buffer(bytes(MESSAGE_OFFSET) + SET_UP_REQUESTS[function.name])
        reset(m, m.read(RAM, RAM_SIZE))
        run = Run(m, linked_symbols[IMAGE_PREFIX + SET_UP], struct.pack(">I", request))
        if run.stopped is not None or run.ended is not None:
            raise RuntimeError(f"{SET_UP}: {run.stopped or run.end()}")


def check(m: Machine, types: Types, function: Function, image: int, rebuilt: int, linked_symbols: dict[str, int]):
    """Yields (description, outcome) for each case: outcome None if gcc's
    copy agrees with the image's, else what differs."""
    for case in range(CASES):
        fresh_ram(m, case)
        establish(m, function, linked_symbols)
        values = arguments(m, types, function.parameters, case, function.names)
        ram = m.read(RAM, RAM_SIZE)

        reset(m, ram)
        original = Run(m, image, original_layout(function, values))
        reset(m, ram)
        gcc = Run(m, rebuilt, gcc_slots(values))

        stopped = stopped_outcome(original, gcc, ("image", "gcc"))
        if stopped:
            yield f"case {case}", stopped
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
        yield f"case {case}", "; ".join(differences) or None


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
    machine = rebuilt_machine(open(image_path, "rb").read(), linked, linked_symbols)
    # a stray write to code faults, rather than changing later cases
    machine.uc.mem_protect(ROM, ROM_SIZE, UC_PROT_READ | UC_PROT_EXEC)
    machine.uc.mem_protect(REBUILT, REBUILT_DATA - REBUILT, UC_PROT_READ | UC_PROT_EXEC)
    types = Types(c)
    conventions = {}
    for line in open(c_path.removesuffix(".c") + ".conventions.jsonl"):
        function = json.loads(line)
        conventions[function["name"]] = function
    runnable, unrunnable = functions(c, types, conventions)
    counts = {"agreeing": 0, "skipped": 0, "failed": 0}
    for function in runnable:
        image = linked_symbols.get(IMAGE_PREFIX + function.name)
        rebuilt = linked_symbols.get(function.name)
        if names and function.name not in names or image is None or rebuilt is None or rebuilt < REBUILT:
            continue
        counts[report(function.name, list(check(machine, types, function, image, rebuilt, linked_symbols)))] += 1
    print(
        f"functions agreeing: {counts['agreeing']}, skipped: {counts['skipped']}, failed: {counts['failed']}; "
        f"prototypes not laid out: {unrunnable}"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
