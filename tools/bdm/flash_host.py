# /// script
# requires-python = ">=3.11"
# ///
"""Programs a 58503A's flash over BDM with the routine of
flash_routine.S, through any Target (target.py): the whole procedure,
from a reset entry to a verified image, and the refusals that keep it
off the boot code and installer (tools/bdm/README.md, "The host").

    python3 -I tools/bdm/flash_host.py --plan CURRENT IMAGE [--allow-low]

prints, from a dump of the flash and the image to program, the sector
pairs a run would erase, or why it would refuse; no target is touched.

    python3 -I tools/bdm/flash_host.py (--pod POD | --model FLASH) --image IMAGE
        (--dry-run | --identify | --write --restore-to FILE) [--expect-sha256 SHA256] [--allow-low]
        [--any-entry-state]

runs the procedure below on a pod (pod_target.py) or on the modeled
board holding FLASH (model_target.py): --dry-run steps 1-4 and 6,
sending the flash no command and running no code; --identify steps
1-6, the routine's identify the only code run; --write all of it, the
dump saved to FILE first, and with --expect-sha256 refused unless the
dump has that SHA-256.  It writes exactly the image it is given: its
lane sums are reported, never refused.  It never starts the firmware:
it ends with the CPU in background mode.

The procedure, each step's failure fatal, never retried:

1. Reset into BDM; read and report the SIM's entry state, and refuse
   anything but a fresh reset's unless told (--any-entry-state).
2. Write SYPCR once: watchdog off, bus monitor on (SYPCR_VALUE).
3. Write the chip selects in CHIP_SELECTS's order, each read back,
   refusing any step that adds a hazard to the decode (hazards()).
4. Load the vector table and the routine into RAM and read them back.
5. Identify both pairs; refuse unless both lanes of both are AMD
   Am29F010s and no sector pair to be written is protected.
6. Dump the flash: its SHA-256 is the restore image's.  Save it if
   asked, and refuse to go on if a SHA-256 is expected and it has
   another.
7. For each sector pair that differs from the image: erase (with the
   routine's blank check), program, verify.
8. Dump the flash again and compare it with the image.

Every call ends in the routine's read/reset of both pairs; on a
failure the host writes read/reset itself before it stops, unless the
routine timed out with a part still busy.  The host never sets PC
outside the routine: it has no way to resume the firmware.
"""

import argparse
import contextlib
import hashlib
import json
import math
import sys
from collections.abc import Callable
from dataclasses import dataclass, field
from enum import Enum
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent / "firmware"))
from board import FLASH_SIZE, PAIR_SIZE, READ_SELECTS, SECTOR_PAIR_SIZE, WORD, WRITE_SELECTS  # noqa: E402
from lane_sums import BYTE_BITS, STORED_SIZE, lane_sums  # noqa: E402
from pod_target import PodTarget  # noqa: E402
from target import Entry, Target, TargetError, TargetTimeout  # noqa: E402

ROUTINE_SYMBOLS = HERE.parents[1] / "target" / "bdm" / "flash_routine.json"

# Below PROTECTED_END are the reset vectors, the boot code and the
# installer, which neither the boot checksum nor the installer's
# writable range covers and which only a programmer could restore
# (docs/firmware/restart.md, "The installer";
# installer_scpi_diagnostic.h, INSTALLER_SCPI_DIAGNOSTIC_FLASH_START).
PROTECTED_END = 0x10000
# The spans each pair's lane sums cover, the sums in each span's last
# four bytes (docs/firmware/restart.md, "The installer", Boot check;
# firmware.rs Layout::AmdLanes).
LANE_SUM_SPANS = ((PROTECTED_END, PAIR_SIZE), (PAIR_SIZE, FLASH_SIZE))

# RAM, the CS0/CS2/CS3 block, all of it free since the firmware never
# runs; the vector table and routine above the firmware's preserved
# region and crash record, 0x100000-0x100bcb, left for inspection
# (docs/firmware/structures.md, "The preserved region"); the stack
# below RAM's top by STACK_SLACK, so that a handler reading past a
# short frame stays in RAM.
RAM_BASE, RAM_END = 0x100000, 0x110000
VECTOR_TABLE = 0x101000
ROUTINE = 0x101400
BUFFER = 0x102000
STACK_SLACK = 0x100
STACK_TOP = RAM_END - STACK_SLACK
# The vector table: 256 longs (MC68331UM 5.9.1).
VECTORS, VECTOR_SIZE = 256, 4
# SR for a call: supervisor, interrupts masked to level 7, no trace
# (MC68331UM 5.2.4.1).  Level 7 is not maskable; a level 7 interrupt goes
# to the vector table like any exception.
SR_SUPERVISOR_MASKED = 0x2700
# BGND, $4AFA, one word (MC68331UM 5.10.2.2.2): after the routine's,
# PC is its address plus this (target.py, read_register).
BGND_SIZE = 2
# A call's outputs: D1 and D2 hold words; an exception's format and
# vector word holds the vector's offset in bits 11-0 (CPU32RM 6.4).
WORD_MASK = 0xFFFF
BYTE_MASK = 0xFF
VECTOR_OFFSET_MASK = 0x0FFF
# identify's results: manufacturer, device, then each sector pair's
# protection word; and what an unprotected Am29F010B pair reads, the
# code in both lanes (Am29F010B, Table 3, Table 4).
IDENTITY_WORDS = 10
AMD_BOTH, AM29F010B_BOTH, UNPROTECTED_BOTH = 0x0101, 0x2020, 0x0000

# The SIM's registers, MM set (MC68331UM Table D-3).
SIMCR = 0xFFFA00
SYNCR = 0xFFFA04
SYPCR = 0xFFFA21
CSPAR0, CSPAR1 = 0xFFFA44, 0xFFFA46
CSBARBT, CSORBT = 0xFFFA48, 0xFFFA4A
CSBAR_FIRST, CS_STRIDE, CSOR_OFFSET = 0xFFFA4C, 4, 2


def csbar(select: int) -> int:
    return CSBAR_FIRST + CS_STRIDE * select


def csor(select: int) -> int:
    return csbar(select) + CSOR_OFFSET


# SIMCR's MM: the modules at $FFF000 (MC68331UM D.3.1), which every
# address here assumes.
SIMCR_MM = 0x0040
# The SIM registers the host reads and reports at entry, each with a
# fresh reset's value, as the bench unit read it at a reset entry on
# 2026-11-21 and as MC68331UM D.3.12 and D.3.23-D.3.26 give the reset
# values for its DATA pins: SYPCR still unwritten (it is write-once),
# CSBOOT over 1 MiB for reads and writes, every pin a 16-bit select.
# The host refuses any other unless told (any_entry_state); either way
# the hazard check starts from what it reads.
ENTRY_STATE = (
    ("SYPCR", SYPCR, 1, 0x80),
    ("CSPAR0", CSPAR0, 2, 0x3FFF),
    ("CSPAR1", CSPAR1, 2, 0x03FF),
    ("CSBARBT", CSBARBT, 2, 0x0007),
    ("CSORBT", CSORBT, 2, 0x7B70),
)
# SYPCR, written once (MC68331UM D.3.12): SWE 0, the watchdog off, which
# would otherwise reset the CPU 2^9 crystal periods (16 ms) into the
# routine, SWP being 0 on this board (bench SYPCR $80; Table D-6); HME
# 0; BME 1, so that a cycle no select answers ends in a bus error, and
# so through the vector table to BGND, not a hang; BMT 00, 64 clocks,
# well past the selects' one wait state.
SYPCR_VALUE = 0x04
# The 3704 boot code's selects, those that reach the flash and the RAM,
# in the order that worked on the bench, 2026-11-21
# (docs/firmware/chip-selects.md, 58503A 3704): CSORBT read-only before
# anything else, so that no write asserts the parts' output enable;
# CSBARBT narrowed before CS1 and CS7 open pair 2's range, so that
# CSBOOT's 1 MiB mirror never answers there.
CHIP_SELECTS = (
    ("CSORBT", CSORBT, 0x6870),
    ("CSBARBT", CSBARBT, 0x0005),
    ("CSBAR1", csbar(1), 0x0405),
    ("CSOR1", csor(1), 0x6870),
    ("CSBAR6", csbar(6), 0x0005),
    ("CSOR6", csor(6), 0x7070),
    ("CSBAR7", csbar(7), 0x0405),
    ("CSOR7", csor(7), 0x7070),
    ("CSBAR0", csbar(0), 0x1003),
    ("CSOR0", csor(0), 0x6830),
    ("CSBAR2", csbar(2), 0x1003),
    ("CSOR2", csor(2), 0x5030),
    ("CSBAR3", csbar(3), 0x1003),
    ("CSOR3", csor(3), 0x3030),
    ("CSPAR0", CSPAR0, 0x2BFF),
    ("CSPAR1", CSPAR1, 0x03AF),
)

# The clock: the 32.768 kHz crystal (docs/firmware/restart.md) through
# the synthesizer, F = Fref * 4(Y + 1) * 2^(2W + X) (MC68331UM 4.3.2,
# D.3.3); the host leaves SYNCR at its reset value.
REFERENCE_HZ = 32768
SYNCR_W, SYNCR_X, SYNCR_Y_SHIFT, SYNCR_Y_MASK = 0x8000, 0x4000, 8, 0x3F
# A status poll's least time: one read, three clocks (MC68331UM 4.5.2)
# and the flash selects' one wait state (CSOR DSACK 0001, Table D-12),
# so that a limit of T * F / POLL_CLOCKS_MIN polls lasts at least T.
POLL_CLOCKS_MIN = 4
# An upper bound on the clocks between two steps of the routine, a poll
# or a word's pass, which test_flash.py holds mc683xx's count to;
# and the passes each word of a run may take: the scan, the compare,
# the command and its check.
STEP_CLOCKS_MAX = 100
PASSES_PER_WORD = 4
# The limits on a part's operations: a byte's program, 300 us at most,
# and a sector's erase, 15 s at most excluding the preprogram of up to
# 16384 bytes at 300 us each (Am29F010B, Erase and Programming
# Performance, p. 28), each rounded up.
PROGRAM_TIME = 0.001
ERASE_TIME = 25.0
# Time beyond the routine's own bound that the host waits for it: a
# pod's latency.
WAIT_MARGIN = 1.0


class Refused(Exception):
    """A precondition failed: the procedure stops, the flash as it was
    unless the message says otherwise."""


class Failed(Exception):
    """The target or the routine failed mid-procedure."""


class Busy(Failed):
    """The routine gave up on a part still erasing or programming, which
    a reset does not stop (Am29F010B, Reset Command, p. 12)."""


@dataclass
class Routine:
    """flash_routine.S assembled (build_routine.py): its bytes, its
    labels' offsets and its constants."""

    code: bytes
    labels: dict[str, int]
    constants: dict[str, int]

    @staticmethod
    def load(symbols: Path = ROUTINE_SYMBOLS) -> "Routine":
        table = json.loads(symbols.read_text())
        code = symbols.with_suffix(".bin").read_bytes()
        if len(code) != table["size"]:
            raise Refused(f"{symbols}: the routine is {len(code)} bytes, not {table['size']}")
        return Routine(code, table["labels"], table["constants"])

    def __getattr__(self, name: str) -> int:
        try:
            return self.constants[name]
        except KeyError:
            raise AttributeError(name) from None

    def status_name(self, status: int) -> str:
        names = [name for name, value in self.constants.items() if name.startswith("STATUS_") and value == status]
        return names[0] if names else f"status {status}"


@dataclass
class Result:
    """A call's outputs (flash_routine.S's interface)."""

    status: int
    address: int
    observed: int
    expected: int


# The selects whose decode hazards() judges: the flash's and the RAM's.
SELECTS = ("CSBOOT", "CS0", "CS1", "CS2", "CS3", "CS6", "CS7")
# CSBAR: the base in bits 15-3 as ADDR23-11, the block size in 2-0,
# 2 KiB << BLKSZ but 1 MiB for 111 (MC68331UM D.3.25, Table D-11).
CSBAR_BASE_MASK, CSBAR_BASE_SHIFT, CSBAR_BLOCK_MASK = 0xFFF8, 8, 0x7
BLOCK_SIZES = (0x800, 0x2000, 0x4000, 0x10000, 0x20000, 0x40000, 0x80000, 0x100000)
# CSOR: BYTE in 14-13, 00 disabled; R/W in 12-11, 01 read, 10 write,
# 11 both (D.3.26, Table D-12).
CSOR_BYTE_SHIFT, CSOR_RW_SHIFT, FIELD_MASK = 13, 11, 0x3
RW_READ, RW_WRITE = 0x1, 0x2
# CSPAR fields: each pin's two bits, 10 or 11 a chip select (D.3.23,
# D.3.24, Table D-10); CSBOOT's pin is always CSBOOT.
CSPAR_FIELDS = {"CS0": (CSPAR0, 2), "CS1": (CSPAR0, 4), "CS2": (CSPAR0, 6), "CS3": (CSPAR0, 8), "CS6": (CSPAR1, 0), "CS7": (CSPAR1, 2)}
CSPAR_CHIP_SELECT = 0x2


class Hazard(Enum):
    """What a chip-select decode can do wrong to the flash."""

    # a flash read select asserted on writes: output enable during a
    # write cycle, the parts driving the bus against the CPU
    READ_SELECT_WRITES = "a flash read select asserts on writes"
    # a flash select over the other pair: CSBOOT's 1 MiB at reset puts
    # pair 1 at 0x40000 (bench, 2026-11-21)
    MIRROR = "a flash select reaches outside its pair"
    READ_OVERLAP = "two selects drive a flash read"
    WRITE_OVERLAP = "two selects take a flash write"


def decode(registers: dict[int, int]) -> dict[str, tuple[int, int, int]]:
    """Each enabled select of SELECTS: (base, end, R/W field)."""
    enabled = {}
    for name in SELECTS:
        bar, option = (CSBARBT, CSORBT) if name == "CSBOOT" else (csbar(int(name[2:])), csor(int(name[2:])))
        if name in CSPAR_FIELDS:
            register, shift = CSPAR_FIELDS[name]
            if not registers[register] >> shift & CSPAR_CHIP_SELECT:
                continue
        value = registers[option]
        if not value >> CSOR_BYTE_SHIFT & FIELD_MASK:
            continue
        base = (registers[bar] & CSBAR_BASE_MASK) << CSBAR_BASE_SHIFT
        enabled[name] = (base, base + BLOCK_SIZES[registers[bar] & CSBAR_BLOCK_MASK], value >> CSOR_RW_SHIFT & FIELD_MASK)
    return enabled


def hazards(registers: dict[int, int]) -> set[tuple[Hazard, frozenset[str]]]:
    """The decode's hazards in the flash's space, each with its selects."""
    found = set()
    selects = {name: (max(base, 0), min(end, FLASH_SIZE), rw) for name, (base, end, rw) in decode(registers).items()}
    selects = {name: span for name, span in selects.items() if span[0] < span[1]}
    for name, (base, end, rw) in selects.items():
        if name in READ_SELECTS and rw & RW_WRITE:
            found.add((Hazard.READ_SELECT_WRITES, frozenset([name])))
        pair = READ_SELECTS.get(name, WRITE_SELECTS.get(name))
        if pair is not None and (base < pair * PAIR_SIZE or end > (pair + 1) * PAIR_SIZE):
            found.add((Hazard.MIRROR, frozenset([name])))
    names = sorted(selects)
    for index, one in enumerate(names):
        for other in names[index + 1 :]:
            (base1, end1, rw1), (base2, end2, rw2) = selects[one], selects[other]
            if base1 < end2 and base2 < end1:
                if rw1 & rw2 & RW_READ:
                    found.add((Hazard.READ_OVERLAP, frozenset([one, other])))
                if rw1 & rw2 & RW_WRITE:
                    found.add((Hazard.WRITE_OVERLAP, frozenset([one, other])))
    return found


def system_hz(syncr: int) -> int:
    w = 1 if syncr & SYNCR_W else 0
    x = 1 if syncr & SYNCR_X else 0
    return REFERENCE_HZ * 4 * ((syncr >> SYNCR_Y_SHIFT & SYNCR_Y_MASK) + 1) * 2 ** (2 * w + x)


def check_image(image: bytes) -> None:
    """Refuses an image that is not the whole flash."""
    if len(image) != FLASH_SIZE:
        raise Refused(f"the image is {len(image)} bytes, not {FLASH_SIZE}")


def lane_sum_report(image: bytes) -> list[str]:
    """Whether each LANE_SUM_SPANS span's lane sums hold, and what it
    means if one does not: the boot check fails and the unit boots into
    the installer, not the primary (docs/firmware/restart.md, "The
    installer", Boot check)."""
    lines = []
    for start, end in LANE_SUM_SPANS:
        computed, stored = lane_sums(image, start, end)
        verdict = "hold" if computed == stored else f"DO NOT HOLD: computed {computed}, stored {stored}"
        lines.append(f"image lane sums of {start:#x}-{end:#x} {verdict}")
    if any("DO NOT HOLD" in line for line in lines):
        lines.append("WARNING: an image whose lane sums fail boots into the installer, not the primary")
    return lines


def with_lane_sums(image: bytes) -> bytes:
    """image with each LANE_SUM_SPANS span's stored sums made to hold."""
    fixed = bytearray(image)
    for start, end in LANE_SUM_SPANS:
        (even, odd), _ = lane_sums(fixed, start, end)
        fixed[end - STORED_SIZE : end] = bytes([even >> BYTE_BITS, odd >> BYTE_BITS, even & BYTE_MASK, odd & BYTE_MASK])
    return bytes(fixed)


def plan(current: bytes, image: bytes, allow_low: bool = False) -> list[int]:
    """The sector pairs where current and image differ; refused if one
    is below PROTECTED_END and allow_low is not given."""
    differing = [
        base
        for base in range(0, FLASH_SIZE, SECTOR_PAIR_SIZE)
        if current[base : base + SECTOR_PAIR_SIZE] != image[base : base + SECTOR_PAIR_SIZE]
    ]
    low = [base for base in differing if base < PROTECTED_END]
    if low and not allow_low:
        raise Refused(f"the image differs below {PROTECTED_END:#x}, at {', '.join(f'{base:#x}' for base in low)}")
    return differing


def differing_ranges(one: bytes, other: bytes) -> list[tuple[int, int]]:
    """The ranges [start, end) where two images' bytes differ."""
    ranges: list[tuple[int, int]] = []
    for at, (a, b) in enumerate(zip(one, other, strict=True)):
        if a != b:
            if ranges and ranges[-1][1] == at:
                ranges[-1] = (ranges[-1][0], at + 1)
            else:
                ranges.append((at, at + 1))
    return ranges


@dataclass
class Options:
    """allow_low: erase and program below PROTECTED_END; any_entry_state:
    go on from a reset entry whose SIM is not a fresh reset's; dry_run:
    stop after the dump, reporting the plan; identify_only: likewise
    after identify;
    restore_path: a new file to save the dump to, before any write;
    expected_restore_sha256: if given, refuse to go on unless the dump
    has it;
    program_time and erase_time: the routine's limits, seconds."""

    allow_low: bool = False
    any_entry_state: bool = False
    dry_run: bool = False
    identify_only: bool = False
    restore_path: Path | None = None
    expected_restore_sha256: str | None = None
    program_time: float = PROGRAM_TIME
    erase_time: float = ERASE_TIME


@dataclass
class Report:
    """What a run did: the restore image's SHA-256 (the flash as found),
    the sector pairs it erased and programmed, the flash's SHA-256 at
    the end, and where it still differs from the image."""

    restore_sha256: str = ""
    restore: bytes = b""
    plan: list[int] = field(default_factory=list)
    done: list[int] = field(default_factory=list)
    final_sha256: str = ""
    differing: list[tuple[int, int]] = field(default_factory=list)


class Session:
    """One reset entry's work on a target: the set-up, then calls into
    the routine.  chip_selects is the order set_up writes them in."""

    def __init__(
        self,
        target: Target,
        routine: Routine,
        log: Callable[[str], None] = print,
        options: Options | None = None,
        chip_selects: tuple[tuple[str, int, int], ...] = CHIP_SELECTS,
        write_sypcr: bool = True,
    ):
        self.target = target
        self.routine = routine
        self.log = log
        self.options = options or Options()
        self.chip_selects = chip_selects
        self.write_sypcr = write_sypcr
        self.selected = False
        self.hz = 0

    def read(self, address: int, width: int) -> int:
        return self.target.read_memory(address, width)

    def set_up(self) -> None:
        """Steps 1-4."""
        self.target.reset_into_bdm()
        simcr = self.read(SIMCR, 2)
        self.log(f"SIMCR {simcr:#06x} at entry")
        if not simcr & SIMCR_MM:
            raise Refused(f"SIMCR {simcr:#06x}: the modules are not at $FFF000")
        registers = {address: self.read(address, width) for _, address, width, _ in ENTRY_STATE}
        self.log("at entry " + ", ".join(f"{name} {registers[address]:#0{2 + 2 * width}x}" for name, address, width, _ in ENTRY_STATE))
        if not self.options.any_entry_state:
            for name, address, width, expected in ENTRY_STATE:
                if registers[address] != expected:
                    raise Refused(f"{name} reads {registers[address]:#x} at entry, not a fresh reset's {expected:#x}")
        for name in SELECTS[1:]:
            number = int(name[2:])
            registers[csbar(number)] = self.read(csbar(number), 2)
            registers[csor(number)] = self.read(csor(number), 2)
        if self.write_sypcr:
            self.target.write_memory(SYPCR, 1, SYPCR_VALUE)
            sypcr = self.read(SYPCR, 1)
            if sypcr != SYPCR_VALUE:
                raise Refused(f"SYPCR reads {sypcr:#04x} after writing {SYPCR_VALUE:#04x}")
        before = hazards(registers)
        for name, address, value in self.chip_selects:
            registers[address] = value
            after = hazards(registers)
            added = after - before
            if added:
                raise Refused(f"writing {name} {value:#06x} adds {sorted((h.value, sorted(s)) for h, s in added)}")
            self.target.write_memory(address, 2, value)
            back = self.read(address, 2)
            if back != value:
                raise Refused(f"{name} reads {back:#06x} after writing {value:#06x}")
            before = after
        if before:
            raise Refused(f"the chip selects leave {sorted((h.value, sorted(s)) for h, s in before)}")
        self.selected = True
        self.hz = system_hz(self.read(SYNCR, 2))
        self.log(f"system clock {self.hz} Hz")
        code = self.routine.code
        table = b"".join((ROUTINE + self.routine.labels["exception"]).to_bytes(VECTOR_SIZE, "big") for _ in range(VECTORS))
        for address, data in ((VECTOR_TABLE, table), (ROUTINE, code + bytes(len(code) % WORD))):
            self.target.write_block(address, data)
            if self.target.read_block(address, len(data)) != data:
                raise Refused(f"RAM at {address:#x} does not read back")

    def limit(self, seconds: float) -> int:
        return math.ceil(seconds * self.hz / POLL_CLOCKS_MIN)

    def call(self, operation: int, address: int, count: int, floor: int, polls: int, steps: int) -> Result:
        """One call: operation at address over count words of BUFFER,
        polls the poll limit; steps the most polls and passes it can
        take, for the host's own timeout."""
        target = self.target
        for name, value in (
            ("D0", operation),
            ("A0", address),
            ("A1", BUFFER),
            ("D1", count),
            ("D2", floor),
            ("D3", polls),
            ("SR", SR_SUPERVISOR_MASKED),
            ("VBR", VECTOR_TABLE),
            ("A7", STACK_TOP),
            ("PC", ROUTINE + self.routine.labels["entry"]),
        ):
            target.write_register(name, value)
            if target.read_register(name) != value:
                raise Failed(f"{name} does not read back {value:#x}")
        target.go()
        timeout = steps * STEP_CLOCKS_MAX / self.hz + WAIT_MARGIN
        entry = target.wait_for_bdm(timeout)
        pc = target.read_register("PC")
        if entry != Entry.BACKGROUND or pc != ROUTINE + self.routine.labels["halt"] + BGND_SIZE:
            raise Failed(f"background mode entered by {entry.value} with PC {pc:#x}, not after the routine's BGND")
        result = Result(
            target.read_register("D0"),
            target.read_register("A0"),
            target.read_register("D1") & WORD_MASK,
            target.read_register("D2") & WORD_MASK,
        )
        if result.status == self.routine.STATUS_EXCEPTION:
            raise Failed(
                f"exception, vector {(target.read_register('D1') & VECTOR_OFFSET_MASK) // VECTOR_SIZE}, PC {result.address:#x},"
                f" fault address {target.read_register('D2'):#x}"
            )
        if result.status != self.routine.STATUS_OK:
            failure = Busy if result.status == self.routine.STATUS_TIMEOUT else Failed
            raise failure(
                f"{self.routine.status_name(result.status)} at {result.address:#x}:"
                f" read {result.observed:#06x}, expected {result.expected:#06x}"
            )
        return result

    def identify(self) -> dict[int, int]:
        """Step 5: both pairs' codes checked; each sector pair's
        protection word, by its base."""
        protection = {}
        for base in (self.routine.PAIR_1_BASE, self.routine.PAIR_2_BASE):
            self.call(self.routine.OPERATION_IDENTIFY, base, 0, 0, 1, IDENTITY_WORDS)
            words = self.target.read_block(BUFFER, IDENTITY_WORDS * WORD)
            codes = [int.from_bytes(words[at : at + WORD], "big") for at in range(0, len(words), WORD)]
            self.log(f"pair at {base:#x}: manufacturer {codes[0]:#06x}, device {codes[1]:#06x}, protection {codes[2:]}")
            if codes[:2] != [AMD_BOTH, AM29F010B_BOTH]:
                raise Refused(f"pair at {base:#x} is not two AMD Am29F010s: {codes[0]:#06x} {codes[1]:#06x}")
            for index, word in enumerate(codes[2:]):
                protection[base + index * SECTOR_PAIR_SIZE] = word
        return protection

    def write_pair(self, base: int, data: bytes, floor: int) -> None:
        """Step 7 for one sector pair: erase, program, verify."""
        routine = self.routine
        words = len(data) // WORD
        polls = self.limit(self.options.erase_time)
        self.call(routine.OPERATION_ERASE, base, 0, floor, polls, polls + words * PASSES_PER_WORD)
        self.log(f"sector pair {base:#07x} erased")
        self.target.write_block(BUFFER, data)
        if self.target.read_block(BUFFER, len(data)) != data:
            raise Failed(f"the buffer does not read back for {base:#x}")
        polls = self.limit(self.options.program_time)
        self.call(routine.OPERATION_PROGRAM, base, words, floor, polls, words * (polls + PASSES_PER_WORD))
        self.log(f"sector pair {base:#07x} programmed")
        self.call(routine.OPERATION_VERIFY, base, words, floor, 1, words * PASSES_PER_WORD)
        self.log(f"sector pair {base:#07x} verified")

    def reset_flash(self) -> None:
        """Both pairs to reading array data, through the write selects."""
        for base in (self.routine.PAIR_1_BASE, self.routine.PAIR_2_BASE):
            self.target.write_memory(base, WORD, self.routine.READ_RESET)


def flash(target: Target, image: bytes, options: Options | None = None, routine: Routine | None = None,
          log: Callable[[str], None] = print, session: Session | None = None) -> Report:
    """The whole procedure (this module's docstring); a Report, or
    Refused or Failed.  session: one made to differ from the default,
    for a test."""
    options = options or Options()
    routine = routine or Routine.load()
    check_image(image)
    for line in lane_sum_report(image):
        log(line)
    report = Report()
    session = session or Session(target, routine, log, options)
    floor = 0 if options.allow_low else PROTECTED_END
    try:
        session.set_up()
        # a dry run sends the flash no command, not even autoselect
        protection = {} if options.dry_run else session.identify()
        report.restore = target.read_block(0, FLASH_SIZE)
        report.restore_sha256 = hashlib.sha256(report.restore).hexdigest()
        log(f"restore image SHA-256 {report.restore_sha256}")
        if options.restore_path is not None:
            try:
                with options.restore_path.open("xb") as restore:
                    restore.write(report.restore)
            except OSError as error:
                raise Refused(f"cannot save the restore image: {error}") from error
            log(f"restore image saved to {options.restore_path}")
        expected = options.expected_restore_sha256
        if expected is not None and report.restore_sha256 != expected.lower():
            raise Refused(f"the flash's SHA-256 is {report.restore_sha256}, not the expected {expected}")
        report.plan = plan(report.restore, image, options.allow_low)
        log(f"sector pairs to write: {', '.join(f'{base:#x}' for base in report.plan) or 'none'}")
        protected = [base for base in report.plan if protection.get(base, UNPROTECTED_BOTH) != UNPROTECTED_BOTH]
        if options.dry_run or options.identify_only:
            if protected:
                log(f"sector pairs to write that are protected: {', '.join(f'{base:#x}' for base in protected)}")
            return report
        if protected:
            raise Refused(f"sector pairs to write are protected: {', '.join(f'{base:#x}' for base in protected)}")
        for base in report.plan:
            session.write_pair(base, image[base : base + SECTOR_PAIR_SIZE], floor)
            report.done.append(base)
            log(f"sector pair {base:#x} written")
        final = target.read_block(0, FLASH_SIZE)
        report.final_sha256 = hashlib.sha256(final).hexdigest()
        log(f"flash SHA-256 {report.final_sha256}")
        report.differing = differing_ranges(final, image)
        if report.differing:
            raise Failed(f"the flash differs from the image at {', '.join(f'{a:#x}-{b:#x}' for a, b in report.differing)}")
        log(f"the flash is the image, SHA-256 {hashlib.sha256(image).hexdigest()}")
        return report
    except (TargetError, Failed, Refused) as error:
        if isinstance(error, TargetTimeout):
            # the routine still runs: the host touches nothing more
            log("the routine did not return: reset the target before anything else")
        elif session.selected:
            try:
                session.reset_flash()
            except TargetError as reset_error:
                log(f"read/reset after the failure failed too: {reset_error}")
        if isinstance(error, (Busy, TargetTimeout)):
            log("a part may still be erasing or programming: poll it before trusting a read")
        if isinstance(error, TargetError):
            raise Failed(f"target: {error}") from error
        raise


# What a bench run ends in, said last: the host never resumes the CPU.
STOPPED = "the CPU is stopped in background mode; the firmware has not run, and this tool does not run it"
UNKNOWN_STATE = "the target failed: the CPU may be running; reset the unit into BDM before anything else"


def arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    modes = parser.add_mutually_exclusive_group(required=True)
    modes.add_argument("--plan", nargs=2, metavar=("CURRENT", "IMAGE"), help="offline: the sector pairs a run would write")
    modes.add_argument("--dry-run", action="store_true", help="set-up, dump and plan: no flash command, no code run")
    modes.add_argument("--identify", action="store_true", help="the dry run and the routine's identify")
    modes.add_argument("--write", action="store_true", help="the whole procedure")
    targets = parser.add_mutually_exclusive_group()
    targets.add_argument("--pod", metavar="POD", help="the pod, as mc683xx_bdm.Pod names it: usb, usb:SERIAL")
    targets.add_argument("--model", metavar="FLASH", type=Path, help="the modeled board, its flash holding FLASH")
    parser.add_argument("--image", type=Path, help="the image to program")
    parser.add_argument("--expect-sha256", metavar="SHA256", help="the SHA-256 the unit's flash must have now")
    parser.add_argument("--restore-to", metavar="FILE", type=Path, help="a new file to save the unit's flash to")
    parser.add_argument("--allow-low", action="store_true", help=f"allow writing below {PROTECTED_END:#x}")
    parser.add_argument(
        "--any-entry-state",
        action="store_true",
        help="go on from a reset entry whose SIM is not a fresh reset's, reporting what it read",
    )
    parsed = parser.parse_args()
    bench_options = (
        ("--pod or --model", parsed.pod or parsed.model),
        ("--image", parsed.image),
        ("--expect-sha256", parsed.expect_sha256),
        ("--restore-to", parsed.restore_to),
        ("--any-entry-state", parsed.any_entry_state),
    )
    if parsed.plan:
        given = [name for name, value in bench_options if value]
        if given:
            parser.error(f"--plan touches no target, so takes no {', '.join(given)}")
    else:
        for name, value in bench_options[:2]:
            if not value:
                parser.error(f"{name} is required")
    if parsed.write and not parsed.restore_to:
        parser.error("--write requires --restore-to")
    return parsed


def bench(parsed: argparse.Namespace, image: bytes, routine: Routine) -> None:
    """A run on the target parsed names, ending in what it left."""
    options = Options(
        allow_low=parsed.allow_low,
        any_entry_state=parsed.any_entry_state,
        dry_run=parsed.dry_run,
        identify_only=parsed.identify,
        restore_path=parsed.restore_to,
        expected_restore_sha256=parsed.expect_sha256,
    )
    if parsed.model:
        from am29f010 import BoardFlash
        from model_target import ModelTarget, timing

        opened = contextlib.nullcontext(ModelTarget(BoardFlash.from_image(parsed.model.read_bytes(), timing())))
    else:
        opened = PodTarget.open(parsed.pod)
    try:
        with opened as target:
            report = flash(target, image, options, routine)
    except Failed as error:
        print(f"FAILED: {error}")
        raise SystemExit(UNKNOWN_STATE if isinstance(error.__cause__, TargetError) else STOPPED) from error
    except TargetError as error:
        # flash() makes its own TargetErrors Failed: this is the pod's
        # opening or closing
        raise SystemExit(f"pod: {error}") from error
    except Refused as error:
        print(f"refused: {error}")
        raise SystemExit(STOPPED) from error
    if parsed.write:
        print(f"success: the flash is the image, SHA-256 {report.final_sha256}")
    print(STOPPED)


def main() -> None:
    parsed = arguments()
    paths = parsed.plan or [parsed.image]
    try:
        *current, image = (Path(path).read_bytes() for path in paths)
        check_image(image)
        if parsed.plan:
            check_image(current[0])
            print("\n".join(lane_sum_report(image)))
            pairs = plan(current[0], image, parsed.allow_low)
            print("\n".join(f"{base:#07x}" for base in pairs) or "nothing to write")
            return
        if parsed.restore_to and parsed.restore_to.exists():
            raise Refused(f"{parsed.restore_to} exists")
        try:
            routine = Routine.load()
        except OSError as error:
            raise Refused(f"no routine, which make bdm-routine builds: {error}") from error
    except (Refused, OSError) as error:
        raise SystemExit(f"refused: {error}") from error
    bench(parsed, image, routine)


if __name__ == "__main__":
    main()
