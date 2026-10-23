/* Small integer and table routines, written by hand in place of the
 * decompiler's (docs/firmware/README.md, "Hand-written C"): absolute
 * value, clamp, CRC-16, rounding shifts and division, the square root,
 * bit and byte helpers, the 64-bit routines, magnitudes and rounding of
 * floats, the powers of ten and the random dither.  Most are called by
 * nothing.  The exported C
 * includes this file after its own, so the types and prototypes it
 * declares are in scope; each function here keeps its generated
 * prototype. */

/* power_of_ten_double and _float's tables hold 1e-30 to 1e30. */
static const int16_t ARITHMETIC_POWER_OF_TEN_LIMIT = 30;

/* The 68000's shifts take their count modulo 64. */
static const uint32_t ARITHMETIC_SHIFT_COUNT_MASK = 63;

static const uint32_t ARITHMETIC_LONG_BITS = 32;

/* An int's magnitude; nothing calls it (0x120e6). */
int32_t int_abs(int32_t value) { return value < 0 ? -value : value; }

/* A value clamped between two bounds, the upper tested first; nothing
 * calls it (0x1211a). */
int32_t int_clamp(int32_t value, int32_t low, int32_t high) {
  if (value > high) {
    return high;
  }
  if (value < low) {
    return low;
  }
  return value;
}

/* A reflected CRC-16, polynomial 0x8408 from all ones, over a count of
 * bytes, and their sum: ~sum << 24 | sum << 16 | crc, 0 for no bytes.  Each
 * of a byte's 8 steps tests the byte's own low bit, never shifted, against
 * the CRC's, so that the CRC is not the CCITT one; no call or table
 * in the image names it (0x1213a). */
uint32_t crc16_with_sum(uint8_t* bytes, int16_t count) {
  /* 0x8408, the CCITT polynomial 0x1021 reflected. */
  static const uint32_t CRC16_POLYNOMIAL = 0x8408;
  static const uint32_t CRC16_MASK = 0xffff;
  uint32_t crc = 0xffffffff;
  uint8_t sum = 0;

  if (count == 0) {
    return 0;
  }
  for (; count > 0; count--, bytes++) {
    sum += *bytes;
    for (int bit = 0; bit < 8; bit++) {
      if ((crc ^ *bytes) & 1) {
        crc = crc >> 1 ^ CRC16_POLYNOMIAL;
      } else {
        crc >>= 1;
      }
    }
  }
  const uint8_t complement = ~sum;

  return (uint32_t)complement << 24 | (uint32_t)sum << 16 | (crc & CRC16_MASK);
}

/* A long shifted as the processor shifts it: the count modulo 64, and 32
 * or more shifting every bit out. */
static uint32_t arithmetic_shift_left(uint32_t value, uint32_t count) {
  count &= ARITHMETIC_SHIFT_COUNT_MASK;
  return count >= ARITHMETIC_LONG_BITS ? 0 : value << count;
}

static int32_t arithmetic_shift_right(int32_t value, uint32_t count) {
  count &= ARITHMETIC_SHIFT_COUNT_MASK;
  if (count >= ARITHMETIC_LONG_BITS) {
    return value < 0 ? -1 : 0;
  }
  return value >> count;
}

/* An int shifted right with rounding, half away from zero; nothing calls
 * it (0x12206). */
int32_t round_shift_right(int32_t value, uint32_t shift) {
  int32_t half = arithmetic_shift_left(1, shift - 1);

  if (value < 0) {
    return -arithmetic_shift_right(half - value, shift);
  }
  return arithmetic_shift_right(value + half, shift);
}

/* An int divided with rounding, half away from zero; nothing calls it
 * (0x122d0). */
int32_t round_divide(int32_t dividend, int32_t divisor) {
  if (dividend < 0) {
    return -(((divisor >> 1) - dividend) / divisor);
  }
  return (dividend + (divisor >> 1)) / divisor;
}

/* A value's square root rounded to nearest, by two bits a step, then a
 * half bit; 0 for a negative value and 256 for one of 16 bits or more;
 * nothing calls it (0x1219a). */
uint16_t isqrt16(uint32_t value) {
  static const uint16_t SQUARE_ROOT_MOST = 0x100;
  static const uint32_t SQUARE_ROOT_DOMAIN = 0x10000;
  static const int32_t BITS_PER_STEP = 2;
  static const uint32_t STEP_MASK = 3;
  uint32_t root = 0;
  uint32_t remainder = 0;

  if ((int32_t)value < 0) {
    return 0;
  }
  if (value >= SQUARE_ROOT_DOMAIN) {
    return SQUARE_ROOT_MOST;
  }
  for (int32_t shift = 14; shift >= 0; shift -= BITS_PER_STEP) {
    const uint32_t trial = root << BITS_PER_STEP | 1;

    remainder = remainder << BITS_PER_STEP | (value >> shift & STEP_MASK);
    root <<= 1;
    if (remainder >= trial) {
      remainder -= trial;
      root |= 1;
    }
  }
  /* The half bit: value >= (root + 1/2)^2 when the remainder exceeds
   * the root. */
  if (remainder > root) {
    root++;
  }
  return root;
}

/* A long rotated left by a count modulo 32, as rol.l takes it
 * (0x13406). */
uint32_t rotate_left(uint32_t value, uint8_t count) {
  return libc_rotate_left(value, count);
}

/* A word's set bits (0x4f1da). */
char count_bits(uint32_t word) {
  char count = 0;

  for (; word != 0; word >>= 1) {
    if (word & 1) {
      count++;
    }
  }
  return count;
}

/* Four bytes, high first, as a long; returns the byte after them
 * (0x4ef8e). */
uint8_t* bytes_to_long(uint8_t* bytes, uint32_t* value) {
  static const int32_t LONG_BYTES = 4;
  uint32_t result = 0;

  for (int32_t i = 0; i < LONG_BYTES; i++) {
    result = result << 8 | *bytes++;
  }
  *value = result;
  return bytes;
}

/* Whether two byte strings have the same nonzero length and bytes
 * (0x4fb54). */
uint32_t bytes_equal_n(uint8_t* a, int16_t a_length, uint8_t* b,
                       int16_t b_length) {
  if (a_length != b_length || a_length == 0) {
    return 0;
  }
  for (; *a == *b; a++, b++) {
    if (--a_length == 0) {
      return 1;
    }
  }
  return 0;
}

/* The 64-bit routines pass a number as two longs, the high first. */
static uint64_t arithmetic_int64(uint32_t high, uint32_t low) {
  return (uint64_t)high << ARITHMETIC_LONG_BITS | low;
}

/* The sum of two 64-bit numbers (0x410fa). */
uint64_t int64_add(int32_t a_high, uint32_t a_low, int32_t b_high,
                   uint32_t b_low) {
  return arithmetic_int64(a_high, a_low) + arithmetic_int64(b_high, b_low);
}

/* A 64-bit number negated (0x4111e). */
uint64_t int64_negate(int32_t high, int32_t low) {
  return -arithmetic_int64(high, low);
}

/* 0 if two 64-bit numbers are equal, else 1 if b - a, taken as
 * int64_add(int64_negate(a), b) and wrapping, is not negative and -1 if
 * it is: 1 for a below b, the reverse of a C comparator; nothing calls
 * it (0x41086). */
int32_t int64_compare(int64_t a, int64_t b) {
  if (a == b) {
    return 0;
  }
  const uint64_t negated = int64_negate(a >> ARITHMETIC_LONG_BITS, a);
  const int64_t difference = int64_add(negated >> ARITHMETIC_LONG_BITS, negated,
                                       b >> ARITHMETIC_LONG_BITS, b);

  return difference < 0 ? -1 : 1;
}

/* A 64-bit number plus a signed long; nothing calls it (0x41132). */
uint64_t int64_add_long(uint32_t addend, int32_t high, uint32_t low) {
  const int32_t signed_addend = addend;

  return arithmetic_int64(high, low) + (int64_t)signed_addend;
}

/* A signed 64-bit number divided by a long.  A quotient too wide for 32
 * bits leaves divs.l's destination as it was, so that the result is the
 * dividend's low long; nothing calls it (0x4115c). */
int32_t int64_divide(int32_t divisor, int32_t high, uint32_t low) {
  static const int64_t LONG_MOST = 0x7fffffff;
  static const int64_t LONG_LEAST = -LONG_MOST - 1;
  const int64_t quotient = (int64_t)arithmetic_int64(high, low) / divisor;

  if (quotient < LONG_LEAST || quotient > LONG_MOST) {
    return low;
  }
  return quotient;
}

/* The same unsigned, as divu.l does it; nothing calls it (0x4117e). */
uint32_t uint64_divide(uint32_t divisor, uint32_t high, uint32_t low) {
  static const uint64_t UNSIGNED_LONG_MOST = 0xffffffff;
  const uint64_t quotient = arithmetic_int64(high, low) / divisor;

  if (quotient > UNSIGNED_LONG_MOST) {
    return low;
  }
  return quotient;
}

/* A signed 64-bit number as a double: its magnitude's low long plus its
 * high long times 2^32 (two_to_32, 0x41300), the sign set after
 * (0x4119c). */
double int64_to_double(int64_t value) {
  uint32_t high = (uint64_t)value >> ARITHMETIC_LONG_BITS;
  uint32_t low = value;
  const bool negative = (high & LIBC_SIGN) != 0;

  if (negative) {
    const uint64_t magnitude = int64_negate(high, low);

    high = magnitude >> ARITHMETIC_LONG_BITS;
    low = magnitude;
  }
  union libc_double result = {.value = unsigned_to_double(low)};

  double_add_in_place(&result.value, double_multiply_pointer(
                                         unsigned_to_double(high), &two_to_32));
  if (negative) {
    result.word.high ^= LIBC_SIGN;
  }
  return result.value;
}

/* A double as a signed 64-bit number: its
 * magnitude over 2^32 the high long, what remains the low, negated after;
 * nothing calls it (0x41244). */
int64_t double_to_int64(double x) {
  union libc_double value = {.value = x};
  /* double_compare(0, x) is negative when x < 0. */
  const bool negative = double_compare(0.0, value.value) < 0;

  if (negative) {
    value.word.high ^= LIBC_SIGN;
  }
  const uint32_t result_high = double_to_unsigned(
      double_divide_reversed_pointers(&two_to_32, &value.value));

  double_subtract_in_place(
      &value.value,
      double_multiply_pointer(unsigned_to_double(result_high), &two_to_32));
  const uint32_t result_low = double_to_unsigned(value.value);

  if (negative) {
    return int64_negate(result_high, result_low);
  }
  return arithmetic_int64(result_high, result_low);
}

/* A float's magnitude: its sign flipped when float_compare finds it
 * below 0, so that -0 and a NaN keep theirs (0x42626). */
float float_abs(float value) {
  union libc_float result = {.value = value};

  if (float_compare(0.0f, value) > 0) {
    result.bits ^= LIBC_SIGN;
  }
  return result.value;
}

/* A float's magnitude, its sign bit cleared (0x133e2). */
float float_absolute_on_stack(float f) { return libc_float_magnitude(f); }

/* A double with its sign bit cleared; nothing calls it (0x133f2). */
double double_abs(double x) { return libc_double_magnitude(x); }

/* A double rounded half away from zero to a long, saturating at the
 * long's bounds (0x1223c).  double_compare(a, x) is negative when
 * x < a and positive when x > a. */
int32_t double_round_to_long(double x) {
  static const double LONG_LEAST = -2147483648.0;
  static const double LONG_MOST = 2147483647.0;
  static const int32_t LONG_LEAST_VALUE = -2147483647 - 1;
  static const int32_t LONG_MOST_VALUE = 2147483647;
  static const double HALF = 0.5;

  if (double_compare(0.0, x) < 0) {
    if (double_compare(LONG_LEAST, x) < 0) {
      return LONG_LEAST_VALUE;
    }
    /* 0.5 - x, the magnitude rounded up. */
    return -double_to_integer(double_subtract_pointer(HALF, &x));
  }
  if (double_compare(LONG_MOST, x) > 0) {
    return LONG_MOST_VALUE;
  }
  return double_to_integer(double_add_pointer(HALF, &x));
}

/* 1e-30 to 1e30: the image's tables at 0x12614 and 0x129fc, which
 * double_decimal_exponent and tfom_table_index bisect too. */
const double powers_of_ten_double[61] = {
    1e-30, 1e-29,
    1e-28, 1e-27,
    1e-26, 1e-25,
    1e-24, 1e-23,
    1e-22, 1e-21,
    1e-20, 1e-19,
    1e-18, 1e-17,
    1e-16, 1e-15,
    1e-14, 1e-13,
    1e-12, 1e-11,
    1e-10, 1e-9,
    1e-8,  1e-7,
    1e-6,  1e-5,
    1e-4,  1e-3,
    1e-2,  1e-1,
    1e0,   1e1,
    1e2,   1e3,
    1e4,   1e5,
    1e6,   1e7,
    1e8,   1e9,
    1e10,  1e11,
    1e12,  1e13,
    1e14,  1e15,
    1e16,  1e17,
    1e18,  1e19,
    1e20,  1e21,
    1e22,  1.0000000000000001e+23 /* 1e23 an ulp high, as the image has it */,
    1e24,  1e25,
    1e26,  1e27,
    1e28,  1e29,
    1e30,
};

const float powers_of_ten_float[61] = {
    1e-30f, 1e-29f, 1e-28f, 1e-27f, 1e-26f, 1e-25f, 1e-24f, 1e-23f, 1e-22f,
    1e-21f, 1e-20f, 1e-19f, 1e-18f, 1e-17f, 1e-16f, 1e-15f, 1e-14f, 1e-13f,
    1e-12f, 1e-11f, 1e-10f, 1e-9f,  1e-8f,  1e-7f,  1e-6f,  1e-5f,  1e-4f,
    1e-3f,  1e-2f,  1e-1f,  1e0f,   1e1f,   1e2f,   1e3f,   1e4f,   1e5f,
    1e6f,   1e7f,   1e8f,   1e9f,   1e10f,  1e11f,  1e12f,  1e13f,  1e14f,
    1e15f,  1e16f,  1e17f,  1e18f,  1e19f,  1e20f,  1e21f,  1e22f,  1e23f,
    1e24f,  1e25f,  1e26f,  1e27f,  1e28f,  1e29f,  1e30f,
};

/* 10 to a power, the power clamped to plus or minus 30; nothing calls it
 * (0x125e2). */
double power_of_ten_double(int16_t power) {
  if (power < -ARITHMETIC_POWER_OF_TEN_LIMIT) {
    power = -ARITHMETIC_POWER_OF_TEN_LIMIT;
  } else if (power > ARITHMETIC_POWER_OF_TEN_LIMIT) {
    power = ARITHMETIC_POWER_OF_TEN_LIMIT;
  }
  return powers_of_ten_double[power + ARITHMETIC_POWER_OF_TEN_LIMIT];
}

/* The same as a float; nothing calls it (0x129d0). */
float power_of_ten_float(int16_t power) {
  if (power < -ARITHMETIC_POWER_OF_TEN_LIMIT) {
    power = -ARITHMETIC_POWER_OF_TEN_LIMIT;
  } else if (power > ARITHMETIC_POWER_OF_TEN_LIMIT) {
    power = ARITHMETIC_POWER_OF_TEN_LIMIT;
  }
  return powers_of_ten_float[power + ARITHMETIC_POWER_OF_TEN_LIMIT];
}

/* A table's entries, 1e-30 to 1e30 or 5e-30 to 5e30, one for each power
 * from -ARITHMETIC_POWER_OF_TEN_LIMIT up. */
#define ARITHMETIC_POWER_ENTRIES 61

/* The index of the entry a double's magnitude equals, else of the last
 * entry below it (upper false) or the first above it (upper true), by
 * bisection; past either end, the end's neighbor.  The magnitude's
 * words compare as integers, the high signed and the low unsigned,
 * which orders positive doubles by value (0x1258a, 0x122f6). */
static int16_t arithmetic_power_bisect(const double* table, double value,
                                       bool upper) {
  union libc_double magnitude = {.value = value};
  int16_t low = 0;
  int16_t high = ARITHMETIC_POWER_ENTRIES;

  magnitude.word.high &= ~LIBC_SIGN;
  for (;;) {
    int16_t middle = (low + high) / 2;

    if (middle == low) {
      return upper ? high : low;
    }
    union libc_double entry = {.value = table[middle]};
    int32_t value_high = magnitude.word.high;
    int32_t entry_high = entry.word.high;

    if (value_high < entry_high ||
        (value_high == entry_high && magnitude.word.low < entry.word.low)) {
      high = middle;
    } else if (value_high > entry_high || magnitude.word.low > entry.word.low) {
      low = middle;
    } else {
      return middle;
    }
  }
}

/* The power of ten a double's magnitude rounds up to, ceil(log10), from
 * -29 to 31; nothing calls it (0x1258a). */
int16_t double_decimal_exponent(double value) {
  return arithmetic_power_bisect(powers_of_ten_double, value, true) -
         ARITHMETIC_POWER_OF_TEN_LIMIT;
}

/* The power of ten a double's magnitude is not below, floor(log10),
 * from -30 to 30, entry 0's for one below it; nothing calls it
 * (0x12534). */
int16_t double_decimal_exponent_floor(double value) {
  return arithmetic_power_bisect(powers_of_ten_double, value, false) -
         ARITHMETIC_POWER_OF_TEN_LIMIT;
}

/* 5e-30 to 5e30, the image's table at 0x1234c; 5e22, as 1e23 in
 * powers_of_ten_double, an ulp high. */
static const double ARITHMETIC_POWERS_TIMES_FIVE[ARITHMETIC_POWER_ENTRIES] = {
    5e-30,
    5e-29,
    5e-28,
    5e-27,
    5e-26,
    5e-25,
    5e-24,
    5e-23,
    5e-22,
    5e-21,
    5e-20,
    5e-19,
    5e-18,
    5e-17,
    5e-16,
    5e-15,
    5e-14,
    5e-13,
    5e-12,
    5e-11,
    5e-10,
    5e-9,
    5e-8,
    5e-7,
    5e-6,
    5e-5,
    5e-4,
    5e-3,
    5e-2,
    5e-1,
    5e0,
    5e1,
    5e2,
    5e3,
    5e4,
    5e5,
    5e6,
    5e7,
    5e8,
    5e9,
    5e10,
    5e11,
    5e12,
    5e13,
    5e14,
    5e15,
    5e16,
    5e17,
    5e18,
    5e19,
    5e20,
    5e21,
    5.0000000000000004e+22,
    5e23,
    5e24,
    5e25,
    5e26,
    5e27,
    5e28,
    5e29,
    5e30,
};

/* The power of ten a double's magnitude rounds to, the next one from 5
 * times a power up, from -29 to 31; nothing calls it (0x122f6). */
int16_t double_decimal_exponent_rounded(double value) {
  return arithmetic_power_bisect(ARITHMETIC_POWERS_TIMES_FIVE, value, false) -
         (ARITHMETIC_POWER_OF_TEN_LIMIT - 1);
}

/* The index of the entry a float's magnitude equals, else of the last
 * entry below it, by bisection; past either end, the end's neighbor.
 * The magnitude's bits compare as a signed long, which orders positive
 * floats by value (0x127fc, 0x1293a). */
static int16_t arithmetic_float_power_bisect(const float* table, float value) {
  uint32_t bits;
  int16_t low = 0;
  int16_t high = ARITHMETIC_POWER_ENTRIES;

  memcpy(&bits, &value, sizeof bits);
  const int32_t magnitude = bits & ~LIBC_SIGN;
  for (;;) {
    int16_t middle = (low + high) / 2;
    int32_t entry;

    if (middle == low) {
      return middle;
    }
    memcpy(&entry, &table[middle], sizeof entry);
    if (magnitude == entry) {
      return middle;
    }
    if (magnitude < entry) {
      high = middle;
    } else {
      low = middle;
    }
  }
}

/* 5e-30 to 5e30 as floats, the image's table at 0x12846,
 * powers_of_ten_times_five_float. */
static const float
    ARITHMETIC_FLOAT_POWERS_TIMES_FIVE[ARITHMETIC_POWER_ENTRIES] = {
        5e-30f, 5e-29f, 5e-28f, 5e-27f, 5e-26f, 5e-25f, 5e-24f, 5e-23f, 5e-22f,
        5e-21f, 5e-20f, 5e-19f, 5e-18f, 5e-17f, 5e-16f, 5e-15f, 5e-14f, 5e-13f,
        5e-12f, 5e-11f, 5e-10f, 5e-9f,  5e-8f,  5e-7f,  5e-6f,  5e-5f,  5e-4f,
        5e-3f,  5e-2f,  5e-1f,  5e0f,   5e1f,   5e2f,   5e3f,   5e4f,   5e5f,
        5e6f,   5e7f,   5e8f,   5e9f,   5e10f,  5e11f,  5e12f,  5e13f,  5e14f,
        5e15f,  5e16f,  5e17f,  5e18f,  5e19f,  5e20f,  5e21f,  5e22f,  5e23f,
        5e24f,  5e25f,  5e26f,  5e27f,  5e28f,  5e29f,  5e30f,
};

/* The power of ten a float's magnitude rounds to, the next one from 5
 * times a power up, from -29 to 31; nothing calls it (0x127fc). */
int16_t float_decimal_exponent_rounded(float value) {
  return arithmetic_float_power_bisect(ARITHMETIC_FLOAT_POWERS_TIMES_FIVE,
                                       value) -
         (ARITHMETIC_POWER_OF_TEN_LIMIT - 1);
}

/* The power of ten a float's magnitude is at or above, floor(log10),
 * from -30 to 30; nothing calls it (0x1293a). */
int16_t float_decimal_exponent_floor(float value) {
  return arithmetic_float_power_bisect(powers_of_ten_float, value) -
         ARITHMETIC_POWER_OF_TEN_LIMIT;
}

/* A small signed random value for the EFC's dither: rand's 1 to 0x7fff,
 * its top 8 bits indexing dither_table from its start for the upper half
 * and, negated, back from its end for the lower.  A value of 0 reads the
 * byte after the table, start_nothing's first, as the image does
 * (0x12af2). */
int32_t random_dither(void) {
  /* A rand value of 1 to 0x7fff, its top 8 bits read from either end
   * of dither_table. */
  static const int32_t DITHER_RAND_MAX = 0x7fff;
  static const int32_t DITHER_SHIFT = 7;
  static const int32_t DITHER_SIZE = 128;
  /* The image's table at 0x12b32, dither_table, and the byte after it
   * that a value of 0 reads, start_nothing's rts (0x4e75). */
  static const int8_t DITHER[] = {0,
                                  0,
                                  0,
                                  0,
                                  1,
                                  1,
                                  1,
                                  1,
                                  1,
                                  1,
                                  2,
                                  2,
                                  2,
                                  2,
                                  2,
                                  2,
                                  2,
                                  3,
                                  3,
                                  3,
                                  3,
                                  3,
                                  3,
                                  4,
                                  4,
                                  4,
                                  4,
                                  4,
                                  4,
                                  5,
                                  5,
                                  5,
                                  5,
                                  5,
                                  5,
                                  6,
                                  6,
                                  6,
                                  6,
                                  6,
                                  6,
                                  7,
                                  7,
                                  7,
                                  7,
                                  7,
                                  7,
                                  8,
                                  8,
                                  8,
                                  8,
                                  8,
                                  8,
                                  9,
                                  9,
                                  9,
                                  9,
                                  9,
                                  10,
                                  10,
                                  10,
                                  10,
                                  10,
                                  10,
                                  11,
                                  11,
                                  11,
                                  11,
                                  11,
                                  12,
                                  12,
                                  12,
                                  12,
                                  13,
                                  13,
                                  13,
                                  13,
                                  13,
                                  14,
                                  14,
                                  14,
                                  14,
                                  15,
                                  15,
                                  15,
                                  15,
                                  15,
                                  16,
                                  16,
                                  16,
                                  16,
                                  17,
                                  17,
                                  17,
                                  18,
                                  18,
                                  18,
                                  18,
                                  19,
                                  19,
                                  19,
                                  20,
                                  20,
                                  20,
                                  21,
                                  21,
                                  22,
                                  22,
                                  22,
                                  23,
                                  23,
                                  24,
                                  24,
                                  25,
                                  25,
                                  26,
                                  26,
                                  27,
                                  28,
                                  28,
                                  29,
                                  30,
                                  31,
                                  32,
                                  33,
                                  34,
                                  36,
                                  39,
                                  /* start_nothing */ 0x4e};
  int32_t value = 0;

  while (value < 1 || value > DITHER_RAND_MAX) {
    value = rand();
  }
  value >>= DITHER_SHIFT;
  if (value >= DITHER_SIZE) {
    return DITHER[value - DITHER_SIZE];
  }
  return -DITHER[DITHER_SIZE - value];
}

/* A value of so many bits sign-extended to a long (0x3145a). */
uint32_t sign_extend_bits(uint32_t value, uint8_t width) {
  return records_sign_extend_bits(value, width);
}

/* A float's magnitude bisected in powers_of_ten_float_21 (1e-21 to
 * 1e21, 0x32700) between entries 0 and 42: the entry after the last at
 * or below it, less an offset, which the image takes unchecked.
 * Nothing calls it (0x3202a). */
float float_table_index(float* value, int16_t offset) {
  static const int16_t TABLE_END = 42;
  const float magnitude = double_to_float(fabs(float_to_double(*value)));
  int16_t low = 0;
  int16_t high = TABLE_END;

  while (low + 1 < high) {
    const int16_t middle = (low + high) / 2;

    if (float_compare(magnitude, powers_of_ten_float_21[middle]) < 0) {
      high = middle;
    } else {
      low = middle;
    }
  }
  return powers_of_ten_float_21[low - offset + 1];
}
