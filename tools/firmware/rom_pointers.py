"""Checks the ROM pointers of an image's hand-written C, which the
function check, comparing data pointers with their addresses masked,
passes over, and with --check fails on any its allowlist does not
justify.

    python3 -I tools/firmware/rom_pointers.py --image IMAGE [--check] [--all]
    python3 -I tools/firmware/rom_pointers.py --self-test

C copied from another image's may keep that image's addresses or text
and still pass the function check, so each pointer into the image's
ROM, from LOWEST_ADDRESS to its end, is checked by what the image holds
there, as its names files and bytes give it, in this order: a label's
start; a function's start; code, inside a function's body
(image_diff.py's, to its last unconditional transfer); inside a label
whose type has a size other than a char array's (m68k gcc's sizeof, from
the image's smartclock.h); a string's start, text (printable or a line's
control) to a NUL after a byte that is not text, after a function's last
rts or the like, or at a label; inside a string; inside any other label
(to the next name, at most comment_addresses.py's LABEL_EXTENT); or
other.  A label of a char type, or of none, holding text is compared by
that text.  The findings, by kind:

- literal: a hexadecimal literal cast to a pointer, in the image's
  rewritten/IMAGE C less its types/ and generated SCPI trees, that
  points into the ROM at anything but a string's or a label's start;
- text: such a literal followed by a comment of a C string, the text
  the C expects there, where the ROM's text differs;
- operand: for each function the image's C writes by hand that its diff
  against a base (docs/firmware/diff/IMAGE-vs-BASE.tsv) pairs identical
  or relocated with one the base writes by hand, the two bodies'
  absolute ROM operands, instruction by instruction, where they differ:
  the two must point at text of the same text, both at its start or
  both inside it, or else at the same bytes (masked as
  comment_addresses.py masks them) or the same kind of object: code at
  code, a label at a label, inside a label at the same offset.  One that
  does not is explained, not a finding, where the image's function's C
  gives the image's object rather than the base's: the image's text as
  a string literal, or inside one (SHORTEST_EVIDENCE characters at
  least); a named constant of that text, or of a pointer literal of the
  image's address; or the name of the label, the image's or the
  base's, that holds it, which the image's link places.

--check fails (exits 1) on a finding the image's allowlist,
analysis/IMAGE/rom-pointer-allow.tsv, does not justify, or on a row
there no finding matches, and prints those findings alone.  The
allowlist is tab-separated: kind, where (the file under rewritten/IMAGE/
of a literal, the function of an operand), base ('-' for a literal),
at (the literal as written, or the operand's instruction's address),
found (the object's kind, an operand's base's and image's as
"BASE -> IMAGE") and the reason it stays.  --all prints every pointer
checked and the allowed findings too.  The counts go to standard error.
--self-test checks the classifier, the agreement and explanation rules
and the C's parsing on synthetic input.  Needs m68k-linux-gnu gcc and
binutils.
"""

import argparse
import bisect
import collections
import dataclasses
import importlib.util
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]


def load(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / 'tools/firmware' / (name + '.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


image_diff = load('image_diff')
comment_addresses = load('comment_addresses')
diff_figures = load('diff_figures')
seed_names = load('seed_names')

DIFFS = ROOT / 'docs/firmware/diff'
COMMON = comment_addresses.REWRITTEN / 'common'
ALLOWLIST = 'rom-pointer-allow.tsv'
# The kinds of object a pointer points at.
FUNCTION = 'function'
CODE = 'code'
LABEL = 'label'
STRING = 'string'
MID_STRING = 'mid-string'
IN_LABEL = 'in label'
OTHER = 'other'
# The kinds that agree with each other as code.
CODE_KINDS = (FUNCTION, CODE)
# The kinds a pointer literal may point at.
LITERAL_KINDS = (STRING, LABEL)
# The findings' kinds.
LITERAL, TEXT, OPERAND = FINDING_KINDS = ('literal', 'text', 'operand')
# The diff statuses whose two bodies pair instruction by instruction.
PAIRED = ('identical', 'relocated')
# The diff's region of the primary code, and comment_addresses.py's.
PRIMARY = 'primary'
PRIMARY_REGION = ''
# The shortest text taken as the C's own inside a longer literal: a
# shorter one ("S3", "%d") may be in one by chance.  A literal of the
# text alone is taken at any length.
SHORTEST_EVIDENCE = 3
# The longest text read at a pointer.
STRING_LIMIT = image_diff.STRING_LIMIT
# A label type whose bytes may be text.
CHARACTER_TYPE = re.compile(r'^(?:const\s+)?(?:unsigned\s+|signed\s+)?char\b')
# A literal cast to a pointer, (T *)0x..., its type's qualifiers in
# either place, a record's type with its keyword or without.
POINTER_LITERAL = re.compile(r'\(\s*(?:const\s+|volatile\s+)*(?:(?:struct|union)\s+)?\w+'
                             r'(?:\s+const|\s+volatile)*\s*\*+\s*(?:const\s*)?\)\s*(0x[0-9a-fA-F]+)\b')
# The comment after such a literal, past its comma or semicolon, that
# gives the text it points at as a C string.
EXPECTED_TEXT = re.compile(r'\s*[,;]?\s*/\*\s*("(?:\\.|[^"\\])*")\s*\*/')
# A C string literal, adjacent ones joined.
C_STRING = re.compile(r'"(?:\\.|[^"\\\n])*"(?:\s*"(?:\\.|[^"\\\n])*")*')
STRING_PIECE = re.compile(r'"((?:\\.|[^"\\\n])*)"')
COMMENT = re.compile(r'/\*.*?\*/|//[^\n]*', re.S)
IDENTIFIER = re.compile(r'\b[A-Za-z_]\w*\b')
# A named constant of the C: an array of a string, NAME[] = "...", or a
# macro of one, and a literal cast to a pointer, NAME = (T *)0x....
TEXT_CONSTANT = re.compile(r'(?:\b(\w+)\s*\[\s*\w*\s*\]\s*=|^#define\s+(\w+))\s*(' + C_STRING.pattern + ')', re.M)
POINTER_CONSTANT = re.compile(r'\b(\w+)\s*=\s*' + POINTER_LITERAL.pattern)


def is_text_byte(byte):
    return 32 <= byte < 127 or byte in image_diff.TEXT_CONTROLS


def unescape(literal):
    """A C string literal's text, adjacent pieces joined."""
    joined = ''.join(STRING_PIECE.findall(literal))
    return joined.encode('latin-1').decode('unicode_escape')


def c_strings(text):
    """The string literals of C, its comments left out."""
    return [unescape(match.group()) for match in C_STRING.finditer(COMMENT.sub(' ', text))]


@dataclasses.dataclass(frozen=True)
class Target:
    """What a pointer points at: its kind; name, the function's or
    label's that holds it; offset, from that one's start; and text, the
    text there of a string or of a label that holds text."""
    kind: str
    name: str = ''
    offset: int = 0
    text: str | None = None

    def __str__(self):
        where = '%s+0x%x' % (self.name, self.offset) if self.offset else self.name
        parts = [self.kind] + ([where] if where else []) + ([repr(self.text)] if self.text is not None else [])
        return ' '.join(parts)


def agree(base, image, same_bytes=False):
    """Whether two pointers, the base's and the image's, point at the
    same kind of object: text at the same text, both at its start or
    both inside it; else the same bytes (same_bytes, the caller's to
    say), code at code, a label at a label, inside a label at the same
    offset."""
    if base.text is not None or image.text is not None:
        return base.text == image.text and (base.kind == MID_STRING) == (image.kind == MID_STRING)
    if same_bytes:
        return True
    if base.kind in CODE_KINDS or image.kind in CODE_KINDS:
        return base.kind in CODE_KINDS and image.kind in CODE_KINDS
    if base.kind == IN_LABEL and image.kind == IN_LABEL:
        return base.offset == image.offset
    return base.kind == image.kind


class Layout:
    """An image's primary memory as a pointer sees it: data, its bytes
    from address 0; labels, each start's (name, C type), '' for none;
    functions, each start's name; code, each function's body as
    (start, end), sorted; sizes, each label type's size where known."""

    def __init__(self, data, labels, functions, code, sizes):
        self.data = data
        self.labels = labels
        self.sizes = sizes
        self.functions = functions
        self.code = sorted(code)
        self.code_starts = [start for start, _ in self.code]
        self.named = sorted(set(labels) | set(functions))
        self.label_starts = sorted(labels)

    def in_rom(self, address):
        return image_diff.LOWEST_ADDRESS <= address < len(self.data)

    def text_at(self, address):
        """The text from an address to its NUL; None if there is none."""
        text, nul, _ = self.data[address:address + STRING_LIMIT].partition(b'\0')
        if not nul or not text or not all(is_text_byte(byte) for byte in text):
            return None
        return text.decode('latin-1')

    def starts_text(self, address):
        """Whether text at an address starts there: at a label, or after
        a byte that is not text or a function's last transfer."""
        return (address in self.labels or not is_text_byte(self.data[address - 1])
                or address % 2 == 0 and self.data[address - 2:address] in image_diff.TRANSFERS)

    def body_at(self, address):
        """The start of the function body holding an address; None if
        none."""
        i = bisect.bisect_right(self.code_starts, address) - 1
        if i >= 0 and address < self.code[i][1]:
            return self.code[i][0]
        return None

    def label_at(self, address):
        """The start of the label whose extent holds an address; None if
        none."""
        i = bisect.bisect_right(self.label_starts, address) - 1
        if i < 0:
            return None
        start = self.label_starts[i]
        size = self.size(start)
        if size is None:
            following = bisect.bisect_right(self.named, start)
            end = self.named[following] if following < len(self.named) else len(self.data)
            size = min(end - start, comment_addresses.LABEL_EXTENT)
        return start if address < start + size else None

    def size(self, start):
        """A label's size, from its type; None if unknown."""
        type_text = self.labels[start][1]
        return self.sizes.get(type_text) or image_diff.type_size(type_text)

    def classify(self, address):
        """What an address points at."""
        if address in self.labels:
            name, type_text = self.labels[address]
            holds_text = not type_text or CHARACTER_TYPE.match(type_text)
            return Target(LABEL, name, text=self.text_at(address) if holds_text else None)
        if address in self.functions:
            return Target(FUNCTION, self.functions[address])
        start = self.body_at(address)
        if start is not None:
            return Target(CODE, self.name_at(start), address - start)
        start = self.label_at(address)
        if start is not None and self.size(start) and not CHARACTER_TYPE.match(self.labels[start][1]):
            return Target(IN_LABEL, self.name_at(start), address - start)
        text = self.text_at(address)
        if text is not None:
            return Target(STRING if self.starts_text(address) else MID_STRING, text=text)
        start = self.label_at(address)
        if start is not None:
            return Target(IN_LABEL, self.name_at(start), address - start)
        return Target(OTHER)

    def name_at(self, address):
        if address in self.functions:
            return self.functions[address]
        return self.labels[address][0]


class Image(comment_addresses.Image):
    """comment_addresses.py's image with its primary memory as
    image_diff.py decodes it and its Layout."""

    def __init__(self, name):
        super().__init__(name)
        data = (comment_addresses.IMAGES / (name + '.bin')).read_bytes()
        self.code_memory = image_diff.region_memories(data)[PRIMARY]
        functions = {address: function for (region, address), function in self.functions.items()
                     if region == PRIMARY_REGION}
        labels = {address: (label, self.label_types.get((region, address), ''))
                  for (region, address), label in self.labels.items() if region == PRIMARY_REGION}
        names = sorted(set(functions) | set(labels))
        code = []
        for start in sorted(functions):
            stop = names[bisect.bisect_right(names, start)] if start < names[-1] else len(data)
            found = image_diff.body(self.code_memory, start, stop)
            if found.instructions:
                code.append((start, found.end))
        types = {type_text for _, type_text in labels.values() if type_text}
        self.layout = Layout(data, labels, functions, code, seed_names.label_sizes(str(self.folder), types))


@dataclasses.dataclass(frozen=True)
class Finding:
    """A pointer that fails, keyed as the allowlist gives it, with what
    it points at."""
    kind: str
    where: str
    base: str
    at: str
    found: str
    detail: str

    def key(self):
        return (self.kind, self.where, self.base, self.at, self.found)

    def __str__(self):
        return '\t'.join(self.key() + (self.detail,))


def literals(image, counts, every):
    """The findings of the C's pointer literals into the ROM."""
    folder = comment_addresses.REWRITTEN / image.name
    for path in image.sources():
        text = path.read_text()
        code = COMMENT.sub(lambda match: ' ' * len(match.group()), text)
        for match in POINTER_LITERAL.finditer(code):
            address = int(match.group(1), 16)
            if not image.layout.in_rom(address):
                continue
            target = image.layout.classify(address)
            counts[LITERAL, target.kind] += 1
            where = str(path.relative_to(folder))
            line = text.count('\n', 0, match.start()) + 1
            if every:
                print(f'literal\t{where}:{line}\t{match.group(1)}\t{target}')
            if target.kind not in LITERAL_KINDS:
                yield Finding(LITERAL, where, '-', match.group(1), target.kind, f'line {line}: {target}')
            expected = EXPECTED_TEXT.match(text, match.end())
            if expected and unescape(expected.group(1)) != target.text:
                yield Finding(TEXT, where, '-', match.group(1), target.kind,
                              f'line {line}: the C expects {unescape(expected.group(1))!r}; {target}')


def hand_written(image):
    """The image's C, its own before common/'s: each function's text by
    name, and each named constant's value by name, a text constant's
    text or a pointer constant's address."""
    bodies, constants = {}, {}
    paths = image.sources() + (sorted(COMMON.rglob('*.[ch]')) if COMMON.is_dir() else [])
    for path in paths:
        text = path.read_text()
        for start, end, name in comment_addresses.owners(text):
            bodies.setdefault(name, text[start:end + 1])
        code = COMMENT.sub(' ', text)
        for match in TEXT_CONSTANT.finditer(code):
            constants.setdefault(match.group(1) or match.group(2), unescape(match.group(3)))
        for match in POINTER_CONSTANT.finditer(code):
            constants.setdefault(match.group(1), int(match.group(2), 16))
    return bodies, constants


def explained(base, target, value, body, constants):
    """Whether a function's C gives the image's object rather than an
    address or text copied from the base's: it has the image's text as a
    string literal, or in one, or names a constant of that text or of the
    image's address, or names the label, the image's or the base's,
    that holds it, which the image's link places."""
    if target.text is not None and any(
            target.text == literal or len(target.text) >= SHORTEST_EVIDENCE and target.text in literal
            for literal in c_strings(body)):
        return True
    names = set(IDENTIFIER.findall(COMMENT.sub(' ', body)))
    if any(constants.get(name) in (value, target.text) for name in names if name in constants):
        return True
    return any(found.kind in (LABEL, IN_LABEL) and found.name in names for found in (base, target))


def pairs(image, base):
    """The functions the diff against a base pairs instruction by
    instruction, both written by hand: (name, base start, base size,
    start, size)."""
    path = DIFFS / f'{image.name}-vs-{base.name}.tsv'
    for row in diff_figures.read_rows(path):
        if row['region'] != PRIMARY or row['kind'] != 'function' or row['status'] not in PAIRED:
            continue
        address = int(row['other_address'], 16)
        name = image.functions.get((PRIMARY_REGION, address))
        if name in image.rewritten and row['name'] in base.rewritten:
            yield name, int(row['base_address'], 16), int(row['base_size']), address, int(row['other_size'])


def operands(image, base, functions, constants, counts, every):
    """The findings of the absolute ROM operands that differ between the
    base's and the image's copies of a function."""
    for name, base_start, base_size, start, size in pairs(image, base):
        counts[OPERAND, 'functions'] += 1
        theirs = base.code_memory.instructions(base_start, base_start + base_size)
        ours = image.code_memory.instructions(start, start + size)
        for their_instruction, our_instruction in zip(theirs, ours):
            their_fields = [f for f in image_diff.fields(their_instruction) if not f.relative]
            our_fields = [f for f in image_diff.fields(our_instruction) if not f.relative]
            for theirs_field, ours_field in zip(their_fields, our_fields):
                base_value, value = theirs_field.target, ours_field.target
                if base_value == value or not (base.layout.in_rom(base_value) and image.layout.in_rom(value)):
                    continue
                base_target, target = base.layout.classify(base_value), image.layout.classify(value)
                counts[OPERAND, 'pairs'] += 1
                base_bytes = base.masked(PRIMARY_REGION, base_value)
                same_bytes = base_bytes is not None and base_bytes == image.masked(PRIMARY_REGION, value)
                ok = agree(base_target, target, same_bytes)
                if every:
                    print(f'operand\t{name}\t{base.name}\t{our_instruction.address:#x}\t'
                          f'{"agrees" if ok else "differs"}\t{base_target} -> {target}')
                if ok:
                    continue
                body = functions.get(name, '')
                if explained(base_target, target, value, body, constants):
                    counts[OPERAND, 'explained'] += 1
                    continue
                note = ''
                if base_target.text is not None and any(base_target.text in literal for literal in c_strings(body)):
                    note = "; the C has the base's text"
                yield Finding(OPERAND, name, base.name, f'{our_instruction.address:#x}',
                              f'{base_target.kind} -> {target.kind}', f'{base_target} -> {target}{note}')


def bases(name):
    """The images an image's diffs are against."""
    prefix = name + '-vs-'
    return sorted(path.stem[len(prefix):] for path in DIFFS.glob(prefix + '*.tsv'))


def read_allowlist(path):
    """The allowlist's rows, {key: reason}."""
    allowed = {}
    if not path.exists():
        return allowed
    for number, line in enumerate(path.read_text().splitlines(), 1):
        if not line.strip() or line.startswith('#'):
            continue
        cells = line.split('\t')
        if len(cells) != 6 or not cells[5].strip():
            sys.exit(f'{path}:{number}: want kind, where, base, at, found and a reason')
        allowed[tuple(cells[:5])] = cells[5]
    return allowed


def self_test():
    """Fails unless the classifier, agree, explained and the C's
    parsing read synthetic input as they should."""
    data = bytearray(0x500)
    at = image_diff.LOWEST_ADDRESS
    # f: a body of nop, nop, rts, then a string right after the rts
    data[at:at + 6] = bytes.fromhex('4e714e714e75')
    data[at + 6:at + 12] = b'Hello\0'
    # table: eight words, text inside them; name: a char[4] label holding
    # text; a line's end after a NUL
    data[at + 0x14:at + 0x17] = b'AP\0'
    data[at + 0x20:at + 0x24] = b'abc\0'
    data[at + 0x41:at + 0x44] = b'\r\n\0'
    layout = Layout(bytes(data), {at + 0x10: ('table', 'uint16_t[8]'), at + 0x20: ('name', 'char[4]')},
                    {at: 'f'}, [(at, at + 6)], {})
    cases = (
        (at, Target(FUNCTION, 'f')),
        (at + 4, Target(CODE, 'f', 4)),
        (at + 6, Target(STRING, text='Hello')),
        (at + 9, Target(MID_STRING, text='lo')),
        (at + 0x14, Target(IN_LABEL, 'table', 4)),
        (at + 0x20, Target(LABEL, 'name', text='abc')),
        (at + 0x22, Target(MID_STRING, text='c')),
        (at + 0x30, Target(OTHER)),
        (at + 0x41, Target(STRING, text='\r\n')),
    )
    failures = [f'{address:#x}: {layout.classify(address)}, want {want}'
                for address, want in cases if layout.classify(address) != want]
    agreements = (
        (Target(STRING, text='abc'), Target(LABEL, 'name', text='abc'), False, True),
        (Target(STRING, text='Hello'), Target(STRING, text='Hellp'), False, False),
        (Target(STRING, text='Hello'), Target(STRING, text='Hellp'), True, False),
        (Target(MID_STRING, text='lo'), Target(STRING, text='lo'), False, False),
        (Target(FUNCTION, 'f'), Target(CODE, 'g', 2), False, True),
        (Target(IN_LABEL, 'table', 4), Target(IN_LABEL, 'list', 6), False, False),
        (Target(LABEL, 'table'), Target(LABEL, 'list'), False, True),
        (Target(LABEL, 'table'), Target(STRING, text='abc'), False, False),
        (Target(CODE, 'f', 2), Target(OTHER), False, False),
        (Target(CODE, 'f', 2), Target(OTHER), True, True),
    )
    failures += [f'agree({base}, {image}, {same_bytes}) is not {want}'
                 for base, image, same_bytes, want in agreements if agree(base, image, same_bytes) != want]
    body = 'x("ab\\r\\n"\n  "cd"); /* "not" */ y = "q\\""; z(TAG, POINTER);'
    if c_strings(body) != ['ab\r\ncd', 'q"']:
        failures.append(f'c_strings: {c_strings(body)}')
    constants = {'TAG': 'S3', 'POINTER': at + 6}
    explanations = (
        (Target(OTHER), Target(STRING, text='b\r\nc'), 0, body, True),
        (Target(OTHER), Target(STRING, text='not'), 0, body, False),
        (Target(OTHER), Target(STRING, text='S3'), 0, body, True),
        (Target(OTHER), Target(STRING, text='S4'), 0, body, False),
        (Target(OTHER), Target(STRING, text='Q'), 0, 'f("Q");', True),
        (Target(OTHER), Target(STRING, text='Q'), 0, 'f("QR");', False),
        (Target(OTHER), Target(OTHER), at + 6, body, True),
        (Target(OTHER), Target(OTHER), at + 7, body, False),
        (Target(OTHER), Target(LABEL, 'name'), 0, 'f(name);', True),
        (Target(IN_LABEL, 'name', 2), Target(OTHER), 0, 'f(name[2]);', True),
        (Target(OTHER), Target(LABEL, 'name'), 0, 'f(names); /* name */', False),
    )
    failures += [f'explained({base}, {target}, {value:#x}, {text!r}) is not {want}'
                 for base, target, value, text, want in explanations
                 if explained(base, target, value, text, constants) != want]
    source = ('static char* const A = (char*)0x406; /* "Hello" */\nT* const B = (const struct t *const)0x420;\n'
              'static const char TAG[] = "S" "3";\n#define LINE "\\r\\n"\n')
    found = [(match.group(1), EXPECTED_TEXT.match(source, match.end())) for match in POINTER_LITERAL.finditer(source)]
    if [value for value, _ in found] != ['0x406', '0x420'] or unescape(found[0][1].group(1)) != 'Hello' or found[1][1]:
        failures.append(f'pointer literals: {found}')
    texts = {match.group(1) or match.group(2): unescape(match.group(3)) for match in TEXT_CONSTANT.finditer(source)}
    pointers = {match.group(1): match.group(2) for match in POINTER_CONSTANT.finditer(source)}
    if texts != {'TAG': 'S3', 'LINE': '\r\n'} or pointers != {'A': '0x406', 'B': '0x420'}:
        failures.append(f'constants: {texts}, {pointers}')
    if failures:
        sys.exit('self-test:\n  ' + '\n  '.join(failures))
    print(f'self-test: {len(cases)} classifications, {len(agreements)} agreements, {len(explanations)} '
          "explanations and the C's parsing as expected")


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--image')
    parser.add_argument('--check', action='store_true', help='print and fail on the findings the allowlist lacks')
    parser.add_argument('--all', action='store_true', help='print every pointer, and the allowed findings')
    parser.add_argument('--self-test', action='store_true')
    arguments = parser.parse_args()
    if arguments.self_test:
        self_test()
        return
    if not arguments.image:
        parser.error('--image or --self-test')
    image = Image(arguments.image)
    counts = collections.Counter()
    findings = list(literals(image, counts, arguments.all))
    functions, constants = hand_written(image)
    for base_name in bases(image.name):
        findings += operands(image, Image(base_name), functions, constants, counts, arguments.all)
    allowlist = image.folder / ALLOWLIST
    allowed = read_allowlist(allowlist)
    matched = set()
    found = collections.Counter()
    for finding in findings:
        key = finding.key()
        if key in allowed:
            matched.add(key)
            found['allowed'] += 1
            if arguments.all:
                print(f'{finding}\t(allowed: {allowed[key]})')
            continue
        found[finding.kind] += 1
        print(finding)
    stale = [key for key in allowed if key not in matched]
    for key in stale:
        print(f'{allowlist.relative_to(ROOT)}: no finding for {"\t".join(key)}', file=sys.stderr)
    by_kind = sorted((kind, count) for (group, kind), count in counts.items() if group == LITERAL)
    print(f'{image.name}: literals {sum(count for _, count in by_kind)} ('
          + ', '.join(f'{kind} {count}' for kind, count in by_kind) + '); '
          + f'operands differing {counts[OPERAND, "pairs"]} in {counts[OPERAND, "functions"]} paired functions, '
          + f'{counts[OPERAND, "explained"]} explained by the C; '
          + 'findings: ' + ', '.join(f'{kind} {found[kind]}' for kind in FINDING_KINDS)
          + f'; allowed {found["allowed"]}', file=sys.stderr)
    if arguments.check and (stale or any(found[kind] for kind in FINDING_KINDS)):
        sys.exit(1)


if __name__ == '__main__':
    main()
