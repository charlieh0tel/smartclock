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

/* memcpy's and installer_memcpy's body: copies n bytes. */
static void* libc_memcpy(void* s1, void* s2, uint32_t n) {
  uint8_t* to = s1;
  uint8_t* from = s2;

  for (; n != 0; n--) {
    *to++ = *from++;
  }
  return s1;
}

/* strncpy's and installer_strncpy's body: copies at most n characters
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

/* toupper's and installer_toupper's body: a lower-case ASCII letter
 * in upper case. */
static int32_t libc_toupper(int32_t c) {
  if (c >= 'a' && c <= 'z') {
    c -= 'a' - 'A';
  }
  return c;
}

/* next_decimal_digit's and installer_next_decimal_digit's body: '0'
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
