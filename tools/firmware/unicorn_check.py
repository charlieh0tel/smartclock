# /// script
# requires-python = ">=3.11"
# dependencies = ["unicorn==2.1.4"]
# ///
"""Checks which of the Unicorn m68k bugs the harness works around
(PLAN.md, "Done 2026-10-22, the harness's status register") the
installed Unicorn shows, against what the workarounds expect, and
that the workarounds give the right condition codes:

1. A read of UC_M68K_REG_SR (qemu/target/m68k/unicorn.c, reg_read)
   sets env->cc_op to CC_OP_FLAGS before it computes the CCR, so it
   returns the last flag-setting instruction's operands read as flags
   and leaves the CPU to go on with them.  Unicorn #2380, fixed on dev
   by #2420, in no release yet.  The harness reads the system byte from
   a saved context (harness.system_status) and the codes with its
   probe, move.w ccr to memory (harness.PROBE).
2. A code hook part way through a translated block runs with
   env->cc_op stale: the translator stores its lazy cc_op only at the
   block's end, a jump or an exception.  So a write of PC from the hook
   loses the condition codes, a read of SR there returns stale ones, a
   write of SR there is read by the rest of the block as the operands
   of the instruction that set the codes, and a write of SR there on an
   instruction that then faults is undone.  Not reported upstream when
   this was written.  The harness stops the run in the hook and goes on
   with a run of its own; in a code hook it reads only the system byte,
   and writes SR only together with PC, which ends the block before the
   instruction runs.

Each case runs a compare, 2 with 2 (Z) and 1 with 2 (N, C), then
scs d1 and seq d3, which record the codes the CPU goes on with, and
says whether Unicorn shows its bug or the fix.  The Unicorn version is
reported, not checked: a dev build calls itself 2.1.4 too.

    uv run tools/firmware/unicorn_check.py

Another Unicorn is checked with its Python, the bindings installed:
`python tools/firmware/unicorn_check.py`.  make check-decompiled runs
it.  Exits nonzero if a bug shows other than its Bug.expected, after
saying what to do about it, or if a workaround gives the wrong codes.
"""

import struct
import sys
from collections.abc import Callable
from dataclasses import dataclass
from enum import Enum

import unicorn
from unicorn import UC_ARCH_M68K, UC_HOOK_CODE, UC_HOOK_INTR, UC_MODE_BIG_ENDIAN, Uc, UcError
from unicorn.m68k_const import (
    UC_CPU_M68K_M68020,
    UC_M68K_REG_A7,
    UC_M68K_REG_D1,
    UC_M68K_REG_D3,
    UC_M68K_REG_PC,
    UC_M68K_REG_SR,
)

from harness import (
    MOVE_FROM_CCR,
    PROBE,
    PROBE_END,
    PROBE_SIZE,
    SUPERVISOR,
    probed_codes,
    system_status,
)

# The code's page, where it starts, and the stack's top within it.
CODE = 0
CODE_SIZE = 0x10000
START = 0x1000
STACK_TOP = 0x8000
# An address no case maps, which a load from faults.
UNMAPPED = 0x00100000


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
# move.l (UNMAPPED).l,d1
LOAD = struct.pack(">HI", 0x2239, UNMAPPED)
BYTE = 0xFF
SET = 0xFF
# What RECORD leaves with the codes clear, and with Z alone set.
CLEAR_RECORDED = (0, 0)
EQUAL_RECORDED = (0, SET)


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

# What a case saw: the status register it read, None for a case that
# reads none, and what RECORD left in D1 and D3.
Seen = tuple[int | None, tuple[int, int]]


class Behavior(Enum):
    """Which a case shows: its bug, or the bug fixed."""

    PRESENT = "present"
    FIXED = "fixed"


@dataclass(frozen=True)
class Bug:
    """A Unicorn bug: its name, what it is, where it stands upstream, which
    behavior the project's workarounds expect of it, and what to do when
    Unicorn shows the other."""

    name: str
    summary: str
    upstream: str
    expected: Behavior
    when_fixed: str
    when_present: str


BUG_1 = Bug(
    "bug 1",
    "a read of SR spoils the condition codes",
    "Unicorn #2380, fixed on dev by #2420",
    Behavior.PRESENT,
    "drop the SR-read workaround (harness.PROBE, RebuiltMachine.status and with_status, and system_status's "
    "saved context), "
    "keep the PC-write one while bug 2 stands, and set BUG_1.expected to FIXED here",
    "Unicorn has bug 1 again: keep or restore the SR-read workaround and set BUG_1.expected to PRESENT here",
)
BUG_2 = Bug(
    "bug 2",
    "a code hook part way through a block sees and leaves stale condition codes",
    "not in a Unicorn release; see PLAN.md",
    Behavior.PRESENT,
    "the PC-write workaround (Run in harness.py, an interrupt taken between runs) may go; nothing else relies "
    "on bug 2 (PLAN.md); set BUG_2.expected to FIXED here",
    "Unicorn has bug 2 again: keep or restore the PC-write workaround and set BUG_2.expected to PRESENT here",
)


@dataclass(frozen=True)
class Case:
    """One way a bug shows.  run(compare, workaround) gives what the case
    saw, Unicorn's own way or, with workaround, the project's; buggy and
    right, what it sees with the bug and without; wanted, what the
    workaround must see, None for a case with no workaround."""

    run: Callable[[Compare, bool], Seen]
    bug: Bug
    buggy: Callable[[Compare], Seen]
    right: Callable[[Compare], Seen]
    wanted: Callable[[Compare], Seen] | None


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


def read_after_stop(compare: Compare, workaround: bool) -> Seen:
    """Bug 1: the status register read after a run stops between the
    compare and RECORD, and the run then goes on."""
    code = compare.code + RECORD
    uc = machine(code)
    after = START + len(compare.code)
    uc.emu_start(START, after)
    status = probe(uc) if workaround else uc.reg_read(UC_M68K_REG_SR)
    uc.emu_start(after, START + len(code))
    return status, recorded(uc)


def read_in_trap(compare: Compare, workaround: bool) -> Seen:
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


def hooked_at_record(compare: Compare, hook: Callable[[Uc, int], None], once: bool = True) -> Uc:
    """Runs the compare and RECORD, a code hook at RECORD, in the
    compare's block, calling hook with RECORD's address, at most once
    if once."""
    code = compare.code + RECORD
    uc = machine(code)
    at = START + len(compare.code)
    done = []

    def code_hook(uc: Uc, address: int, size: int, _) -> None:
        if once and done:
            return
        done.append(address)
        hook(uc, address)

    uc.hook_add(UC_HOOK_CODE, code_hook, begin=at, end=at)
    uc.emu_start(START, START + len(code))
    return uc


def pc_write_in_hook(compare: Compare, workaround: bool) -> Seen:
    """Bug 2: a code hook at RECORD writes PC (to RECORD again); the
    workaround stops the run there instead and goes on with a run of its
    own, as the harness takes an interrupt."""

    def hook(uc: Uc, address: int) -> None:
        if workaround:
            uc.emu_stop()
        else:
            uc.reg_write(UC_M68K_REG_PC, address)

    uc = hooked_at_record(compare, hook)
    if workaround:
        uc.emu_start(uc.reg_read(UC_M68K_REG_PC), START + len(compare.code + RECORD))
    return None, recorded(uc)


def sr_read_in_hook(compare: Compare, workaround: bool) -> Seen:
    """Bug 2: a code hook at RECORD reads the status register, which
    shows the compare's codes only once bug 1 is fixed too; the
    workaround reads the system byte alone, as the harness's code hooks
    do (system_status)."""
    seen = []

    def hook(uc: Uc, address: int) -> None:
        seen.append(system_status(uc) if workaround else uc.reg_read(UC_M68K_REG_SR))

    uc = hooked_at_record(compare, hook)
    return seen[0], recorded(uc)


def sr_write_in_hook(compare: Compare, workaround: bool) -> Seen:
    """Bug 2: a code hook at RECORD writes the status register, its
    codes clear, which RECORD must go on with; the workaround writes PC
    too, to RECORD again, as the harness's move (sp)+,sr does, past the
    instruction."""

    def hook(uc: Uc, address: int) -> None:
        uc.reg_write(UC_M68K_REG_SR, SUPERVISOR)
        if workaround:
            uc.reg_write(UC_M68K_REG_PC, address)

    uc = hooked_at_record(compare, hook)
    return None, recorded(uc)


def sr_write_then_fault(compare: Compare, workaround: bool) -> Seen:
    """Bug 2: a code hook on a load that faults, after the compare in its
    block, writes the status register, its codes clear; a run from past
    the load then records the codes the fault left.  Nothing here does
    this: no workaround."""
    code = compare.code + LOAD + RECORD
    uc = machine(code)
    at = START + len(compare.code)
    uc.hook_add(UC_HOOK_CODE, lambda uc, *_: uc.reg_write(UC_M68K_REG_SR, SUPERVISOR), begin=at, end=at)
    try:
        uc.emu_start(START, START + len(code))
    except UcError:
        pass
    uc.emu_start(at + len(LOAD), START + len(code))
    return None, recorded(uc)


def codes_read(compare: Compare) -> Seen:
    return SUPERVISOR | compare.codes, compare.recorded


def codes_lost(compare: Compare) -> Seen:
    return SUPERVISOR, CLEAR_RECORDED


def codes_kept(compare: Compare) -> Seen:
    return None, compare.recorded


def codes_dropped(compare: Compare) -> Seen:
    return None, CLEAR_RECORDED


def system_byte_read(compare: Compare) -> Seen:
    """The system byte read, its codes clear, as the stale codes, the
    move to CCR's, are too, and the compare's codes gone on with."""
    return SUPERVISOR, compare.recorded


def written_codes_misread(compare: Compare) -> Seen:
    """The codes a hook wrote, clear, read as the compare's operands,
    0 with 0: Z."""
    return None, EQUAL_RECORDED


CASES = (
    Case(read_after_stop, BUG_1, codes_lost, codes_read, codes_read),
    Case(read_in_trap, BUG_1, codes_lost, codes_read, codes_read),
    Case(pc_write_in_hook, BUG_2, codes_dropped, codes_kept, codes_kept),
    Case(sr_read_in_hook, BUG_2, system_byte_read, codes_read, system_byte_read),
    Case(sr_write_in_hook, BUG_2, written_codes_misread, codes_dropped, codes_dropped),
    Case(sr_write_then_fault, BUG_2, written_codes_misread, codes_dropped, None),
)


def shown(seen: Seen) -> str:
    status, after = seen
    read = "" if status is None else f"SR {status:04x}, "
    return f"{read}scs, seq {after}"


def behavior(case: Case, compare: Compare) -> tuple[Behavior | None, Seen]:
    """Which behavior a case shows on a compare, None for neither, and
    what it saw."""
    seen = case.run(compare, False)
    if seen == case.buggy(compare):
        return Behavior.PRESENT, seen
    if seen == case.right(compare):
        return Behavior.FIXED, seen
    return None, seen


def main() -> int:
    print(f"Unicorn {unicorn.__version__}")
    failures = []
    actions = []
    for case in CASES:
        bug = case.bug
        for compare in COMPARES:
            where = f"{case.run.__name__}, {compare.name}"
            shows, seen = behavior(case, compare)
            if shows is None:
                failures.append(
                    f"{where}: {shown(seen)}, neither {bug.name} ({shown(case.buggy(compare))}) "
                    f"nor its fix ({shown(case.right(compare))})"
                )
            elif shows != bug.expected:
                failures.append(f"{where}: {bug.name} {shows.value}, the workarounds expect it {bug.expected.value}")
                action = bug.when_fixed if shows == Behavior.FIXED else bug.when_present
                if action not in actions:
                    actions.append(action)
            if case.wanted is not None:
                seen = case.run(compare, True)
                if seen != case.wanted(compare):
                    failures.append(f"{where}: the workaround saw {shown(seen)}, want {shown(case.wanted(compare))}")
    for failure in failures:
        print(failure)
    for action in actions:
        print(f"so: {action}")
    if failures:
        return 1
    for bug in (BUG_1, BUG_2):
        print(f"{bug.name}, {bug.summary} ({bug.upstream}): {bug.expected.value}, as the workarounds expect")
    print("the workarounds right")
    return 0


if __name__ == "__main__":
    sys.exit(main())
