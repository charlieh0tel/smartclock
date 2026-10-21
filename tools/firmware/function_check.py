# /// script
# requires-python = ">=3.11"
# dependencies = ["unicorn>=2.1"]
# ///
"""Runs each decompiled C function, as gcc compiled it, against the
image's own: in the rebuilt image (rebuild_link.py), the image's copy is
called with its arguments where the .conventions.jsonl sidecar says the
original reads them, as Ghidra's storage has them, and gcc's copy as
gcc's callers lay them, in 4-byte slots.  From the same RAM, the two must give
the same result and leave RAM the same.

    DECOMPILED_CFLAGS='...' uv run tools/firmware/function_check.py \\
        third_party/firmware/decompiled/z3805a-3543b.c \\
        third_party/firmware/z3805a-3543b.bin [NAME...]

With NAMEs, only those.  gcc's copy calls gcc's C and, through their
names and the bridges, the assembly; so a difference may lie in a
callee.  A function that reaches beyond what the harness maps, writes
ROM, or traps the same way both times is skipped: a peripheral or the
kernel would decide its result.  A function whose prototype has an
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
    REBUILT_SIZE,
    UNSIGNED,
    Machine,
    Run,
    Types,
    arguments,
    fresh_ram,
    gcc_slots,
    ram_difference,
    rebuilt_machine,
    report,
    reset,
    stopped_outcome,
)
from libc_check import RAM, RAM_SIZE, ROM, ROM_SIZE
from rebuild_link import IMAGE_PREFIX, REBUILT, link, symbols

# A stack argument's offset above the stack pointer at entry, past the
# return address.
FIRST_ARGUMENT = 4
# The widest argument or result the harness lays out.
WIDEST = 8


@dataclass
class Function:
    """A C function: its prototype's parameter and result types, and
    each parameter's offset in the original's stack."""

    name: str
    result: str
    parameters: list[str]
    offsets: list[int]


def functions(c: str, types: Types, conventions: dict) -> tuple[list[Function], int]:
    """Every C function in ROM whose prototype the harness can lay out,
    and how many it cannot."""
    found, unrunnable = [], 0
    for result, name, parameters in re.findall(r"^([A-Za-z_][\w \*]*?)\b(\w+)\(([^()]*)\);$", c, re.M):
        convention = conventions.get(name)
        if parameters == "" or convention is None or convention["address"].startswith("installer"):
            continue
        kinds = [] if parameters == "void" else [re.sub(r"\w+$", "", p.strip()).strip() for p in parameters.split(",")]
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
        found.append(Function(name, result, kinds, [item["offset"] for item in stack]))
    return found, unrunnable


def original_layout(function: Function, values: list[bytes]) -> bytes:
    """Arguments where the original reads them, above the return address."""
    end = max((offset + len(value) for offset, value in zip(function.offsets, values)), default=FIRST_ARGUMENT)
    layout = bytearray(end - FIRST_ARGUMENT)
    for offset, value in zip(function.offsets, values):
        layout[offset - FIRST_ARGUMENT : offset - FIRST_ARGUMENT + len(value)] = value
    return bytes(layout)


def result(function: Function, types: Types, run: Run) -> int | None:
    """The function's result as its type has it, from D0 or D0:D1."""
    if function.result == "void":
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


def check(m: Machine, types: Types, function: Function, image: int, rebuilt: int):
    """Yields (description, outcome) for each case: outcome None if gcc's
    copy agrees with the image's, else what differs."""
    for case in range(CASES):
        fresh_ram(m)
        values = arguments(m, types, function.parameters, case)
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
        ram_differs = ram_difference(original, gcc)
        if ram_differs:
            differences.append(ram_differs)
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
    machine = rebuilt_machine(open(image_path, "rb").read(), linked)
    # a stray write to code faults, rather than changing later cases
    machine.uc.mem_protect(ROM, ROM_SIZE, UC_PROT_READ | UC_PROT_EXEC)
    machine.uc.mem_protect(REBUILT, REBUILT_SIZE, UC_PROT_READ | UC_PROT_EXEC)
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
        counts[report(function.name, list(check(machine, types, function, image, rebuilt)))] += 1
    print(
        f"functions agreeing: {counts['agreeing']}, skipped: {counts['skipped']}, failed: {counts['failed']}; "
        f"prototypes not laid out: {unrunnable}"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
