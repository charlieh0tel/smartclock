# /// script
# requires-python = ">=3.11"
# ///
"""Links the decompiled C into a rebuilt image and confirms its assembly
lands on the image's own bytes.

Two objects are linked.  The assembly -- every routine that is not C,
and the bridges -- is assembled alone, as decompiled_check.py assembles
it, and each piece (a section .text.NAME.ADDRESS) placed at its address,
the installer's at theirs in RAM.  A C function it calls is the image's
own, at the function's address, renamed __image_NAME so that it is not
gcc's: the original's callers keep the original's callees, and its
2-byte slots, until a function is replaced.  The C is compiled without
the assembly, its globals kept, and placed at REBUILT, clear of
everything the board decodes; it calls gcc's C, and the assembly
through its names and the bridges.  gcc's helpers come from libgcc.
The generated SCPI trees' records must lie in the link as the parser
reads them: each child list's children right after the list, and every
record but text at an even address.

    DECOMPILED_CFLAGS='...' uv run tools/firmware/rebuild_link.py \\
        third_party/firmware/z3805a-3543b/decompiled/z3805a-3543b.c \\
        third_party/firmware/z3805a-3543b.bin [ELF]

make check-decompiled runs it with its flags.  Needs m68k-linux-gnu gcc
and binutils.  Exits nonzero if the link fails, a piece is not the
image's bytes or a tree's record is misplaced.
"""

import json
import os
import re
import shlex
import struct
import subprocess
import sys
import tempfile
from dataclasses import dataclass

from decompiled_check import RAM, TOOLS, assembly_lines, installer_image

# Where gcc's code is linked: above the chip selects the reset code
# programs, the last of which, CS10, is at 0x500000 (docs/firmware/README.md),
# so that a read of the EEPROM at 0x400000 reaches no code.
REBUILT = 0x600000
# Where gcc's own data is, apart from its code, which the harness keeps
# read-only: libgcc's _fpCCR and its GOT.
REBUILT_DATA = 0x6F0000
# The rebuilt image's sections the harness loads: its code, its data
# and, after the data, the GOT.
GOT = ".got"
LOADED = (".rebuilt", ".rebuilt_data", GOT)
# A piece's section, its routine's name and its address.
PIECE = re.compile(r"^\.text\.(\w+)\.([0-9a-f]+)$")
# An __asm__ block that holds code: a routine's pieces or a bridge.
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


def script(pieces: list[str], image_names: dict[str, int]) -> str:
    """The linker script: the image's copy of each function at its
    address, each piece at its address, the rest at REBUILT."""
    lines = [f"{IMAGE_PREFIX}{name} = 0x{address:x};" for name, address in image_names.items()]
    lines.append("SECTIONS {")
    for piece in pieces:
        address = int(PIECE.match(piece).group(2), 16)
        lines.append(f"  {piece} 0x{address:x} : {{ *({piece}) }}")
    lines.append(
        f"  .rebuilt 0x{REBUILT:x} : {{ *(.text) *(.text.*) *(.rodata) *(.rodata.*) }}"
    )
    lines.append(f"  .rebuilt_data 0x{REBUILT_DATA:x} : {{ *(.data) *(.data.*) *(.bss) *(.bss.*) *(COMMON) }}")
    # libgcc's float routines are PIC: their status word, _fpCCR, is
    # reached through the GOT, an output section of its own as in ld's
    # default script, since ld takes a GOT entry's offset from its start
    lines.append(f"  {GOT} : {{ *(.got.plt) *(.igot.plt) *(.got) *(.igot) }}")
    lines.append("  /DISCARD/ : { *(.comment) *(.note.*) }")
    lines.append("}")
    return "\n".join(lines) + "\n"


def link(c_path: str, cflags: list[str], work: str, elf_path: str, extra: str = "") -> list[Section]:
    """Links the C file's rebuilt image to elf_path, with extra C after
    the file's own (the function check's probes); its sections."""
    c = open(c_path).read()
    base = c_path.removesuffix(".c")
    addresses = {}
    for line in open(base + ".conventions.jsonl"):
        function = json.loads(line)
        addresses[function["name"]] = int(function["address"].split(":")[-1], 16)
    made = set(re.findall(r"^(\w+) = 0x", open(base + ".symbols.ld").read(), re.M))

    c_only, compiled = work + "/c.c", work + "/c.o"
    open(c_only, "w").write(CODE_BLOCK.sub("", c) + extra)
    # the hand-written C it includes is named from the C file's own folder
    run(TOOLS + "gcc", *cflags, "-I", os.path.dirname(os.path.abspath(c_path)), "-c", "-o", compiled, c_only)
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
    libgcc = run(TOOLS + "gcc", "-mcpu=cpu32", "-print-libgcc-file-name").strip()
    linker_script = work + "/rebuilt.ld"
    # every function's image copy is named, the harness's to call
    open(linker_script, "w").write(script(pieces, addresses))
    # routines that share code each carry the shared bytes, so their
    # pieces overlap; main confirms every copy is the image's
    run(
        TOOLS + "ld", "--no-check-sections", "--no-warn-rwx-segments",
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
    rebuilt = next(s for s in linked if s.name == ".rebuilt")
    print(f"linked pieces identical: {identical}, differing: {differing}; gcc's code: {len(rebuilt.data)} bytes")
    return 1 if differing or missing or broken else 0


if __name__ == "__main__":
    sys.exit(main())
