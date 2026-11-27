#!/usr/bin/env python3
"""Run an image's FPGA netlist on a set of cases, for the emulator's
differential tests of its models (crates/smartclock-emu/src/board/fpga/z3805a.rs
and lca.rs).

    python3 -I tools/fpga/differential.py [--image I] [--netlist V] [--binary B]
        [--built] [--jobs N] [--only NAME] [OUTPUT]
    python3 -I tools/fpga/differential.py [--image I] --stimulus NAME > NAME.stim
    python3 -I tools/fpga/differential.py [--image I] --list
    python3 -I tools/fpga/differential.py --images

Each case is a stimulus in the emulator's board time, CPU cycles of
16,777,216 Hz: a reset, register writes and reads as the firmware's
`fpga_read` and `counter_arm` make them, GPS 1 PPS edges and the
interpolator's byte.  The script turns each into picoseconds for the
image's `<image>_stimulus.v` (stimulus.vh; the Z3817A's a harness of
its own, with its P84 input and its flags), runs it in Icarus Verilog
over the netlist (--netlist, target/fpga/<image>.v by default;
docs/firmware/outputs.md, "Netlists", makes it), built into --binary
(<image>_stimulus beside the netlist) unless --built says the caller
built it, and writes the cases with what the netlist read and the
internal 1 PPS's edges to OUTPUT (standard output by default), the
image's fixture (FIXTURES).  --image is z3805a unless given.  A
simulated second takes minutes, so the cases run in parallel, --jobs at
once (one per CPU); tools/fpga/Makefile's check-differential runs each
case, --only NAME, as a target of its own, so that make's -j sets the
parallelism instead.

The fixture's lines, a case's after its `case NAME`:

    options OPTION...       the harness's options the case ran with, which
                            say what it prints (+edges, +p19); none, no line
    CYCLE reset             RESET released
    CYCLE write R V         register R written V, hex
    CYCLE read V            the port read, V hex, what the netlist gave
    CYCLE gps               the GPS 1 PPS rises (the Z3817A's P82), High
                            GPS_WIDTH_PS
    CYCLE p84 PS            the Z3817A's P84 rises, High PS ps
    CYCLE pins V            the interpolator's byte
    CYCLE watch P           pin P's changes recorded from then on
    CYCLE end
    pps TICK                the internal 1 PPS rose on clock edge TICK
    interval PS             the start's interval (the image's stimulus
                            bench names its net) was High PS ps
    edge P PS L             a watched pin P changed to L at PS ps
    flag TICK NAME V        the Z3817A's net NAME, a status bit (FI.X,
                            GJ.X) or an output (P16, P19, P72), became V
                            after clock edge TICK

Board time 0 is a rising edge of the 10 MHz clock, the netlist's at
50 ns, so that clock edge TICK is at TICK x 100 ns of board time.
"""

import argparse
import concurrent.futures
import os
import pathlib
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent.parent
OUT = ROOT / "target" / "fpga"
# The images with a stimulus bench, and the fixture each one's cases are
# written to, which the emulator's tests read.
FIXTURES = {
    "z3805a": "crates/smartclock-emu/src/board/fpga/z3805a.fixture",
    "58503a-z3801a": "crates/smartclock-emu/src/board/fpga/58503a-z3801a.fixture",
    "58503b": "crates/smartclock-emu/src/board/fpga/58503b.fixture",
    "z3815a": "crates/smartclock-emu/src/board/fpga/z3815a.fixture",
    "z3816a": "crates/smartclock-emu/src/board/fpga/z3816a.fixture",
    "z3817a": "crates/smartclock-emu/src/board/fpga/z3817a.fixture",
}
DEFAULT_IMAGE = "z3805a"
# The image whose cases are its own alone (z3817a_cases): its design has
# no interpolator byte, and counter_arm's registers are not its own.
Z3817A_IMAGE = "z3817a"
# The pins of each image's cases of its own (image_cases) watch: those
# that change at most a few times a second, over seconds, and the faster
# ones, over a few milliseconds.  The images' stimulus benches name the
# pins that may be watched.
SLOW_PINS = {
    "58503a-z3801a": [14, 72, 78],
    "58503b": [14, 72, 78],
    "z3815a": [14, 20, 21, 24, 25, 26, 28, 34],
    "z3816a": [14, 20, 28],
}
FAST_PINS = {
    "58503a-z3801a": [],
    "58503b": [19],
    "z3815a": [19, 29, 30, 51, 57],
    "z3816a": [51, 57],
}
# The phase-frequency detector's pins, P38 and P40, which compare at
# 1.6 kHz, over tens of their periods.
DETECTOR_PINS = {"z3815a": [38, 40], "z3816a": [38, 40]}

CPU_HZ = 16_777_216
PS_PER_SECOND = 10**12
# The netlist's clock rises at 50 ns and every 100 ns after; board time 0
# is that first edge.
CLOCK_PHASE_PS = 50_000
TICK_PS = 100_000
# A bus operation's span in the stimulus, cycles: stimulus.vh's write
# takes 1.25 us.
BUS_STEP = 32
# The GPS 1 PPS's width (z3805a_tb.v's 100 us).
GPS_WIDTH_PS = 100_000_000
# The start's interval's pulses shorter than this are the logic's glitches, not a start.
GLITCH_PS = 5_000
# Seconds, in cycles.
SECOND = CPU_HZ

# The registers the port's three address bits name.
REGISTERS = 8
# counter.md's register fields.
PRESET = 0x40
MODE_MASK = 0x03
START = 0x01
REFERENCE_HIGH = 0x02
ADDRESS_MASK = 0xF8
SYNC_MASK = 0x03


def to_ps(cycles):
    """A board time in cycles as the netlist's picoseconds."""
    return CLOCK_PHASE_PS + round(cycles * PS_PER_SECOND / CPU_HZ)


def to_tick(ps):
    """The clock edge a netlist output's change at `ps` follows."""
    return (ps - CLOCK_PHASE_PS) // TICK_PS


# The Z3805A's registers as counter_configuration_init loads them.
Z3805A_REGISTERS = [PRESET, REFERENCE_HIGH, 0, 0]


class Case:
    """A stimulus, built in time order: the bus operations from a cursor,
    each BUS_STEP cycles on, and GPS edges at their own times.  It
    starts with the interpolator's byte `pins`, if the design has one,
    and the registers' load, `registers` written in order."""

    def __init__(self, name, reset=16, pins=0x5A, registers=Z3805A_REGISTERS):
        self.name = name
        # the harness's options, `+edges` and the like
        self.options = []
        self.lines = [(reset, "reset")]
        self.at = reset + BUS_STEP
        self.registers = [0] * REGISTERS
        if pins is not None:
            self.set_pins(pins)
        for register, value in enumerate(registers):
            self.write(register, value)

    def bus(self, *operation):
        self.lines.append((self.at, *operation))
        self.at += BUS_STEP

    def set_pins(self, value):
        self.bus("pins", f"{value:02x}")

    def watch(self, pins):
        """`pins` watched from board time 0."""
        self.lines += [(0, "watch", str(pin)) for pin in pins]

    def write(self, register, value):
        self.registers[register] = value
        self.bus("write", str(register), f"{value:02x}")

    def field(self, register, mask, value):
        self.write(register, self.registers[register] & ~mask | value & mask)

    def read(self, address):
        """fpga_read: the address into register 1, the read, register 1
        back."""
        control = self.registers[1]
        self.bus("write", "1", f"{address << 3 | control & ~ADDRESS_MASK & 0xFF:02x}")
        self.bus("read")
        self.bus("write", "1", f"{control:02x}")

    def read_status(self):
        self.read(0x00)

    def read_all(self):
        """Every address."""
        for address in range(32):
            self.read(address)

    def read_count(self):
        for address in [0x17, 0x16, 0x15, 0x14, 0x13, 0x12, 0x11, 0x10]:
            self.read(address)

    def arm(self, sync=None):
        """counter_arm: preset on, mode 0, start clear, address 0, a sync's
        pair, preset off."""
        self.field(0, PRESET, PRESET)
        self.field(0, MODE_MASK, 0)
        self.field(1, START, 0)
        self.field(1, ADDRESS_MASK, 0)
        if sync is not None:
            self.field(3, SYNC_MASK, 0)
            self.field(3, SYNC_MASK, sync)
        self.field(0, PRESET, 0)

    def measure(self, high):
        """interpolator_prepare, then interpolator_measure, then a read of
        the status and the byte."""
        self.field(0, PRESET, PRESET)
        self.field(0, MODE_MASK, 3)
        self.field(1, START, 0)
        self.field(1, ADDRESS_MASK, 0)
        self.field(1, REFERENCE_HIGH, REFERENCE_HIGH if high else 0)
        self.field(0, PRESET, PRESET)
        self.field(1, START, START)
        self.field(0, PRESET, 0)
        self.field(1, START, 0)
        self.read_status()
        self.read(0x11)
        self.read(0x10)

    def until(self, at):
        assert at >= self.at, f"{self.name}: {at} is past {self.at}"
        self.at = at

    def gps(self, at):
        self.lines.append((at, "gps"))

    def end(self):
        self.lines.append((self.at, "end"))
        # stable: a GPS edge sorts among the bus operations by its time
        self.lines.sort(key=lambda line: line[0])
        return self


def at(seconds):
    """A board time in cycles, from seconds."""
    return round(seconds * CPU_HZ)


# How often the cases poll the status: every fifth of counter_wait_ready's
# polls, psos_pause(10), 10 of the periodic interrupt's 1024 a second,
# to keep the fixture small.
POLL = at(5 * 10 / 1024)


def each_second(offset, last):
    """`offset` seconds into every second before `last`, or the times
    `offset` lists."""
    if isinstance(offset, list):
        return offset
    return [second + offset for second in range(int(last) + 1) if second + offset < last]


def counting(name, gps, arms, sync=None, reference_high=True, mode=0, pins=0x5A, length=1.0,
             watch=(), writes=(), case=None, p84=None):
    """A case that arms the counter at each of `arms`, seconds, with the
    GPS 1 PPS `gps` seconds into every second, or at each of `gps`: it
    polls the status, at a fifth of counter_wait_ready's pace, from each
    arm to the next, or to `length` seconds after the last, then reads
    the count and the interpolator as counter_read does.  `sync` is the
    sync pair's kind for each arm, or None.  It watches the pins `watch`,
    and makes each of `writes`, (seconds, register, value), in place of
    the first poll at or after its time.  `case`, if given, is the case
    to build on, the Z3817A's, and `p84` its P84 1 PPS, as `gps`."""
    case = case or Case(name, pins=pins)
    case.watch(watch)
    writes = sorted(writes)
    if not reference_high:
        case.field(1, REFERENCE_HIGH, 0)
    last = arms[-1] + length
    for edge in each_second(gps, last):
        case.gps(at(edge))
    for edge in each_second(p84, last) if p84 is not None else []:
        case.p84(at(edge))
    syncs = sync or [None] * len(arms)
    for index, arm in enumerate(arms):
        case.until(at(arm))
        case.arm(syncs[index])
        if mode:
            case.field(0, MODE_MASK, mode)
        finish = at(arms[index + 1]) if index + 1 < len(arms) else at(last)
        while case.at + POLL < finish - 40 * BUS_STEP:
            if writes and at(writes[0][0]) <= case.at:
                _, register, value = writes.pop(0)
                case.write(register, value)
            else:
                case.read_status()
            case.until(case.at + POLL)
        case.read_status()
        case.read_count()
    return case.end()


def cases(image):
    """The cases `image`'s fixture holds: the Z3805A's, every image's but
    the Z3817A's, and the others' their own.  RESET is released about
    1 us into board time, and the internal 1 PPS's first pulse is a
    second on."""
    if image == Z3817A_IMAGE:
        return z3817a_cases()
    found = shared_cases()
    if image != DEFAULT_IMAGE:
        found += image_cases(image)
    return found


def shared_cases():
    """The cases of every image but the Z3817A's: the Z3805A's fixture,
    which the hand-written model's test also replays."""
    found = []
    # the registers' reads with the preset on, at two bytes
    case = Case("registers")
    case.read_all()
    case.set_pins(0xA5)
    case.read_all()
    found.append(case.end())
    # interpolator_measure at both references, the status and count read
    case = Case("references")
    for high in [False, True, False]:
        case.measure(high)
        case.read_count()
    case.set_pins(0x3C)
    case.measure(True)
    case.read_all()
    case.arm()
    case.read_all()
    found.append(case.end())
    # the GPS 1 PPS before and after the internal 1 PPS, both references
    found.append(counting("early", 0.75 + 37e-9, [0.5]))
    found.append(counting("early_low", 0.75 + 37e-9, [0.5], reference_high=False))
    found.append(counting("late", 0.2 + 1234e-9, [0.5], pins=0xA5))
    found.append(counting("late_low", 0.2 + 1234e-9, [0.5], reference_high=False))
    # the GPS 1 PPS after the stop: the next second's stop ends the count
    found.append(counting("after_stop", 0.45 + 77e-9, [0.5], length=2.0))
    # modes 1 and 2
    found.append(counting("mode_1", 0.75 + 37e-9, [0.5], mode=1))
    found.append(counting("mode_2", 0.2 + 1234e-9, [0.5], mode=2))
    # the sync kinds: a step left at its kind, a step asked again, and a
    # full sync left at its kind, the GPS 1 PPS moved on
    found.append(counting("sync_1", 0.75 + 37e-9, [0.5, 1.5], sync=[1, None]))
    found.append(counting("sync_2", 0.75 + 37e-9, [0.5, 1.5], sync=[2, 2]))
    found.append(counting("sync_3", [0.75 + 37e-9, 1.8 + 37e-9], [0.5, 1.3], sync=[3, None]))
    # a full sync the internal 1 PPS comes before
    found.append(counting("sync_3_late", 0.2 + 1234e-9, [0.5], sync=[3], length=2.0))
    # the first second's tap, before the first internal 1 PPS
    found.append(counting("first_second", 0.3 + 53e-9, [0.1], length=0.4))
    # the GPS 1 PPS a clock or so before the tap
    found.append(counting("near_tap", 0.4 + 433e-9, [0.5]))
    return found


class Z3817aCase(Case):
    """A case of the Z3817A's design (z3817a-trace.md): no interpolator
    byte, five registers, the preset set and register 4 `select` at
    start as z3817a_tb.v loads them, and the 3752 firmware's arm and
    count read."""

    def __init__(self, name, select=0):
        super().__init__(name, pins=None, registers=[PRESET, 0, 0, 0, select])

    def arm(self, sync=None):
        """The arm (`0x457c0`): the preset on, address 0, the preset off,
        and no sync: the design's sync is not among these cases."""
        assert sync is None, "no sync cases"
        self.field(0, PRESET, PRESET)
        self.field(1, ADDRESS_MASK, 0)
        self.field(0, PRESET, 0)

    def read_count(self):
        """`0x456fc`: the bytes at 0x18, 0x16, 0x14 and 0x12, each
        `reg + 1` then `reg` (`0x4569c`), then the P84 count at 0x01."""
        for address in [0x19, 0x18, 0x17, 0x16, 0x15, 0x14, 0x13, 0x12, 0x01]:
            self.read(address)

    def p84(self, at, width=GPS_WIDTH_PS):
        self.lines.append((at, "p84", str(width)))


# P84's pulses in the `registers` case, which counts them through the
# P84 counter's wrap: how many, their width, picoseconds, and their
# spacing, cycles.
P84_RISES = 17
P84_SHORT_PS = 2_000_000
P84_SPACING = 64
# How long the `outputs` case runs, and its P82 1 PPS, seconds: three of
# P72's pulses, a millisecond apart, and P16's copy of the 1 PPS.
OUTPUTS_LENGTH = 3.2e-3
OUTPUTS_GPS = 1.5e-3 + 37e-9
# The `p19` case's length, seconds: some of P19's 6.5 us periods.
P19_LENGTH = 50e-6


def z3817a_cases():
    """The Z3817A's fixture's cases: the read map, P82 and P84 starts,
    the P84 counter, the status's bits 2 and 3 and the outputs."""
    found = []
    # every address with the preset on, at either select; P84's rises
    # counted through the counter's wrap
    case = Z3817aCase("registers")
    case.read_all()
    case.write(4, 0x01)
    case.read_all()
    for _ in range(P84_RISES):
        case.p84(case.at, P84_SHORT_PS)
        case.until(case.at + P84_SPACING)
        case.read(0x01)
    case.write(4, 0x00)
    case.read_all()
    found.append(case.end())
    # the P82 1 PPS before and after the internal 1 PPS
    found.append(counting("early", 0.75 + 37e-9, [0.5], case=Z3817aCase("early")))
    found.append(counting("late", 0.2 + 1234e-9, [0.5], case=Z3817aCase("late")))
    # the first second's tap, and a 1 PPS a few clocks before a tap
    found.append(counting("first_second", 0.3 + 53e-9, [0.1], length=0.4,
                          case=Z3817aCase("first_second")))
    found.append(counting("near_tap", 0.4 + 433e-9, [0.5], case=Z3817aCase("near_tap")))
    # P84 selected: it starts the count, P82 does not
    found.append(counting("select_p84", 0.75 + 37e-9, [0.5], p84=0.6 + 37e-9,
                          case=Z3817aCase("select_p84", select=1)))
    # P72's pulses and P16's copy of P82
    case = Z3817aCase("outputs")
    case.options.append("+edges")
    case.gps(at(OUTPUTS_GPS))
    case.until(at(OUTPUTS_LENGTH))
    found.append(case.end())
    # P19, GCLK / 65
    case = Z3817aCase("p19")
    case.options.append("+p19")
    case.until(at(P19_LENGTH))
    found.append(case.end())
    return found


# What image_cases writes to every register in turn, then reads back.
REGISTER_PATTERNS = [0xFF, 0x00, 0xA5, 0x5A]

def watching(name, pins, length):
    """A case that watches `pins` from board time 0, arms the counter as
    the firmware does at 1 ms, the GPS 1 PPS a millisecond on, and runs
    `length` seconds more."""
    case = Case(name)
    case.watch(pins)
    case.gps(at(0.002 + 37e-9))
    case.until(at(0.001))
    case.arm()
    case.until(at(0.001 + length))
    case.read_status()
    case.read_count()
    return case.end()


def image_cases(image):
    """The cases of an image but the Z3805A: every register written, its
    pins that pulse on the seconds watched over three, register 2's bit
    0 written among them (the Z3815A's and Z3816A's P14 and P28 phase),
    its faster outputs over a few milliseconds, and its phase-frequency
    detector, if it has one, over tens of milliseconds."""
    found = []
    # every address's write, each followed by every address's read, then
    # counter_configuration_init's registers again
    case = Case("every_register")
    for pattern in REGISTER_PATTERNS:
        for register in range(REGISTERS):
            case.write(register, pattern)
        case.read_all()
    for register, value in enumerate(Z3805A_REGISTERS):
        case.write(register, value)
    case.read_all()
    found.append(case.end())
    found.append(counting("seconds", 0.75 + 37e-9, [0.5, 1.5, 2.5], length=0.7,
                          watch=SLOW_PINS[image], writes=[(1.2, 2, 0x01), (1.3, 2, 0x00)]))
    found.append(watching("outputs", FAST_PINS[image], 0.0005))
    if image in DETECTOR_PINS:
        found.append(watching("detector", DETECTOR_PINS[image], 0.02))
    return found


def stimulus(case):
    """The case in stimulus.vh's picoseconds."""
    lines = []
    for at, operation, *rest in case.lines:
        if operation == "gps":
            rest = [str(GPS_WIDTH_PS)]
        lines.append(" ".join([str(to_ps(at)), operation, *rest]))
    return "\n".join(lines) + "\n"


def run(case, binary):
    """The case through the netlist, simulated by `binary`: its fixture
    lines."""
    with tempfile.NamedTemporaryFile("w", suffix=".stim", dir=binary.parent, delete=False) as file:
        file.write(stimulus(case))
        path = file.name
    try:
        result = subprocess.run(
            ["vvp", "-n", str(binary), f"+stimulus={path}", *case.options],
            capture_output=True, text=True, check=True)
    finally:
        os.unlink(path)
    reads = []
    pulses = []
    intervals = []
    flags = []
    edges = []
    for line in result.stdout.splitlines():
        words = line.split()
        if not words:
            continue
        if words[0] == "error:":
            raise RuntimeError(f"{case.name}: {line}")
        if words[0] == "read":
            reads.append(words[2])
        elif words[0] == "pps":
            pulses.append(to_tick(int(words[1])))
        elif words[0] == "edge":
            edges.append(" ".join(words))
        elif words[0] == "interval":
            # past the reset's own glitch
            if int(words[1]) > to_ps(case.lines[0][0]) and int(words[2]) > GLITCH_PS:
                intervals.append(int(words[2]))
        elif words[0] == "flag":
            flags.append(f"flag {to_tick(int(words[1]))} {words[2]} {words[3]}")
    out = [f"case {case.name}"]
    if case.options:
        out.append(" ".join(["options", *case.options]))
    for at, operation, *rest in case.lines:
        if operation == "read":
            rest = [reads.pop(0)]
        out.append(" ".join([str(at), operation, *rest]))
    out += [f"pps {tick}" for tick in pulses]
    out += [f"interval {width}" for width in intervals]
    out += flags
    out += edges
    assert not reads, f"{case.name}: reads left over"
    return "\n".join(out) + "\n"


def build(image, netlist, binary):
    if not netlist.exists():
        sys.exit(f"differential.py: no {netlist}; docs/firmware/outputs.md, \"Netlists\", makes it")
    subprocess.run(
        ["iverilog", "-g2005", "-I", str(HERE), "-o", str(binary), str(HERE / "xc3000.v"),
         str(HERE / "probe.v"), str(netlist), str(HERE / f"{image}_stimulus.v")],
        check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("output", nargs="?")
    parser.add_argument("--image", choices=sorted(FIXTURES), default=DEFAULT_IMAGE)
    parser.add_argument("--images", action="store_true",
                        help="print the images and their fixtures and run nothing")
    parser.add_argument("--netlist", type=pathlib.Path,
                        help="the image's netlist (target/fpga/<image>.v)")
    parser.add_argument("--binary", type=pathlib.Path,
                        help="the simulation (<image>_stimulus beside the netlist)")
    parser.add_argument("--built", action="store_true", help="the binary is built: do not build it")
    parser.add_argument("--list", action="store_true", help="print the cases' names and run nothing")
    parser.add_argument("--jobs", type=int, default=os.cpu_count())
    parser.add_argument("--only", action="append", help="run only the case NAME")
    parser.add_argument("--stimulus", metavar="NAME",
                        help="print the case NAME's stimulus file and run nothing")
    arguments = parser.parse_args()
    if arguments.images:
        print("\n".join(f"{image} {fixture}" for image, fixture in FIXTURES.items()))
        return
    if arguments.list:
        print("\n".join(case.name for case in cases(arguments.image)))
        return
    if arguments.stimulus:
        named = [case for case in cases(arguments.image) if case.name == arguments.stimulus]
        if not named:
            sys.exit(f"differential.py: no case {arguments.stimulus}")
        sys.stdout.write(stimulus(named[0]))
        return
    netlist = arguments.netlist or OUT / f"{arguments.image}.v"
    binary = arguments.binary or netlist.parent / f"{arguments.image}_stimulus"
    if not arguments.built:
        build(arguments.image, netlist, binary)
    selected = [case for case in cases(arguments.image) if not arguments.only or case.name in arguments.only]
    missing = set(arguments.only or []) - {case.name for case in selected}
    if missing:
        sys.exit(f"differential.py: no case {', '.join(sorted(missing))}")
    with concurrent.futures.ThreadPoolExecutor(arguments.jobs) as pool:
        results = list(pool.map(lambda case: run(case, binary), selected))
    text = "".join(results)
    if arguments.output:
        pathlib.Path(arguments.output).write_text(text)
    else:
        sys.stdout.write(text)


if __name__ == "__main__":
    main()
