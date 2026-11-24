"""Every path by which a SmartClock image sends its GPS engine a command,
to set a value or to ask one: what gps_fields.py takes a command
field's setters from, and the table of them it keeps in
docs/firmware/gps.md ("Every path to a setting").

A path is one of PATH_KINDS:

    script     a script of the image's GPS task: each step a request
    list       a list of queries, the command monitor's
    sequence   a function of the loop task that posts a request through
               gpsx_send with a constant code and mode: the start-up,
               leap second, self-test and diagnostic sequences
    monitor    a function of the GPS task that posts a request through
               gps_request_post with a constant code
    SCPI       a SCPI descriptor whose flags send its request to the GPS,
               which pll_scpi_send_to_loop posts in mode 0
    console    a console word that leaves a code in gps_command, which
               gps_command_send maps to a request and posts

Each request is a code and an argument mode.  On an Oncore image a code
below the message table's length is the message the table holds there;
on a Furuno image the code is first translated
(gps_request_translations) and a translated code of the sentence
descriptors (gps_sentence_descriptors) is that sentence, set's or int's
by its parameter.  Any other code is a script of the image, or a step,
which the GPS task does itself and which sends nothing.  A code is named
as the image's gps_request_code names it (types/smartclock.h).

How each is found:

- Scripts and lists: an Oncore image's from dump-oncore's text
  (docs/firmware/oncore/IMAGE.txt); a Furuno image's from the tables
  names.tsv labels gps_script_XX and command_monitor_queries.
- Sequences and monitors: a scan of the primary's instructions for the
  calls of gpsx_send and gps_request_post, noting the values pushed.  A
  call whose code is not a constant is gps_command_send's, the console
  path; gps_request_post's POST_STEP posts a step of the command
  monitor's list, which is its own path.
- SCPI: a descriptor is a ROM address an instruction names, or that
  names.tsv labels scpi_param_*, whose bytes are a descriptor's: its type
  one that sends a request, its flags no more than PLLC_FLAGS, its
  NV widths in ROM and its value in RAM (rewritten/common/types/scpi.h,
  scpi_setting_param.h).  A descriptor only a ROM table names, and no
  instruction or label, is missed.  It is named by its label, its
  command, or by its address.
- Console: each function with an instruction that moves a constant to
  gps_command's code is a word, named as its function less DIAG_PREFIX;
  gps_command_send is run (mc683xx's CPU32) on each code found, with gpsx_send
  answering at once, and the request it posts noted.  gps_change and
  gps_query post the request the console gives: any.

A request is refused where the GPS task does not take it: the Z3817A's
gps_request_admitted takes a sequence's, a setting's or the console's
(senders 1 to 3) only if gps_admitted_requests has its code.  A script
is refused where the image keeps such a list, its code is not on it and
no monitor posts it.
"""

import bisect
import re
import struct
from dataclasses import dataclass

import mc683xx

import record_access
from loop_trace import CALL_LIMIT, RAM, RAM_SIZE, RETURN, ROM_SIZE, STACK, SUPERVISOR, globals_of, names

ROOT = record_access.ROOT
FIRMWARE = ROOT / 'third_party/firmware'
ONCORE = ROOT / 'docs/firmware/oncore'

# The images: those with an Oncore message table, a dump of it and
# rewritten C, and the two whose engine is a Furuno, each with its sentence
# descriptors' first code and their count (the label's type in names.tsv).
ONCORE_IMAGES = ('z3805a-3543b', 'z3801a-3543', '58503a-3704', 'z3817a-3752', 'z3816a-4001', '58503b-1.01.04')
NMEA_IMAGES = {'z3815a-4010': (0x39, 20), 'z3805a-1.01.03': (0x40, 22)}
PATH_IMAGES = ONCORE_IMAGES + tuple(NMEA_IMAGES)
PATH_KINDS = ('script', 'list', 'sequence', 'monitor', 'SCPI', 'console')
SCRIPT, LIST, SEQUENCE, MONITOR, SCPI, CONSOLE = PATH_KINDS

REQUEST_NONE = 0xFF
VECTOR_SIZE = 4
# The Furuno images' tables: an nmea translation, a code and a mode, the
# code it is sent as and its mode; a descriptor, its sentence's address
# first (rewritten/z3815a-4010/types/smartclock.h).
TRANSLATIONS = 'gps_request_translations'
TRANSLATION = 8
DESCRIPTORS = 'gps_sentence_descriptors'
DESCRIPTOR = 0x20
NMEA_SCRIPT = re.compile(r'gps_script_([0-9a-f]+)')
NMEA_LIST = 'command_monitor_queries'
# The parameterized sentences, by their parameter: set's letter, int's
# sentence name.
PARAMETERS = {'set': 1, 'int': 3}

# The posting functions, and where each takes its code and mode among
# its arguments, the first pushed last; gps_request_post's mode is the
# step's or the script's own.
GPSX_SEND = 'gpsx_send'
REQUEST_POST = 'gps_request_post'
POSTERS = {GPSX_SEND: (0, 1), REQUEST_POST: (1, None)}
# gps_request_post's code for a step of the command monitor's list
# (rewritten/z3817a-3752/gps_requests.c, gps_request_post).
POST_STEP = 4
CONSOLE_SENDER = 'gps_command_send'

# A SCPI descriptor: of each type that sends a request, where its flags
# word is, the request byte after it, and where its value pointer is
# (types/scpi.h, scpi_setting_param.h); the flags it may have
# (rewritten/common/pllc.h), the one that sends its request, and the mode
# pll_scpi_send_to_loop sends it in.
SCPI_LABEL = re.compile(r'scpi_param_(\w+)')
DESCRIPTOR_LAYOUT = {0: (0x1E, 0x16), 1: (0x14, 0x0C), 3: (0x1E, 0x16)}
DESCRIPTOR_WIDTHS = 2
PLLC_FLAGS = 7
SEND_TO_GPS = 1
SCPI_MODE = 0
ROM_FLOOR = 0x10000

# The console: gps_command's code at its start and the request
# gps_console_command's change and query take at +0x12
# (rewritten/common/types/oncore_receiver_status.h); the codes there are
# (CONSOLE_CODES); a word's function's prefix; and what is left as the
# request, to tell the request posted is the console's.
COMMAND_REQUEST = 0x12
CONSOLE_CODES = 0x40
DIAG_PREFIX = 'diag_'
ANY = 0x7AB1
# What gpsx_send is made to do in the run: moveq #1,%d0; rts.
ANSWER = bytes.fromhex('70014e75')
# gpsx_send's frame at entry: the return address, then the code word and
# the mode byte, the high byte of its word.
FRAME_CODE = 4
FRAME_MODE = 6

# The codes the GPS task takes from senders 1 to 3, where the image keeps
# a list of them, a word each, to REQUEST_NONE (rewritten/z3817a-3752/
# gps_requests.c, gps_request_admitted).
ADMITTED = 'gps_admitted_requests'


@dataclass(frozen=True)
class Target:
    """What a request sends: a message, a script, a step, or any request
    the console gives.  A message is an Oncore ID, or a Furuno
    sentence's name with set's or int's parameter as its key."""

    kind: str
    message: str
    key: str | None = None
    name: str = ''

    def shown(self) -> str:
        if self.kind == 'message':
            if len(self.message) == 2:
                return f'`@@{self.message}`'
            address = self.message + (f',{self.key}' if self.key else '')
            return f'`$PFEC,GP{address}`' if self.message[0].islower() else f'`$GP{address}`'
        if self.kind == 'any':
            return 'any request'
        return f'{self.kind} `{self.name}`'


@dataclass(frozen=True)
class Path:
    """One way an image sends its engine a request: its kind, its name,
    what it sends, the mode (None where the target's steps carry their
    own) and whether the GPS task refuses it."""

    image: str
    kind: str
    name: str
    target: Target
    mode: int | None
    refused: bool


def labels(image: str) -> dict[str, int]:
    """The primary's labels by name, from names.tsv."""
    found = {}
    for line in (FIRMWARE / 'analysis' / image / 'names.tsv').read_text().splitlines():
        cells = line.split('\t')
        if line.startswith('#') or len(cells) < 3 or cells[1] != 'label' or '::' in cells[0]:
            continue
        found.setdefault(cells[2], int(cells[0], 16))
    return found


def code_names(image: str) -> dict[int, str]:
    """Each request code by its name in gps_request_code."""
    text = (FIRMWARE / 'rewritten' / image / 'types' / 'smartclock.h').read_text()
    body = text.partition('gps_request_code {')[2].partition('} gps_request_code;')[0]
    return {int(code, 16): name for name, code in re.findall(r'^\s+(\w+) = (0x[0-9a-f]+),', body, re.MULTILINE)}


def admitted(image: str, rom: bytes) -> set[int] | None:
    """The codes the image's GPS task takes from senders 1 to 3, None for
    any (ADMITTED)."""
    at = labels(image).get(ADMITTED)
    if at is None:
        return None
    codes = set()
    while (code := struct.unpack_from('>H', rom, at)[0]) != REQUEST_NONE:
        codes.add(code)
        at += 2
    return codes


class Family:
    """How an image's request codes become targets, and its scripts and
    lists as steps: (code, mode) each."""

    def __init__(self, image: str, rom: bytes) -> None:
        self.image = image
        self.rom = rom
        self.names = code_names(image)
        self.messages: dict[int, str] = {}
        self.scripts: dict[int, list[tuple[int, int]]] = {}
        self.lists: dict[str, list[tuple[int, int]]] = {}
        self.translations: dict[tuple[int, int], tuple[int, int]] = {}
        self.sentences: dict[int, str] = {}
        if image in NMEA_IMAGES:
            self.read_nmea()
        else:
            self.read_oncore()

    def long(self, at: int) -> int:
        return struct.unpack_from('>I', self.rom, at)[0]

    def read_oncore(self) -> None:
        ids: dict[str, int] = {}
        for line in (ONCORE / f'{self.image}.txt').read_text().splitlines():
            entry = re.match(r'message (0x[0-9a-f]+) (\w\w) ', line)
            if entry:
                self.messages[int(entry[1], 16)] = entry[2]
                ids[entry[2]] = int(entry[1], 16)
                continue
            steps = [(ids[sent], int(mode)) for sent, mode in re.findall(r' (\w\w)/(\d)', line)]
            if line.startswith('script '):
                self.scripts[int(line.split()[1], 16)] = steps
            elif line.startswith('list '):
                self.lists[line.split()[1]] = steps

    def read_nmea(self) -> None:
        first, count = NMEA_IMAGES[self.image]
        found = labels(self.image)
        at = found[TRANSLATIONS]
        while (entry := struct.unpack_from('>HBxHBx', self.rom, at))[0] != REQUEST_NONE:
            self.translations.setdefault(entry[:2], entry[2:])
            at += TRANSLATION
        for index in range(count):
            address = self.long(found[DESCRIPTORS] + index * DESCRIPTOR)
            self.sentences[first + index] = self.rom[address : self.rom.index(0, address)].decode('ascii')
        for name, at in found.items():
            script = NMEA_SCRIPT.fullmatch(name)
            if not script and name != NMEA_LIST:
                continue
            steps = []
            while step := self.long(at):
                at += VECTOR_SIZE
                # gps_script_message's step is in RAM, a request's own.
                if step < len(self.rom):
                    steps.append(struct.unpack_from('>HB', self.rom, step))
            if script:
                self.scripts[int(script[1], 16)] = steps
            else:
                self.lists[name] = steps

    def target(self, code: int, mode: int | None) -> tuple[Target, int | None]:
        """What a request sends, and the mode it is sent in."""
        if mode is not None:
            code, mode = self.translations.get((code, mode), (code, mode))
        if code in self.messages:
            return Target('message', self.messages[code]), mode
        if code in self.sentences:
            name, _, parameter = self.sentences[code].partition(',')
            key = parameter[: PARAMETERS[name]] if name in PARAMETERS else None
            return Target('message', name, key), mode
        named = self.names.get(code, f'0x{code:02x}')
        script = code in self.scripts or 'SCRIPT' in named
        return Target(SCRIPT if script else 'step', '', None, named), None

    def script_name(self, code: int) -> str:
        return self.names.get(code, f'0x{code:02x}')


class Posts(record_access.Scan):
    """record_access's scan, noting at each call of a poster the values
    its arguments were pushed with, the first first, None where the scan
    does not follow one: (poster, function, values)."""

    def __init__(self, functions: dict[int, str], posters: dict[int, str]) -> None:
        super().__init__(functions, 0, 0, {})
        self.posters = posters
        self.function: int | None = None
        self.pushes: list[int | None] = []
        self.found: list[tuple[str, str, tuple[int | None, ...]]] = []

    def step(self, function: int, address: int, mnemonic: str, operands: list[str]) -> None:
        if function != self.function:
            self.function, self.pushes = function, []
        pushed = None
        if len(operands) == 2 and operands[1] == '%sp@-':
            pushed = [self.value(operands[0])]
        elif operands == ['%sp@-'] and mnemonic.startswith('clr'):
            pushed = [0]
        elif mnemonic == 'pea':
            pushed = [None]
        callee = None
        if mnemonic.startswith(('jsr', 'bsr')):
            target = record_access.ABSOLUTE.fullmatch(operands[0]) or re.fullmatch(r'%pc@\((0x[0-9a-f]+)\)', operands[0])
            register = re.fullmatch(r'%(a\d)@', operands[0])
            callee = int(target[1], 16) if target else self.registers.get(register[1]) if register else None
        super().step(function, address, mnemonic, operands)
        self.pushes += pushed or []
        if callee is not None:
            if callee in self.posters:
                self.found.append((self.posters[callee], self.functions[function], tuple(reversed(self.pushes))))
            self.pushes = []


def numbers(operands: str) -> list[int]:
    return [int(decimal) if decimal else int(hexadecimal, 16) for decimal, hexadecimal in re.findall(r'#(-?\d+)|\b0x([0-9a-f]+)\b', operands)]


def descriptors(image: str, rom: bytes, instructions: list[tuple[int, str, str]]) -> dict[int, str]:
    """The SCPI descriptors that send a request, each by its name."""
    named = {address: name for name, address in labels(image).items()}
    candidates = {at for at, name in named.items() if SCPI_LABEL.fullmatch(name)}
    candidates |= {value for _, _, operands in instructions for value in numbers(operands) if ROM_FLOOR <= value < len(rom)}
    found = {}
    for at in sorted(candidates):
        if at + DESCRIPTOR > len(rom) or rom[at] not in DESCRIPTOR_LAYOUT:
            continue
        flags_at, value_at = DESCRIPTOR_LAYOUT[rom[at]]
        flags, code = struct.unpack_from('>HB', rom, at + flags_at)
        value = struct.unpack_from('>I', rom, at + value_at)[0]
        widths = struct.unpack_from('>I', rom, at + DESCRIPTOR_WIDTHS)[0]
        if flags & ~PLLC_FLAGS or not RAM <= value < RAM + RAM_SIZE or not ROM_FLOOR <= widths < len(rom):
            continue
        if flags & SEND_TO_GPS and code != REQUEST_NONE:
            label = SCPI_LABEL.fullmatch(named.get(at, ''))
            found[at] = command(label[1]) if label else f'descriptor 0x{at:x}'
    return found


def command(label: str) -> str:
    """A SCPI command from its descriptor's label: its keywords, and any
    lower-case part after them, which names one of its values."""
    keywords = [part for part in label.split('_') if part[:1].isupper()]
    rest = [part for part in label.split('_') if not part[:1].isupper()]
    return ':' + ':'.join(keywords) + (f" ({' '.join(rest)})" if rest else '')


def console_words(image: str, instructions: list[tuple[int, str, str]]) -> dict[int, set[str]]:
    """The console words by the code each leaves in gps_command."""
    code_at = globals_of(image)['gps_command']
    functions = record_access.primary_functions(image)
    starts = sorted(functions)
    found: dict[int, set[str]] = {}
    for address, mnemonic, operands in instructions:
        parts = record_access.split_operands(operands)
        if not mnemonic.startswith('moveb') or len(parts) != 2:
            continue
        immediate = re.fullmatch(r'#(\d+)', parts[0])
        destination = record_access.ABSOLUTE.fullmatch(parts[1])
        if immediate and destination and int(destination[1], 16) == code_at and int(immediate[1]):
            function = functions[starts[bisect.bisect_right(starts, address) - 1]]
            if function != CONSOLE_SENDER:
                found.setdefault(int(immediate[1]), set()).add(function.removeprefix(DIAG_PREFIX))
    return found


def console_requests(image: str, rom: bytes, codes: set[int]) -> dict[int, tuple[int, int]]:
    """The request gps_command_send posts for each console code, run with
    gpsx_send answering at once."""
    send = names(image)[GPSX_SEND]
    machine = mc683xx.Machine()
    machine.map(0, ROM_SIZE, contents=rom[:ROM_SIZE])
    machine.poke_bytes(send, ANSWER)
    machine.map(RAM, RAM_SIZE)
    machine.set_stop_on_fault(True)
    machine.set_stop_on_exception(None)
    posted: list[tuple[int, int]] = []

    def hook(instruction: mc683xx.Instruction) -> None:
        sp = machine.read_register('A7')
        posted.append((struct.unpack('>H', machine.peek_bytes(sp + FRAME_CODE, 2))[0], machine.peek_bytes(sp + FRAME_MODE, 1)[0]))

    machine.add_code_hook(hook, send, send)
    mailbox = globals_of(image)['gps_command']
    found = {}
    for code in sorted(codes):
        posted.clear()
        machine.poke_bytes(mailbox, bytes([code]))
        machine.poke_bytes(mailbox + COMMAND_REQUEST, struct.pack('>I', ANY))
        machine.poke_bytes(STACK, struct.pack('>I', RETURN))
        machine.write_register('SR', SUPERVISOR)
        machine.write_register('A7', STACK)
        match machine.run_limited(begin=names(image)[CONSOLE_SENDER], reach=RETURN, instructions=CALL_LIMIT).end:
            case mc683xx.End.Fault() | mc683xx.End.Exception() as end:
                raise RuntimeError(f'{image}: {end}')
        if posted:
            found[code] = posted[0]
    return found


def paths(image: str) -> list[Path]:
    """Every path of the image's."""
    rom = (FIRMWARE / 'images' / f'{image}.bin').read_bytes()
    family = Family(image, rom)
    takes = admitted(image, rom)
    instructions = record_access.disassemble(image)
    found: list[Path] = []

    def add(kind: str, name: str, code: int, mode: int | None, refusable: bool) -> None:
        target, mode = family.target(code, mode)
        refused = refusable and takes is not None and code not in takes
        found.append(Path(image, kind, name, target, mode, refused))

    posters = {names(image)[poster]: poster for poster in POSTERS}
    scan = Posts(record_access.primary_functions(image), posters)
    scan.run(instructions, image)
    monitored = set()
    for poster, function, values in scan.found:
        code_at, mode_at = POSTERS[poster]
        code = values[code_at] if code_at < len(values) else None
        mode = values[mode_at] if mode_at is not None and mode_at < len(values) else None
        if code is None or (mode_at is not None and mode is None):
            continue
        if poster == REQUEST_POST:
            if code != POST_STEP:
                monitored.add(code)
                add(MONITOR, function, code, mode, False)
        else:
            add(SEQUENCE, function, code, mode, True)
    for code, steps in family.scripts.items():
        refused = takes is not None and code not in takes and code not in monitored
        for step, mode in steps:
            target, mode = family.target(step, mode)
            found.append(Path(image, SCRIPT, family.script_name(code), target, mode, refused))
    for name, steps in family.lists.items():
        for step, mode in steps:
            target, mode = family.target(step, mode)
            found.append(Path(image, LIST, name, target, mode, False))
    for at, name in descriptors(image, rom, instructions).items():
        flags_at, _ = DESCRIPTOR_LAYOUT[rom[at]]
        add(SCPI, name, rom[at + flags_at + 2], SCPI_MODE, True)
    words = console_words(image, instructions)
    for code, (request, mode) in console_requests(image, rom, set(words)).items():
        for word in sorted(words[code]):
            if request == ANY:
                refused = takes is not None
                found.append(Path(image, CONSOLE, word, Target('any', ''), mode, refused))
            else:
                add(CONSOLE, word, request, mode, True)
    return found
