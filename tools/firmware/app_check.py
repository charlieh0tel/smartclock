# /// script
# requires-python = ">=3.11"
# dependencies = ["unicorn>=2.1"]
# ///
"""Confirms application routines named from the decompiled firmware by
running them, as libc_check.py does the C library's: each is called in a
Unicorn 68k emulator on known inputs and its result compared with what
its name says.

    uv run tools/firmware/app_check.py third_party/firmware/z3805a-3543b.bin

So far the SCPI parser's numeric converters, its real formatter and the
number engine under them (docs/scpi/README.md, "Parameters and
responses").  The addresses are
the Z3805A 3543B's.  Exits nonzero if any check fails.
"""

import struct
import sys

from unicorn.m68k_const import (
    UC_M68K_REG_A0,
    UC_M68K_REG_A1,
    UC_M68K_REG_A7,
    UC_M68K_REG_D0,
    UC_M68K_REG_D1,
    UC_M68K_REG_D2,
    UC_M68K_REG_D3,
    UC_M68K_REG_D7,
)

from libc_check import (
    RETURN,
    STACK_TOP,
    STEP_LIMIT,
    Machine,
    double_words,
    from_double_words,
    read_double,
    signed,
)

# The routines, by name, at their Z3805A 3543B addresses.
ADDRESSES = {
    "scpi_number_to_short": 0x2609A,
    "scpi_number_to_unsigned_short": 0x26140,
    "scpi_number_to_long": 0x261E4,
    "scpi_number_to_unsigned_long": 0x2628A,
    "scpi_number_to_double": 0x254BA,
    "scpi_real_to_text": 0x2553A,
    "scpi_double_to_decimal": 0x24E18,
    "scpi_scale_by_power_of_ten": 0x251E4,
    "scpi_double_multiply": 0x252BC,
}

# The registers the number engine's routines take their arguments in.
REGISTERS = {
    "d0": UC_M68K_REG_D0,
    "d1": UC_M68K_REG_D1,
    "d2": UC_M68K_REG_D2,
    "d3": UC_M68K_REG_D3,
    "d7": UC_M68K_REG_D7,
    "a0": UC_M68K_REG_A0,
    "a1": UC_M68K_REG_A1,
}

# The table of powers of ten scpi_bcd_to_double hands
# scpi_scale_by_power_of_ten in A1.
POWERS_OF_TEN = 0x256AE
# The power of ten scpi_scale_by_power_of_ten takes less this many: the
# 16 BCD digits are an integer.
BCD_DIGITS = 16
# Where a decimal record's sign word, digits and exponent are.
DECIMAL_DIGITS = 2
DECIMAL_EXPONENT = 18

# A parsed number: a sign word, nonzero for negative, then its digits'
# values, most significant first, then the exponent of 10 that makes
# 0.d1d2... the number's value with DIGITS added.
DIGITS = 16
TOKEN_SIZE = 0x28

# The converters' result for a number out of their type's range.
OUT_OF_RANGE = -11

# scpi_real_to_text's resolution for "every digit asked for".
FULL_RESOLUTION = -308
# Its third argument, which every caller passes.
REAL_TEXT_MODE = 3


def token(m: Machine, value: int) -> int:
    """A parsed-number record for value, in a buffer."""
    text = str(abs(value))
    digits = bytes(int(c) for c in text).ljust(DIGITS, b"\0")
    raw = struct.pack(">H", value < 0) + digits + struct.pack(">h", len(text) - DIGITS)
    return m.buffer(raw.ljust(TOKEN_SIZE, b"\0"))


def call_packed(m: Machine, name: str, arguments: bytes) -> int:
    """Calls name with arguments laid on the stack as given: these routines
    take words and bytes in 2-byte slots, which Machine.call does not."""
    sp = STACK_TOP - len(arguments) - 4
    m.uc.mem_write(sp, struct.pack(">I", RETURN) + arguments)
    m.uc.reg_write(UC_M68K_REG_A7, sp)
    m.uc.emu_start(ADDRESSES[name], RETURN, count=STEP_LIMIT)
    return signed(m.uc.reg_read(UC_M68K_REG_D0))


def call_registers(m: Machine, name: str, **registers: int) -> tuple[int, int]:
    """Calls name with its arguments in the registers given; returns D0
    and D1."""
    sp = STACK_TOP - 4
    m.uc.mem_write(sp, struct.pack(">I", RETURN))
    m.uc.reg_write(UC_M68K_REG_A7, sp)
    for register, value in registers.items():
        m.uc.reg_write(REGISTERS[register], value & 0xFFFFFFFF)
    m.uc.emu_start(ADDRESSES[name], RETURN, count=STEP_LIMIT)
    return m.uc.reg_read(UC_M68K_REG_D0), m.uc.reg_read(UC_M68K_REG_D1)


def checks(m: Machine):
    """Yields (routine, description, passed) for each check."""
    # Each converter's result width and range, and whether it keeps the sign
    # or takes the magnitude.
    for name, width, low, high, signed_result in (
        ("scpi_number_to_short", 2, -0x8000, 0x7FFF, True),
        ("scpi_number_to_unsigned_short", 2, 0, 0xFFFF, False),
        ("scpi_number_to_long", 4, -0x80000000, 0x7FFFFFFF, True),
        ("scpi_number_to_unsigned_long", 4, 0, 0xFFFFFFFF, False),
    ):
        for value in (1234, -1234, 40000, -40000, 70000, 3_000_000_000):
            out = m.buffer(b"\0" * 8)
            status = call_packed(m, name, struct.pack(">IIh", token(m, value), out, 0))
            wanted = value if signed_result else abs(value)
            if not low <= wanted <= high:
                yield name, f"{value}", status == OUT_OF_RANGE
                continue
            got = int.from_bytes(m.read(out, width), "big", signed=signed_result)
            yield name, f"{value}", status == 0 and got == wanted

    for value in (1234, -5):
        out = m.buffer(b"\0" * 8)
        status = call_packed(m, "scpi_number_to_double", struct.pack(">IIh", token(m, value), out, 0))
        yield "scpi_number_to_double", f"{value}", status == 0 and read_double(m, out) == value

    # Significant digits, and the resolution: the power of ten of the last
    # digit kept.
    for value, digits, resolution, wanted in (
        (123.456, 15, FULL_RESOLUTION, b"+1.23456000000000E+002"),
        (123.456, 6, FULL_RESOLUTION, b"+1.23456E+002"),
        (0.00125, 6, FULL_RESOLUTION, b"+1.25000E-003"),
        (123.456, 6, 0, b"+1.23E+002"),
        (-1e-9, 6, -9, b"-0.1E-008"),
    ):
        out = m.buffer()
        arguments = struct.pack(">dIhhhBx", value, out, digits, REAL_TEXT_MODE, resolution, 1)
        status = call_packed(m, "scpi_real_to_text", arguments)
        got = m.read(out, len(wanted) + 1)
        yield "scpi_real_to_text", f"{value} {digits} {resolution}", status == 0 and got == wanted + b"\0"

    # The number engine under them, which takes its arguments in registers.
    for a, b in ((1.5, 2.0), (123.456, 1e10), (-2.5, 4.0)):
        high, low = double_words(a)
        b_high, b_low = double_words(b)
        got = from_double_words(*call_registers(m, "scpi_double_multiply", d0=high, d1=low, d2=b_high, d3=b_low))
        yield "scpi_double_multiply", f"{a}*{b}", got == a * b

    for value, power in ((1.5, 3), (2.0, -100), (1234.0, 300), (7.0, -290)):
        record = m.buffer(struct.pack(">H8xh", 0, power))
        high, low = double_words(value)
        got = from_double_words(
            *call_registers(m, "scpi_scale_by_power_of_ten", d0=high, d1=low, a0=record + 10, a1=POWERS_OF_TEN)
        )
        wanted = value * 10.0 ** (power - BCD_DIGITS)
        yield "scpi_scale_by_power_of_ten", f"{value} {power}", abs(got - wanted) <= abs(wanted) * 1e-15

    # D7 digits and two more, for rounding.
    for value, digits, wanted, exponent in (
        (123.456, 6, b"12345600", 3),
        (0.00125, 6, b"12500000", -2),
        (-9.87654321e20, 9, b"98765432", 21),
    ):
        out = m.buffer()
        high, low = double_words(value)
        call_registers(m, "scpi_double_to_decimal", d0=high, d1=low, a0=out, d7=digits)
        raw = m.read(out, DECIMAL_EXPONENT + 2)
        sign = struct.unpack(">H", raw[:DECIMAL_DIGITS])[0]
        got_exponent = struct.unpack(">h", raw[DECIMAL_EXPONENT:])[0]
        passed = sign == (value < 0) and raw[DECIMAL_DIGITS:].startswith(wanted) and got_exponent == exponent
        yield "scpi_double_to_decimal", f"{value} {digits}", passed


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    with open(sys.argv[1], "rb") as image:
        machine = Machine(image.read())
    failed = 0
    for routine, description, passed in checks(machine):
        print(f"{'ok' if passed else 'FAIL'} {routine} {description}")
        failed += not passed
    print(f"{failed} failed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
