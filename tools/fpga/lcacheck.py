#!/usr/bin/env python3
"""Check netdump.js's decode of a bitstream against the XACT design it was built from.

    python3 -I tools/fpga/lcacheck.py DESIGN.LCA DESIGN.json

DESIGN.LCA is the routed design XACT's APR wrote and MakeBits built
DESIGN.bit from; DESIGN.json is netdump.js's decode of DESIGN.bit.  The
LCA is the ground truth.  Checked:
- the part: the LCA's, against the family the decoder recognized;
- every logic block in either: its F and G equations as Boolean
  functions (truth tables over both sides' variables), its X and Y
  output selections and, where a flip-flop is used, its data source,
  clock enable, direct reset and clock inversion;
- every I/O block in either: direct and registered input, latch or
  flip-flop and the input clock's inversion, output, registered,
  inverted, fast, 3-state and its inversion, the output clock's
  inversion, and the pull-up;
- every net in either, by the pins it connects, both ways: each LCA
  net must be one decoded net, and each decoded net one LCA net;
- that no decoded pin is driven by two nets.

A block in only one of the two is a disagreement, so a block the LCA
configures without connecting must be decoded too.  Prints one line per
disagreement and a summary; exits 1 on any.  tools/fpga/xact/README.md
says where the designs come from.
"""

import argparse
import itertools
import json
import re
import sys

CLB_NAME = re.compile(r"^[A-Z][A-Z]$")
# An I/O block by package pin ("P11"), unbonded pad ("U70") or pad ("PAD7").
IOB_NAME = re.compile(r"^(P|U|PAD)\d+$")


# A Boolean expression from either side: ~ (not), @ (xor), * (and),
# + (or), parentheses, variable names, 0 and 1.  Binding, tightest
# first: ~, @, *, +.  XACT parenthesizes its @ terms, and the decoder
# writes sums of products, so the @/* order is never exercised.
def parse(text):
    tokens = re.findall(r"[A-Za-z_][A-Za-z0-9_]*|[01]|[~@*+()]", text)
    position = 0

    def peek():
        return tokens[position] if position < len(tokens) else None

    def take():
        nonlocal position
        position += 1
        return tokens[position - 1]

    def binary(operator, operand, combine):
        def rule():
            node = operand()
            while peek() == operator:
                take()
                node = (combine, node, operand())
            return node

        return rule

    def unary():
        token = take()
        if token == "~":
            return ("not", unary())
        if token == "(":
            node = sum_()
            if take() != ")":
                raise ValueError(f"unbalanced: {text}")
            return node
        if token in ("0", "1"):
            return ("const", token == "1")
        return ("var", token)

    xor = binary("@", unary, "xor")
    product = binary("*", xor, "and")
    sum_ = binary("+", product, "or")
    node = sum_()
    if position != len(tokens):
        raise ValueError(f"trailing tokens: {text}")
    return node


def variables(node):
    if node[0] == "var":
        return {node[1]}
    if node[0] == "const":
        return set()
    return set().union(*(variables(child) for child in node[1:]))


def evaluate(node, values):
    kind = node[0]
    if kind == "var":
        return values[node[1]]
    if kind == "const":
        return node[1]
    if kind == "not":
        return not evaluate(node[1], values)
    left, right = evaluate(node[1], values), evaluate(node[2], values)
    return {"and": left and right, "or": left or right, "xor": left != right}[kind]


def same_function(left, right):
    """None if equal, else a counterexample assignment."""
    names = sorted(variables(left) | variables(right))
    for bits in itertools.product((False, True), repeat=len(names)):
        values = dict(zip(names, bits))
        if evaluate(left, values) != evaluate(right, values):
            return {name: int(bit) for name, bit in values.items()}
    return None


def read_lca(path):
    """The part, the blocks (name -> {base, config, equations}) and the
    nets (name -> pins) of an LCA file."""
    part, blocks, nets, block = None, {}, {}, None
    with open(path, encoding="latin-1") as source:
        for line in source:
            words = line.split()
            if not words:
                continue
            keyword = words[0]
            if keyword == "Design":
                part = words[1].upper()
            elif keyword == "Editblk":
                block = blocks.setdefault(words[1].upper(), {"base": None, "config": {}, "equations": {}})
            elif keyword == "Endblk":
                block = None
            elif keyword == "Base" and block is not None:
                block["base"] = words[1]
            elif keyword == "Config" and block is not None:
                for word in words[1:]:
                    key, _, value = word.partition(":")
                    block["config"][key] = value.split(":") if value else []
            elif keyword == "Equate" and block is not None:
                name, _, expression = line.split(None, 1)[1].partition("=")
                block["equations"][name.strip()] = expression.strip()
            elif keyword in ("Addnet", "Addpin"):
                nets.setdefault(words[1], []).extend(word.upper() for word in words[2:])
    return part, blocks, nets


class Checker:
    """One LCA against one decode, collecting disagreements."""

    def __init__(self, lca_path, json_path):
        self.part, self.blocks, self.lca_nets = read_lca(lca_path)
        with open(json_path) as source:
            self.design = json.load(source)
        self.problems = []
        self.checked = {"clb": 0, "iob": 0, "equation": 0, "net": 0}
        # Package pin or unbonded pad -> the decoder's pad.
        self.pad_of_pin = {pin: pad for pad, pin in self.design["padPins"].items()}
        # Every pin on an LCA net, in the decoder's spelling.
        self.lca_pins = {self.decoder_pin(pin) for pins in self.lca_nets.values() for pin in pins}

    def problem(self, text):
        self.problems.append(text)

    def pad(self, name):
        return name if name.startswith("PAD") else self.pad_of_pin.get(name)

    def decoder_pin(self, pin):
        """An LCA pin name in the decoder's spelling."""
        block, _, port = pin.rpartition(".")
        if IOB_NAME.match(block) and self.pad(block):
            return f"{self.pad(block)}.{port}"
        return pin

    def check_part(self):
        # "3042PC84" or "3042APC84"; a bitstream cannot tell the XC3042A
        # from the XC3142A, nor either from the XC3042 unless it uses
        # what only the A parts have (netdump.js's part names).
        match = re.match(r"(\d{4})(A?)", self.part or "")
        decoded = self.design["part"]
        if not match or f"XC{match.group(1)}" not in decoded.replace("XC31", "XC30"):
            self.problem(f"part: LCA {self.part}, decoded {decoded}")
        elif decoded.endswith("A") and not match.group(2):
            self.problem(f"part: LCA {self.part} is not an A part, decoded {decoded}")

    def check_clb(self, name, block, decoded):
        config = block["config"]
        if block["base"] != decoded["mode"]:
            self.problem(f"{name}: base {block['base']} decoded as mode {decoded['mode']}")
        for function, expression in block["equations"].items():
            self.checked["equation"] += 1
            theirs = decoded["equations"].get(function)
            if theirs is None:
                self.problem(f"{name}.{function}: no decoded equation")
                continue
            counter = same_function(parse(expression), parse(theirs))
            if counter is not None:
                self.problem(f"{name}.{function}: LCA {expression!r} vs decoded {theirs!r}, differ at {counter}")
        uses_flip_flop = False
        for output in ("X", "Y"):
            want = (config.get(output) or [None])[0]
            uses_flip_flop |= want in ("QX", "QY")
            if want and decoded["outputs"].get(output) != want:
                self.problem(f"{name}.{output}: LCA {want} vs decoded {decoded['outputs'].get(output)}")
        if uses_flip_flop:
            for data, output in (("DX", "X"), ("DY", "Y")):
                want = (config.get(data) or [None])[0]
                if want and decoded["data"].get(output) != want:
                    self.problem(f"{name}.{data}: LCA {want} vs decoded {decoded['data'].get(output)}")
            pairs = (
                ("ENCLK", "EC" in config.get("ENCLK", []), decoded["enableClock"]),
                ("RSTDIR", "RD" in config.get("RSTDIR", []), decoded["directReset"]),
                ("CLK:NOT", "NOT" in config.get("CLK", []), decoded["clockInvert"]),
            )
            for label, want, got in pairs:
                if bool(want) != bool(got):
                    self.problem(f"{name}.{label}: LCA {want} vs decoded {got}")

    def check_iob(self, name, block, decoded):
        config = block["config"]
        input_, output, tri = config.get("IN", []), config.get("OUT", []), config.get("TRI", [])
        registered_in = "IQ" in input_
        registered_out = "OQ" in output
        # A direct input is a route, not a setting: with no net on I it
        # leaves nothing in the bitstream but the pull-up off.
        pairs = [("IN direct", "I" in input_, decoded["input"]["direct"])] if f"{self.pad(name)}.I" in self.lca_pins else []
        pairs += [
            ("IN registered", registered_in, decoded["input"]["registered"]),
            ("OUT enabled", bool(output), decoded["output"]["enabled"]),
            # MakeBits pulls up every pad it is not told to use (DSREF2,
            # 7-18), an IOB with an empty configuration among them.
            ("IN pull-up", "PULLUP" in input_ or not (input_ or output), decoded["pullup"]),
        ]
        if registered_in:
            latch = "LATCH" in input_
            # The decoder gives the clock line's polarity, inverted when its
            # flip-flops take the falling edge (data sheet, Figure 4).  XACT
            # names a latch's IKNOT the other way about from a flip-flop's
            # on the same line (DSREF3, 4-70: "The latch-clock polarity must
            # be opposite of the flip-flop clock polarity").
            pairs += [
                ("IN latch", latch, decoded["input"]["storage"] == "latch"),
                ("IN IKNOT", ("IKNOT" in input_) != latch, decoded["input"]["clockInvert"]),
            ]
        if output:
            pairs += [
                ("OUT registered", registered_out, decoded["output"]["registered"]),
                ("OUT invert", "NOT" in output, decoded["output"]["invert"]),
                ("OUT fast", "FAST" in output, decoded["output"]["fast"]),
                ("TRI", "T" in tri, decoded["output"]["threeState"]),
                ("TRI invert", "NOT" in tri, decoded["output"]["threeStateInvert"]),
            ]
        if registered_out:
            pairs += [("OUT OKNOT", "OKNOT" in output, decoded["output"]["clockInvert"])]
        for label, want, got in pairs:
            if bool(want) != bool(got):
                self.problem(f"{name} ({self.pad(name)}) {label}: LCA {want} vs decoded {got}")

    def check_blocks(self):
        clbs, iobs = self.design["clbs"], self.design["iobs"]
        seen_clbs, seen_iobs = set(), set()
        for name, block in sorted(self.blocks.items()):
            if CLB_NAME.match(name):
                self.checked["clb"] += 1
                seen_clbs.add(name)
                if name not in clbs:
                    self.problem(f"{name}: in the LCA, not decoded")
                    continue
                self.check_clb(name, block, clbs[name])
            elif IOB_NAME.match(name):
                self.checked["iob"] += 1
                pad = self.pad(name)
                seen_iobs.add(pad)
                if pad not in iobs:
                    if not self.blank_iob(block):
                        self.problem(f"{name} ({pad}): in the LCA, not decoded")
                    continue
                self.check_iob(name, block, iobs[pad])
            else:
                self.problem(f"{name}: block kind not compared ({block['base']})")
        for name in sorted(set(clbs) - seen_clbs):
            self.problem(f"{name}: decoded, not in the LCA")
        for pad in sorted(set(iobs) - seen_iobs):
            if not self.clock_pad_only(iobs[pad]):
                self.problem(f"{pad} ({iobs[pad]['pin']}): decoded, not in the LCA")

    @staticmethod
    def blank_iob(block):
        """An IOB the LCA configures as MakeBits leaves an unused one: an
        input with its pull-up and nothing else, which no bit tells from a
        pad the design does not use (DSREF2, 7-18)."""
        config = block["config"]
        return set(config.get("IN", [])) <= {"PULLUP"} and not config.get("OUT") and not config.get("TRI")

    def clock_pad_only(self, iob):
        """An unconfigured pad used only by its dedicated clock input
        (TCLKIN, BCLKIN), which an LCA may leave without a block."""
        clock = iob["dedicatedClock"]
        idle = not (iob["input"]["direct"] or iob["input"]["registered"] or iob["output"]["enabled"])
        return bool(clock) and f"{clock}.I" in self.lca_pins and idle and iob["pullup"]

    def check_nets(self):
        decoded = {}
        for net in self.design["nets"]:
            decoded.setdefault(frozenset(net["sources"] + net["destinations"]), net["name"])
        by_pin = {pin: pins for pins in decoded for pin in pins}
        matched = set()
        for name, pins in sorted(self.lca_nets.items()):
            self.checked["net"] += 1
            want = frozenset(self.decoder_pin(pin) for pin in pins)
            if want in decoded:
                matched.add(want)
                continue
            got = set().union(*(by_pin.get(pin, frozenset()) for pin in want))
            self.problem(f"net {name}: missing {sorted(want - got)}, extra {sorted(got - want)}")
            matched.update(by_pin[pin] for pin in want if pin in by_pin)
        for pins, name in sorted(decoded.items(), key=lambda item: item[1]):
            if pins not in matched:
                self.problem(f"decoded net {name} {sorted(pins)}: in no LCA net")
        drivers = {}
        for net in self.design["nets"]:
            for pin in net["destinations"]:
                drivers.setdefault(pin, []).append(net["name"])
        for pin, sources in sorted(drivers.items()):
            if len(sources) > 1:
                self.problem(f"decoded pin {pin} driven by {len(sources)} nets: {', '.join(sorted(sources))}")

    def run(self):
        self.check_part()
        self.check_blocks()
        self.check_nets()
        return self.problems


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("lca")
    parser.add_argument("json")
    arguments = parser.parse_args()
    checker = Checker(arguments.lca, arguments.json)
    problems = checker.run()
    for text in problems:
        print(text)
    summary = ", ".join(f"{count} {kind}s" for kind, count in checker.checked.items())
    print(f"{arguments.lca}: part {checker.design['part']}; {summary}; {len(problems)} disagreements")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
