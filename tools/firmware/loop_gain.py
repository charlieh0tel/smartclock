"""Checks where each image's loop gain G comes from, the one source
docs/firmware/loop-gain.json, against the firmware images, decodes the
calibration block of every EEPROM in third_party/firmware/images/eeprom/
by the rules of its unit's image, and checks the two tables made from
both against them: docs/firmware/loop.md's, the evidence, with the
addresses, and docs/pages/loop.html's, without.  With --write it
rewrites the tables.

    python3 tools/firmware/loop_gain.py [--write]

The data, one JSON object:

    block    the EEPROM's calibration block: at, its address, size, its
             bytes, and eeprom, the address of the EEPROM's first byte
    efc      the EFC, in DAC counts, at which a gain that depends on
             the EFC is given beside its value at 0
    images   one entry an image, in the tables' order (below)
    eeproms  every EEPROM held: file, in third_party/firmware/images/
             eeprom/, and image, the image whose rules decode it, its
             unit's; synthetic, true for one the project made rather
             than read from a unit, listed but decoded for no table

An image's entry: image, model and firmware, as loop-constants.json's;
rule, how its G is set (below); values, {name: {value, unit, rom}}, the
constants its text names, a gain unless unit is "s", each read from the
image where rom places it (tools/firmware/loop_tables.py); code, each
{at, bytes, what}, an instruction or datum the text cites and the bytes
the image must hold there; source, what sets G; override, {answer,
text, page}, whether and how the EEPROM changes it, page the page's text
where it differs; and, by rule, fields or block.  In source and override's text, {name} is a value and [a, b] the
addresses cited, each a value's or code's at: loop.md gives them, the
page leaves them out.

The rules:

    fixed   G is the value gain; the block holds fields, each {name,
            offset, type}, type float or word, a word 0xffff read as 0
    switch  G is set by a bit of a hardware word; no EEPROM is decoded
    type    G is the type's entry of a table of floats, block's table,
            entry bytes apart, types entries, the type the block's byte
            at type, 0 when the block is bad, the byte 0xff or past the
            last; a valid EFC calibration's A replaces it
    kind    G = A + B·u², A and B by kind, the block's byte at type, 0
            when the block is bad or the byte 0xff: a0 and b0 for kind
            0, a1 and 0 for any other; τ tau0, or tau1 for kind
            block's kind_tau; a valid EFC calibration's A and B replace
            them

A block is valid when its first word is its checksum: the byte sum s of
its other bytes, as (s << 8) | ~s & 0xff.  Its EFC calibration, DADC, A
and B at block's dadc, a and b, is valid when the block is, none of the
three has all the bits of the long at mask set, A is not 0, and the byte
at type is the type or 0xff.  Exits nonzero on a fault or drift.
"""

import argparse
import json
import math
import re
import struct
import sys

from loop_tables import IMAGES, MINUS, ROOT, SUPERSCRIPTS, Markers, code, float_at, image, number, place_fault, power

DATA = ROOT / 'docs/firmware/loop-gain.json'
EEPROMS = IMAGES / 'eeprom'
EVIDENCE = ROOT / 'docs/firmware/loop.md'
PAGE = ROOT / 'docs/pages/loop.html'
SCRIPT = 'tools/firmware/loop_gain.py'
MARKERS = Markers('loop_gain.py')
# where loop.md links an EEPROM from, and where the page does
EVIDENCE_LINK = '../../third_party/firmware/images/eeprom/'
PAGE_LINK = 'https://github.com/charlieh0tel/smartclock/blob/main/third_party/firmware/images/eeprom/'

# the significant digits of a gain decoded from an EEPROM
DECODED_DIGITS = 4
ERASED_BYTE = 0xff
ERASED_WORD = 0xffff
BYTE_MASK = 0xff

VALUE = re.compile(r'\{(\w+)\}')
CITE = re.compile(r' ?\[([0-9a-fx, ]+)\]')


def check_image(entry):
    """The entry's values and code against its image; the faults found
    and the addresses read."""
    rom = image(entry['image'])
    faults, read = [], set()
    for name, choice in entry['values'].items():
        for place in choice['rom']:
            read.add(place['at'])
            fault = place_fault(rom, place, choice['value'])
            if fault:
                faults.append('%s %s at %s: %s' % (entry['image'], name, place['at'], fault))
    for item in entry['code']:
        read.add(item['at'])
        at = int(item['at'], 16)
        expected = bytes.fromhex(item['bytes'])
        found = rom[at:at + len(expected)]
        if found != expected:
            faults.append('%s %s at %s: the image holds %s' % (entry['image'], item['what'], item['at'], found.hex(' ', 2)))
    return faults, read


def value_text(choice):
    if choice.get('unit') == 's':
        return number(choice['value']) + ' s'
    return power(choice['value'])


def fill(entry, text, read, faults, cited):
    """text's values filled in, and its citations as loop.md gives them
    when cited, else left out."""
    def value(match):
        return value_text(entry['values'][match.group(1)])

    def cite(match):
        addresses = [a.strip() for a in match.group(1).split(',')]
        for address in addresses:
            if address not in read:
                faults.append('%s cites %s, which it does not read' % (entry['image'], address))
        return ' (%s)' % ', '.join('`%s`' % a for a in addresses) if cited else ''
    return CITE.sub(cite, VALUE.sub(value, text))


def signed(value):
    text = '%g' % abs(value)
    return (MINUS if value < 0 else '') + text


def decoded(value):
    return power(value, DECODED_DIGITS)


def count(value):
    """A count, as a power of two where it is one."""
    exponent = int(math.log2(value)) if value > 0 else 0
    if 1 << exponent == value:
        return '2' + str(exponent).translate(SUPERSCRIPTS)
    return number(value)


class Block:
    """An EEPROM's calibration block."""

    def __init__(self, data, eeprom):
        at = int(data['block']['at'], 16) - int(data['block']['eeprom'], 16)
        self.bytes = eeprom[at:at + data['block']['size']]
        total = sum(self.bytes[2:]) & BYTE_MASK
        self.valid = struct.unpack('>H', self.bytes[:2])[0] == (total << 8) | (~total & BYTE_MASK)
        self.erased = all(b == ERASED_BYTE for b in self.bytes)

    def long(self, offset):
        at = int(offset, 16)
        return struct.unpack('>I', self.bytes[at:at + 4])[0]

    def single(self, offset):
        at = int(offset, 16)
        return struct.unpack('>f', self.bytes[at:at + 4])[0]

    def word(self, offset):
        at = int(offset, 16)
        return struct.unpack('>H', self.bytes[at:at + 2])[0]

    def byte(self, offset):
        return self.bytes[int(offset, 16)]

    def state(self):
        if self.erased:
            return 'block erased'
        return 'block valid' if self.valid else 'block\'s checksum bad'

    def selector(self, layout, last=None):
        """The type or kind the block gives."""
        chosen = self.byte(layout['type']) if self.valid else 0
        if chosen == ERASED_BYTE or (last is not None and chosen > last):
            return 0
        return chosen

    def calibration(self, layout, rom, selected):
        """A and B when the block's EFC calibration is valid, else
        None."""
        if not self.valid:
            return None
        at = int(layout['mask'], 16)
        mask = struct.unpack('>I', rom[at:at + 4])[0]
        for name in ('dadc', 'a', 'b'):
            if self.long(layout[name]) & mask == mask:
                return None
        if self.long(layout['a']) == 0:
            return None
        if self.byte(layout['type']) not in (selected, ERASED_BYTE):
            return None
        return self.single(layout['a']), self.single(layout['b'])


def decode_fixed(entry, block, data, rom):
    text = block.state()
    if block.valid:
        values = []
        for field in entry['fields']:
            if field['type'] == 'word':
                word = block.word(field['offset'])
                values.append('%s %d' % (field['name'], 0 if word == ERASED_WORD else word))
            else:
                values.append('%s %s' % (field['name'], signed(block.single(field['offset']))))
        text += ': ' + ', '.join(values)
    return '%s; G %s, the image\'s' % (text, power(entry['values']['gain']['value']))


def decode_type(entry, block, data, rom):
    layout = entry['block']
    selected = block.selector(layout, layout['types'] - 1)
    table = float_at(rom, int(layout['table'], 16) + selected * layout['entry'])
    calibration = block.calibration(layout, rom, selected)
    text = '%s, type %d' % (block.state(), selected)
    if calibration is None:
        return '%s, no EFC calibration: G %s, the type\'s' % (text, power(table))
    a, b = calibration
    unused = '' if b == 0 else ', its B %s unused' % decoded(b)
    return '%s, A %s%s: G %s in place of the type\'s %s' % (text, decoded(a), unused, decoded(a), power(table))


def decode_kind(entry, block, data, rom):
    layout, values = entry['block'], entry['values']
    selected = block.selector(layout)
    if selected == 0:
        default = values['a0']['value'], values['b0']['value']
    else:
        default = values['a1']['value'], 0
    tau = values['tau1' if selected == layout['kind_tau'] else 'tau0']
    calibration = block.calibration(layout, rom, selected)
    u = data['efc']
    text = '%s, kind %d' % (block.state(), selected)
    if calibration is None:
        a, b = default
        text += ', no EFC calibration: the defaults'
    else:
        a, b = calibration
        text += ', A %s and B %s' % (decoded(a), decoded(b))
    text += ': G %s at EFC 0 and %s at u = %s' % (decoded(a), decoded(a + b * u * u), count(u))
    if calibration is not None:
        text += ', where the defaults give %s and %s' % (decoded(default[0]), decoded(default[0] + default[1] * u * u))
    return '%s; τ %s' % (text, value_text(tau))


DECODERS = {'fixed': decode_fixed, 'type': decode_type, 'kind': decode_kind}


def held(data, entry, faults, link):
    """The cell for the EEPROMs whose unit runs entry's image."""
    results = []
    for eeprom in data['eeproms']:
        if eeprom['image'] != entry['image'] or eeprom.get('synthetic'):
            continue
        decoder = DECODERS.get(entry['rule'])
        if decoder is None:
            faults.append('%s: rule %s decodes no EEPROM' % (eeprom['file'], entry['rule']))
            continue
        block = Block(data, (EEPROMS / eeprom['file']).read_bytes())
        results.append(link(eeprom['file']) + ': ' + decoder(entry, block, data, image(entry['image'])))
    return results


def check_eeproms(data, faults):
    listed = [eeprom['file'] for eeprom in data['eeproms']]
    present = sorted(path.name for path in EEPROMS.glob('*.bin'))
    for name in sorted(set(present) - set(listed)):
        faults.append('%s: an EEPROM loop-gain.json does not list' % name)
    for name in sorted(set(listed) - set(present)):
        faults.append('%s: listed, not in %s' % (name, EEPROMS.relative_to(ROOT)))
    images = {entry['image'] for entry in data['images']}
    for eeprom in data['eeproms']:
        if eeprom['image'] not in images:
            faults.append('%s: no image %s' % (eeprom['file'], eeprom['image']))


def evidence_table(data, faults):
    lines = ['| Image | G comes from | Can the EEPROM change it? | The EEPROMs held |',
             '| ----- | ------------ | ------------------------- | ---------------- |']
    for entry in data['images']:
        _, read = check_image(entry)
        results = held(data, entry, faults, lambda name: '[`%s`](%s%s)' % (name, EVIDENCE_LINK, name))
        lines.append('| %s %s | %s | %s: %s | %s |' % (
            entry['model'], entry['firmware'],
            fill(entry, entry['source'], read, faults, True),
            entry['override']['answer'], fill(entry, entry['override']['text'], read, faults, True),
            '; '.join(results) if results else 'None held'))
    return lines


def page_table(data, faults):
    lines = [
        '<div class="wide" data-scroll="x"><table class="models">',
        '  <colgroup><col style="width:13%"><col style="width:29%"><col style="width:29%"><col style="width:29%"></colgroup>',
        '  <thead><tr><th>Firmware</th><th>G comes from</th><th>Can the EEPROM change it?</th>'
        '<th>The EEPROMs held</th></tr></thead>',
        '  <tbody>',
    ]
    for entry in data['images']:
        _, read = check_image(entry)
        results = held(data, entry, [], lambda name: '<a href="%s%s"><code>%s</code></a>' % (PAGE_LINK, name, name))
        cells = [
            '%s %s' % (entry['model'], entry['firmware']),
            code(fill(entry, entry['source'], read, faults, False)),
            '%s: %s' % (entry['override']['answer'],
                       code(fill(entry, entry['override'].get('page', entry['override']['text']), read, faults, False))),
            '<br>'.join(results) if results else 'None held',
        ]
        lines.append('    <tr>%s</tr>' % ''.join('<td>%s</td>' % cell for cell in cells))
    lines += ['  </tbody>', '</table></div>']
    return lines


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--write', action='store_true', help='rewrite the tables in the documents')
    arguments = parser.parse_args()
    data = json.loads(DATA.read_text())
    faults = []
    for entry in data['images']:
        faults += check_image(entry)[0]
    check_eeproms(data, faults)
    tables = {'evidence': evidence_table(data, faults), 'page': page_table(data, faults)}
    for fault in dict.fromkeys(faults):
        print('%s: %s' % (SCRIPT, fault))
    drifted = MARKERS.update({EVIDENCE: 'evidence', PAGE: 'page'}, tables, arguments.write)
    if drifted:
        print('%s: not what %s gives: %s; make loop-gain rewrites them'
              % (SCRIPT, DATA.relative_to(ROOT), ', '.join(drifted)))
    return 1 if faults or drifted else 0


if __name__ == '__main__':
    sys.exit(main())
