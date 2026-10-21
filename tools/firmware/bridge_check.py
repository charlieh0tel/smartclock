# /// script
# requires-python = ">=3.11"
# dependencies = ["unicorn>=2.1"]
# ///
"""Confirms the decompiled C's bridges by running them: each wrapper c_NAME,
which gcc's C calls in place of a routine that takes its arguments in
registers (docs/firmware/README.md), is called in the rebuilt image as
gcc calls it, its arguments in 4-byte stack slots, and the routine
itself is called with the same arguments in the registers the
.conventions.jsonl sidecar names.  The two must give the same result,
leave RAM the same and make the same kernel calls and device writes
(harness.py), and the wrapper must keep the registers gcc's callee
saves.

    DECOMPILED_CFLAGS='...' uv run tools/firmware/bridge_check.py \\
        third_party/firmware/decompiled/z3805a-3543b.c \\
        third_party/firmware/z3805a-3543b.bin

A routine that reaches beyond what the harness maps, calls a kernel
service the harness does not model, or traps, the same way both times
is skipped.  Installer routines are not run.  make check-decompiled runs it
with its flags.  Exits nonzero if any bridge disagrees with its routine.
"""

import json
import os
import re
import shlex
import struct
import sys
import tempfile
from dataclasses import dataclass

from harness import (
    CASES,
    REGISTERS,
    STARTING,
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
from libc_check import RAM, RAM_SIZE
from rebuild_link import link, symbols

# The registers gcc's callee must keep.
CALLEE_SAVED = ["D2", "D3", "D4", "D5", "D6", "D7", "A2", "A3", "A4", "A5", "A6"]


@dataclass
class Bridge:
    """A bridged routine: its C prototype's parameter and result types
    and its sidecar convention's registers for each."""

    name: str
    result: str
    parameters: list[str]
    registers: list[str]
    result_register: str | None


def bridges(c: str, conventions: dict) -> list[Bridge]:
    """Every bridge of a routine in ROM whose sidecar gives a register for
    each argument and its result."""
    found = []
    for result, name, parameters in re.findall(r'^(.+?)\b(\w+)\((.*)\) __asm__\("c_\w+"\);$', c, re.M):
        convention = conventions.get(name)
        if convention is None or convention["address"].startswith("installer"):
            continue
        types = [] if parameters == "void" else [re.sub(r"\w+$", "", p.strip()).strip() for p in parameters.split(",")]
        items = convention.get("in") or []
        outs = convention.get("out") or []
        if len(items) != len(types):
            continue
        found.append(
            Bridge(name, result.strip(), types, [i["register"] for i in items], outs[0]["register"] if outs else None)
        )
    return found


def gcc_result(bridge: Bridge, types: Types, direct: int) -> tuple[str, int]:
    """Where gcc takes the result, and what it must be there given the
    routine's own: a char or short extended as its type says."""
    size = types.size(bridge.result)
    if size == 8:
        return "D0:D1", direct
    if size < 4:
        bits = 8 * size
        value = direct & ((1 << bits) - 1)
        if bridge.result not in UNSIGNED and value >> (bits - 1):
            value -= 1 << bits
        return "D0", value & 0xFFFFFFFF
    return ("A0" if types.is_pointer(bridge.result) else "D0"), direct


def load_registers(m: Machine, registers: list[str], values: list[bytes]) -> None:
    for register, value in zip(registers, values):
        if len(value) < 4:
            # a char or short reaches its register extended, as the
            # original's callers load it with moveq
            value = int.from_bytes(value, "big", signed=True).to_bytes(4, "big", signed=True)
        for part, word in zip(register.split(":"), struct.unpack(f">{len(value) // 4}I", value)):
            m.uc.reg_write(REGISTERS[part], word)


def check(m: Machine, types: Types, bridge: Bridge, wrapper: int, address: int):
    """Yields (description, outcome) for each case: outcome None if the
    bridge agrees with its routine, else what differs."""
    for case in range(CASES):
        fresh_ram(m)
        values = arguments(m, types, bridge.parameters, case)
        ram = m.read(RAM, RAM_SIZE)

        reset(m, ram)
        load_registers(m, bridge.registers, values)
        direct = Run(m, address)
        reset(m, ram)
        bridged = Run(m, wrapper, gcc_slots(values))

        stopped = stopped_outcome(direct, bridged, ("direct", "bridged"))
        if stopped:
            yield f"case {case}", stopped
            continue
        differences = []
        if bridge.result != "void" and bridge.result_register:
            register, wanted = gcc_result(bridge, types, direct.value(bridge.result_register))
            got = bridged.value(register)
            if got != wanted:
                differences.append(f"result {got:x} in {register}, routine's {wanted:x}")
        kept = [r for r in REGISTERS if r in CALLEE_SAVED and bridged.registers[r] != STARTING[r]]
        if kept:
            differences.append(f"not kept: {', '.join(kept)}")
        ram_differs = ram_difference(m, direct, bridged)
        if ram_differs:
            differences.append(ram_differs)
        effects_differ = effects_difference(direct, bridged, ("direct", "bridged"))
        if effects_differ:
            differences.append(effects_differ)
        yield f"case {case}", "; ".join(differences) or None


def main() -> int:
    if len(sys.argv) != 3 or "DECOMPILED_CFLAGS" not in os.environ:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    c_path, image_path = sys.argv[1], sys.argv[2]
    c = open(c_path).read()
    conventions = {}
    for line in open(c_path.removesuffix(".c") + ".conventions.jsonl"):
        function = json.loads(line)
        conventions[function["name"]] = function
    with tempfile.TemporaryDirectory() as work:
        elf = work + "/rebuilt.elf"
        linked = link(c_path, shlex.split(os.environ["DECOMPILED_CFLAGS"]), work, elf)
        linked_symbols = symbols(elf)
    machine = rebuilt_machine(open(image_path, "rb").read(), linked, linked_symbols)
    types = Types(c)
    counts = {"agreeing": 0, "skipped": 0, "failed": 0}
    runnable = bridges(c, conventions)
    for bridge in runnable:
        outcomes = check(machine, types, bridge, linked_symbols["c_" + bridge.name], linked_symbols[bridge.name])
        counts[report(bridge.name, list(outcomes))] += 1
    wrappers = sum(name.startswith("c_") for name in linked_symbols)
    print(
        f"bridges agreeing: {counts['agreeing']}, skipped: {counts['skipped']}, failed: {counts['failed']}; "
        f"not run: {wrappers - len(runnable)} (the installer's, or no register in the sidecar)"
    )
    return 1 if counts["failed"] else 0


if __name__ == "__main__":
    sys.exit(main())
