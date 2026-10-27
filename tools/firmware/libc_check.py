# /// script
# requires-python = ">=3.11"
# dependencies = ["unicorn==2.1.4"]
# ///
"""Confirms the C library routines named in docs/firmware/libc.md by running
them: each is called in a Unicorn 68k emulator on known inputs and its
result compared with what the C routine of that name gives.

    uv run tools/firmware/libc_check.py third_party/firmware/images/z3805a-3543b.bin

Only routines that touch nothing but their arguments and RAM are run.  The
addresses are the Z3805A 3543B's.  Exits nonzero if any check fails.
"""

import math
import struct
import sys

from unicorn import UC_ARCH_M68K, UC_MODE_BIG_ENDIAN, Uc
from unicorn.m68k_const import (
    UC_CPU_M68K_M68020,
    UC_M68K_REG_A0,
    UC_M68K_REG_A1,
    UC_M68K_REG_A7,
    UC_M68K_REG_D0,
    UC_M68K_REG_D1,
)

ROM = 0x000000
ROM_SIZE = 0x80000
RAM = 0x100000
RAM_SIZE = 0x10000
STACK_TOP = RAM + RAM_SIZE - 0x10
# Where a call returns to: mapped, never executed, the emulation's end.
RETURN = 0x800000
RETURN_SIZE = 0x1000
# Scratch buffers for arguments and results.
BUFFER = RAM + 0x8000
BUFFER_SPACING = 0x100
# A byte comparator for qsort, assembled by hand:
#   movea.l (4,sp),a0; movea.l (8,sp),a1; moveq #0,d0; move.b (a0),d0;
#   moveq #0,d1; move.b (a1),d1; sub.l d1,d0; rts
COMPARE_BYTES = bytes.fromhex("206f0004226f00087000101072001211" "90814e75")
COMPARE_AT = RAM + 0x7000
# Instructions a call may run before it is taken to be stuck.
STEP_LIMIT = 2_000_000

# The routines, by name, at their Z3805A 3543B addresses.
ADDRESSES = {
    "abs": 0x5EC9E,
    "labs": 0x5EF02,
    "atoi": 0x5ECFA,
    "div": 0x5EEA2,
    "ldiv": 0x5EED2,
    "memcmp": 0x5EF12,
    "memcpy": 0x5EF48,
    "memset": 0x5EF66,
    "qsort": 0x5F0CC,
    "rand": 0x5F102,
    "strcat": 0x5F128,
    "strchr": 0x5F140,
    "strncmp": 0x5F15A,
    "strncpy": 0x5F190,
    "toupper": 0x5F1BE,
    "sprintf": 0x60896,
    "fabs": 0x6092A,
    "frexp": 0x6098A,
    "modf": 0x60D94,
    "log": 0x60A10,
    "pow": 0x60F5E,
    "sqrt": 0x6191C,
    "integer_to_float": 0x5E168,
    "unsigned_to_float": 0x5E20E,
    "float_to_integer": 0x5DFBC,
    "float_to_unsigned": 0x5E04E,
    "integer_to_double": 0x5E08E,
    "unsigned_to_double": 0x5E108,
    "double_to_integer": 0x5DDDE,
    "double_to_unsigned": 0x5DE9E,
    "double_subtract_reversed": 0x5D30E,
    "float_add_in_place": 0x5D08A,
    "float_subtract_in_place": 0x5D080,
    "float_multiply_in_place": 0x5E316,
    "float_divide_in_place": 0x5D7DA,
    "double_add_in_place": 0x5D338,
    "double_add_pointers": 0x5D34A,
    "double_add_pointer": 0x5D34E,
    "double_subtract_in_place": 0x5D2E2,
    "double_subtract_pointer": 0x5D2F8,
    "double_subtract_reversed_pointers": 0x5D306,
    "double_subtract_reversed_pointer": 0x5D30A,
    "double_multiply_in_place": 0x5E5E6,
    "double_multiply_pointers": 0x5E5F8,
    "double_multiply_pointer": 0x5E5FC,
    "double_divide_in_place": 0x5DA3E,
    "double_divide_reversed_pointers": 0x5DA62,
    "double_divide_reversed_pointer": 0x5DA66,
    "double_is_integer": 0x60E68,
    "double_truncate_sixteenths": 0x60EFA,
    "next_decimal_digit": 0x5FAD6,
    "double_to_digits": 0x5F3BA,
}

RAND_SEED = 0x103936


class Machine:
    """The image in an emulator, with calls into it."""

    def __init__(self, image: bytes):
        self.uc = Uc(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN)
        self.uc.ctl_set_cpu_model(UC_CPU_M68K_M68020)
        self.uc.mem_map(ROM, ROM_SIZE)
        self.uc.mem_write(ROM, image)
        self.uc.mem_map(RAM, RAM_SIZE)
        self.uc.mem_map(RETURN, RETURN_SIZE)
        self.uc.mem_write(COMPARE_AT, COMPARE_BYTES)
        self.next_buffer = BUFFER

    def buffer(self, contents: bytes = b"") -> int:
        at = self.next_buffer
        self.next_buffer += BUFFER_SPACING
        self.uc.mem_write(at, contents.ljust(BUFFER_SPACING, b"\xa5"))
        return at

    def read(self, at: int, length: int) -> bytes:
        return bytes(self.uc.mem_read(at, length))

    def string(self, at: int) -> bytes:
        data = self.read(at, BUFFER_SPACING)
        return data[: data.index(0)]

    def call(
        self, name: str, *longs: int, d0: int = 0, d1: int = 0, a0: int = 0, a1: int = 0
    ) -> tuple[int, int]:
        """Calls name with longs on the stack, first argument lowest, and
        d0, d1, a0 and a1 in their registers; returns D0 and D1."""
        sp = STACK_TOP
        for value in reversed(longs):
            sp -= 4
            self.uc.mem_write(sp, struct.pack(">I", value & 0xFFFFFFFF))
        sp -= 4
        self.uc.mem_write(sp, struct.pack(">I", RETURN))
        self.uc.reg_write(UC_M68K_REG_A7, sp)
        self.uc.reg_write(UC_M68K_REG_D0, d0 & 0xFFFFFFFF)
        self.uc.reg_write(UC_M68K_REG_D1, d1 & 0xFFFFFFFF)
        self.uc.reg_write(UC_M68K_REG_A0, a0 & 0xFFFFFFFF)
        self.uc.reg_write(UC_M68K_REG_A1, a1 & 0xFFFFFFFF)
        self.uc.emu_start(ADDRESSES[name], RETURN, count=STEP_LIMIT)
        return self.uc.reg_read(UC_M68K_REG_D0), self.uc.reg_read(UC_M68K_REG_D1)


def signed(value: int) -> int:
    return value - (1 << 32) if value & 0x80000000 else value


def float_bits(value: float) -> int:
    return struct.unpack(">I", struct.pack(">f", value))[0]


def from_float_bits(bits: int) -> float:
    return struct.unpack(">f", struct.pack(">I", bits))[0]


def double_words(value: float) -> tuple[int, int]:
    return struct.unpack(">II", struct.pack(">d", value))


def from_double_words(high: int, low: int) -> float:
    return struct.unpack(">d", struct.pack(">II", high, low))[0]


def double_buffer(m: Machine, value: float) -> int:
    return m.buffer(struct.pack(">d", value))


def read_double(m: Machine, at: int) -> float:
    return struct.unpack(">d", m.read(at, 8))[0]


def entry_checks(m: Machine):
    """The in-place and pointer entries of the float and double routines."""
    x, y = 6.0, 1.5
    for name, operation in (
        ("float_add_in_place", float.__add__),
        ("float_subtract_in_place", float.__sub__),
        ("float_multiply_in_place", float.__mul__),
        ("float_divide_in_place", float.__truediv__),
    ):
        at = m.buffer(struct.pack(">f", x))
        m.call(name, d1=float_bits(y), a0=at)
        got = struct.unpack(">f", m.read(at, 4))[0]
        yield name, f"*x={x}, y={y}", got == operation(x, y)

    y_high, y_low = double_words(y)
    for name, operation in (
        ("double_add_in_place", float.__add__),
        ("double_subtract_in_place", float.__sub__),
        ("double_multiply_in_place", float.__mul__),
        ("double_divide_in_place", float.__truediv__),
    ):
        at = double_buffer(m, x)
        m.call(name, d0=y_high, d1=y_low, a0=at)
        yield name, f"*x={x}, y={y}", read_double(m, at) == operation(x, y)

    # Both operands by pointer, a in A0 and b in A1.
    for name, operation in (
        ("double_add_pointers", lambda a, b: a + b),
        ("double_multiply_pointers", lambda a, b: a * b),
        ("double_subtract_reversed_pointers", lambda a, b: b - a),
        ("double_divide_reversed_pointers", lambda a, b: b / a),
    ):
        got = from_double_words(*m.call(name, a0=double_buffer(m, x), a1=double_buffer(m, y)))
        yield name, f"*a={x}, *b={y}", got == operation(x, y)

    # a in D0:D1, b by pointer in A1.
    x_high, x_low = double_words(x)
    for name, operation in (
        ("double_add_pointer", lambda a, b: a + b),
        ("double_multiply_pointer", lambda a, b: a * b),
        ("double_subtract_pointer", lambda a, b: a - b),
        ("double_subtract_reversed_pointer", lambda a, b: b - a),
        ("double_divide_reversed_pointer", lambda a, b: b / a),
    ):
        got = from_double_words(*m.call(name, d0=x_high, d1=x_low, a1=double_buffer(m, y)))
        yield name, f"a={x}, *b={y}", got == operation(x, y)


def checks(m: Machine):
    """Yields (routine, description, passed) for each check."""
    for name in ("abs", "labs"):
        for value in (0, 5, -5, -0x7FFFFFFF):
            got = signed(m.call(name, value)[0])
            yield name, f"{value}", got == abs(value)

    for text, value in ((b"0", 0), (b"42", 42), (b"  -17x", -17), (b"+8", 8), (b"x9", 0)):
        got = signed(m.call("atoi", m.buffer(text + b"\0"))[0])
        yield "atoi", repr(text), got == value

    for name in ("div", "ldiv"):
        for a, b in ((17, 5), (-17, 5), (17, -5)):
            quotient, remainder = m.call(name, a, b)
            wanted = int(a / b)
            yield name, f"{a}/{b}", (signed(quotient), signed(remainder)) == (wanted, a - b * wanted)

    for a, b, n in ((b"abc", b"abd", 3), (b"abc", b"abd", 2), (b"abd", b"abc", 3)):
        got = signed(m.call("memcmp", m.buffer(a), m.buffer(b), n)[0])
        wanted = (a[:n] > b[:n]) - (a[:n] < b[:n])
        yield "memcmp", f"{a}/{b}/{n}", (got > 0) - (got < 0) == wanted

    destination = m.buffer()
    got = m.call("memcpy", destination, m.buffer(b"hello world"), 5)[0]
    yield "memcpy", "5 bytes", got == destination and m.read(destination, 6) == b"hello\xa5"

    destination = m.buffer()
    got = m.call("memset", destination, 0x41, 4)[0]
    yield "memset", "4 bytes", got == destination and m.read(destination, 5) == b"AAAA\xa5"

    values = b"qsortbyte"
    base = m.buffer(values)
    m.call("qsort", base, len(values), 1, COMPARE_AT)
    yield "qsort", "bytes", m.read(base, len(values)) == bytes(sorted(values))

    m.uc.mem_write(RAND_SEED, struct.pack(">I", 1))
    seed = 1
    for _ in range(3):
        seed = (seed * 1103515245 + 12345) & 0xFFFFFFFF
        yield "rand", "LCG 1103515245, 12345", m.call("rand")[0] == (seed >> 16) & 0x7FFF

    destination = m.buffer(b"ab\0")
    got = m.call("strcat", destination, m.buffer(b"cd\0"))[0]
    yield "strcat", "ab+cd", got == destination and m.string(destination) == b"abcd"

    haystack = m.buffer(b"hello\0")
    for character, offset in ((ord("l"), 2), (ord("z"), None), (0, 5)):
        got = m.call("strchr", haystack, character)[0]
        yield "strchr", chr(character) if character else "NUL", got == (0 if offset is None else haystack + offset)

    for a, b, n, sign in ((b"abc", b"abd", 3, -1), (b"abc", b"abd", 2, 0), (b"ab", b"ab", 9, 0), (b"b", b"a", 1, 1)):
        got = signed(m.call("strncmp", m.buffer(a + b"\0"), m.buffer(b + b"\0"), n)[0])
        yield "strncmp", f"{a}/{b}/{n}", (got > 0) - (got < 0) == sign

    destination = m.buffer()
    got = m.call("strncpy", destination, m.buffer(b"ab\0"), 4)[0]
    yield "strncpy", "pads", got == destination and m.read(destination, 5) == b"ab\0\0\xa5"
    destination = m.buffer()
    m.call("strncpy", destination, m.buffer(b"abcdef\0"), 3)
    yield "strncpy", "truncates", m.read(destination, 4) == b"abc\xa5"

    for character in b"aZz0{`":
        got = m.call("toupper", character)[0]
        yield "toupper", chr(character), got == ord(chr(character).upper())

    destination = m.buffer()
    m.call("sprintf", destination, m.buffer(b"%d|%5s|%x|%c\0"), 42, m.buffer(b"ab\0"), 255, ord("Q"))
    yield "sprintf", "%d %5s %x %c", m.string(destination) == b"42|   ab|ff|Q"

    for value in (2.5, -2.5, 0.0):
        got = from_double_words(*m.call("fabs", *double_words(value)))
        yield "fabs", f"{value}", got == abs(value)

    for value in (8.0, 0.75, -3.0):
        exponent = m.buffer()
        high, low = m.call("frexp", *double_words(value), exponent)
        mantissa, wanted_exponent = math.frexp(value)
        got_exponent = signed(struct.unpack(">I", m.read(exponent, 4))[0])
        yield "frexp", f"{value}", (from_double_words(high, low), got_exponent) == (mantissa, wanted_exponent)

    for value in (3.25, -2.5):
        whole = m.buffer()
        high, low = m.call("modf", *double_words(value), whole)
        fraction, wanted_whole = math.modf(value)
        got_whole = from_double_words(*struct.unpack(">II", m.read(whole, 8)))
        yield "modf", f"{value}", (from_double_words(high, low), got_whole) == (fraction, wanted_whole)

    for name, function, values in (("sqrt", math.sqrt, (4.0, 2.0)), ("log", math.log, (1.0, math.e, 10.0))):
        for value in values:
            got = from_double_words(*m.call(name, *double_words(value)))
            yield name, f"{value}", math.isclose(got, function(value), rel_tol=1e-15)
    for x, y in ((2.0, 10.0), (9.0, 0.5)):
        got = from_double_words(*m.call("pow", *double_words(x), *double_words(y)))
        yield "pow", f"{x}^{y}", math.isclose(got, x**y, rel_tol=1e-15)

    for value in (0, 7, -7):
        got = from_float_bits(m.call("integer_to_float", d0=value)[0])
        yield "integer_to_float", f"{value}", got == float(value)
    for value in (7, 0xF0000000):
        got = from_float_bits(m.call("unsigned_to_float", d0=value)[0])
        yield "unsigned_to_float", f"{value:#x}", got == float(value)
    for value in (7.9, -7.9):
        got = signed(m.call("float_to_integer", d0=float_bits(value))[0])
        yield "float_to_integer", f"{value}", got == int(value)
    got = m.call("float_to_unsigned", d0=float_bits(3e9))[0]
    yield "float_to_unsigned", "3e9", got == int(from_float_bits(float_bits(3e9)))
    for value in (0, 7, -7):
        got = from_double_words(*m.call("integer_to_double", d0=value))
        yield "integer_to_double", f"{value}", got == float(value)
    got = from_double_words(*m.call("unsigned_to_double", d0=0xF0000000))
    yield "unsigned_to_double", "0xf0000000", got == float(0xF0000000)
    for value in (7.9, -7.9):
        high, low = double_words(value)
        got = signed(m.call("double_to_integer", d0=high, d1=low)[0])
        yield "double_to_integer", f"{value}", got == int(value)
    high, low = double_words(3e9)
    got = m.call("double_to_unsigned", d0=high, d1=low)[0]
    yield "double_to_unsigned", "3e9", got == 3_000_000_000

    # pow's helpers: whether its argument is an integer, and odd, and its
    # argument truncated to sixteenths.
    for value, integer, odd in ((3.0, 1, 1), (4.0, 1, 0), (2.5, 0, 0), (-5.0, 1, 1), (1e30, 1, 0)):
        flag = m.buffer(b"\0" * 4)
        got = m.call("double_is_integer", *double_words(value), flag)[0]
        got_odd = struct.unpack(">I", m.read(flag, 4))[0]
        yield "double_is_integer", f"{value}", (got, got_odd) == (integer, odd)
    for value in (3.14159, -2.71828, 0.1, 100.03, 0.03):
        got = from_double_words(*m.call("double_truncate_sixteenths", *double_words(value)))
        yield "double_truncate_sixteenths", f"{value}", got == math.trunc(value * 16) / 16

    # The formatting engine's: a decimal digit off a number, and a double's
    # digits with its exponent, the first digit's power of ten plus one --
    # that many significant digits when the last argument is 1, as %e
    # wants, that many after the point when 0, as %f does.
    for value in (1234, 9, 0, 1000000007):
        at = m.buffer(struct.pack(">I", value))
        digit = m.call("next_decimal_digit", at)[0]
        rest = struct.unpack(">I", m.read(at, 4))[0]
        yield "next_decimal_digit", f"{value}", (digit, rest) == (ord("0") + value % 10, value // 10)
    for value, digits, wanted, wanted_exponent in ((123.456, 3, b"123", 3), (0.00125, 4, b"13", -2)):
        exponent, sign, out = m.buffer(b"\0" * 4), m.buffer(b"\0" * 4), m.buffer()
        m.call("double_to_digits", *double_words(value), digits, exponent, sign, out, value > 1)
        got_exponent = signed(struct.unpack(">I", m.read(exponent, 4))[0])
        yield "double_to_digits", f"{value}", (m.string(out), got_exponent) == (wanted, wanted_exponent)

    for a, d in ((3.0, 1.0), (1.0, 3.0), (-2.5, 0.5)):
        a_high, a_low = double_words(a)
        d_high, d_low = double_words(d)
        got = from_double_words(
            *m.call("double_subtract_reversed", d0=d_high, d1=d_low, a0=a_high, a1=a_low)
        )
        yield "double_subtract_reversed", f"{a}-{d}", got == a - d


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    with open(sys.argv[1], "rb") as image:
        machine = Machine(image.read())
    failed = 0
    for routine, description, passed in [*checks(machine), *entry_checks(machine)]:
        print(f"{'ok' if passed else 'FAIL'} {routine} {description}")
        failed += not passed
    print(f"{failed} failed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
