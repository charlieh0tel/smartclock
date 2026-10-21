/* Small integer and table routines, written by hand in place of the
 * decompiler's (docs/firmware/README.md, "Hand-written C"): absolute
 * value, clamp, CRC-16, rounding shifts and division, the powers of ten
 * and the random dither.  Most are called by nothing.  The exported C
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
  return (uint32_t)(uint8_t)~sum << 24 | (uint32_t)sum << 16 |
         (crc & CRC16_MASK);
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
  int32_t value = 0;

  while (value < 1 || value > DITHER_RAND_MAX) {
    value = rand();
  }
  value >>= DITHER_SHIFT;
  if (value >= DITHER_SIZE) {
    return (int8_t)dither_table[value - DITHER_SIZE];
  }
  return -(int8_t)dither_table[DITHER_SIZE - value];
}
