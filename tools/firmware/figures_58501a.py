# /// script
# requires-python = ">=3.11"
# dependencies = ["capstone==5.0.9"]
# ///
"""Checks the figures docs/firmware/58501a.md quotes against a fresh
reading of the 58501A's image and EEPROM, the 58503A images and the
FPGA bitstreams held, and the SHA-256s third_party/NOTICE gives them.

    uv run -q tools/firmware/figures_58501a.py --viewer VIEWER_HTML_DIR

Each figure is computed here, put into the sentence that quotes it, and
the sentence must be in the document exactly once, its whitespace
folded; the table of the pins the 58501A's and the 58503A's FPGA
designs configure differently, between its marker comments, is
generated and must match.  It also holds the code the document reads
to the bytes it cites.  VIEWER_HTML_DIR is the XC3000 viewer's html/
folder at the pinned commit (tools/fpga/decoder.mk), which
tools/fpga/iobdump.js reads; `make check-58501a-figures` clones it.
Exits nonzero on any difference.
"""

import argparse
import hashlib
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/firmware'))
import pforth_words  # noqa: E402

IMAGES = ROOT / 'third_party/firmware/images'
IMAGE = IMAGES / '58501a-prototype/58501a-fiji_ege.bin'
EEPROM = IMAGES / '58501a-prototype/58501a-eeprom.bin'
IMAGE_3633 = IMAGES / '58503a-3633.bin'
IMAGE_3704 = IMAGES / '58503a-3704.bin'
BITSTREAMS = IMAGES / 'fpga'
BITSTREAM_3704 = BITSTREAMS / '58503a-z3801a.bit'
DOCUMENT = ROOT / 'docs/firmware/58501a.md'
NOTICE = ROOT / 'third_party/NOTICE'
IOBDUMP = ROOT / 'tools/fpga/iobdump.js'

BANK = 0x40000
# The lane sums cover a bank but its last four bytes, which hold them.
SUM_SPAN = BANK - 4

# The SIM's chip-select registers, CSPAR0 to CSOR10 (MC68331UM Table D-3).
CS_FIRST, CS_LAST = 0xfffa44, 0xfffa77
CSPAR0 = 0xfffa44
# Where each image's reset code writes them (docs/firmware/chip-selects.md).
CS_RANGE_58501A = (0x28ea, 0x29b2)
CS_RANGE_58503A_BOOT = (0x568, 0x628)

REVISION = (0x1e80, b'Fiji_EGE')
MODEL = (0x178b6, b'58501A')
PSOS = (0x2bd2, b' V4.1 COPYRIGHT 1983')

# The XC3000 preamble, the length in bytes of an XC3042 bitstream, and
# the 58503A 3704's bitstream and readback mask block
# (crates/smartclock-cli/tests/fpga_bitstreams.rs).
FPGA_HEADER = bytes([0xff, 0x20, 0x07, 0x86])
FPGA_LENGTH = 3860
FPGA_3704 = (0x1fd16, 0x20c34)
FPGA_TABLE = 0xdea0
# Each held design's name as the document says it.
DESIGNS = {
    '58503a-z3801a.bit': "58503A and Z3801A's",
    'z3805a.bit': "Z3805A's",
    '58503b.bit': "58503B's",
    'z3815a.bit': "Z3815A's",
    'z3816a.bit': "Z3816A's",
    'z3817a.bit': "Z3817A's",
}

ONCORE_TABLE = 0x47162
ONCORE_ENTRY = 52

# The EEPROM's records (docs/firmware/58501a.md, "The EEPROM").
EEPROM_SIZE = 0x2000
RECORD = 0x40
RECORD_LENGTH = 0x40
SLOTS = (0x80, 0xc0)
SLOT_LENGTH = 0x40
SLOT_TAG = 0x5313
RESET_RECORDS = (0x100, 0xc80)
RESET_RECORD_LENGTH = 0x5c
RESET_RECORD_COUNT = 32
LOG = 0xc80
LOG_PAGE = 0x40
POSITION = 0x1c
MILLIARCSECONDS_PER_DEGREE = 3_600_000
CENTIMETERS_PER_METER = 100

# Code the document reads, at the addresses it cites: each address and
# the bytes there.
CODE = [
    (0x1725c, '0c505313'),          # cmpi.w #0x5313,(a0): a slot's tag
    (0x17324, '2f037600'),          # the configuration block's sum
    (0x1732c, '7208'),              # moveq #8,d1: from +8
    (0x1733e, '7040b280'),          # moveq #0x40,d0; cmp.l d0,d1: to +0x3f
    (0x174d6, '35400002'),          # move.w d0,2(a2): stored at +2
    (0x1756c, 'b06a0002'),          # cmp.w 2(a2),d0: checked at +2
    (0x17572, '48780008487900001e80'),  # strncmp with "Fiji_EGE", 8 bytes
    (0x15eba, '2f032f02'),          # the record at 0x40's sum
    (0x15ece, '703e'),              # over 0x3e bytes, +2 to +0x3f
    (0x178c6, '7004'),              # a reset record's, from +4
    (0x178d6, '7058'),              # over 0x58 bytes
    (0x1650a, '703c'),              # a log page's, over 0x3c bytes from +4
    (0x5928, '5581'),               # the lane sums: count - 2
    (0x69f8, '2f390003fffc'),       # bank 1's stored sums
    (0x6a58, '2f390007fffc'),       # bank 2's
    (0x4060a, '207c00304000'),      # fpga_write: 0x304000 plus the register
    (0xf8fe, '267c0000dead'),       # the readback mask, five bytes in
]

NUMBERS = {3: 'three', 6: 'six'}

faults = []


def fault(text):
    faults.append(text)


def u16(data, offset):
    return int.from_bytes(data[offset:offset + 2], 'big')


def u32(data, offset):
    return int.from_bytes(data[offset:offset + 4], 'big')


def s32(data, offset):
    return int.from_bytes(data[offset:offset + 4], 'big', signed=True)


def sum_and_complement(data):
    """The other images' record_checksum: the byte sum and its complement."""
    total = sum(data) & 0xff
    return total << 8 | (0xff - total)


def short_hash(path):
    """A file's SHA-256 as third_party/NOTICE quotes one: first and last eight."""
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    return f'{digest[:8]}...{digest[-8:]}'


class Document:
    """The document's text, its whitespace folded, and what it must say."""

    def __init__(self, path):
        self.path = path
        self.raw = path.read_text()
        self.text = ' '.join(self.raw.split())

    def need(self, sentence):
        count = self.text.count(' '.join(sentence.split()))
        if count != 1:
            fault(f'{self.path.relative_to(ROOT)}: {count} times, not once: {sentence}')


def lane_sums(image, base):
    """A bank's even and odd byte sums, and the two its last four bytes hold."""
    bank = image[base:base + SUM_SPAN]
    even, odd = sum(bank[0::2]) & 0xffff, sum(bank[1::2]) & 0xffff
    stored = image[base + SUM_SPAN:base + BANK]
    return (even, odd), (stored[0] << 8 | stored[2], stored[1] << 8 | stored[3])


def check_rom(image, document):
    if len(image) != 2 * BANK:
        fault(f'{IMAGE.name}: {len(image)} bytes, not two banks')
    figures = []
    for base in (0, BANK):
        computed, stored = lane_sums(image, base)
        if computed != stored:
            fault(f'bank at {base:#x}: sums {computed} but stores {stored}')
        figures.append(computed)
    (even1, odd1), (even2, odd2) = figures
    document.need(f"Bank 1's sums are `{even1:#06x}` and `{odd1:#06x}`, bank 2's "
                  f"`{even2:#06x}` and `{odd2:#06x}`, and both banks' hold.")
    for (address, text) in (REVISION, MODEL, PSOS):
        if image[address:address + len(text)] != text:
            fault(f'{IMAGE.name}: {text!r} not at {address:#x}')
    document.need(f'The revision string `{REVISION[1].decode()}` is at `{REVISION[0]:#x}`')
    document.need(f'the model `{MODEL[1].decode()}` is at `{MODEL[0]:#x}`')
    document.need(f'(`{PSOS[0]:#x}`)')


def chip_select_writes(image, first, last):
    """Each move.w #value,abs.l to a chip-select register in first..last."""
    writes = []
    for match in re.finditer(rb'\x33\xfc(..)\x00\xff(..)', image[first:last], re.S):
        register = 0xff0000 | int.from_bytes(match.group(2), 'big')
        if match.start() % 2 == 0 and CS_FIRST <= register <= CS_LAST:
            writes.append((first + match.start(), register, int.from_bytes(match.group(1), 'big')))
    return writes


def check_chip_selects(image, document):
    ours = chip_select_writes(image, *CS_RANGE_58501A)
    boot_3633 = chip_select_writes(IMAGE_3633.read_bytes(), *CS_RANGE_58503A_BOOT)
    boot_3704 = chip_select_writes(IMAGE_3704.read_bytes(), *CS_RANGE_58503A_BOOT)
    values = [(register, value) for _, register, value in ours]
    if not ours or values != [(register, value) for _, register, value in boot_3633]:
        fault('the chip selects are not the 58503A 3633 boot code\'s')
    differ = [(a, b) for a, b in zip(values, [(r, v) for _, r, v in boot_3704]) if a != b]
    if len(boot_3704) != len(ours) or len(differ) != 1 or differ[0][0][0] != CSPAR0:
        fault('the chip selects differ from the 3704\'s otherwise than in CSPAR0')
        return
    (_, here), (_, there) = differ[0]
    document.need(f'writes the chip selects from `{ours[0][0]:#x}` to `{ours[-1][0]:#x}`; the values it '
                  'writes are the 58503A 3633 boot code\'s, in the same order, the 3704\'s but for '
                  f'CSPAR0, `{here:#06x}` here and `{there:#06x}` there.')


def check_pforth(document):
    ours = pforth_words.image_words(IMAGE)
    theirs = pforth_words.image_words(IMAGE_3704)

    def names(words, table):
        return [word.name for word in words if word.table == table]

    if names(ours, 'kernel') != names(theirs, 'kernel'):
        fault('the pForth kernel\'s words are not the 3704\'s')
    document.need('Its kernel\'s words, read as `tools/firmware/pforth_words.py` reads them, are '
                  'the 58503A 3704\'s, the same names in the same order.')
    ours_diag, theirs_diag = set(names(ours, 'diagnostic')), set(names(theirs, 'diagnostic'))
    missing = ['loop_time', 'pll_rep', 'pr_time_raim']
    added = ['adc_oven', 'log_rd', 'run_diag']
    if not all(name in theirs_diag - ours_diag for name in missing):
        fault(f'not all of {missing} are the 3704\'s alone')
    if not all(name in ours_diag - theirs_diag for name in added):
        fault(f'not all of {added} are the 58501A\'s alone')
    document.need("3704's `loop_time`, `pll_rep` and `pr_time_raim` are not among them, and words "
                  'such as `adc_oven`, `log_rd` and `run_diag` are.')


def listing(items):
    return ', '.join(items[:-1]) + ' and ' + items[-1]


def check_oncore(image, document):
    """The message table's names: the entries run up to the first name."""
    end = u32(image, ONCORE_TABLE)
    names = []
    for entry in range(ONCORE_TABLE, end, ONCORE_ENTRY):
        pointer = u32(image, entry)
        if pointer:
            names.append(image[pointer:image.index(b'\0', pointer)].decode('ascii'))
    if not names or any(len(name) != 2 for name in names):
        fault(f'no message table of two-letter names at {ONCORE_TABLE:#x}')
    document.need(f'The GPS task\'s message table is at `{ONCORE_TABLE:#x}`.  Its entries name '
                  f'these Oncore messages: {listing(names)}.')
    for absent in ('Bn', 'En', 'Ea'):
        if absent in names:
            fault(f'the message table has {absent}')


def differing(first, second):
    return sum(a != b for a, b in zip(first, second))


def check_fpga(image, document, viewer):
    starts = [match.start() for match in re.finditer(re.escape(FPGA_HEADER), image)]
    if len(starts) != 2:
        fault(f'{len(starts)} XC3000 headers, not a bitstream and its readback mask')
        return
    start, mask = starts
    bitstream = image[start:start + FPGA_LENGTH]
    image_3704 = IMAGE_3704.read_bytes()
    if mask - start != FPGA_3704[1] - FPGA_3704[0]:
        fault('the readback mask is not where it is in the 3704')
    if u32(image, FPGA_TABLE) != start:
        fault(f'the table at {FPGA_TABLE:#x} does not point to the bitstream')
    mask_hash = hashlib.sha256(image[mask:mask + FPGA_LENGTH]).hexdigest()[:8]
    if mask_hash != hashlib.sha256(image_3704[FPGA_3704[1]:FPGA_3704[1] + FPGA_LENGTH]).hexdigest()[:8]:
        fault('the readback mask differs from the 3704\'s')
    document.need(f'the image loads one XC3042 bitstream, at `{start:#x}`, {FPGA_LENGTH} bytes, '
                  f'SHA-256 `{hashlib.sha256(bitstream).hexdigest()[:8]}`')
    document.need(f'before the second block points to it (`{FPGA_TABLE:#x}`).  The second block, at '
                  f'`{mask:#x}`, is the readback mask, SHA-256 `{mask_hash}`, the same as every '
                  'other image\'s, and it follows the bitstream at the same distance as in the others')

    counts = {path.name: differing(bitstream, path.read_bytes())
              for path in sorted(BITSTREAMS.glob('*.bit'))}
    nearest = min(counts, key=counts.get)
    document.need(f'It differs from the {DESIGNS[BITSTREAM_3704.name]} design '
                  f'(`third_party/firmware/images/fpga/{BITSTREAM_3704.name}`) in '
                  f'{counts[BITSTREAM_3704.name]} of its {FPGA_LENGTH} bytes, and from the nearest, '
                  f'the {DESIGNS[nearest]}, in {counts[nearest]}.')

    bitstream_file = ROOT / 'target/firmware/58501a/58501a.bit'
    bitstream_file.parent.mkdir(parents=True, exist_ok=True)
    bitstream_file.write_bytes(bitstream)
    ours, theirs = iob_blocks(viewer, bitstream_file), iob_blocks(viewer, BITSTREAM_3704)
    if ours is None or theirs is None:
        return
    bonded = [pin for pin in theirs if pin.startswith('P')]
    differ = sorted((pin for pin in bonded if ours.get(pin) != theirs[pin]), key=lambda pin: int(pin[1:]))
    document.need(f'of the {len(bonded)} bonded pins of its PC84 package, {len(bonded) - len(differ)} '
                  'are configured as the 58503A\'s design configures them.  The '
                  f'{NUMBERS[len(differ)]} that differ:')
    rows = ['| Pin | 58501A | 58503A 3704 |', '| --- | ------ | ----------- |']
    rows += [f'| {pin} | {ours[pin]} | {theirs[pin]} |' for pin in differ]
    table = '<!-- figures_58501a.py pins -->\n' + '\n'.join(rows) + '\n<!-- end figures_58501a.py pins -->'
    if document.raw.count(table) != 1:
        fault(f'{DOCUMENT.relative_to(ROOT)}: the pin table is not iobdump.js\'s:\n{table}')


def iob_blocks(viewer, path):
    """iobdump.js's configuration of each pad, by pin."""
    run = subprocess.run(['node', str(IOBDUMP), str(viewer), str(path)],
                         capture_output=True, text=True)
    if run.returncode != 0:
        fault(f'iobdump.js {path.name} failed: {run.stderr.strip()}')
        return None
    blocks = {}
    for line in run.stdout.splitlines():
        pin, _, _, configuration = line.split('\t')
        blocks[pin] = configuration
    return blocks


def slot_sum(slot):
    """The configuration block's checksum: bytes +8 to +0x3f, the low 16 bits."""
    return sum(slot[8:SLOT_LENGTH]) & 0xffff


def check_eeprom(eeprom, image, document):
    if len(eeprom) != EEPROM_SIZE:
        fault(f'{EEPROM.name}: {len(eeprom)} bytes')
    document.need('An AT28C64-sized part, 8 KiB')
    record = eeprom[RECORD:RECORD + RECORD_LENGTH]
    if sum_and_complement(record[2:]) != u16(record, 0):
        fault('the record at 0x40\'s checksum does not hold')

    slot0, slot1 = (eeprom[base:base + SLOT_LENGTH] for base in SLOTS)
    revision = image[REVISION[0]:REVISION[0] + len(REVISION[1])]
    if not (u16(slot0, 0) == SLOT_TAG and slot_sum(slot0) == u16(slot0, 2) and slot0[8:16] == revision):
        fault('slot 0 is not valid')
    latitude, longitude, height = (s32(slot0, POSITION + 4 * i) for i in range(3))
    document.need(f'slot 0 is valid, and its position decodes to '
                  f'{latitude / MILLIARCSECONDS_PER_DEGREE:.8f}°, '
                  f'{longitude / MILLIARCSECONDS_PER_DEGREE:.8f}°, '
                  f'{height / CENTIMETERS_PER_METER:.2f} m, as Steve read it.')
    if not (slot1[0] == 0xff and slot1[1] == SLOT_TAG & 0xff and slot1[8:16] == b'Fiji\0\0\0\0'
            and slot_sum(slot1) == u16(slot1, 2)):
        fault('slot 1 is not as the document says')
    document.need('Slot 1 is not: its first byte is `0xff`, and its revision is `Fiji` with four '
                  'zero bytes after it, though its sum holds.')
    height_offset = SLOTS[0] + POSITION + 8
    document.need(f'Steve changed the height, `{u32(eeprom, height_offset):#x}` at '
                  f'`{height_offset:#05x}` (slot 0\'s `+{POSITION + 8:#x}`)')

    first, end = RESET_RECORDS
    if (end - first) // RESET_RECORD_LENGTH != RESET_RECORD_COUNT or end != LOG:
        fault('the reset records do not fill 0x100 to 0xc80')
    records = [eeprom[first + RESET_RECORD_LENGTH * i:first + RESET_RECORD_LENGTH * (i + 1)]
               for i in range(RESET_RECORD_COUNT)]
    valid = 0
    while valid < len(records) and sum_and_complement(records[valid][4:]) == u16(records[valid], 2):
        valid += 1
    zeroed = valid
    while zeroed < len(records) and not any(records[zeroed][:4]) and any(records[zeroed][4:]):
        zeroed += 1
    if any(record != b'\xff' * RESET_RECORD_LENGTH for record in records[zeroed:]):
        fault('the reset records after the zeroed ones are not erased')
    document.need(f'The first {NUMBERS[valid]} records hold.  The next {NUMBERS[zeroed - valid]} carry '
                  'text, but their sequence and checksum words are zero, and fail; the rest are erased.')

    pages = [eeprom[base:base + LOG_PAGE] for base in range(LOG, EEPROM_SIZE, LOG_PAGE)]
    if any(sum_and_complement(page[4:]) != u16(page, 2) for page in pages):
        fault('a log page\'s checksum does not hold')
    document.need('Every page in the dump holds.')


def check_code(image):
    for address, text in CODE:
        expected = bytes.fromhex(text)
        if image[address:address + len(expected)] != expected:
            fault(f'{IMAGE.name}: not {text} at {address:#x}')


def check_notice():
    notice = Document(NOTICE)
    for path in (IMAGE, EEPROM):
        notice.need(f'SHA-256 {short_hash(path)}')


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--viewer', type=pathlib.Path, required=True,
                        help="the XC3000 viewer's html folder, at the pinned commit")
    arguments = parser.parse_args()
    image, eeprom = IMAGE.read_bytes(), EEPROM.read_bytes()
    document = Document(DOCUMENT)
    check_rom(image, document)
    check_chip_selects(image, document)
    check_pforth(document)
    check_oncore(image, document)
    check_fpga(image, document, arguments.viewer)
    check_eeprom(eeprom, image, document)
    check_code(image)
    check_notice()
    for text in faults:
        print(f'{sys.argv[0]}: {text}', file=sys.stderr)
    return 1 if faults else 0


if __name__ == '__main__':
    sys.exit(main())
