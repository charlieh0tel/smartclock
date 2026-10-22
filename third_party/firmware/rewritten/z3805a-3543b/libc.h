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

/* float_raise's code (libc.md, "Exception codes"): the high word names
 * the routine, here the math library's (LIBC_RAISE_MATH and its
 * index), the low word the cause. */
enum {
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

/* The long above a routine's first argument in its frame: A6's saved
 * value, the return address and the argument come first; what
 * float_exception_report passes fatal_error_with_code as its code. */
static const int32_t LIBC_FRAME_LONG_ABOVE_CODE = 3;

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
