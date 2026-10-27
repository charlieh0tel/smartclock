# /// script
# requires-python = ">=3.11"
# dependencies = []
# ///
"""Reports the magic numbers in an image's hand-written C, rewritten/
with its installer/ and bindings/, and with --check fails on any its
allowlist does not justify.

    python3 -I tools/firmware/magic_check.py [--image IMAGE] [--check] [--all]

It needs only the standard library and clang (CLANG, else clang on
the path); make check-magic-numbers and magic-numbers run it.

The C is read as clang parses it within the image's export
(decompiled/IMAGE.c), which includes every hand-written file after the
types smartclock.h gives it: clang's JSON AST, --target=m68k-linux-gnu
and the parsing flags of DECOMPILED_CFLAGS, which check-decompiled
compiles the export with.  The generated SCPI trees (*scpi_tree_data.c)
are the image's data and the export's own decompiled bodies are not
hand-written; neither is looked at.

A literal is an integer written in the C, or one negated; not one a
macro's body spells, which a #define names.  What a literal is given
to, its target, is the other side of a comparison or assignment, the
variable it initializes, a function's parameter (FUNCTION#N), a record
field it initializes, the switch a case label is in, or a function's
result.  A field is RECORD.FIELD, an element NAME[].  The findings, one
a line, by kind:

  enum        a literal given to something whose type is an enum, which
              has an enumerator of its value, or for an enum of bits
              enumerators whose bits make it up: their names are given
  missing     the same, where the enum has none; 0 given to an enum of
              bits, the empty set, is not one
  coded       a literal given to an integer field or global that holds
              an enum's values without its type: smartclock.h's comment
              on the field or names.tsv's note on the global names the
              enum
  bits        a hex literal masking, setting or tested against a
              register of include/'s headers (mc68331.h, mc68681.h)
              where the header names a bit or field of that value, or
              masking something whose type is an enum with an
              enumerator of that value
  unnamed     a literal given to a field or variable that a named
              constant (a static const or #define) of the same value is
              given to elsewhere: its name is given
  duplicate   a scalar constant of the same name and initializer
              defined in two or more functions or files, but for a
              floating-point 0, 1, -1, 2 or 0.5: a candidate for one in
              the header all see
  argument    an unnamed for a function's parameter, which may take values
              of different meanings, an index of whatever table its
              first parameter is
  similar     a function's constant of the same value as a file-scope
              constant whose name ends in its own: the same thing named
              twice, or two things of one value
  repeated    a literal other than 0, 1 and -1 given to the same target
              in two or more functions: a candidate for a static const
              or #define where both see it.  Literals given to no
              target -- a shift by a bit's position, an index, an
              operand of arithmetic -- are not counted

--check fails (exits 1) if a finding of a checked kind, all but
argument, similar and repeated, since whether two uses mean the same is
a reader's call, is not in the allowlist, IMAGE/magic-allow.tsv, or if
an allowlist row matches no finding; it prints those findings alone.
The allowlist is tab-separated: kind, file (relative to rewritten/, or
common/ and its path there),
function, literal as written, target as the report prints it, and the
reason it stays a literal; no line numbers, so that a row survives
edits around it.  --all prints the allowed findings too.  The counts by
kind go to standard error.
"""

import argparse
import collections
import dataclasses
import json
import os
import pathlib
import re
import shlex
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
FIRMWARE = ROOT / 'third_party/firmware'
# A header's #include of another by its path from the header's folder.
HEADER_INCLUDE = re.compile(r'(?m)^#include "([^"]+)"$')
# The images' shared hand-written C (docs/firmware/rewrite.md, "Shared C").
COMMON = FIRMWARE / 'common'
INCLUDE = FIRMWARE / 'include'
DEFAULT_IMAGE = 'z3805a-3543b'
ALLOWLIST = 'magic-allow.tsv'
# The register headers whose #defines name bits.
REGISTER_HEADERS = ('mc68331.h', 'mc68681.h')
CLANG = os.environ.get('CLANG', 'clang')
CLANG_TARGET = '--target=m68k-linux-gnu'
# DECOMPILED_CFLAGS's flags that change how C parses; the rest are gcc's
# code generation, which clang need not know.
PARSING_FLAG = re.compile(r'^(-std=|-f(no-)?builtin$|-D|-U|-I|-w$|-W)')
# The flags check-decompiled gives that parsing needs, for a run outside
# make.
DEFAULT_CFLAGS = '-std=gnu17 -fno-builtin -w'
# Errors in clang that gcc only warns of, which the export's calls
# through loosely typed pointers raise.
CLANG_LENIENCY = ('-Wno-error=int-conversion', '-Wno-error=incompatible-pointer-types',
                  '-Wno-error=incompatible-function-pointer-types')
# The generated files, data from the image.
GENERATED = re.compile(r'scpi_tree_data\.c$')
# The kinds, and those --check fails on.
KINDS = ('enum', 'missing', 'coded', 'bits', 'unnamed', 'duplicate', 'argument', 'similar', 'repeated')
CHECKED_KINDS = KINDS[:-3]
# A function's parameter as a target.
PARAMETER = re.compile(r'#\d+$')
# The values never a magic number for repeated.
TRIVIAL = frozenset((0, 1, -1))
# The floating-point values a duplicate's is not counted for: the
# arithmetic's operands that the float library's calls take as
# constants.
TRIVIAL_FLOATS = frozenset((0.0, 1.0, -1.0, 2.0, 0.5))
# The operators that give a literal to their other operand, and those
# that mask with it.
GIVING_OPERATORS = frozenset(('==', '!=', '<', '<=', '>', '>=', '='))
MASK_OPERATORS = frozenset(('&', '|', '^', '&=', '|=', '^='))
# The macros that spell a literal without naming what it means.
NOT_NAMES = frozenset(('NULL', 'true', 'false'))
# The nodes a constant expression is made of, but for references.
CONSTANT_NODES = frozenset(('IntegerLiteral', 'CharacterLiteral', 'FloatingLiteral', 'BinaryOperator',
                            'UnaryOperator', 'ParenExpr', 'ImplicitCastExpr', 'CStyleCastExpr', 'ConstantExpr',
                            'UnaryExprOrTypeTraitExpr', 'ConditionalOperator'))
# The nodes a value passes through unchanged for this purpose.
TRANSPARENT = frozenset(('ImplicitCastExpr', 'ParenExpr', 'ConstantExpr', 'CStyleCastExpr'))
# The same less an explicit cast, which changes the type.
IMPLICIT = frozenset(('ImplicitCastExpr', 'ParenExpr', 'ConstantExpr'))
ENUM_TYPE = re.compile(r'^(?:(?:const|volatile) )*enum (\w+)$')
FUNCTION_RETURNS_ENUM = re.compile(r'^(?:(?:const|volatile) )*enum (\w+) \(')
INTEGER_TYPE = re.compile(r'^(?:(?:const|volatile) )*(?:(?:unsigned|signed) )?(?:char|short|int|long|long long|'
                          r'u?int(?:8|16|32)_t)$')
SCALAR_TYPE = re.compile(r'^(?:(?:const|volatile) )*(?:(?:unsigned|signed) )?(?:char|short|int|long|long long|'
                         r'u?int(?:8|16|32)_t|float|double)$')
ARRAY_TYPE = re.compile(r'^(.*?) ?\[\d*\]$')
DEFINE = re.compile(r'^\s*#\s*define\s+(\w+)\s+\(?(0x[0-9a-fA-F]+|\d+)[uUlL]*\)?\s*(?:/\*.*)?$')
# smartclock.h's field with a comment, its name and the comment's start;
# the type is left out where clang-format wraps it to the line above.
FIELD = re.compile(r'^\s+(?:[\w ]+?[\s*]+)?(\w+)(?:\[[^]]*\])?;\s*/\*(.*)$')
RECORD_END = re.compile(r'^}\s*(\w+)\s*;')
# An enum of bits ends so, whatever its enumerators.
BIT_ENUM_NAME = re.compile(r'_(bit|bits|failure|error|flag|mask)$')


@dataclasses.dataclass(frozen=True)
class Location:
    """Where a token is: its file as clang names it, line, and byte
    offset and length in the file."""
    file: str
    line: int
    offset: int
    length: int


@dataclasses.dataclass(frozen=True)
class Finding:
    """A magic number found: its kind, where, the function it is in, the
    literal as written, its target, and what the report says of it."""
    kind: str
    file: str
    line: int
    function: str
    literal: str
    target: str
    message: str

    def key(self):
        return (self.kind, self.file, self.function, self.literal, self.target)

    def __str__(self):
        return f'{self.kind}\t{self.file}:{self.line}\t{self.function}\t{self.literal}\t{self.target}\t{self.message}'


class Locator:
    """Follows clang's JSON locations, which leave out a file and line
    the same as the last location's written."""

    def __init__(self):
        self.file = None
        self.line = None

    def bare(self, location):
        if 'offset' not in location:
            return None
        self.file = location.get('file', self.file)
        self.line = location.get('line', self.line)
        return Location(self.file, self.line, location['offset'], location.get('tokLen', 0))

    def read(self, location):
        """A location's spelling and expansion, in the order clang writes
        them."""
        if 'spellingLoc' in location:
            return self.bare(location['spellingLoc']), self.bare(location['expansionLoc'])
        here = self.bare(location)
        return here, here


def included(header):
    """A header's text with each #include "PATH" replaced by that
    header's, so expanded in turn: smartclock.h with common/'s types."""
    return HEADER_INCLUDE.sub(lambda match: included(header.parent / match.group(1)), header.read_text())


def annotate(tree):
    """Gives every node its spelling and expansion location, '_spelling'
    and '_expansion': its loc, else its range's start; and its range's
    expansions, '_extent'."""
    locator = Locator()
    stack = [tree]
    while stack:
        node = stack.pop()
        if 'loc' in node:
            node['_spelling'], node['_expansion'] = locator.read(node['loc'])
        if 'range' in node:
            begin = locator.read(node['range']['begin'])
            node['_extent'] = begin[1], locator.read(node['range']['end'])[1]
            if '_spelling' not in node:
                node['_spelling'], node['_expansion'] = begin
        stack.extend(reversed(node.get('inner', ())))


def strip(node, through=TRANSPARENT):
    while node.get('kind') in through and node.get('inner'):
        node = node['inner'][-1]
    return node


def type_of(node):
    """A node's type, desugared."""
    kind = node.get('type', {})
    return kind.get('desugaredQualType', kind.get('qualType', ''))


def enum_of(node):
    """The enum a node's type is, before any implicit conversion."""
    match = ENUM_TYPE.match(type_of(strip(node, IMPLICIT)))
    return match and match.group(1)


def integer_literal(node):
    """The literal a node is, as (value, literal node, negated), or
    None."""
    node = strip(node)
    if node.get('kind') == 'IntegerLiteral':
        return int(node['value']), node, False
    if node.get('kind') == 'UnaryOperator' and node['opcode'] == '-':
        inner = strip(node['inner'][0])
        if inner.get('kind') == 'IntegerLiteral':
            return -int(inner['value']), inner, True
    return None


def record_of(type_name):
    return re.sub(r'^(?:(?:const|volatile|struct|union) )*|[ *]+$', '', type_name)


def walk_all(node, kinds):
    """The nodes under node of the kinds, through records and the kinds."""
    stack = [node]
    while stack:
        node = stack.pop()
        if node.get('kind') in kinds:
            yield node
        stack.extend(child for child in node.get('inner', ()) if child.get('kind') in kinds)


class Image:
    """An image's export parsed, with what the checker looks up in it."""

    def __init__(self, image):
        self.folder = FIRMWARE / image
        self.rewritten = self.folder / 'rewritten'
        self.export = self.folder / 'decompiled' / f'{image}.c'
        self.sources = {}
        self.enums = {}          # enum -> {value: [enumerator]}
        self.functions = {}      # function declaration id -> [parameter type]
        self.fields = {}         # field declaration id -> (record, field)
        self.records = {}        # record or its typedef -> [(field, type)]
        self.register_fields = set()  # the register headers' field declaration ids
        self.globals = set()     # file-scope variable declaration ids
        tree = self.parse()
        annotate(tree)
        self.declarations = tree['inner']
        self.index()
        self.bits = self.register_bits()
        self.coded = self.coded_fields()

    def parse(self):
        flags = [flag for flag in shlex.split(os.environ.get('DECOMPILED_CFLAGS', DEFAULT_CFLAGS))
                 if PARSING_FLAG.match(flag)]
        command = [CLANG, CLANG_TARGET, *flags, *CLANG_LENIENCY, '-fsyntax-only', '-Xclang', '-ast-dump=json',
                   self.export.name]
        result = subprocess.run(command, cwd=self.export.parent, capture_output=True, check=False)
        if result.returncode:
            sys.exit(f'{shlex.join(command)} failed:\n{result.stderr.decode()}')
        return json.loads(result.stdout)

    def path(self, name):
        return (self.export.parent / name).resolve()

    def source(self, name):
        if name not in self.sources:
            self.sources[name] = self.path(name).read_bytes()
        return self.sources[name]

    def text(self, location):
        return self.source(location.file)[location.offset:location.offset + location.length].decode()

    def extent_text(self, node):
        """A node's source as written, its whitespace made single spaces,
        if it is in one file."""
        begin, end = node.get('_extent', (None, None))
        if not begin or not end or begin.file != end.file:
            return None
        data = self.source(begin.file)[begin.offset:end.offset + end.length].decode(errors='replace')
        return ' '.join(data.split())

    def line_text(self, location):
        data = self.source(location.file)
        start = data.rfind(b'\n', 0, location.offset) + 1
        end = data.find(b'\n', location.offset)
        return data[start:end if end >= 0 else len(data)].decode(errors='replace')

    def hand_written(self, name):
        """The file relative to rewritten/, if it is hand-written C, or
        common/ and its path in it, if it is the images' shared C."""
        if name is None:
            return None
        path = self.path(name)
        if GENERATED.search(path.name):
            return None
        if path.is_relative_to(COMMON):
            return str(COMMON.name / path.relative_to(COMMON))
        if not path.is_relative_to(self.rewritten):
            return None
        return str(path.relative_to(self.rewritten))

    def index(self):
        for node in self.declarations:
            kind = node['kind']
            if kind == 'EnumDecl' and node.get('name'):
                self.index_enum(node)
            elif kind == 'VarDecl':
                self.globals.add(node['id'])
            elif kind == 'FunctionDecl':
                self.functions[node['id']] = [type_of(parameter) for parameter in node.get('inner', ())
                                              if parameter.get('kind') == 'ParmVarDecl']
            elif kind == 'RecordDecl':
                header = node.get('_spelling') and pathlib.Path(node['_spelling'].file).name
                for record in walk_all(node, ('RecordDecl',)):
                    fields = [field for field in record.get('inner', ()) if field.get('kind') == 'FieldDecl']
                    for field in fields:
                        self.fields[field['id']] = (record.get('name', ''), field.get('name', ''))
                        if header in REGISTER_HEADERS:
                            self.register_fields.add(field['id'])
                    if record.get('name') and record.get('completeDefinition'):
                        self.records[record['name']] = [(field.get('name', ''), type_of(field)) for field in fields]
        for node in self.declarations:
            if node['kind'] == 'TypedefDecl':
                target = record_of(type_of(node))
                if target in self.records:
                    self.records.setdefault(node['name'], self.records[target])

    def index_enum(self, node):
        values = self.enums.setdefault(node['name'], {})
        value = -1
        for constant in node.get('inner', ()):
            if constant.get('kind') != 'EnumConstantDecl':
                continue
            given = [child for child in constant.get('inner', ()) if 'value' in child]
            value = int(given[0]['value']) if given else value + 1
            values.setdefault(value, []).append(constant['name'])

    def bit_enum(self, enum):
        """Whether an enum's values are bits, which combine."""
        values = self.enums.get(enum, {})
        powers = all(value > 0 and value & (value - 1) == 0 for value in values)
        return powers and (len(values) > 1 or bool(BIT_ENUM_NAME.search(enum)))

    def enumerators(self, enum, value):
        """The enumerators naming a value: its own, or for an enum of bits
        those whose bits make it up; None if none does."""
        values = self.enums.get(enum, {})
        if value in values:
            return ' or '.join(values[value])
        if value > 0 and self.bit_enum(enum):
            bits = [bit for bit in values if value & bit]
            if sum(bits) == value:
                return ' | '.join(values[bit][0] for bit in sorted(bits))
        return None

    def register_bits(self):
        """The register headers' #defines, {value: [name]}."""
        bits = {}
        for header in REGISTER_HEADERS:
            for line in (INCLUDE / header).read_text().splitlines():
                match = DEFINE.match(line)
                if match and not match.group(1).endswith('_SHIFT'):
                    bits.setdefault(int(match.group(2), 0), []).append(match.group(1))
        return bits

    def coded_fields(self):
        """The integer fields and globals that hold an enum's values but are
        not typed with it: {record.field or global: enum}."""
        coded = {}
        enum_word = re.compile(r'\b(' + '|'.join(sorted(self.enums, key=len, reverse=True)) + r')\b')
        lines = included(self.folder / 'smartclock.h').splitlines()
        pending = []
        for number, line in enumerate(lines):
            if line.startswith('typedef'):
                pending = []
            match = FIELD.match(line)
            if match:
                comment = match.group(2)
                following = number + 1
                while '*/' not in comment and following < len(lines):
                    comment += ' ' + lines[following].strip()
                    following += 1
                named = enum_word.search(comment.split('*/')[0])
                if named:
                    pending.append((match.group(1), named.group(1)))
            end = RECORD_END.match(line)
            if end:
                coded.update((f'{end.group(1)}.{field}', enum) for field, enum in pending)
                pending = []
        coded = {target: enum for target, enum in coded.items()
                 if INTEGER_TYPE.match(self.field_type(target) or 'int')}
        for row in (self.folder / 'names.tsv').read_text().splitlines():
            cells = row.split('\t')
            if len(cells) > 5 and cells[1] == 'label' and INTEGER_TYPE.match(cells[5].strip()):
                named = enum_word.search(cells[4])
                if named:
                    coded[cells[2]] = named.group(1)
        return coded

    def field_type(self, target):
        record, _, name = target.partition('.')
        return dict(self.records.get(record, ())).get(name)


@dataclasses.dataclass(frozen=True)
class Context:
    """Where the walk is: the file, function, its result's enum, and the
    switch it is in, by its condition's enum and target."""
    file: str
    function: str = ''
    returns: str | None = None
    switch_enum: str | None = None
    switch_target: str = ''


@dataclasses.dataclass(frozen=True)
class Use:
    """A literal given to a target: where, and as written."""
    context: Context
    literal: dict
    text: str


class Checker:
    """Walks an image's hand-written C for magic numbers."""

    def __init__(self, image):
        self.image = image
        self.findings = []
        self.uses = collections.defaultdict(list)       # (target, value) -> [Use]
        self.names = collections.defaultdict(set)       # (target, value) -> {constant}
        self.constants = {}                             # declaration id -> value
        self.definitions = collections.defaultdict(set)  # (name, value) -> {(file, function)}
        self.defined_at = {}                            # (name, value) -> first (Context, node)

    def run(self):
        for node in self.image.declarations:
            spelling = node.get('_spelling')
            file = self.image.hand_written(spelling and spelling.file)
            if file and node['kind'] in ('FunctionDecl', 'VarDecl'):
                self.walk(node, Context(file))
        self.unnamed()
        self.duplicates()
        self.repeated()
        return sorted(self.findings, key=lambda finding: (finding.file, finding.line, finding.kind))

    def describe(self, node, context):
        """What an expression names, as a target: a local or parameter as
        FUNCTION:NAME, the same only within its function."""
        node = strip(node)
        kind = node.get('kind')
        if kind == 'MemberExpr':
            record, field = self.image.fields.get(node.get('referencedMemberDecl'), ('', node['name']))
            return f'{record or record_of(type_of(node["inner"][0]))}.{field}'
        if kind == 'DeclRefExpr':
            declaration = node['referencedDecl']
            if declaration['id'] in self.image.globals:
                return declaration['name']
            return f'{context.function}:{declaration.get("name", "")}'
        if kind == 'ArraySubscriptExpr':
            return self.describe(node['inner'][0], context) + '[]'
        if kind == 'UnaryOperator' and node['opcode'] == '*':
            return '*' + self.describe(node['inner'][0], context)
        return ''

    def register(self, node):
        """The register field's name, if an expression is a field of a
        register header's structures."""
        node = strip(node)
        while node.get('kind') == 'ArraySubscriptExpr':
            node = strip(node['inner'][0])
        if node.get('kind') == 'MemberExpr' and node.get('referencedMemberDecl') in self.image.register_fields:
            return node['name']
        return None

    def literal(self, node):
        """(value, literal node, text) if a node is a literal written in
        hand-written C."""
        found = integer_literal(node)
        if not found:
            return None
        value, literal, negated = found
        spelling, expansion = literal['_spelling'], literal['_expansion']
        if self.image.hand_written(spelling.file) is None:
            return None
        if spelling != expansion and self.image.line_text(spelling).lstrip().startswith('#'):
            return None
        return value, literal, ('-' if negated else '') + self.image.text(spelling)

    def named(self, node):
        """(value, name) if a node is a named constant: a static const, or a
        macro's literal."""
        stripped = strip(node)
        if stripped.get('kind') == 'DeclRefExpr':
            declaration = stripped['referencedDecl']
            if declaration['id'] in self.constants:
                return self.constants[declaration['id']], declaration['name']
            return None
        found = integer_literal(node)
        if found and found[1]['_spelling'] != found[1]['_expansion']:
            expansion = found[1]['_expansion']
            if self.image.line_text(found[1]['_spelling']).lstrip().startswith('#'):
                return found[0], self.image.text(expansion)
        return None

    def add(self, kind, context, literal, text, target, message):
        self.findings.append(Finding(kind, context.file, literal['_expansion'].line, context.function, text, target,
                                     message))

    def give(self, context, node, enum, target, coded=True):
        """A literal given to a target, of an enum's type or coded; kept
        for unnamed and repeated otherwise."""
        found = self.literal(node)
        if not found:
            named = self.named(node)
            if named and target and named[1] not in NOT_NAMES:
                self.names[(target, named[0])].add(named[1])
            return
        value, literal, text = found
        kind = 'enum'
        if not enum and coded and target in self.image.coded:
            enum, kind = self.image.coded[target], 'coded'
        if enum:
            names = self.image.enumerators(enum, value)
            if names:
                self.add(kind, context, literal, text, target, f'{enum}: {names}')
            elif not (value == 0 and self.image.bit_enum(enum)):
                self.add('missing', context, literal, text, target, f'{enum} has no enumerator of {value}')
            return
        if target:
            self.uses[(target, value)].append(Use(context, literal, text))

    def bits(self, context, node, other, operator):
        """A hex literal masking or set into a register, or masking an
        enum, where a header names a bit of its value."""
        found = self.literal(node)
        if not found or not found[2].lower().startswith('0x'):
            return False
        value, literal, text = found
        register = self.register(other)
        if register:
            names = [name for name in self.image.bits.get(value, ())
                     if register.startswith(re.sub(r'_.*', '', name).lower())]
            if names:
                self.add('bits', context, literal, text, self.describe(other, context), ' or '.join(names))
                return True
        enum = enum_of(other)
        names = enum and operator in MASK_OPERATORS and self.image.enumerators(enum, value)
        if names:
            self.add('bits', context, literal, text, self.describe(other, context), f'{enum}: {names}')
            return True
        return False

    def walk(self, node, context):
        kind = node.get('kind')
        inner = node.get('inner', ())
        if kind == 'FunctionDecl':
            if not any(child.get('kind') == 'CompoundStmt' for child in inner):
                return
            match = FUNCTION_RETURNS_ENUM.match(type_of(node))
            context = dataclasses.replace(context, function=node['name'], returns=match and match.group(1))
        elif kind == 'VarDecl':
            if self.variable(node, context):
                return
        elif kind == 'SwitchStmt':
            condition = inner[-2] if len(inner) > 1 else inner[0]
            context = dataclasses.replace(context, switch_enum=enum_of(condition),
                                          switch_target=self.describe(condition, context))
        elif kind == 'CaseStmt':
            self.give(context, inner[0], context.switch_enum, context.switch_target)
        elif kind in ('BinaryOperator', 'CompoundAssignOperator'):
            self.operator(node, context)
        elif kind == 'ReturnStmt' and inner and context.returns:
            self.give(context, inner[0], context.returns, f'return of {context.function}')
        elif kind == 'CallExpr':
            self.call(node, context)
        elif kind == 'InitListExpr':
            self.initializers(node, context)
        for child in inner:
            self.walk(child, context)

    def variable(self, node, context):
        """A variable's initializer; true for a constant's, which names its
        value rather than using it."""
        initializer = node.get('init') and node.get('inner', [None])[-1]
        if not initializer:
            return False
        constant = 'const' in node['type']['qualType'].split()
        found = self.literal(initializer)
        if constant and found and not enum_of(node):
            self.constants[node['id']] = found[0]
        if constant and SCALAR_TYPE.match(type_of(node)) and self.constant_expression(initializer):
            text = self.image.extent_text(initializer)
            if text:
                key = (node['name'], found[0] if found else text)
                self.definitions[key].add((context.file, context.function))
                self.defined_at.setdefault(key, (context, initializer, text))
            if found and not enum_of(node):
                return True
        target = node['name'] if node['id'] in self.image.globals else f'{context.function}:{node["name"]}'
        self.give(context, initializer, enum_of(node), target)
        return False

    def constant_expression(self, node):
        """Whether an expression is made of literals, enumerators, constants
        and sizeof alone."""
        for child in walk_all(node, CONSTANT_NODES | {'DeclRefExpr'}):
            if child['kind'] == 'DeclRefExpr':
                declaration = child['referencedDecl']
                if declaration.get('kind') != 'EnumConstantDecl' and declaration['id'] not in self.constants:
                    return False
        return all(child.get('kind') in CONSTANT_NODES | {'DeclRefExpr'} for child in self.descendants(node))

    @staticmethod
    def descendants(node):
        stack = [node]
        while stack:
            node = stack.pop()
            yield node
            stack.extend(child for child in node.get('inner', ()) if child)

    def operator(self, node, context):
        operator = node['opcode']
        left, right = node['inner']
        for value, other in ((right, left), (left, right)):
            if self.bits(context, value, other, operator):
                continue
            if operator in GIVING_OPERATORS:
                self.give(context, value, enum_of(other), self.describe(other, context))

    def call(self, node, context):
        callee = strip(node['inner'][0])
        if callee.get('kind') != 'DeclRefExpr':
            return
        declaration = callee['referencedDecl']
        parameters = self.image.functions.get(declaration['id'], [])
        for index, argument in enumerate(node['inner'][1:]):
            enum = ENUM_TYPE.match(parameters[index]) if index < len(parameters) else None
            self.give(context, argument, enum and enum.group(1), f'{declaration.get("name")}#{index + 1}')

    def initializers(self, node, context):
        """A record's or array's initializers, each given to its field or
        to the array's elements."""
        node_type = type_of(node)
        if ARRAY_TYPE.match(node_type):
            # An element's type is its conversion's, the array's type
            # perhaps a typedef's that is not desugared.
            for child in node.get('inner', ()):
                enum = ENUM_TYPE.match(type_of(child))
                if enum:
                    self.give(context, child, enum.group(1), f'{enum.group(1)}[]')
            return
        record = record_of(node_type)
        for (name, field_type), child in zip(self.image.records.get(record, ()), node.get('inner', ())):
            enum = ENUM_TYPE.match(field_type)
            target = f'{record}.{name}'
            if enum or target in self.image.coded:
                self.give(context, child, enum and enum.group(1), target)

    def unnamed(self):
        for (target, value), uses in self.uses.items():
            names = self.names.get((target, value))
            if names:
                kind = 'argument' if PARAMETER.search(target) else 'unnamed'
                for use in uses:
                    self.add(kind, use.context, use.literal, use.text, target,
                             f'named {" or ".join(sorted(names))} elsewhere')

    def duplicates(self):
        shared = collections.defaultdict(list)  # value -> [(name, file)] at file scope
        for (name, value), places in self.definitions.items():
            for file, function in places:
                if not function:
                    shared[value].append((name, file))
        for (name, value), places in self.definitions.items():
            context, initializer, text = self.defined_at[(name, value)]
            literal = initializer if '_expansion' in initializer else strip(initializer)
            if len(places) > 1 and not self.trivial_float(text):
                where = ', '.join(sorted(f'{file}:{function or "file scope"}' for file, function in places))
                self.add('duplicate', context, literal, text, name, f'{len(places)} definitions: {where}')
            named = [f'{other} ({file})' for other, file in shared.get(value, ()) if other.endswith('_' + name)]
            if named and any(function for _, function in places):
                self.add('similar', context, literal, text, name, f'named {" or ".join(named)} at file scope')

    @staticmethod
    def trivial_float(text):
        try:
            return float(text.rstrip('fFlL')) in TRIVIAL_FLOATS
        except ValueError:
            return False

    def repeated(self):
        for (target, value), uses in self.uses.items():
            places = {(use.context.file, use.context.function) for use in uses}
            if value in TRIVIAL or len(places) < 2 or (target, value) in self.names:
                continue
            where = ', '.join(sorted(f'{file}:{function}' for file, function in places))
            for use in uses:
                self.add('repeated', use.context, use.literal, use.text, target, f'{len(places)} functions: {where}')


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
            sys.exit(f'{path}:{number}: want kind, file, function, literal, target and a reason')
        allowed[tuple(cells[:5])] = cells[5]
    return allowed


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--image', default=DEFAULT_IMAGE)
    parser.add_argument('--check', action='store_true',
                        help='print and fail on the checked findings the allowlist lacks')
    parser.add_argument('--all', action='store_true', help='print the allowed findings too')
    arguments = parser.parse_args()
    image = Image(arguments.image)
    findings = Checker(image).run()
    allowlist = image.folder / ALLOWLIST
    allowed = read_allowlist(allowlist)
    counts = collections.Counter()
    allowed_counts = collections.Counter()
    matched = set()
    for finding in findings:
        key = finding.key()
        if key in allowed:
            matched.add(key)
            allowed_counts[finding.kind] += 1
            if arguments.all:
                print(f'{finding}\t(allowed: {allowed[key]})')
            continue
        counts[finding.kind] += 1
        if not arguments.check or finding.kind in CHECKED_KINDS:
            print(finding)
    stale = [key for key in allowed if key not in matched]
    for key in stale:
        print(f'{allowlist.relative_to(ROOT)}: no finding for {"\t".join(key)}', file=sys.stderr)
    print('findings: ' + ', '.join(f'{kind} {counts[kind]}' for kind in KINDS)
          + '; allowed: ' + ', '.join(f'{kind} {allowed_counts[kind]}' for kind in KINDS), file=sys.stderr)
    if arguments.check and (stale or any(counts[kind] for kind in CHECKED_KINDS)):
        sys.exit(1)


if __name__ == '__main__':
    main()
