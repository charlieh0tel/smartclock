# /// script
# requires-python = ">=3.11"
# dependencies = ["unicorn==2.1.4"]
# ///
"""Confirms the decompiled C's bindings by running them: each function
the hand-written bindings/ headers define under the name and prototype
of a routine that takes its arguments in registers, which binds them to
the routine's registers and makes the jsr in one asm statement
(docs/firmware/README.md, "Hand-written C", register-bound calls), is
called in the rebuilt image through a check function gcc compiles in
its own convention, its arguments in 4-byte stack slots, and the
routine itself is called with the same arguments in the registers the
.conventions.jsonl sidecar names.  The two must give the same result,
leave RAM the same and make the same kernel calls and device writes
(harness.py), and the check function must keep the registers gcc's
callee saves and the LIVE values it holds across the binding: gcc keeps
them in every register the binding's statement does not name as
changed, its callee-saved ones among them, so that a register the
routine changes and the statement leaves out of its outputs and
clobbers loses one, or one the check function does not save.  A routine
that returns in the condition codes or in registers beside its result's
has no binding of this kind: the hand-written C calls it in an asm
statement of its own, and the function check runs those callers.

    DECOMPILED_CFLAGS='...' uv run tools/firmware/bridge_check.py \\
        third_party/firmware/z3805a-3543b/decompiled/z3805a-3543b.c \\
        third_party/firmware/z3805a-3543b.bin

A routine that reaches beyond what the harness maps, calls a kernel
service the harness does not model, or traps, the same way both times
is skipped.  The installer's run on a machine of their own, whose RAM is
the installer as the boot code unpacks it (harness.installer_profile),
and are counted apart.  make check-decompiled runs it with its flags.
Exits nonzero if any binding disagrees with its routine.
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
    CALLEE_SAVED,
    CASES,
    PRIMARY,
    REGISTERS,
    STARTING,
    Machine,
    Run,
    Types,
    arguments,
    effects_difference,
    fresh_ram,
    gcc_slots,
    installer_profile,
    overlay,
    ram_difference,
    rebuilt_machine,
    report,
    reset,
    snapshot,
    stopped_outcome,
)
from rebuild_link import link, symbols

# The sidecar's convention of a routine a trap or interrupt enters.
EXCEPTION = "exception"
# The C's include of a bindings header, its path from the C's folder.
BINDINGS_INCLUDE = re.compile(r'^#include "([^"]*/bindings/\w+\.h)"$', re.M)
# A binding's definition: its result type, name and parameters.
BINDING = re.compile(r"static inline\s+([^;{}()]*?)\b(\w+)\s*\(([^()]*)\)\s*\{")
# The values a check function holds across its binding: as many as gcc
# has registers to keep them in, D0 to D7 and A0 to A5, so that it uses
# every one the binding's statement leaves it.
LIVE = 14
# The check functions' names, their binding's after it, and the array
# they load their live values from and store them back to.
CHECK_PREFIX = "binding_check_"
LIVE_ARRAY = "binding_check_live"
# The live values a call starts with: distinct, and no address mapped
# memory lies at.
LIVE_VALUE = 0x6C000000
LIVE_STRIDE = 0x01010101
LONG = struct.Struct(">I")


@dataclass
class Binding:
    """A bound routine: its binding's parameter and result types and its
    sidecar convention's registers for each."""

    name: str
    result: str
    parameters: list[str]
    registers: list[str]
    result_register: str | None


def declared_bindings(c_path: str, c: str) -> dict[str, tuple[str, list[str]]]:
    """The bindings the C's bindings headers define, by name: their
    result type and parameter types."""
    found = {}
    folder = os.path.dirname(os.path.abspath(c_path))
    for header in BINDINGS_INCLUDE.findall(c):
        text = open(os.path.join(folder, header)).read()
        for result, name, parameters in BINDING.findall(text):
            parameters = " ".join(parameters.split())
            types = (
                []
                if parameters in ("", "void")
                else [re.sub(r"\w+$", "", p.strip()).strip() for p in parameters.split(",")]
            )
            found[name] = (" ".join(result.split()), types)
    return found


def bindings(declared: dict, conventions: dict, image: str | None) -> list[Binding]:
    """Every binding of a routine of an image, the primary's or an
    overlay's, not an exception handler, whose sidecar gives a register
    for each argument and its result."""
    found = []
    for name, (result, types) in declared.items():
        convention = conventions.get(name)
        if convention is None or overlay(convention) != image or convention["convention"] == EXCEPTION:
            continue
        items = convention.get("in") or []
        outs = convention.get("out") or []
        if len(items) != len(types):
            continue
        found.append(Binding(name, result, types, [i["register"] for i in items], outs[0]["register"] if outs else None))
    return found


def check_functions(declared: dict) -> str:
    """C for each binding's check function: it loads LIVE values, calls
    the binding with its own arguments and stores the values back."""
    lines = [f"volatile uint32_t {LIVE_ARRAY}[{LIVE}];"]
    for name, (result, types) in declared.items():
        parameters = ", ".join(f"{kind} p{i}" for i, kind in enumerate(types)) or "void"
        passed = ", ".join(f"p{i}" for i in range(len(types)))
        lines.append(f"{result} {CHECK_PREFIX}{name}({parameters}) {{")
        lines += [f"  const uint32_t live{i} = {LIVE_ARRAY}[{i}];" for i in range(LIVE)]
        call = f"{name}({passed})"
        lines.append(f"  {call};" if result == "void" else f"  const {result} result = {call};")
        lines += [f"  {LIVE_ARRAY}[{i}] = live{i};" for i in range(LIVE)]
        if result != "void":
            lines.append("  return result;")
        lines.append("}")
    return "\n" + "\n".join(lines) + "\n"


def gcc_result(binding: Binding, types: Types) -> tuple[str, int]:
    """Where gcc takes the result, and the bits of it compared: a char's
    or short's alone, which gcc's callee leaves unextended and its
    caller extends."""
    size = types.size(binding.result)
    if size == 8:
        return "D0:D1", (1 << 64) - 1
    if size < 4:
        return "D0", (1 << (8 * size)) - 1
    return ("A0" if types.is_pointer(binding.result) else "D0"), 0xFFFFFFFF


def load_registers(m: Machine, registers: list[str], values: list[bytes]) -> None:
    for register, value in zip(registers, values):
        if len(value) < 4:
            # a char or short reaches its register extended, as the
            # original's callers load it with moveq
            value = int.from_bytes(value, "big", signed=True).to_bytes(4, "big", signed=True)
        for part, word in zip(register.split(":"), struct.unpack(f">{len(value) // 4}I", value)):
            m.uc.reg_write(REGISTERS[part], word)


def live_values() -> list[int]:
    return [LIVE_VALUE + LIVE_STRIDE * i for i in range(LIVE)]


def check(m: Machine, types: Types, binding: Binding, function: int, address: int, live: int):
    """Yields (description, outcome) for each case: outcome None if the
    binding agrees with its routine, else what differs."""
    for case in range(CASES):
        fresh_ram(m, case)
        values = arguments(m, types, binding.parameters, case)
        memory = snapshot(m)

        reset(m, memory)
        load_registers(m, binding.registers, values)
        direct = Run(m, address)
        reset(m, memory)
        m.uc.mem_write(live, b"".join(LONG.pack(v) for v in live_values()))
        bound = Run(m, function, gcc_slots(values))
        kept = [LONG.unpack(m.read(live + LONG.size * i, LONG.size))[0] for i in range(LIVE)]

        stopped = stopped_outcome(direct, bound, ("direct", "bound"))
        if stopped:
            yield f"case {case}", stopped
            continue
        differences = []
        returned = direct.ended is None
        if returned and binding.result != "void" and binding.result_register:
            register, bits = gcc_result(binding, types)
            wanted = direct.value(binding.result_register) & bits
            got = bound.value(register) & bits
            if got != wanted:
                differences.append(f"result {got:x} in {register}, routine's {wanted:x}")
        not_kept = [r for r in REGISTERS if returned and r in CALLEE_SAVED and bound.registers[r] != STARTING[r]]
        if not_kept:
            differences.append(f"not kept: {', '.join(not_kept)}")
        lost = [f"{i}: {got:x}" for i, (got, wanted) in enumerate(zip(kept, live_values())) if returned and got != wanted]
        if lost:
            differences.append(f"live values lost: {', '.join(lost)}")
        ram_differs = ram_difference(m, direct, bound)
        if ram_differs:
            differences.append(ram_differs)
        effects_differ = effects_difference(direct, bound, ("direct", "bound"))
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
    declared = {name: kinds for name, kinds in declared_bindings(c_path, c).items() if name in conventions}
    with tempfile.TemporaryDirectory() as work:
        elf = work + "/rebuilt.elf"
        linked = link(c_path, shlex.split(os.environ["DECOMPILED_CFLAGS"]), work, elf, check_functions(declared))
        linked_symbols = symbols(elf)
    image = open(image_path, "rb").read()
    types = Types(c)
    run, failed = 0, 0
    for profile in (PRIMARY, installer_profile(image)):
        machine = rebuilt_machine(image, linked, linked_symbols, profile)
        counts = {"agreeing": 0, "skipped": 0, "failed": 0}
        runnable = bindings(declared, conventions, profile.overlay)
        for binding in runnable:
            outcomes = check(
                machine,
                types,
                binding,
                linked_symbols[CHECK_PREFIX + binding.name],
                linked_symbols[binding.name],
                linked_symbols[LIVE_ARRAY],
            )
            counts[report(binding.name, list(outcomes))] += 1
        label = f"{profile.overlay} " if profile.overlay else ""
        print(
            f"{label}bindings agreeing: {counts['agreeing']}, skipped: {counts['skipped']}, failed: {counts['failed']}"
        )
        run += len(runnable)
        failed += counts["failed"]
    print(f"bindings not run: {len(declared) - run} (an exception handler's, or no register in the sidecar)")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
