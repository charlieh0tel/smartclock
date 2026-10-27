"""Checks that the C in third_party/firmware/rewritten/common/ is what docs/firmware/
rewrite.md's "Shared C" lets be shared, in every image whose export
includes it.

    python3 -I tools/firmware/common_check.py

1. Same code: each function common/'s files define, or bind under its
   name (bindings/), is classed identical or relocated in each sharing
   image's diff against BASE (docs/firmware/diff/IMAGE-vs-BASE.tsv); a
   static helper, which no image names, is checked by its callers.
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
   need only have the same storage.
3. No address: no comment in common/ cites a ROM, RAM or installer
   address (comment_addresses.py's ranges), nor does its code cast such
   a literal to a pointer or write an installer address; a device's
   register, the board's, is no image's address.

Prints each failure; exits nonzero if any.  Needs clang.
"""

import collections
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


def load(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / 'tools/firmware' / (name + '.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


magic_check = load('magic_check')
comment_addresses = load('comment_addresses')

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
        self.macros = collections.defaultdict(list)
        for path in [*(path for path in self.rewritten.rglob('*.[ch]')
                       if TYPES not in path.relative_to(self.rewritten).parts),
                     *INCLUDE_FOLDER.glob('*.h'),
                     *common_files()]:
            for match in MACRO.finditer(COMMENT.sub(' ', path.read_text())):
                body = ' '.join((match.group(2) + match.group(3)).split())
                self.macros[match.group(1)].append((self.place(str(path)), body))

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
        declarations = [node for node in self.top if self.place_of(node) == place]
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


def same_data(images, failures):
    """Rule 2."""
    exports = [Export(image) for image in images]
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


def main():
    failures = []
    images = sharing_images()
    if BASE not in images and images:
        failures.append(f'{BASE}, the diffs\' base, does not include common/')
    if common_files():
        same_code(images, failures)
        no_address(failures)
        if images:
            same_data(images, failures)
    for failure in failures:
        print(failure)
    print(f'common/: {len(common_files())} files, shared by {", ".join(images) or "no image"}')
    if failures:
        sys.exit(f'{len(failures)} failures')


if __name__ == '__main__':
    main()
