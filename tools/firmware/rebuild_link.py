# /// script
# requires-python = ">=3.11"
# ///
"""Links the decompiled C into a rebuilt image and confirms its assembly
lands on the image's own bytes.

Two objects are linked.  The assembly, every routine that is not C,
is assembled alone, as decompiled_check.py assembles
it, and each piece (a section .text.NAME.ADDRESS) placed at its address,
the installer's at theirs in RAM.  A C function it calls is the image's
own, at the function's address, renamed __image_NAME so that it is not
gcc's: the original's callers keep the original's callees, and its
2-byte slots, until a function is replaced.  The C is compiled without
the assembly, its globals kept, and placed at REBUILT, clear of
everything the board decodes; it calls gcc's C, and the assembly
through its names (the bindings' asm statements).  gcc's helpers come
from CPU32_LIBGCC, the CPU32's soft-float libgcc that cpu32_libgcc.py
builds, not the toolchain's 68020 one.
The generated SCPI trees' records must lie in the link as the parser
reads them: each child list's children right after the list, and every
record but text at an even address.

    DECOMPILED_CFLAGS='...' CPU32_LIBGCC=... uv run tools/firmware/rebuild_link.py \\
        third_party/firmware/decompiled/z3805a-3543b/z3805a-3543b.c \\
        third_party/firmware/images/z3805a-3543b.bin [ELF]

make check-decompiled runs it with its flags and library, as make runs
the function and bridge checks, which link the same way.  Needs
m68k-linux-gnu gcc and binutils.  Exits nonzero if the link fails, a
piece is not the image's bytes, a tree's record is misplaced, gcc's
code holds an instruction the CPU32 does not have
(cpu32_libgcc.NOT_CPU32) or, as linked or compiled with no function
inlined, a word or long access at an odd offset from the frame or
stack pointer (misaligned), or to an object of the C's data at an odd
address or aligned to a byte alone (misaligned_data).
"""

import bisect
import json
import os
import re
import shlex
import struct
import subprocess
import sys
import tempfile
from dataclasses import dataclass

from cpu32_libgcc import not_cpu32
from decompiled_check import RAM, TOOLS, assembly_lines, installer_image

# Where gcc's code is linked: above the chip selects the reset code
# programs, the highest of which, CS4, is at 0x500000
# (docs/firmware/restart.md, "The chip selects"), so that a read of the
# EEPROM at 0x400000 reaches no code.
REBUILT = 0x600000
# Where gcc's own data is, apart from its code, which the harness keeps
# read-only: libgcc's _fpCCR among it.
REBUILT_DATA = 0x6F0000
# The rebuilt image's sections the harness loads: its code and its data.
REBUILT_SECTION = ".rebuilt"
LOADED = (REBUILT_SECTION, ".rebuilt_data")
# The variable naming the CPU32's libgcc.
LIBGCC_VARIABLE = "CPU32_LIBGCC"
# A piece's section, its routine's name and its address.
PIECE = re.compile(r"^\.text\.(\w+)\.([0-9a-f]+)$")
# An __asm__ block that holds code: a routine's pieces.
CODE_BLOCK = re.compile(r"^__asm__\(\n.*?\.pushsection.*?^\);$", re.S | re.M)
# The prefix of the image's own copy of a C function.
IMAGE_PREFIX = "__image_"
# The generated SCPI trees the C includes (docs/scpi/README.md, "As C").
TREE_INCLUDE = re.compile(r'^#include "([^"]*scpi_tree_data\.c)"$', re.M)
# A generated tree's record definition, its name, and its type's start;
# a keyword pair, a unit's name or other text is a char array.
TREE_RECORD = re.compile(r"^(const char\b|const\s|\w+\* const\s)[^=;{}]*?\b(\w+)(?:\s*\[\])?\s*=", re.M)
TEXT_TYPE = "const char"
# What a child list's children's name adds to the list's.
CHILDREN_SUFFIX = "_children"
# ELF32 header and section header layouts, big-endian.
ELF_HEADER = ">16sHHIIIIIHHHHHH"
SECTION_HEADER = ">IIIIIIIIII"
NOBITS = 8
# readelf's type and binding of a global data object's symbol.
OBJECT = "OBJECT"
GLOBAL = "GLOBAL"
# objdump's m68k listing: a function's heading, an instruction's address,
# mnemonic and operands, and a displacement from the frame or stack
# pointer, in decimal (an indexed operand's, in hex, has an index of no
# known parity).  A mnemonic's last letter is its size; those that take
# an address without accessing it are not accesses, nor is movep, which
# moves its word or long a byte at a time (and which gcc does not make:
# objdump finds it in a switch's table of offsets read as code).
FUNCTION = re.compile(r"^[0-9a-f]+ <(\S+)>:$")
INSTRUCTION = re.compile(r"^\s*([0-9a-f]+):\s+(\S+)\s*(.*)$")
STACK_OFFSET = re.compile(r"%(?:fp|sp)@\((-?\d+)\)")
WORD_LONG = "wl"
# The CPU32's alignment of a word or long access: at an odd address it
# takes an address error (MC68331UM, 4.4.4).
ACCESS_ALIGNMENT = 2
NOT_ACCESSES = ("lea", "pea", "jsr", "jmp", "movepw", "movepl")
# objdump's heading of a section, and a mark past the last
SECTION_HEADING = re.compile(r"^Disassembly of section (\S+):$")
SECTION_HEADING_END = "\0"
# A switch's jump through its table of offsets (code_listing).
SWITCH_JUMP = re.compile(r"\tjmp %pc@\(([0-9a-f]+) <[^>]*>,%d\d:w\)")
# The C compiled again with no function inlined, each function in a
# frame of its own alone: where a frame's objects lie, and so their
# parity, changes with what is inlined into it, and a link that calls
# the image's copy of a callee in place of gcc's inlines none of it.
NO_INLINE = "-fno-inline"
# The C compiled again with each object in a section of its own, whose
# alignment is then the object's (misaligned_data).
DATA_SECTIONS = "-fdata-sections"
# readelf's line of a section: its name and, last, its alignment.
OWN_SECTION = re.compile(r"^\s*\[\s*\d+\]\s+\.(?:rodata|data|bss)\.(\S+)\s.*\s(\d+)$")
# An address register loaded with an address, and one written otherwise.
ADDRESS_LOAD = re.compile(r"^(?:lea|moveal|movel)$")
LOADED_ADDRESS = re.compile(r"^(?:#(\d+)|([0-9a-f]+)(?: <[^>]*>)?),%(a[0-7])$")
WRITTEN_REGISTER = re.compile(r",%(a[0-7])$")
INDIRECT = re.compile(r"%(a[0-7])@")
ABSOLUTE_OPERAND = re.compile(r"(?<![#\w@(])([0-9a-f]{5,8})(?: <[^>]*>)?(?=,|$)")


@dataclass
class Section:
    """A section of a linked ELF: its address and its bytes (zeros for
    one that occupies no file space)."""

    name: str
    address: int
    data: bytes


def sections(elf: bytes) -> list[Section]:
    header = struct.unpack_from(ELF_HEADER, elf)
    table, entry_size, count, names_index = header[6], header[11], header[12], header[13]
    raw = [struct.unpack_from(SECTION_HEADER, elf, table + i * entry_size) for i in range(count)]
    names = raw[names_index][4]
    found = []
    for name, kind, _, address, offset, size, *_ in raw:
        end = elf.index(b"\0", names + name)
        data = bytes(size) if kind == NOBITS else elf[offset : offset + size]
        found.append(Section(elf[names + name : end].decode(), address, data))
    return found


def run(*command: str) -> str:
    return subprocess.run(command, capture_output=True, text=True, check=True).stdout


def compile_c(c_path: str, cflags: list[str], source: str, compiled: str) -> None:
    """Compiles `source`, the C file's C without its assembly, to
    `compiled`, the hand-written C it includes named from the C file's
    own folder."""
    run(TOOLS + "gcc", *cflags, "-I", os.path.dirname(os.path.abspath(c_path)), "-c", "-o", compiled, source)


def disassembly(path: str, section: str | None = None) -> str:
    """objdump's listing of an object's code, or of an ELF's section."""
    selected = ("-j", section) if section else ()
    return run(TOOLS + "objdump", "-d", "--no-show-raw-insn", *selected, path)


def code_listing(path: str, section: str | None = None) -> str:
    """disassembly's listing with each switch's table of offsets left
    out, and the code after a table disassembled again from the table's
    end where objdump, reading the table as code, ran past it.  gcc's
    switch is a movew and a jmp through %pc@(TABLE,%dN:w), its table
    words from TABLE, each an offset from it to a case: the table ends
    at the nearest case."""
    data = {found.name: found for found in sections(open(path, "rb").read())}
    lines, name, listing = [], section, []
    for line in disassembly(path, section).splitlines() + [SECTION_HEADING_END]:
        heading = SECTION_HEADING.match(line)
        if heading or line == SECTION_HEADING_END:
            if name in data:
                lines += without_tables(path, name, data[name], listing)
            name, listing = (heading.group(1) if heading else None), []
            continue
        listing.append(line)
    return "\n".join(lines)


def without_tables(path: str, name: str, section: Section, listing: list[str]) -> list[str]:
    """One section's listing, code_listing's way."""
    done: set[int] = set()
    while True:
        table = next((found for found in tables(listing, section) if found[0] not in done), None)
        if table is None:
            return listing
        start, end = table
        done.add(start)
        starts = {int(found.group(1), 16) for found in map(INSTRUCTION.match, listing) if found}
        heads = [int(line.split()[0], 16) for line in listing if FUNCTION.match(line)]
        stop = min([head for head in heads if head > start], default=section.address + len(section.data))
        again = []
        if end not in starts and end < stop:
            again = [line for line in run(TOOLS + "objdump", "-d", "--no-show-raw-insn", "-j", name,
                                          f"--start-address={end:#x}", f"--stop-address={stop:#x}",
                                          path).splitlines() if INSTRUCTION.match(line)]
        else:
            stop = end
        at = next((i for i, line in enumerate(listing) if in_range(line, start, stop)), len(listing))
        listing = [line for line in listing[:at] if not in_range(line, start, stop)] + again + [
            line for line in listing[at:] if not in_range(line, start, stop)]


def in_range(line: str, start: int, end: int) -> bool:
    """Whether a listing's instruction line lies from start up to end."""
    found = INSTRUCTION.match(line)
    return bool(found) and not FUNCTION.match(line) and start <= int(found.group(1), 16) < end


def tables(listing: list[str], section: Section) -> list[tuple[int, int]]:
    """Each switch's table in a listing, its start and end (code_listing)."""
    found = []
    for line in listing:
        dispatch = SWITCH_JUMP.search(line)
        if not dispatch:
            continue
        start = int(dispatch.group(1), 16)
        at, end = start, None
        while end is None or at < end:
            offset = at - section.address
            if offset + 2 > len(section.data):
                break
            entry = struct.unpack_from(">h", section.data, offset)[0]
            if entry <= 0:
                break
            end = start + entry if end is None else min(end, start + entry)
            at += 2
        if end is not None:
            found.append((start, end))
    return found


def misaligned(listing: str) -> list[str]:
    """The word and long accesses at an odd offset from the frame or
    stack pointer, both even, in objdump's listing, each as its function
    and instruction.  gcc makes them for an object smartclock.h packs
    (#pragma pack(1)) that it puts at an odd offset, -mstrict-align
    notwithstanding.  An access through another register is not seen:
    its base's parity is not in the listing."""
    found, function = [], None
    for line in listing.splitlines():
        heading = FUNCTION.match(line)
        if heading:
            function = heading.group(1)
            continue
        instruction = INSTRUCTION.match(line)
        if not function or not instruction or instruction.group(2) in NOT_ACCESSES:
            continue
        if instruction.group(2)[-1] in WORD_LONG and any(
                int(offset) % ACCESS_ALIGNMENT for offset in STACK_OFFSET.findall(instruction.group(3))):
            found.append(f"{function}: {instruction.group(1)}: {instruction.group(2)} {instruction.group(3)}")
    return found


def misaligned_c(c_path: str, cflags: list[str], work: str, elf_path: str) -> list[str]:
    """misaligned's accesses in gcc's code as link() linked it in work,
    and in its C compiled again with NO_INLINE."""
    no_inline = work + "/c-no-inline.o"
    compile_c(c_path, [*cflags, NO_INLINE], work + "/c.c", no_inline)
    return misaligned(code_listing(elf_path, REBUILT_SECTION)) + [
        f"{access} ({NO_INLINE})" for access in misaligned(code_listing(no_inline))]


def data_objects(elf_path: str) -> list[tuple[int, int, str]]:
    """A linked ELF's data objects, local ones too: (address, size, name),
    by address."""
    found = []
    for line in run(TOOLS + "readelf", "-sW", elf_path).splitlines():
        # number, value, size, type, binding, visibility, section, name
        fields = line.split()
        if len(fields) == 8 and fields[3] == OBJECT:
            found.append((int(fields[1], 16), int(fields[2], 0), fields[7]))
    return sorted(found)


def alignments(compiled: str) -> dict[str, int]:
    """Each data object's alignment in an object compiled with
    DATA_SECTIONS, by name."""
    return {found.group(1): int(found.group(2))
            for found in map(OWN_SECTION.match, run(TOOLS + "readelf", "-SW", compiled).splitlines()) if found}


def misaligned_data(listing: str, placed: list[tuple[int, int, str]], aligned: dict[str, int]) -> list[str]:
    """gcc's word and long accesses to an object of the C's data that is
    at an odd address, or that only a byte's alignment keeps from one:
    an object smartclock.h packs (#pragma pack(1)) may be put anywhere,
    and the CPU32 takes an address error on a word or long at an odd
    address (MC68331UM, 4.4.4) where Unicorn's 68020 does not.  An
    access is seen absolute, or through an address register a lea or a
    move of the object's address loaded in the same function and
    nothing has written since but an increment."""
    starts = [at for at, _, _ in placed]

    def holding(address: int) -> tuple[int, int, str] | None:
        at = bisect.bisect_right(starts, address) - 1
        if at >= 0 and address < placed[at][0] + max(placed[at][1], 1):
            return placed[at]
        return None

    def exposed(found: tuple[int, int, str]) -> bool:
        return found[0] % ACCESS_ALIGNMENT != 0 or aligned.get(found[2], ACCESS_ALIGNMENT) < ACCESS_ALIGNMENT

    flagged, function, registers = [], None, {}
    for line in listing.splitlines():
        heading = FUNCTION.match(line)
        if heading:
            function, registers = heading.group(1), {}
            continue
        instruction = INSTRUCTION.match(line)
        if not function or not instruction:
            continue
        mnemonic, operands = instruction.group(2), instruction.group(3)
        if mnemonic[-1:] in WORD_LONG and mnemonic not in NOT_ACCESSES:
            targets = [registers.get(register) for register in INDIRECT.findall(operands)]
            targets += [holding(int(value, 16)) for value in ABSOLUTE_OPERAND.findall(operands)]
            for found in {found for found in targets if found is not None and exposed(found)}:
                flagged.append(f"{function}: {instruction.group(1)}: {mnemonic} {operands}: {found[2]} at "
                               f"{found[0]:#x}, aligned to {aligned.get(found[2], '?')}")
        load = LOADED_ADDRESS.match(operands)
        if ADDRESS_LOAD.match(mnemonic) and load:
            address = int(load.group(1)) if load.group(1) else int(load.group(2), 16)
            registers[load.group(3)] = holding(address)
            continue
        written = WRITTEN_REGISTER.search(operands)
        if written and not (mnemonic.startswith(("addq", "subq", "adda", "suba")) or mnemonic.startswith("lea")
                            and f"%{written.group(1)}@" in operands):
            registers.pop(written.group(1), None)
    return flagged


def misaligned_static(c_path: str, cflags: list[str], work: str, elf_path: str) -> list[str]:
    """misaligned_data's accesses in gcc's code as link() linked it in
    work, against its objects' alignments compiled with DATA_SECTIONS."""
    with_sections = work + "/c-data-sections.o"
    compile_c(c_path, [*cflags, DATA_SECTIONS], work + "/c.c", with_sections)
    return misaligned_data(code_listing(elf_path, REBUILT_SECTION), data_objects(elf_path), alignments(with_sections))


def script(pieces: list[str], image_names: dict[str, int]) -> str:
    """The linker script: the image's copy of each function at its
    address, each piece at its address, the rest at REBUILT."""
    lines = [f"{IMAGE_PREFIX}{name} = 0x{address:x};" for name, address in image_names.items()]
    lines.append("SECTIONS {")
    for piece in pieces:
        address = int(PIECE.match(piece).group(2), 16)
        lines.append(f"  {piece} 0x{address:x} : {{ *({piece}) }}")
    lines.append(
        f"  {REBUILT_SECTION} 0x{REBUILT:x} : {{ *(.text) *(.text.*) *(.rodata) *(.rodata.*) }}"
    )
    lines.append(f"  .rebuilt_data 0x{REBUILT_DATA:x} : {{ *(.data) *(.data.*) *(.bss) *(.bss.*) *(COMMON) }}")
    lines.append("  /DISCARD/ : { *(.comment) *(.note.*) }")
    lines.append("}")
    return "\n".join(lines) + "\n"


def link(c_path: str, cflags: list[str], work: str, elf_path: str, extra: str = "",
         c: str | None = None, ld_options: tuple[str, ...] = ()) -> list[Section]:
    """Links the C file's rebuilt image to elf_path, with extra C after
    the file's own (the function check's probes); its sections.  c, if
    given, is linked in place of the file's text (link_check.py's, its
    decompiled C left out), and ld_options go to ld."""
    c = open(c_path).read() if c is None else c
    base = c_path.removesuffix(".c")
    addresses = {}
    for line in open(base + ".conventions.jsonl"):
        function = json.loads(line)
        addresses[function["name"]] = int(function["address"].split(":")[-1], 16)
    made = set(re.findall(r"^(\w+) = 0x", open(base + ".symbols.ld").read(), re.M))

    c_only, compiled = work + "/c.c", work + "/c.o"
    open(c_only, "w").write(CODE_BLOCK.sub("", c) + extra)
    compile_c(c_path, cflags, c_only, compiled)
    # what the C defines that is not a function: the data the assembly may name
    c_data = {
        name for kind, name in re.findall(r"^\S* ([BCDRbdr]) (\S+)$", run(TOOLS + "nm", compiled), re.M)
    }

    assembly, assembled, renamed = work + "/a.s", work + "/a.o", work + "/a-image.o"
    open(assembly, "w").write("".join(assembly_lines(c)))
    run(TOOLS + "as", "-mcpu=cpu32", "-o", assembled, assembly)
    undefined = [name for name in run(TOOLS + "nm", "-u", assembled).split()[1::2] if name not in c_data]
    image_names = {name: addresses[name] for name in undefined if name not in made}
    unknown = [name for name in undefined if name not in made and name not in addresses]
    if unknown:
        raise SystemExit(f"the assembly names what is neither a function nor made: {unknown[:5]}")
    with open(work + "/rename", "w") as rename:
        rename.writelines(f"{name} {IMAGE_PREFIX}{name}\n" for name in image_names)
    run(TOOLS + "objcopy", "--redefine-syms", work + "/rename", assembled, renamed)

    listing = run(TOOLS + "readelf", "-SW", renamed)
    pieces = [name for name in re.findall(r"\]\s+(\.text\.\S+)", listing) if PIECE.match(name)]
    libgcc = os.environ.get(LIBGCC_VARIABLE)
    if not libgcc:
        raise SystemExit(f"{LIBGCC_VARIABLE} unset: the CPU32's libgcc, which make builds (cpu32_libgcc.py)")
    linker_script = work + "/rebuilt.ld"
    # every function's image copy is named, the harness's to call
    open(linker_script, "w").write(script(pieces, addresses))
    # routines that share code each carry the shared bytes, so their
    # pieces overlap; main confirms every copy is the image's
    run(
        TOOLS + "ld", "--no-check-sections", "--no-warn-rwx-segments", *ld_options,
        "-T", base + ".symbols.ld", "-T", linker_script,
        "-o", elf_path, renamed, compiled, libgcc,
    )
    return sections(open(elf_path, "rb").read())


def symbols(elf_path: str) -> dict[str, int]:
    """A linked ELF's symbols and their values."""
    values = {}
    for line in run(TOOLS + "nm", elf_path).splitlines():
        fields = line.split()
        if len(fields) == 3:
            values[fields[2]] = int(fields[0], 16)
    return values


def objects(elf_path: str) -> dict[str, tuple[int, int]]:
    """A linked ELF's data objects, by name their address and size."""
    found = {}
    for line in run(TOOLS + "readelf", "-sW", elf_path).splitlines():
        # number, value, size, type, binding, visibility, section, name
        fields = line.split()
        if len(fields) == 8 and fields[3] == OBJECT and fields[4] == GLOBAL:
            found[fields[7]] = (int(fields[1], 16), int(fields[2], 0))
    return found


def tree_records(c_path: str) -> tuple[set[str], set[str]]:
    """The records of the generated SCPI trees the C file includes, by
    name, and those of them that are text."""
    records, texts = set(), set()
    for included in TREE_INCLUDE.findall(open(c_path).read()):
        tree = open(os.path.join(os.path.dirname(os.path.abspath(c_path)), included)).read()
        for kind, name in TREE_RECORD.findall(tree):
            records.add(name)
            if kind == TEXT_TYPE:
                texts.add(name)
    return records, texts


def tree_layout(found: dict[str, tuple[int, int]], texts: set[str]) -> list[str]:
    """What breaks the layout the parser reads of a tree's records, by
    name their (address, size): a child list's children, an array of
    their own that the parser reads past the list's end, not right after
    the list, as the image lays them out, and a record but text at an
    odd address, where its words are misaligned (MC68331UM, 4.4.4)."""
    broken = []
    for name, (at, size) in sorted(found.items(), key=lambda item: item[1]):
        owner = found.get(name.removesuffix(CHILDREN_SUFFIX)) if name.endswith(CHILDREN_SUFFIX) else None
        if owner is not None and sum(owner) != at:
            broken.append(f"{name}: at {at:#x}, not right after its list, which ends at {sum(owner):#x}")
        if name not in texts and at % 2:
            broken.append(f"{name}: at an odd address, {at:#x}, where its words are misaligned")
    return broken


def main() -> int:
    if len(sys.argv) not in (3, 4) or "DECOMPILED_CFLAGS" not in os.environ:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    c_path, image_path = sys.argv[1:3]
    image = open(image_path, "rb").read()
    installer = installer_image(image)
    with tempfile.TemporaryDirectory() as work:
        elf = sys.argv[3] if len(sys.argv) == 4 else work + "/rebuilt.elf"
        try:
            linked = link(c_path, shlex.split(os.environ["DECOMPILED_CFLAGS"]), work, elf)
        except subprocess.CalledProcessError as error:
            print(error.stderr, file=sys.stderr)
            return 1
        linked_objects = objects(elf)
        foreign = not_cpu32(elf, REBUILT_SECTION)
        unaligned = misaligned_c(c_path, shlex.split(os.environ["DECOMPILED_CFLAGS"]), work, elf)
        unaligned += misaligned_static(c_path, shlex.split(os.environ["DECOMPILED_CFLAGS"]), work, elf)
    records, texts = tree_records(c_path)
    missing = sorted(records - linked_objects.keys())
    broken = tree_layout({name: linked_objects[name] for name in records & linked_objects.keys()}, texts)
    for problem in [f"{name}: not in the link" for name in missing[:5]] + broken:
        print(f"  {problem}")
    print(f"SCPI tree records: {len(records)}, misplaced: {len(missing) + len(broken)}")
    identical = differing = 0
    for section in linked:
        if not PIECE.match(section.name):
            continue
        at, size = section.address, len(section.data)
        original = installer[at - RAM : at - RAM + size] if at >= RAM else image[at : at + size]
        if section.data == original:
            identical += 1
        else:
            differing += 1
            first = next(i for i in range(size) if section.data[i] != original[i])
            print(f"  {section.name}: differs from +{first:x}")
    rebuilt = next(s for s in linked if s.name == REBUILT_SECTION)
    print(f"linked pieces identical: {identical}, differing: {differing}; gcc's code: {len(rebuilt.data)} bytes")
    if foreign:
        print(f"  gcc's code: {', '.join(sorted(set(foreign)))}")
    print(f"gcc's instructions the CPU32 does not have: {len(foreign)}")
    for access in unaligned:
        print(f"  misaligned: {access}")
    print(f"gcc's word or long accesses at odd frame or stack offsets or to data a byte's alignment may put "
          f"at an odd address, which the CPU32 faults on: {len(unaligned)}")
    return 1 if differing or missing or broken or foreign or unaligned else 0


if __name__ == "__main__":
    sys.exit(main())
