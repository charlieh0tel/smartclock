/* The compiler's C library (docs/firmware/libc.md), written by hand in
 * place of the decompiler's (docs/firmware/README.md, "Hand-written
 * C").  The exported C includes this file after its own, so the types
 * and prototypes it declares are in scope; each function here keeps
 * its generated prototype.
 *
 * The image has no FPU.  Floating arithmetic here is a call to the
 * image's soft-float routines (docs/firmware/softfloat.md), never a C
 * operator, which gcc would hand to libgcc's; and the math routines
 * work on a double's bits, as the image's do, through union
 * libc_double. */

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

/* Fields of a double's high word, and of its 64 bits; an infinity's
 * high word is the exponent's mask, all ones. */
static const uint32_t LIBC_SIGN = 0x80000000u;
static const uint32_t LIBC_INFINITY_HIGH = 0x7ff00000u;
static const uint64_t LIBC_MANTISSA_MASK = 0x000fffffffffffffull;
static const uint64_t LIBC_IMPLICIT_ONE = 1ull << 52;

static const int32_t LIBC_EXPONENT_SHIFT = 20;
static const int32_t LIBC_EXPONENT_MAX = 0x7ff;
static const int32_t LIBC_EXPONENT_BIAS = 0x3ff;
static const int32_t LIBC_MANTISSA_BITS = 52;
/* The biased exponent of [0.5, 1), what frexp and the routines
 * that reduce their argument to it give a mantissa. */
static const int32_t LIBC_HALF_EXPONENT = 0x3fe;
static const int32_t LIBC_WORD_BITS = 32;

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
  LIBC_CAUSE_INEXACT = 0x200,
};

#define LIBC_RAISE_CODE(routine, cause) \
  ((uint32_t)(LIBC_RAISE_MATH | (routine)) << LIBC_WORD_BITS / 2 | (cause))

/* float_raise's result, a double in D0:D1, as the uint64_t the export
 * declares it to return. */
static double libc_from_bits(uint64_t bits) {
  union libc_double result = {.bits = bits};

  return result.value;
}

/* Whether a double's bits are a NaN. */
static bool libc_is_nan(union libc_double x) {
  uint32_t magnitude = x.word.high & ~LIBC_SIGN;

  return magnitude > LIBC_INFINITY_HIGH ||
         (magnitude == LIBC_INFINITY_HIGH && x.word.low != 0);
}

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

/* |j| (0x5ec9e). */
int32_t abs(int32_t j) { return j < 0 ? -j : j; }

/* A decimal integer after any spaces and a sign (0x5ecfa).  It counts
 * down from zero, so that the most negative int parses. */
int32_t atoi(char* s) {
  /* ctype_table's bits (libc.md). */
  static const uint8_t CTYPE_DIGIT = 0x04;
  static const uint8_t CTYPE_SPACE = 0x08;
  bool negative = false;
  int32_t c = *s;

  if ((ctype_table[c + 1] & CTYPE_DIGIT) == 0) {
    while ((ctype_table[c + 1] & CTYPE_SPACE) != 0) {
      c = *++s;
    }
    if (c == '-') {
      negative = true;
      c = *++s;
    } else if (c == '+') {
      c = *++s;
    }
    if ((ctype_table[c + 1] & CTYPE_DIGIT) == 0) {
      return 0;
    }
  }
  int32_t value = '0' - c;

  for (c = *++s; (ctype_table[c + 1] & CTYPE_DIGIT) != 0; c = *++s) {
    value = value * 10 + ('0' - c);
  }
  return negative ? value : -value;
}

/* The quotient in the high word, D0, and the remainder in the low, D1
 * (0x5eea2). */
uint64_t div(int32_t numerator, int32_t denominator) {
  int32_t quotient = numerator / denominator;
  int32_t remainder = numerator - denominator * quotient;

  return (uint64_t)quotient << LIBC_WORD_BITS | (uint32_t)remainder;
}

/* The same, ldiv (0x5eed2). */
int64_t ldiv(int32_t numerator, int32_t denominator) {
  return div(numerator, denominator);
}

/* |j|, labs (0x5ef02). */
int32_t labs(int32_t j) { return abs(j); }

/* The first difference of two blocks' bytes, taken as signed (0x5ef12). */
int32_t memcmp(void* s1, void* s2, uint32_t n) {
  int8_t* a = s1;
  int8_t* b = s2;

  if (a == b) {
    return 0;
  }
  for (; n != 0; n--, a++, b++) {
    int32_t difference = *a - *b;

    if (difference != 0) {
      return difference;
    }
  }
  return 0;
}

/* Copies n bytes (0x5ef48). */
void* memcpy(void* s1, void* s2, uint32_t n) {
  uint8_t* to = s1;
  uint8_t* from = s2;

  for (; n != 0; n--) {
    *to++ = *from++;
  }
  return s1;
}

/* Fills n bytes (0x5ef66). */
void* memset(void* s, int32_t c, uint32_t n) {
  uint8_t* to = s;

  for (; n != 0; n--) {
    *to++ = c;
  }
  return s;
}

/* Swaps two of qsort's elements (0x5ef86). */
void qsort_swap(uint8_t* a, uint8_t* b) {
  uint32_t count = qsort_size;

  do {
    uint8_t byte = *a;

    *a++ = *b;
    *b++ = byte;
  } while (--count != 0);
}

/* Rotates three of qsort's elements: a's to b, b's to c, c's to a
 * (0x5efa0). */
void qsort_rotate(uint8_t* a, uint8_t* b, uint8_t* c) {
  uint32_t count = qsort_size;

  do {
    uint8_t byte = *a;

    *a++ = *c;
    *c++ = *b;
    *b++ = byte;
  } while (--count != 0);
}

/* Sorts the elements from first up to end by qsort_compare, a
 * three-way partition about the middle element, then the smaller side
 * by recursion and the larger by iteration (0x5efc4).  While it
 * partitions, [first, low) is less than the pivot, [equal_low,
 * equal_high] equal to it and (high, end) greater; [low, equal_low)
 * and (equal_high, high] are yet to be compared. */
void qsort_range(uint8_t* first, uint8_t* end) {
  uint32_t size = qsort_size;

  while ((uint32_t)(end - first) > size) {
    uint8_t* low = first;
    uint8_t* equal_low = first + size * ((uint32_t)(end - first) / (size * 2));
    uint8_t* equal_high = equal_low;
    uint8_t* high = end - size;

    for (;;) {
      if (low < equal_low) {
        int32_t order = qsort_compare(low, equal_low);

        if (order == 0) {
          equal_low -= size;
          qsort_swap(low, equal_low);
          continue;
        }
        if (order < 0) {
          low += size;
          continue;
        }
      }
      /* *low, if below equal_low, belongs above the pivot. */
      bool exchanged = false;

      while (!exchanged && equal_high < high) {
        int32_t order = qsort_compare(equal_high, high);

        if (order == 0) {
          equal_high += size;
          qsort_swap(equal_high, high);
        } else if (order < 0) {
          high -= size;
        } else if (low != equal_low) {
          qsort_swap(low, high);
          high -= size;
          low += size;
          exchanged = true;
        } else {
          equal_high += size;
          qsort_rotate(low, equal_high, high);
          low = equal_low = equal_low + size;
        }
      }
      if (exchanged) {
        continue;
      }
      if (low == equal_low) {
        break;
      }
      equal_low -= size;
      qsort_rotate(high, equal_low, low);
      equal_high -= size;
      high = equal_high;
    }
    if (equal_low - first < end - equal_high) {
      qsort_range(first, equal_low);
      first = equal_high + size;
    } else {
      qsort_range(equal_high + size, end);
      end = equal_low;
    }
  }
}

/* Sorts nmemb elements of size bytes by compar, leaving compar and
 * size where qsort_range reads them (0x5f0cc). */
void qsort(void* base, uint32_t nmemb, uint32_t size, void* compar) {
  qsort_compare = compar;
  qsort_size = size;
  qsort_range(base, (uint8_t*)base + size * nmemb);
}

/* A pseudo-random number in 0 to 32767 (0x5f102). */
int32_t rand(void) {
  /* rand's generator, the ANSI C example's. */
  static const uint32_t RAND_MULTIPLIER = 1103515245;
  static const uint32_t RAND_INCREMENT = 12345;
  static const uint32_t RAND_SHIFT = 16;
  static const uint32_t RAND_MASK = 0x7fff;

  rand_seed = rand_seed * RAND_MULTIPLIER + RAND_INCREMENT;
  return rand_seed >> RAND_SHIFT & RAND_MASK;
}

/* Appends s2 to s1 (0x5f128). */
char* strcat(char* s1, char* s2) {
  char* end = s1;

  while (*end != '\0') {
    end++;
  }
  while ((*end++ = *s2++) != '\0') {
  }
  return s1;
}

/* The first c in s, its NUL among them (0x5f140). */
char* strchr(char* s, int32_t c) {
  do {
    if (*s == (char)c) {
      return s;
    }
  } while (*s++ != '\0');
  return NULL;
}

/* Compares at most n characters, as signed (0x5f15a). */
int32_t strncmp(char* s1, char* s2, uint32_t n) {
  for (; n != 0 && *s1 == *s2; n--, s1++, s2++) {
    if (*s1 == '\0') {
      return 0;
    }
  }
  if (n == 0) {
    return 0;
  }
  return *s1 - *s2;
}

/* Copies at most n characters and pads with NULs; a string it
 * truncates it leaves unterminated (0x5f190). */
char* strncpy(char* s1, char* s2, uint32_t n) {
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

/* A lower-case ASCII letter in upper case (0x5f1be). */
int32_t toupper(int32_t c) {
  if (c >= 'a' && c <= 'z') {
    c -= 'a' - 'A';
  }
  return c;
}

/* '0' plus *n % 10 in the high word, D0, and *n divided by ten, which
 * it stores back, in the low, D1 (0x5fad6). */
uint64_t next_decimal_digit(uint32_t* n) {
  uint32_t value = *n;
  uint32_t digit = '0' + value % 10;

  *n = value / 10;
  return (uint64_t)digit << LIBC_WORD_BITS | *n;
}

/* |x|; a NaN raises (0x6092a). */
double fabs(double x) {
  union libc_double v = {.value = x};

  if (libc_is_nan(v)) {
    return libc_from_bits(
        float_raise(LIBC_RAISE_CODE(LIBC_RAISE_FABS, LIBC_CAUSE_SIGNALING_NAN),
                    v.word.high, v.word.low));
  }
  v.word.high &= ~LIBC_SIGN;
  return v.value;
}

/* value as a mantissa in [0.5, 1) and a power of two (0x6098a).  A
 * subnormal it normalizes first, an exponent field of 1 less its
 * shift (0x60974-0x60986). */
double frexp(double value, int32_t* exponent) {
  static const uint32_t MANTISSA_HIGH_MASK = 0x000fffffu;
  union libc_double v = {.value = value};
  uint32_t sign = v.word.high & LIBC_SIGN;
  uint32_t magnitude = v.word.high & ~LIBC_SIGN;
  int16_t field = magnitude >> LIBC_EXPONENT_SHIFT;

  if (field == 0) {
    uint64_t mantissa = v.bits & ~((uint64_t)LIBC_SIGN << LIBC_WORD_BITS);

    if (mantissa == 0) {
      *exponent = 0;
      return value;
    }
    field = 1 - libc_normalize(&mantissa);
    v.bits = mantissa;
  } else if (field == LIBC_EXPONENT_MAX) {
    *exponent = 0;
    if (magnitude == LIBC_INFINITY_HIGH && v.word.low == 0) {
      return libc_from_bits(float_raise(
          LIBC_RAISE_CODE(LIBC_RAISE_FREXP, LIBC_CAUSE_OPERAND_ERROR)));
    }
    return libc_from_bits(
        float_raise(LIBC_RAISE_CODE(LIBC_RAISE_FREXP, LIBC_CAUSE_SIGNALING_NAN),
                    v.word.high, v.word.low));
  }
  *exponent = (int16_t)(field - LIBC_HALF_EXPONENT);
  v.word.high = sign | (uint32_t)LIBC_HALF_EXPONENT << LIBC_EXPONENT_SHIFT |
                (v.word.high & MANTISSA_HIGH_MASK);
  return v.value;
}

/* value's fraction, its integral part through iptr, both with value's
 * sign (0x60d94). */
double modf(double value, double* iptr) {
  union libc_double v = {.value = value};
  uint32_t magnitude = v.word.high & ~LIBC_SIGN;

  if (magnitude >= LIBC_INFINITY_HIGH) {
    if (magnitude == LIBC_INFINITY_HIGH && v.word.low == 0) {
      double fraction = libc_from_bits(float_raise(
          LIBC_RAISE_CODE(LIBC_RAISE_MODF, LIBC_CAUSE_OPERAND_ERROR)));

      *iptr = value;
      return fraction;
    }
    double nan = libc_from_bits(
        float_raise(LIBC_RAISE_CODE(LIBC_RAISE_MODF, LIBC_CAUSE_SIGNALING_NAN),
                    v.word.high, v.word.low));

    *iptr = nan;
    return nan;
  }
  /* The number of the mantissa's bits, with the implicit one, at or
   * above the units' place. */
  int32_t whole_bits =
      (int32_t)(magnitude >> LIBC_EXPONENT_SHIFT) - LIBC_HALF_EXPONENT;

  if (whole_bits <= 0) {
    union libc_double zero = {.bits = (uint64_t)(v.word.high & LIBC_SIGN)
                                      << LIBC_WORD_BITS};

    *iptr = zero.value;
    return value;
  }
  union libc_double whole = v;

  if (whole_bits <= LIBC_MANTISSA_BITS) {
    whole.bits &= ~0ull << (LIBC_MANTISSA_BITS + 1 - whole_bits);
  }
  *iptr = whole.value;

  /* whole - value, the fraction negated, then its sign turned back;
   * a result whose high word is zero is left as it is. */
  union libc_double fraction = {
      .value = double_subtract_reversed(value, whole.value)};

  if (fraction.word.high != 0) {
    fraction.word.high ^= LIBC_SIGN;
  }
  return fraction.value;
}

/* Whether the double high:low is an integer, and through odd whether
 * it is an odd one; zero is not an integer to it (0x60e68). */
uint32_t double_is_integer(uint32_t high, uint32_t low, uint32_t* odd) {
  /* The mantissa's bits below the units' place. */
  int32_t fraction_bits =
      LIBC_EXPONENT_BIAS + LIBC_MANTISSA_BITS -
      (int32_t)(high >> LIBC_EXPONENT_SHIFT & LIBC_EXPONENT_MAX);
  union libc_double v = {.word = {high, low}};

  *odd = 0;
  if (fraction_bits < 0) {
    return 1;
  }
  if (fraction_bits > LIBC_MANTISSA_BITS) {
    return 0;
  }
  uint64_t units = 1ull << fraction_bits;

  if ((v.bits & (units - 1)) != 0) {
    return 0;
  }
  *odd = (v.bits & units) != 0;
  return 1;
}

/* value with its mantissa cleared below 1/16; a value under 1/16 is
 * +0 (0x60efa). */
double double_truncate_sixteenths(double value) {
  static const int32_t SIXTEENTHS_BITS = 4;
  union libc_double v = {.value = value};
  int32_t drop = LIBC_EXPONENT_BIAS + LIBC_MANTISSA_BITS - SIXTEENTHS_BITS -
                 (int32_t)((v.word.high & ~LIBC_SIGN) >> LIBC_EXPONENT_SHIFT);

  if (drop <= 0) {
    return value;
  }
  if (drop > LIBC_MANTISSA_BITS) {
    return 0.0;
  }
  v.bits &= ~0ull << drop;
  return v.value;
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

/* Adds n to a double's binary exponent, n << 20 to its high word:
 * multiplies a finite, normal x by 2^n. */
static double libc_scale(double x, int32_t n) {
  union libc_double v = {.value = x};

  v.word.high += (uint32_t)n << LIBC_EXPONENT_SHIFT;
  return v.value;
}

/* x's mantissa as a double in [0.5, 1), a subnormal's normalized, and
 * through exponent the power of two it was scaled by: x is the result
 * times 2^exponent.  For a finite x > 0; sqrt's and log's start. */
static double libc_reduce(union libc_double x, int32_t* exponent) {
  uint32_t field = (x.word.high & ~LIBC_SIGN) >> LIBC_EXPONENT_SHIFT;
  uint64_t mantissa = x.bits & LIBC_MANTISSA_MASK;
  union libc_double reduced;

  if (field == 0) {
    field = 1 - libc_normalize(&mantissa);
  }
  *exponent = (int32_t)field - LIBC_HALF_EXPONENT;
  reduced.bits = (mantissa & LIBC_MANTISSA_MASK) | (uint64_t)LIBC_HALF_EXPONENT
                                                       << LIBC_MANTISSA_BITS;
  return reduced.value;
}

/* The natural log (0x60a10): of the argument reduced to [0.5, 1), and
 * then to [sqrt(1/2), sqrt(2)), z = 2(r - 1)/(r + 1) and a rational
 * function of z^2, plus the exponent times ln 2 in two parts.  Of zero
 * -infinity, with errno ERANGE. */
double log(double x) {
  static const int32_t ERANGE = 34;
  /* sqrt(1/2), where log and pow choose how to reduce their argument. */
  static const double SQRT_HALF = 0.7071067811865476;
  /* ln 2 in a high part and a low, and the rational approximation's
   * coefficients: the image's log_ln2_high to log_b2 (0x60d48 to
   * 0x60d80). */
  static const double LN2_HIGH = 0.693359375;
  static const double LN2_LOW = -0.00021219444005469057;
  static const double A0 = -64.12494342374558;
  static const double A1 = 16.383943563021536;
  static const double A2 = -0.7895611288749126;
  static const double B0 = -769.4993210849487;
  static const double B1 = 312.03222091924533;
  static const double B2 = -35.66797773903465;
  union libc_double v = {.value = x};

  if (libc_is_nan(v)) {
    return libc_from_bits(
        float_raise(LIBC_RAISE_CODE(LIBC_RAISE_LOG, LIBC_CAUSE_SIGNALING_NAN),
                    v.word.high, v.word.low));
  }
  bool zero = (v.bits & ~((uint64_t)LIBC_SIGN << LIBC_WORD_BITS)) == 0;

  if (zero || (v.word.high & LIBC_SIGN) != 0) {
    double result = libc_from_bits(
        float_raise(LIBC_RAISE_CODE(LIBC_RAISE_LOG, LIBC_CAUSE_OPERAND_ERROR)));

    if (zero) {
      union libc_double minus_infinity = {
          .bits = (uint64_t)(LIBC_SIGN | LIBC_INFINITY_HIGH) << LIBC_WORD_BITS};

      result = minus_infinity.value;
      errno = ERANGE;
    }
    return result;
  }
  if (v.word.high == LIBC_INFINITY_HIGH && v.word.low == 0) {
    return x;
  }

  int32_t exponent;
  double reduced = libc_reduce(v, &exponent);
  double z;

  if (double_compare(SQRT_HALF, reduced) < 1) {
    exponent--;
    double numerator = double_add(-0.5, reduced);
    double denominator = double_add(0.5, double_multiply(0.5, numerator));

    z = double_divide_reversed(denominator, numerator);
  } else {
    double numerator = double_add(-0.5, double_add(-0.5, reduced));
    double denominator = double_add(0.5, libc_scale(reduced, -1));

    z = double_divide_reversed(denominator, numerator);
  }
  double w = double_multiply(z, z);
  double scale = integer_to_double(exponent);
  double p = double_multiply(w, A2);

  p = double_add(p, A1);
  p = double_multiply(p, w);
  p = double_add(p, A0);
  p = double_multiply(p, w);
  double q = double_add(w, B2);

  q = double_multiply(q, w);
  q = double_add(q, B1);
  q = double_multiply(q, w);
  q = double_add(q, B0);
  double result = double_divide_reversed(q, p);

  result = double_multiply(result, z);
  result = double_add(result, z);
  result = double_add(result, double_multiply(LN2_LOW, scale));
  return double_add(double_multiply(LN2_HIGH, scale), result);
}

/* The square root (0x6191c): of the argument reduced to [0.5, 1), a
 * linear seed and three Newton steps, the second folding in the
 * halving; an odd exponent then multiplied by sqrt(1/2), in two parts,
 * and the exponent halved. */
double sqrt(double x) {
  /* The seed's coefficients and sqrt(1/2) in a high part and a low:
   * the image's sqrt_seed_a to sqrt_half_low (0x61b94 to 0x61bac). */
  static const double SEED_A = 0.41731;
  static const double SEED_B = 0.59016;
  static const double HALF_HIGH = 0.7071067809592932;
  static const double HALF_LOW = 2.2725427813161828e-10;
  union libc_double v = {.value = x};

  if (libc_is_nan(v)) {
    return libc_from_bits(
        float_raise(LIBC_RAISE_CODE(LIBC_RAISE_SQRT, LIBC_CAUSE_SIGNALING_NAN),
                    v.word.high, v.word.low));
  }
  if ((v.bits & ~((uint64_t)LIBC_SIGN << LIBC_WORD_BITS)) == 0 ||
      (v.word.high == LIBC_INFINITY_HIGH && v.word.low == 0)) {
    return x;
  }
  if ((v.word.high & LIBC_SIGN) != 0) {
    return libc_from_bits(float_raise(
        LIBC_RAISE_CODE(LIBC_RAISE_SQRT, LIBC_CAUSE_OPERAND_ERROR)));
  }

  int32_t exponent;
  double reduced = libc_reduce(v, &exponent);
  double root = double_add(double_multiply(reduced, SEED_B), SEED_A);

  root = double_add(double_divide_reversed(root, reduced), root);
  root =
      double_add(double_divide_reversed(root, reduced), libc_scale(root, -2));
  root = double_add(double_divide_reversed(root, reduced), root);

  uint32_t scale = exponent - 2;

  if ((scale & 1) != 0) {
    scale = exponent - 1;
    /* root as a high part, its high word, and the rest. */
    union libc_double high = {
        .word = {.high = ((union libc_double){.value = root}).word.high}};
    double rest = double_subtract_reversed(high.value, root);
    double sum = double_add(double_multiply(HALF_HIGH, rest),
                            double_multiply(HALF_LOW, rest));

    sum = double_add(double_multiply(HALF_LOW, high.value), sum);
    root = double_add(double_multiply(HALF_HIGH, high.value), sum);
  }
  return libc_scale(root, (int32_t)scale / 2);
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

/* Whether a double's bits are a zero, of either sign. */
static bool libc_is_zero(union libc_double x) {
  return ((x.word.high & ~LIBC_SIGN) | x.word.low) == 0;
}

/* Whether a double's bits are an infinity, of either sign. */
static bool libc_is_infinity(union libc_double x) {
  return (x.word.high & ~LIBC_SIGN) == LIBC_INFINITY_HIGH && x.word.low == 0;
}

/* x to the power y (0x60f5e), by Cody and Waite's method: log2 x in
 * sixteenths, from x's mantissa reduced by a power of 2^(1/16) and a
 * polynomial; y times it in high and low parts, each a multiple of
 * 1/16 and the rest; and 2 to that by a power of 2^(1/16) times a
 * polynomial, the exponent added last.  A negative x it takes only to
 * an integer power, the result negated for an odd one.  Its special
 * cases, a zero, an infinity or a NaN, it settles first. */
double pow(double x, double y) {
  /* 2^(-k/16), k from 0 to 16, and for an odd k at (k + 1) / 2 the rest
   * of 2^(-k/16) that it leaves out: the image's pow_two_powers
   * (0x6184c) and pow_two_power_tails (0x618d4). */
  static const double TWO_POWERS[] = {1.0,
                                      0.9576032806985737,
                                      0.9170040432046712,
                                      0.8781260801866497,
                                      0.8408964152537145,
                                      0.8052451659746271,
                                      0.7711054127039704,
                                      0.7384130729697497,
                                      0.7071067811865476,
                                      0.6771277734684463,
                                      0.6484197773255048,
                                      0.620928906036742,
                                      0.5946035575013605,
                                      0.5693943173783458,
                                      0.5452538663326288,
                                      0.5221368912137069,
                                      0.5};
  static const double TWO_POWER_TAILS[] = {0.0,
                                           -2.65498651404579e-17,
                                           7.400351738593444e-18,
                                           6.1767981423511124e-18,
                                           -8.709986392171569e-18,
                                           1.925237094945093e-17,
                                           1.1645068979529732e-17,
                                           2.228203169026117e-17,
                                           2.137972426376836e-17};
  /* The polynomials' coefficients, log2 of 1 + v and 2^w, and log2(e)
   * - 1: the image's pow_log_p1 to pow_exp2_q7 and log2_e_minus_1
   * (0x617e4 to 0x6183c). */
  static const double LOG_P1 = 0.08333333333333322;
  static const double LOG_P2 = 0.012500000000503799;
  static const double LOG_P3 = 0.002232142128592426;
  static const double LOG_P4 = 0.0004344577567216312;
  static const double EXP2_Q1 = 0.6931471805599453;
  static const double EXP2_Q2 = 0.24022650695909537;
  static const double EXP2_Q3 = 0.055504108664085595;
  static const double EXP2_Q4 = 0.009618129059517241;
  static const double EXP2_Q5 = 0.0013333541313585784;
  static const double EXP2_Q6 = 0.00015400290440989765;
  static const double EXP2_Q7 = 1.4928852680595609e-05;
  static const double LOG2_E_MINUS_1 = 0.4426950408889634;
  /* The result of 0 to a power 0 or negative, a NaN by its bits: the
   * image's pow_error_result (0x60d88). */
  static const union libc_double ERROR_RESULT = {.bits = 0x7fffffffffffffffu};
  static const int32_t POW_SIXTEENTHS = 16;
  static const int32_t POW_SQRT_HALF = 8;
  /* float_raise's underflow takes the result denormalized: the shift
   * that denormalizes it, its high and low words and a word of the
   * bits below them, whether any is set (0x5e990).  A shift past the
   * mantissa's bits, more than 0x37, gives zero (0x5e9f0). */
  static const int32_t POW_UNDERFLOW_TO_ZERO = 100;
  static const int32_t POW_INEXACT_BITS = 1;
  /* float_raise's causes, for float_control's trap enables. */
  static const uint16_t POW_UNTRAPPED =
      ~(LIBC_CAUSE_OPERAND_ERROR | LIBC_CAUSE_OVERFLOW | LIBC_CAUSE_UNDERFLOW |
        LIBC_CAUSE_INEXACT);
  union libc_double base = {.value = x};
  union libc_double power = {.value = y};
  union libc_double one = {.value = 1.0};
  union libc_double result = {.bits = (uint64_t)LIBC_INFINITY_HIGH
                                      << LIBC_WORD_BITS};
  bool base_negative = (base.word.high & LIBC_SIGN) != 0;
  bool power_negative = (power.word.high & LIBC_SIGN) != 0;
  /* Whether the result is negated: a negative base, an odd power. */
  uint32_t negate = 0;

  if (libc_is_nan(base) || libc_is_nan(power)) {
    union libc_double nan = libc_is_nan(base) ? base : power;

    return libc_from_bits(
        float_raise(LIBC_RAISE_CODE(LIBC_RAISE_POW, LIBC_CAUSE_SIGNALING_NAN),
                    nan.word.high, nan.word.low));
  }

  /* The special cases: their result, and whether they are an operand error. */
  bool special = true;
  bool invalid = false;

  if (libc_is_zero(base) || libc_is_zero(power)) {
    if (libc_is_zero(base) && (libc_is_zero(power) || power_negative)) {
      result.value = ERROR_RESULT.value;
      invalid = true;
    } else {
      result.bits = libc_is_zero(base) ? 0 : one.bits;
    }
  } else if (libc_is_infinity(base)) {
    if (power_negative) {
      result.bits = 0;
    } else if (base_negative) {
      if (libc_is_infinity(power)) {
        invalid = true;
      } else {
        if (!double_is_integer(power.word.high, power.word.low, &negate)) {
          invalid = true;
        }
        if (negate) {
          result.word.high |= LIBC_SIGN;
        }
      }
    }
  } else if (libc_is_infinity(power)) {
    if (base_negative) {
      if (double_compare(-1.0, x) == 0) {
        result = one;
        invalid = true;
      } else if (power_negative) {
        if (double_compare(-1.0, x) > 0) {
          invalid = true;
        } else {
          result.bits = 0;
        }
      } else if (double_compare(-1.0, x) < 0) {
        invalid = true;
      } else {
        result.bits = 0;
      }
    } else if (double_compare(1.0, x) == 0) {
      result = base;
    } else if (power_negative) {
      if (double_compare(1.0, x) > 0) {
        result.bits = 0;
      }
    } else if (double_compare(1.0, x) < 0) {
      result.bits = 0;
    }
  } else {
    special = false;
  }
  if (invalid) {
    float_raise(LIBC_RAISE_CODE(LIBC_RAISE_POW, LIBC_CAUSE_OPERAND_ERROR));
  }
  if (special) {
    return result.value;
  }

  if (base_negative) {
    if (!double_is_integer(power.word.high, power.word.low, &negate)) {
      return libc_from_bits(float_raise(
          LIBC_RAISE_CODE(LIBC_RAISE_POW, LIBC_CAUSE_OPERAND_ERROR)));
    }
    base.word.high &= ~LIBC_SIGN;
  }
  if (base.bits == one.bits) {
    result = one;
  } else {
    /* log2 of the mantissa m: m = 2^(-k/16) (1 + z/2)/(1 - z/2)
     * for the k whose power lies nearest below it, by a binary
     * search, and log2 of that fraction a polynomial in z. */
    int32_t binary_exponent;
    double mantissa = frexp(base.value, &binary_exponent);
    int32_t k = 1;

    if (double_compare(mantissa, TWO_POWERS[POW_SQRT_HALF]) >= 0) {
      k = 9;
    }
    if (double_compare(mantissa, TWO_POWERS[k + 3]) >= 0) {
      k += 4;
    }
    if (double_compare(mantissa, TWO_POWERS[k + 1]) >= 0) {
      k += 2;
    }
    double numerator = double_subtract_reversed(TWO_POWERS[k], mantissa);

    numerator =
        double_subtract_reversed(TWO_POWER_TAILS[(k + 1) >> 1], numerator);
    double z = libc_scale(
        double_divide_reversed(double_add(mantissa, TWO_POWERS[k]), numerator),
        1);
    double w = double_multiply(z, z);
    double t = double_multiply(w, LOG_P4);

    t = double_add(t, LOG_P3);
    t = double_multiply(t, w);
    t = double_add(t, LOG_P2);
    t = double_multiply(t, w);
    t = double_add(t, LOG_P1);
    t = double_multiply(t, w);
    t = double_multiply(t, z);
    t = double_add(double_multiply(t, LOG2_E_MINUS_1), t);
    double log2_fraction =
        double_add(double_add(double_multiply(LOG2_E_MINUS_1, z), t), z);
    /* log2 x less log2_fraction, a whole number of sixteenths. */
    double log2_whole =
        libc_scale(integer_to_double(binary_exponent * POW_SIXTEENTHS - k), -4);

    /* y log2 x, untrapped, as a whole number of sixteenths and the
     * rest, y and the products each split at 1/16. */
    uint16_t saved_status = float_status;
    uint16_t saved_control = float_control;

    float_status = 0;
    float_control = saved_control & POW_UNTRAPPED;
    double y_high = double_truncate_sixteenths(y);
    double y_low = double_subtract_reversed(y_high, y);
    double product = double_multiply(y, log2_fraction);

    product = double_add(double_multiply(y_low, log2_whole), product);
    double high = double_truncate_sixteenths(product);
    double low = double_subtract_reversed(high, product);

    product = double_add(double_multiply(y_high, log2_whole), high);
    high = double_truncate_sixteenths(product);
    low = double_add(double_subtract_reversed(high, product), low);
    double low_sixteenths = double_truncate_sixteenths(low);

    low = double_subtract_reversed(low_sixteenths, low);
    int32_t sixteenths = double_to_integer(double_multiply(
        POW_SIXTEENTHS,
        double_truncate_sixteenths(double_add(high, low_sixteenths))));

    if ((float_status & LIBC_CAUSE_OPERAND_ERROR) != 0) {
      /* Too many sixteenths for an int: the result overflows
       * or underflows, by which side of 1 |x| is.  The status
       * and control words it leaves as they are. */
      base.value = x;
      base.word.high &= ~LIBC_SIGN;
      bool overflow = power_negative ? double_compare(1.0, base.value) < 0
                                     : double_compare(1.0, base.value) > 0;

      if (overflow) {
        result.value = libc_from_bits(
            float_raise(LIBC_RAISE_CODE(LIBC_RAISE_POW, LIBC_CAUSE_OVERFLOW)));
      } else {
        result.value = libc_from_bits(
            float_raise(LIBC_RAISE_CODE(LIBC_RAISE_POW, LIBC_CAUSE_UNDERFLOW),
                        POW_UNDERFLOW_TO_ZERO, 0, 0, POW_INEXACT_BITS));
      }
    } else {
      /* 2^(y log2 x) = 2^scale 2^(-k/16) 2^low, low in
       * (-1/16, 0]. */
      if ((((union libc_double){.value = low}).word.high & LIBC_SIGN) == 0) {
        sixteenths++;
        low = double_add(-0.0625, low);
      }
      int32_t scale = (sixteenths >> 4) + 1;

      k = scale * POW_SIXTEENTHS - sixteenths;
      double r = double_multiply(low, EXP2_Q7);

      r = double_add(r, EXP2_Q6);
      r = double_multiply(r, low);
      r = double_add(r, EXP2_Q5);
      r = double_multiply(r, low);
      r = double_add(r, EXP2_Q4);
      r = double_multiply(r, low);
      r = double_add(r, EXP2_Q3);
      r = double_multiply(r, low);
      r = double_add(r, EXP2_Q2);
      r = double_multiply(r, low);
      r = double_add(r, EXP2_Q1);
      r = double_multiply(r, low);
      result.value =
          double_add(double_multiply(r, TWO_POWERS[k]), TWO_POWERS[k]);

      int32_t field =
          (int32_t)(result.word.high >> LIBC_EXPONENT_SHIFT) + scale;

      float_control = saved_control;
      uint16_t status = float_status;

      float_status = saved_status;
      if ((status & (LIBC_CAUSE_OVERFLOW | LIBC_CAUSE_UNDERFLOW)) == 0 &&
          field >= 1 && field < LIBC_EXPONENT_MAX) {
        if ((status & LIBC_CAUSE_INEXACT) != 0) {
          float_status = saved_status | LIBC_CAUSE_INEXACT;
          if ((float_control & LIBC_CAUSE_INEXACT) != 0) {
            result.value = libc_from_bits(float_raise(
                LIBC_RAISE_CODE(LIBC_RAISE_POW, LIBC_CAUSE_INEXACT)));
          }
        }
        result.word.high += (uint32_t)scale << LIBC_EXPONENT_SHIFT;
      } else if ((status & LIBC_CAUSE_UNDERFLOW) != 0 || field < 1) {
        result.value = libc_from_bits(float_raise(
            LIBC_RAISE_CODE(LIBC_RAISE_POW, LIBC_CAUSE_UNDERFLOW), 1 - field,
            result.word.high, result.word.low, POW_INEXACT_BITS));
      } else {
        result.value = libc_from_bits(
            float_raise(LIBC_RAISE_CODE(LIBC_RAISE_POW, LIBC_CAUSE_OVERFLOW)));
      }
    }
  }
  if (negate) {
    result.word.high |= LIBC_SIGN;
  }
  return result.value;
}
