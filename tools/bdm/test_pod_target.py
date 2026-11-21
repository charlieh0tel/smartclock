"""Tests of pod_target.py against a stand-in for the pod's module, by
the names and signatures of its type stub (the pod's repository,
crates/py/python/mc683xx_bdm/__init__.pyi): no pod, no USB.

    python3 -I tools/bdm/test_pod_target.py

`make test-bdm` runs it.
"""

import sys
import types
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from pod_target import POLL_INTERVAL, SUPERVISOR_DATA, PodTarget, widths  # noqa: E402
from target import Entry, TargetError, TargetTimeout  # noqa: E402


class FakeTransportError(OSError):
    pass


class FakeProtocolError(RuntimeError):
    pass


class FakeTargetError(RuntimeError):
    pass


class FakeBusError(Exception):
    pass


class FakeEntry:
    """The stub's Entry and its variants."""

    class Background:
        pass

    class Breakpoint:
        pass

    class DoubleBusFault:
        def __init__(self, ssw: int):
            self.ssw = ssw

    class Unknown:
        def __init__(self, atemp: int):
            self.atemp = atemp


class FakePins:
    def __init__(self, reset: bool):
        self.reset = reset


class FakePod:
    """Records every call; background answers from a script, one item a
    read, the last repeated; resets the RESET levels pins() gives, the
    last repeated; raising maps a method's name to the exception it
    raises."""

    def __init__(self, background=(), resets=(True,), raising=None, memory=None):
        self.calls: list[tuple] = []
        self.backgrounds = list(background) or [FakeEntry.Breakpoint()]
        self.resets = list(resets)
        self.raising = raising or {}
        self.memory = memory or {}

    def record(self, name, *arguments, **keywords):
        self.calls.append((name, arguments, keywords))
        if name in self.raising:
            raise self.raising[name]

    @property
    def background(self):
        self.record("background")
        return self.backgrounds.pop(0) if len(self.backgrounds) > 1 else self.backgrounds[0]

    def pins(self):
        self.record("pins")
        return FakePins(self.resets.pop(0) if len(self.resets) > 1 else self.resets[0])

    def reset_with_breakpoint(self):
        self.record("reset_with_breakpoint")
        return FakeEntry.Breakpoint()

    def halt(self):
        self.record("halt")

    def go(self):
        self.record("go")

    def close(self):
        self.record("close")

    def read_register(self, name):
        self.record("read_register", name)
        return 0x1234

    def write_register(self, name, value):
        self.record("write_register", name, value)

    def bus_read(self, address, size, function_code=SUPERVISOR_DATA):
        self.record("bus_read", address, size, function_code)
        return 0xAB

    def bus_write(self, address, size, value, function_code=SUPERVISOR_DATA):
        self.record("bus_write", address, size, value, function_code)

    def read_memory(self, address, length, *, size=1, function_code=SUPERVISOR_DATA):
        self.record("read_memory", address, length, size=size, function_code=function_code)
        return bytes(self.memory.get(at, 0) for at in range(address, address + length))

    def write_memory(self, address, data, *, size=1, function_code=SUPERVISOR_DATA):
        self.record("write_memory", address, bytes(data), size=size, function_code=function_code)
        for offset, byte in enumerate(data):
            self.memory[address + offset] = byte


def fake_module(pod: FakePod | None = None) -> types.ModuleType:
    module = types.ModuleType("mc683xx_bdm")
    module.TransportError = FakeTransportError
    module.ProtocolError = FakeProtocolError
    module.TargetError = FakeTargetError
    module.BusError = FakeBusError
    module.Entry = FakeEntry
    module.Pod = lambda name: pod or FakePod()
    return module


class Clock:
    """A clock that advances only by what the adapter sleeps."""

    def __init__(self):
        self.now = 0.0
        self.sleeps = 0

    def __call__(self) -> float:
        return self.now

    def sleep(self, seconds: float) -> None:
        self.sleeps += 1
        self.now += seconds


def adapter(pod: FakePod) -> tuple[PodTarget, Clock]:
    clock = Clock()
    return PodTarget(pod, fake_module(pod), clock, clock.sleep), clock


def names(pod: FakePod) -> list[str]:
    return [call[0] for call in pod.calls]


EXCEPTIONS = (
    FakeTransportError("no pod on USB"),
    FakeProtocolError("another protocol version"),
    FakeTargetError("no target power"),
    FakeBusError("bus error"),
    ValueError("no register X9"),
)


class Methods(unittest.TestCase):
    def test_reset_into_bdm(self):
        pod = FakePod()
        target, _ = adapter(pod)
        target.reset_into_bdm()
        self.assertEqual(names(pod), ["reset_with_breakpoint", "background"])

    def test_reset_not_in_background_mode(self):
        target, _ = adapter(FakePod(background=[None]))
        with self.assertRaisesRegex(TargetError, "not in background mode"):
            target.reset_into_bdm()

    def test_single_accesses(self):
        pod = FakePod()
        target, _ = adapter(pod)
        self.assertEqual(target.read_memory(0xFFFA00, 2), 0xAB)
        target.write_memory(0xFFFA21, 1, 0x04)
        self.assertEqual(
            pod.calls,
            [("bus_read", (0xFFFA00, 2, SUPERVISOR_DATA), {}), ("bus_write", (0xFFFA21, 1, 0x04, SUPERVISOR_DATA), {})],
        )
        for width in (0, 3, 8):
            with self.assertRaisesRegex(TargetError, "width"):
                target.read_memory(0, width)
            with self.assertRaisesRegex(TargetError, "width"):
                target.write_memory(0, width, 0)

    def test_registers_pass_through(self):
        # target.py's names are the pod's: PC its alias for RPC, A7 the
        # stack pointer SR's S bit selects
        pod = FakePod()
        target, _ = adapter(pod)
        for name in ("D0", "A0", "A7", "SR", "VBR", "PC"):
            target.write_register(name, 0x100)
            self.assertEqual(target.read_register(name), 0x1234)
        self.assertEqual(
            [call[1] for call in pod.calls if call[0] == "write_register"],
            [(name, 0x100) for name in ("D0", "A0", "A7", "SR", "VBR", "PC")],
        )

    def test_go(self):
        pod = FakePod()
        target, _ = adapter(pod)
        target.go()
        self.assertEqual(names(pod), ["background", "go"])
        target, _ = adapter(FakePod(background=[None]))
        with self.assertRaisesRegex(TargetError, "not in background mode"):
            target.go()

    def test_open_and_close(self):
        pod = FakePod()
        with PodTarget.open("usb", fake_module(pod)) as target:
            self.assertIs(target.pod, pod)
        self.assertEqual(names(pod), ["close"])

    def test_open_maps_errors(self):
        module = fake_module()

        def refused(name):
            raise FakeTransportError("no pod on USB")

        module.Pod = refused
        with self.assertRaisesRegex(TargetError, "opening pod usb: FakeTransportError: no pod on USB"):
            with PodTarget.open("usb", module):
                pass


class Blocks(unittest.TestCase):
    def test_widths(self):
        self.assertEqual(list(widths(0x100000, 0x400)), [(0x100000, 0x400, 4)])
        self.assertEqual(list(widths(0x101402, 6)), [(0x101402, 2, 2), (0x101404, 4, 4)])
        self.assertEqual(list(widths(0x101400, 6)), [(0x101400, 4, 4), (0x101404, 2, 2)])
        self.assertEqual(list(widths(0x101402, 8)), [(0x101402, 2, 2), (0x101404, 4, 4), (0x101408, 2, 2)])
        self.assertEqual(list(widths(0x2, 2)), [(0x2, 2, 2)])
        self.assertEqual(list(widths(0x0, 0)), [])

    def test_whole_flash_read_is_longs(self):
        pod = FakePod()
        target, _ = adapter(pod)
        self.assertEqual(len(target.read_block(0, 0x80000)), 0x80000)
        self.assertEqual(pod.calls, [("read_memory", (0, 0x80000), {"size": 4, "function_code": SUPERVISOR_DATA})])

    def test_write_then_read_back_unaligned(self):
        pod = FakePod()
        target, _ = adapter(pod)
        data = bytes(range(1, 11))
        target.write_block(0x101402, data)
        self.assertEqual(target.read_block(0x101402, len(data)), data)
        self.assertEqual(
            [(call[1][0], len(call[1][1]), call[2]["size"]) for call in pod.calls if call[0] == "write_memory"],
            [(0x101402, 2, 2), (0x101404, 8, 4)],
        )

    def test_odd_refused(self):
        pod = FakePod()
        target, _ = adapter(pod)
        for address, length in ((1, 2), (0, 3)):
            with self.assertRaisesRegex(TargetError, "not even"):
                target.read_block(address, length)
            with self.assertRaisesRegex(TargetError, "not even"):
                target.write_block(address, bytes(length))
        self.assertEqual(pod.calls, [])


class Waits(unittest.TestCase):
    def test_entries(self):
        for background, entry in (
            (FakeEntry.Background(), Entry.BACKGROUND),
            (FakeEntry.Breakpoint(), Entry.BREAKPOINT),
            (FakeEntry.DoubleBusFault(0x1234), Entry.DOUBLE_BUS_FAULT),
        ):
            with self.subTest(entry=entry):
                pod = FakePod(background=[None, None, background])
                target, clock = adapter(pod)
                self.assertEqual(target.wait_for_bdm(1.0), entry)
                self.assertEqual(clock.sleeps, 2)
                self.assertNotIn("halt", names(pod))

    def test_unknown_entry(self):
        target, _ = adapter(FakePod(background=[FakeEntry.Unknown(0x5)]))
        with self.assertRaisesRegex(TargetError, "does not name"):
            target.wait_for_bdm(1.0)

    def test_timeout_without_halt(self):
        pod = FakePod(background=[None])
        target, clock = adapter(pod)
        with self.assertRaisesRegex(TargetTimeout, "still running after 0.5 s; not halted.*reset the unit into BDM"):
            target.wait_for_bdm(0.5)
        self.assertGreaterEqual(clock.now, 0.5)
        self.assertLess(clock.now, 0.5 + 2 * POLL_INTERVAL)
        self.assertNotIn("halt", names(pod))
        self.assertNotIn("go", names(pod))

    def test_reset_while_running(self):
        pod = FakePod(background=[None], resets=[True, True, False])
        target, clock = adapter(pod)
        with self.assertRaises(TargetError) as caught:
            target.wait_for_bdm(10.0)
        self.assertNotIsInstance(caught.exception, TargetTimeout)
        self.assertIn("reset while running", str(caught.exception))
        self.assertEqual(clock.sleeps, 2)
        self.assertNotIn("halt", names(pod))

    def test_background_wins_over_reset_line(self):
        # in background mode the RESET line is not read
        pod = FakePod(background=[FakeEntry.Background()], resets=[False])
        target, _ = adapter(pod)
        self.assertEqual(target.wait_for_bdm(1.0), Entry.BACKGROUND)
        self.assertNotIn("pins", names(pod))


class Exceptions(unittest.TestCase):
    CALLS = (
        ("reset_with_breakpoint", lambda target: target.reset_into_bdm()),
        ("bus_read", lambda target: target.read_memory(0, 2)),
        ("bus_write", lambda target: target.write_memory(0, 2, 0)),
        ("read_memory", lambda target: target.read_block(0, 8)),
        ("write_memory", lambda target: target.write_block(0, bytes(8))),
        ("read_register", lambda target: target.read_register("D0")),
        ("write_register", lambda target: target.write_register("D0", 0)),
        ("go", lambda target: target.go()),
        ("background", lambda target: target.wait_for_bdm(1.0)),
        ("pins", lambda target: target.wait_for_bdm(1.0)),
    )

    def test_every_exception_of_every_call(self):
        for method, call in self.CALLS:
            for error in EXCEPTIONS:
                with self.subTest(method=method, error=type(error).__name__):
                    background = [None] if method == "pins" else []
                    target, _ = adapter(FakePod(background=background, raising={method: error}))
                    with self.assertRaises(TargetError) as caught:
                        call(target)
                    self.assertIs(caught.exception.__cause__, error)
                    self.assertIn(f"{type(error).__name__}: {error}", str(caught.exception))

    def test_others_not_caught(self):
        target, _ = adapter(FakePod(raising={"bus_read": KeyError("a bug")}))
        with self.assertRaises(KeyError):
            target.read_memory(0, 2)


if __name__ == "__main__":
    unittest.main()
