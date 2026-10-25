# /// script
# requires-python = ">=3.11"
# dependencies = ["unicorn==2.1.4"]
# ///
"""Runs the disciplining loop's own code, each image's startup_pll and
pll_normal, in Unicorn on synthetic readings, and writes what it does
second by second to crates/smartclock/tests/fixtures/loop/IMAGE.tsv,
which crates/smartclock/tests/loop_firmware_trace.rs replays against
the update law docs/firmware/loop.md states; or, with --check, fails
if a fixture is not what the images give.

    uv run -q tools/firmware/loop_trace.py [--check]

The images are the Z3805A 3543B's and the 58503A 3704's.  Each runs as
its ROM is but for the routines the two stages call that reach outside
the loop's arithmetic, which are overwritten with a few instructions of
this script's (STUBS): the interval reading, the oscillator current,
the uptime and the GPS time return what a row gives, from mailboxes in
RAM; the clock check and the leap acknowledgment report nothing to do;
the debug output and the status events do nothing.  The soft-float
routines, efc_clamp and efc_gain_set run as the image has them.  A
call into any other routine, a trap or an access outside ROM and RAM
fails the run.

The globals' addresses are the exported C's (.set lines of
IMAGE/decompiled/IMAGE.c), the routines' IMAGE/names.tsv's, and the
structures' offsets IMAGE/smartclock.h's as m68k gcc lays them out
(M68K_CC, by default m68k-linux-gnu-gcc).  The gain is the constant
pllp_task passes efc_gain_set, the time constant the ROM's default
(TAU_BLOCKS).  Floats are written as their IEEE single bits, in hex.
"""

import argparse
import os
import pathlib
import re
import struct
import subprocess
import sys
import tempfile
from dataclasses import dataclass, field

from unicorn import UC_ARCH_M68K, UC_HOOK_INTR, UC_HOOK_MEM_INVALID, UC_MODE_BIG_ENDIAN, Uc, UcError
from unicorn.m68k_const import (
    UC_CPU_M68K_M68020,
    UC_M68K_REG_A7,
    UC_M68K_REG_D0,
    UC_M68K_REG_PC,
    UC_M68K_REG_SR,
)

ROOT = pathlib.Path(__file__).resolve().parents[2]
FIRMWARE = ROOT / 'third_party/firmware'
FIXTURES = ROOT / 'crates/smartclock/tests/fixtures/loop'

IMAGES = ('z3805a-3543b', '58503a-3704')

# Where each image's ROM holds the 25-byte block of defaults the τ cell
# is copied from, and τ's offset in it (docs/firmware/loop.md, "τ and
# G" and "In every image").
TAU_BLOCKS = {'z3805a-3543b': 0x3094C, '58503a-3704': 0x30840}
TAU_OFFSET = 0x14

ROM_SIZE = 0x80000
RAM = 0x100000
RAM_SIZE = 0x100000
# The stack a call starts on, the mailboxes the stubs read, and the
# return address a call stops at: none of them the image's.
STACK = 0x1FF000
MAILBOX = 0x1FF100
RETURN = ROM_SIZE - 0x10
SUPERVISOR = 0x2700
# Instructions a call may run before it is taken to be lost.
CALL_LIMIT = 2_000_000

# The mailboxes, longs from MAILBOX.
READING, VALID, UPTIME, CURRENT = (MAILBOX + 4 * index for index in range(4))

# The stubs' code.  moveq #0,d0; rts and moveq #1,d0; rts.
RETURN_0 = bytes.fromhex('70004e75')
RETURN_1 = bytes.fromhex('70014e75')


def load(mailbox: int) -> bytes:
    """move.l (mailbox).l,d0; rts."""
    return bytes.fromhex('2039') + struct.pack('>I', mailbox) + bytes.fromhex('4e75')


def reading() -> bytes:
    """interval_next_reading(arm_mode, *reading): the byte at 4(sp), the
    pointer at 6(sp) (the callers push clr.l, clr.b or pea, clr.b).
    move.l 6(sp),d1; beq.s +8; movea.l d1,a0; move.l (READING).l,(a0);
    moveq #0,d0; move.b (VALID+3).l,d0; rts."""
    return (
        bytes.fromhex('222f00066708204120b9')
        + struct.pack('>I', READING)
        + bytes.fromhex('70001039')
        + struct.pack('>I', VALID + 3)
        + bytes.fromhex('4e75')
    )


# Each routine the stages call that is stood in for, and its stand-in.
# The clock check's 1 and the acknowledgment's 1 both mean nothing to do.
STUBS = {
    'interval_next_reading': reading(),
    'adc_average': load(CURRENT),
    'get_uptime': load(UPTIME),
    'get_gps_time': load(UPTIME),
    'get_date_time': RETURN_0,
    'debug_sprintf': RETURN_0,
    'task_dmessage': RETURN_0,
    'status_happening': RETURN_0,
    'leap_ack_pending': RETURN_1,
    'clock_check_against_gps': RETURN_1,
    'clock_check_against_gps_58503a': RETURN_1,
}

# The routines that run as the image has them: the two stages, what
# they call for arithmetic and the EFC's clamp, and the gains' setter.
RUN = re.compile(
    r'(startup_pll|pll_normal|efc_clamp|efc_gain_set|float_\w+|double_\w+|unsigned_to_float|integer_to_float)$'
)

# The offsets the trace needs, by structure and field.
FIELDS = {
    'pll_state': (
        'stage',
        'startup_state',
        'normal_state',
        'hold_causes',
        'efc',
        'aging_state',
        'efc_history.start_ptr',
        'efc_history.stop_ptr',
        'efc_history.fit_b',
        'efc_history.fit_c',
        'ti_filtered',
        'efc_base',
        'loop_time_constant',
    ),
    'loop_steering_state': (
        'integral',
        'ti_sum',
        'large_offset_blocks',
        'seconds',
        'readings',
        'proportional_gain',
        'integral_gain',
        'filter_weight',
    ),
    'efc_gain_values': ('gain', 'reciprocal_ti', 'reciprocal_tau', 'step_limit'),
    'nv_settings_fields': ('holdover_limit_threshold', 'holdover_limit_state'),
}

GLOBALS = (
    'pll',
    'loop_steering',
    'efc_gains',
    'nv_settings',
    'max_loop_time',
    'ti_setpoint',
    'interval_mean',
    'scpi_value_DIAGnostic_ROSCillator_TCOefficient',
    'gps_reference_good',
    'internal_pps_dead',
    'gps_pps_dead',
    'debug_enabled',
    'pll_debug_enabled',
)
# The 58503A 3704's alone (docs/firmware/loop.md, "The update in the
# Z3805A 3543B").
OPTIONAL_GLOBALS = ('gps_reference_questionable',)

# The values of smartclock.h's enumerations the trace sets.
PLL_AGING_COLLECTING = 3
PLL_STAGE_RESULT_DONE = 1
HOLDOVER_LIMIT_ON = 0


def names(image: str) -> dict[str, int]:
    """The primary's functions by name, from names.tsv."""
    found = {}
    for line in (FIRMWARE / image / 'names.tsv').read_text().splitlines():
        cells = line.split('\t')
        if line.startswith('#') or len(cells) < 3 or cells[1] != 'function' or '::' in cells[0]:
            continue
        found.setdefault(cells[2], int(cells[0], 16))
    return found


def globals_of(image: str) -> dict[str, int]:
    """Every global's address, from the exported C's .set lines."""
    text = (FIRMWARE / image / 'decompiled' / f'{image}.c').read_text()
    return {name: int(address, 16) for name, address in re.findall(r'\.set (\w+), (0x[0-9a-f]+)"', text)}


def layout(image: str) -> dict[str, tuple[int, int]]:
    """Each FIELDS entry's offset and size, as m68k gcc lays out
    smartclock.h."""
    lines = ['#include <stddef.h>', f'#include "{FIRMWARE / image / "smartclock.h"}"']
    keys = []
    for structure, members in FIELDS.items():
        for member in members:
            keys.append(f'{structure}.{member}')
            lines.append(f'const unsigned long offset_{len(keys)} = offsetof({structure}, {member});')
            lines.append(f'const unsigned long size_{len(keys)} = sizeof((({structure} *)0)->{member});')
    with tempfile.TemporaryDirectory() as folder:
        source = os.path.join(folder, 'layout.c')
        pathlib.Path(source).write_text('\n'.join(lines) + '\n')
        compiler = os.environ.get('M68K_CC', 'm68k-linux-gnu-gcc')
        assembly = subprocess.run(
            [compiler, '-mcpu=cpu32', '-S', '-o', '-', source], check=True, capture_output=True, text=True
        ).stdout
    # gcc writes a zero as .zero 4, the long's bytes.
    values = {
        (kind, index): 0 if directive == 'zero' else int(value)
        for kind, index, directive, value in re.findall(
            r'^(offset|size)_(\d+):\s*\.(long|zero)\s+(\d+)', assembly, re.MULTILINE
        )
    }
    return {key: (values['offset', str(index + 1)], values['size', str(index + 1)]) for index, key in enumerate(keys)}


def float_bits(value: float) -> int:
    return struct.unpack('>I', struct.pack('>f', value))[0]


def bits_float(bits: int) -> float:
    return struct.unpack('>f', struct.pack('>I', bits))[0]


def pushed_gain(image: bytes, task: int, setter: int) -> int:
    """The gain pllp_task passes efc_gain_set: move.l #G,-(sp) and then
    jsr (d16,pc) to it, the one such pair in the task."""
    found = []
    for at in range(task, task + 0x400, 2):
        if image[at : at + 2] == b'\x2f\x3c' and image[at + 6 : at + 8] == b'\x4e\xba':
            target = at + 8 + struct.unpack('>h', image[at + 8 : at + 10])[0]
            if target == setter:
                found.append(struct.unpack('>I', image[at + 2 : at + 6])[0])
    if len(found) != 1:
        sys.exit(f'pllp_task at {task:#x} passes efc_gain_set {len(found)} constants')
    return found[0]


@dataclass
class Machine:
    """One image in Unicorn, its stubs in place."""

    image: str
    rom: bytes
    routines: dict[str, int]
    addresses: dict[str, int]
    layout: dict[str, tuple[int, int]]
    uc: Uc = field(init=False)
    fault: str | None = field(init=False, default=None)

    def __post_init__(self) -> None:
        patched = bytearray(self.rom)
        for name, code in STUBS.items():
            if name in self.routines:
                at = self.routines[name]
                patched[at : at + len(code)] = code
        self.uc = Uc(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN)
        self.uc.ctl_set_cpu_model(UC_CPU_M68K_M68020)
        self.uc.mem_map(0, ROM_SIZE)
        self.uc.mem_write(0, bytes(patched))
        self.uc.mem_map(RAM, RAM_SIZE)
        self.uc.hook_add(UC_HOOK_MEM_INVALID, self.invalid)
        self.uc.hook_add(UC_HOOK_INTR, self.trap)
        self.known = sorted(
            (address, name) for name, address in self.routines.items() if RUN.match(name) or name in STUBS
        )

    def invalid(self, uc: Uc, access: int, address: int, size: int, value: int, data: object) -> bool:
        pc = uc.reg_read(UC_M68K_REG_PC)
        self.fault = f'{self.image}: access {access} of {size} bytes at {address:#x} from {pc:#x}'
        return False

    def trap(self, uc: Uc, number: int, data: object) -> None:
        pc = uc.reg_read(UC_M68K_REG_PC)
        self.fault = f'{self.image}: exception {number} at {pc:#x}'
        uc.emu_stop()

    def field(self, structure: str, member: str) -> int:
        """A field's address."""
        variable = {
            'pll_state': 'pll',
            'loop_steering_state': 'loop_steering',
            'efc_gain_values': 'efc_gains',
            'nv_settings_fields': 'nv_settings',
        }[structure]
        return self.addresses[variable] + self.layout[f'{structure}.{member}'][0]

    def write(self, at: int, fmt: str, value: float | int) -> None:
        self.uc.mem_write(at, struct.pack('>' + fmt, value))

    def read(self, at: int, fmt: str) -> float | int:
        return struct.unpack('>' + fmt, self.uc.mem_read(at, struct.calcsize(fmt)))[0]

    def call(self, routine: str, argument: bytes) -> int:
        """Calls a routine with its stack arguments' bytes; its D0."""
        sp = STACK - 4 - len(argument)
        self.uc.mem_write(sp, struct.pack('>I', RETURN) + argument)
        # SR first: setting S swaps in the supervisor's stack pointer.
        self.uc.reg_write(UC_M68K_REG_SR, SUPERVISOR)
        self.uc.reg_write(UC_M68K_REG_A7, sp)
        self.uc.reg_write(UC_M68K_REG_D0, 0)
        self.fault = None
        try:
            self.uc.emu_start(self.routines[routine], RETURN, count=CALL_LIMIT)
        except UcError as error:
            sys.exit(f'{self.fault or self.image}: {error}')
        if self.fault is not None:
            sys.exit(self.fault)
        if self.uc.reg_read(UC_M68K_REG_PC) != RETURN:
            sys.exit(f'{self.image}: {routine} did not return')
        return self.uc.reg_read(UC_M68K_REG_D0)


def check_callees(machine: Machine) -> None:
    """Fails unless every jsr or bsr in the two stages and what they run
    lands on a routine that runs or is stood in for."""
    starts = sorted(machine.routines.values())
    allowed = {address for address, _ in machine.known}
    for name in ('startup_pll', 'pll_normal', 'efc_clamp', 'efc_gain_set'):
        start = machine.routines[name]
        end = next(address for address in starts if address > start)
        rom = machine.rom
        for at in range(start, end - 5, 2):
            word = rom[at : at + 2]
            if word == b'\x4e\xb9':
                target = struct.unpack('>I', rom[at + 2 : at + 6])[0]
            elif word == b'\x4e\xba' or (word[0] == 0x61 and word[1] == 0):
                target = at + 2 + struct.unpack('>h', rom[at + 2 : at + 4])[0]
            else:
                continue
            # A pair of bytes that only looks like a call lands
            # mid-routine; the routines either run or are stubbed.
            if target in machine.routines.values() and target not in allowed:
                callee = next(n for n, a in machine.routines.items() if a == target)
                sys.exit(f'{machine.image}: {name} calls {callee} at {at:#x}, neither run nor stood in for')


@dataclass
class Second:
    """One call of a stage, its inputs: the stage, its argument, the
    second's reading and its flag, the uptime, the current, the GPS
    reference's flags, the aging fit's b and c, whether its sampler is
    collecting and whether the history holds its first entries."""

    stage: str
    same_stage: int
    valid: int
    reading: float
    uptime: int
    current: float
    reference_good: int = 1
    questionable: int = 0
    fit_b: float = 0.0
    fit_c: float = 0.0
    collecting: int = 1
    history_ready: int = 0


class Random:
    """A 32-bit linear congruential generator, so the fixture is the same
    on every run (Numerical Recipes' constants)."""

    def __init__(self, seed: int) -> None:
        self.state = seed

    def uniform(self) -> float:
        self.state = (1664525 * self.state + 1013904223) & 0xFFFFFFFF
        return self.state / 2**32 - 0.5


# The scenarios' fixed inputs.
SETPOINT = 20e-9
TEMPERATURE_COEFFICIENT = 2.5
THRESHOLD = 1e-6
FIRST_EFC = 520000.0
FIRST_UPTIME = 50000
FIRST_TIME_CONSTANT = 150.0


def normal_seconds(random: Random) -> list[Second]:
    """pll_normal from its entry: 400 s of readings within 300 ns, some
    failed, some past the threshold, a block with none valid, a block
    with the reference questionable, and fit slopes that pass the clamp
    either way."""
    seconds = [Second('pll_normal', 0, 0, 0.0, FIRST_UPTIME, 1.0)]
    for second in range(1, 401):
        reading = 3e-7 * random.uniform() + 1e-7
        valid = 0 if second % 37 == 0 or 200 < second <= 210 else 1
        if second % 53 == 0:
            reading = 2 * THRESHOLD
        fit_b = 5000.0 if 250 < second <= 290 else -5000.0 if 330 < second <= 360 else 3.0
        seconds.append(
            Second(
                'pll_normal',
                1,
                valid,
                reading,
                FIRST_UPTIME + second,
                1.0 + 0.2 * random.uniform(),
                questionable=1 if 300 < second <= 310 or second in (315, 317) else 0,
                fit_b=fit_b,
                fit_c=50.0,
            )
        )
    return seconds


def startup_seconds(random: Random, collecting: bool) -> list[Second]:
    """startup_pll from its entry: a block past 150 ns early, then
    readings within 100 ns until the ramp ends, then pll_normal from its
    entry for 100 s.  Without the sampler collecting, the history's
    first three entries arrive at 400 s."""
    seconds = [Second('startup_pll', 0, 0, 0.0, FIRST_UPTIME, 1.0)]
    for second in range(1, 2400):
        reading = 4e-7 if 20 < second <= 30 else 1e-7 * random.uniform() + 3e-8
        seconds.append(
            Second(
                'startup_pll',
                1,
                0 if second % 41 == 0 else 1,
                reading,
                FIRST_UPTIME + second,
                1.0 + 0.2 * random.uniform(),
                fit_b=3.0,
                fit_c=50.0,
                collecting=int(collecting),
                history_ready=0 if collecting or second < 400 else 1,
            )
        )
    return seconds


# What each row records after its call: the stage's result, then each
# value as a column, and how to read it.
OUTPUTS = (
    ('ti_filtered', 'pll_state', 'f'),
    ('integral', 'loop_steering_state', 'f'),
    ('efc_base', 'pll_state', 'f'),
    ('efc', 'pll_state', 'f'),
    ('ti_sum', 'loop_steering_state', 'f'),
    ('readings', 'loop_steering_state', 'B'),
    ('seconds', 'loop_steering_state', 'B'),
    ('loop_time_constant', 'pll_state', 'f'),
    ('startup_state', 'pll_state', 'B'),
    ('large_offset_blocks', 'loop_steering_state', 'i'),
    ('proportional_gain', 'loop_steering_state', 'f'),
    ('integral_gain', 'loop_steering_state', 'f'),
    ('filter_weight', 'loop_steering_state', 'f'),
)

INPUTS = (
    'stage',
    'same_stage',
    'valid',
    'reading',
    'uptime',
    'current',
    'reference_good',
    'questionable',
    'fit_b',
    'fit_c',
    'collecting',
    'history_ready',
)


def cell(value: float | int | str, fmt: str) -> str:
    if fmt == 'f':
        return f'{float_bits(value):08x}'
    return str(value)


def fresh(machine: Machine) -> dict[str, str]:
    """RAM cleared and set for a scenario; the constants it set."""
    uc = machine.uc
    uc.mem_write(RAM, bytes(RAM_SIZE))
    gain = pushed_gain(machine.rom, machine.routines['pllp_task'], machine.routines['efc_gain_set'])
    machine.call('efc_gain_set', struct.pack('>I', gain))
    tau = struct.unpack('>f', machine.rom[TAU_BLOCKS[machine.image] + TAU_OFFSET :][:4])[0]
    machine.write(machine.addresses['max_loop_time'], 'f', tau)
    machine.write(machine.addresses['ti_setpoint'], 'f', SETPOINT)
    machine.write(machine.addresses['scpi_value_DIAGnostic_ROSCillator_TCOefficient'], 'f', TEMPERATURE_COEFFICIENT)
    machine.write(machine.field('nv_settings_fields', 'holdover_limit_threshold'), 'f', THRESHOLD)
    machine.write(machine.field('nv_settings_fields', 'holdover_limit_state'), 'B', HOLDOVER_LIMIT_ON)
    machine.write(machine.field('pll_state', 'efc'), 'f', FIRST_EFC)
    machine.write(machine.field('pll_state', 'loop_time_constant'), 'f', FIRST_TIME_CONSTANT)
    gains = {
        name: f'{machine.read(machine.field("efc_gain_values", name), "I"):08x}'
        for name in ('gain', 'reciprocal_ti', 'reciprocal_tau', 'step_limit')
    }
    return {
        **gains,
        'max_loop_time': f'{float_bits(tau):08x}',
        'ti_setpoint': f'{float_bits(SETPOINT):08x}',
        'tc_coefficient': f'{float_bits(TEMPERATURE_COEFFICIENT):08x}',
        'threshold': f'{float_bits(THRESHOLD):08x}',
        'first_efc': f'{float_bits(FIRST_EFC):08x}',
        'first_time_constant': f'{float_bits(FIRST_TIME_CONSTANT):08x}',
    }


def run(machine: Machine, name: str, seconds: list[Second], stop_when_done: bool) -> list[str]:
    """A scenario's rows: its constants as comments, then one row a call.
    With stop_when_done, startup_pll's first DONE hands over to
    pll_normal from its entry for 100 s."""
    constants = fresh(machine)
    lines = [f'# {name} {key} {value}' for key, value in constants.items()]
    handed_over = None
    for index, second in enumerate(seconds):
        if handed_over is not None:
            if index - handed_over > 100:
                break
            second.stage = 'pll_normal'
            second.same_stage = 0 if index == handed_over else 1
        machine.write(READING, 'f', second.reading)
        machine.write(VALID, 'I', second.valid)
        machine.write(UPTIME, 'I', second.uptime)
        machine.write(CURRENT, 'f', second.current)
        machine.write(machine.addresses['gps_reference_good'], 'B', second.reference_good)
        if 'gps_reference_questionable' in machine.addresses:
            machine.write(machine.addresses['gps_reference_questionable'], 'B', second.questionable)
        machine.write(machine.field('pll_state', 'efc_history.fit_b'), 'f', second.fit_b)
        machine.write(machine.field('pll_state', 'efc_history.fit_c'), 'f', second.fit_c)
        machine.write(machine.field('pll_state', 'aging_state'), 'B', PLL_AGING_COLLECTING if second.collecting else 0)
        if second.history_ready:
            machine.write(machine.field('pll_state', 'efc_history.stop_ptr'), 'B', 3)
            machine.write(machine.field('pll_state', 'efc_history.start_ptr'), 'B', 0)
        result = machine.call(second.stage, bytes([second.same_stage, 0])) & 0xFF
        # The causes cleared, so that a row's result is its own call's.
        size = machine.layout['pll_state.hold_causes'][1]
        machine.uc.mem_write(machine.field('pll_state', 'hold_causes'), bytes(size))
        row = [name] + [
            cell(getattr(second, key), 'f' if key in ('reading', 'current', 'fit_b', 'fit_c') else 'd')
            for key in INPUTS
        ]
        row.append(str(result))
        for member, structure, fmt in OUTPUTS:
            row.append(cell(machine.read(machine.field(structure, member), fmt), fmt))
        lines.append('\t'.join(row))
        if stop_when_done and second.stage == 'startup_pll' and result == PLL_STAGE_RESULT_DONE and handed_over is None:
            handed_over = index + 1
    return lines


def trace(image: str) -> str:
    rom = (FIRMWARE / f'{image}.bin').read_bytes()
    routines = names(image)
    addresses = globals_of(image)
    missing = [name for name in GLOBALS if name not in addresses]
    if missing:
        sys.exit(f'{image}: no address for {missing}')
    machine = Machine(image, rom, routines, addresses, layout(image))
    check_callees(machine)
    columns = ['scenario', *INPUTS, 'result', *(member for member, _, _ in OUTPUTS)]
    lines = [
        f'# {image}: startup_pll and pll_normal run in Unicorn by tools/firmware/loop_trace.py;',
        '# do not edit.  Floats are IEEE single bits in hex.',
    ]
    lines += run(machine, 'normal', normal_seconds(Random(1)), False)
    lines += run(machine, 'startup', startup_seconds(Random(2), True), True)
    lines += run(machine, 'startup_history', startup_seconds(Random(3), False)[:1200], False)
    return '\n'.join(['\t'.join(columns)] + lines) + '\n'


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--check', action='store_true', help='fail if a fixture is stale')
    arguments = parser.parse_args()
    stale = False
    FIXTURES.mkdir(parents=True, exist_ok=True)
    for image in IMAGES:
        path = FIXTURES / f'{image}.tsv'
        text = trace(image)
        if arguments.check:
            if not path.exists() or path.read_text() != text:
                print(f'{path.relative_to(ROOT)} is not what {image} gives', file=sys.stderr)
                stale = True
        else:
            path.write_text(text)
    sys.exit(1 if stale else 0)


if __name__ == '__main__':
    main()
