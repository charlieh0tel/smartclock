# /// script
# requires-python = ">=3.12"
# dependencies = ["mc683xx==0.8.0"]
# ///
"""Checks the rounding of the 58503A 3704's single-precision add,
subtract, multiply and divide (docs/firmware/softfloat.md) by calling
them through the harness (libc_check.Machine) on chosen ties and on
random operands, against the exact result rounded to nearest,
ties to even:

    uv run tools/firmware/softfloat_check.py

The add, subtract and multiply must round to nearest every time; the
divide must round to nearest or fall 1 ulp toward zero, and 1/3 must
give 0x3eaaaaaa.  Exits nonzero if one does not.

With --sum, runs the aging fit's sampler instead (docs/firmware/loop.md,
"The aging fit"): for each EFC level, 2700 additions of it into a zero
sum by the image's add in place, the sum divided by 2700 by its divide,
and prints the level and the mean's error as TSV, then the error's
range:

    uv run tools/firmware/softfloat_check.py --sum [LEVEL]

LEVEL, in EFC counts, is where the levels start, by default 700000;
they cover two periods of 128 counts.
"""

import pathlib
import random
import struct
import sys
from fractions import Fraction

import libc_check
from libc_check import Machine, float_bits, from_float_bits

ROOT = pathlib.Path(__file__).resolve().parents[2]
IMAGE = ROOT / "third_party/firmware/images/58503a-3704.bin"
# The 3704's routines (softfloat.md's table).
ROUTINES = {
    "float_subtract": 0x6129A,
    "float_add": 0x612BE,
    "float_multiply": 0x62524,
    "float_divide": 0x619EA,
    "float_add_in_place": 0x6128E,
    "unsigned_to_float": 0x62412,
}
MANTISSA_BITS = 24
# Random operands per operation, and the seed that picks them.
RANDOM_CASES = 2000
SEED = 1
# The sampler's additions per entry, and the levels --sum runs: two
# periods of the sum's spacing past 2^30, in steps of half a count.
SAMPLES = 2700
SUM_SPACING = 128
SUM_PERIODS = 2
SUM_STEP = 0.5
DEFAULT_LEVEL = 700000.0
# 1/3, which the divide gives 1 ulp low.
THIRD = 0x3EAAAAAA


def nearest_even(value: Fraction) -> float:
    """value rounded to a single-precision float, to nearest, ties to
    even (normal range only)."""
    if value == 0:
        return 0.0
    sign = -1 if value < 0 else 1
    magnitude = abs(value)
    exponent = 0
    while magnitude >= 2**MANTISSA_BITS:
        magnitude /= 2
        exponent += 1
    while magnitude < 2 ** (MANTISSA_BITS - 1):
        magnitude *= 2
        exponent -= 1
    whole = int(magnitude)
    rest = magnitude - whole
    if rest > Fraction(1, 2) or (rest == Fraction(1, 2) and whole & 1):
        whole += 1
    return sign * float(whole * Fraction(2) ** exponent)


def toward_zero(value: float) -> float:
    """The float 1 ulp nearer zero than value."""
    return from_float_bits(float_bits(value) - 1)


def single(value: float) -> float:
    return from_float_bits(float_bits(value))


def random_operand(rng: random.Random, low: float, high: float) -> float:
    return single(rng.uniform(-1, 1) * 10 ** rng.uniform(low, high))


def run(m: Machine, name: str, x: float, y: float) -> float:
    return from_float_bits(m.call(name, d0=float_bits(x), d1=float_bits(y))[0])


def check(m: Machine) -> int:
    rng = random.Random(SEED)
    ulp = 2.0 ** -(MANTISSA_BITS - 1)
    # Exact ties, even and odd, and just either side of one; then a tie
    # at the spacing of the aging fit's sum.
    ties = [
        (1.0, ulp / 2),
        (1.0 + ulp, ulp / 2),
        (1.0, ulp / 2 * (1 + 2.0**-20)),
        (1.0, ulp / 2 * (1 - 2.0**-20)),
        (2.0**30, SUM_SPACING / 2),
        (2.0**30 + SUM_SPACING, SUM_SPACING / 2),
    ]
    sums = ties + [(random_operand(rng, -3, 9), random_operand(rng, -3, 9)) for _ in range(RANDOM_CASES)]
    # Exact ties, rounding up to even and down to even.
    products = [(1.0 + ulp, 1.5), (1.0 + 3 * ulp, 1.5)] + [
        (random_operand(rng, -3, 6), random_operand(rng, -3, 6)) for _ in range(RANDOM_CASES)
    ]
    quotients = [(random_operand(rng, -3, 6), abs(random_operand(rng, -3, 6))) for _ in range(RANDOM_CASES)]
    failures = 0
    for name, operation, cases in (
        ("float_add", lambda x, y: x + y, sums),
        ("float_subtract", lambda x, y: x - y, sums),
        ("float_multiply", lambda x, y: x * y, products),
    ):
        wrong = [(x, y) for x, y in cases if run(m, name, x, y) != nearest_even(operation(Fraction(x), Fraction(y)))]
        failures += len(wrong)
        print(f"{name}: {len(cases)} cases, {len(wrong)} not to nearest even")
        for x, y in wrong[:5]:
            print(f"  {x!r} {y!r} -> {run(m, name, x, y)!r}")
    low = other = 0
    for x, y in quotients:
        got = run(m, "float_divide", x, y)
        nearest = nearest_even(Fraction(x) / Fraction(y))
        if got != nearest:
            if got == toward_zero(nearest):
                low += 1
            else:
                other += 1
                print(f"  float_divide {x!r} {y!r} -> {got!r}, nearest {nearest!r}")
    failures += other
    print(f"float_divide: {len(quotients)} cases, {low} 1 ulp toward zero, {other} otherwise off")
    third = m.call("float_divide", d0=float_bits(1.0), d1=float_bits(3.0))[0]
    print(f"float_divide: 1/3 -> {third:#010x}")
    failures += third != THIRD
    return failures


def sum_errors(m: Machine, start: float) -> int:
    at = m.buffer()
    count = m.call("unsigned_to_float", d0=SAMPLES)[0]
    steps = int(SUM_PERIODS * SUM_SPACING / SUM_STEP)
    errors = []
    print("level\terror")
    for step in range(steps):
        level = single(start + step * SUM_STEP)
        m.uc.mem_write(at, struct.pack(">I", 0))
        for _ in range(SAMPLES):
            m.call("float_add_in_place", d1=float_bits(level), a0=at)
        total = struct.unpack(">I", m.read(at, 4))[0]
        mean = from_float_bits(m.call("float_divide", d0=total, d1=count)[0])
        errors.append(mean - level)
        print(f"{level:.1f}\t{errors[-1]:+.3f}")
    print(f"error from {min(errors):+.3f} to {max(errors):+.3f} counts")
    return 0


def main() -> int:
    libc_check.ADDRESSES.update(ROUTINES)
    with open(IMAGE, "rb") as image:
        m = Machine(image.read())
    if sys.argv[1:2] == ["--sum"]:
        start = float(sys.argv[2]) if len(sys.argv) > 2 else DEFAULT_LEVEL
        return sum_errors(m, start)
    if len(sys.argv) != 1:
        print(__doc__, file=sys.stderr)
        return 2
    failures = check(m)
    print("PASS" if failures == 0 else f"FAIL: {failures}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
