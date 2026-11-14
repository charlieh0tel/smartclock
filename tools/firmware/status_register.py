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
from dataclasses import dataclass

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
    a constant; a CALLED's name is the function called."""

    kind: str
    value: int | None = None
    name: str | None = None

    def __str__(self) -> str:
        if self.kind == CALLED:
            return f"{self.kind} of {self.name}"
        if self.kind in WITH_LITERAL:
            return f"{self.kind} {'a value not constant' if self.value is None else f'{self.value:#x}'}"
        return self.kind


# The routines whose call enters supervisor mode and returns the SR it
# left in D0, the primary's and the installer's.
ENTER_ROUTINES = ("enter_supervisor", "installer_enter_supervisor")
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


def image_events(memory, start: int, functions: dict[int, str]) -> list[tuple[int, Event]]:
    """A function's Events in the image, each with its instruction's
    address, by address: those of every instruction control reaches from
    start, through its branches and a jmp's table of cases, and its
    calls of functions.  functions is every function's name in the
    memory by its start; a branch or a fixed jmp to one leaves this
    function, the Events there that function's, and one into the middle
    of another function is followed, since code two functions share is
    each one's own."""
    found, events, pending, starts = set(), {}, [start], sorted(functions)
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
            if instruction.mnemonic in CALLS:
                for name in (functions[target] for target in targets if target in functions):
                    event = Event(ENTER) if name in ENTER_ROUTINES else Event(CALLED, name=name)
            elif BRANCH.match(instruction.mnemonic) or (
                instruction.mnemonic == "jmp" and FIXED_JUMP.match(instruction.operands)
            ):
                jumps = [int(target, 16) for target in PC_RELATIVE.findall(instruction.operands)] or targets
                pending.extend(
                    target
                    for target in jumps
                    if target == start or (memory.contains(target) and target not in functions)
                )
            elif instruction.mnemonic == "jmp":
                pending.extend(jump_table(memory, instruction, stop))
            if event is not None:
                events[instruction.address] = event
            if instruction.mnemonic in TERMINALS:
                break
    return sorted(events.items())


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


def bracketed(events: list, of=lambda event: event) -> list:
    """Events without the calls before the first and after the last
    that touches the register: what the function calls with the
    register as it left it, and nothing if it never touches it.  of
    gives an item's Event."""
    touching = [at for at, item in enumerate(events) if of(item).kind != CALLED]
    return events[touching[0] : touching[-1] + 1] if touching else []


def listed(events: list[Event]) -> str:
    """Events as a difference prints them."""
    return ", ".join(map(str, events)) or "nothing"


def differences(image: list[tuple[int, Event]], source: list[Event]) -> list[str]:
    """What differs between a function's Events in the image and in its
    C: nothing if the two lists are equal, else one difference giving
    both in full and the address of the image's first Event that the
    C's does not match, if the C's do not merely run on past them."""
    events = [event for _, event in image]
    if events == source:
        return []
    first = next(
        (at for at, pair in enumerate(zip(events, source)) if pair[0] != pair[1]), min(len(events), len(source))
    )
    where = f", differing from {image[first][0]:#x}" if first < len(image) else ", which go on past the image's"
    return [f"status register: the image's {listed(events)}; the C's {listed(source)}{where}"]


def compared(memory, region: str, conventions: dict[str, dict], source: Source) -> list[tuple[str, list[str]]]:
    """Each function of a region written by hand that touches the status
    register, in the image or in its C, and its differences, by
    address.  conventions is the sidecar's rows by name; region is the
    one its addresses name (image_diff.parse_address), whose code memory
    holds."""
    located = {
        address: name
        for name, convention in conventions.items()
        for found, address in [parse_address(convention["address"])]
        if found == region
    }
    in_image = frozenset(conventions)
    found = []
    for address, name in sorted(located.items()):
        if HAND_WRITTEN not in conventions[name]:
            continue
        image = bracketed(image_events(memory, address, located), lambda item: item[1])
        written = bracketed(source.events(name, in_image))
        if image or written:
            found.append((name, differences(image, written)))
    return found
