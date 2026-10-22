# /// script
# requires-python = ">=3.11"
# dependencies = ["capstone>=5"]
# ///
"""Writes docs/firmware/pforth/IMAGE-words.md, a table of each firmware
image's pForth words, for every image in third_party/firmware/*.bin, and
checks them, or with --write rewrites them.

    uv run -q tools/firmware/pforth_words.py [--write | --sanity]

--sanity prints each image's counts and compares the effects read for
dup, drop, swap, over, rot, +, -, @, !, . and emit with the standard's.

For each image it finds the kernel's dictionary by its shape: the latest
word, `user`, is the one header whose code pushes the header's own
address (`move.l #HEADER,-(a6)`, `rts`); from it the links run down to
a null link.  The diagnostic words are a table of diag_word_entry
records (docs/firmware/structures.md, "The console's dictionary"), the
one run of them ended by a null code whose address the image holds.  An
image where either is not found exactly once, or where a link leaves
the image or loops, is refused, not guessed at.

A kernel word's stack effect is read from its code (an inline word's,
its inline_size bytes): every path through it, into the routines it
calls, counting the A6 data stack's pops, pushes and reaches.  It is "?"
where the paths that return do not agree, or on one of them A6 is
copied, indexed or loaded, or a call goes through a register, or a
jump does.  A path that resets the stacks (abort's movea to A7) does
not return, and what it did to A6 does not count.  A trap is taken to
leave A6 as it was: trap #3's and #4's arguments and results are in D0
to D3, D7, A0 and A1 (docs/firmware/psos.md, "Calls").  A diagnostic word's effect is its
table entry's argument and result counts, which the wrapper
pforth_define_word compiles around its routine pops and pushes.  A
word's description is its routine's note in the names file, in the
Z3805A 3543B's table only, or what docs/firmware/console.md says of
it.  Exits nonzero on drift.

Needs Capstone, which uv installs from the script's dependencies, for the instructions'
lengths and operands.
"""

import argparse
import dataclasses
import pathlib
import struct
import sys

try:
    import capstone
    from capstone import m68k
except ImportError:
    sys.exit('pforth_words.py: needs the python3 capstone module')

ROOT = pathlib.Path(__file__).resolve().parents[2]
IMAGES = ROOT / 'third_party/firmware'
OUTPUT = ROOT / 'docs/firmware/pforth'
NAMES = ROOT / 'tools/ghidra/names/z3805a-3543b.tsv'
# the image the names file's notes are of, the only one they describe
NAMES_IMAGE = 'z3805a-3543b.bin'
SCRIPT = 'tools/firmware/pforth_words.py'

# pforth_word_header: link, code, inline_size, flags, then the name
HEADER = struct.Struct('>IIHH')
# pforth_word_flag's bits, by the names the table gives them
FLAG_NAMES = ((0x01, 'compile-only'), (0x02, 'hidden'), (0x04, 'immediate'),
              (0x08, 'inline'))
FLAG_MASK = 0x0f
FLAG_INLINE = 0x08
# SmartClockSetup.java's PFORTH_NAME_LIMIT: a name's characters at most
NAME_LIMIT = 32
NAME_PAD = 0xff
# printable, not space: what a Forth word's name is made of
NAME_FIRST, NAME_LAST = 0x21, 0x7e
# user's code: move.l #HEADER,-(a6), then rts
PUSH_LONG_A6 = 0x2d3c
RTS = 0x4e75

# diag_word_entry: code, name[26], argument_count, result_count
DIAG_ENTRY = struct.Struct('>I26siI')
# a count past this is not a count: the run is not the table
DIAG_COUNT_LIMIT = 16
# fewer records than this in a run is not the table
DIAG_MINIMUM = 16
# the wrapper pforth_define_word compiles pushes D0 for a result count
# of 1 and D0 and D1 for 2 or more
DIAG_RESULTS_PUSHED = 2

CELL = 4
# a word every path of which resets the stacks, as abort does
NO_RETURN = 'does not return'
# a routine with more paths than this is not straight-line code
STATE_LIMIT = 4096
ACCESS_SIZES = {'b': 1, 'w': 2, 'l': 4}
# the routine's caller's return stack reset: the path does not return
STACK_POINTER = 'a7'
DATA_STACK = 'a6'
# the instructions that read A6 into a data register or only compare it
A6_HARMLESS_READS = ('cmp', 'cmpa', 'tst')
A6_ADJUST = {'addq': 1, 'adda': 1, 'subq': -1, 'suba': -1}
# rtr returns as rts does, past the condition codes a routine pushed
EXITS = ('rts', 'rtr')
CALLS = ('jsr', 'bsr')
JUMPS = ('jmp', 'bra')
# the instructions whose effect on a stack this does not follow
UNREAD = ('rte', 'rtd', 'illegal', 'stop', 'lpstop', 'bgnd')
# the instructions that take A6 whole, its value or a register list
WHOLE_REGISTER = ('link', 'unlk', 'exg', 'movem', 'pea', 'lea', 'movep')
# a register list's bit for A6: bit 0 is D0, bit 8 A0
REGISTER_BITS_A6 = 1 << 14
# the modes whose address register Capstone gives as the operand's reg
REGISTER_FIELD_MODES = (m68k.M68K_AM_REGI_ADDR, m68k.M68K_AM_REGI_ADDR_POST_INC,
                        m68k.M68K_AM_REGI_ADDR_PRE_DEC)
# the longest instruction with the 68020's addressing modes, in bytes
INSTRUCTION_LIMIT = 22

# What docs/firmware/console.md says a word does, where the names file
# has no note for its routine: by section, "The kernel's registers" (the
# user variables), "Leaving it", "Resetting the GPS engine from the
# console" and "Reading the GPS engine from the console".
CONSOLE_DESCRIPTIONS = {
    '!iodev': 'sets iodev, the console\'s device: closes the current one '
              'and opens the new through trap #4',
    'expect': 'reads a line from iodev (I/O function 3)',
    'emit': 'writes a character to iodev (I/O function 4)',
    'type': 'writes a string to iodev (I/O function 4)',
    'pad': 'pushes pad, the user area\'s -0x22',
    'tib': 'pushes tib, the user area\'s -0x1e',
    'fence': 'pushes the address of fence, the user area\'s -0x1a',
    'unfence': 'resets fence',
    'here': 'stores A4 at the user area\'s -0x12 and pushes its address',
    'base': 'pushes the address of base, the user area\'s -0xe',
    'decimal': 'stores 10 in base',
    'hex': 'stores 16 in base',
    'state': 'pushes state, the user area\'s -0xa',
    '[': 'clears state',
    ']': 'sets state',
    'depth': 'the data stack\'s base, the user area\'s -0x8, less A6, '
             'over 4',
    'abort': 'reloads A6 and SP from the user area\'s -0x8 and -0x4',
    ';': 'compiles an rts with move.w #0x4e75,(A4)+',
    ',': 'stores through A4, the dictionary pointer',
    'root': 'pushes the address of console_task_id, the p4th task\'s id',
    'halt': 'leaves the console for SCPI through the hook cell, or trap '
            '#14 when it is zero',
    'watch': 'saves every register to a block, then not traced; the '
             '58503B\'s session left the console with it',
    'master_reset': 'sends the GPS engine @@Cf and the HP defaults '
                    '(script code 0x47)',
    'init_gps': 're-sends the GPS engine\'s fixed option set, with no @@Cf '
                '(script code 0x48)',
    'clear_nv': 'writes 0xff over the first byte of both settings records',
    'wr_eeprom': 'a raw one-byte EEPROM write',
    'disable_gps_cmds': 'sets one flag',
    'gps_change': 'code p1 p2 p3 p4: sends entry code of the Oncore table',
    'abr_stat': '1 prints a gs: line on every @@Ba, 0 stops it',
    'print_bc': 'prints the @@Bk record',
    'ext_msg_rate': '0 asks the GPS engine for @@Bk again',
    'cal': 'prints curr= efc= and tempco =, storing nothing',
}

# Setup's SPELLED: a word's punctuation as its routine's name spells it
SPELLED = {
    '!': 'store', '@': 'fetch', '+': 'plus', '-': 'minus', '*': 'star',
    '/': 'slash', '.': 'dot', '"': 'quote', '\'': 'tick', '(': 'paren',
    ')': 'close_paren', ',': 'comma', ':': 'colon', ';': 'semicolon',
    '<': 'less', '>': 'greater', '=': 'equal', '?': 'query', '#': 'number',
    '[': 'bracket', ']': 'close_bracket', '$': 'dollar', '%': 'percent',
    '&': 'and', '|': 'or', '~': 'tilde', '^': 'caret', '{': 'brace',
    '}': 'close_brace', '\\': 'backslash',
}

# the standard effects of well-known words, in cells, for the sanity check
STANDARD_EFFECTS = {
    'dup': (1, 2), 'drop': (1, 0), 'swap': (2, 2), 'over': (2, 3),
    'rot': (3, 3), '+': (2, 1), '-': (2, 1), '@': (1, 1), '!': (2, 0),
    '.': (1, 0), 'emit': (1, 0),
}


class Refused(Exception):
    """The image's dictionary or table is not where its shape says."""


class Ambiguous(Exception):
    """A routine's stack effect cannot be read from its code."""


@dataclasses.dataclass(frozen=True)
class Word:
    """One word of an image's console."""

    table: str  # 'kernel' or 'diagnostic'
    name: str
    header: int | None  # the header's address; None for one built in RAM
    code: int
    flags: int | None  # pforth_word_flag bits; None for a diagnostic word
    inline_size: int | None
    effect: tuple[int, int] | None  # cells taken and left; None for "?"
    reason: str  # why the effect is "?", or what it is, if not plain


@dataclasses.dataclass(frozen=True)
class Effect:
    """A routine's net effect on the data stack, in bytes from A6 at its
    entry: the deepest byte of its caller's stack it pops, reads or
    writes, and A6 at its return less A6 at entry (positive: popped).
    returns is False when every path resets the stacks (abort)."""

    reach: int
    delta: int
    returns: bool


def u16(image, address):
    return struct.unpack_from('>H', image, address)[0]


def u32(image, address):
    return struct.unpack_from('>I', image, address)[0]


def is_code(image, address):
    return address != 0 and address % 2 == 0 and address < len(image)


def header_name(image, address):
    """A header's name, or None where the bytes after the fixed part are
    not a name padded to even."""
    start = address + HEADER.size
    end = image.find(b'\0', start, start + NAME_LIMIT + 1)
    if end <= start:
        return None
    name = image[start:end]
    if any(not NAME_FIRST <= c <= NAME_LAST for c in name):
        return None
    if (end + 1) % 2 and (end + 1 >= len(image) or image[end + 1] != NAME_PAD):
        return None
    return name.decode('ascii')


def header_at(image, address):
    """The header at address, (link, code, inline_size, flags, name), or
    None where its shape is not a header's."""
    if address % 2 or address + HEADER.size > len(image):
        return None
    link, code, inline_size, flags = HEADER.unpack_from(image, address)
    if link % 2 or link >= len(image) or not is_code(image, code):
        return None
    if flags & ~FLAG_MASK:
        return None
    name = header_name(image, address)
    if name is None:
        return None
    return link, code, inline_size, flags, name


def find_latest(image):
    """The header of `user`, the latest kernel word: the one whose code is
    move.l #HEADER,-(a6), rts."""
    found = []
    for address in range(0, len(image) - 8, 2):
        if u16(image, address) != PUSH_LONG_A6 or u16(image, address + 6) != RTS:
            continue
        header = u32(image, address + 2)
        fields = header_at(image, header)
        if fields is not None and fields[1] == address:
            found.append(header)
    if len(found) != 1:
        raise Refused(f'{len(found)} headers whose code pushes them')
    return found[0]


def kernel_headers(image):
    """The kernel's headers, (address, fields), oldest first."""
    headers = []
    seen = set()
    address = find_latest(image)
    while address:
        if address in seen:
            raise Refused(f'the links loop at {address:#x}')
        seen.add(address)
        fields = header_at(image, address)
        if fields is None:
            raise Refused(f'no header at {address:#x}')
        headers.append((address, fields))
        address = fields[0]
    return headers[::-1]


def diag_entry_at(image, address):
    """The diag_word_entry at address, (code, name, arguments, results),
    or None where its shape is not one's."""
    if address + DIAG_ENTRY.size > len(image):
        return None
    code, raw, arguments, results = DIAG_ENTRY.unpack_from(image, address)
    if not is_code(image, code):
        return None
    name, nul, padding = raw.partition(b'\0')
    if not name or not nul or padding.strip(b'\0'):
        return None
    if any(not NAME_FIRST <= c <= NAME_LAST for c in name):
        return None
    if not (0 <= arguments < DIAG_COUNT_LIMIT and 0 <= results < DIAG_COUNT_LIMIT):
        return None
    return code, name.decode('ascii'), arguments, results


def diag_entries(image):
    """The diagnostic word table: the one run of entries ended by a null
    code whose address the image holds."""
    runs = []
    for start in range(0, len(image) - DIAG_ENTRY.size, 2):
        if diag_entry_at(image, start) is None:
            continue
        if start >= DIAG_ENTRY.size and diag_entry_at(image, start - DIAG_ENTRY.size):
            continue
        entries = []
        address = start
        while (entry := diag_entry_at(image, address)) is not None:
            entries.append(entry)
            address += DIAG_ENTRY.size
        if len(entries) < DIAG_MINIMUM or u32(image, address) != 0:
            continue
        if image.count(struct.pack('>I', start)) == 0:
            continue
        runs.append((start, entries))
    if len(runs) != 1:
        raise Refused(f'{len(runs)} diagnostic word tables')
    return runs[0]




@dataclasses.dataclass(frozen=True)
class Flow:
    """Where a path goes after an instruction: to successors, into a
    routine and back to following, or out of the routine."""

    successors: tuple[int, ...] = ()
    following: int | None = None  # set for a call: where it returns to
    call: int | None = None  # the callee, None where a register holds it
    returns: bool = False  # an rts


class Reader:
    """Reads routines' stack effects from an image, remembering each
    routine's.  A path's state is A6's offset from its value at the
    routine's entry, in bytes, or Unknown once it cannot be followed."""

    def __init__(self, image):
        self.image = image
        self.disassembler = capstone.Cs(
            capstone.CS_ARCH_M68K,
            capstone.CS_MODE_BIG_ENDIAN | capstone.CS_MODE_M68K_040)
        self.disassembler.detail = True
        # routine address: its Effect, the Ambiguous it raised, or None
        # while it is being read
        self.effects = {}
        self.instructions = {}  # address: the instruction there

    def instruction(self, address):
        if address not in self.instructions:
            if not is_code(self.image, address):
                raise Ambiguous(f'code outside the image at {address:#x}')
            found = next(self.disassembler.disasm(
                self.image[address:address + INSTRUCTION_LIMIT], address), None)
            if found is None:
                raise Ambiguous(f'no instruction at {address:#x}')
            self.instructions[address] = found
        return self.instructions[address]

    def routine(self, address):
        """The effect of the routine at address, as jsr or bsr runs it."""
        if address in self.effects:
            effect = self.effects[address]
            if effect is None:
                raise Ambiguous(f'recursion into {address:#x}')
            if isinstance(effect, Ambiguous):
                raise effect
            return effect
        self.effects[address] = None
        try:
            effect = self.walk(address, None)
        except Ambiguous as error:
            self.effects[address] = error
            raise
        self.effects[address] = effect
        return effect

    def walk(self, entry, end):
        """Every path from entry to an rts or, for an inline word, to end,
        the byte after its code copied."""
        seen = {}  # address: A6's offset there
        exits = set()  # A6's offsets at the returns
        reach = 0
        work = [(entry, 0)]
        while work:
            address, offset = work.pop()
            if end is not None and address >= end:
                if address != end:
                    raise Ambiguous(f'a branch past the inline code at {address:#x}')
                exits.add(known(offset, address))
                continue
            if address in seen:
                if not same(seen[address], offset):
                    raise Ambiguous(f'A6 differs where paths join at {address:#x}')
                continue
            seen[address] = offset
            if len(seen) > STATE_LIMIT:
                raise Ambiguous('too many instructions')
            offset, reach, flow = self.step(self.instruction(address), offset, reach)
            if flow.returns:
                exits.add(known(offset, address))
            elif flow.following is not None:
                offset, reach, returns = self.call(flow.call, offset, reach)
                if returns:
                    work.append((flow.following, offset))
            else:
                work.extend((successor, offset) for successor in flow.successors)
        if len(exits) > 1:
            raise Ambiguous('A6 differs at the returns')
        if not exits:
            return Effect(reach, 0, False)
        return Effect(reach, exits.pop(), True)

    def call(self, callee, offset, reach):
        """A call: A6's offset after it, the reach, and whether it returns.
        A callee read as "?" leaves A6 unknown, which matters only if the
        path returns; a call through a register is one such callee."""
        if callee is None:
            return Unknown('a call through a register'), reach, True
        try:
            effect = self.routine(callee)
        except Ambiguous as error:
            return Unknown(f'through {callee:#x}, {error}'), reach, True
        if not effect.returns:
            return offset, reach, False
        if isinstance(offset, Unknown):
            return offset, reach, True
        return offset + effect.delta, max(reach, offset + effect.reach), True

    def step(self, instruction, offset, reach):
        """One instruction: A6's offset after it, the reach, its Flow."""
        mnemonic, _, suffix = instruction.mnemonic.partition('.')
        operands = instruction.operands
        address = instruction.address
        following = address + instruction.size
        if mnemonic in EXITS:
            return offset, reach, Flow(returns=True)
        if mnemonic in UNREAD:
            raise Ambiguous(f'{mnemonic} at {address:#x}')
        if mnemonic in WHOLE_REGISTER and self.names_a6(instruction):
            if mnemonic == 'lea' and self.lea_a6(instruction):
                offset = moved(offset, operands[0].mem.disp)
                return offset, extend(reach, offset, 0), Flow((following,))
            if mnemonic == 'movem':
                offset, reach = self.movem(instruction, suffix, offset, reach)
                return offset, reach, Flow((following,))
            return Unknown(f'{mnemonic} with A6 at {address:#x}'), reach, Flow((following,))
        size = ACCESS_SIZES.get(suffix)
        for index, operand in enumerate(operands):
            is_destination = len(operands) > 1 and index == len(operands) - 1
            if self.is_register(instruction, operand, DATA_STACK):
                if mnemonic in A6_HARMLESS_READS:
                    continue
                if is_destination:
                    offset = self.a6_written(instruction, mnemonic, offset)
                    reach = extend(reach, offset, 0)
                elif not self.into_data_register(instruction):
                    offset = Unknown(f'A6 copied at {address:#x}')
            elif operand.type == m68k.M68K_OP_MEM:
                offset, reach = self.access(instruction, operand, size, offset, reach)
        if mnemonic in ('move', 'movea') and self.is_register(instruction, operands[-1],
                                                             STACK_POINTER):
            # the return stack reset: abort's way, which does not return
            return offset, reach, Flow()
        if mnemonic in CALLS:
            return offset, reach, Flow(call=self.target(instruction), following=following)
        if mnemonic in JUMPS:
            target = self.target(instruction)
            if target is None:
                raise Ambiguous(f'{instruction.mnemonic} through a register at {address:#x}')
            return offset, reach, Flow((target,))
        if any(o.type == m68k.M68K_OP_BR_DISP for o in operands):
            return offset, reach, Flow((following, self.target(instruction)))
        return offset, reach, Flow((following,))

    def movem(self, instruction, suffix, offset, reach):
        """movem: through A6, a pop, push or reach of every register in
        its list; A6 in a list loaded from memory, A6 lost."""
        registers, memory = instruction.operands
        loads = registers.type != m68k.M68K_OP_REG_BITS
        if loads:
            registers, memory = memory, registers
        if loads and registers.register_bits & REGISTER_BITS_A6:
            return Unknown(f'movem to A6 at {instruction.address:#x}'), reach
        size = ACCESS_SIZES.get(suffix)
        if size is not None:
            size *= registers.register_bits.bit_count()
        return self.access(instruction, memory, size, offset, reach)

    def target(self, instruction):
        """A jump's or call's destination, or None where a register holds it."""
        operand = instruction.operands[-1]
        if operand.type == m68k.M68K_OP_BR_DISP:
            return instruction.address + 2 + operand.br_disp.disp
        mode = operand.address_mode
        if mode == m68k.M68K_AM_PCI_DISP:
            return instruction.address + 2 + operand.mem.disp
        if mode in (m68k.M68K_AM_ABSOLUTE_DATA_LONG, m68k.M68K_AM_ABSOLUTE_DATA_SHORT):
            return operand.imm
        return None

    @staticmethod
    def register(instruction, register):
        return instruction.reg_name(register) if register else None

    def is_register(self, instruction, operand, name):
        return (operand.type == m68k.M68K_OP_REG
                and self.register(instruction, operand.reg) == name)

    def base(self, instruction, operand):
        """The address register a memory operand goes through, or None."""
        if operand.type != m68k.M68K_OP_MEM:
            return None
        if operand.address_mode in REGISTER_FIELD_MODES:
            return self.register(instruction, operand.reg)
        return self.register(instruction, operand.mem.base_reg)

    def names_a6(self, instruction):
        for operand in instruction.operands:
            if operand.type == m68k.M68K_OP_REG_BITS:
                if operand.register_bits & REGISTER_BITS_A6:
                    return True
            elif (self.base(instruction, operand) == DATA_STACK
                  or self.is_register(instruction, operand, DATA_STACK)):
                return True
        return False

    def lea_a6(self, instruction):
        """lea d(a6),a6: A6 moved by d."""
        source, destination = instruction.operands
        return (source.address_mode == m68k.M68K_AM_REGI_ADDR_DISP
                and self.base(instruction, source) == DATA_STACK
                and self.is_register(instruction, destination, DATA_STACK))

    def a6_written(self, instruction, mnemonic, offset):
        """A6 as an instruction's destination: moved by an immediate, or
        loaded from elsewhere."""
        source = instruction.operands[0]
        if mnemonic in A6_ADJUST and source.type == m68k.M68K_OP_IMM:
            return moved(offset, A6_ADJUST[mnemonic] * source.imm)
        return Unknown(f'{instruction.mnemonic} to A6 at {instruction.address:#x}')

    def into_data_register(self, instruction):
        """A6 read into a data register, where nothing reaches the stack
        through it."""
        destination = instruction.operands[-1]
        return (destination.type == m68k.M68K_OP_REG
                and self.register(instruction, destination.reg).startswith('d'))

    def access(self, instruction, operand, size, offset, reach):
        """A memory operand: a pop, push or reach through A6, if it is one."""
        address = instruction.address
        if self.base(instruction, operand) != DATA_STACK:
            if (operand.address_mode not in REGISTER_FIELD_MODES
                    and self.register(instruction, operand.mem.index_reg) == DATA_STACK):
                return Unknown(f'A6 as an index at {address:#x}'), reach
            return offset, reach
        mode = operand.address_mode
        if size is None:
            return Unknown(f'{instruction.mnemonic} through A6 at {address:#x}'), reach
        if mode == m68k.M68K_AM_REGI_ADDR_POST_INC:
            offset = moved(offset, size)
            return offset, extend(reach, offset, 0)
        if mode == m68k.M68K_AM_REGI_ADDR_PRE_DEC:
            return moved(offset, -size), reach
        if mode == m68k.M68K_AM_REGI_ADDR:
            return offset, extend(reach, offset, size)
        if mode == m68k.M68K_AM_REGI_ADDR_DISP:
            return offset, extend(reach, offset, operand.mem.disp + size)
        return Unknown(f'A6 indexed at {address:#x}'), reach


@dataclasses.dataclass(frozen=True)
class Unknown:
    """A6 on a path where its offset can no longer be followed, and why:
    harmless on a path that then resets the stacks, "?" on one that
    returns.  Any two are the same state where paths join."""

    why: str


def known(offset, address):
    """A6's offset where a path returns: "?" if it is unknown."""
    if isinstance(offset, Unknown):
        raise Ambiguous(offset.why)
    return offset


def same(first, second):
    if isinstance(first, Unknown) or isinstance(second, Unknown):
        return isinstance(first, Unknown) and isinstance(second, Unknown)
    return first == second


def moved(offset, by):
    return offset if isinstance(offset, Unknown) else offset + by


def extend(reach, offset, end):
    """The reach, with an access from offset to offset + end."""
    return reach if isinstance(offset, Unknown) else max(reach, offset + end)


def kernel_effect(reader, code, flags, inline_size):
    """A kernel word's effect in cells, (taken, left), and a note: why it
    is "?" (None), or that it does not return."""
    inline = flags & FLAG_INLINE and inline_size
    try:
        effect = reader.walk(code, code + inline_size) if inline else reader.routine(code)
    except Ambiguous as error:
        return None, str(error)
    if not effect.returns:
        return None, NO_RETURN
    if effect.delta % CELL:
        return None, f'A6 moved by {effect.delta} bytes'
    taken = -(-max(effect.reach, effect.delta, 0) // CELL)
    return (taken, taken - effect.delta // CELL), ''


def image_words(path):
    """An image's words, kernel then diagnostic, each in its table's order."""
    image = path.read_bytes()
    reader = Reader(image)
    words = []
    for address, (_, code, inline_size, flags, name) in kernel_headers(image):
        effect, reason = kernel_effect(reader, code, flags, inline_size)
        words.append(Word('kernel', name, address, code, flags, inline_size, effect, reason))
    _, entries = diag_entries(image)
    for code, name, arguments, results in entries:
        left = min(results, DIAG_RESULTS_PUSHED)
        reason = '' if left == results else f'result count {results}'
        words.append(Word('diagnostic', name, None, code, None, None, (arguments, left), reason))
    return words


def spell(word):
    """Setup's spell(): a word as an identifier, its punctuation spelled."""
    name = ''
    for character in word:
        spelled = SPELLED.get(character)
        if spelled is None:
            name += character
            continue
        if name and not name.endswith('_'):
            name += '_'
        name += spelled + '_'
    return name.removesuffix('_')


def names_notes():
    """The names file's function notes, by function name."""
    notes = {}
    for line in NAMES.read_text().splitlines():
        if line.startswith('#') or not line.strip():
            continue
        fields = line.split('\t')
        if len(fields) > 4 and fields[1] == 'function' and fields[4]:
            notes[fields[2]] = fields[4]
    return notes


def description(word, notes):
    prefix = 'forth_' if word.table == 'kernel' else 'diag_'
    return notes.get(prefix + spell(word.name)) or CONSOLE_DESCRIPTIONS.get(word.name, '')


def model(path):
    """An image's model and revision, as models.md heads its columns."""
    name, _, revision = path.stem.partition('-')
    return f'{name.upper()} {revision.upper()}'


def cell(text):
    return text.replace('|', '\\|')


def code_span(text):
    if '`' in text:
        return cell(f'`` {text} ``')
    return cell(f'`{text}`')


def flag_names(flags):
    return ', '.join(name for bit, name in FLAG_NAMES if flags & bit)


def stack(word):
    if word.effect is None:
        return word.reason if word.reason == NO_RETURN else f'? ({word.reason})'
    taken, left = word.effect
    note = f' ({word.reason})' if word.reason else ''
    return f'{taken} -- {left}{note}'


def render(path, words, notes):
    """The tables for one image, as IMAGE-words.md holds them."""
    kernel = [w for w in words if w.table == 'kernel']
    diagnostic = [w for w in words if w.table == 'diagnostic']
    read = sum(1 for w in kernel if w.effect is not None)
    lines = [
        f'# pForth words: {model(path)}',
        '',
        f'Generated by `{SCRIPT}` from',
        f'`third_party/firmware/{path.name}`; do not edit.  `make pforth-words`',
        'rewrites it and `make check-pforth-words` fails when it and the image',
        'disagree.  `README.md` here says how the tables are read.',
        '',
        f'{len(kernel)} kernel words, {read} of them with a stack effect read',
        f'from their code, and {len(diagnostic)} diagnostic words, whose effects',
        'are their table entries\' counts.  Stack is cells taken -- cells left.',
        '',
        '## The kernel words',
        '',
        'Oldest first, as the links run back from `user`.',
        '',
        '| Word | Header | Code | Flags | Inline | Stack | Description |',
        '| --- | --- | --- | --- | --- | --- | --- |',
    ]
    for word in kernel:
        inline = str(word.inline_size) if word.inline_size else ''
        lines.append(f'| {code_span(word.name)} | `{word.header:#x}` | `{word.code:#x}` '
                     f'| {flag_names(word.flags)} | {inline} | {cell(stack(word))} '
                     f'| {cell(description(word, notes))} |')
    lines += [
        '',
        '## The diagnostic words',
        '',
        'In the table\'s order; their headers are built in RAM, with no flags.',
        '',
        '| Word | Code | Stack | Description |',
        '| --- | --- | --- | --- |',
    ]
    for word in diagnostic:
        lines.append(f'| {code_span(word.name)} | `{word.code:#x}` | {cell(stack(word))} '
                     f'| {cell(description(word, notes))} |')
    return '\n'.join(lines) + '\n'


def output_path(path):
    return OUTPUT / f'{path.stem}-words.md'


def sanity(words):
    """Each well-known word's effect read and standard: (name, read,
    standard), read None for "?"."""
    by_name = {w.name: w for w in words if w.table == 'kernel'}
    return [(name, by_name[name].effect, standard)
            for name, standard in STANDARD_EFFECTS.items() if name in by_name]


def report(path, words):
    """--sanity's lines for one image."""
    kernel = [w for w in words if w.table == 'kernel']
    read = sum(1 for w in kernel if w.effect is not None)
    print(f'{path.name}: {len(kernel)} kernel words, {read} read, '
          f'{len(kernel) - read} not; {len(words) - len(kernel)} diagnostic')
    for name, read_effect, standard in sanity(words):
        verdict = 'agrees' if read_effect == standard else 'DIFFERS'
        print(f'  {name}: read {read_effect}, standard {standard}: {verdict}')


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--write', action='store_true', help='rewrite the tables')
    parser.add_argument('--sanity', action='store_true',
                        help='compare well-known words\' effects with the standard\'s')
    arguments = parser.parse_args()
    notes = names_notes()
    drift = []
    for path in sorted(IMAGES.glob('*.bin')):
        try:
            words = image_words(path)
        except Refused as error:
            sys.exit(f'{path.name}: refused: {error}')
        if arguments.sanity:
            report(path, words)
            continue
        text = render(path, words, notes if path.name == NAMES_IMAGE else {})
        output = output_path(path)
        if arguments.write:
            output.write_text(text)
        elif not output.exists() or output.read_text() != text:
            drift.append(output.relative_to(ROOT))
    for path in drift:
        print(f'{path}: differs from the image; make pforth-words rewrites it')
    return 1 if drift else 0


if __name__ == '__main__':
    sys.exit(main())
