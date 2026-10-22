"""Computes docs/firmware/modules.md's figures -- the functions in each
layer and module, the callers and calls it cites, the calls the wrong
way -- from tools/ghidra/names/modules.tsv and the direct calls of
third_party/firmware/decompiled/z3805a-3543b.c, and checks them, or with
--write rewrites them in place.

    python3 -I tools/firmware/module_figures.py [--write]

The calls are those the exported C makes by name from each function's
body: the decompiler's reading, which the export keeps (under #if 0) for
a function written by hand too, so that the figures are the image's and
not a rewrite's.  A call through a pointer is not one; nor is a call
from a function modules.tsv leaves out (the installer's copies, the
assembly routines, the pForth kernel).  Exits nonzero on drift.
"""

import argparse
import collections
import dataclasses
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
MODULES = ROOT / 'tools/ghidra/names/modules.tsv'
EXPORT = ROOT / 'third_party/firmware/decompiled/z3805a-3543b.c'
DOCUMENT = ROOT / 'docs/firmware/modules.md'

# lowest first; a call from a layer to a later one goes the wrong way
LAYERS = ('platform', 'devices', 'services', 'applications')
# the export's line before each function: its address
FUNCTION_START = re.compile(r'^/\* [0-9a-f]{8} \*/$', re.M)
# a definition's first line: at column 0, its name before its parameters
DEFINITION = re.compile(r'^[A-Za-z_][^;(]*?\b([A-Za-z_]\w*)\s*\(', re.M)
# a body: from a brace at column 0 to the next
BODY = re.compile(r'^\{$.*?^\}$', re.M | re.S)
# an assembly routine's block names it
ASSEMBLY = re.compile(r'^\t"\t\.globl (\w+)\\n"$', re.M)
# comments and literals, cut from a body before its calls are read
COMMENT = re.compile(r'/\*.*?\*/|//[^\n]*', re.S)
STRING = re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'')
# a call by name, or a name a macro or cast puts before a parenthesis
CALL = re.compile(r'\b([A-Za-z_]\w*)\s*\(')
# the debug output the drivers call (modules.md, "platform/debug")
DEBUG_OUTPUT = ('fatal_error', 'task_debug_message', 'task_dmessage',
                'debug_sprintf')
HANDLERS = 'applications/scpi/handlers/'
# a figure modules.md spells out, by its value
NUMBER_WORDS = ('zero', 'one', 'two', 'three', 'four', 'five', 'six',
                'seven', 'eight', 'nine', 'ten', 'eleven', 'twelve',
                'thirteen', 'fourteen', 'fifteen', 'sixteen', 'seventeen',
                'eighteen', 'nineteen', 'twenty')


@dataclasses.dataclass
class Program:
    """modules.tsv's functions and the export's direct calls among them."""

    module: dict[str, str]  # each function's module path, LAYER/MODULE[/PART]
    assembly: set[str]  # those of them the export gives as assembly
    # each function's direct calls, by callee, one entry to a call site
    calls: dict[str, collections.Counter]

    def layer(self, name):
        return self.module[name].split('/')[0]

    def call_sites(self, callee, callers=None):
        """Call sites of callee, from callers' bodies (default all)."""
        return sum(counts[callee] for caller, counts in self.calls.items()
                   if callers is None or caller in callers)

    def callers(self, callees):
        """The functions calling any of callees."""
        return {caller for caller, counts in self.calls.items()
                if any(counts[callee] for callee in callees)}

    def in_modules(self, predicate):
        return {name for name, path in self.module.items() if predicate(path)}

    def wrong_way(self):
        """(caller, callee) pairs calling from a layer to a higher one."""
        return {(caller, callee) for caller, counts in self.calls.items()
                for callee in counts if callee in self.module
                and LAYERS.index(self.layer(callee))
                > LAYERS.index(self.layer(caller))}


def read_modules():
    module = {}
    for line in MODULES.read_text().splitlines():
        if line and not line.startswith('#'):
            _, name, path = line.split('\t')[:3]
            module[name] = path
    return module


def read_program():
    module = read_modules()
    text = EXPORT.read_text()
    starts = [match.start() for match in FUNCTION_START.finditer(text)]
    calls = {}
    for start, end in zip(starts, starts[1:] + [len(text)]):
        piece = text[start:end]
        body = BODY.search(piece)
        if body is None:
            continue
        definitions = DEFINITION.findall(piece, 0, body.start())
        if not definitions or definitions[-1] not in module:
            continue
        name = definitions[-1]
        body = STRING.sub('""', COMMENT.sub(' ', body.group()))
        calls[name] = collections.Counter(
            callee for callee in CALL.findall(body) if callee != name)
    missing = sorted(set(module) - set(calls))
    if missing:
        sys.exit(f'no body in the export for {", ".join(missing)}')
    assembly = set(ASSEMBLY.findall(text)) & set(module)
    return Program(module, assembly, calls)


def figures(program):
    """Each figure's pattern in modules.md, its number the one group, and
    its value: "callers" counts the functions calling, "call sites" the
    calls, and a phrase citing a call count gives both, "N call sites in
    M callers", each checked by a pattern of its own.  A pattern matching
    other than
    once is an error, so that a figure reworded in the prose is not left
    unchecked."""
    count = collections.Counter(program.module.values())
    layer = collections.Counter(path.split('/')[0]
                                for path in program.module.values())
    devices = program.in_modules(lambda path: path.startswith('devices/'))
    outside_status = program.in_modules(lambda path: path != 'services/status')
    diag_words = program.in_modules(
        lambda path: path == 'applications/console') & {
            name for name in program.module if name.startswith('diag_')}
    handlers = sum(n for path, n in count.items() if path.startswith(HANDLERS))

    rows = [(rf'`{re.escape(name)}` \((\d+) callers?\)',
             len(program.callers([name])))
            for name in ('set_interrupt_mask', 'duart_output_bits',
                         'eeprom_write', 'get_date_time',
                         'interval_next_reading')]
    rows += [
        (r'each of (\d+)\s+functions', len(program.module)),
        (r'all (\d+) it gives as C', len(program.module)
         - len(program.assembly)),
        (r'and (\w+), `drtb_read`\s+and `sci_read`, assembly',
         len(program.assembly)),
        (r'\((\d+) callers\s+outside the module\)',
         len(program.callers(['status_report_from']) & outside_status)),
        (r'`scpi_numeric_set`, (\d+) callers',
         len(program.callers(['scpi_numeric_set']))),
        (r'the (\d+) `diag_\*` words', len(diag_words)),
        (r'^(\w+) direct calls go from a lower layer',
         len(program.wrong_way())),
        (r'^\| `scpi/handlers/\*` \| (\d+) \|', handlers),
    ]
    # a call count is the call sites and the functions they are in, each
    # phrase's two numbers checked apart
    for prefix, callees, callers in (
            (r'interval counter\'s: ', DEBUG_OUTPUT, devices),
            (r'`return_zero` \(', ('return_zero',), None),
            (r'`scpi_query_failed` \(', ('scpi_query_failed',), None)):
        sites = sum(program.call_sites(name, callers) for name in callees)
        calling = program.callers(callees)
        if callers is not None:
            calling &= callers
        rows += [(prefix + r'(\d+) call sites in \d+ callers\)', sites),
                 (prefix + r'\d+ call sites in (\d+) callers\)', len(calling))]
    rows += [(rf'^\| {name} \| (\d+) \|', layer[name]) for name in LAYERS]
    for path, n in count.items():
        if path.startswith(HANDLERS):
            group = re.escape(path[len(HANDLERS):])
            rows.append((rf'`{group}` \([^)]*?(\d+)[,)]', n))
        else:
            rows.append((module_row(path), n))
    return rows


def module_row(path):
    """A module's row in its layer's table: `MODULE` under its layer's
    heading, or `MODULE/PART`."""
    layer, _, rest = path.partition('/')
    heading = layer.capitalize()
    return (rf'^## {heading}\n(?:(?!^## ).*\n)*?\| `{re.escape(rest)}` '
            r'\| (\d+) \|')


def spelled(value, old):
    """value written as old was: digits, or a word capitalized as it."""
    if old.isdigit():
        return str(value)
    word = NUMBER_WORDS[value]
    return word.capitalize() if old[0].isupper() else word


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--write', action='store_true',
                        help='rewrite the figures in modules.md')
    arguments = parser.parse_args()
    program = read_program()
    text = DOCUMENT.read_text()
    errors = []
    edits = []
    for pattern, value in figures(program):
        matches = list(re.finditer(pattern, text, re.M))
        if len(matches) != 1:
            errors.append(f'{len(matches)} matches for {pattern!r}')
            continue
        match = matches[0]
        old = match.group(1)
        new = spelled(value, old)
        if new != old:
            line = text.count('\n', 0, match.start(1)) + 1
            errors.append(f'{DOCUMENT.name}:{line}: {old} should be {new}')
            edits.append((match.start(1), match.end(1), new))
    if arguments.write and edits:
        for start, end, new in sorted(edits, reverse=True):
            text = text[:start] + new + text[end:]
        DOCUMENT.write_text(text)
    for error in errors:
        print(error)
    unfixed = len(errors) - (len(edits) if arguments.write else 0)
    return 1 if unfixed else 0


if __name__ == '__main__':
    sys.exit(main())
