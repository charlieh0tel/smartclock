"""Builds libgcc for the CPU32 with soft float, the library the rebuilt
image links (rebuild_link.py), from GCC's source vendored in
third_party/gcc/ with the m68k gcc.

    python3 -I tools/firmware/cpu32_libgcc.py ARCHIVE

The m68k-linux-gnu toolchain has one libgcc, a 68020's with a 68881,
and no CPU32 multilib (gcc -print-multi-lib gives only the default):
its float conversions use the FPU and its 64-bit division and count of
leading zeros BFFFO, neither of which the CPU32 has
(docs/cpu-core-brief.md, "The CPU").  So the members m68k-elf's
gcc builds for its CPU32 multilib are built here: libgcc/Makefile.in's
libgcc2.c routines and config/m68k/t-floatlib's, lb1sf68.S's assembly
and fpgnulib.c, compiled as libgcc/Makefile.in compiles them, with
-mcpu=cpu32, which implies soft float.  Not built: what a hosted
runtime needs and the firmware's C never calls (HOSTED).

Warns if the m68k gcc's version is not the vendored source's.  Exits
nonzero if a member does not build, or if any member holds an
instruction the CPU32 does not have (NOT_CPU32).
"""

import os
import re
import shutil
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor

TOOLS = "m68k-linux-gnu-"
CC = os.environ.get("M68K_CC", TOOLS + "gcc")
# The vendored source (third_party/gcc/README.md), and in it the
# headers written in place of those GCC's build generates.
SOURCE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "third_party", "gcc")
GENERATED = "generated"
VERSION = "gcc/BASE-VER"
# libgcc/Makefile.in's options for compiling libgcc2.a, but -g and the
# divide routines' exceptions, which the firmware has no unwinder for;
# inhibit_libc, since it has no C library either.
CFLAGS = (
    "-mcpu=cpu32", "-O2", "-DIN_LIBGCC2", "-fbuilding-libgcc", "-fno-stack-protector",
    "-Dinhibit_libc", "-fvisibility=hidden", "-DHIDE_EXPORTS",
)
INCLUDES = (GENERATED, "libgcc", "gcc", "include")
# config/m68k/t-floatlib's LIB1ASMFUNCS, from lb1sf68.S.
LIB1ASMFUNCS = (
    "_mulsi3 _udivsi3 _divsi3 _umodsi3 _modsi3 _double _float _floatex "
    "_eqdf2 _nedf2 _gtdf2 _gedf2 _ltdf2 _ledf2 _eqsf2 _nesf2 _gtsf2 _gesf2 _ltsf2 _lesf2"
).split()
# libgcc/Makefile.in's lib2funcs, with its single- and double-word float
# conversions (swfloatfuncs, dwfloatfuncs), LIB2FUNCS_ST and
# LIB2_DIVMOD_FUNCS, from libgcc2.c.
LIB2FUNCS = (
    "_muldi3 _negdi2 _lshrdi3 _ashldi3 _ashrdi3 _cmpdi2 _ucmpdi2 _clear_cache _trampoline __main "
    "_absvsi2 _absvdi2 _addvsi3 _addvdi3 _subvsi3 _subvdi3 _mulvsi3 _mulvdi3 "
    "_negvsi2 _negvdi2 _ctors _ffssi2 _ffsdi2 _clz _clzsi2 "
    "_clzdi2 _ctzsi2 _ctzdi2 _popcount_tab _popcountsi2 _popcountdi2 _paritysi2 _paritydi2 "
    "_powisf2 _powidf2 _powixf2 _powitf2 _mulhc3 _mulsc3 _muldc3 _mulxc3 _multc3 _divhc3 _divsc3 "
    "_divdc3 _divxc3 _divtc3 _bswapsi2 _bswapdi2 _clrsbsi2 _clrsbdi2 _mulbitint3 "
    "_fixunssfsi _fixunsdfsi _fixunsxfsi "
    "_fixsfdi _fixdfdi _fixxfdi _fixtfdi _fixunssfdi _fixunsdfdi _fixunsxfdi _fixunstfdi "
    "_floatdisf _floatdidf _floatdixf _floatditf _floatundisf _floatundidf _floatundixf _floatunditf "
    "_eprintf __gcc_bcmp "
    "_divdi3 _moddi3 _divmoddi4 _udivdi3 _umoddi3 _udivmoddi4 _udiv_w_sdiv _divmodbitint4"
).split()
# Those of them for a hosted runtime: a trampoline's cache flush,
# constructors, __main, and LIB2FUNCS_ST's assertion message and bcmp.
HOSTED = ("_clear_cache", "_trampoline", "__main", "_ctors", "_eprintf", "__gcc_bcmp")
# t-floatlib's LIB2ADD: fpgnulib.c, and xfgnulib.c, fpgnulib.c with
# EXTFLOAT defined.
FPGNULIB = "libgcc/config/m68k/fpgnulib.c"
XFGNULIB = GENERATED + "/xfgnulib.c"
LB1SF68 = "libgcc/config/m68k/lb1sf68.S"
LIBGCC2 = "libgcc/libgcc2.c"
# What the 68020 and 68040 have and the CPU32, the 68000's set and its
# own additions, does not (docs/cpu-core-brief.md, "The CPU"): the bit
# field, CAS, CAS2, PACK, UNPK, CALLM and RTM instructions, any
# coprocessor's, the 68881's among them, the MMU's and MOVE16; their
# mnemonics as objdump decodes a 68040's.
NOT_CPU32 = re.compile(r"^(bf\w+|cas2?\w?|pack|unpk|callm|rtm|cp\w+|f\w+|move16|pflush\w*|pmove|ptest\w*)$")
DECODE_ALL = "m68k:68040"


def run(*command: str) -> str:
    done = subprocess.run(command, capture_output=True, text=True)
    if done.returncode:
        raise SystemExit(f"{' '.join(command)}:\n{done.stderr}")
    return done.stdout


def not_cpu32(path: str, section: str | None = None) -> list[str]:
    """The instructions the CPU32 does not have in an object, or in an
    ELF's section, as their mnemonics."""
    found = []
    selected = ("-j", section) if section else ()
    for line in run(TOOLS + "objdump", "-d", "-m", DECODE_ALL, *selected, path).splitlines():
        fields = line.split("\t")
        if len(fields) >= 3 and NOT_CPU32.match(mnemonic := fields[2].split(" ")[0]):
            found.append(mnemonic)
    return found


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    archive = sys.argv[1]
    source = os.path.normpath(SOURCE)
    version = open(os.path.join(source, VERSION)).read().strip()
    compiler = run(CC, "-dumpfullversion").strip()
    if version != compiler:
        print(f"warning: {CC} is GCC {compiler}, the vendored libgcc source {version}'s", file=sys.stderr)
    includes = [f"-I{os.path.join(source, folder)}" for folder in INCLUDES]
    compiles = [(f"{name}.o", ["-DL" + name, "-xassembler-with-cpp", LB1SF68]) for name in LIB1ASMFUNCS]
    compiles += [(f"{name}.o", ["-DL" + name, LIBGCC2]) for name in LIB2FUNCS if name not in HOSTED]
    compiles += [("fpgnulib.o", [FPGNULIB]), ("xfgnulib.o", [XFGNULIB])]
    with tempfile.TemporaryDirectory() as work:
        members = [os.path.join(work, member) for member, _ in compiles]

        def compile_member(job: tuple[str, list[str]]) -> str:
            member, arguments = job
            *flags, path = arguments
            return run(CC, *CFLAGS, *includes, "-c", "-o", os.path.join(work, member), *flags,
                       os.path.join(source, path))

        with ThreadPoolExecutor() as pool:
            list(pool.map(compile_member, compiles))
        refused = {os.path.basename(member): found for member in members if (found := not_cpu32(member))}
        if refused:
            for member, found in sorted(refused.items()):
                print(f"  {member}: {', '.join(sorted(set(found)))}", file=sys.stderr)
            raise SystemExit("members hold instructions the CPU32 does not have")
        os.makedirs(os.path.dirname(os.path.abspath(archive)), exist_ok=True)
        built = os.path.join(work, "libgcc.a")
        run(TOOLS + "ar", "rcs", built, *members)
        shutil.move(built, archive)
    print(f"{archive}: {len(members)} members, GCC {version}'s source, -mcpu=cpu32")
    return 0


if __name__ == "__main__":
    sys.exit(main())
