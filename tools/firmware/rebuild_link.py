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

    DECOMPILED_CFLAGS='...' uv run tools/firmware/rebuild_link.py \\
        third_party/firmware/decompiled/z3805a-3543b.c \\
        third_party/firmware/z3805a-3543b.bin [ELF]

make check-decompiled runs it with its flags.  Needs m68k-linux-gnu gcc
and binutils.  Exits nonzero if the link fails or a piece is not the
image's bytes.
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
# read-only: a bridge's __ghidra_ccr.
REBUILT_DATA = 0x6F0000
# A piece's section, its routine's name and its address.
PIECE = re.compile(r"^\.text\.(\w+)\.([0-9a-f]+)$")
# An __asm__ block that holds code: a routine's pieces or a bridge.
CODE_BLOCK = re.compile(r"^__asm__\(\n.*?\.pushsection.*?^\);$", re.S | re.M)
# The prefix of the image's own copy of a C function.
IMAGE_PREFIX = "__image_"
# ELF32 header and section header layouts, big-endian.
ELF_HEADER = ">16sHHIIIIIHHHHHH"
SECTION_HEADER = ">IIIIIIIIII"
NOBITS = 8


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
    lines.append("  /DISCARD/ : { *(.comment) *(.note.*) }")
    lines.append("}")
    return "\n".join(lines) + "\n"


def link(c_path: str, cflags: list[str], work: str, elf_path: str) -> list[Section]:
    """Links the C file's rebuilt image to elf_path; its sections."""
    c = open(c_path).read()
    base = c_path.removesuffix(".c")
    addresses = {}
    for line in open(base + ".conventions.jsonl"):
        function = json.loads(line)
        addresses[function["name"]] = int(function["address"].split(":")[-1], 16)
    made = set(re.findall(r"^(\w+) = 0x", open(base + ".symbols.ld").read(), re.M))

    c_only, compiled = work + "/c.c", work + "/c.o"
    open(c_only, "w").write(CODE_BLOCK.sub("", c))
    run(TOOLS + "gcc", *cflags, "-c", "-o", compiled, c_only)
    # what the C defines that is not a function, a bridge's __ghidra_ccr
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


def main() -> int:
    if len(sys.argv) not in (3, 4) or "DECOMPILED_CFLAGS" not in os.environ:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    c_path, image_path = sys.argv[1:3]
    image = open(image_path, "rb").read()
    installer = installer_image(image)
    with tempfile.TemporaryDirectory() as work:
        try:
            linked = link(
                c_path, shlex.split(os.environ["DECOMPILED_CFLAGS"]), work,
                sys.argv[3] if len(sys.argv) == 4 else work + "/rebuilt.elf",
            )
        except subprocess.CalledProcessError as error:
            print(error.stderr, file=sys.stderr)
            return 1
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
    return 1 if differing else 0


if __name__ == "__main__":
    sys.exit(main())
