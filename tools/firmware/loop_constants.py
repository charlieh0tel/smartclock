"""Checks the disciplining loop's constants by model, the one source
docs/firmware/loop-constants.json, against the firmware images, and the
two tables made from it against it: docs/firmware/loop.md's, the
evidence, with the addresses, and docs/pages/loop.html's "By model",
the explanation, with what follows from each τ.  With --write it
rewrites the tables.

    python3 -I tools/firmware/loop_constants.py [--write]

The data, one JSON object:

    law      the update law's constants the page's columns follow from:
             prefilter (a = prefilter / τ), integral_scale
             (k = 1 / (integral_scale·G·τ²)), update_seconds, the
             seconds between updates, and ramp_step, the seconds
             startup_pll lengthens its constant by each update
    images   one entry an image, in the tables' order: image (its
             third_party/firmware/images/IMAGE.bin), model, firmware,
             and its quantities, tau (τ, seconds), startup (startup_pll's
             first constant, seconds), gain (G) and recovery (the
             longest holdover after which holdover recovery keeps
             startup's constant, seconds; null where not traced)

A quantity is {values, by}: values, the choices the image has, each
{value, rom, traced}; by, what chooses among them or sets it,
{text, cite, page}.  rom lists where the image holds the value, each
{at, kind, offset}: kind block, a float at offset from at, the start of
the ROM block of defaults the tables cite; immediate, the operand of a
move.l #imm (M68000PRM, MOVE: size bits 10, source mode 7 register 4,
in the opcode word before it); float, a float at at.  Each is read from
the image and must be the value.  traced false marks a value present in
the image whose setter was not traced.  by's text is Markdown, cite the
addresses loop.md gives after it, page the page's text where it differs.

The page's a, slowest pole and startup ramp are computed from law and
each τ: the pole from the ten-second update closed around the
oscillator, as crates/smartclock/tests/loop_firmware.rs's poles() takes
it, rounded to 10 s; the ramp as startup_pll steps it, rounded to
100 s.  That test checks both against the firmware's own traces.  Its
last column is recovery, the holdover past which the ramp starts again.

Each table sits between the markers

    <!-- loop_constants.py evidence | page -->
    <!-- end loop_constants.py -->

and the rest of each document is written by hand.  Exits nonzero on
drift.
"""

import argparse
import html
import json
import math
import pathlib
import re
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
DATA = ROOT / 'docs/firmware/loop-constants.json'
IMAGES = ROOT / 'third_party/firmware/images'
EVIDENCE = ROOT / 'docs/firmware/loop.md'
PAGE = ROOT / 'docs/pages/loop.html'
SCRIPT = 'tools/firmware/loop_constants.py'
# an image's quantities, in loop.md's columns' order
QUANTITIES = ('tau', 'startup', 'gain', 'recovery')
# loop.md's cell for a quantity not traced
UNTRACED = 'not traced'

MARKER = re.compile(r'^([ \t]*)<!-- loop_constants\.py (evidence|page) -->\n.*?^[ \t]*<!-- end loop_constants\.py -->$',
                    re.M | re.S)

# move.l #imm,<ea>: the size bits and the source field of its opcode
MOVE_SIZE_MASK = 0xf000
MOVE_LONG = 0x2000
SOURCE_MASK = 0x003f
SOURCE_IMMEDIATE = 0x003c

# what the page rounds the slowest pole's and the ramp's seconds to
POLE_ROUNDING = 10
RAMP_ROUNDING = 100

# Durand-Kerner's starting point and passes, as poles() takes them
ROOT_SEED = complex(0.4, 0.9)
ROOT_PASSES = 500

SUPERSCRIPTS = str.maketrans('-0123456789', '⁻⁰¹²³⁴⁵⁶⁷⁸⁹')
MINUS = '−'


def number(value):
    """A time or a coefficient as the tables write it: 500, 0.0595."""
    return '%g' % value


def power(value):
    """A gain, signed, as N × 10ⁿ."""
    mantissa, exponent = ('%e' % abs(value)).split('e')
    mantissa = mantissa.rstrip('0').rstrip('.')
    sign = MINUS if value < 0 else '+'
    return '%s%s × 10%s' % (sign, mantissa, str(int(exponent)).translate(SUPERSCRIPTS))


def code(text):
    """Markdown's backticks as the page's <code>."""
    return re.sub(r'`([^`]*)`', r'<code>\1</code>', html.escape(text, quote=False))


def float_at(rom, at):
    return struct.unpack('>f', rom[at:at + 4])[0]


def check_rom(data):
    """Each value against where its image holds it; the faults found."""
    faults = []
    for entry in data['images']:
        rom = (IMAGES / (entry['image'] + '.bin')).read_bytes()
        for name in QUANTITIES:
            for choice in (entry[name] or {'values': []})['values']:
                for place in choice.get('rom', []):
                    at = int(place['at'], 16)
                    where = '%s %s %s at %s' % (entry['image'], name, number(choice['value']), place['at'])
                    if place['kind'] == 'block':
                        at += int(place['offset'], 16)
                    elif place['kind'] == 'immediate':
                        opcode = struct.unpack('>H', rom[at - 2:at])[0]
                        if opcode & MOVE_SIZE_MASK != MOVE_LONG or opcode & SOURCE_MASK != SOURCE_IMMEDIATE:
                            faults.append('%s: no move.l #imm (opcode %#06x)' % (where, opcode))
                            continue
                    elif place['kind'] != 'float':
                        faults.append('%s: no kind %r' % (where, place['kind']))
                        continue
                    found = float_at(rom, at)
                    if not math.isclose(found, choice['value'], rel_tol=1e-6):
                        faults.append('%s: the image holds %g' % (where, found))
    return faults


def update(law, tau):
    """The update closed around the oscillator, a matrix on (phase at
    the update, f, I), G taken as 1: loop_firmware.rs's update()."""
    block = law['update_seconds']
    a = law['prefilter'] / tau
    proportional = 1 / tau
    integral = 1 / (law['integral_scale'] * tau * tau)
    held = (block + 1) / 2
    u = [0, proportional, 1]
    mean = [1 - held * u[0], -held * u[1], -held * u[2]]
    filtered = [a * mean[0], (1 - a) + a * mean[1], a * mean[2]]
    integrator = [block * integral * filtered[0], block * integral * filtered[1],
                  1 + block * integral * filtered[2]]
    phase = [1 - block * u[0], -block * u[1], -block * u[2]]
    return [phase, filtered, integrator]


def slowest_pole_seconds(law, tau):
    """The time constant of the update's slowest pole."""
    m = update(law, tau)
    trace = m[0][0] + m[1][1] + m[2][2]
    minors = (m[0][0] * m[1][1] - m[0][1] * m[1][0] + m[0][0] * m[2][2] - m[0][2] * m[2][0]
              + m[1][1] * m[2][2] - m[1][2] * m[2][1])
    determinant = (m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1])
                   - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
                   + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]))

    def value(z):
        return z ** 3 - trace * z ** 2 + minors * z - determinant

    roots = [complex(1, 0), ROOT_SEED, ROOT_SEED * ROOT_SEED]
    for _ in range(ROOT_PASSES):
        for i in range(3):
            denominator = 1
            for j in range(3):
                if j != i:
                    denominator *= roots[i] - roots[j]
            roots[i] -= value(roots[i]) / denominator
    slowest = max(math.log(abs(z)) / law['update_seconds'] for z in roots)
    return -1 / slowest


def ramp_seconds(law, start, tau):
    """startup_pll's ramp from start to tau: a step each update to
    within a step of τ, then one update to τ (loop_firmware.rs's
    ramp_from())."""
    step = law['ramp_step']
    return (math.ceil((tau - step - start) / step) + 1) * law['update_seconds']


def rounded(value, unit):
    return number(round(value / unit) * unit)


def cited(addresses):
    return ' (%s)' % ', '.join('`%s`' % a for a in addresses) if addresses else ''


def evidence_value(name, choice):
    if choice.get('traced', True) is False:
        return '%.1f present, %s' % (choice['value'], UNTRACED)
    text = power(choice['value']) if name == 'gain' else number(choice['value']) + ' s'
    return text + cited([place['at'] for place in choice.get('rom', [])])


def evidence_cell(name, quantity):
    if quantity is None:
        return UNTRACED
    text = ' or '.join(evidence_value(name, choice) for choice in quantity['values'])
    by = quantity.get('by')
    if by:
        text += ', ' + by['text'] + cited(by.get('cite', []))
    return text


def page_by(quantity):
    by = quantity.get('by')
    return ', ' + code(by.get('page', by['text'])) if by else ''


def evidence_table(data):
    lines = ['| Image | τ, as the ROM block sets it | Startup\'s constant | G | Longest holdover keeping startup\'s constant |',
             '| ----- | --------------------------- | ------------------ | - | ------------------------------------------- |']
    for entry in data['images']:
        cells = ['%s %s' % (entry['model'], entry['firmware'])]
        cells += [evidence_cell(name, entry[name]) for name in QUANTITIES]
        lines.append('| %s |' % ' | '.join(cells))
    return lines


def page_cells(law, entry):
    taus = [choice['value'] for choice in entry['tau']['values']]
    start = entry['startup']['values'][0]['value']
    return [
        ' or '.join(number(tau) for tau in taus) + ' s' + page_by(entry['tau']),
        ' or '.join(power(choice['value']) for choice in entry['gain']['values']) + page_by(entry['gain']),
        ' or '.join(number(law['prefilter'] / tau) for tau in taus),
        ' or '.join(rounded(slowest_pole_seconds(law, tau), POLE_ROUNDING) for tau in taus) + ' s',
        '≈ ' + ' or '.join(rounded(ramp_seconds(law, start, tau), RAMP_ROUNDING) for tau in taus) + ' s'
        + page_by(entry['startup']),
        recovery_cell(entry['recovery']),
    ]


def recovery_cell(quantity):
    """The page's cell for the holdover past which recovery starts the
    ramp again."""
    if quantity is None:
        return UNTRACED
    return 'over ' + ' or '.join(number(choice['value']) for choice in quantity['values']) + ' s'


def page_table(data):
    """The page's rows, an image's merged into the one before it when
    their cells are the same."""
    rows = []
    for entry in data['images']:
        cells = page_cells(data['law'], entry)
        if rows and rows[-1][2] == cells:
            same = rows[-1][1] == entry['model']
            rows[-1][0].append(entry['firmware'] if same else '%s %s' % (entry['model'], entry['firmware']))
            rows[-1][1] = entry['model']
            continue
        rows.append([['%s %s' % (entry['model'], entry['firmware'])], entry['model'], cells])
    lines = [
        '<div class="wide"><table class="models">',
        '  <colgroup><col style="width:15%"><col style="width:13%"><col style="width:22%">'
        '<col style="width:10%"><col style="width:12%"><col style="width:13%"><col style="width:15%"></colgroup>',
        '  <thead><tr><th>Firmware</th><th class="sym">τ</th><th class="sym">G</th><th class="sym">a</th>'
        '<th>Slowest pole, <span class="sym">≈ 2.7τ</span></th><th>Startup ramp</th>'
        '<th>Holdover that restarts the ramp</th></tr></thead>',
        '  <tbody>',
    ]
    for names, _, cells in rows:
        lines.append('    <tr>%s</tr>' % ''.join('<td>%s</td>' % cell for cell in [', '.join(names)] + cells))
    lines += ['  </tbody>', '</table></div>']
    return lines


def rewrite(text, tables):
    def replace(match):
        indent, which = match.group(1), match.group(2)
        body = [indent + line if line else line for line in tables[which]]
        return '\n'.join(['%s<!-- loop_constants.py %s -->' % (indent, which), *body,
                          '%s<!-- end loop_constants.py -->' % indent])
    return MARKER.sub(replace, text)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--write', action='store_true', help='rewrite the tables in the documents')
    arguments = parser.parse_args()
    data = json.loads(DATA.read_text())
    faults = check_rom(data)
    for fault in faults:
        print('%s: %s' % (SCRIPT, fault))
    tables = {'evidence': evidence_table(data), 'page': page_table(data)}
    drifted = []
    for path, which in ((EVIDENCE, 'evidence'), (PAGE, 'page')):
        current = path.read_text()
        if len(MARKER.findall(current)) != 1 or MARKER.search(current).group(2) != which:
            sys.exit('%s: %s has no one loop_constants.py %s block' % (SCRIPT, path.relative_to(ROOT), which))
        text = rewrite(current, tables)
        if text == current:
            continue
        if arguments.write:
            path.write_text(text)
        else:
            drifted.append(str(path.relative_to(ROOT)))
    if drifted:
        print('%s: not what %s gives: %s; make loop-constants rewrites them'
              % (SCRIPT, DATA.relative_to(ROOT), ', '.join(drifted)))
    return 1 if faults or drifted else 0


if __name__ == '__main__':
    sys.exit(main())
