# /// script
# requires-python = ">=3.11"
# dependencies = ["unicorn>=2.1"]
# ///
"""Confirms the decompiled C's bridges by running them: each wrapper c_NAME,
which gcc's C calls in place of a routine that takes its arguments in
registers (docs/firmware/README.md), is called in a Unicorn 68k emulator
as gcc calls it, its arguments in 4-byte stack slots, and the routine
itself is called with the same arguments in the registers the
.conventions.jsonl sidecar names.  The two must give the same result and
leave RAM the same, and the wrapper must keep the registers gcc's
callee saves.

    uv run tools/firmware/bridge_check.py \\
        third_party/firmware/decompiled/z3805a-3543b.c \\
        third_party/firmware/z3805a-3543b.bin

A routine that reaches beyond ROM and RAM, or traps, the same way both
times is skipped: a peripheral or the kernel would decide its result.
Installer routines are not run.  Unicorn has no CPU32, so the 68020
stands in: what the bridges and the routines under them use is common to
both.  Needs m68k-linux-gnu binutils.  Exits nonzero if any bridge
disagrees with its routine.
"""

import json
import re
import struct
import subprocess
import sys
import tempfile
from dataclasses import dataclass

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

from decompiled_check import TOOLS, assembly_lines
from libc_check import (
    BUFFER_SPACING,
    RAM,
    RAM_SIZE,
    RETURN,
    STACK_TOP,
    STEP_LIMIT,
    Machine,
)

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
# The registers gcc's callee must keep.
CALLEE_SAVED = ["D2", "D3", "D4", "D5", "D6", "D7", "A2", "A3", "A4", "A5", "A6"]
# Where the wrappers are linked and loaded: clear of ROM, RAM, the
# peripherals and the return address.
BRIDGES = 0x400000
BRIDGES_SIZE = 0x10000
# The status register a call starts with: supervisor, interrupts masked,
# as the firmware runs.
SUPERVISOR = 0x2700
# Below the stack top, what a call's frame may write; not compared.
STACK_SPAN = 0x1000
# A register's value before a call, distinct for each register.
SENTINEL = 0x5A5A0000
# Arguments the cases cycle through, by kind.
INTEGERS = [0, 1, -1, 0x12345678, -100000, 7]
CHARS = [-1, 0x41, 0x7F, 0, -0x80, 3]
SHORTS = [-2, 0x1234, -30000, 0, 0x7FFF, 9]
REALS = [1.5, -2.25, 1e10, 3.0e-5, 0.0, 123.456]
CASES = 6
# How far apart in the lists one case's successive arguments are taken.
POSITION_STRIDE = 1
# The bytes of a pointer argument's buffer, by what it points at.
TEXT = b"123.456\0"


@dataclass
class Bridge:
    """A bridged routine: its C prototype's parameter and result types
    and its sidecar convention's registers for each."""

    name: str
    result: str
    parameters: list[str]
    registers: list[str]
    result_register: str | None


def pointer_typedefs(c: str) -> set[str]:
    return set(re.findall(r"^typedef [^;()]*\*\s*(\w+);$", c, re.M))


def bridges(c: str, conventions: dict) -> list[Bridge]:
    """Every bridge whose routine's sidecar gives a register for each
    argument and its result."""
    found = []
    for result, name, parameters in re.findall(
        r'^(.+?)\b(\w+)\((.*)\) __asm__\("c_\w+"\);$', c, re.M
    ):
        convention = conventions.get(name)
        if convention is None or convention["address"].startswith("installer"):
            continue
        types = [] if parameters == "void" else [re.sub(r"\w+$", "", p).strip() for p in parameters.split(",")]
        items = convention.get("in") or []
        outs = convention.get("out") or []
        if len(items) != len(types):
            continue
        result = result.strip()
        found.append(
            Bridge(name, result, types, [i["register"] for i in items], outs[0]["register"] if outs else None)
        )
    return found


def link(c: str, addresses: dict[str, int], work: str) -> tuple[dict[str, int], bytes]:
    """The wrappers linked at BRIDGES against the routines' addresses:
    each wrapper's address, and the linked bytes."""
    blocks = re.findall(r"^/\* \w+ from gcc's convention.*?\*/\n(__asm__\(.*?\);)$", c, re.S | re.M)
    source, obj, linked, raw = (work + name for name in ("/w.s", "/w.o", "/w.elf", "/w.bin"))
    open(source, "w").write("".join(assembly_lines("\n".join(blocks))))
    subprocess.run([TOOLS + "as", "-mcpu=cpu32", "-o", obj, source], check=True)
    script = work + "/w.ld"
    with open(script, "w") as out:
        for name, address in addresses.items():
            out.write(f"{name} = 0x{address:x};\n")
        out.write(f"SECTIONS {{ . = 0x{BRIDGES:x}; .text : {{ *(.text*) }} }}\n")
    subprocess.run([TOOLS + "ld", "-T", script, "-o", linked, obj], check=True)
    subprocess.run([TOOLS + "objcopy", "-O", "binary", "-j", ".text", linked, raw], check=True)
    wrappers = {}
    for line in subprocess.run([TOOLS + "nm", linked], capture_output=True, text=True).stdout.splitlines():
        value, kind, symbol = line.split()
        if kind in "Tt" and symbol.startswith("c_"):
            wrappers[symbol.removeprefix("c_")] = int(value, 16)
    return wrappers, open(raw, "rb").read()


def size_of(kind: str, pointers: set[str]) -> int:
    if kind.endswith("*") or kind in pointers:
        return 4
    return {"char": 1, "short": 2, "double": 8}.get(kind, 4)


def argument(m: Machine, kind: str, case: int, pointers: set[str]) -> bytes:
    """The case'th argument of a kind, as its bytes, a pointer to a fresh
    buffer of what it points at."""
    if kind.endswith("*") or kind in pointers:
        pointee = kind.removesuffix("*").strip()
        real = REALS[case % len(REALS)]
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
        return struct.pack(">d", REALS[case % len(REALS)])
    if kind == "float":
        return struct.pack(">f", REALS[case % len(REALS)])
    if kind == "char":
        return struct.pack(">b", CHARS[case % len(CHARS)])
    if kind == "short":
        return struct.pack(">h", SHORTS[case % len(SHORTS)])
    return struct.pack(">I", INTEGERS[case % len(INTEGERS)] & 0xFFFFFFFF)


class Run:
    """One call's outcome: its registers after, RAM after, and the fault
    or trap that stopped it, if one did."""

    def __init__(self, m: Machine, start: int, sp: int):
        trapped = []

        def trap(uc, number, _):
            trapped.append(number)
            uc.emu_stop()

        hook = m.uc.hook_add(UC_HOOK_INTR, trap)
        m.uc.reg_write(UC_M68K_REG_A7, sp)
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
        self.ram = m.read(RAM, STACK_TOP - STACK_SPAN - RAM)


def reset(m: Machine, ram: bytes):
    m.uc.mem_write(RAM, ram)
    m.uc.reg_write(UC_M68K_REG_SR, SUPERVISOR)
    for i, (_, register) in enumerate(REGISTERS.items()):
        m.uc.reg_write(register, SENTINEL + i)


def result_of(run: Run, register: str) -> int:
    """A result's value in a register or pair, high register first."""
    value = 0
    for part in register.split(":"):
        value = value << 32 | run.registers[part]
    return value


def gcc_result(bridge: Bridge, direct: int, pointers: set[str]) -> tuple[str, int]:
    """Where gcc takes the result, and what it must be there given the
    routine's own: a char or short extended as its type says."""
    kind = bridge.result
    size = size_of(kind, pointers)
    if size == 8:
        return "D0:D1", direct
    if size < 4:
        bits = 8 * size
        value = direct & ((1 << bits) - 1)
        signed = not kind.startswith(("u", "unsigned"))
        if signed and value >> (bits - 1):
            value -= 1 << bits
        return "D0", value & 0xFFFFFFFF
    return ("A0" if kind.endswith("*") or kind in pointers else "D0"), direct


def check(m: Machine, bridge: Bridge, wrapper: int, address: int, pointers: set[str]):
    """Yields (description, outcome) for each case: outcome None if the
    bridge agrees with its routine, else what differs."""
    for case in range(CASES):
        m.next_buffer = RAM + 0x8000
        reset(m, bytes(RAM_SIZE))
        # each position takes a different value, so that arguments
        # swapped or one loaded twice show
        values = [
            argument(m, kind, case + POSITION_STRIDE * position, pointers)
            for position, kind in enumerate(bridge.parameters)
        ]
        ram = m.read(RAM, RAM_SIZE)
        description = f"case {case}"

        reset(m, ram)
        for register, value in zip(bridge.registers, values):
            parts = register.split(":")
            words = struct.unpack(f">{len(value) // 4}I", value) if len(value) >= 4 else None
            if words is None:
                # a char or short reaches its register extended, as the
                # original's callers load it with moveq
                words = [struct.unpack(">i", struct.pack(">i", int.from_bytes(value, "big", signed=True)))[0] & 0xFFFFFFFF]
            for part, word in zip(parts, words):
                m.uc.reg_write(REGISTERS[part], word)
        sp = STACK_TOP - 4
        m.uc.mem_write(sp, struct.pack(">I", RETURN))
        direct = Run(m, address, sp)

        reset(m, ram)
        slots = b"".join(
            value if len(value) >= 4 else int.from_bytes(value, "big", signed=True).to_bytes(4, "big", signed=True)
            for value in values
        )
        sp = STACK_TOP - 4 - len(slots)
        m.uc.mem_write(sp, struct.pack(">I", RETURN) + slots)
        bridged = Run(m, wrapper, sp)

        if direct.stopped or bridged.stopped:
            same = direct.stopped is not None and bridged.stopped is not None
            yield description, "skipped: " + direct.stopped if same else (
                f"one stopped: direct {direct.stopped}, bridged {bridged.stopped}"
            )
            continue
        differences = []
        if bridge.result != "void" and bridge.result_register:
            register, wanted = gcc_result(bridge, result_of(direct, bridge.result_register), pointers)
            got = result_of(bridged, register)
            if got != wanted:
                differences.append(f"result {got:x} in {register}, routine's {wanted:x}")
        kept = [r for i, r in enumerate(REGISTERS) if r in CALLEE_SAVED and bridged.registers[r] != SENTINEL + i]
        if kept:
            differences.append(f"not kept: {', '.join(kept)}")
        if direct.ram != bridged.ram:
            first = next(i for i in range(len(direct.ram)) if direct.ram[i] != bridged.ram[i])
            differences.append(f"RAM differs from {RAM + first:x}")
        yield description, "; ".join(differences) or None


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    c_path, image_path = sys.argv[1], sys.argv[2]
    c = open(c_path).read()
    conventions = {}
    for line in open(c_path.removesuffix(".c") + ".conventions.jsonl"):
        function = json.loads(line)
        conventions[function["name"]] = function
    addresses = {name: int(function["address"].split(":")[-1], 16) for name, function in conventions.items()}
    pointers = pointer_typedefs(c)
    with tempfile.TemporaryDirectory() as work:
        wrappers, linked = link(c, addresses, work)
    machine = Machine(open(image_path, "rb").read())
    machine.uc.mem_map(BRIDGES, BRIDGES_SIZE)
    machine.uc.mem_write(BRIDGES, linked)
    agree = skipped = failed = 0
    runnable = bridges(c, conventions)
    for bridge in runnable:
        outcomes = list(check(machine, bridge, wrappers[bridge.name], addresses[bridge.name], pointers))
        failures = [(d, o) for d, o in outcomes if o and not o.startswith("skipped")]
        skips = [o for _, o in outcomes if o and o.startswith("skipped")]
        if failures:
            failed += 1
            for description, outcome in failures:
                print(f"FAIL {bridge.name} {description}: {outcome}")
        elif len(skips) == len(outcomes):
            skipped += 1
            print(f"skip {bridge.name}: {skips[0].removeprefix('skipped: ')}")
        else:
            agree += 1
            print(f"ok {bridge.name}: {len(outcomes) - len(skips)} of {len(outcomes)} cases run")
    print(
        f"bridges agreeing: {agree}, skipped: {skipped}, failed: {failed}; "
        f"not run: {len(wrappers) - len(runnable)} (the installer's, or no register in the sidecar)"
    )
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
