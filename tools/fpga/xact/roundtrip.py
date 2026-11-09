#!/usr/bin/env python3
"""Decode each test design's bitstream and check it against its routed LCA.

    python3 -I tools/fpga/xact/roundtrip.py VIEWER_HTML_DIR [NAME ...]

For every designs/NAME.bit (or the NAMEs given), runs netdump.js with the
XC3000 viewer in VIEWER_HTML_DIR and lcacheck.py against
designs/NAME.routed.lca, the design as XACT routed and built it.  Prints
each design's disagreements and a summary; exits 1 on any.  No XACT is
needed: build.sh made the files, and they are committed (README.md).
"""

import argparse
import os
import pathlib
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
DESIGNS = HERE / "designs"
NETDUMP = HERE.parent / "netdump.js"
LCACHECK = HERE.parent / "lcacheck.py"


def check(viewer, name, scratch):
    """Run one design's round trip; True when it agrees."""
    decoded = scratch / f"{name}.json"
    with open(decoded, "w") as output:
        dump = subprocess.run(["node", str(NETDUMP), str(viewer), str(DESIGNS / f"{name}.bit")],
                              stdout=output, stderr=subprocess.PIPE, text=True, check=False)
    if dump.returncode:
        print(f"{name}: netdump.js failed:\n{dump.stderr}")
        return False
    routed = os.path.relpath(DESIGNS / f"{name}.routed.lca")
    result = subprocess.run([sys.executable, "-I", str(LCACHECK), routed, str(decoded)],
                            capture_output=True, text=True, check=False)
    sys.stdout.write(result.stdout)
    return result.returncode == 0


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("viewer", type=pathlib.Path, help="the Arisotura/xc3000 clone's html directory")
    parser.add_argument("names", nargs="*", help="designs to check (default: all)")
    arguments = parser.parse_args()
    names = arguments.names or sorted(path.stem for path in DESIGNS.glob("*.bit"))
    with tempfile.TemporaryDirectory() as scratch:
        failed = [name for name in names if not check(arguments.viewer, name, pathlib.Path(scratch))]
    print(f"roundtrip.py: {len(names) - len(failed)} of {len(names)} designs agree with XACT"
          + (f"; failed: {', '.join(failed)}" if failed else ""))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
