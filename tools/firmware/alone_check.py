# /// script
# requires-python = ">=3.11"
# ///
"""Compiles each hand-written file an image's export includes alone,
after the export's declarations and the hand-written headers, so that a
file naming what only another file defines -- its static, its constant,
a helper -- fails.

The export is one translation unit: its declarations, every function's
reading, the hand-written headers, then every hand-written .c file,
rewritten/common/'s before the image's own.  In it a file can name what
a file included before it defines, and the build would never say so;
the rule is that it may not (docs/firmware/rewrite.md, "Shared C").
Here each .c is compiled on its own after the declarations (everything
before the first function, link_check.py's) and every header the export
includes, which are compiled once into a precompiled header, so that
each file costs only its own parse.

    DECOMPILED_CFLAGS='...' python3 tools/firmware/alone_check.py \\
        third_party/firmware/decompiled/z3805a-3543b/z3805a-3543b.c

make check-alone runs it for every image with an export.  Needs
m68k-linux-gnu gcc.  Prints each error and a summary line; exits
nonzero if any file fails to compile alone.
"""

import concurrent.futures
import os
import re
import shlex
import subprocess
import sys
import tempfile

from decompiled_check import TOOLS
from link_check import FUNCTION_MARKER

# A hand-written file the export includes, header or .c, its path from
# the export's folder.
INCLUDED = re.compile(r'^#include "(\.\./\.\./rewritten/[^"]+\.([ch]))"$', re.M)
# What gcc must take for an error here beyond what is one already: a
# function no declaration names, which C before C99 took as implicit.
STRICT = ["-Werror=implicit-function-declaration"]
# The preamble's name in the work folder, beside which gcc finds its
# precompiled .gch.
PREAMBLE = "preamble.h"


def preamble(c: str) -> tuple[str, list[str]]:
    """The export's declarations and every hand-written header it
    includes; and the .c files, in order, their paths from the export's
    folder."""
    first = FUNCTION_MARKER.search(c)
    if first is None:
        raise SystemExit("the export has no function")
    headers, sources = [], []
    for found in INCLUDED.finditer(c, first.start()):
        (headers if found.group(2) == "h" else sources).append(found.group(1))
    return c[: first.start()] + "".join(f'#include "{path}"\n' for path in headers), sources


def compile_alone(cflags: list[str], work: str, folder: str, source: str) -> str:
    """gcc's errors compiling one file after the preamble; empty if
    none.  Each file's unit is named for its place among the export's,
    which two files of one name, common/'s and the image's, share."""
    unit = os.path.join(work, source.replace("/", "_").lstrip("._") + ".c")
    with open(unit, "w") as f:
        f.write(f'#include "{PREAMBLE}"\n#include "{source}"\n')
    done = subprocess.run([TOOLS + "gcc", *cflags, *STRICT, "-I", work, "-I", folder, "-fsyntax-only", unit],
                          capture_output=True, text=True)
    errors = [line for line in done.stderr.splitlines() if "error" in line]
    if done.returncode and not errors:
        errors = done.stderr.splitlines()[-1:] or [f"gcc exited {done.returncode}"]
    return "\n".join(errors)


def main() -> int:
    if len(sys.argv) != 2 or "DECOMPILED_CFLAGS" not in os.environ:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    c_path = sys.argv[1]
    image = os.path.basename(c_path).removesuffix(".c")
    cflags = shlex.split(os.environ["DECOMPILED_CFLAGS"])
    folder = os.path.dirname(os.path.abspath(c_path))
    head, sources = preamble(open(c_path).read())
    with tempfile.TemporaryDirectory() as work:
        header = os.path.join(work, PREAMBLE)
        with open(header, "w") as f:
            f.write(head)
        subprocess.run([TOOLS + "gcc", *cflags, "-I", folder, "-x", "c-header", "-o", header + ".gch", header],
                       check=True)
        with concurrent.futures.ThreadPoolExecutor(os.cpu_count()) as pool:
            results = list(pool.map(lambda source: compile_alone(cflags, work, folder, source), sources))
    failed = [(source, errors) for source, errors in zip(sources, results) if errors]
    for source, errors in failed:
        print(f"{source.removeprefix('../../')}:\n{errors}")
    print(f"{image}: {len(sources)} hand-written files, {len(failed)} not compiling alone")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
