#!/usr/bin/env python3
"""Compares two emulator traces by the order and values of their device
accesses, and reports the first divergence (docs/emulator.md, "The
mc683xx backend").

Reads either format: smartclock-emu's Unicorn backend's
(`smartclock-emu: device cycle=... address=... size=... value=...`) and
mc683xx-board's (`mc68331: bus-cycle ...`, mc683xx's docs/tracing.md).  Cycles
are dropped: the backends' times differ (Unicorn counts about two cycles
a byte of code, mc683xx the CPU32's tables).  What is compared:

- each device access as bytes, its address and value, a word or a long
  split high byte first, since mc683xx sizes a cycle to its 8-bit port
  and Unicorn traces the instruction's access whole;
- only the board's devices, `--low` to `--high` (the DUART, chip select
  5's window, the EEPROM, CS4: 0x200000 to 0x600000), since mc683xx
  traces only the cycles a board part answers, never the MC68331's own
  modules;
- with `--exceptions`, the exceptions' vectors too, in order;
- unless `--no-collapse`, a run of one read repeated, a poll, as one,
  since how often a loop polls is timing;
- with `--reads-as-sets`, the reads between two writes (or exceptions)
  as a set of bytes, not in order: Unicorn (QEMU) traces a device
  access the size does not align as the aligned accesses covering it,
  `move.l (2,a2)` at 0x400060 as longs at 0x400060 and 0x400064, so
  its trace reads bytes again, and early, that the CPU32 reads once.

With `--writes`, reads are left out: what the firmware decided, not
how often it looked.  With `--by-region`, each device's window is compared alone, so that
the firmware's tasks interleaving differently, as their timing moves
them, does not end the comparison at the first task switch.

Usage: trace_diff.py A B [--context N] [--exceptions] [--no-collapse]
       [--reads-as-sets] [--writes] [--by-region]
"""

import argparse
import re
import sys

PREFIXES = ("smartclock-emu: ", "mc68331: ")
# A bus cycle's record kind: the Unicorn backend's, then mc683xx-board's.
BUS_CYCLE = ("device", "bus-cycle")
FIELD = re.compile(r"(\w+)=(\S+)")
BOARD_LOW = 0x20_0000
BOARD_HIGH = 0x60_0000
CONTEXT = 6
# The board's devices' windows, each compared alone with --by-region:
# the DUART on CS8, chip select 5's four 8 KiB devices (the latch, the
# switch byte, the FPGA's port, the EFC's strobe), the EEPROM on CS9
# (docs/firmware/restart.md, "The chip selects").
REGIONS = [
    ("duart", 0x20_0000, 0x20_1000),
    ("latch", 0x30_0000, 0x30_2000),
    ("switch", 0x30_2000, 0x30_4000),
    ("fpga", 0x30_4000, 0x30_6000),
    ("strobe", 0x30_6000, 0x30_8000),
    ("eeprom", 0x40_0000, 0x40_2000),
]


def records(path, low, high, exceptions):
    """The trace at `path` as (key, line number, line): key a device
    byte (`r` or `w`, address, value) or an exception's vector."""
    with open(path, encoding="ascii", errors="replace") as trace:
        for number, line in enumerate(trace, 1):
            line = line.rstrip("\n")
            prefix = next((p for p in PREFIXES if line.startswith(p)), None)
            if prefix is None:
                continue
            kind, _, rest = line[len(prefix):].partition(" ")
            fields = dict(FIELD.findall(rest))
            if kind in BUS_CYCLE:
                address = int(fields["address"], 16)
                if not low <= address < high or fields["value"] == "berr":
                    continue
                size = int(fields["size"])
                value = int(fields["value"], 16)
                access = fields["access"][0]
                for index in range(size):
                    byte = value >> (8 * (size - 1 - index)) & 0xFF
                    yield (access, address + index, byte), number, line
            elif kind == "exception" and exceptions:
                yield ("x", int(fields["vector"]), fields.get("level")), number, line


def collapsed(stream):
    """`stream` with each run of one read repeated as its first."""
    last = None
    for record in stream:
        key = record[0]
        if key == last and key[0] == "r":
            continue
        last = key
        yield record


def read_sets(stream):
    """`stream` with each run of reads as one record, its key the set of
    the bytes read (`R`, the set), at the run's first line."""
    run, first = set(), None
    for record in stream:
        key = record[0]
        if key[0] == "r":
            run.add(key[1:])
            first = first or record
            continue
        if run:
            yield ("R", frozenset(run)), first[1], first[2]
            run, first = set(), None
        yield record
    if run:
        yield ("R", frozenset(run)), first[1], first[2]


def bytes_text(reads):
    """A set of bytes read as text, by address."""
    return " ".join(f"{address:#08x}={value:#04x}" for address, value in sorted(reads))


def show(key):
    """A key as text."""
    if key[0] == "R":
        return f"reads of {len(key[1])} bytes"
    if key[0] == "x":
        return f"exception vector={key[1]} level={key[2]}"
    access = "read " if key[0] == "r" else "write"
    return f"{access} {key[1]:#08x} {key[2]:#04x}"


def compare(a, b, context):
    """Reports how the records `a` and `b` differ: whether they agree."""
    for index, (left, right) in enumerate(zip(a, b)):
        if left[0] != right[0]:
            break
    else:
        index = min(len(a), len(b))
        if len(a) == len(b):
            print(f"identical: {len(a)} records")
            return True
    print(f"{len(a)} and {len(b)} records; the first divergence is record {index}")
    for offset in range(max(0, index - context), index):
        print(f"  = {show(a[offset][0])}")
    if index < min(len(a), len(b)) and a[index][0][0] == b[index][0][0] == "R":
        left, right = a[index][0][1], b[index][0][1]
        print(f"  only in a: {bytes_text(left - right)}")
        print(f"  only in b: {bytes_text(right - left)}")
    for name, side in (("a", a), ("b", b)):
        for record in side[index:index + context]:
            key, number, line = record
            print(f"  {name} {show(key):<34} line {number}: {line}")
        if index >= len(side):
            print(f"  {name} (ended)")
    return False


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("a")
    parser.add_argument("b")
    parser.add_argument("--context", type=int, default=CONTEXT)
    parser.add_argument("--exceptions", action="store_true")
    parser.add_argument("--no-collapse", action="store_true")
    parser.add_argument("--reads-as-sets", action="store_true")
    parser.add_argument("--by-region", action="store_true")
    parser.add_argument("--writes", action="store_true")
    parser.add_argument("--low", type=lambda text: int(text, 0), default=BOARD_LOW)
    parser.add_argument("--high", type=lambda text: int(text, 0), default=BOARD_HIGH)
    arguments = parser.parse_args()
    regions = REGIONS if arguments.by_region else [("board", arguments.low, arguments.high)]
    agree = True
    for name, low, high in regions:
        if arguments.by_region:
            print(f"{name} ({low:#08x} to {high:#08x}):")

        def load(path):
            stream = records(path, low, high, arguments.exceptions)
            if arguments.writes:
                stream = (record for record in stream if record[0][0] != "r")
            stream = stream if arguments.no_collapse else collapsed(stream)
            return list(read_sets(stream) if arguments.reads_as_sets else stream)

        agree &= compare(load(arguments.a), load(arguments.b), arguments.context)
    return 0 if agree else 1


if __name__ == "__main__":
    sys.exit(main())
