# /// script
# requires-python = ">=3.11"
# ///
"""What a function does to the status register, read two ways: from the
image's instructions and from the hand-written C.  The function check
compares the two (function_check.py; docs/firmware/rewrite.md, "The
status register").

Ghidra's decompiler renders none of it -- not `ori #imm,sr`, `andi
#imm,sr`, a move to or from SR, nor what follows entering supervisor
mode -- so C taken from the export loses a function's interrupt masking
without a sign, and a run that masks and restores ends as one that does
neither.

The image's side is every such instruction control reaches from the
function's entry, by address.  The C's side is every call of the
helpers that are the C's only way to the register
(rewritten/common/cpu.h), in the order the source gives them, a C
function with no counterpart in the image standing for its body
wherever it is called.  Each is a list of Events; a function agrees
when the two lists are equal.
"""

import bisect
import re
from collections import Counter
from collections.abc import Callable
from dataclasses import dataclass, field

from image_diff import BRANCH, CALLS, PC_RELATIVE, TERMINALS, fields, jump_table, parse_address

# The status register's width: an instruction's immediate, which objdump
# prints signed, as the word it is.
WORD_MASK = 0xFFFF
# The interrupt priority mask, SR's bits 8 to 10 (cpu.h's CPU_IPL_7).
INTERRUPT_MASK = 0x700

# What an Event is, each named as a difference prints it.
SAVE = "save"
RAISE = "or"
LOWER = "and"
TOGGLE = "exclusive or"
WRITE = "write"
WRITE_LITERAL = "write of"
ENTER = "enter supervisor"
STOP = "stop"
RETURN_FROM_EXCEPTION = "rte"
USER_STACK = "move usp"
# Assembly in the C that names the register outside the helpers, which
# agrees with nothing.
OPAQUE = "assembly naming sr"
# A call of one of the image's functions, which is no Event of the
# register's but is kept between them (bracketed).
CALLED = "call"
# The kinds that carry a literal.
WITH_LITERAL = (RAISE, LOWER, TOGGLE, WRITE_LITERAL, STOP)


@dataclass(frozen=True)
class Event:
    """One thing done to the status register: its kind (SAVE: SR read;
    RAISE, LOWER, TOGGLE: SR ored, anded or exclusive-ored with a
    literal; WRITE: SR written from a register or memory; WRITE_LITERAL:
    from a literal; ENTER: a call of enter_supervisor; STOP, a stop's
    literal SR; RETURN_FROM_EXCEPTION; USER_STACK; OPAQUE; CALLED) and
    its literal, None for a kind with none or for C whose literal is not
    a constant; a CALLED's name is the function called.  On a path
    (Trace), a WRITE's value or an rte's is the system byte it leaves,
    and a CALLED's of a MASK_ARGUMENT routine the mask it was given."""

    kind: str
    value: int | None = None
    name: str | None = None

    def __str__(self) -> str:
        if self.kind == CALLED:
            return f"{self.kind} of {self.name}" + ("" if self.value is None else f" with mask {self.value:#x}")
        if self.kind in (WRITE, RETURN_FROM_EXCEPTION) and self.value is not None:
            return f"{self.kind} of system byte {self.value:#x}"
        if self.kind in WITH_LITERAL:
            return f"{self.kind} {'a value not constant' if self.value is None else f'{self.value:#x}'}"
        return self.kind


# The routines whose call enters supervisor mode and returns the SR it
# left in D0, the primary's and the installer's.
ENTER_ROUTINES = ("enter_supervisor", "installer_enter_supervisor")
# The C's routines that set the interrupt mask for their caller, the
# primary's and the installer's copies, whose calls a path keeps as it
# keeps the register's own Events (on_path): set_interrupt_mask, with
# the mask its first argument gives (MASK_ARGUMENT), and
# enter_supervisor_mode, which masks every interrupt.
MASK_ARGUMENT = ("set_interrupt_mask", "installer_set_interrupt_mask")
MASKING = MASK_ARGUMENT + ("enter_supervisor_mode", "installer_enter_supervisor_mode")
# The primary's region, as image_diff.parse_address names it; an
# overlay's is its own name.
PRIMARY = "primary"
# The sidecar's field naming the file of a function written by hand.
HAND_WRITTEN = "rewritten"

IMMEDIATE = re.compile(r"^#(-?\d+)")
TO_SR = re.compile(r",%sr$")
FROM_SR = re.compile(r"^%sr,")
USER_STACK_REGISTER = "%usp"
# The instructions that combine a literal with SR, by mnemonic's start.
COMBINING = (("ori", RAISE), ("andi", LOWER), ("eori", TOGGLE))
# A jmp to one fixed address, absolute or PC-relative, not through a
# register or a table.
FIXED_JUMP = re.compile(r"^(?:0x[0-9a-f]+|%pc@\(0x[0-9a-f]+\))$")


def instruction_event(mnemonic: str, operands: str) -> Event | None:
    """The Event of one instruction as objdump prints it, or None.  The
    condition codes' instructions, which name %ccr, are none."""
    literal = IMMEDIATE.match(operands)
    value = int(literal.group(1)) & WORD_MASK if literal else None
    if mnemonic == "rte":
        return Event(RETURN_FROM_EXCEPTION)
    if mnemonic == "stop":
        return Event(STOP, value)
    if USER_STACK_REGISTER in operands:
        return Event(USER_STACK)
    if TO_SR.search(operands):
        for prefix, kind in COMBINING:
            if mnemonic.startswith(prefix):
                return Event(kind, value)
        return Event(WRITE_LITERAL, value) if literal else Event(WRITE)
    if FROM_SR.search(operands):
        return Event(SAVE)
    return None


@dataclass(frozen=True)
class Step:
    """One instruction control reaches from a function's entry: its
    Event, None if it has none, and where control goes on from it, by
    address: the next instruction, unless it ends a path, and a
    branch's, a fixed jmp's or a table's targets, a jump that leaves the
    function among them."""

    event: Event | None
    successors: tuple[int, ...]


def walk(memory, start: int, functions: dict[int, str]) -> dict[int, Step]:
    """A function's Steps by address: those of every instruction control
    reaches from start, through its branches and a jmp's table of cases,
    not into the functions it calls.  functions is every function's name
    in the memory by its start; a branch or a fixed jmp to one leaves
    this function, the instructions there that function's, and one into
    the middle of another function is followed, since code two functions
    share is each one's own."""
    found, steps, pending, starts = set(), {}, [start], sorted(functions)
    while pending:
        at = pending.pop()
        after = bisect.bisect_right(starts, at)
        stop = starts[after] if after < len(starts) else memory.base + len(memory.data)
        for instruction in memory.instructions(at, stop):
            if instruction.address in found or instruction.mnemonic == ".short" or instruction.end > stop:
                break
            found.add(instruction.address)
            event = instruction_event(instruction.mnemonic, instruction.operands)
            targets = [field.target for field in fields(instruction)]
            jumps = []
            if instruction.mnemonic in CALLS:
                for name in (functions[target] for target in targets if target in functions):
                    event = Event(ENTER) if name in ENTER_ROUTINES else Event(CALLED, name=name)
            elif BRANCH.match(instruction.mnemonic) or (
                instruction.mnemonic == "jmp" and FIXED_JUMP.match(instruction.operands)
            ):
                named = [int(target, 16) for target in PC_RELATIVE.findall(instruction.operands)] or targets
                jumps = [target for target in named if target == start or memory.contains(target)]
                pending.extend(target for target in jumps if target == start or target not in functions)
            elif instruction.mnemonic == "jmp":
                jumps = jump_table(memory, instruction, stop)
                pending.extend(jumps)
            ends = instruction.mnemonic in TERMINALS
            steps[instruction.address] = Step(event, tuple(dict.fromkeys(jumps + ([] if ends else [instruction.end]))))
            if ends:
                break
    return steps


def step_events(steps: dict[int, Step]) -> list[tuple[int, Event]]:
    """The Events of a walk's Steps, each with its instruction's
    address, by address."""
    return sorted((address, step.event) for address, step in steps.items() if step.event)


def image_events(memory, start: int, functions: dict[int, str]) -> list[tuple[int, Event]]:
    """A function's Events in the image, its calls of functions among
    them (walk, step_events)."""
    return step_events(walk(memory, start, functions))


# The helpers that read and write the register themselves, each one
# instruction (cpu.h), and what a call of each is.
PRIMITIVES = {
    "cpu_sr_read": SAVE,
    "cpu_sr_write": WRITE,
    "cpu_enter_supervisor": ENTER,
    "cpu_installer_enter_supervisor": ENTER,
}
# The helpers that are an `ori #mask,sr`, and that and the `andi` that
# lowers the mask to the level; each returns the SR it found, saved
# first where its result is used.
RAISING = "cpu_interrupts_raise"
LEVELING = "cpu_interrupts_at"

STRING = re.compile(r'"(?:[^"\\\n]|\\.)*"')
CHARACTER = re.compile(r"'(?:[^'\\\n]|\\.)*'")
ATTRIBUTE = re.compile(r"__attribute__\s*\(\((?:[^()]|\([^()]*\))*\)\)")
# A function definition's declarator, up to its body's brace: its name
# and parameter list last.
DEFINITION = re.compile(r"\b([A-Za-z_]\w*)\s*\((?:[^()]|\((?:[^()]|\([^()]*\))*\))*\)\s*$")
CALL = re.compile(r"\b([A-Za-z_]\w*)\s*\(")
# An enumeration's body, and in one an enumerator given a literal.
ENUMERATION = re.compile(r"\benum\b[^{;()]*\{([^{}]*)\}")
ENUMERATOR = re.compile(r"(?:^|,)\s*([A-Za-z_]\w*)\s*=\s*(0[xX][0-9a-fA-F]+|\d+)\s*(?=,|$)")
CAST = re.compile(r"\(\s*(?:u?int\d+_t|unsigned|int|short|long)\s*\)")
NAME = re.compile(r"\b[A-Za-z_]\w*\b")
# An expression of literals alone, which Python reads as C does.
CONSTANT_EXPRESSION = re.compile(r"^(?:\s|0[xX][0-9a-fA-F]+|\d+|<<|>>|[|&~()+-])+$")
# Top-level assembly: a routine's label and what it calls.
ASSEMBLY_LABEL = re.compile(r"^([A-Za-z_]\w*):", re.M)
ASSEMBLY_CALL = re.compile(r"\bjsr\s+([A-Za-z_]\w*)")
NAMES_SR = re.compile(r"%%?sr\b|%%?usp\b|\bstop\b|\brte\b")
# What ends the statement before an expression statement.
STATEMENT_ENDS = ";{}"
# How C spells assembly, in a function and at top level, and the
# qualifiers that may stand between the word and its parenthesis, which
# stand before one nowhere else.
ASSEMBLY = ("__asm__", "asm")
ASSEMBLY_QUALIFIERS = ("volatile", "__volatile__", "inline", "goto")
DISCARDED = re.compile(r"\(\s*void\s*\)\s*$")


def blank(match: re.Match) -> str:
    """A literal's text as spaces, so that offsets hold."""
    return " " * len(match.group())


def matching(text: str, at: int, opening: str = "(", closing: str = ")") -> int:
    """The offset just past the bracket that closes the one at at."""
    depth = 0
    for offset in range(at, len(text)):
        depth += (text[offset] == opening) - (text[offset] == closing)
        if depth == 0:
            return offset + 1
    return len(text)


class Source:
    """A translation unit's preprocessed text, read for the status
    register: functions, each defined function's body by name, its
    literals blanked; strings, the same bodies with their literals, as
    assembly's are read; wrappers, each routine top-level assembly
    defines by the routines it calls (cpu.h's CPU_GIVING_CALLERS_CCR and
    its like); opaque, the routines whose top-level assembly names the
    register itself; constants, the enumerators given a literal."""

    def __init__(self, text: str):
        self.constants = {
            name: int(value, 0) for body in ENUMERATION.findall(text) for name, value in ENUMERATOR.findall(body)
        }
        code = CHARACTER.sub(blank, STRING.sub(blank, text))
        self.functions: dict[str, str] = {}
        self.strings: dict[str, str] = {}
        self.wrappers: dict[str, list[str]] = {}
        self.opaque: set[str] = set()
        at = boundary = 0
        while at < len(code):
            if code[at] == "(":
                at = matching(code, at)
            elif code[at] == ";":
                self.assembly(text[boundary:at])
                at += 1
                boundary = at
            elif code[at] == "{":
                end = matching(code, at, "{", "}")
                definition = DEFINITION.search(ATTRIBUTE.sub(blank, code[boundary:at]))
                if definition:
                    self.functions[definition.group(1)] = code[at:end]
                    self.strings[definition.group(1)] = text[at:end]
                    boundary = end
                at = end
            else:
                at += 1

    def assembly(self, statement: str) -> None:
        """Records a top-level statement's routines if it is assembly."""
        if not set(statement.split("(")[0].split()) & set(ASSEMBLY):
            return
        listing = "".join(string[1:-1] for string in STRING.findall(statement)).replace("\\n", "\n").replace("\\t", " ")
        for label in ASSEMBLY_LABEL.findall(listing):
            self.wrappers[label] = ASSEMBLY_CALL.findall(listing)
            if NAMES_SR.search(listing):
                self.opaque.add(label)

    def constant(self, expression: str) -> int | None:
        """An argument's value if it is a constant expression of
        literals and enumerators, as a word; else None."""
        text = NAME.sub(lambda name: str(self.constants.get(name.group(), name.group())), CAST.sub("", expression))
        if not CONSTANT_EXPRESSION.match(text):
            return None
        try:
            return int(eval(text, {"__builtins__": {}})) & WORD_MASK  # noqa: S307
        except (SyntaxError, TypeError, ValueError):
            return None

    def defines(self, name: str) -> bool:
        """Whether the unit gives a routine code of its own."""
        return name in self.functions or name in self.wrappers

    def events(self, name: str, in_image: frozenset[str], active: tuple[str, ...] = ()) -> list[Event]:
        """A routine's Events in source order.  in_image is the names of
        the image's functions: a call of one is a call, and a call of a
        function the unit defines that is none of them stands for that
        function's Events, being code the image has in the caller.
        active is the routines being read, none read again within
        itself."""
        if name in active or not self.defines(name):
            return []
        if name not in self.functions:
            found = [Event(OPAQUE)] if name in self.opaque else []
            for callee in self.wrappers[name]:
                if callee in ENTER_ROUTINES:
                    found.append(Event(ENTER))
                elif callee in in_image:
                    found.append(Event(CALLED, name=callee))
                elif callee in self.functions:
                    found += self.events(callee, in_image, active + (name,))
            return found
        return self.text_events(self.functions[name], self.strings[name], in_image, active + (name,))

    def text_events(self, code: str, strings: str, in_image: frozenset[str], active: tuple[str, ...]) -> list[Event]:
        """The Events of a stretch of a routine's body, code, in the
        order it runs them where the source fixes one: each call's
        arguments' before its own."""
        found, at = [], 0
        for call in CALL.finditer(code):
            if call.start() < at:
                continue
            callee, opening = call.group(1), call.end() - 1
            at = matching(code, opening)
            if callee in ASSEMBLY + ASSEMBLY_QUALIFIERS:
                found += [Event(OPAQUE)] if NAMES_SR.search(strings[opening:at]) else []
                continue
            argument = code[opening + 1 : at - 1]
            found += self.text_events(argument, strings[opening + 1 : at - 1], in_image, active)
            before = code[: call.start()].rstrip()
            used = not (before[-1:] in STATEMENT_ENDS or DISCARDED.search(before))
            if callee in PRIMITIVES:
                kind = PRIMITIVES[callee]
                value = self.constant(argument) if kind == WRITE else None
                found.append(Event(WRITE_LITERAL, value) if value is not None else Event(kind))
            elif callee in ENTER_ROUTINES:
                found.append(Event(ENTER))
            elif callee in (RAISING, LEVELING):
                level = self.constant(argument)
                found += [Event(SAVE)] if used else []
                found.append(Event(RAISE, level))
                if callee == LEVELING:
                    found.append(Event(LOWER, None if level is None else ~(INTERRUPT_MASK & ~level) & WORD_MASK))
            elif callee in in_image:
                found.append(Event(CALLED, name=callee))
            else:
                found += self.events(callee, in_image, active)
        return found


def of_register(event: Event) -> bool:
    """Whether an Event is the register's own, not a call."""
    return event.kind != CALLED


def on_path(event: Event) -> bool:
    """Whether a path (Trace) keeps an Event as one that touches the
    register: the register's own and a call of a MASKING routine."""
    return of_register(event) or event.name in MASKING


def bracketed(events: list, of=lambda event: event, touching=of_register) -> list:
    """Events without the calls before the first and after the last
    that touches the register: what the function calls with the
    register as it left it, and nothing if it never touches it.  of
    gives an item's Event, touching whether an Event touches it."""
    found = [at for at, item in enumerate(events) if touching(of(item))]
    return events[found[0] : found[-1] + 1] if found else []


def listed(events: list[Event]) -> str:
    """Events as a difference prints them."""
    return ", ".join(map(str, events)) or "nothing"


# What a difference names: the two readings', and two paths'.
READ = "status register"
PATH = "status register's path"


def differences(
    image: list[tuple[int, Event]], source: list[Event], what: str = READ, ordered: bool = True
) -> list[str]:
    """What differs between a function's Events in the image and in its
    C: nothing if the two lists are equal, or, not ordered, hold the
    same Events as many times each, else one difference giving both in
    full and the address of the image's first Event that the C's does
    not match, if the C's do not merely run on past them."""
    events = [event for _, event in image]
    if events == source or not ordered and Counter(events) == Counter(source):
        return []
    first = next(
        (at for at, pair in enumerate(zip(events, source)) if pair[0] != pair[1]), min(len(events), len(source))
    )
    where = f", differing from {image[first][0]:#x}" if first < len(image) else ", which go on past the image's"
    return [f"{what}: the image's {listed(events)}; the C's {listed(source)}{where}"]


@dataclass(frozen=True)
class Reading:
    """A function written by hand, read both ways: its name, its start
    in the image, its Steps there (walk), and its Events in the image,
    each with its address, and in its C."""

    name: str
    address: int
    steps: dict[int, Step]
    image: list[tuple[int, Event]]
    written: list[Event]


def located(region: str, conventions: dict[str, dict]) -> dict[int, str]:
    """The sidecar's functions of a region by their starts there."""
    return {
        address: name
        for name, convention in conventions.items()
        for found, address in [parse_address(convention["address"])]
        if found == region
    }


def readings(memory, region: str, conventions: dict[str, dict], source: Source) -> list[Reading]:
    """Each function of a region written by hand, read both ways, by
    address.  conventions is the sidecar's rows by name; region is the
    one its addresses name (image_diff.parse_address), whose code memory
    holds."""
    functions = located(region, conventions)
    in_image = frozenset(conventions)
    found = []
    for address, name in sorted(functions.items()):
        if HAND_WRITTEN not in conventions[name]:
            continue
        steps = walk(memory, address, functions)
        found.append(Reading(name, address, steps, step_events(steps), source.events(name, in_image)))
    return found


def touches(reading: Reading) -> bool:
    """Whether a function touches the status register, in the image or
    in its C."""
    return bool(bracketed(reading.image, lambda item: item[1]) or bracketed(reading.written))


def traced(reading: Reading) -> bool:
    """Whether a function's runs are traced: whether, in the image or in
    its C, it touches the register or calls a MASKING routine."""
    return any(on_path(event) for _, event in reading.image) or any(map(on_path, reading.written))


def reading_differences(reading: Reading, ordered: bool = True) -> list[str]:
    """What differs between a function's Events in the image and in its
    C, each side's calls outside its events dropped; or, not ordered,
    where its paths compare the order and the calls between events, the
    register's own Events alone, as many times each (differences)."""
    if not ordered:
        return differences(
            [(at, event) for at, event in reading.image if of_register(event)],
            list(filter(of_register, reading.written)),
            ordered=False,
        )
    return differences(bracketed(reading.image, lambda item: item[1]), bracketed(reading.written))


def compared(memory, region: str, conventions: dict[str, dict], source: Source) -> list[tuple[str, list[str]]]:
    """Each function of a region written by hand that touches the status
    register, in the image or in its C, and its differences, by
    address (readings)."""
    return [
        (reading.name, reading_differences(reading))
        for reading in readings(memory, region, conventions, source)
        if touches(reading)
    ]


# The opcode words of the instructions that touch the register, as the
# CPU decodes them (CPU32RM, section 4), for a path's reading of each
# instruction run: those combining an immediate word with SR, a move
# from or to SR, its effective address in the low six bits, a move of
# USP either way, its register in the low four, stop, and rte.
COMBINING_OPCODES = {0x007C: RAISE, 0x027C: LOWER, 0x0A7C: TOGGLE}
EFFECTIVE_ADDRESS = 0x3F
MOVE_FROM_SR = 0x40C0
MOVE_TO_SR = 0x46C0
USER_STACK_OPCODES = 0x4E60
USER_STACK_REGISTER_BITS = 0x0F
STOP_OPCODE = 0x4E72
RETURN_FROM_EXCEPTION_OPCODE = 0x4E73
# The calls: jsr, its effective address in the low six bits, and bsr,
# its displacement in the low byte, a word's following for 0 and a
# long's for 0xFF.
JUMP_TO_SUBROUTINE = 0x4E80
BRANCH_TO_SUBROUTINE = 0x6100
DISPLACEMENT_BITS = 0xFF
WORD_DISPLACEMENT = 0
LONG_DISPLACEMENT = 0xFF
# An effective address's mode, its high three bits, and register, its
# low three; mode 7's immediate.
MODE_SHIFT = 3
REGISTER_BITS = 7
OTHER_MODES = 7
IMMEDIATE_OPERAND = OTHER_MODES << MODE_SHIFT | 4
# The words of extension each mode of a jsr's effective address takes
# (CPU32RM, section 2, its brief format alone for an index), mode 7's
# by its register: absolute word, absolute long, PC with displacement,
# PC with index.
MODE_EXTENSIONS = {2: 0, 5: 1, 6: 1}
OTHER_MODE_EXTENSIONS = {0: 1, 1: 2, 2: 1, 3: 1}
WORD = 2
LONG = 4
# The status register's system byte, its trace, supervisor and master
# bits and interrupt mask, by which a path compares what an instruction
# leaves in it: its condition codes are each copy's own.
SYSTEM_BYTE = 0xFF00
# The kinds of the instructions that set the register, whose Event on a
# path is the system byte each leaves (effect).
SETTING = (RAISE, LOWER, TOGGLE, WRITE, WRITE_LITERAL, RETURN_FROM_EXCEPTION)


class Cpu:
    """What Trace reads of the CPU before an instruction: a word of
    memory, a register by name, D0 to D7 and A0 to A7, and the status
    register's system byte as the instruction before left it."""

    def word(self, at: int) -> int:
        raise NotImplementedError

    def register(self, name: str) -> int:
        raise NotImplementedError

    def system_byte(self) -> int:
        raise NotImplementedError


def opcode_event(at: int, cpu: Cpu) -> Event | None:
    """The Event of the instruction at an address as the CPU runs it, or
    None: instruction_event's, read from its words."""
    opcode = cpu.word(at)
    operation = opcode & ~EFFECTIVE_ADDRESS
    if opcode in COMBINING_OPCODES:
        return Event(COMBINING_OPCODES[opcode], cpu.word(at + WORD))
    if opcode == STOP_OPCODE:
        return Event(STOP, cpu.word(at + WORD))
    if opcode == RETURN_FROM_EXCEPTION_OPCODE:
        return Event(RETURN_FROM_EXCEPTION)
    if opcode & ~USER_STACK_REGISTER_BITS == USER_STACK_OPCODES:
        return Event(USER_STACK)
    if operation == MOVE_FROM_SR:
        return Event(SAVE)
    if operation == MOVE_TO_SR:
        if opcode & EFFECTIVE_ADDRESS == IMMEDIATE_OPERAND:
            return Event(WRITE_LITERAL, cpu.word(at + WORD))
        return Event(WRITE)
    return None


def call_length(opcode: int) -> int | None:
    """The length of a call, jsr or bsr, by its opcode word, or None if
    it is no call."""
    if opcode & ~DISPLACEMENT_BITS == BRANCH_TO_SUBROUTINE:
        displacement = opcode & DISPLACEMENT_BITS
        return WORD + {WORD_DISPLACEMENT: WORD, LONG_DISPLACEMENT: LONG}.get(displacement, 0)
    if opcode & ~EFFECTIVE_ADDRESS != JUMP_TO_SUBROUTINE:
        return None
    mode, register = opcode >> MODE_SHIFT & REGISTER_BITS, opcode & REGISTER_BITS
    extensions = OTHER_MODE_EXTENSIONS.get(register) if mode == OTHER_MODES else MODE_EXTENSIONS.get(mode)
    return None if extensions is None else WORD + extensions * WORD


# Where a named routine entered by a jump, not a call, returns to when
# no call of the path's is open: nowhere, the rest of the run its.
NEVER = -1


@dataclass
class Trace:
    """One run's path, read as it runs (instruction, before each
    instruction): its Events, each with its instruction's address, in
    the order run, of the instructions run at the function's own level,
    the code of its calls of functions not named run in its place, as
    the C's static helpers and the image's local subroutines are.  Each
    is what the instruction does (effect): an instruction that sets the
    register is a WRITE, or an rte, of the system byte it leaves, read
    before the next instruction, and a read of it none.  A call of a
    named function, or a jump to one, is a CALLED Event, ENTER for one
    of ENTER_ROUTINES, given the mask mask gives, by its name, its entry
    and SP there, for one of MASK_ARGUMENT, and its body is the
    callee's.

    start is the function's entry; named the functions' names by their
    entries, each copy's; decoded each instruction's Event (opcode_event)
    and its length if it is a call, by its address, which paths of the
    same code may share; seen the addresses run at the function's own
    level, edges each two of them run one after the other.  setting is
    the address and kind of the instruction just run that sets the
    register, None if it was none; calling, the address, return address
    and SP of the call just run, None if it was none; inside, the return
    address of the named callee running, None at the function's own
    level, NEVER for one entered for good; returns, the return address
    of each call of a function not named still open, innermost last.  A
    return is known by its address alone, so that a function called
    again before its call returns, from within its callee, is not read
    rightly."""

    start: int
    named: dict[int, str]
    mask: Callable[[str, int, int], int | None] = lambda name, entry, sp: None
    decoded: dict[int, tuple[Event | None, int | None]] = field(default_factory=dict)
    events: list[tuple[int, Event]] = field(default_factory=list)
    seen: set[int] = field(default_factory=set)
    edges: set[tuple[int, int]] = field(default_factory=set)
    previous: int | None = None
    setting: tuple[int, str] | None = None
    calling: tuple[int, int, int] | None = None
    inside: int | None = None
    returns: list[int] = field(default_factory=list)

    def instruction(self, address: int, cpu: Cpu) -> None:
        """Reads the instruction about to run at an address."""
        self.settled(cpu)
        if self.inside is not None:
            if self.inside != address:
                return
            self.inside = None
        calling, self.calling = self.calling, None
        if self.previous is not None:
            self.edges.add((self.previous, address))
        if address in self.named and (calling is not None or address != self.start):
            self.enter(address, calling)
            return
        if calling is not None:
            self.returns.append(calling[1])
        while self.returns and self.returns[-1] == address:
            self.returns.pop()
        self.seen.add(address)
        self.previous = address
        if address not in self.decoded:
            self.decoded[address] = (opcode_event(address, cpu), call_length(cpu.word(address)))
        event, length = self.decoded[address]
        if event is not None and event.kind in SETTING:
            self.setting = (address, RETURN_FROM_EXCEPTION if event.kind == RETURN_FROM_EXCEPTION else WRITE)
        elif event is not None and event.kind != SAVE:
            self.events.append((address, event))
        if length is not None:
            self.calling = (address, address + length, cpu.register("A7"))

    def settled(self, cpu: Cpu) -> None:
        """Keeps the instruction just run that set the register, if one
        did, with the system byte it left (setting)."""
        if self.setting is not None:
            at, kind = self.setting
            self.events.append((at, Event(kind, cpu.system_byte())))
            self.setting = None

    def enter(self, entry: int, calling: tuple[int, int, int] | None) -> None:
        """Keeps the entry into a named function, called from calling,
        or jumped to for None, and steps over its body."""
        name = self.named[entry]
        value = self.mask(name, entry, calling[2] - LONG) if name in MASK_ARGUMENT and calling else None
        mask = None if value is None else value & INTERRUPT_MASK
        event = Event(ENTER) if name in ENTER_ROUTINES else Event(CALLED, mask, name)
        self.events.append((self.previous if calling is None else calling[0], event))
        if calling is not None:
            self.inside = calling[1]
        else:
            self.inside = self.returns.pop() if self.returns else NEVER


def path_differences(image: Trace, gcc: Trace) -> list[str]:
    """What differs between the paths of a case's two runs, the image's
    copy's and gcc's, each without the calls before its first and after
    its last Event that touches the register (on_path)."""
    return differences(
        bracketed(image.events, lambda item: item[1], on_path),
        [event for _, event in bracketed(gcc.events, lambda item: item[1], on_path)],
        PATH,
    )


@dataclass(frozen=True)
class Arm:
    """Where control may go from a branch of a function's walk whose
    targets lead on to different Events that touch the register, or,
    from within its stretch between such Events, to different calls:
    the branch's address and the target's."""

    branch: int
    target: int


def reachable(steps: dict[int, Step], start: int) -> set[int]:
    """The addresses of a walk's Steps control may reach from an
    address, it among them if it is one."""
    found, pending = set(), [start]
    while pending:
        at = pending.pop()
        if at in found or at not in steps:
            continue
        found.add(at)
        pending.extend(steps[at].successors)
    return found


def arms(steps: dict[int, Step], touching=on_path) -> list[Arm]:
    """A function's Arms, by its walk's Steps, by address: each target
    of a branch of more than one whose targets reach different Events
    that touch the register, or, of a branch control reaches after one
    such Event and that reaches another, different calls."""
    events = {at for at, step in steps.items() if step.event and touching(step.event)}
    calls = {at for at, step in steps.items() if step.event and not touching(step.event)}
    after = set().union(*(reachable(steps, target) for at in events for target in steps[at].successors))
    found = []
    for branch, step in sorted(steps.items()):
        if len(step.successors) < 2:
            continue
        leading = [reachable(steps, target) for target in step.successors]
        differ = len({frozenset(reached & events) for reached in leading}) > 1
        within = (
            branch in after
            and reachable(steps, branch) & events
            and len({frozenset(reached & calls) for reached in leading}) > 1
        )
        if differ or within:
            found += [Arm(branch, target) for target in step.successors]
    return found


@dataclass(frozen=True)
class Coverage:
    """How much of a function's walk its image copy's runs reached: the
    addresses of the Events touching the register (on_path) and the
    Arms none reached, and how many Arms the walk has."""

    events: list[int]
    arms: list[Arm]
    all_arms: int

    def complete(self) -> bool:
        return not self.events and not self.arms


def coverage(steps: dict[int, Step], seen: set[int], edges: set[tuple[int, int]]) -> Coverage:
    """A function's Coverage, by its walk's Steps and what its image
    copy's runs reached, by the walk's addresses (Trace.seen and
    Trace.edges)."""
    found = arms(steps)
    return Coverage(
        sorted(at for at, step in steps.items() if step.event and on_path(step.event) and at not in seen),
        [arm for arm in found if (arm.branch, arm.target) not in edges],
        len(found),
    )
