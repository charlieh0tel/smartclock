"""What flash_host.py needs of a BDM target, in one place: an adapter
for a pod (pod_target.py), or for mc683xx's MC68331 (model_target.py), is
a class with these methods (tools/bdm/README.md, "The target
interface").

Every method acts at once or raises TargetError; a TargetError is
fatal to the procedure, which never retries.  Memory is supervisor
data space (FC 5, MC68331UM Table 4-11), which the board's chip selects
answer (their CSORs' SPACE field, S/U), through the chip's own decode
as the CPU's cycles would be: a BDM pod's READ/WRITE and DUMP/FILL
commands (MC68331UM 5.10.2.4).
"""

from enum import Enum
from typing import Protocol


class TargetError(Exception):
    """The target did not do what was asked: a bus error, a link
    fault, a reset, or anything the adapter cannot account for."""


class TargetTimeout(TargetError):
    """The CPU did not return to background mode in time."""


class Entry(Enum):
    """Why the CPU entered background mode, as ATEMP reports it on
    entry (MC68331UM 5.10.2.3, Table 5-4)."""

    BACKGROUND = "BGND instruction"
    BREAKPOINT = "hardware breakpoint"
    DOUBLE_BUS_FAULT = "double bus fault"


# The widths read_memory and write_memory take, in bytes.
WIDTHS = (1, 2, 4)


class Target(Protocol):
    """A CPU32 whose background debug mode the host drives."""

    def reset_into_bdm(self) -> None:
        """RESET with BKPT held low through it: background mode enabled,
        the CPU stopped before its first instruction, the SIM at its
        reset values (MC68331UM 5.10.2.1)."""
        ...

    def read_memory(self, address: int, width: int) -> int:
        """One read of width bytes, one of WIDTHS."""
        ...

    def write_memory(self, address: int, width: int, value: int) -> None:
        """One write of width bytes, one of WIDTHS."""
        ...

    def read_block(self, address: int, length: int) -> bytes:
        """length bytes from address, both even: the bulk read (DUMP)."""
        ...

    def write_block(self, address: int, data: bytes) -> None:
        """data at address, both even in length and address: the bulk
        write (FILL)."""
        ...

    def read_register(self, name: str) -> int:
        """A register by name: D0-D7, A0-A7, PC, SR or VBR.  A7 is the
        stack pointer SR's S bit selects, the supervisor's once SR is
        written with S set, so the host writes SR before A7.  PC is the
        return program counter, RPC, where GO resumes (MC68331UM
        5.10.2.5.2): after a BGND entry, the address after the BGND."""
        ...

    def write_register(self, name: str, value: int) -> None:
        """A register by read_register's names."""
        ...

    def go(self) -> None:
        """Leave background mode, running from PC (BDM GO)."""
        ...

    def wait_for_bdm(self, timeout: float) -> Entry:
        """Waits until the CPU is in background mode again, up to timeout
        seconds of the target's time; TargetTimeout after that, the CPU
        left running, never halted.  A reset meanwhile is a TargetError."""
        ...
