"""Reads each firmware image's chip-select map from its reset code --
the SIM's chip-select registers, CSPAR0 and CSPAR1, CSBARBT and CSORBT,
CSBAR0 to CSBAR10 and CSOR0 to CSOR10, as the boot code and the primary
write them -- decodes it by MC68331UM (third_party/motorola/MC68331UM.pdf:
Tables D-8 to D-12, section 4.8) and checks the tables the documents
give, or with --write rewrites them, or with --print prints every
image's.

    python3 -I tools/firmware/chip_selects.py [--write | --print]

The images are third_party/firmware/*.bin.  The registers' addresses
are third_party/firmware/include/mc68331.h's sim_registers.  Each image
must write them only with move.w #imm,(xxx).l, in runs of consecutive
instructions: every long in the image that is one of their addresses
must be such an instruction's operand, or the image is refused.  The
boot code's runs are those below the primary's vector table, the one
the boot code loads into VBR (move.l #TABLE,a0, movec a0,vbr), the
primary's those above it; the first run of each must start within a
few instructions of its reset vector's entry, the primary has one run,
and the boot code one, or two with the jump into the primary
(jmp (a0)) between them.  A register a run does not write keeps what
ran before it: the reset state (MC68331UM Table 4-23) for the boot
code's first run, that run's for the boot code's second and for the
primary, which the boot code enters from its first.

docs/firmware/chip-selects.md is written whole.  docs/firmware/restart.md
and docs/firmware/README.md hold one image's map each between the
markers

    <!-- chip_selects.py IMAGE MAP [IMAGE MAP]... -->
    <!-- end chip_selects.py -->

the first pair the map shown, any more compared with it, as the
image's other maps always are; MAP is boot or primary, or boot@0xADDR
where the boot code has two runs.  The table there has a last column,
Device, kept by hand: its cells are read back by select and written
again.  Exits nonzero on drift.
"""

import argparse
import dataclasses
import pathlib
import re
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
FIRMWARE = ROOT / 'third_party/firmware'
HEADER = FIRMWARE / 'include/mc68331.h'
TABLES = ROOT / 'docs/firmware/chip-selects.md'
# the documents holding a map between markers
DOCUMENTS = (ROOT / 'docs/firmware/restart.md', ROOT / 'docs/firmware/README.md')
SCRIPT = 'tools/firmware/chip_selects.py'
# the image the summary compares every map with
REFERENCE_IMAGE = 'z3805a-3543b'

MARKER = re.compile(r'^<!-- chip_selects\.py ((?:\S+ \S+ ?)+) -->\n(.*?)^<!-- end chip_selects\.py -->$',
                    re.M | re.S)
# the hand column's heading, and its cell in a row read back
DEVICE = 'Device'

# mc68331.h: the SIM's base and the chip-select fields' offsets
SIM_BASE = re.compile(r'/\* SIM, [^*]*? at (0x[0-9a-f]+)')
SIM_FIELD = re.compile(r'^\s*\w+ (cspar0|cspar1|csboot|cs\[11\]);\s*/\* \+(0x[0-9a-f]+)', re.M)
SELECTS = 11
# a chip select's two registers, base then option, each a word
REGISTER_SIZE = 2

# move.w #imm,(xxx).l and its length; move.l #imm,a0 then movec a0,vbr
MOVE_WORD_ABSOLUTE = 0x33fc
MOVE_LENGTH = 8
VBR_LOAD = re.compile(rb'\x20\x7c(....)\x4e\x7b\x88\x01', re.S)
JUMP_A0 = b'\x4e\xd0'
# the reset vector's place in a vector table
RESET_VECTOR = 4
# how far past its entry a reset's first run may start: the boot code's
# status register, VBR, stack and SYPCR writes
PROLOGUE = 32
# a word of an instruction lies on an even address
ALIGNMENT = 2

# Table D-11: BLKSZ to the block's size
BLOCK_SIZES = (2 << 10, 8 << 10, 16 << 10, 64 << 10, 128 << 10, 256 << 10, 512 << 10, 1 << 20)
BLOCK_MASK = 0x7
BASE_MASK = 0xfff8
BASE_SHIFT = 8
# 4.8.3: an interrupt acknowledge's ADDR[19:16], and the largest block
# that compares them
CPU_SPACE_TYPE = 0x0f0000
CPU_SPACE_LARGEST = 64 << 10
KIB = 1 << 10

# Table D-12: CSOR's fields, by shift and width, and their values' meanings
MODE_SHIFT, BYTE_SHIFT, RW_SHIFT, STRB_SHIFT, DSACK_SHIFT = 15, 13, 11, 10, 6
SPACE_SHIFT, IPL_SHIFT = 4, 1
TWO_BITS, THREE_BITS, FOUR_BITS = 0x3, 0x7, 0xf
BYTES = ('disabled', 'lower', 'upper', 'both')
READ_WRITE = ('reserved', 'read', 'write', 'read and write')
# the R/W fields' order in a window's rows: reads first
READ_WRITE_ORDER = (3, 0, 1, 2)
DSACK_F_TERM, DSACK_EXTERNAL = 0xe, 0xf
SPACES = ('CPU space', 'user space', 'supervisor space', 'supervisor and user space')
SPACE_CPU, SPACE_BOTH = 0, 3
BYTE_DISABLED = 0

# Tables D-8 and D-9: each select's CSPAR register and field; Figure 3-2:
# its pin on the 132-pin QFP; and the pin's discrete output and alternate
# function, if any
@dataclasses.dataclass(frozen=True)
class Pin:
    """A chip select's pin: its CSPAR register (0 or 1) and 2-bit field,
    its number, and what else it can be."""
    register: int
    field: int
    number: int
    discrete: str
    alternate: str


PINS = {
    'CSBOOT': Pin(0, 0, 112, '', ''),
    'CS0': Pin(0, 1, 113, '', 'BR'),
    'CS1': Pin(0, 2, 114, '', 'BG'),
    'CS2': Pin(0, 3, 115, '', 'BGACK'),
    'CS3': Pin(0, 4, 118, 'PC0', 'FC0'),
    'CS4': Pin(0, 5, 119, 'PC1', 'FC1'),
    'CS5': Pin(0, 6, 120, 'PC2', 'FC2'),
    'CS6': Pin(1, 0, 121, 'PC3', 'ADDR19'),
    'CS7': Pin(1, 1, 122, 'PC4', 'ADDR20'),
    'CS8': Pin(1, 2, 123, 'PC5', 'ADDR21'),
    'CS9': Pin(1, 3, 124, 'PC6', 'ADDR22'),
    'CS10': Pin(1, 4, 125, 'ECLK', 'ADDR23'),
}
# Table D-10: a CSPAR field's value
FIELD_DISCRETE, FIELD_ALTERNATE, FIELD_8_BIT, FIELD_16_BIT = 0, 1, 2, 3
# CSPAR0's CSBOOT field: only its low bit counts, the port's size
CSBOOT_16_BIT = 1


@dataclasses.dataclass
class Run:
    """One run of chip-select writes: its code ('boot' or 'primary'),
    its instructions' first and last addresses, and each register it
    writes, by name, as (value, the instruction's address)."""
    code: str
    first: int
    last: int
    writes: dict


@dataclasses.dataclass
class Map:
    """A run with what it leaves: its key ('boot', 'boot@0x418',
    'primary'), its run, each register's value after it and the map
    before it by name, None for the reset state."""
    key: str
    run: Run
    registers: dict
    before: str


def register_names():
    """Each chip-select register's address, from mc68331.h, to its
    name, in the SIM's order."""
    text = HEADER.read_text()
    base = int(SIM_BASE.search(text).group(1), 16)
    offsets = {name: int(offset, 16) for name, offset in SIM_FIELD.findall(text)}
    if set(offsets) != {'cspar0', 'cspar1', 'csboot', 'cs[11]'}:
        sys.exit('%s: no chip-select fields in sim_registers' % HEADER)
    names = {base + offsets['cspar0']: 'CSPAR0', base + offsets['cspar1']: 'CSPAR1',
             base + offsets['csboot']: 'CSBARBT', base + offsets['csboot'] + REGISTER_SIZE: 'CSORBT'}
    for n in range(SELECTS):
        at = base + offsets['cs[11]'] + 2 * REGISTER_SIZE * n
        names[at], names[at + REGISTER_SIZE] = 'CSBAR%d' % n, 'CSOR%d' % n
    return dict(sorted(names.items()))


def image_title(stem):
    model, revision = stem.split('-', 1)
    return '%s %s' % (model.upper(), revision.upper())


def long_at(image, at):
    return struct.unpack_from('>I', image, at)[0]


def read_maps(stem, names):
    """An image's maps, in the order they run: the boot code's, then the
    primary's."""
    image = (FIRMWARE / (stem + '.bin')).read_bytes()
    where = '%s.bin' % stem
    runs, covered = [], set()
    for at in range(0, len(image) - MOVE_LENGTH + 1, ALIGNMENT):
        if struct.unpack_from('>H', image, at)[0] != MOVE_WORD_ABSOLUTE:
            continue
        name = names.get(long_at(image, at + 4))
        if name is None:
            continue
        value = struct.unpack_from('>H', image, at + 2)[0]
        if runs and runs[-1].last + MOVE_LENGTH == at and name not in runs[-1].writes:
            runs[-1].writes[name] = (value, at)
            runs[-1].last = at
        else:
            runs.append(Run('', at, at, {name: (value, at)}))
        covered.add(at + 4)
    stray = [at for at in range(0, len(image) - 3, ALIGNMENT)
             if long_at(image, at) in names and at not in covered]
    if stray:
        sys.exit('%s: a chip-select address not written by move.w #imm,(xxx).l at %s'
                 % (where, ', '.join('0x%x' % at for at in stray)))
    boot_entry = long_at(image, RESET_VECTOR)
    tables = {long_at(image, m.start(1)) for m in VBR_LOAD.finditer(image)} - {0}
    if len(tables) != 1:
        sys.exit('%s: not one primary vector table the boot code loads into VBR: %s' % (where, sorted(tables)))
    table = tables.pop()
    primary_entry = long_at(image, table + RESET_VECTOR)
    for run in runs:
        run.code = 'boot' if run.first < table else 'primary'
    boot = [r for r in runs if r.code == 'boot']
    primary = [r for r in runs if r.code == 'primary']
    if len(primary) != 1 or not 0 <= primary[0].first - primary_entry <= PROLOGUE:
        sys.exit('%s: the primary\'s chip-select writes are not one run at its entry 0x%x' % (where, primary_entry))
    if not boot or len(boot) > 2 or not 0 <= boot[0].first - boot_entry <= PROLOGUE:
        sys.exit('%s: the boot code\'s chip-select writes are not one or two runs from its entry 0x%x'
                 % (where, boot_entry))
    if len(boot) == 2 and JUMP_A0 not in image[boot[0].last:boot[1].first]:
        sys.exit('%s: no jump into the primary between the boot code\'s two runs' % where)
    maps = []
    for n, run in enumerate(boot + primary):
        if run.code == 'boot' and len(boot) > 1:
            key = 'boot@0x%x' % run.first
        else:
            key = run.code
        before = None if n == 0 else maps[0]
        registers = dict(before.registers) if before else {name: None for name in names.values()}
        registers.update({name: value for name, (value, _) in run.writes.items()})
        maps.append(Map(key, run, registers, before.key if before else None))
    return maps


def hex_word(value):
    return '`0x%04x`' % value


def code_range(run):
    return '`0x%x` to `0x%x`' % (run.first, run.last)


def map_title(item):
    if item.key == 'primary':
        return 'The primary'
    if item.key == 'boot':
        return 'The boot code'
    return 'The boot code\'s %s run' % ('first' if item.before is None else 'second')


def select_names():
    return ['CSBOOT'] + ['CS%d' % n for n in range(SELECTS)]


def select_registers(select):
    if select == 'CSBOOT':
        return 'CSBARBT', 'CSORBT'
    return 'CSBAR' + select[2:], 'CSOR' + select[2:]


def port(item, select):
    """What CSPAR makes of a select's pin."""
    pin = PINS[select]
    value = item.registers['CSPAR%d' % pin.register]
    if value is None:
        return 'from the DATA lines at reset (4.8.4)'
    field = (value >> (2 * pin.field)) & TWO_BITS
    if select == 'CSBOOT':
        return '16-bit' if field & CSBOOT_16_BIT else '8-bit'
    return {FIELD_DISCRETE: 'discrete output %s' % pin.discrete,
            FIELD_ALTERNATE: 'alternate function %s' % pin.alternate,
            FIELD_8_BIT: '8-bit', FIELD_16_BIT: '16-bit'}[field]


def pin_text(select):
    pin = PINS[select]
    others = '/'.join(p for p in (pin.discrete, pin.alternate) if p)
    return '%d, %s' % (pin.number, others) if others else '%d' % pin.number


@dataclasses.dataclass
class Row:
    """A select's row: its sort key and its cells."""
    key: tuple
    select: str
    window: str
    cells: list


def select_row(item, select):
    bar_name, or_name = select_registers(select)
    bar, option = item.registers[bar_name], item.registers[or_name]
    bar, option = bar or 0, option or 0
    size = BLOCK_SIZES[bar & BLOCK_MASK]
    base = ((bar & BASE_MASK) << BASE_SHIFT) & ~(size - 1)
    byte = (option >> BYTE_SHIFT) & TWO_BITS
    read_write = (option >> RW_SHIFT) & TWO_BITS
    space = (option >> SPACE_SHIFT) & TWO_BITS
    ipl = (option >> IPL_SHIFT) & THREE_BITS
    level = 'level %d' % ipl if ipl else 'any level'
    if byte == BYTE_DISABLED:
        window, key = 'none: BYTE disabled', (3, 0, 0)
    elif space == SPACE_CPU and base & CPU_SPACE_TYPE == CPU_SPACE_TYPE and size <= CPU_SPACE_LARGEST:
        window, key = 'CPU space, interrupt acknowledge at %s' % level, (2, ipl, 0)
    else:
        window = '`0x%06x`--`0x%06x`, %d KiB' % (base, base + size - 1, size // KIB)
        key = (0 if space != SPACE_CPU else 1, base, READ_WRITE_ORDER[read_write])
    if (option >> MODE_SHIFT) & 1:
        timing = 'synchronous'
    else:
        dsack = (option >> DSACK_SHIFT) & FOUR_BITS
        timing = {DSACK_F_TERM: 'F term', DSACK_EXTERNAL: 'external'}.get(dsack, '%d wait' % dsack)
    fields = '%s, %s, %s' % (BYTES[byte], READ_WRITE[read_write], timing)
    extras = []
    if (option >> STRB_SHIFT) & 1:
        extras.append('data strobe')
    if space != SPACE_BOTH:
        extras.append(SPACES[space])
    if ipl:
        extras.append('IPL %d' % ipl)
    if option & 1:
        extras.append('autovector')
    if byte == BYTE_DISABLED:
        fields = 'disabled'
    elif extras:
        fields += '; ' + ', '.join(extras)
    written = []
    for name in (bar_name, or_name):
        if name in item.run.writes:
            written.append('`0x%x`' % item.run.writes[name][1])
        else:
            written.append('kept')
    values = '%s, %s; %s' % (hex_word(bar), hex_word(option), ', '.join(written))
    if 'kept' in written:
        values += ' (%s)' % ('reset' if item.before is None else 'as the boot code\'s %sleft them' % (
            '' if item.before == 'boot' else 'first run '))
    return Row(key + (select_names().index(select),), select, window,
               [select, pin_text(select), values, port(item, select), fields])


def pin_line(item):
    parts = []
    for name in ('CSPAR0', 'CSPAR1'):
        if name in item.run.writes:
            value, at = item.run.writes[name]
            parts.append('%s %s at `0x%x`' % (name, hex_word(value), at))
        else:
            parts.append('%s not written' % name)
    return 'Pins: %s.' % ', '.join(parts)


def table(item, devices=None):
    """A map's table, with the Device column from devices (select to
    cell) if given."""
    heading = ['Window', 'Select', 'Pin', 'CSBAR, CSOR; written at', 'Port', 'BYTE, R/W, DSACK']
    if devices is not None:
        heading.append(DEVICE)
    lines = ['| ' + ' | '.join(heading) + ' |', '| ' + ' | '.join('-' * len(h) for h in heading) + ' |']
    previous = None
    for row in sorted((select_row(item, s) for s in select_names()), key=lambda r: r.key):
        cells = ['the same' if row.window == previous else row.window] + row.cells
        previous = row.window
        if devices is not None:
            cells.append(devices.get(row.select, ''))
        lines.append('| ' + ' | '.join(cells) + ' |')
    return '\n'.join(lines)


def comparison(item, other, shift=True):
    """What other's run writes against item's, as a predicate: the same
    values, with how far its instructions lie from item's if shift and
    they are all one distance away, or the registers that differ."""
    names = [n for n in register_order() if n in item.run.writes or n in other.run.writes]
    differing = [n for n in names if item.run.writes.get(n, (None,))[0] != other.run.writes.get(n, (None,))[0]]
    if not differing:
        distances = {other.run.writes[n][1] - item.run.writes[n][1] for n in names}
        apart = ''
        if shift and len(distances) == 1:
            distance = distances.pop()
            apart = ', each `0x%x` %s' % (abs(distance), 'above' if distance > 0 else 'below')
        return 'writes the same %d values%s' % (len(names), apart)
    values = ['%s %s' % (n, hex_word(other.run.writes[n][0])) for n in differing if n in other.run.writes]
    unwritten = [n for n in differing if n not in other.run.writes]
    clauses = []
    if values:
        clauses.append('writes ' + ', '.join(values))
    if unwritten:
        clauses.append('writes no ' + ', '.join(unwritten))
    return ' and '.join(clauses)


def upper_first(text):
    return text[:1].upper() + text[1:]


def sentence(subject, item, other, shift=True):
    return '%s %s.' % (subject, comparison(item, other, shift))


def register_order():
    return list(register_names().values())


def every_image():
    names = register_names()
    return {path.stem: read_maps(path.stem, names) for path in sorted(FIRMWARE.glob('*.bin'))}


def find_map(images, stem, key):
    if stem not in images:
        sys.exit('%s: no image %s' % (SCRIPT, stem))
    maps = images[stem]
    found = [m for m in maps if m.key == key or (key == 'boot' and m.key.startswith('boot@'))]
    if len(found) != 1:
        sys.exit('%s: %s has no one map %s; its maps: %s' % (SCRIPT, stem, key, ', '.join(m.key for m in maps)))
    return found[0]


def generated_tables(images):
    """docs/firmware/chip-selects.md's text."""
    reference = find_map(images, REFERENCE_IMAGE, 'primary')
    out = ['# Chip selects', '',
           'Generated by `%s` from `third_party/firmware/*.bin`; do not edit.' % SCRIPT,
           '`make chip-selects` rewrites it and the maps `restart.md` and `README.md`',
           'give; `make check-chip-selects` fails when they and the images disagree.',
           '`restart.md`, "The chip selects", says what the Z3805A\'s selects reach.', '',
           'Each image\'s primary writes the SIM\'s chip-select registers in one',
           'run of `move.w #imm,(xxx).l` from its reset entry, and its boot code in',
           'one or two, and nothing else in the image writes them.  Where the boot',
           'code has two runs, the first sets a map to check the primary\'s',
           'checksum under and jumps into the primary, and the second, the boot',
           'code\'s own map, runs when the primary is refused.  The installer runs',
           'on the boot code\'s last map.',
           'A register a run leaves alone keeps what ran before it: the reset state',
           '(MC68331UM Table 4-23) for the first run, the first run\'s for the',
           'others.  Decoded by MC68331UM Tables D-8 to D-12 (4.8.1): a window is',
           'the base masked to its block (Table D-11); a select in CPU space whose',
           'base compares ADDR[19:16] as all ones is an interrupt acknowledge',
           '(4.8.3); BYTE, R/W and DSACK are Table D-12\'s fields, then any of',
           'MODE, STRB, SPACE, IPL and AVEC not at its usual value (asynchronous,',
           'address strobe, supervisor and user space, any level, no autovector).',
           'On an 8-bit port any BYTE but disabled enables the select (4.8.1.3).',
           'Pin numbers are the 132-pin QFP\'s (Figure 3-2).', '',
           '## Each map against the Z3805A 3543B\'s primary', '',
           '| Image | Map | Written at | Against the Z3805A 3543B\'s primary |',
           '| ----- | --- | ---------- | ---------------------------------- |']
    for stem, maps in images.items():
        for item in maps:
            against = '--' if item is reference else upper_first(comparison(reference, item, shift=False))
            out.append('| %s | %s | %s | %s |' % (image_title(stem), item.key, code_range(item.run), against))
    for stem, maps in images.items():
        out += ['', '## %s' % image_title(stem)]
        for item in maps:
            out += ['', '### %s, %s' % (map_title(item), code_range(item.run)), '', pin_line(item), '',
                    table(item)]
            for other in maps:
                if other.run.first > item.run.first:
                    out += ['', sentence('%s (%s)' % (map_title(other), code_range(other.run)), item, other)]
    return '\n'.join(out) + '\n'


def read_devices(block):
    """A block's Device cells by select."""
    devices = {}
    lines = [l for l in block.split('\n') if l.startswith('| ')]
    if not lines:
        return devices
    heading = [c.strip() for c in lines[0].strip('|').split(' | ')]
    if DEVICE not in heading:
        return devices
    for line in lines[2:]:
        cells = [c.strip() for c in line[2:-2].split(' | ')]
        devices[cells[heading.index('Select')]] = cells[heading.index(DEVICE)]
    return devices


def block(images, specification, old):
    words = specification.split()
    pairs = [(words[n], words[n + 1]) for n in range(0, len(words), 2)]
    item = find_map(images, *pairs[0])
    stem = pairs[0][0]
    out = [pin_line(item), '', table(item, read_devices(old))]
    compared = [m for m in images[stem] if m is not item]
    out += ['', ' '.join(sentence('%s (%s)' % (map_title(other), code_range(other.run)), item, other)
                         for other in compared)] if compared else []
    for other_stem, key in pairs[1:]:
        other = find_map(images, other_stem, key)
        subject = 'The %s\'s %s (%s)' % (image_title(other_stem), map_title(other)[4:], code_range(other.run))
        out += ['', sentence(subject, item, other, shift=False)]
    return '\n'.join(out) + '\n'


def rewrite_document(text, images):
    def replace(match):
        return '<!-- chip_selects.py %s -->\n%s<!-- end chip_selects.py -->' % (
            match.group(1), block(images, match.group(1), match.group(2)))
    return MARKER.sub(replace, text)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    group = parser.add_mutually_exclusive_group()
    group.add_argument('--write', action='store_true', help='rewrite the tables in the documents')
    group.add_argument('--print', action='store_true', help='print every image\'s tables')
    arguments = parser.parse_args()
    images = every_image()
    tables = generated_tables(images)
    if arguments.print:
        sys.stdout.write(tables)
        return 0
    drifted = []
    outputs = [(TABLES, tables)]
    for document in DOCUMENTS:
        text = document.read_text()
        if not MARKER.search(text):
            sys.exit('%s: no chip_selects.py block' % document)
        outputs.append((document, rewrite_document(text, images)))
    for path, text in outputs:
        current = path.read_text() if path.exists() else ''
        if current == text:
            continue
        if arguments.write:
            path.write_text(text)
        else:
            drifted.append(str(path.relative_to(ROOT)))
    if drifted:
        print('%s: not what the images give: %s; make chip-selects rewrites them' % (SCRIPT, ', '.join(drifted)))
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
