/* What the C library's files share (math.c, stdio.c, stdlib.c,
 * docs/firmware/libc.md): a double's IEEE 754 bits and their fields. The
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

/* Appends a character to a message of a length byte and its text, as
 * float_exception_report and program_end_report build theirs for
 * fatal_error_with_code. */
static void libc_message_append(uint8_t* message, char c) {
  message[1 + message[0]++] = c;
}
