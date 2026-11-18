"""What the tools that write the loop's tables share: reading a value
where an image holds it, writing a gain as the tables do, and rewriting
a table between its markers in a document.

A place is where an image holds a float: {at, kind, offset}, kind
block, a float at offset from at; immediate, the operand of a
move.l #imm (M68000PRM, MOVE: size bits 10, source mode 7 register 4,
in the opcode word before it); float, a float at at.
"""

import html
import math
import pathlib
import re
import struct

ROOT = pathlib.Path(__file__).resolve().parents[2]
IMAGES = ROOT / 'third_party/firmware/images'

# move.l #imm,<ea>: the size bits and the source field of its opcode
MOVE_SIZE_MASK = 0xf000
MOVE_LONG = 0x2000
SOURCE_MASK = 0x003f
SOURCE_IMMEDIATE = 0x003c

# how close a float read from an image must be to the value cited
FLOAT_TOLERANCE = 1e-6

SUPERSCRIPTS = str.maketrans('-0123456789', '⁻⁰¹²³⁴⁵⁶⁷⁸⁹')
MINUS = '−'


def number(value):
    """A time or a coefficient as the tables write it: 500, 0.0595."""
    return '%g' % value


def power(value, digits=None):
    """A gain, signed, as N × 10ⁿ; to digits significant digits when
    given, else as many as it has."""
    text = '%e' % abs(value) if digits is None else '%.*e' % (digits - 1, abs(value))
    mantissa, exponent = text.split('e')
    mantissa = mantissa.rstrip('0').rstrip('.') if '.' in mantissa else mantissa
    sign = MINUS if value < 0 else '+'
    return '%s%s × 10%s' % (sign, mantissa, str(int(exponent)).translate(SUPERSCRIPTS))


def code(text):
    """Markdown's backticks as the page's <code>."""
    return re.sub(r'`([^`]*)`', r'<code>\1</code>', html.escape(text, quote=False))


def float_at(rom, at):
    return struct.unpack('>f', rom[at:at + 4])[0]


def image(name):
    """The bytes of third_party/firmware/images/NAME.bin."""
    return (IMAGES / (name + '.bin')).read_bytes()


def place_fault(rom, place, value):
    """Why the float at place is not value, or None when it is."""
    at = int(place['at'], 16)
    if place['kind'] == 'block':
        at += int(place['offset'], 16)
    elif place['kind'] == 'immediate':
        opcode = struct.unpack('>H', rom[at - 2:at])[0]
        if opcode & MOVE_SIZE_MASK != MOVE_LONG or opcode & SOURCE_MASK != SOURCE_IMMEDIATE:
            return 'no move.l #imm (opcode %#06x)' % opcode
    elif place['kind'] != 'float':
        return 'no kind %r' % place['kind']
    found = float_at(rom, at)
    if not math.isclose(found, value, rel_tol=FLOAT_TOLERANCE):
        return 'the image holds %g' % found
    return None


class Markers:
    """The markers a tool writes a document's table between,

        <!-- SCRIPT WHICH -->
        <!-- end SCRIPT -->

    SCRIPT the tool's file name, WHICH which table."""

    def __init__(self, script):
        self.script = script
        self.pattern = re.compile(r'^([ \t]*)<!-- %s (\S+) -->\n.*?^[ \t]*<!-- end %s -->$'
                                  % (re.escape(script), re.escape(script)), re.M | re.S)

    def rewrite(self, text, tables):
        def replace(match):
            indent, which = match.group(1), match.group(2)
            body = [indent + line if line else line for line in tables[which]]
            return '\n'.join(['%s<!-- %s %s -->' % (indent, self.script, which), *body,
                              '%s<!-- end %s -->' % (indent, self.script)])
        return self.pattern.sub(replace, text)

    def update(self, documents, tables, write):
        """Each of documents, {path: which}, with its table rewritten,
        written when write; exits when a document lacks its one block.
        The documents that drifted, when not write."""
        drifted = []
        for path, which in documents.items():
            current = path.read_text()
            found = self.pattern.findall(current)
            if len(found) != 1 or found[0][1] != which:
                raise SystemExit('tools/firmware/%s: %s has no one %s %s block'
                                 % (self.script, path.relative_to(ROOT), self.script, which))
            text = self.rewrite(current, tables)
            if text == current:
                continue
            if write:
                path.write_text(text)
            else:
                drifted.append(str(path.relative_to(ROOT)))
        return drifted
