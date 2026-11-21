"""Assembles flash_routine.S with the m68k binutils the firmware's
checks use (docs/firmware/README.md) and writes, into a folder, the
routine's bytes (flash_routine.bin), its symbols (flash_routine.json:
its labels' offsets and its .equ constants, the one source of the
operations, statuses and flash geometry the host shares with it) and
its listing (flash_routine.lst).

    python3 -I tools/bdm/build_routine.py [FOLDER]

FOLDER is target/bdm by default.  Exits nonzero if the object has a
relocation, which would make the routine depend on where it is loaded,
or lacks a label the host enters by.
"""

import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE = Path(__file__).resolve().parent / "flash_routine.S"
FOLDER = ROOT / "target" / "bdm"
TOOLS = "m68k-linux-gnu-"
NAME = "flash_routine"
# The labels the host enters by: its PC, and every vector's.
ENTRY, EXCEPTION, END = "entry", "exception", "routine_end"
# nm's types: a local label in .text, an absolute symbol (.equ).
LABEL, CONSTANT = "t", "a"


def run(*command: str) -> str:
    return subprocess.run(command, check=True, capture_output=True, text=True).stdout


def build(folder: Path = FOLDER) -> Path:
    """The routine assembled into folder; the path of its JSON."""
    folder.mkdir(parents=True, exist_ok=True)
    obj = folder / f"{NAME}.o"
    run(f"{TOOLS}gcc", "-c", "-mcpu=cpu32", "-o", str(obj), str(SOURCE))
    relocations = [line for line in run(f"{TOOLS}objdump", "-r", str(obj)).splitlines() if line.startswith("0")]
    if relocations:
        raise SystemExit(f"{SOURCE}: not position independent: {relocations}")
    binary = folder / f"{NAME}.bin"
    run(f"{TOOLS}objcopy", "-O", "binary", "-j", ".text", str(obj), str(binary))
    labels: dict[str, int] = {}
    constants: dict[str, int] = {}
    for line in run(f"{TOOLS}nm", str(obj)).splitlines():
        value, kind, name = line.split()
        if kind == LABEL:
            labels[name] = int(value, 16)
        elif kind == CONSTANT:
            constants[name] = int(value, 16)
    missing = {ENTRY, EXCEPTION, END} - labels.keys()
    if missing:
        raise SystemExit(f"{SOURCE}: no {sorted(missing)}")
    size = binary.stat().st_size
    if size != labels[END]:
        raise SystemExit(f"{SOURCE}: {size} bytes but {END} at {labels[END]:#x}")
    (folder / f"{NAME}.lst").write_text(run(f"{TOOLS}objdump", "-d", str(obj)))
    symbols = folder / f"{NAME}.json"
    symbols.write_text(json.dumps({"size": size, "labels": labels, "constants": constants}, indent=1) + "\n")
    return symbols


if __name__ == "__main__":
    print(build(Path(sys.argv[1]) if len(sys.argv) > 1 else FOLDER))
