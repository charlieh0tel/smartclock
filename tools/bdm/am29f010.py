"""A model of the 58503A's flash, for testing the BDM flash routine:
four Am29F010s in two word-interleaved pairs on the board's chip
selects (tools/bdm/README.md, "The flash model").

Am29F010 is one part's command state machine, for exactly the
commands the routine and the installer give: the unlock cycles,
autoselect, byte program, sector erase and read/reset, with the
write operation status a part shows while it works (Am29F010B,
Command Definitions, pp. 12-15; Write Operation Status, pp. 16-18).
BoardFlash wires four of them as the board does: a part's output
enable on its pair's read select, CSBOOT or CS1, its write enable on
the pair's write select, CS6 or CS7, the even part on D15-D8, and a
part's address lines on A1-A17 (docs/firmware/restart.md, "The
installer"; docs/firmware/chip-selects.md, 58503A 3704).  Neither imports mc683xx: time
is the caller's count of system clocks, cycles are a select's name,
an address and a size.

Not modeled: DQ3's meaning beyond its level, DQ6's toggle beyond one
flip per status read, the toggle-bit algorithm, erase suspend and
resume, chip erase, the high-voltage autoselect and sector protection
commands, the 50 us window's extension by further sectors' commands
(one is accepted, as the routine gives one), and every electrical and
timing margin: a cycle is accepted whatever its timing.
"""

from dataclasses import dataclass, field
from enum import Enum, auto

from board import FLASH_SIZE, PAIR_SIZE, READ_SELECTS, WORD, WRITE_SELECTS

# A part: 128 KiB, eight 16 KiB sectors by A16-A14 (Am29F010B, Table 2,
# p. 9).
PART_SIZE = 0x20000
SECTOR_SIZE = 0x4000
SECTORS = PART_SIZE // SECTOR_SIZE
ERASED_BYTE = 0xFF
BYTE_MASK = 0xFF
BYTE_BITS = 8

# The address bits the unlock cycles decode, and their addresses and
# data; the commands the third cycle gives (Am29F010B, Table 4, p. 15).
UNLOCK_MASK = 0x7FFF
UNLOCK_FIRST, UNLOCK_SECOND = 0x5555, 0x2AAA
UNLOCK_FIRST_DATA, UNLOCK_SECOND_DATA = 0xAA, 0x55
AUTOSELECT, PROGRAM, ERASE_SETUP, SECTOR_ERASE, READ_RESET = 0x90, 0xA0, 0x80, 0x30, 0xF0
# Autoselect: the address's low byte chooses the code (Table 4, Table 3).
AUTOSELECT_SELECT_MASK = 0xFF
MANUFACTURER_ADDRESS, DEVICE_ADDRESS, PROTECTION_ADDRESS = 0x00, 0x01, 0x02
AMD, AM29F010B = 0x01, 0x20
PROTECTED, UNPROTECTED = 0x01, 0x00

# Write operation status bits (Table 5, p. 18).
DQ7, DQ6, DQ5, DQ3 = 0x80, 0x40, 0x20, 0x08


class State(Enum):
    """Where a part is in a command sequence or an embedded operation."""

    READ = auto()
    UNLOCKED_1 = auto()
    UNLOCKED_2 = auto()
    AUTOSELECT = auto()
    PROGRAM_SETUP = auto()
    ERASE_SETUP = auto()
    ERASE_UNLOCKED_1 = auto()
    ERASE_UNLOCKED_2 = auto()
    # the sector erase time-out, 50 us, before erasing begins (p. 13)
    ERASE_WINDOW = auto()
    PROGRAMMING = auto()
    ERASING = auto()
    # DQ5 set: the operation ran out of time; only read/reset leaves
    EXCEEDED_PROGRAM = auto()
    EXCEEDED_ERASE = auto()


@dataclass
class Timing:
    """How long a part's embedded operations take, in system clocks.
    stuck: none ever completes; each sets DQ5 after its time instead
    (a failed part, p. 18)."""

    program: int
    erase: int
    erase_window: int
    # a program or erase of a protected sector: status this long, then
    # reading array data, nothing changed (pp. 16-17)
    protected_program: int
    protected_erase: int
    stuck: bool = False


@dataclass
class Am29F010:
    """One part: its cells, its protected sectors, the codes autoselect
    gives, its state and the operation in progress.  unmodeled records a
    command sequence the model does not implement, which tests require
    to be empty; operations records each program and erase begun, and
    status_reads counts the reads that gave status."""

    cells: bytearray
    timing: Timing
    protected: frozenset[int] = frozenset()
    manufacturer: int = AMD
    device: int = AM29F010B
    state: State = State.READ
    deadline: int = 0
    address: int = 0
    data: int = 0
    sector: int = 0
    toggle: int = 0
    unmodeled: list[tuple[State, int, int]] = field(default_factory=list)
    operations: list[tuple[str, int, int]] = field(default_factory=list)
    status_reads: int = 0

    def settle(self, now: int) -> None:
        """The embedded operation's progress up to now."""
        if self.state == State.ERASE_WINDOW and now >= self.deadline:
            self.state = State.ERASING
            self.deadline += self.erase_time()
        if self.state not in (State.PROGRAMMING, State.ERASING) or now < self.deadline:
            return
        if self.state == State.PROGRAMMING:
            if self.timing.stuck:
                self.state = State.EXCEEDED_PROGRAM
                return
            old = self.cells[self.address]
            if not self.sector_protected(self.address):
                self.cells[self.address] = old & self.data
            # a 1 asked where a 0 is: DQ5 (p. 12)
            self.state = State.EXCEEDED_PROGRAM if self.data & ~old & BYTE_MASK else State.READ
        else:
            if self.timing.stuck:
                self.state = State.EXCEEDED_ERASE
                return
            if self.sector not in self.protected:
                start = self.sector * SECTOR_SIZE
                self.cells[start : start + SECTOR_SIZE] = bytes([ERASED_BYTE]) * SECTOR_SIZE
            self.state = State.READ

    def sector_protected(self, address: int) -> bool:
        return address // SECTOR_SIZE in self.protected

    def erase_time(self) -> int:
        return self.timing.protected_erase if self.sector in self.protected else self.timing.erase

    def status(self, dq7: int, dq5: int, dq3: int) -> int:
        """A status read: DQ6 toggling, the other bits as given; DQ4 and
        DQ2-DQ0, which Table 5 leaves undefined, the complement of DQ7's
        datum's, so that no read of status passes for the data."""
        self.toggle ^= DQ6
        self.status_reads += 1
        undefined = ~(self.data if self.state in (State.PROGRAMMING, State.EXCEEDED_PROGRAM) else ERASED_BYTE)
        return dq7 | self.toggle | dq5 | dq3 | (undefined & ~(DQ7 | DQ6 | DQ5 | DQ3) & BYTE_MASK)

    def read(self, address: int, now: int) -> int:
        self.settle(now)
        match self.state:
            case State.AUTOSELECT:
                select = address & AUTOSELECT_SELECT_MASK
                if select == MANUFACTURER_ADDRESS:
                    return self.manufacturer
                if select == DEVICE_ADDRESS:
                    return self.device
                if select == PROTECTION_ADDRESS:
                    return PROTECTED if self.sector_protected(address) else UNPROTECTED
                self.unmodeled.append((self.state, address, -1))
                return 0
            case State.PROGRAMMING:
                return self.status(~self.data & DQ7, 0, 0)
            case State.EXCEEDED_PROGRAM:
                return self.status(~self.data & DQ7, DQ5, 0)
            case State.ERASE_WINDOW:
                return self.status(0, 0, 0)
            case State.ERASING:
                return self.status(0, 0, DQ3)
            case State.EXCEEDED_ERASE:
                return self.status(0, DQ5, DQ3)
            case _:
                return self.cells[address]

    def write(self, address: int, data: int, now: int) -> None:
        self.settle(now)
        unlock = address & UNLOCK_MASK
        match self.state:
            case State.PROGRAMMING | State.ERASING:
                # ignored until the operation completes (pp. 12-13)
                return
            case State.EXCEEDED_PROGRAM | State.EXCEEDED_ERASE:
                if data == READ_RESET:
                    self.state = State.READ
                return
            case State.ERASE_WINDOW:
                # any other command ends the window and the erase (p. 13)
                if data != SECTOR_ERASE:
                    self.state = State.READ
                else:
                    self.unmodeled.append((self.state, address, data))
                return
            case State.READ | State.AUTOSELECT:
                if unlock == UNLOCK_FIRST and data == UNLOCK_FIRST_DATA:
                    self.state = State.UNLOCKED_1
                elif data == READ_RESET:
                    self.state = State.READ
                else:
                    self.unmodeled.append((self.state, address, data))
                    self.state = State.READ
            case State.UNLOCKED_1:
                self.state = State.UNLOCKED_2 if (unlock, data) == (UNLOCK_SECOND, UNLOCK_SECOND_DATA) else State.READ
            case State.UNLOCKED_2:
                commands = {AUTOSELECT: State.AUTOSELECT, PROGRAM: State.PROGRAM_SETUP, ERASE_SETUP: State.ERASE_SETUP}
                if unlock == UNLOCK_FIRST and data in commands:
                    self.state = commands[data]
                else:
                    if data != READ_RESET:
                        self.unmodeled.append((self.state, address, data))
                    self.state = State.READ
            case State.PROGRAM_SETUP:
                self.address, self.data = address, data
                self.operations.append(("program", address, data))
                time = self.timing.protected_program if self.sector_protected(address) else self.timing.program
                if self.sector_protected(address):
                    # status a while, then the array, nothing programmed
                    self.data = self.cells[address]
                self.state, self.deadline = State.PROGRAMMING, now + time
            case State.ERASE_SETUP:
                self.state = State.ERASE_UNLOCKED_1 if (unlock, data) == (UNLOCK_FIRST, UNLOCK_FIRST_DATA) else State.READ
            case State.ERASE_UNLOCKED_1:
                ok = (unlock, data) == (UNLOCK_SECOND, UNLOCK_SECOND_DATA)
                self.state = State.ERASE_UNLOCKED_2 if ok else State.READ
            case State.ERASE_UNLOCKED_2:
                if data == SECTOR_ERASE:
                    self.sector = address // SECTOR_SIZE
                    self.operations.append(("erase", address, data))
                    self.state, self.deadline = State.ERASE_WINDOW, now + self.timing.erase_window
                else:
                    self.unmodeled.append((self.state, address, data))
                    self.state = State.READ


class Hazard(Enum):
    """A cycle the board's wiring makes unsafe."""

    # CSBOOT asserted above its pair: at reset CSBOOT decodes 1 MiB
    # (MC68331UM D.3.25), and pair 1's parts, with no A18, answer there
    MIRROR = "read select outside its pair"
    # a read select, the parts' output enable, asserted on a write:
    # the parts drive the bus against the CPU; whether the write lands
    # depends on WE#'s wiring, which is not traced, so the model drops it
    WRITE_ON_READ_SELECT = "write with output enable asserted"


@dataclass
class BoardFlash:
    """The four parts on the board's selects: pairs[n] is (even part,
    odd part); hazards records each unsafe cycle with its select and
    address, which tests require to be empty."""

    pairs: list[tuple[Am29F010, Am29F010]]
    hazards: list[tuple[Hazard, str, int]] = field(default_factory=list)

    @staticmethod
    def from_image(image: bytes, timing: Timing | list[Timing], protected: frozenset[int] = frozenset()) -> "BoardFlash":
        """The parts holding a flash image; timing for all four, or by
        part: pair 1 even, odd, pair 2 even, odd."""
        timings = timing if isinstance(timing, list) else [timing] * 4
        parts = [
            Am29F010(bytearray(image[base + lane : base + PAIR_SIZE : WORD]), timings[2 * pair + lane], protected)
            for pair, base in enumerate((0, PAIR_SIZE))
            for lane in range(WORD)
        ]
        return BoardFlash([(parts[0], parts[1]), (parts[2], parts[3])])

    def parts(self) -> list[Am29F010]:
        return [part for pair in self.pairs for part in pair]

    def image(self) -> bytes:
        """The cells as the bus reads them in read mode."""
        image = bytearray(FLASH_SIZE)
        for index, (even, odd) in enumerate(self.pairs):
            base = index * PAIR_SIZE
            image[base : base + PAIR_SIZE : WORD] = even.cells
            image[base + 1 : base + PAIR_SIZE : WORD] = odd.cells
        return bytes(image)

    def lanes(self, pair: int, address: int, size: int) -> list[tuple[Am29F010, int]]:
        """The parts a cycle reaches, each with its data's shift."""
        even, odd = self.pairs[pair]
        if size == WORD:
            return [(even, BYTE_BITS), (odd, 0)]
        return [(odd, 0)] if address & 1 else [(even, 0)]

    @staticmethod
    def part_address(address: int) -> int:
        return (address >> 1) & (PART_SIZE - 1)

    def read(self, select: str, address: int, size: int, now: int) -> int:
        pair = READ_SELECTS[select]
        if address // PAIR_SIZE != pair:
            self.hazards.append((Hazard.MIRROR, select, address))
        value = 0
        for part, shift in self.lanes(pair, address, size):
            value |= part.read(self.part_address(address), now) << shift
        return value

    def write(self, select: str, address: int, size: int, value: int, now: int) -> None:
        if select in READ_SELECTS:
            if address // PAIR_SIZE != READ_SELECTS[select]:
                self.hazards.append((Hazard.MIRROR, select, address))
            self.hazards.append((Hazard.WRITE_ON_READ_SELECT, select, address))
            return
        pair = WRITE_SELECTS[select]
        for part, shift in self.lanes(pair, address, size):
            part.write(self.part_address(address), value >> shift & BYTE_MASK, now)

    def unmodeled(self) -> list[tuple[State, int, int]]:
        return [entry for part in self.parts() for entry in part.unmodeled]
