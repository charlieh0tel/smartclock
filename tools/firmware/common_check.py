"""Checks that the C in third_party/firmware/rewritten/common/ is what docs/firmware/
rewrite.md's "Shared C" lets be shared, in every image whose export
includes it.

    python3 -I tools/firmware/common_check.py

1. Same code: each function common/'s files define, or bind under its
   name (bindings/), is classed identical or relocated in each sharing
   image's diff against BASE (docs/firmware/diff/IMAGE-vs-BASE.tsv); a
   static helper, which no image names, is checked by its callers.
   And the same callees, which rule 1's masking cannot see: the
   routines each sharing image's code of a function common/ defines
   refers to -- calls, jumps, a fall through, an address taken, a tail
   of another function branched into, through a thunk -- are those its
   C names, as clang parses it -- calls, a table's routines, bindings'
   and asm statements' routines, through the static functions it
   reaches -- each way, but where CALLEE_EXCEPTIONS says why not.
2. Same data: everything common/'s code names that it does not define
   -- a function's or global's declared type, a type, an enumerator, a
   constant, a macro -- is declared the same in each sharing image's
   export, as clang parses it, and a static function it calls, whose
   body is compiled into its caller, is common/'s too, as is anything
   else of it a hand-written file defines; and each type the export defines from the
   image's smartclock.h is defined in a common/types/ header that each
   image's smartclock.h includes, or in include/, which each image's
   Setup takes as types= too, with the same sizes and offsets in
   each image's check-layout.c.  An enum may be split: its shared head
   of members in a common/types/ header that each image's definition
   includes among its members, the rest the image's own; then it is the
   same to common/ code when its storage is (packed, signed, size) and
   each member that code names has the same value and is in the head;
   an enum it names no member of, a field's type in a shared record,
   need only have the same storage.  And each field common/'s code
   reaches through a global, global.field, whose offset is then part of
   an absolute address in the image's code, which rule 1's masking
   cannot see, is asserted in each image's check-layout.c, where
   tools/firmware/layout_addresses.py checks that a seeded image's
   addresses are its own; a field of a register block, include/'s, is
   the board's.
3. No address: no comment in common/ cites a ROM, RAM or installer
   address (comment_addresses.py's ranges), nor does its code cast such
   a literal to a pointer or write an installer address; a device's
   register, the board's, is no image's address.

And the figures the documents quote of what common/ holds (FIGURES):
its functions an export names and its static helpers, its files, and
each image's own functions an export names; with --write, rewrites them.

    python3 -I tools/firmware/common_check.py [--write]

Prints each failure; exits nonzero if any.  Needs clang and m68k-linux-gnu
binutils.
"""

import argparse
import bisect
import collections
import dataclasses
import functools
import importlib.util
import json
import os
import pathlib
import re
import shlex
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
FIRMWARE = ROOT / 'third_party/firmware'
DECOMPILED = FIRMWARE / 'decompiled'
REWRITTEN = FIRMWARE / 'rewritten'
COMMON = REWRITTEN / 'common'
INCLUDE_FOLDER = FIRMWARE / 'include'
DIFFS = ROOT / 'docs/firmware/diff'
IMAGES = FIRMWARE / 'images'
ANALYSIS = FIRMWARE / 'analysis'


def load(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / 'tools/firmware' / (name + '.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


magic_check = load('magic_check')
comment_addresses = load('comment_addresses')
image_diff = load('image_diff')

# The folder of common/ holding the types the images' smartclock.h
# include, and of each image's rewritten C holding its smartclock.h and
# check-layout.c, which the exporter skips (SmartClockExportC.java,
# TYPES_FOLDER).
TYPES = 'types'
# The image the others' diffs are against, whose C common/ came from.
BASE = 'z3805a-3543b'
# The statuses rule 1 shares.
SHARED = {'identical', 'relocated'}
# A definition in hand-written C, as the exporter finds it
# (SmartClockExportC.java, DEFINITION, WRAPPED_DEFINITION), and a
# binding's (BINDING_DEFINITION).
DEFINITION = re.compile(r'(?m)^(?:[A-Za-z_][\w \*]*?\b)?(\w+)\([^;{}]*\)\s*\{')
WRAPPED_DEFINITION = re.compile(r'(?m)^CPU_[A-Z0-9_]+\(\s*(\w+)\s*,')
BINDING_DEFINITION = re.compile(r'static inline\s[^;{}()]*?\b(\w+)\s*\(')
STATIC = re.compile(r'(?m)^(?:__attribute__\(\([^)]*\)\)\s*)?static\b[^;{]*?\b(\w+)\s*\(')
NOT_NAMES = {'if', 'while', 'for', 'switch', 'return', 'sizeof', '__attribute__'}
INCLUDE = re.compile(r'(?m)^#include "([^"]+)"$')
# A literal cast to a pointer, an address in code, ending where the
# literal starts.
POINTER_CAST = re.compile(r'\*\s*\)\s*$')
COMMENT = re.compile(r'/\*.*?\*/|//[^\n]*', re.S)
STRING = re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'')
IDENTIFIER = re.compile(r'\b[A-Za-z_]\w*\b')
MACRO = re.compile(r'(?m)^#define\s+(\w+)((?:\([^)]*\))?)((?:[^\n]*\\\n)*[^\n]*)$')
TYPE_WORD = re.compile(r'\b(?:struct|union|enum)\s+(\w+)|\b([A-Za-z_]\w*)\b')
TYPE_KEYWORDS = set('const volatile unsigned signed char short int long float double void struct union enum '
                    '_Bool restrict __attribute__ aligned packed'.split())
# The integer types the export declares itself, Ghidra's and <stdint.h>'s.
BUILTIN_TYPE = re.compile(r'^(u?int\d+_t|u?(byte|word|short|int|long|longlong)|sbyte|bool|uchar|char|undefined\d*|'
                          r'float\d*|double|ulonglong|dword|qword|wchar_t|size_t)$')
CHECK = re.compile(r'CHECK_(SIZE|FIELD)\(\s*(\w+)\s*,\s*(?:(\w+(?:\.\w+|\[\d+\])*)\s*,\s*)?([^;]*)\);', re.S)
ARITHMETIC = re.compile(r'^[\s0-9a-fA-FxX+\-*/()]+$')
RECORD_QUALIFIERS = re.compile(r'^(?:(?:const|volatile|struct|union)\s+)+')
# The register blocks include/'s headers define, the board's.
BOARD_TYPES = {match.group(1) for path in INCLUDE_FOLDER.glob('*.h')
               for match in re.finditer(r'(?m)^\}\s*(\w+)\s*;', path.read_text())}


def sharing_images():
    """The images whose export includes a common/ file, by name."""
    images = []
    for export in sorted(DECOMPILED.glob('*/*.c')):
        text = export.read_text()
        if any((export.parent / path).resolve().is_relative_to(COMMON) for path in INCLUDE.findall(text)):
            images.append(export.parent.name)
    return images


def common_files():
    return sorted(COMMON.rglob('*.[ch]')) if COMMON.is_dir() else []


def defined(text):
    names = [m.group(1) for pattern in (DEFINITION, WRAPPED_DEFINITION) for m in pattern.finditer(text)]
    return [name for name in names if name not in NOT_NAMES]


def statuses(image):
    """Each base function's statuses in the image's diff against BASE."""
    found = collections.defaultdict(set)
    for line in open(DIFFS / f'{image}-vs-{BASE}.tsv'):
        fields = line.rstrip('\n').split('\t')
        if line.startswith('#') or fields[0] == 'region' or fields[1] != 'function':
            continue
        found[fields[5]].add(fields[2])
    return found


def same_code(images, failures):
    """Rule 1."""
    others = [image for image in images if image != BASE]
    diffs = {image: statuses(image) for image in others}
    for path in common_files():
        text = COMMENT.sub(' ', path.read_text())
        statics = set(STATIC.findall(text))
        names = set(defined(text)) | set(BINDING_DEFINITION.findall(text))
        for name in sorted(names):
            for image in others:
                found = diffs[image].get(name)
                if found is None and name in statics:
                    continue
                if found is None or not found <= SHARED:
                    failures.append(f'{path.relative_to(ROOT)}: {name} is {"/".join(sorted(found or {"unnamed"}))} '
                                    f'in {image}')

# A string an asm statement calls by: one holding "jsr NAME", or the
# routine's name alone, which a binding's macro joins to its "jsr ".
ASM_CALL = re.compile(r'\b(?:jsr|bsr|jmp)\s+([A-Za-z_]\w*)')
ASM_ROUTINE = re.compile(r'^[A-Za-z_]\w*$')
# The instructions that return.
RETURNS = {'rts', 'rte', 'rtr', 'rtd'}


def asm_callees(export, node):
    """The routines a function's asm statements call, in its text and the
    macros it uses."""
    found, texts, words = set(), [export.text_of(node) or ''], set()
    while texts:
        text = texts.pop()
        for literal in STRING.findall(text):
            body = literal[1:-1].strip()
            found.update(ASM_CALL.findall(body))
            if ASM_ROUTINE.match(body):
                found.add(body)
        for word in set(IDENTIFIER.findall(STRING.sub(' ', text))) - words:
            words.add(word)
            texts.extend(body for _, body in export.macros.get(word, ()))
    return found


def c_callees(export, function):
    """The routines a common/ function's C names, and those it reaches
    under another name.  Named: each function it refers to, in its body
    or in the initializer of a variable it refers to (a table of
    routines), with the routines its asm statements call, and, through
    each static function it reaches, whose body gcc compiles into it (a
    binding's, bindings/), those that one names.  Reached: the routines
    whose C is a static function's body (Export.bodies) that it reaches
    other than as its own body; that function's names are those
    routines', not its."""
    own = export.forwarded(function)
    named, reached, seen, pending = set(), set(), set(), [function]
    while pending:
        node = pending.pop()
        if id(node) in seen:
            continue
        seen.add(id(node))
        named |= asm_callees(export, node)
        stack = list(node.get('inner', ()))
        while stack:
            current = stack.pop()
            stack.extend(current.get('inner', ()))
            if current.get('kind') != 'DeclRefExpr':
                continue
            reference = current['referencedDecl']
            if reference['kind'] == 'VarDecl' and reference.get('name') in export.initialized:
                pending.append(export.initialized[reference['name']])
            if reference['kind'] != 'FunctionDecl':
                continue
            name = reference['name']
            if name not in export.static_functions:
                named.add(name)
            elif name in export.bodies and name != own:
                reached |= export.bodies[name]
            else:
                pending.append(export.static_functions[name])
    return named, reached


class ImageCode:
    """An image's functions as its sidecar names them, and the routines
    each one's code refers to."""

    def __init__(self, image):
        conventions = DECOMPILED / image / f'{image}.conventions.jsonl'
        items, self.starts = image_diff.load_base(str(conventions), str(ANALYSIS / image / 'names.tsv'), None)
        self.functions = {(region, item.address): item.name for region, found in items.items() for item in found
                          if item.kind == 'function'}
        self.by_name = {name: place for place, name in self.functions.items()}
        self.memories = image_diff.region_memories((IMAGES / f'{image}.bin').read_bytes())
        self.ordered = {region: sorted(starts) for region, starts in self.starts.items()}

    def stop(self, region, address):
        """Where the function or label after an address starts."""
        order = self.ordered[region]
        following = order[bisect.bisect_right(order, address):]
        return following[0] if following else address + image_diff.BODY_LIMIT

    def thunk(self, region, address):
        """The function a thunk, a function that is a branch, goes to."""
        first = self.memories[region].instructions(address, address + image_diff.BODY_LIMIT)[:1]
        if first and (first[0].mnemonic == 'jmp' or image_diff.BRANCH.match(first[0].mnemonic)) and first[
                0].mnemonic not in image_diff.CALLS:
            targets = [field.target for field in image_diff.fields(first[0])]
            if len(targets) == 1 and (region, targets[0]) in self.functions:
                return targets[0]
        return None

    def returns(self, region, address):
        """Whether a function is a return alone, as sci_echo_none's rts,
        which the code before it falls through to as its own."""
        first = self.memories[region].instructions(address, address + image_diff.BODY_LIMIT)[:1]
        return bool(first) and first[0].mnemonic in RETURNS

    def callees(self, name):
        """The functions a function's code refers to: those it calls, jumps
        to, falls through to or takes the address of, and those of any
        other function's code it branches into, a tail shared; each as a
        chain, the function and, if it is a thunk, what it goes to, and so
        on."""
        region, start = self.by_name[name]
        found, extents, pending = set(), set(), [start]
        while pending:
            at = pending.pop()
            stop = self.stop(region, at)
            if (at, stop) in extents:
                continue
            extents.add((at, stop))
            instructions = image_diff.reachable(self.memories[region], at, stop)
            for instruction in instructions:
                jumps = image_diff.BRANCH.match(instruction.mnemonic) and instruction.mnemonic not in image_diff.CALLS
                for field in image_diff.fields(instruction):
                    if not field.relative and field.size < 4:
                        continue  # an absolute short: a constant, as pea 0x400's signal
                    if (region, field.target) in self.functions:
                        found.add(field.target)
                    elif jumps and field.relative and not at <= field.target < stop:
                        pending.append(field.target)
                if instruction.end == stop and instruction.mnemonic not in image_diff.TERMINALS and (
                        region, stop) in self.functions and not self.returns(region, stop):
                    found.add(stop)
        chains = []
        for address in sorted(found - {start}):
            chain = []
            while address is not None and self.functions[region, address] not in chain:
                chain.append(self.functions[region, address])
                address = self.thunk(region, address)
            chains.append(tuple(chain))
        return chains


# The kinds of CALLEE_EXCEPTIONS: what the image's code and the C do
# differently there, which same_callees then allows.
#
# REPEATS: the function's code repeats the routine's in place where its
#   C calls it: its C may name the routine its code does not call, and
#   its code call what the routine's calls.
# DOES: the function's code calls the routine for what its C does itself,
#   by an operator or a helper of its own.
# ADDRESS: the function's code takes the routine's address as data.
# SOFT_FLOAT: the function's code calls the soft-float routines, those
#   bindings/softfloat.h binds, by other entries than its C's bindings do:
#   any of them, either way.
# FINDING: in the image, the function's code (any common/ function's,
#   where none is named) calls the routine, which its C does not name,
#   and, where another is given, not the other, which its C names: a
#   finding not yet fixed (docs/firmware/z3815a-4010.md, "The export and
#   the checks").
REPEATS, DOES, ADDRESS, SOFT_FLOAT, FINDING = 'repeats', 'does', 'address', 'soft-float', 'finding'


@dataclasses.dataclass(frozen=True)
class CalleeException:
    """A mismatch same_callees allows: its kind (above), the common/
    function (None for any, FINDING's), the routine, the other routine
    (FINDING's, or None), the images it holds in (None for every sharing
    image), and why.  It must match in each of them, or it fails as
    stale."""
    kind: str
    function: str | None
    routine: str | None
    reason: str
    other: str | None = None
    images: tuple | None = None


SOFT_FLOAT_BINDINGS = COMMON / 'bindings/softfloat.h'
Z3815A = ('z3815a-4010',)
CALLEE_EXCEPTIONS = [
    # The C calls a routine whose code the image repeats in place.
    *(CalleeException(REPEATS, function, 'hms_to_seconds', 'the image repeats hms_to_seconds\' code, '
                      'which the C calls, the copies being the same')
      for function in ('time_to_seconds', 'time_packed_to_seconds', 'gps_seconds')),
    CalleeException(REPEATS, 'labs', 'abs', 'labs repeats abs\'s code, which the C calls'),
    CalleeException(REPEATS, 'ldiv', 'div', 'ldiv repeats div\'s code, which the C calls'),
    CalleeException(REPEATS, 'scpi_query_SOURce_GPSystem_POSition_ACTual', 'labs',
                    'the image takes each coordinate\'s absolute value in place, the C by labs'),
    *(CalleeException(REPEATS, function, routine, f'{function} repeats {routine}\'s code, the status bit\'s '
                      'switch, which the C calls')
      for function, routine in (('status_apply_condition', 'status_set_bit'),
                                ('installer_status_apply_condition', 'installer_status_set_bit'))),
    *(CalleeException(REPEATS, function, routine, f'{function} traps to the kernel in line, as {routine} does, '
                      'which the C calls through os.h\'s binding')
      for function, routine in (('psos_version', 'psos_call'), ('installer_psos_version', 'installer_psos_call'))),
    *(CalleeException(REPEATS, function, routine, f'{function} calls psos_call itself, as the stub {routine} '
                      'does, which the C calls')
      for function, routine in (('pps_init', 'psos_create_x'), ('pps_interrupt_stop', 'psos_delete_x'),
                                ('pps_wait_time', 'psos_request_x'))),
    *(CalleeException(REPEATS, function, 'memcpy', 'the image moves a value\'s bytes in place, which the C '
                      'copies with memcpy, as C must to read a float\'s bits')
      for function in ('float_decimal_exponent_floor', 'float_decimal_exponent_rounded', 'lifetime_record_read')),
    # The image calls a routine for what the C does itself.
    CalleeException(DOES, 'format_duration_short', 'ldiv', 'the image divides by ldiv, the C by / and %'),
    CalleeException(DOES, 'scpi_query_SOURce_ROSCillator_HOLDover_RECovery_AUTO', 'return_byte',
                    'the image sign-extends the byte by return_byte, the C by a cast'),
    *(CalleeException(DOES, 'counter_offset_apply', routine, f'the image adds the 64-bit offset by {routine}, '
                      'the C by its 64-bit operators')
      for routine in ('int64_add', 'int64_negate')),
    CalleeException(DOES, 'frexp', 'double_normalize', 'the C normalizes a denormal by libc_normalize, the image '
                    'by double_normalize'),
    # The image takes a routine's address as data.
    CalleeException(ADDRESS, 'random_dither', 'start_nothing', 'a value of 0 reads the byte after dither_table, '
                    'start_nothing\'s first, as the C\'s copy of the table ends'),
    # The soft-float routines by other entries.
    *(CalleeException(SOFT_FLOAT, function, None, 'the image passes the operands by pointer or in place, '
                      'the C\'s bindings in register pairs')
      for function in ('log', 'pow', 'sqrt')),
    # The Z3815A 4010's findings: its copy of the function calls another
    # routine than the others' (rule 6 splits it), or its names place the
    # routine the C calls at the wrong address.
]


def soft_float_routines():
    """The soft-float routines bindings/softfloat.h binds."""
    return set(re.findall(r'"((?:double|float)_\w+)"', SOFT_FLOAT_BINDINGS.read_text()))


def allow(exception, image, code, name, extra, missing):
    """Takes what an exception allows out of a function's mismatches in an
    image: extra, the chains its code calls and its C does not name, and
    missing, what its C names and its code does not call.  Whether it
    matched."""
    def calls(routine):
        return [chain for chain in extra if routine in chain]

    if exception.kind == REPEATS and exception.routine in missing:
        missing.discard(exception.routine)
        repeated = {callee for chain in code.callees(exception.routine) for callee in chain}
        extra[:] = [chain for chain in extra if not repeated.intersection(chain)]
        return True
    if exception.kind in (DOES, ADDRESS) and calls(exception.routine):
        extra[:] = [chain for chain in extra if exception.routine not in chain]
        return True
    if exception.kind == SOFT_FLOAT:
        routines = soft_float_routines()
        before = len(extra) + len(missing)
        extra[:] = [chain for chain in extra if not routines.intersection(chain)]
        missing -= routines
        return len(extra) + len(missing) < before
    if exception.kind == FINDING and calls(exception.routine) and (
            exception.other is None or exception.other in missing):
        extra[:] = [chain for chain in extra if exception.routine not in chain]
        missing.discard(exception.other)
        return True
    return False


def same_callees(images, exports, failures):
    """Rule 1's other half: rule 1 masks a callee's address as it does
    any other, so that a function of the same instructions calling
    another routine passes it.  For each function common/ defines, the
    routines each image's code refers to are those its C names, but
    CALLEE_EXCEPTIONS'."""
    codes = {image: ImageCode(image) for image in images}
    matched = set()
    for export in exports:
        code = codes[export.image]
        exceptions = [exception for exception in CALLEE_EXCEPTIONS
                      if exception.images is None or export.image in exception.images]
        for path in common_files():
            place = 'common:' + str(path.relative_to(COMMON))
            for node in export.declared(place):
                name = node.get('name')
                if node.get('kind') != 'FunctionDecl' or name not in code.by_name or node.get(
                        'storageClass') == 'static' or not any(
                        child.get('kind') == 'CompoundStmt' for child in node.get('inner', ())):
                    continue
                named, reached = c_callees(export, node)
                named = {callee for callee in named if callee in code.by_name} - {name}
                chains = code.callees(name)
                extra = [chain for chain in chains if not (named | reached).intersection(chain)]
                missing = named - {callee for chain in chains for callee in chain}
                for exception in exceptions:
                    if exception.function in (None, name) and allow(exception, export.image, code, name, extra,
                                                                     missing):
                        matched.add((exception, export.image))
                for chain in extra:
                    failures.append(f'{path.relative_to(ROOT)}: {name} in {export.image} calls {chain[0]}, '
                                    'which its C does not name')
                for callee in sorted(missing):
                    failures.append(f'{path.relative_to(ROOT)}: {name}\'s C names {callee}, which '
                                    f'{export.image}\'s code does not call')
    for exception in CALLEE_EXCEPTIONS:
        for image in exception.images or images:
            if (exception, image) not in matched:
                failures.append(f'common_check.py: CALLEE_EXCEPTIONS\' {exception.kind} '
                                f'{exception.function or "any"} {exception.routine or ""} matches nothing in {image}')


def no_address(failures):
    """Rule 3."""
    for path in common_files():
        text = path.read_text()
        comments = [(m.start(), m.end()) for m in COMMENT.finditer(text)]
        for match in comment_addresses.ADDRESS.finditer(text):
            region = comment_addresses.OVERLAY if match.group(1) else ''
            address = int(match.group(2), 16)
            in_comment = any(start <= match.start() < end for start, end in comments)
            if in_comment and comment_addresses.checked(region, address) or not in_comment and (
                    region or POINTER_CAST.search(text, 0, match.start())
                    and comment_addresses.checked(region, address)):
                line = text.count('\n', 0, match.start()) + 1
                failures.append(f'{path.relative_to(ROOT)}:{line}: cites the address {match.group()}')


class Export:
    """An image's export as clang parses it, with what common/'s code
    names in it."""

    def __init__(self, image):
        self.image = image
        self.rewritten = REWRITTEN / image
        self.decompiled = DECOMPILED / image
        export = self.decompiled / f'{image}.c'
        flags = [flag for flag in shlex.split(os.environ.get('DECOMPILED_CFLAGS', magic_check.DEFAULT_CFLAGS))
                 if magic_check.PARSING_FLAG.match(flag)]
        command = [magic_check.CLANG, magic_check.CLANG_TARGET, *flags, *magic_check.CLANG_LENIENCY,
                   '-fsyntax-only', '-Xclang', '-ast-dump=json', export.name]
        result = subprocess.run(command, cwd=export.parent, capture_output=True, check=False)
        if result.returncode:
            sys.exit(f'{shlex.join(command)} failed:\n{result.stderr.decode()}')
        tree = json.loads(result.stdout)
        magic_check.annotate(tree)
        self.top = tree['inner']
        self.by_id, self.records, self.typedefs, self.enums, self.enum_of = {}, {}, {}, {}, {}
        for node in self.top:
            self.index(node)
        self.static_functions = {node['name']: node for node in self.top
                                 if node.get('kind') == 'FunctionDecl' and node.get('storageClass') == 'static'
                                 and any(child.get('kind') == 'CompoundStmt' for child in node.get('inner', ()))}
        # common/'s variables with an initializer, by name, and the
        # routines whose C is one static function's call: the body
        # several copies share, as scpi_engine.h's and status.h's
        self.initialized = {node['name']: node for node in self.top if node.get('kind') == 'VarDecl'
                            and node.get('init') and self.place_of(node).startswith('common:')}
        self.bodies = collections.defaultdict(set)
        for node in self.top:
            body = self.forwarded(node)
            if body:
                self.bodies[body].add(node['name'])
        self.macros = collections.defaultdict(list)
        for path in [*(path for path in self.rewritten.rglob('*.[ch]')
                       if TYPES not in path.relative_to(self.rewritten).parts),
                     *INCLUDE_FOLDER.glob('*.h'),
                     *common_files()]:
            for match in MACRO.finditer(COMMENT.sub(' ', path.read_text())):
                body = ' '.join((match.group(2) + match.group(3)).split())
                self.macros[match.group(1)].append((self.place(str(path)), body))

    def forwarded(self, node):
        """The static function a common/ function's whole body calls,
        returning what it returns, or None."""
        if node.get('kind') != 'FunctionDecl' or node.get('storageClass') == 'static' or not self.place_of(
                node).startswith('common:'):
            return None
        bodies = [child for child in node.get('inner', ()) if child.get('kind') == 'CompoundStmt']
        statements = bodies[0].get('inner', []) if bodies else []
        if len(statements) != 1:
            return None
        call = magic_check.strip(statements[0], ('ReturnStmt', 'ImplicitCastExpr', 'ParenExpr', 'CStyleCastExpr'))
        if call.get('kind') != 'CallExpr':
            return None
        callee = magic_check.strip(call['inner'][0], ('ImplicitCastExpr', 'ParenExpr'))
        name = callee.get('referencedDecl', {}).get('name')
        return name if callee.get('kind') == 'DeclRefExpr' and name in self.static_functions else None

    def index(self, node):
        stack = [node]
        while stack:
            current = stack.pop()
            if 'id' in current:
                self.by_id[current['id']] = current
            kind = current.get('kind')
            if kind == 'RecordDecl' and current.get('name') and current.get('completeDefinition'):
                self.records[current['name']] = current
            elif kind == 'TypedefDecl':
                self.typedefs[current['name']] = current
            elif kind == 'EnumDecl':
                if current.get('name'):
                    self.enums[current['name']] = current
                for child in current.get('inner', ()):
                    if child.get('kind') == 'EnumConstantDecl':
                        self.enum_of[child['name']] = current
            if kind in ('RecordDecl', 'EnumDecl') or current is node:
                stack.extend(current.get('inner', ()))

    @functools.cache
    def place(self, file):
        """Where a declaration is: common:PATH, rewritten:PATH, export,
        include:NAME or system."""
        if file is None:
            return 'none'
        path = pathlib.Path(file) if os.path.isabs(file) else (self.decompiled / file).resolve()
        for prefix, folder in (('common:', COMMON), ('rewritten:', self.rewritten)):
            if path.is_relative_to(folder):
                return prefix + str(path.relative_to(folder))
        if path.is_relative_to(self.decompiled):
            return 'export'
        if path.is_relative_to(INCLUDE_FOLDER):
            return 'include:' + path.name
        return 'system'

    @functools.cache
    def declared(self, place):
        """The top-level declarations a file holds, by its place."""
        return [node for node in self.top if self.place_of(node) == place]

    def place_of(self, node):
        location = node.get('_spelling')
        return self.place(location.file if location else None)

    def text_of(self, node):
        begin, end = node.get('_extent', (None, None))
        if not begin or not end or begin.file != end.file:
            return None
        data = (self.decompiled / begin.file).resolve().read_bytes()
        return ' '.join(COMMENT.sub(' ', data[begin.offset:end.offset + end.length].decode()).split())

    @staticmethod
    def enum_values(node):
        """An enum's enumerators and their values, an implicit one its
        predecessor's plus one."""
        values, last = {}, -1
        for child in node.get('inner', ()):
            if child.get('kind') != 'EnumConstantDecl':
                continue
            stack, value = [child], None
            while stack and value is None:
                current = stack.pop()
                value = current.get('value') if current.get('kind') == 'ConstantExpr' else None
                stack.extend(current.get('inner', ()))
            last = int(value) if value is not None else last + 1
            values[child['name']] = last
        return values

    def enum_signature(self, node):
        """An enum type as common/ code depends on it: its storage, as gcc
        sizes it, not its members, for an enum split between common/types/
        and the image (rewrite.md, "Shared C"); the members it names are
        compared one by one."""
        values = self.enum_values(node).values() or [0]
        packed = any(child.get('kind') == 'PackedAttr' for child in node.get('inner', ()))
        low, high = min(values), max(values)
        size = next((size for size in (1, 2, 4) if packed and (
            -(1 << (8 * size - 1)) <= low and high < (1 << (8 * size - 1)) if low < 0
            else high < (1 << (8 * size)))), 4)
        return ('enum', node.get('name'), packed, low < 0, size, self.place_of(node))

    def record_fields(self, node):
        fields = []
        for child in node.get('inner', ()):
            if child.get('kind') == 'FieldDecl':
                fields.append((child.get('name'), child['type']['qualType']))
            elif child.get('kind') == 'RecordDecl' and child.get('completeDefinition') and not child.get('name'):
                fields.append(('', self.record_fields(child)))
        return tuple(fields)

    @staticmethod
    def type_names(qualified):
        names = set()
        for match in TYPE_WORD.finditer(str(qualified)):
            name = match.group(1) or match.group(2)
            if name and name not in TYPE_KEYWORDS:
                names.add(name)
        return names

    def types(self, names):
        """Each type named and those it names, by name, with its
        signature."""
        seen, pending = {}, list(names)
        while pending:
            name = pending.pop()
            if name in seen:
                continue
            if name in self.records:
                node = self.records[name]
                fields = self.record_fields(node)
                seen[name] = ('record', node.get('tagUsed'), fields, self.place_of(node))
                pending.extend(self.type_names(field_types(fields)))
            elif name in self.typedefs:
                node = self.typedefs[name]
                tag = re.fullmatch(r'enum (\w+)', node['type']['qualType'])
                if tag and tag.group(1) in self.enums:
                    # an enum's typedef is the enum, split or not
                    seen[name] = self.enum_signature(self.enums[tag.group(1)])
                    continue
                seen[name] = ('typedef', node['type']['qualType'], self.place_of(node))
                pending.extend(self.type_names(node['type']['qualType']))
            elif name in self.enums:
                seen[name] = self.enum_signature(self.enums[name])
            else:
                seen[name] = ('none',)
        return seen

    def named(self, path):
        """Everything a common/ file's code names that it does not
        define, by kind and name, with its signature here."""
        place = 'common:' + str(path.relative_to(COMMON))
        declarations = self.declared(place)
        own = {node.get('name') for node in declarations}
        found, type_names = {}, set()
        stack = list(declarations)
        while stack:
            node = stack.pop()
            stack.extend(node.get('inner', ()))
            if isinstance(node.get('type'), dict):
                type_names |= self.type_names(node['type'].get('qualType', ''))
            if node.get('kind') != 'DeclRefExpr':
                continue
            reference = node['referencedDecl']
            name, kind = reference.get('name'), reference['kind']
            declaration = self.by_id.get(reference['id'])
            if name in own or kind == 'ParmVarDecl' or declaration is None:
                continue  # its own, a parameter, or a local
            if kind == 'EnumConstantDecl':
                enum = self.enum_of[name]
                found['enumerator ' + name] = ('enumerator', enum.get('name'), self.enum_values(enum)[name],
                                               self.place_of(enum))
            elif kind == 'VarDecl':
                where = self.place_of(declaration)
                if where != place:
                    value = self.text_of(declaration) if where != 'export' else None
                    found['global ' + name] = (reference['type']['qualType'], where, value)
            elif kind == 'FunctionDecl':
                # a static function's body is compiled into its caller, so
                # where it is defined counts; another resolves by address
                static = self.static_functions.get(name)
                found['function ' + name] = (reference['type']['qualType'], static and self.place_of(static))
            type_names |= self.type_names(reference.get('type', {}).get('qualType', ''))
        for name, signature in self.types(type_names).items():
            found['type ' + name] = signature
        code = STRING.sub(' ', COMMENT.sub(' ', path.read_text()))
        for word in set(IDENTIFIER.findall(code)):
            if word in self.macros:
                found['macro ' + word] = tuple(sorted(self.macros[word]))
        return found

    def global_fields(self, path):
        """Each (record, field) a common/ file's code reaches through a
        global, by a chain of . and constant subscripts, whose offset an
        absolute address holds; a register block include/ defines, which
        the export declares again, is the board's, and left out."""
        place = 'common:' + str(path.relative_to(COMMON))
        found = set()
        stack = list(self.declared(place))
        while stack:
            node = stack.pop()
            stack.extend(node.get('inner', ()))
            if node.get('kind') != 'MemberExpr' or node.get('isArrow') or not self.from_global(node['inner'][0]):
                continue
            record = RECORD_QUALIFIERS.sub('', node['inner'][0]['type']['qualType']).strip()
            if record not in BOARD_TYPES:
                found.add((record, node['name']))
        return found

    def from_global(self, node):
        while node.get('kind') in ('ImplicitCastExpr', 'ParenExpr'):
            node = node['inner'][0]
        kind = node.get('kind')
        if kind == 'DeclRefExpr':
            reference = node['referencedDecl']
            return reference['kind'] == 'VarDecl' and reference['id'] in self.by_id
        if kind == 'MemberExpr' and not node.get('isArrow'):
            return self.from_global(node['inner'][0])
        if kind == 'ArraySubscriptExpr':
            index = node['inner'][1]
            while index.get('kind') in ('ImplicitCastExpr', 'ParenExpr'):
                index = index['inner'][0]
            return index.get('kind') == 'IntegerLiteral' and self.from_global(node['inner'][0])
        return False

    def layout(self):
        """Each type's sizes and offsets that check-layout.c asserts, as
        values where they are arithmetic."""
        asserted = collections.defaultdict(set)
        text = COMMENT.sub(' ', (self.rewritten / TYPES / 'check-layout.c').read_text())
        for kind, type_name, field, expression in CHECK.findall(text):
            expression = ' '.join(expression.split())
            if ARITHMETIC.match(expression):
                expression = int(eval(expression.replace('/', '//')))  # only digits and operators
            asserted[type_name].add((field or '', expression))
        return asserted


def split_enum(image, name):
    """Whether the image's smartclock.h defines an enum with a common/types/
    header included in its members: its shared head (rewrite.md, "Shared
    C")."""
    text = (REWRITTEN / image / TYPES / 'smartclock.h').read_text()
    match = re.search(r'(?ms)^typedef enum\b[^{;]*\{((?:(?!^\}).)*)^\}\s*%s\s*;' % name, text)
    return bool(match) and any(path.startswith(f'../../common/{TYPES}/') for path in INCLUDE.findall(match.group(1)))


def field_types(fields):
    """A record's fields' types, nested records' among them."""
    return ' '.join(field_types(kind) if isinstance(kind, tuple) else kind for _, kind in fields)


def flatten(signature):
    """The strings in a signature, nested tuples and all."""
    if isinstance(signature, str):
        yield signature
    elif isinstance(signature, tuple):
        for item in signature:
            yield from flatten(item)


def same_data(images, exports, failures):
    """Rule 2."""
    layouts = [export.layout() for export in exports]
    types = sorted((COMMON / TYPES).glob('*.h'))
    # include/'s headers, the CPU's and the DUART's, every image's types= too
    headers = '\n'.join(path.read_text() for path in [*types, *INCLUDE_FOLDER.glob('*.h')])
    for image in images:
        header = REWRITTEN / image / TYPES / 'smartclock.h'
        included = {(header.parent / path).resolve() for path in INCLUDE.findall(header.read_text())}
        for path in types:
            if path.resolve() not in included:
                failures.append(f'{header.relative_to(ROOT)} does not include {path.relative_to(ROOT)}')
    for path in common_files():
        found = [export.named(path) for export in exports]
        relative = path.relative_to(ROOT)
        for record, field in sorted(set().union(*(export.global_fields(path) for export in exports))):
            for image, layout in zip(images, layouts):
                if not any(asserted == field for asserted, _ in layout.get(record, ())):
                    failures.append(f'{relative}: {record}.{field}, reached through a global, is not asserted in '
                                    f'rewritten/{image}/{TYPES}/check-layout.c')
        for key in sorted(set().union(*found)):
            signatures = [names.get(key) for names in found]
            if any(signature != signatures[0] for signature in signatures):
                failures.append(f'{relative}: {key} differs: '
                                + '; '.join(f'{image} {signature}' for image, signature in zip(images, signatures)))
                continue
            kind, name = key.split(' ', 1)
            signature = signatures[0]
            if kind == 'function' and signature[1] and not signature[1].startswith('common:'):
                failures.append(f'{relative}: calls {name}, a static function in {signature[1]}')
            per_image = [place for place in flatten(signature) if place.startswith('rewritten:')]
            if per_image and kind != 'function':
                failures.append(f'{relative}: {key} is per image, in {per_image[0]}')
            if kind == 'enumerator' and signature[-1] == 'export' and not re.search(
                    r'(?m)^\s*%s\s*=' % name, headers):
                failures.append(f'{relative}: enumerator {name} is not defined in a common/{TYPES}/ header')
            if kind != 'type' or signature[-1] != 'export' or BUILTIN_TYPE.match(name):
                continue
            named_members = any(key.startswith('enumerator ') and found[0][key][1] == name for key in found[0])
            if signature[0] == 'enum' and (not named_members or all(split_enum(image, name) for image in images)):
                pass  # its storage is the same, and it names none of its members, or the head's
            elif not re.search(r'}\s*%s\s*;|\b(?:struct|union|enum)\s+%s\s*\{|\btypedef\b[^;{}]*\b%s\s*[(;\[]'
                             % (name, name, name), headers):
                failures.append(f'{relative}: type {name} is not defined in a common/{TYPES}/ header')
            asserted = [layout.get(name, set()) for layout in layouts]
            if any(each != asserted[0] for each in asserted):
                failures.append(f'{relative}: type {name} is laid out differently by '
                                + ', '.join(f'rewritten/{image}/{TYPES}/check-layout.c' for image in images))


# The sentences that quote common/'s figures, each in its document once,
# {name} where a figure of figures() stands.
FIGURES = [
    (ROOT / 'docs/firmware/rewrite.md',
     '`common/` holds {functions} functions the exports name and {helpers}\nstatic helpers, in {files} files; the rest is each image\'s own:\n{z3805a-3543b} functions the Z3805A 3543B\'s export names, {58503a-3704} the\n58503A 3704\'s and {z3801a-3543} the Z3801A 3543\'s'),
    (ROOT / 'docs/firmware/README.md',
     '`common/` holds {functions} functions\nand {helpers} static helpers'),
    (ROOT / 'docs/firmware/58503a-3704.md',
     'its own C defines {58503a-3704} functions its export names, beside\n`common/`\'s {functions}'),
    (ROOT / 'docs/firmware/z3801a-3543.md',
     'its own C defines {z3801a-3543} functions its export names, beside\n`common/`\'s {functions}'),
    (FIRMWARE / 'README.md',
     '`common/`, the C several images share ({functions} functions and\n{helpers} static helpers'),
]
FIGURE = re.compile(r'\{([\w-]+)\}')


def defined_in(paths):
    """The functions the files define: those an export names, and the
    static ones."""
    named, helpers = set(), set()
    for path in paths:
        text = COMMENT.sub(' ', path.read_text())
        statics = set(STATIC.findall(text))
        for name in set(defined(text)):
            (helpers if name in statics else named).add(name)
    return named, helpers


def figures(images):
    named, helpers = defined_in(common_files())
    found = {'functions': len(named), 'helpers': len(helpers), 'files': len(common_files())}
    for image in images:
        own = [path for path in (REWRITTEN / image).rglob('*.[ch]')
               if TYPES not in path.relative_to(REWRITTEN / image).parts]
        found[image] = len(defined_in(own)[0])
    return found


def figure_pattern(parts):
    """A FIGURES sentence as a pattern, its figures groups and any run of
    white space in it any other, so that a reflowed line still matches."""
    pattern = ''
    for index, part in enumerate(parts):
        if index % 2:
            pattern += r'(\d+)'
            continue
        words = part.split()
        literal = r'\s+'.join(map(re.escape, words))
        pattern += (r'\s+' if part[:1].isspace() else '') + literal + (
            r'\s+' if part[-1:].isspace() and words else '')
    return pattern


def check_figures(images, write, failures):
    """Each FIGURES sentence against figures(), rewritten with write."""
    found = figures(images)
    for document, template in FIGURES:
        parts = FIGURE.split(template)
        names = parts[1::2]
        text = document.read_text()
        matches = list(re.finditer(figure_pattern(parts), text))
        if len(matches) != 1:
            failures.append(f'{document.relative_to(ROOT)}: {len(matches)} of the sentence {template!r}')
            continue
        match = matches[0]
        wrong = [(name, int(value)) for name, value in zip(names, match.groups()) if int(value) != found[name]]
        if not wrong:
            continue
        if write:
            for index in reversed(range(len(names))):
                start, end = match.span(index + 1)
                text = text[:start] + str(found[names[index]]) + text[end:]
            document.write_text(text)
        else:
            failures.append(f'{document.relative_to(ROOT)}: ' + ', '.join(
                f'{name} is {value}, not {found[name]}' for name, value in wrong))


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--write', action='store_true')
    arguments = parser.parse_args()
    failures = []
    images = sharing_images()
    if BASE not in images and images:
        failures.append(f'{BASE}, the diffs\' base, does not include common/')
    if common_files():
        same_code(images, failures)
        no_address(failures)
        if images:
            exports = [Export(image) for image in images]
            same_data(images, exports, failures)
            same_callees(images, exports, failures)
    check_figures(images, arguments.write, failures)
    for failure in failures:
        print(failure)
    print(f'common/: {len(common_files())} files, shared by {", ".join(images) or "no image"}')
    if failures:
        sys.exit(f'{len(failures)} failures')


if __name__ == '__main__':
    main()
