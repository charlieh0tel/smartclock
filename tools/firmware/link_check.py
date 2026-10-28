# /// script
# requires-python = ">=3.11"
# ///
"""Proves an image's rewrite complete by linking it: the hand-written C
and the assembly alone, every function of the image required.

The C is the export's declarations -- its types, globals and prototypes,
everything before its first function -- the hand-written C it includes,
rewritten/common/ and rewritten/IMAGE/, and the .set statements that
make one routine's name another's; the decompiled C, every function's
reading, is left out whether or not it is under #if 0.  The assembly is
the export's __asm__ blocks.  rebuild_link.py links the two with the
CPU32's libgcc, every function of the export's .conventions.jsonl
required (ld's --require-defined; a name another's .set makes, its
target), so that one neither written by hand nor assembly fails the
link, named, referenced or not.  The link's layout is rebuild_link.py's:
each piece at its address, since its short branches reach only there,
and gcc's code where the linker places it.  No byte is compared: the
function check proves that what is there agrees, and this that nothing
is missing.

    DECOMPILED_CFLAGS='...' CPU32_LIBGCC=... uv run tools/firmware/link_check.py \\
        third_party/firmware/decompiled/z3805a-3543b/z3805a-3543b.c

make check-link runs it for every image.  Needs m68k-linux-gnu gcc and
binutils.  Prints one summary line, the image's functions required and
how many the hand-written C and the assembly define; exits nonzero if
the link fails, the export's declarations define a function, or gcc's
code reads a word or long of an object of the C's data that is at an
odd address or aligned to a byte alone (rebuild_link.misaligned_data):
the CPU32 faults on it, where Unicorn's 68020 does not.
"""

import json
import os
import re
import shlex
import subprocess
import sys
import tempfile

from decompiled_check import TOOLS
from rebuild_link import CODE_BLOCK, compile_c, link, misaligned_static, run

# The marker that heads each function in the export: its address, an
# installer function's prefixed.  The first ends the declarations.
FUNCTION_MARKER = re.compile(r"^/\* (?:installer::)?[0-9a-f]{8} \*/$", re.M)
# What is kept after the declarations: a hand-written file the export
# includes, a statement making one name another's, and an __asm__ block
# holding a routine's pieces (rebuild_link.CODE_BLOCK).
KEPT = re.compile(r'^(?:#include "[^"]*"|__asm__\("\.set \w+, \w+"\);)$|' + CODE_BLOCK.pattern, re.M | re.S)
# A name another's .set makes, and its target.
ALIAS = re.compile(r'^__asm__\("\.set (\w+), (\w+)"\);$', re.M)
# nm's kinds of a symbol defined in code.
CODE_KINDS = "Tt"
# What ld says of a required function that nothing defines.
MISSING = re.compile(r"required symbol `(\w+)' not defined")


def code_symbols(path: str) -> set[str]:
    """The names an object defines in code."""
    return {fields[2] for fields in map(str.split, run(TOOLS + "nm", path).splitlines())
            if len(fields) == 3 and fields[1] in CODE_KINDS}


def target(name: str, aliases: dict[str, str]) -> str:
    """The routine a name is, through any .set making it another's."""
    while name in aliases:
        name = aliases[name]
    return name


def check(c_path: str, cflags: list[str], work: str) -> tuple[int, int, int, int]:
    """Links the image's hand-written C and assembly in work; the number
    of functions required, and of them those the hand-written C defines,
    those the assembly does and those a .set makes another routine's
    name.  Exits naming each function missing."""
    c = open(c_path).read()
    base = c_path.removesuffix(".c")
    first = FUNCTION_MARKER.search(c)
    if first is None:
        raise SystemExit(f"{c_path}: no function")
    declarations = c[: first.start()]
    declared = work + "/declarations.c"
    open(declared, "w").write(declarations)
    compile_c(c_path, cflags, declared, work + "/declarations.o")
    defined = code_symbols(work + "/declarations.o")
    if defined:
        raise SystemExit(f"{c_path}: the declarations define functions: {sorted(defined)[:5]}")

    kept = "".join(found.group(0) + "\n" for found in KEPT.finditer(c, first.start()))
    aliases = dict(ALIAS.findall(c))
    names = [json.loads(line)["name"] for line in open(base + ".conventions.jsonl")]
    required = sorted({target(name, aliases) for name in names})
    try:
        link(c_path, cflags, work, work + "/linked.elf", c=declarations + kept,
             ld_options=tuple(f"--require-defined={name}" for name in required))
    except subprocess.CalledProcessError as error:
        missing = MISSING.findall(error.stderr)
        for name in missing:
            made = sorted(alias for alias, routine in aliases.items() if target(alias, aliases) == name)
            print(f"  missing: {name}" + (f", and its other names {', '.join(made)}" if made else ""))
        raise SystemExit(error.stderr if not missing else f"{len(missing)} of {len(names)} functions missing")
    c_defined, assembly_defined = code_symbols(work + "/c.o"), code_symbols(work + "/a-image.o")
    own = [name for name in names if name not in aliases]
    elsewhere = [name for name in own if name not in c_defined | assembly_defined]
    if elsewhere:
        raise SystemExit(f"defined by neither the C nor the assembly: {', '.join(elsewhere)}")
    unaligned = misaligned_static(c_path, cflags, work, work + "/linked.elf")
    for access in unaligned:
        print(f"  misaligned: {access}")
    if unaligned:
        raise SystemExit(f"{len(unaligned)} word or long accesses to data a byte's alignment may put at an odd address")
    hand = sum(name in c_defined for name in own)
    return len(names), hand, len(own) - hand, len(names) - len(own)


def main() -> int:
    if len(sys.argv) != 2 or "DECOMPILED_CFLAGS" not in os.environ:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    c_path = sys.argv[1]
    image = os.path.basename(c_path).removesuffix(".c")
    with tempfile.TemporaryDirectory() as work:
        count, hand, assembly, named = check(c_path, shlex.split(os.environ["DECOMPILED_CFLAGS"]), work)
    print(f"{image}: functions required: {count}, all defined: {hand} hand-written C, {assembly} assembly, "
          f"{named} another routine's name")
    return 0


if __name__ == "__main__":
    sys.exit(main())
