#!/usr/bin/env python3
"""Check the XC3000 viewer's XC3042 pad tables against Xilinx's data sheet.

    python3 tools/fpga/pincheck.py target/fpga/xc3000/html

The viewer (https://github.com/Arisotura/xc3000, read at 8563397) names
the XC3042's 96 I/O blocks PAD1 to PAD96, clockwise from the top left,
and maps each to a package pin.  The data sheet,
third_party/xilinx/xc3000.pdf, lists every pad in its 132-pin PGA
pinout (7-70) in order around the die, so that table fixes which pad
each PADn is; its 84-pin PLCC pinout (7-67) gives each PC84 pin's
function but not its pad.  So this checks:
- every PADn's PG132 pin against the data sheet's pad order;
- each PC84 pin the data sheet names (D0-DIN, A5, TCLKIN and the rest)
  against the function the PG132 table gives the same pad;
- that the PC84 pins the viewer bonds are the data sheet's I/O pins, in
  the same order around the die.
A generic "I/O" PC84 pin between two named ones could be any pad between
them; the data sheet does not say which, so that is not checked.

Needs pdftotext (poppler) and the viewer's clone, which `check`
makes; CI runs it there.
"""

import argparse
import json
import pathlib
import re
import subprocess
import sys

DATA_SHEET = pathlib.Path(__file__).resolve().parents[2] / "third_party/xilinx/xc3000.pdf"
PART = "XC3042"
IOB_COUNT = 96
PGA132_TITLE = "XC3000 Series 132-Pin Ceramic and Plastic PGA Pinouts"
PLCC84_TITLE = "XC3000 Series 68-Pin PLCC, 84-Pin PLCC and PGA Pinouts"
TABLE_END = "Unprogrammed IOBs"
# A data-sheet function with no number or letter suffix of its own.
GENERIC = {"I/O", "I/O*"}


def table_lines(text, title):
    start = text.index(title)
    return text[start:text.index(TABLE_END, start)].splitlines()[1:]


def pga132(text):
    """The data sheet's 132-pin pads in order around the die, as (pin, function)."""
    rows = [re.findall(r"\b([A-P]\d{1,2})\s+(\S+)", line) for line in table_lines(text, PGA132_TITLE)]
    rows = [row for row in rows if len(row) == 4]
    # Read down each of the four column pairs in turn.
    pads = [row[column] for column in range(4) for row in rows]
    # An asterisk marks a pin with no pad on the XC3042A.
    return [(pin, function) for pin, function in pads if not function.endswith("*")]


def plcc84(text):
    """The data sheet's XC3042 84-pin PLCC pins, as {pin: function}."""
    pins = {}
    for line in table_lines(text, PLCC84_TITLE):
        tokens = line.split()
        for i, token in enumerate(tokens[:-1]):
            if re.search(r"[A-Z]", token) and tokens[i + 1].isdigit():
                pins[int(tokens[i + 1])] = token
    return pins


def is_iob(function):
    return "I/O" in function


def parts(function):
    """A function's names, which the two tables order and annotate differently
    ("XTL1(OUT)-BCLKIN-I/O" and "XTL1-I/O-BCLKIN")."""
    return set(re.sub(r"\((IN|OUT)\)", "", function).split("-"))


def viewer_tables(viewer):
    """The viewer's XC3042 package tables, {package: {PADn: pin}}."""
    source = (viewer / "chippackage.js").read_text()
    start = source.index(f'"{PART}"')
    body = source[source.index("{", start):]
    depth = 0
    for end, character in enumerate(body):
        depth += {"{": 1, "}": -1}.get(character, 0)
        if depth == 0:
            break
    return json.loads(body[:end + 1])


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("viewer", type=pathlib.Path, help="the viewer's html directory")
    arguments = parser.parse_args()
    text = subprocess.run(["pdftotext", "-layout", str(DATA_SHEET), "-"],
                          check=True, capture_output=True, text=True).stdout
    tables = viewer_tables(arguments.viewer)
    problems = []

    # The data sheet runs counterclockwise from the top of the left edge,
    # the viewer clockwise from the left of the top edge, so its last pad
    # is the data sheet's first.
    pads = [pad for pad in pga132(text) if is_iob(pad[1])]
    if len(pads) != IOB_COUNT:
        sys.exit(f"pincheck.py: read {len(pads)} I/O pads from the 132-pin table, not {IOB_COUNT}")
    function_of_pad = {}
    for index, (pin, function) in enumerate(pads):
        pad = f"PAD{IOB_COUNT - index}"
        function_of_pad[pad] = function
        if tables["PGA132"][pad] != pin:
            problems.append(f"{pad}: viewer PG132 {tables['PGA132'][pad]}, data sheet {pin} ({function})")

    pc84 = plcc84(text)
    viewer_pc84 = {pad: int(pin[1:]) for pad, pin in tables["PLCC84"].items() if pin.startswith("P")}
    named = 0
    for pad, pin in sorted(viewer_pc84.items(), key=lambda item: int(item[0][3:])):
        function = pc84.get(pin)
        if function is None or not is_iob(function):
            problems.append(f"{pad}: viewer PC84 P{pin}, which the data sheet gives as {function}")
        elif function not in GENERIC:
            named += 1
            if parts(function) != parts(function_of_pad[pad]):
                problems.append(f"{pad}: viewer PC84 P{pin}, {function}; the pad is {function_of_pad[pad]}")
    iob_pins = sorted(pin for pin, function in pc84.items() if is_iob(function))
    if sorted(viewer_pc84.values()) != iob_pins:
        problems.append("the viewer's bonded PC84 pins are not the data sheet's I/O pins")
    # Around the die from PAD1, the pins fall from P11 to P2, wrap to P84
    # and fall on from there.
    order = [viewer_pc84[f"PAD{n}"] for n in range(1, IOB_COUNT + 1) if f"PAD{n}" in viewer_pc84]
    if sum(1 for a, b in zip(order, order[1:]) if b > a) != 1:
        problems.append("the viewer's PC84 pins are not in order around the die")

    print(f"PG132: {IOB_COUNT} pads checked; PC84: {len(viewer_pc84)} bonded pads, "
          f"{named} named pins checked")
    for problem in problems:
        print(problem)
    sys.exit(1 if problems else 0)


if __name__ == "__main__":
    main()
