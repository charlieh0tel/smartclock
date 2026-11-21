"""The Target of target.py on mc683xx's MC68331 (its Python module,
docs/python.md there): the chip's own SIM decodes the board's chip
selects, its BDM enters background mode at a reset with BKPT held
(reset_with_breakpoint) and at BGND, and its bus cycles are what the
host's memory commands make.  Wired to it as the 58503A's board wires
them: the flash model (am29f010.py) on CSBOOT, CS1, CS6 and CS7, and
64 KiB of RAM on CS0 (reads), CS2 (upper byte writes) and CS3 (lower)
(docs/firmware/chip-selects.md, 58503A 3704).  Nothing else of the
board is: no FPGA, DUART, EEPROM or ports.

mc683xx's reset SIMCR is the manual's, $00CF (MC68331UM D.3.1), where
the bench pod's reset entry read $60CF; flash_host.py relies on
neither.  A reset during a run (the watchdog's, say) stops the run and
is a TargetError, as a pod would see the CPU leave background mode
unasked.
"""

from dataclasses import dataclass

import mc683xx

from am29f010 import BoardFlash
from target import Entry, TargetError, TargetTimeout, WIDTHS

RAM_SIZE = 0x10000
FLASH_PINS = ["CSBOOT", "CS1", "CS6", "CS7"]
RAM_PINS = ["CS0", "CS2", "CS3"]
WORD = 2
# The chip's background entries as target.py's.
ENTRIES = {
    mc683xx.Entry.Background: Entry.BACKGROUND,
    mc683xx.Entry.Breakpoint: Entry.BREAKPOINT,
    mc683xx.Entry.DoubleBusFault: Entry.DOUBLE_BUS_FAULT,
}


@dataclass
class FlashPeripheral:
    """BoardFlash as an mc683xx peripheral; bus_errors the addresses
    whose reads end in BERR, as a fault to inject."""

    flash: BoardFlash
    bus_errors: frozenset[int] = frozenset()

    def read(self, cycle: mc683xx.BusCycle) -> int:
        if cycle.address in self.bus_errors:
            raise mc683xx.BusError()
        return self.flash.read(cycle.select, cycle.address, cycle.size, cycle.now)

    def write(self, cycle: mc683xx.BusCycle, value: int) -> None:
        self.flash.write(cycle.select, cycle.address, cycle.size, value, cycle.now)


class ResetWatch:
    """A peripheral on no pin that sees every reset: while armed, one
    stops the run."""

    def __init__(self, stop: mc683xx.StopRequest):
        self.stop = stop
        self.armed = False
        self.resets = 0

    def reset(self, now: int) -> None:
        if self.armed:
            self.resets += 1
            self.stop.request()


class ModelTarget:
    """The board above as a Target."""

    def __init__(self, flash: BoardFlash, bus_errors: frozenset[int] = frozenset()):
        builder = mc683xx.Builder()
        self.flash = flash
        self.peripheral = FlashPeripheral(flash, bus_errors)
        builder.peripheral(self.peripheral, FLASH_PINS)
        self.ram = builder.memory(bytes(RAM_SIZE), RAM_PINS)
        self.stop = builder.stop_request()
        self.watch = ResetWatch(self.stop)
        builder.unwired_peripheral(self.watch)
        self.chip = builder.build()
        self.runs = 0

    def reset_into_bdm(self) -> None:
        self.chip.reset_with_breakpoint()
        if self.chip.background is None:
            raise TargetError("not in background mode after a reset with BKPT held")

    def access(self, action, *arguments):
        try:
            return action(*arguments)
        except mc683xx.BusError as error:
            raise TargetError(f"bus error at {arguments[0]:#x}") from error

    def read_memory(self, address: int, width: int) -> int:
        if width not in WIDTHS:
            raise TargetError(f"width {width}")
        return self.access(self.chip.bus_read, address, width)

    def write_memory(self, address: int, width: int, value: int) -> None:
        if width not in WIDTHS:
            raise TargetError(f"width {width}")
        self.access(self.chip.bus_write, address, width, value)

    def read_block(self, address: int, length: int) -> bytes:
        if address % WORD or length % WORD:
            raise TargetError(f"block {address:#x}+{length:#x} not even")
        data = bytearray()
        for at in range(address, address + length, WORD):
            data += self.read_memory(at, WORD).to_bytes(WORD, "big")
        return bytes(data)

    def write_block(self, address: int, data: bytes) -> None:
        if address % WORD or len(data) % WORD:
            raise TargetError(f"block {address:#x}+{len(data):#x} not even")
        for offset in range(0, len(data), WORD):
            self.write_memory(address + offset, WORD, int.from_bytes(data[offset : offset + WORD], "big"))

    def read_register(self, name: str) -> int:
        return self.chip.read_register(name)

    def write_register(self, name: str, value: int) -> None:
        self.chip.write_register(name, value)

    def go(self) -> None:
        if self.chip.background is None:
            raise TargetError("go with the CPU not in background mode")
        self.chip.go()

    def wait_for_bdm(self, timeout: float) -> Entry:
        self.runs += 1
        self.stop.withdraw()
        self.watch.armed = True
        try:
            run = self.chip.run_for(int(timeout * self.chip.system_hz))
        except mc683xx.BusError as error:
            raise TargetError("bus error during the run") from error
        finally:
            self.watch.armed = False
        if self.watch.resets:
            raise TargetError("the target reset while running")
        match run.stop:
            case mc683xx.Stop.Background(entry):
                return ENTRIES[type(entry)]
            case mc683xx.Stop.Budget():
                raise TargetTimeout(f"still running after {timeout} s")
            case other:
                raise TargetError(f"the run ended {other}")
