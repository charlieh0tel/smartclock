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
 * the CRC's, so that the CRC is not the CCITT one; nothing calls it
 * (0x1213a). */
uint32_t crc16_ccitt(uint8_t* bytes, int16_t count) {
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
  count %= ARITHMETIC_LONG_BITS;
  if (count == 0) {
    return value;
  }
  return value << count | value >> (ARITHMETIC_LONG_BITS - count);
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
uint32_t bytes_equal_n(char* a, int16_t a_length, char* b, int16_t b_length) {
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
float float_absolute_on_stack(float f) {
  union libc_float result = {.value = f};

  result.bits &= ~LIBC_SIGN;
  return result.value;
}

/* A double with its sign bit cleared; nothing calls it (0x133f2). */
double double_abs(double x) {
  union libc_double result = {.value = x};

  result.word.high &= ~LIBC_SIGN;
  return result.value;
}

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
 * double_decimal_exponent and tfom_table_index, the decompiler's, bisect
 * too. */
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
  if (width == 8) {
    return (uint32_t)(int8_t)value;
  }
  if (width == 16) {
    return (uint32_t)(int16_t)value;
  }
  if (width == 0 || width >= RECORDS_LONG_BITS) {
    return value;
  }
  if (value & (uint32_t)1 << (width - 1)) {
    value |= ~(uint32_t)0 << width;
  }
  return value;
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
