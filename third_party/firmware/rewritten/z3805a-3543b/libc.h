/* What the C library's files share (math.c, stdio.c, stdlib.c,
 * string.c, docs/firmware/libc.md): a double's IEEE 754 bits and their
 * fields, and the bodies of the routines the installer has its own copy
 * of and that read nothing else, which both copies call. The
 * exported C includes the hand-written headers after its own
 * declarations and before the hand-written .c files, so every one of
 * them can use it.
 *
 * The image has no FPU.  Floating arithmetic in the C library is a call
 * to the image's soft-float routines (docs/firmware/softfloat.md), never
 * a C operator, which gcc would hand to libgcc's; and the math routines
 * work on a double's bits, as the image's do, through union libc_double. */

#include <stdarg.h>
#include <stddef.h>

/* A double's IEEE 754 bits.  The 68k is big-endian: the high word,
 * sign, exponent and the top 20 bits of the mantissa, comes first. */
union libc_double {
  double value;
  uint64_t bits;
  struct {
    uint32_t high;
    uint32_t low;
  } word;
};

/* A float's IEEE 754 bits, which a step record holds and the magnitude
 * routines change in place of an arithmetic negation. */
union libc_float {
  float value;
  uint32_t bits;
};

/* ctype_table's bits, by the characters that carry them (libc.md);
 * stdlib.c defines the table, which gps_oncore.c reads too. */
enum {
  LIBC_CTYPE_UPPER = 0x01, /* A to Z */
  LIBC_CTYPE_LOWER = 0x02, /* a to z */
  LIBC_CTYPE_DIGIT = 0x04,
  LIBC_CTYPE_SPACE = 0x08,       /* tab to carriage return, and space */
  LIBC_CTYPE_PUNCTUATION = 0x10, /* the printing characters but these */
  LIBC_CTYPE_CONTROL = 0x20,     /* 0 to 0x1f and 0x7f */
  LIBC_CTYPE_BLANK = 0x40,       /* space alone */
  LIBC_CTYPE_HEX = 0x80,         /* 0 to 9, A to F, a to f */
};

/* errno's value for a result out of range. */
static const int32_t LIBC_ERANGE = 34;

/* Fields of a double's high word, and of its 64 bits; an infinity's
 * high word is the exponent's mask, all ones. */
static const uint32_t LIBC_SIGN = 0x80000000u;

static const uint64_t LIBC_MANTISSA_MASK = 0x000fffffffffffffull;

static const uint64_t LIBC_IMPLICIT_ONE = 1ull << 52;

static const int32_t LIBC_EXPONENT_SHIFT = 20;

static const int32_t LIBC_EXPONENT_MAX = 0x7ff;

static const int32_t LIBC_EXPONENT_BIAS = 0x3ff;

static const int32_t LIBC_WORD_BITS = 32;

/* A subnormal's mantissa shifted left until its top bit is the
 * implicit one's, and the shift: double_normalize's work (0x5e882), and
 * what sqrt, log and double_to_digits do in line. */
static int32_t libc_normalize(uint64_t* mantissa) {
  int32_t shift = 0;

  while ((*mantissa & LIBC_IMPLICIT_ONE) == 0) {
    *mantissa <<= 1;
    shift++;
  }
  return shift;
}

/* A text's length, as the image counts it in line: it has no strlen. */
static uint32_t libc_length(const char* text) {
  uint32_t length = 0;

  while (text[length] != 0) {
    length++;
  }
  return length;
}

/* ldiv's quotient, its result's high long, leaving the remainder, its
 * low long, in *numerator. */
static int32_t libc_divide(int32_t* numerator, int32_t denominator) {
  static const int32_t LONG_BITS = 32;
  int64_t result = ldiv(*numerator, denominator);

  *numerator = (int32_t)result;
  return result >> LONG_BITS;
}

/* Copies a text with its NUL, as the image does in line. */
static void libc_copy(char* destination, const char* text) {
  while ((*destination++ = *text++) != 0) {
  }
}

/* Appends a character to a message of a length byte and its text, as
 * float_exception_report and program_end_report build theirs for
 * fatal_error_with_code. */
static void libc_message_append(uint8_t* message, char c) {
  message[1 + message[0]++] = c;
}

/* memcpy's and its installer copy's body: copies n bytes. */
static void* libc_memcpy(void* s1, void* s2, uint32_t n) {
  uint8_t* to = s1;
  uint8_t* from = s2;

  for (; n != 0; n--) {
    *to++ = *from++;
  }
  return s1;
}

/* strncpy's and its installer copy's body: copies at most n characters
 * and pads with NULs; a string it truncates it leaves unterminated. */
static char* libc_strncpy(char* s1, char* s2, uint32_t n) {
  char* to = s1;

  while (n != 0) {
    n--;
    if ((*to++ = *s2++) == '\0') {
      break;
    }
  }
  for (; n != 0; n--) {
    *to++ = '\0';
  }
  return s1;
}

/* toupper's and its installer copy's body: a lower-case ASCII letter
 * in upper case. */
static int32_t libc_toupper(int32_t c) {
  if (c >= 'a' && c <= 'z') {
    c -= 'a' - 'A';
  }
  return c;
}

/* next_decimal_digit's and its installer copy's body: '0'
 * plus *n % 10, dividing *n by ten.  The image leaves the quotient in
 * D1 too, where divsl.l put it, which no caller reads. */
static int32_t libc_next_decimal_digit(uint32_t* n) {
  uint32_t value = *n;

  *n = value / 10;
  return '0' + value % 10;
}

/* double_to_digits_multiply's and its installer copy's body: the
 * 128-bit product of the 64-bit numbers product[0..1] and
 * product[2..3], high word first, into product[0..3]. */
static void libc_double_to_digits_multiply(uint32_t* product) {
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

/* double_to_digits's and its installer copy's body, whose tables the
 * installer's code at installer::0x109562 holds the same: value's
 * decimal digits into digits, NUL-terminated, its sign
 * through negative and through exponent the power of ten of its first
 * digit plus one; count digits after the point or significant ones, by
 * mode.  An infinity is "++" or "--" and a NaN "?", exponent
 * their length.  It scales the mantissa by a power of ten estimated
 * from the binary exponent so that its integer part has the digits
 * wanted, and estimates again one higher if that has one too many. */
static void libc_double_to_digits(double value, int32_t count,
                                  int32_t* exponent, uint32_t* negative,
                                  char* digits, int32_t mode) {
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
          libc_double_to_digits_multiply(product);
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
      libc_double_to_digits_multiply(product);
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

/* sprintf_put's and its installer copy's body: copies so many items of
 * doprnt's output, of a size of 1 only, to sprintf's buffer; returns
 * the count copied, 0 for another size. */
static int32_t libc_sprintf_put(char* text, int32_t size, int32_t count,
                                sprintf_output* output) {
  if (size != 1) {
    return 0;
  }
  for (int32_t left = count; left > 0; left--) {
    *output->cursor++ = *text++;
  }
  return count;
}

/* The buffers the printf family formats into on its stack, as array
 * sizes: printf's, printf_polled's and sci_printf's, and the debug and
 * SCI polled routines' (0x1de96, 0x1ef90, 0x202f6). */
#define LIBC_PRINTF_LENGTH 160
#define LIBC_SHORT_PRINTF_LENGTH 80

/* float_raise's code (libc.md, "Exception codes"): the high word names
 * the routine, here the math library's (LIBC_RAISE_MATH and its
 * index), its bit 0 set for a routine of single precision (each odd
 * entry of either table: ftoi, add32, acosf ...); the low word the
 * cause. */
enum {
  LIBC_RAISE_SINGLE = 0x01,
  LIBC_RAISE_MATH = 0x80,
  LIBC_RAISE_FABS = 16,
  LIBC_RAISE_FREXP = 22,
  LIBC_RAISE_LOG = 26,
  LIBC_RAISE_MODF = 30,
  LIBC_RAISE_POW = 32,
  LIBC_RAISE_SQRT = 38,
  LIBC_CAUSE_SIGNALING_NAN = 0x4000,
  LIBC_CAUSE_OPERAND_ERROR = 0x2000,
  LIBC_CAUSE_OVERFLOW = 0x1000,
  LIBC_CAUSE_UNDERFLOW = 0x800,
  LIBC_CAUSE_DIVIDE_BY_ZERO = 0x400,
  LIBC_CAUSE_INEXACT = 0x200,
  LIBC_CAUSE_PRECISION_LOST = 0x80,
};

#define LIBC_RAISE_CODE(routine, cause) \
  ((uint32_t)(LIBC_RAISE_MATH | (routine)) << LIBC_WORD_BITS / 2 | (cause))

/* float_exception_report's and its installer copy's body: builds
 * float_raise's report, "fp <cause>: <routine>", a length byte and its
 * text, in message (libc.md, "Exception codes"). */
static void libc_float_exception_message(uint8_t* message, uint32_t code) {
  /* The causes, tested in this order, and their messages. */
  static const struct {
    uint16_t cause;
    const char* message;
  } CAUSES[] = {
      {LIBC_CAUSE_SIGNALING_NAN, "fp signaling NaN: "},
      {LIBC_CAUSE_OPERAND_ERROR, "fp operand error: "},
      {LIBC_CAUSE_OVERFLOW, "fp overflow: "},
      {LIBC_CAUSE_UNDERFLOW, "fp underflow: "},
      {LIBC_CAUSE_DIVIDE_BY_ZERO, "fp divide by zero: "},
      {LIBC_CAUSE_INEXACT, "fp inexact result: "},
      {LIBC_CAUSE_PRECISION_LOST, "fp all precision lost:"},
  };
  /* The routines' names, 8 characters each, spaces dropped, indexed by
   * the code's high word's low 6 bits: the arithmetic's 1 to 0x13 and
   * the math library's 0x80 to 0xed, a code of 0 or past either taken as
   * its last. */
  static const uint16_t ARITHMETIC_LAST = 0x13;
  static const uint16_t MATH_LAST = 0xed;
  static const uint16_t NAME_INDEX_MASK = 0x3f;
  static const int32_t NAME_LENGTH = 8;
  const char* cause = "fp unknown exception: ";
  const uint16_t cause_bits = code;
  uint16_t routine = code >> LIBC_WORD_BITS / 2;
  const char* names;

  message[0] = 0;
  for (size_t i = 0; i < sizeof CAUSES / sizeof CAUSES[0]; i++) {
    if ((cause_bits & CAUSES[i].cause) != 0) {
      cause = CAUSES[i].message;
      break;
    }
  }
  for (; *cause != '\0'; cause++) {
    libc_message_append(message, *cause);
  }
  if ((routine & LIBC_RAISE_MATH) != 0) {
    names =
        "  acos() acosf()  asin() asinf()  atan() atanf() atan2()atan2f()"
        "  ceil() ceilf()   cos()  cosf()  cosh() coshf()   exp()  expf()"
        "  fabs() fabsf() floor()floorf()  fmod() fmodf() frexp()frexpf()"
        " ldexp()ldexpf()   log()  logf() log10()log10f()  modf() modff()"
        "   pow()  powf()   sin()  sinf()  sinh() sinhf()  sqrt() sqrtf()"
        "   tan()  tanf()  tanh() tanhf()  frem() fremf()  ????? ";
    if (routine > MATH_LAST || routine == 0) {
      routine = MATH_LAST;
    }
  } else {
    names =
        "  ?????   ftoi()  ftod()  dtof()  dtoi() cmp32() cmp64() add32()"
        " add64() sub32() sub64() mul32() mul64() div32() div64()  itof()"
        " dtoui() ftoui() uitod() uitof()  ????? ";
    if (routine > ARITHMETIC_LAST || routine == 0) {
      routine = ARITHMETIC_LAST;
    }
  }
  const char* name = names + (routine & NAME_INDEX_MASK) * NAME_LENGTH;

  for (int32_t i = 0; i < NAME_LENGTH; i++) {
    if (name[i] != ' ') {
      libc_message_append(message, name[i]);
    }
  }
}

/* errno's value for an argument out of a function's domain. */
static const int32_t LIBC_EDOM = 33;

/* The longs float_raise reads after its code, as its callers push them:
 * a NaN's bits; an underflow's shift count and its mantissa, a double's
 * in three longs and a float's in two; and, for the math library, the
 * address its report gives, after those.  An array size, so #define. */
#define LIBC_RAISE_OPERANDS 4
/* And its stack arguments, the code first, as many as it reads. */
#define LIBC_RAISE_ARGUMENTS (1 + LIBC_RAISE_OPERANDS)

/* What float_raise's body decides: the double it returns, its bits in
 * D0:D1, high first, and whether it reports instead, with the address
 * the report prints as its code (float_exception_report). */
typedef struct libc_raised {
  uint32_t high;
  uint32_t low;
  bool report;
  uint32_t address;
} libc_raised;

/* float_raise's state: the primary's or the installer's status, whose
 * bits record the causes raised, and control, whose same bits enable
 * each cause's report; and errno. */
typedef struct libc_raise_state {
  uint16_t* status;
  uint16_t control;
  int32_t* error;
} libc_raise_state;

/* The operands from float_raise's variable list, all LIBC_RAISE_OPERANDS
 * of them whatever the cause, as the image reads them from its
 * caller's frame. */
static void libc_raise_operands(va_list list,
                                uint32_t operand[LIBC_RAISE_OPERANDS]) {
  for (int32_t i = 0; i < LIBC_RAISE_OPERANDS; i++) {
    operand[i] = va_arg(list, uint32_t);
  }
}

/* The address float_raise's caller returns to, which the image reads
 * through the caller's frame pointer (0x5e99a), as one of its reports'
 * addresses. */
#define LIBC_CALLERS_RETURN() (((const uint32_t*)__builtin_frame_address(1))[1])

/* A long shifted right by a count, as lsr.l shifts by a register: the
 * count modulo 64, nothing left from 32 on (MC68000PRM, LSR). */
static uint32_t libc_shift_right(uint32_t value, uint32_t count) {
  count &= 2 * LIBC_WORD_BITS - 1;
  return count < (uint32_t)LIBC_WORD_BITS ? value >> count : 0;
}

/* The same left, as lsl.l does it. */
static uint32_t libc_shift_left(uint32_t value, uint32_t count) {
  count &= 2 * LIBC_WORD_BITS - 1;
  return count < (uint32_t)LIBC_WORD_BITS ? value << count : 0;
}

/* A long rotated right by a count, as ror.l rotates by a register:
 * the count modulo 64, a rotation by 32 the long as it was (MC68000PRM,
 * ROL, ROR). */
static uint32_t libc_rotate_right(uint32_t value, uint32_t count) {
  count &= LIBC_WORD_BITS - 1;
  return count == 0 ? value
                    : value >> count | value << (LIBC_WORD_BITS - count);
}

/* The same left, as rol.l does it. */
static uint32_t libc_rotate_left(uint32_t value, uint32_t count) {
  return libc_rotate_right(value,
                           LIBC_WORD_BITS - (count & (LIBC_WORD_BITS - 1)));
}

/* Whether a result rounds up, to nearest with ties to even, by the
 * bits shifted out of it, its guard bit at the top of guard and its
 * sticky bits below, and the long whose bit 0 is its last (0x5ea44,
 * 0x5eacc). */
static bool libc_raise_rounds_up(uint32_t guard, uint32_t last) {
  static const uint32_t GUARD = 0x80000000u;

  return (guard & GUARD) != 0 && ((guard & ~GUARD) != 0 || (last & 1) != 0);
}

/* An infinity of a routine's precision, a float's in the high long
 * alone (0x5eb90, 0x5eb9a). */
static void libc_raise_infinity(libc_raised* raised, bool single) {
  static const uint32_t SINGLE_INFINITY = 0x7f800000;
  static const uint32_t DOUBLE_INFINITY = 0x7ff00000;

  raised->report = false;
  raised->high = single ? SINGLE_INFINITY : DOUBLE_INFINITY;
  if (!single) {
    raised->low = 0;
  }
}

/* An underflow's result, a double's mantissa shifted right by an
 * exponent's count, with its guard and sticky bits (0x5e990 to
 * 0x5ea32): a count below 32 shifts the three longs as one, its bits
 * shifted out of the lowest kept as sticky; 32 moves each down a long;
 * up to 55 does both; and anything past 55, which leaves nothing, the
 * caller makes zero.  The count is compared as the low word of its long
 * and shifts by its low 6 bits, as the image's does; the implicit one
 * is set and then cleared again, so that a result rounded up into it
 * keeps it. */
static uint32_t libc_raise_double_underflow(libc_raised* raised, uint32_t count,
                                            uint32_t high, uint32_t low,
                                            uint32_t sticky) {
  static const uint32_t HIGH_MANTISSA = 0x000fffff;
  static const uint32_t IMPLICIT_ONE = 0x00100000;
  const int16_t places = count;
  uint32_t guard;

  high = (high & HIGH_MANTISSA) | IMPLICIT_ONE;
  if (places != LIBC_WORD_BITS) {
    const uint32_t shift =
        places < LIBC_WORD_BITS ? count : count - LIBC_WORD_BITS;
    const uint32_t kept = libc_shift_right(~0u, shift);
    const uint32_t rotated_high = libc_rotate_right(high, shift);
    const uint32_t rotated_low = libc_rotate_right(low, shift);

    high = rotated_high & kept;
    low = (rotated_low & kept) | (rotated_high & ~kept);
    guard = libc_shift_right(sticky, shift) | (rotated_low & ~kept);
    if ((sticky & libc_rotate_left(~kept, shift)) != 0) {
      guard |= 1;
    }
    sticky = guard;
  }
  if (places >= LIBC_WORD_BITS) {
    guard = low | (sticky != 0);
    low = high;
    high = 0;
  }
  raised->high = high & ~IMPLICIT_ONE;
  raised->low = low;
  return guard;
}

/* The same for a float's mantissa, its high long, and the long after it
 * (0x5ea7c to 0x5eaba): the bits shifted out of the mantissa are the
 * guard's top, those out of the long after it below them, and what of
 * that long the shift keeps is left as the result's low long. */
static uint32_t libc_raise_single_underflow(libc_raised* raised, uint32_t count,
                                            uint32_t high, uint32_t after) {
  static const uint32_t MANTISSA = 0x007fffff;
  static const uint32_t IMPLICIT_ONE = 0x00800000;
  const uint32_t shifted_out = ~libc_shift_left(~0u, count);

  high = (high & MANTISSA) | IMPLICIT_ONE;
  uint32_t guard = libc_shift_right(after, count) |
                   libc_rotate_right(high & shifted_out, count);

  if ((after & shifted_out) != 0) {
    guard |= 1;
  }
  raised->high = libc_shift_right(high, count) & ~IMPLICIT_ONE;
  raised->low = after & shifted_out;
  return guard;
}

/* float_raise's and its installer copy's body (0x5e93c): records the
 * cause in the state's status, sets errno for the math library's, and
 * gives the double its caller returns, an infinity, the largest NaN, the
 * operand quieted or an underflow's denormal, or, where the state's
 * control enables the cause, the report.  low starts as callers_d1, the
 * register as the caller left it with its low word the cause, which a
 * float's infinity and its quieted NaN return beside their one long
 * (0x5e948).  The report's address is, by the cause, an operand the math
 * library passes, the address its caller's caller returns to,
 * callers_return, or 0 for a code of no cause, chosen by the routine's
 * bit 7 but for two: the signaling NaN's tests the cause's bit 7, the
 * precision lost (0x5ec24), and an underflow's inexact report the
 * result's (0x5ebe0). */
static libc_raised libc_float_raise(uint32_t code, uint32_t callers_d1,
                                    const uint32_t operand[LIBC_RAISE_OPERANDS],
                                    uint32_t callers_return,
                                    libc_raise_state state) {
  static const uint32_t SINGLE_QUIET = 0x00400000;
  static const uint32_t DOUBLE_QUIET = 0x00080000;
  static const uint32_t LARGEST_NAN_HIGH = 0x7fffffff;
  static const uint32_t LARGEST_NAN_LOW = 0xffffffff;
  static const int16_t SINGLE_UNDERFLOW_MOST = 24;
  static const int16_t DOUBLE_UNDERFLOW_MOST = 55;
  static const uint32_t HIGH_WORD = 0xffff0000;
  const uint16_t cause = code;
  const uint16_t routine = code >> LIBC_WORD_BITS / 2;
  const bool single = (routine & LIBC_RAISE_SINGLE) != 0;
  const bool math = (routine & LIBC_RAISE_MATH) != 0;
  /* the address an inexact result's report gives (0x5ebdc) */
  const uint32_t inexact_address = math ? operand[0] : callers_return;
  libc_raised raised = {.high = 0,
                        .low = (callers_d1 & HIGH_WORD) | cause,
                        .report = true,
                        .address = inexact_address};

  if ((cause & LIBC_CAUSE_SIGNALING_NAN) != 0) {
    const uint32_t quiet = single ? SINGLE_QUIET : DOUBLE_QUIET;

    raised.high = operand[0];
    if (!single) {
      raised.low = operand[1];
    }
    raised.address = operand[single ? 1 : 2];
    if ((raised.high & quiet) != 0) {
      raised.report = false;
      return raised;
    }
    raised.high |= quiet;
    *state.status |= LIBC_CAUSE_OPERAND_ERROR;
    raised.report = (state.control & LIBC_CAUSE_OPERAND_ERROR) != 0;
    if ((code & LIBC_CAUSE_PRECISION_LOST) == 0) {
      raised.address = callers_return;
    }
  } else if ((cause & LIBC_CAUSE_OPERAND_ERROR) != 0) {
    raised.address = operand[0];
    *state.status |= LIBC_CAUSE_OPERAND_ERROR;
    if (math) {
      raised.address = callers_return;
      *state.error = LIBC_EDOM;
    }
    raised.report = (state.control & LIBC_CAUSE_OPERAND_ERROR) != 0;
    raised.high = LARGEST_NAN_HIGH;
    raised.low = LARGEST_NAN_LOW;
  } else if ((cause & LIBC_CAUSE_OVERFLOW) != 0) {
    raised.address = operand[0];
    *state.status |= LIBC_CAUSE_OVERFLOW | LIBC_CAUSE_INEXACT;
    if (math) {
      raised.address = callers_return;
      *state.error = LIBC_ERANGE;
    }
    if ((state.control & LIBC_CAUSE_OVERFLOW) != 0) {
      return raised;
    }
    if ((state.control & LIBC_CAUSE_INEXACT) != 0) {
      raised.address = inexact_address;
      return raised;
    }
    libc_raise_infinity(&raised, single);
  } else if ((cause & LIBC_CAUSE_UNDERFLOW) != 0) {
    const int16_t places = operand[0];

    if (single) {
      raised.address = math ? operand[2] << LIBC_WORD_BITS / 2 |
                                  operand[3] >> LIBC_WORD_BITS / 2
                            : callers_return;
    } else {
      raised.address = math ? operand[3] : callers_return;
    }
    if (places > (single ? SINGLE_UNDERFLOW_MOST : DOUBLE_UNDERFLOW_MOST)) {
      /* nothing is left (0x5eafe) */
      if (math) {
        *state.error = LIBC_ERANGE;
      }
      raised.high = 0;
      raised.low = 0;
    } else if (single) {
      const uint32_t guard = libc_raise_single_underflow(
          &raised, operand[0], operand[1], operand[2]);

      if (guard == 0) {
        raised.report = false;
        return raised;
      }
      *state.status |= LIBC_CAUSE_INEXACT;
      if (libc_raise_rounds_up(guard, raised.high)) {
        raised.high++;
      }
    } else {
      const uint32_t guard = libc_raise_double_underflow(
          &raised, operand[0], operand[1], operand[2], operand[3]);

      if (guard == 0) {
        raised.report = false;
        return raised;
      }
      *state.status |= LIBC_CAUSE_INEXACT;
      if (libc_raise_rounds_up(guard, raised.low) && ++raised.low == 0) {
        raised.high++;
      }
    }
    *state.status |= LIBC_CAUSE_UNDERFLOW | LIBC_CAUSE_INEXACT;
    if ((state.control & LIBC_CAUSE_UNDERFLOW) != 0) {
      return raised;
    }
    if ((state.control & LIBC_CAUSE_INEXACT) != 0) {
      /* the inexact report's test of the routine's bit 7 reads the
       * result's high long, which the image keeps where it kept the
       * routine (0x5ebe0) */
      raised.address =
          (raised.high & LIBC_RAISE_MATH) != 0 ? operand[0] : callers_return;
      return raised;
    }
    raised.report = false;
  } else if ((cause & LIBC_CAUSE_DIVIDE_BY_ZERO) != 0) {
    raised.address = operand[0];
    *state.status |= LIBC_CAUSE_DIVIDE_BY_ZERO;
    if (math) {
      raised.address = callers_return;
      *state.error = LIBC_ERANGE;
    }
    if ((state.control & LIBC_CAUSE_DIVIDE_BY_ZERO) != 0) {
      return raised;
    }
    libc_raise_infinity(&raised, single);
  } else if ((cause & LIBC_CAUSE_INEXACT) != 0) {
    return raised;
  } else if ((cause & LIBC_CAUSE_PRECISION_LOST) != 0) {
    *state.status |= LIBC_CAUSE_PRECISION_LOST | LIBC_CAUSE_INEXACT;
    raised.report =
        (state.control & (LIBC_CAUSE_PRECISION_LOST | LIBC_CAUSE_INEXACT)) != 0;
    raised.high = LARGEST_NAN_HIGH;
    raised.low = LARGEST_NAN_LOW;
  } else {
    raised.address = 0;
  }
  return raised;
}

/* doprnt's state for the flags of a conversion, as it keeps them in one
 * word (0x5fb5e on). */
enum {
  LIBC_PRINT_LEFT = 0x04,           /* '-': padded on the right */
  LIBC_PRINT_PRECISION = 0x40,      /* a precision was given */
  LIBC_PRINT_EXPONENT = 0x80,       /* an exponent follows the digits */
  LIBC_PRINT_TRAILING_ZEROS = 0x100 /* zeros past the digits computed */
};

/* Puts count copies of a run's character, 20 at a time from text, a run
 * of 20, the last put even when it is empty; the count put. */
static int32_t libc_print_run(const char* text, int32_t count,
                              sprintf_put_function* put,
                              sprintf_output* output) {
  static const int32_t RUN = 20;
  int32_t done = 0;

  for (; count > RUN; count -= RUN) {
    done += RUN;
    put((char*)text, 1, RUN, output);
  }
  put((char*)text, 1, count, output);
  return done + count;
}

/* A long's decimal digits, signed as the image divides them, written
 * backward before start; the first digit's place.  None for 0 when a
 * precision was given. */
static char* libc_print_decimal(uint32_t value, char* start,
                                bool precision_given) {
  static const int32_t BASE = 10;
  int32_t rest = value;

  if (rest < BASE) {
    if (rest != 0 || !precision_given) {
      *--start = '0' + rest;
    }
    return start;
  }
  do {
    *--start = '0' + rest % BASE;
    rest /= BASE;
  } while (rest >= BASE);
  *--start = '0' + rest;
  return start;
}

/* doprnt's and its installer copy's body: formats the format with the
 * arguments, putting each run of text through put to output; the
 * count the conversions' widths make.  ctype is its image's ctype_table
 * past its EOF entry, indexed by a character.  The C library's printf engine,
 * as libc.md describes it: flags ' ', '#', '+', '-' and '0', a width and a
 * precision, each a number or '*', the sizes h, l and L, and the
 * conversions c, d, e, E, f, g, G, i, n, o, p, P, s, S, u, x, X and %;
 * another character is printed as itself. */
static int32_t libc_doprnt(const uint8_t* ctype, char* format, void* arguments,
                           sprintf_put_function* put, sprintf_output* output) {
  /* The most digits double_to_digits gives significant, and the most a
   * fixed-point fraction takes. */
  static const int32_t SIGNIFICANT_MOST = 17;
  static const int32_t FRACTION_MOST = 60;
  static const int32_t DEFAULT_PRECISION = 6;
  /* %g's exponent range for the fixed-point form. */
  static const int32_t FIXED_LEAST = -3;
  static const uint32_t DOUBLE_MAGNITUDE = 0x7fffffff;
  static const char SPACES[] = "                    ";
  static const char ZEROS[] = "00000000000000000000";
  /* The conversion's text: its own 20 bytes, then double_to_digits'
   * digits from the 22nd, which a fixed-point number's integer part
   * runs on into as it is written, behind its reading of them. */
  char text[20 + 352];
  char* const conversion = text;
  char* const digits = text + 21;
  /* A number's last place for a decimal or hex conversion. */
  char* const number_end = conversion + 11;
  char exponent[6];
  /* What the conversions make, widths included, and what has been put,
   * which %n stores. */
  int32_t counted = 0;
  int32_t written = 0;
  /* The values a conversion leaves for the next, as the image keeps
   * them in its frame: only read where they were set for it. */
  const char* prefix = 0;
  int32_t leading_zeros = 0;
  int32_t trailing_zeros = 0;
  char* exponent_text = 0;
  int32_t exponent_length = 0;
  int32_t precision = 0;
  int32_t significant = 0;
  int32_t fraction_most = 0;
  uint32_t* next_argument = arguments;

  for (;;) {
    char* scan = format + 1;
    uint8_t c = *format;

    if (c != 0 && c != '%') {
      do {
        c = *scan++;
      } while (c != 0 && c != '%');
      const int32_t length = scan - 1 - format;

      counted += length;
      written += length;
      put(format, 1, length, output);
    }
    if (c == 0) {
      return counted;
    }
    uint32_t flags = 0;
    bool zero_pad = false;
    bool long_size = false;
    bool alternate = false;
    bool short_size = false;
    bool space = false;
    bool plus = false;
    int32_t extra = 0;
    int32_t prefix_length = 0;
    int32_t width = 0;
    bool zeros = false;
    bool string_short = false;
    char* start = 0;
    char* end = 0;
    uint32_t conversion_char;

  next_flag:
    zeros = false;
    format = scan + 1;
    conversion_char = (uint8_t)*scan;
    string_short = false;
    switch (conversion_char) {
      case ' ':
        space = true;
        scan = format;
        goto next_flag;
      case '#':
        alternate = true;
        scan = format;
        goto next_flag;
      case '%':
        conversion[0] = '%';
        start = conversion;
        end = conversion + 1;
        break;
      case '*':
        if ((flags & LIBC_PRINT_PRECISION) == 0) {
          width = *next_argument++;
          if (width < 0) {
            width = -width;
            flags ^= LIBC_PRINT_LEFT;
          }
        } else {
          precision = *next_argument++;
          if (precision < 0) {
            flags ^= LIBC_PRINT_PRECISION;
          }
        }
        scan = format;
        goto next_flag;
      case '+':
        plus = true;
        scan = format;
        goto next_flag;
      case '-':
        flags |= LIBC_PRINT_LEFT;
        zero_pad = false;
        scan = format;
        goto next_flag;
      case '.':
        flags |= LIBC_PRINT_PRECISION;
        precision = 0;
        scan = format;
        goto next_flag;
      case '0':
        if (flags == 0) {
          zero_pad = true;
        }
        scan = format;
        goto next_flag;
      case '1':
      case '2':
      case '3':
      case '4':
      case '5':
      case '6':
      case '7':
      case '8':
      case '9': {
        int32_t number;

        for (;;) {
          number = conversion_char - '0';
          conversion_char = (uint8_t)*format;
          if (conversion_char > 0x7f ||
              (ctype[conversion_char] & LIBC_CTYPE_DIGIT) == 0) {
            break;
          }
          conversion_char += number * 10;
          format++;
        }
        if ((flags & LIBC_PRINT_PRECISION) == 0) {
          width = number;
        } else {
          precision = number;
        }
        scan = format;
        goto next_flag;
      }
      case 'L':
        scan = format;
        goto next_flag;
      case 'h':
        short_size = true;
        scan = format;
        goto next_flag;
      case 'l':
        long_size = true;
        scan = format;
        goto next_flag;
      case 'c':
        conversion[0] = (char)*next_argument++;
        start = conversion;
        end = conversion + 1;
        break;
      case 'n':
        if (short_size) {
          *(uint16_t*)*next_argument++ = written;
        } else {
          *(uint32_t*)*next_argument++ = written;
        }
        continue;
      case 'd':
      case 'i': {
        uint32_t value = *next_argument++;

        start = number_end;
        if ((int32_t)value < 0) {
          prefix = "-";
          prefix_length = 1;
          if (value == LIBC_SIGN) {
            conversion[10] = libc_next_decimal_digit(&value);
            start = conversion + 10;
          } else {
            value = -value;
          }
        } else if (plus) {
          prefix = "+";
          prefix_length = 1;
        } else if (space) {
          prefix = " ";
          prefix_length = 1;
        }
        end = number_end;
        start = libc_print_decimal(value, start,
                                   (flags & LIBC_PRINT_PRECISION) != 0);
        if ((flags & LIBC_PRINT_PRECISION) != 0) {
          const int32_t more = precision - (end - start);

          zeros = more > 0;
          if (zeros) {
            leading_zeros = more;
            extra = more;
          }
          zero_pad = false;
        }
        break;
      }
      case 'u': {
        uint32_t value = *next_argument++;

        if (short_size) {
          value &= 0xffff;
        }
        start = number_end;
        if ((value & LIBC_SIGN) != 0) {
          conversion[10] = libc_next_decimal_digit(&value);
          start = conversion + 10;
        }
        end = number_end;
        start = libc_print_decimal(value, start,
                                   (flags & LIBC_PRINT_PRECISION) != 0);
        if ((flags & LIBC_PRINT_PRECISION) != 0) {
          const int32_t more = precision - (end - start);

          zeros = more > 0;
          if (zeros) {
            leading_zeros = more;
            extra = more;
          }
          zero_pad = false;
        }
        break;
      }
      case 'o':
      case 'X':
      case 'x': {
        /* The digit's mask, and its bits less the one shifted first. */
        const uint32_t mask = conversion_char == 'o' ? 7 : 0xf;
        const uint32_t shift = conversion_char == 'o' ? 2 : 3;
        const char* hex =
            conversion_char == 'X' ? "0123456789ABCDEF" : "0123456789abcdef";
        uint32_t value = *next_argument++;

        if (short_size) {
          value &= 0xffff;
        }
        end = number_end;
        start = end;
        if (value == 0) {
          if ((flags & LIBC_PRINT_PRECISION) == 0) {
            leading_zeros = 1;
            extra = 1;
            zeros = true;
          }
        } else {
          for (uint32_t rest = value; rest != 0; rest = rest >> 1 >> shift) {
            *--start = hex[rest & mask];
          }
        }
        if ((flags & LIBC_PRINT_PRECISION) != 0) {
          const int32_t more = precision - (end - start);

          if (more > 0) {
            zeros = true;
            leading_zeros = more;
            extra = more;
          }
          zero_pad = false;
        }
        if (alternate && value != 0) {
          if (conversion_char == 'X') {
            prefix = "0X";
            prefix_length = 2;
          } else if (conversion_char == 'o') {
            if (!zeros) {
              leading_zeros = 1;
              extra = 1;
              zeros = true;
            }
          } else {
            prefix = "0x";
            prefix_length = 2;
          }
        }
        break;
      }
      case 'P':
      case 'p': {
        static const int32_t POINTER_DIGITS = 8;
        static const int32_t DIGIT_BITS = 4;
        static const uint32_t DIGIT_MASK = 0xf;
        const char* hex =
            conversion_char == 'P' ? "0123456789ABCDEF" : "0123456789abcdef";
        int32_t rest = *next_argument++;

        end = number_end;
        start = end;
        for (int32_t left = POINTER_DIGITS; left > 0; left--) {
          *--start = hex[rest & DIGIT_MASK];
          rest >>= DIGIT_BITS;
        }
        if ((flags & LIBC_PRINT_PRECISION) != 0) {
          const int32_t more = precision - (end - start);

          if (more > 0) {
            zeros = true;
            leading_zeros = more;
            extra = more;
          }
        }
        if (alternate) {
          prefix = conversion_char == 'p' ? "0x" : "0X";
          prefix_length = 2;
        }
        break;
      }
      case 's':
        start = (char*)*next_argument++;
        end = start;
        if (start != 0) {
          if ((flags & LIBC_PRINT_PRECISION) == 0) {
            while (*end != 0) {
              end++;
            }
          } else {
            while (*end != 0 && precision-- > 0) {
              end++;
            }
          }
        }
        break;
      case 'S':
        start = (char*)*next_argument++;
        end = start;
        if (start != 0) {
          if ((flags & LIBC_PRINT_PRECISION) == 0) {
            while (*end != 0) {
              end++;
            }
          } else {
            for (; *end != 0 && precision > 0; precision--) {
              end++;
            }
            if (precision < 0) {
              string_short = true;
              end--;
            }
          }
        }
        break;
      case 'E':
      case 'G':
      case 'e':
      case 'f':
      case 'g': {
        const uint32_t high = next_argument[0];
        const uint32_t low = next_argument[1];
        const double value = *(double*)next_argument;
        const bool nonzero = low != 0 || (high & DOUBLE_MAGNITUDE) != 0;
        int32_t decimal_exponent;
        uint32_t negative;
        bool fixed;

        if ((flags & LIBC_PRINT_PRECISION) == 0) {
          precision = DEFAULT_PRECISION;
        } else if (precision == 0 && (conversion_char | 0x20) == 'g') {
          precision = 1;
        }
        if (conversion_char == 'f') {
          fraction_most = FRACTION_MOST;
          significant = SIGNIFICANT_MOST;
          libc_double_to_digits(
              value, precision < FRACTION_MOST ? precision : FRACTION_MOST,
              &decimal_exponent, &negative, digits, 0);
          fixed = true;
        } else if ((conversion_char | 0x20) == 'e') {
          libc_double_to_digits(value,
                                precision + 1 < SIGNIFICANT_MOST
                                    ? precision + 1
                                    : SIGNIFICANT_MOST,
                                &decimal_exponent, &negative, digits, 1);
          fixed = false;
        } else {
          int32_t count = precision;

          fraction_most = FRACTION_MOST;
          significant = SIGNIFICANT_MOST;
          libc_double_to_digits(
              value,
              precision > SIGNIFICANT_MOST - 1 ? SIGNIFICANT_MOST : precision,
              &decimal_exponent, &negative, digits, 1);
          if (!nonzero) {
            decimal_exponent = 1;
          }
          if (!alternate) {
            const int32_t length = libc_length(digits);

            if (length < precision) {
              count = length;
            }
            while (count > 0 && digits[count - 1] == '0') {
              count--;
            }
          }
          fixed = decimal_exponent >= FIXED_LEAST &&
                  decimal_exponent < precision + 1;
          precision = fixed ? count - decimal_exponent : count - 1;
        }
        next_argument += 2;
        if (fixed) {
          /* The fixed-point form: no sign for a negative that rounds to
           * zero. */
          if (negative != 0 && decimal_exponent > -precision &&
              digits[0] != '0') {
            prefix = "-";
            prefix_length = 1;
          } else if (plus) {
            prefix = "+";
            prefix_length = 1;
          } else if (space) {
            prefix = " ";
            prefix_length = 1;
          }
          const char* digit = digits;
          int32_t used = 0;
          char* to = conversion;
          int32_t left = decimal_exponent;

          do {
            if (left < 1 || *digit == 0 || used >= significant) {
              *to++ = '0';
            } else {
              used++;
              *to++ = *digit++;
            }
            left--;
          } while (left > 0);
          if (alternate || precision > 0) {
            *to++ = '.';
          }
          int32_t fraction =
              precision < fraction_most ? precision : fraction_most;

          if (fraction < precision) {
            flags |= LIBC_PRINT_TRAILING_ZEROS;
            trailing_zeros = precision - fraction;
            extra = trailing_zeros;
          }
          while (--fraction >= 0) {
            decimal_exponent++;
            if (decimal_exponent < 1 || *digit == 0 || used >= significant) {
              *to++ = '0';
            } else {
              used++;
              *to++ = *digit++;
            }
          }
          start = conversion;
          end = to;
        } else {
          /* The exponent form, d.ddde+dd. */
          if (negative != 0) {
            prefix = "-";
            prefix_length = 1;
          } else if (plus) {
            prefix = "+";
            prefix_length = 1;
          } else if (space) {
            prefix = " ";
            prefix_length = 1;
          }
          const char* digit = digits;
          char* to = conversion + 1;

          if (digits[0] == 0) {
            conversion[0] = '0';
          } else {
            conversion[0] = *digit++;
          }
          if (precision != 0 || alternate) {
            *to++ = '.';
          }
          int32_t left = precision;

          for (; left > 0 && *digit != 0; digit++, left--) {
            *to++ = *digit;
          }
          if (left > 0) {
            trailing_zeros = left;
            extra = left;
            flags |= LIBC_PRINT_TRAILING_ZEROS;
          }
          start = conversion;
          end = to;
          exponent_text = exponent + 5;
          exponent[5] = 0;
          if (nonzero) {
            int32_t power = decimal_exponent - 1;

            if (power < 0) {
              power = -power;
            }
            for (; power > 9; power /= 10) {
              *--exponent_text = '0' + power % 10;
            }
            *--exponent_text = '0' + power;
          }
          while (exponent_text > exponent + 3) {
            *--exponent_text = '0';
          }
          exponent_text[-1] = decimal_exponent < 1 && nonzero ? '-' : '+';
          exponent_text -= 2;
          *exponent_text =
              (ctype[conversion_char] & LIBC_CTYPE_UPPER) != 0 ? 'E' : 'e';
          exponent_length = exponent + 5 - exponent_text;
          extra += exponent_length;
          flags |= LIBC_PRINT_EXPONENT;
        }
        break;
      }
      default:
        /* Not a conversion: printed as itself. */
        format = scan;
        continue;
    }
    const int32_t length = end - start;
    int32_t total = extra + prefix_length + length;

    if (total < width) {
      counted += width;
      string_short = false;
      if (zero_pad) {
        if (zeros) {
          leading_zeros += width - total;
        } else {
          zeros = true;
          leading_zeros = width - total;
        }
        total = width;
      } else if ((flags & LIBC_PRINT_LEFT) == 0) {
        written += libc_print_run(SPACES, width - total, put, output);
      }
    } else {
      counted += total;
    }
    if (prefix_length != 0) {
      written += prefix_length;
      put((char*)prefix, 1, prefix_length, output);
    }
    if (zeros) {
      if (string_short) {
        leading_zeros++;
      }
      written += libc_print_run(ZEROS, leading_zeros, put, output);
    }
    if (length > 0) {
      written += length;
      put(start, 1, length, output);
    }
    if ((flags & LIBC_PRINT_TRAILING_ZEROS) != 0) {
      written += libc_print_run(ZEROS, trailing_zeros, put, output);
    }
    if ((flags & LIBC_PRINT_EXPONENT) != 0) {
      written += exponent_length;
      put(exponent_text, 1, exponent_length, output);
    }
    if ((flags & LIBC_PRINT_LEFT) != 0 && total < width) {
      written += libc_print_run(SPACES, width - total, put, output);
    }
  }
}
