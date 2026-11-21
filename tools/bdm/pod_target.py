"""The Target of target.py on the mc683xx BDM pod, through its Python
module `mc683xx_bdm` (the pod's repository, crates/py: its README and
the type stub python/mc683xx_bdm/__init__.pyi).  The module is imported
only when a pod is opened, so the tests and the model path need no
pod package.

Every pod exception (TransportError, ProtocolError, TargetError,
BusError, and the ValueError of an argument the pod rejects) becomes a
TargetError carrying the original's type and message, with the original
as its cause.  A bulk access that ends in a bus error has made the
accesses before it.

The pod has no wait call: wait_for_bdm polls `background`, None while
the CPU runs, and the RESET line between polls.  It never halts the CPU
on a time-out: the host treats a time-out as fatal and touches the
target no more, since a halt of a CPU that a reset left running with
BDM off would reach the firmware as a breakpoint exception.  A reset
while the CPU runs is reported by no pod call; the RESET line seen
low between polls is how this adapter knows, so a reset pulse shorter
than a poll goes unseen and ends in the time-out instead.
"""

import importlib
import time
from collections.abc import Callable, Iterator
from contextlib import contextmanager
from types import ModuleType
from typing import Any

from target import Entry, TargetError, TargetTimeout, WIDTHS

MODULE = "mc683xx_bdm"
# Memory accesses' function code: supervisor data (MC68331UM Table 4-11).
SUPERVISOR_DATA = 5
WORD, LONG = 2, 4
# Seconds between polls of `background` and the RESET line: each poll is
# two USB round trips, and a call's shortest run is far below this.
POLL_INTERVAL = 0.01
TIMEOUT_ADVICE = "not halted: touch nothing more, and reset the unit into BDM (reset with BKPT held) before anything else"


def pod_exceptions(module: ModuleType) -> tuple[type[BaseException], ...]:
    """Every exception a pod call raises, by the stub's names."""
    return (module.TransportError, module.ProtocolError, module.TargetError, module.BusError, ValueError)


def widths(address: int, length: int) -> Iterator[tuple[int, int, int]]:
    """A block, both even, as runs of one access width, widest first:
    (address, length, width) for a leading word up to a long boundary,
    the longs, and a trailing word."""
    end = address + length
    if address % LONG and address < end:
        yield address, WORD, WORD
        address += WORD
    longs = (end - address) // LONG * LONG
    if longs:
        yield address, longs, LONG
        address += longs
    if address < end:
        yield address, WORD, WORD


class PodTarget:
    """A pod, open, as a Target.  module: the pod's Python module, or a
    stand-in for it; clock and sleep: wait_for_bdm's time, a test's to
    replace."""

    def __init__(
        self,
        pod: Any,
        module: ModuleType,
        clock: Callable[[], float] = time.monotonic,
        sleep: Callable[[float], None] = time.sleep,
    ):
        self.pod = pod
        self.module = module
        self.exceptions = pod_exceptions(module)
        self.clock = clock
        self.sleep = sleep
        # The pod's background entries as target.py's.
        self.entries = (
            (module.Entry.Background, Entry.BACKGROUND),
            (module.Entry.Breakpoint, Entry.BREAKPOINT),
            (module.Entry.DoubleBusFault, Entry.DOUBLE_BUS_FAULT),
        )

    @staticmethod
    @contextmanager
    def open(name: str, module: ModuleType | None = None) -> Iterator["PodTarget"]:
        """The pod `name` names (`usb`, `usb:SERIAL`, as `bdm --pod`
        does), closed at the end; nothing is sent to the target."""
        if module is None:
            try:
                module = importlib.import_module(MODULE)
            except ImportError as error:
                raise TargetError(f"no {MODULE} module: {error}") from error
        target = PodTarget(None, module)
        with target.mapped(f"opening pod {name}"):
            target.pod = module.Pod(name)
        try:
            yield target
        finally:
            with target.mapped("closing the pod"):
                target.pod.close()

    @contextmanager
    def mapped(self, what: str) -> Iterator[None]:
        try:
            yield
        except self.exceptions as error:
            raise TargetError(f"{what}: {type(error).__name__}: {error}") from error

    def reset_into_bdm(self) -> None:
        with self.mapped("reset with BKPT held"):
            self.pod.reset_with_breakpoint()
            entry = self.pod.background
        if entry is None:
            raise TargetError("not in background mode after a reset with BKPT held")

    def read_memory(self, address: int, width: int) -> int:
        if width not in WIDTHS:
            raise TargetError(f"width {width}")
        with self.mapped(f"read of {width} at {address:#x}"):
            return self.pod.bus_read(address, width, SUPERVISOR_DATA)

    def write_memory(self, address: int, width: int, value: int) -> None:
        if width not in WIDTHS:
            raise TargetError(f"width {width}")
        with self.mapped(f"write of {width} at {address:#x}"):
            self.pod.bus_write(address, width, value, SUPERVISOR_DATA)

    def read_block(self, address: int, length: int) -> bytes:
        if address % WORD or length % WORD:
            raise TargetError(f"block {address:#x}+{length:#x} not even")
        data = bytearray()
        for at, run, width in widths(address, length):
            with self.mapped(f"bulk read of {run:#x} at {at:#x}"):
                data += self.pod.read_memory(at, run, size=width, function_code=SUPERVISOR_DATA)
        if len(data) != length:
            raise TargetError(f"bulk read at {address:#x} gave {len(data)} bytes, not {length}")
        return bytes(data)

    def write_block(self, address: int, data: bytes) -> None:
        if address % WORD or len(data) % WORD:
            raise TargetError(f"block {address:#x}+{len(data):#x} not even")
        for at, run, width in widths(address, len(data)):
            offset = at - address
            with self.mapped(f"bulk write of {run:#x} at {at:#x}"):
                self.pod.write_memory(at, data[offset : offset + run], size=width, function_code=SUPERVISOR_DATA)

    def read_register(self, name: str) -> int:
        with self.mapped(f"read of {name}"):
            return self.pod.read_register(name)

    def write_register(self, name: str, value: int) -> None:
        with self.mapped(f"write of {name}"):
            self.pod.write_register(name, value)

    def go(self) -> None:
        with self.mapped("go"):
            if self.pod.background is None:
                raise TargetError("go with the CPU not in background mode")
            self.pod.go()

    def entry(self, background: Any) -> Entry:
        for kind, entry in self.entries:
            if isinstance(background, kind):
                return entry
        raise TargetError(f"background mode entered by {background!r}, which target.py does not name")

    def wait_for_bdm(self, timeout: float) -> Entry:
        deadline = self.clock() + timeout
        while True:
            with self.mapped("polling for background mode"):
                background = self.pod.background
                reset = background is None and not self.pod.pins().reset
            if background is not None:
                return self.entry(background)
            if reset:
                raise TargetError("the target reset while running: RESET low, the CPU running with BDM off; " + TIMEOUT_ADVICE)
            if self.clock() >= deadline:
                raise TargetTimeout(f"still running after {timeout} s; {TIMEOUT_ADVICE}")
            self.sleep(POLL_INTERVAL)
