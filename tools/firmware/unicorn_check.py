# /// script
# requires-python = ">=3.11"
# dependencies = ["unicorn==2.1.4"]
# ///
"""Checks the two Unicorn m68k bugs the harness works around (PLAN.md,
"Done 2026-10-22, the harness's status register") are still there, and
that the harness's workarounds still give the right condition codes:

1. A read of UC_M68K_REG_SR (qemu/target/m68k/unicorn.c, reg_read)
   sets env->cc_op to CC_OP_FLAGS before it computes the CCR, so it
   returns the last flag-setting instruction's operands read as flags
   and leaves the CPU to go on with them.  The harness reads the system
   byte from a saved context (harness.system_status) and the codes with
   its probe, move.w ccr to memory (harness.PROBE).
2. A write of PC from a code hook part way through a translated block
   loses QEMU's lazy condition codes.  The harness stops the run in the
   hook and goes on with a run of its own.

Each case runs a compare, 2 with 2 (Z) and 1 with 2 (N, C), then
scs d1 and seq d3, which record the codes the CPU goes on with.

    uv run tools/firmware/unicorn_check.py

make check-decompiled runs it.  Exits nonzero if a bug no longer shows
as recorded, so that the workarounds in harness.py, and this check,
are revisited, or if a workaround gives the wrong codes.
"""

import struct
import sys
from dataclasses import dataclass

import unicorn
from unicorn import UC_ARCH_M68K, UC_HOOK_CODE, UC_HOOK_INTR, UC_MODE_BIG_ENDIAN, Uc
from unicorn.m68k_const import (
    UC_CPU_M68K_M68020,
    UC_M68K_REG_A7,
    UC_M68K_REG_D1,
    UC_M68K_REG_D3,
    UC_M68K_REG_PC,
    UC_M68K_REG_SR,
)

from harness import (
    CONDITION_CODES,
    MOVE_FROM_CCR,
    PROBE,
    PROBE_END,
    PROBE_SIZE,
    SUPERVISOR,
    probed_codes,
    system_status,
)

# The Unicorn the bugs are recorded against.
RECORDED_VERSION = "2.1.4"

# The code's page, where it starts, and the stack's top within it.
CODE = 0
CODE_SIZE = 0x10000
START = 0x1000
STACK_TOP = 0x8000


def words(*values: int) -> bytes:
    return b"".join(struct.pack(">H", value) for value in values)


# move #0,ccr; moveq #2,d0; cmpi.l #2,d0 (M68000PM)
EQUAL = words(0x44FC, 0x0000, 0x7002, 0x0C80, 0x0000, 0x0002)
# move #0,ccr; moveq #1,d0; cmpi.l #2,d0
LESS = words(0x44FC, 0x0000, 0x7001, 0x0C80, 0x0000, 0x0002)
# scs d1; seq d3: each ff if its condition holds, else 00
RECORD = words(0x55C1, 0x57C3)
# trap #0, which the cases' interrupt hooks answer by stepping past it
TRAP = words(0x4E40)
BYTE = 0xFF
SET = 0xFF


@dataclass(frozen=True)
class Compare:
    """A compare, the condition codes it leaves, and what RECORD then
    leaves in D1 and D3."""

    name: str
    code: bytes
    codes: int
    recorded: tuple[int, int]


# Z for 2 with 2; N and C for 1 with 2
COMPARES = (
    Compare("2 cmp 2", EQUAL, 0x04, (0, SET)),
    Compare("1 cmp 2", LESS, 0x09, (SET, 0)),
)

# What the bugs give, in Unicorn RECORDED_VERSION: the read's codes
# (the move to CCR's cleared ones) and the codes the CPU goes on with,
# in either compare.
BUGGY_CODES = 0x00
BUGGY_RECORDED = (0, 0)


def machine(code: bytes) -> Uc:
    """A 68020 with the code at START, in supervisor mode with
    interrupts masked, and the harness's probe mapped."""
    uc = Uc(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN)
    uc.ctl_set_cpu_model(UC_CPU_M68K_M68020)
    uc.mem_map(CODE, CODE_SIZE)
    uc.mem_write(START, code)
    uc.mem_map(PROBE, PROBE_SIZE)
    uc.mem_write(PROBE, MOVE_FROM_CCR)
    uc.reg_write(UC_M68K_REG_SR, SUPERVISOR)
    uc.reg_write(UC_M68K_REG_A7, STACK_TOP)
    return uc


def recorded(uc: Uc) -> tuple[int, int]:
    return uc.reg_read(UC_M68K_REG_D1) & BYTE, uc.reg_read(UC_M68K_REG_D3) & BYTE


def probe(uc: Uc) -> int:
    """The status register as the harness reads it outside a run
    (RebuiltMachine.status), PC kept."""
    pc = uc.reg_read(UC_M68K_REG_PC)
    uc.emu_start(PROBE, PROBE_END)
    uc.reg_write(UC_M68K_REG_PC, pc)
    return system_status(uc) | probed_codes(uc)


def read_after_stop(compare: Compare, workaround: bool) -> tuple[int, tuple[int, int]]:
    """Bug 1: the status register read after a run stops between the
    compare and RECORD, and the run then goes on."""
    code = compare.code + RECORD
    uc = machine(code)
    after = START + len(compare.code)
    uc.emu_start(START, after)
    status = probe(uc) if workaround else uc.reg_read(UC_M68K_REG_SR)
    uc.emu_start(after, START + len(code))
    return status, recorded(uc)


def read_in_trap(compare: Compare, workaround: bool) -> tuple[int, tuple[int, int]]:
    """Bug 1: the status register read in a trap's interrupt hook
    between the compare and RECORD, the trap stepped past.  The
    workaround is RebuiltMachine.with_status and probed: the hook sends
    the CPU through the probe, and a hook at its end puts PC back."""
    code = compare.code + TRAP + RECORD
    uc = machine(code)
    seen = []

    def trap(uc: Uc, number: int, _) -> None:
        past = uc.reg_read(UC_M68K_REG_PC) + len(TRAP)
        if workaround:
            uc.reg_write(UC_M68K_REG_PC, PROBE)
            seen.append(past)
        else:
            seen.append(uc.reg_read(UC_M68K_REG_SR))
            uc.reg_write(UC_M68K_REG_PC, past)

    def probed(uc: Uc, address: int, size: int, _) -> None:
        past = seen.pop()
        uc.reg_write(UC_M68K_REG_PC, past)
        seen.append(system_status(uc) | probed_codes(uc))

    uc.hook_add(UC_HOOK_INTR, trap)
    if workaround:
        uc.hook_add(UC_HOOK_CODE, probed, begin=PROBE_END, end=PROBE_END)
    uc.emu_start(START, START + len(code))
    return seen[0], recorded(uc)


def pc_write_in_hook(compare: Compare, workaround: bool) -> tuple[None, tuple[int, int]]:
    """Bug 2: a code hook at RECORD, in the compare's block, writes PC
    (to RECORD again); the workaround stops the run there instead and
    goes on with a run of its own, as the harness takes an interrupt."""
    code = compare.code + RECORD
    uc = machine(code)
    after = START + len(compare.code)
    done = []

    def hook(uc: Uc, address: int, size: int, _) -> None:
        if done:
            return
        done.append(address)
        if workaround:
            uc.emu_stop()
        else:
            uc.reg_write(UC_M68K_REG_PC, address)

    uc.hook_add(UC_HOOK_CODE, hook, begin=after, end=after)
    uc.emu_start(START, START + len(code))
    if workaround:
        uc.emu_start(uc.reg_read(UC_M68K_REG_PC), START + len(code))
    return None, recorded(uc)


CASES = (read_after_stop, read_in_trap, pc_write_in_hook)


def main() -> int:
    failures = []
    if unicorn.__version__ != RECORDED_VERSION:
        failures.append(f"Unicorn {unicorn.__version__}, the bugs recorded against {RECORDED_VERSION}")
    for case in CASES:
        for compare in COMPARES:
            # the bug as recorded, then the workaround right
            status, after = case(compare, False)
            if status is not None and status & CONDITION_CODES != BUGGY_CODES:
                failures.append(
                    f"{case.__name__}, {compare.name}: Unicorn read the codes {status & CONDITION_CODES:02x}, "
                    f"recorded {BUGGY_CODES:02x}: the bug has changed"
                )
            if after != BUGGY_RECORDED:
                failures.append(
                    f"{case.__name__}, {compare.name}: Unicorn went on with scs, seq {after}, "
                    f"recorded {BUGGY_RECORDED}: the bug has changed"
                )
            status, after = case(compare, True)
            if status is not None and status != SUPERVISOR | compare.codes:
                failures.append(
                    f"{case.__name__}, {compare.name}: the workaround read {status:04x}, "
                    f"want {SUPERVISOR | compare.codes:04x}"
                )
            if after != compare.recorded:
                failures.append(
                    f"{case.__name__}, {compare.name}: with the workaround scs, seq {after}, want {compare.recorded}"
                )
    for failure in failures:
        print(failure)
    if failures:
        print("revisit the workarounds (tools/firmware/harness.py, PROBE; crates/smartclock-emu/src/cpu/unicorn.rs)")
        return 1
    print(f"Unicorn {unicorn.__version__}: both m68k bugs as recorded, the workarounds right")
    return 0


if __name__ == "__main__":
    sys.exit(main())
