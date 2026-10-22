/* The C library's formatted output and the console device, written by
 * hand in place of the decompiler's (docs/firmware/README.md, "Hand-
 * written C"; docs/firmware/modules.md, `platform/stdio`).  The exported
 * C includes this file after its own, so the types and prototypes it
 * declares are in scope; each function here keeps its generated
 * prototype. */

/* Writes a string to the console, device 0; returns its length
 * (0x1de76). */
uint32_t console_write(char* s) {
  uint32_t length;

  psos_io_write(IO_DEVICE_DUART_B, s, &length);
  return length;
}

/* Reads from the console, device 0, and its last read errors
 * (0x1debc). */
uint32_t console_read(void* buffer, uint32_t length, uint8_t* errors) {
  psos_io_read(IO_DEVICE_DUART_B, buffer, &length);
  if (errors != 0) {
    *errors = duartb.read_errors;
  }
  return length;
}

/* '0' plus *n % 10, dividing *n by ten (0x5fad6).  It leaves the
 * quotient in D1 too, where divsl.l put it, which no caller reads. */
int32_t next_decimal_digit(uint32_t* n) {
  uint32_t value = *n;

  *n = value / 10;
  return '0' + value % 10;
}

/* The 128-bit product of the 64-bit numbers product[0..1] and
 * product[2..3], high word first, into product[0..3] (0x5f1d6). */
void double_to_digits_multiply(uint32_t* product) {
  uint64_t a_high = product[0], a_low = product[1];
  uint64_t b_high = product[2], b_low = product[3];
  uint64_t low = a_low * b_low;
  uint64_t cross_1 = a_low * b_high;
  uint64_t cross_2 = a_high * b_low;
  uint64_t middle =
      (low >> LIBC_WORD_BITS) + (uint32_t)cross_1 + (uint32_t)cross_2;
  uint64_t high = a_high * b_high + (cross_1 >> LIBC_WORD_BITS) +
                  (cross_2 >> LIBC_WORD_BITS) + (middle >> LIBC_WORD_BITS);

  product[0] = high >> LIBC_WORD_BITS;
  product[1] = high;
  product[2] = middle;
  product[3] = low;
}

/* mode: the count is of digits after the point, as %f wants, or of
 * significant digits, as %e does. */
enum {
  LIBC_DIGITS_AFTER_POINT = 0,
};

/* A 64-bit number from two words, high first, as the tables keep them. */
static uint64_t libc_words(const uint32_t* words) {
  return (uint64_t)words[0] << LIBC_WORD_BITS | words[1];
}

/* 10^n, n 0 to 17: the image's powers_of_ten_64 (0x5fa46). */
static uint64_t libc_power_of_ten(int32_t n) {
  static const uint64_t POWERS[] = {
      1,
      10,
      100,
      1000,
      10000,
      100000,
      1000000,
      10000000,
      100000000,
      1000000000,
      10000000000u,
      100000000000u,
      1000000000000u,
      10000000000000u,
      100000000000000u,
      1000000000000000u,
      10000000000000000u,
      100000000000000000u,
  };

  return POWERS[n];
}

/* The 128-bit product[0..3], high word first, shifted right by 32 +
 * shift and rounded to the nearest 64-bit integer, ties to even:
 * double_to_digits's integer, in the image's cases.  A shift that is
 * a multiple of 32 other than 64 leaves the high 64 bits. */
static uint64_t libc_digits_round(const uint32_t product[4], int16_t shift) {
  int16_t bit = shift & (LIBC_WORD_BITS - 1);
  int16_t word = 2 - (shift >> 5);
  uint32_t round_bit;

  if (bit == 0) {
    round_bit = LIBC_SIGN;
    word++;
  } else {
    round_bit = 1u << (bit - 1);
  }
  uint32_t half = product[word] & round_bit;
  /* The bits below the round bit, whether any is set. */
  uint32_t sticky = 0;

  if (half != 0) {
    sticky = product[word] & (half - 1);
    while (word < 3) {
      sticky |= product[++word];
    }
  }
  uint64_t high = libc_words(product);
  uint64_t integer;

  if (bit == 0) {
    integer = shift == 2 * LIBC_WORD_BITS ? product[0] : high;
  } else if (shift < LIBC_WORD_BITS) {
    integer = high << (LIBC_WORD_BITS - bit) | product[2] >> bit;
  } else if (shift > 2 * LIBC_WORD_BITS) {
    integer = product[0] >> bit;
  } else {
    integer = high >> bit;
  }
  if (half != 0) {
    integer++;
    if (sticky == 0) {
      integer &= ~1ull;
    }
  }
  return integer;
}

/* value's decimal digits into digits, NUL-terminated, its sign
 * through negative and through exponent the power of ten of its first
 * digit plus one; count digits after the point or significant ones, by
 * mode (0x5f3ba).  An infinity is "++" or "--" and a NaN "?", exponent
 * their length.  It scales the mantissa by a power of ten estimated
 * from the binary exponent so that its integer part has the digits
 * wanted, and estimates again one higher if that has one too many. */
void double_to_digits(double value, int32_t count, int32_t* exponent,
                      uint32_t* negative, char* digits, int32_t mode) {
  static const uint32_t EXPONENT_MASK = 0x7ff00000u;
  /* The most digits it writes. */
  static const int32_t DIGITS_MAX = 0x15c;
  /* The most it computes; those past them are zeros. */
  static const int32_t DIGITS_EXACT = 17;
  /* log10(2) in 16 fractional bits, rounded down for a positive
   * exponent and up for a negative one, so that the decimal
   * exponent estimated from the binary one is never too large. */
  static const int32_t LOG10_2_DOWN = 0x4d10;
  static const int32_t LOG10_2_UP = 0x4d11;
  static const int32_t LOG10_2_BITS = 16;
  /* 10^(25k), k -13 to 13, as a 64-bit mantissa, high word first, and
   * its binary exponent: the image's powers_of_ten_by_25_mantissas and
   * _exponents (0x5f860, 0x5f938). */
  static const uint32_t BY_25[][2] = {
      {0xa5ced43b, 0x7e3e9188}, {0xab70fe17, 0xc79ac6cb},
      {0xb1442798, 0xf49ffb4b}, {0xb749faed, 0x14125d37},
      {0xbd8430bd, 0x08277232}, {0xc3f490aa, 0x77bd60fd},
      {0xca9cf1d2, 0x06fdc03c}, {0xd17f3b51, 0xfca3a7a1},
      {0xd89d64d5, 0x7a607745}, {0xdff97724, 0x70297ebe},
      {0xe7958cb8, 0x7392c2c3}, {0xef73d256, 0xa5c0f77d},
      {0xf79687ae, 0xd3eec551}, {0x00000000, 0x00000000},
      {0x84595161, 0x401484a0}, {0x88d8762b, 0xf324cd10},
      {0x8d7eb760, 0x70a08aed}, {0x924d692c, 0xa61be758},
      {0x9745eb4d, 0x50ce6333}, {0x9c69a972, 0x84b578d8},
      {0xa1ba1ba7, 0x9e1632dc}, {0xa738c6be, 0xbb12d16d},
      {0xace73cbf, 0xdc0bfb7b}, {0xb2c71d5b, 0xca9023f8},
      {0xb8da1662, 0xe7b00a17}, {0xbf21e440, 0x03acdd2d},
      {0xc5a05277, 0x621be294}};
  static const int16_t BY_25_EXPONENTS[] = {
      -1080, -997, -914, -831, -748, -665, -582, -499, -416,
      -333,  -250, -167, -84,  0,    83,   166,  249,  332,
      415,   498,  581,  664,  747,  830,  913,  996,  1079};
  /* The same for 10^1 to 10^24: powers_of_ten_1_to_24_mantissas and
   * _exponents (0x5f96e, 0x5fa2e). */
  static const uint32_t ONE_TO_24[][2] = {
      {0xa0000000, 0x00000000}, {0xc8000000, 0x00000000},
      {0xfa000000, 0x00000000}, {0x9c400000, 0x00000000},
      {0xc3500000, 0x00000000}, {0xf4240000, 0x00000000},
      {0x98968000, 0x00000000}, {0xbebc2000, 0x00000000},
      {0xee6b2800, 0x00000000}, {0x9502f900, 0x00000000},
      {0xba43b740, 0x00000000}, {0xe8d4a510, 0x00000000},
      {0x9184e72a, 0x00000000}, {0xb5e620f4, 0x80000000},
      {0xe35fa931, 0xa0000000}, {0x8e1bc9bf, 0x04000000},
      {0xb1a2bc2e, 0xc5000000}, {0xde0b6b3a, 0x76400000},
      {0x8ac72304, 0x89e80000}, {0xad78ebc5, 0xac620000},
      {0xd8d726b7, 0x177a8000}, {0x87867832, 0x6eac9000},
      {0xa968163f, 0x0a57b400}, {0xd3c21bce, 0xcceda100}};
  static const uint8_t ONE_TO_24_EXPONENTS[] = {3,  6,  9,  13, 16, 19, 23, 26,
                                                29, 33, 36, 39, 43, 46, 49, 53,
                                                56, 59, 63, 66, 69, 73, 76, 79};
  /* The power of ten it scales by is a power of 10^25, from the
   * table's 10^-325 to 10^325 (index 13 is 10^0), times one of
   * 10^1 to 10^24 (libc.md, 0x5f864). */
  static const int32_t POWER_STEP = 25;
  static const int32_t POWER_STEP_ZERO = 13;
  /* The scaled product's binary point: the shift that brings its
   * integer part to its low 64 bits is this less the binary
   * exponent and the powers' exponents. */
  static const int32_t DIGITS_POINT = 0x52;
  union libc_double v = {.value = value};
  uint32_t field = (v.word.high & EXPONENT_MASK) >> LIBC_EXPONENT_SHIFT;
  uint64_t mantissa = v.bits & LIBC_MANTISSA_MASK;

  *negative = (v.word.high & LIBC_SIGN) != 0;
  if (field == LIBC_EXPONENT_MAX) {
    if (mantissa == 0) {
      char sign = *negative ? '-' : '+';

      *digits++ = sign;
      *digits++ = sign;
      *exponent = 2;
    } else {
      *digits++ = '?';
      *exponent = 1;
    }
    *digits = '\0';
    return;
  }
  if (field == 0) {
    if (mantissa == 0) {
      if (count < 0) {
        count = 0;
      } else if (count > DIGITS_MAX) {
        count = DIGITS_MAX;
      }
      for (; count != 0; count--) {
        *digits++ = '0';
      }
      *digits = '\0';
      *exponent = 0;
      return;
    }
    field = 1 - libc_normalize(&mantissa);
  }
  mantissa |= LIBC_IMPLICIT_ONE;
  int32_t binary_exponent = (int32_t)field - LIBC_EXPONENT_BIAS;
  int32_t log10_2 = binary_exponent < 0 ? LOG10_2_UP : LOG10_2_DOWN;
  int16_t decimal = (uint32_t)(binary_exponent * log10_2) >> LOG10_2_BITS;

  if (count < 0) {
    count = 0;
  }
  if (mode == LIBC_DIGITS_AFTER_POINT) {
    count += decimal + 1;
  }
  if (count > DIGITS_MAX) {
    count = DIGITS_MAX;
  }
  int32_t significant = count < 0              ? 0
                        : count > DIGITS_EXACT ? DIGITS_EXACT
                                               : count;
  int16_t guess;
  uint64_t integer;

  for (;;) {
    guess = decimal;
    /* The power of ten to scale by, 10^(25 big + small). */
    int16_t power = (int16_t)significant - guess - 1;
    int32_t big = power / POWER_STEP;
    int32_t small = power - (int16_t)big * POWER_STEP;

    if (small < 0) {
      big--;
      small += POWER_STEP;
    }
    int16_t shift = DIGITS_POINT - (int16_t)binary_exponent;
    uint32_t product[4];

    if (small == 0 && big == 0) {
      product[0] = mantissa >> LIBC_WORD_BITS;
      product[1] = mantissa;
      product[2] = 0;
      product[3] = 0;
      shift += 2;
    } else {
      if (small == 0) {
        product[0] = BY_25[big + POWER_STEP_ZERO][0];
        product[1] = BY_25[big + POWER_STEP_ZERO][1];
        shift -= BY_25_EXPONENTS[big + POWER_STEP_ZERO];
        shift++;
      } else {
        product[0] = ONE_TO_24[small - 1][0];
        product[1] = ONE_TO_24[small - 1][1];
        shift -= ONE_TO_24_EXPONENTS[small - 1];
        if (big == 0) {
          shift++;
        } else {
          product[2] = BY_25[big + POWER_STEP_ZERO][0];
          product[3] = BY_25[big + POWER_STEP_ZERO][1];
          shift -= BY_25_EXPONENTS[big + POWER_STEP_ZERO];
          double_to_digits_multiply(product);
          /* The power's 128 bits rounded to their high 64, ties to even. */
          if ((product[2] & LIBC_SIGN) != 0) {
            if (++product[1] == 0) {
              product[0]++;
            }
            if (product[3] == 0 && (product[2] & ~LIBC_SIGN) == 0) {
              product[1] &= ~1u;
            }
          }
        }
      }
      product[2] = mantissa >> LIBC_WORD_BITS;
      product[3] = mantissa;
      double_to_digits_multiply(product);
    }
    integer = libc_digits_round(product, shift);

    uint64_t limit = libc_power_of_ten(significant);

    if (integer < limit) {
      break;
    }
    if (integer == limit && mode != LIBC_DIGITS_AFTER_POINT) {
      guess++;
      if (significant != 0) {
        integer = libc_power_of_ten(significant - 1);
      }
      break;
    }
    if (mode == LIBC_DIGITS_AFTER_POINT) {
      guess++;
      significant++;
      count++;
      break;
    }
    decimal = guess + 1;
  }
  if (count < 1) {
    *exponent = guess + 1;
    *digits = '\0';
    return;
  }

  /* The integer's digits, by subtracting powers of ten: in 64 bits
   * while it needs them, then in 32. */
  int16_t place = significant;
  char* out = digits;

  while (--place, integer >> LIBC_WORD_BITS != 0) {
    uint64_t power = libc_power_of_ten(place);

    *out = '0';
    for (; power <= integer; integer -= power) {
      (*out)++;
    }
    out++;
  }
  /* Places worth 2^32 or more are zeros now, written once a digit has been. */
  while (libc_power_of_ten(place) >> LIBC_WORD_BITS != 0) {
    place--;
    if (out > digits) {
      *out++ = '0';
    }
  }
  uint32_t rest = integer;

  for (; place != 0; place--) {
    uint32_t power = libc_power_of_ten(place);

    *out = '0';
    for (; power <= rest; rest -= power) {
      (*out)++;
    }
    out++;
  }
  *out++ = '0' + rest;
  for (int32_t zeros = count - significant; zeros != 0; zeros--) {
    *out++ = '0';
  }
  *out = '\0';
  *exponent = guess + 1;
}
