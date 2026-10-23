/* The soft-float library's routines (docs/firmware/softfloat.md), the
 * primary's and the installer's copies, as C calls them: each a wrapper
 * under the routine's own name and prototype, which binds the
 * arguments to the routine's registers, makes the jsr in one asm
 * statement and takes its result from D0 or D0:D1, naming as changed
 * each register the routine changes (docs/firmware/README.md,
 * "Hand-written C", register-bound calls).  The exporter gives these
 * routines no prototype and no bridge, and includes this folder's
 * headers in their place, after its prototypes, so that every function
 * of the C sees them.
 *
 * What each changes is read from its code: a routine keeps D2 to D7 and
 * A2 to A6 (the movem at its entry and exit, 0x5d0ba and 0x5d1f8 for
 * float_add), and of D0, D1, A0 and A1 changes those its comment
 * names, its result's among them; float_raise, which a routine calls
 * on an exception, keeps A0 and A1 for it (0x5e940).  Every one may
 * write the library's status word, float_status, and the in-place ones
 * store through A0, so each statement changes memory. */

/* A double as the routines take and give it, in a register pair, its
 * high long in the first: the same bits as two longs, not a
 * conversion. */
typedef union softfloat_double {
  double value;
  uint32_t word[2];
} softfloat_double;

/* A float's bits, as a routine takes and gives it in one register: the
 * same bits, not a conversion. */
typedef union softfloat_float {
  float value;
  uint32_t word;
  int32_t signed_word;
} softfloat_float;

/* The library's shapes, each one asm statement: the routine's
 * arguments in its registers, the changed ones among the outputs,
 * clobbers the routine's other changed registers. */

/* D0 and D1 in, both changed, D0 the result. */
#define SOFTFLOAT_TWO_WORDS(routine, x, y, ...)               \
  ({                                                          \
    register uint32_t softfloat_d0 __asm__("d0") = (x);       \
    register uint32_t softfloat_d1 __asm__("d1") = (y);       \
                                                              \
    __asm__ volatile("jsr " routine                           \
                     : "+r"(softfloat_d0), "+r"(softfloat_d1) \
                     :                                        \
                     : "cc", "memory", ##__VA_ARGS__);        \
    softfloat_d0;                                             \
  })

/* D0 in, D0 the result. */
#define SOFTFLOAT_ONE_WORD(routine, x, ...)             \
  ({                                                    \
    register uint32_t softfloat_d0 __asm__("d0") = (x); \
                                                        \
    __asm__ volatile("jsr " routine                     \
                     : "+r"(softfloat_d0)               \
                     :                                  \
                     : "cc", "memory", ##__VA_ARGS__);  \
    softfloat_d0;                                       \
  })

/* D0 in, D0:D1 the result. */
#define SOFTFLOAT_WORD_TO_PAIR(routine, x, ...)                     \
  ({                                                                \
    register uint32_t softfloat_d0 __asm__("d0") = (x);             \
    register uint32_t softfloat_d1 __asm__("d1");                   \
                                                                    \
    __asm__ volatile("jsr " routine                                 \
                     : "+r"(softfloat_d0), "=r"(softfloat_d1)       \
                     :                                              \
                     : "cc", "memory", ##__VA_ARGS__);              \
    (softfloat_double){.word = {softfloat_d0, softfloat_d1}}.value; \
  })

/* D0:D1 in, both changed, D0 the result. */
#define SOFTFLOAT_PAIR_TO_WORD(routine, x, ...)                         \
  ({                                                                    \
    const softfloat_double softfloat_x = {.value = (x)};                \
    register uint32_t softfloat_d0 __asm__("d0") = softfloat_x.word[0]; \
    register uint32_t softfloat_d1 __asm__("d1") = softfloat_x.word[1]; \
                                                                        \
    __asm__ volatile("jsr " routine                                     \
                     : "+r"(softfloat_d0), "+r"(softfloat_d1)           \
                     :                                                  \
                     : "cc", "memory", ##__VA_ARGS__);                  \
    softfloat_d0;                                                       \
  })

/* A0, the float to change, which it keeps, and D1 in, D1 changed, D0
 * the result, which it also stores at A0. */
#define SOFTFLOAT_FLOAT_IN_PLACE(routine, x, y, ...)          \
  ({                                                          \
    register float* softfloat_a0 __asm__("a0") = (x);         \
    register uint32_t softfloat_d0 __asm__("d0");             \
    register uint32_t softfloat_d1 __asm__("d1") =            \
        (softfloat_float){.value = (y)}.word;                 \
                                                              \
    __asm__ volatile("jsr " routine                           \
                     : "=r"(softfloat_d0), "+r"(softfloat_d1) \
                     : "r"(softfloat_a0)                      \
                     : "cc", "memory", ##__VA_ARGS__);        \
    (softfloat_float){.word = softfloat_d0}.value;            \
  })

/* A0, the double to change, which it keeps, and D0:D1 in, D0:D1 the
 * result, which it also stores at A0. */
#define SOFTFLOAT_DOUBLE_IN_PLACE(routine, x, y, ...)                   \
  ({                                                                    \
    const softfloat_double softfloat_y = {.value = (y)};                \
    register double* softfloat_a0 __asm__("a0") = (x);                  \
    register uint32_t softfloat_d0 __asm__("d0") = softfloat_y.word[0]; \
    register uint32_t softfloat_d1 __asm__("d1") = softfloat_y.word[1]; \
                                                                        \
    __asm__ volatile("jsr " routine                                     \
                     : "+r"(softfloat_d0), "+r"(softfloat_d1)           \
                     : "r"(softfloat_a0)                                \
                     : "cc", "memory", ##__VA_ARGS__);                  \
    (softfloat_double){.word = {softfloat_d0, softfloat_d1}}.value;     \
  })

/* D0:D1 and A0:A1 in, all four changed, D0:D1 the result. */
#define SOFTFLOAT_TWO_PAIRS(routine, x, y)                              \
  ({                                                                    \
    const softfloat_double softfloat_x = {.value = (x)};                \
    const softfloat_double softfloat_y = {.value = (y)};                \
    register uint32_t softfloat_d0 __asm__("d0") = softfloat_x.word[0]; \
    register uint32_t softfloat_d1 __asm__("d1") = softfloat_x.word[1]; \
    register uint32_t softfloat_a0 __asm__("a0") = softfloat_y.word[0]; \
    register uint32_t softfloat_a1 __asm__("a1") = softfloat_y.word[1]; \
                                                                        \
    __asm__ volatile("jsr " routine                                     \
                     : "+r"(softfloat_d0), "+r"(softfloat_d1),          \
                       "+r"(softfloat_a0), "+r"(softfloat_a1)           \
                     :                                                  \
                     : "cc", "memory");                                 \
    (softfloat_double){.word = {softfloat_d0, softfloat_d1}}.value;     \
  })

/* D0:D1 and A1, the double it reads, in, all four changed, D0:D1 the
 * result. */
#define SOFTFLOAT_PAIR_AND_POINTER(routine, x, y)                       \
  ({                                                                    \
    const softfloat_double softfloat_x = {.value = (x)};                \
    register uint32_t softfloat_d0 __asm__("d0") = softfloat_x.word[0]; \
    register uint32_t softfloat_d1 __asm__("d1") = softfloat_x.word[1]; \
    register double* softfloat_a1 __asm__("a1") = (y);                  \
                                                                        \
    __asm__ volatile("jsr " routine                                     \
                     : "+r"(softfloat_d0), "+r"(softfloat_d1),          \
                       "+r"(softfloat_a1)                               \
                     :                                                  \
                     : "a0", "cc", "memory");                           \
    (softfloat_double){.word = {softfloat_d0, softfloat_d1}}.value;     \
  })

/* A0 and A1, the doubles it reads, in, all four changed, D0:D1 the
 * result. */
#define SOFTFLOAT_TWO_POINTERS(routine, x, y)                       \
  ({                                                                \
    register double* softfloat_a0 __asm__("a0") = (x);              \
    register double* softfloat_a1 __asm__("a1") = (y);              \
    register uint32_t softfloat_d0 __asm__("d0");                   \
    register uint32_t softfloat_d1 __asm__("d1");                   \
                                                                    \
    __asm__ volatile("jsr " routine                                 \
                     : "=r"(softfloat_d0), "=r"(softfloat_d1),      \
                       "+r"(softfloat_a0), "+r"(softfloat_a1)       \
                     :                                              \
                     : "cc", "memory");                             \
    (softfloat_double){.word = {softfloat_d0, softfloat_d1}}.value; \
  })

/* A float's bits, as a routine takes it in a register. */
__attribute__((always_inline)) static inline uint32_t softfloat_bits(
    float value) {
  return (softfloat_float){.value = value}.word;
}

/* The float a routine's register holds the bits of. */
__attribute__((always_inline)) static inline float softfloat_value(
    uint32_t bits) {
  return (softfloat_float){.word = bits}.value;
}

/* *A0 -= D1 (0x5d080): changes D1 and A1. */
__attribute__((always_inline)) static inline float float_subtract_in_place(
    float* x, float y) {
  return SOFTFLOAT_FLOAT_IN_PLACE("float_subtract_in_place", x, y, "a1");
}

/* *A0 += D1 (0x5d08a): changes D1 and A1. */
__attribute__((always_inline)) static inline float float_add_in_place(float* x,
                                                                      float y) {
  return SOFTFLOAT_FLOAT_IN_PLACE("float_add_in_place", x, y, "a1");
}

/* D1 - D0 (0x5d094): changes D1 and A1. */
__attribute__((always_inline)) static inline float float_subtract_reversed(
    float a, float b) {
  return softfloat_value(SOFTFLOAT_TWO_WORDS(
      "float_subtract_reversed", softfloat_bits(a), softfloat_bits(b), "a1"));
}

/* D0 - D1 (0x5d096): changes D1 and A1. */
__attribute__((always_inline)) static inline float float_subtract(float a,
                                                                  float b) {
  return softfloat_value(SOFTFLOAT_TWO_WORDS(
      "float_subtract", softfloat_bits(a), softfloat_bits(b), "a1"));
}

/* D0 + D1 (0x5d0ba): changes D1 and A1. */
__attribute__((always_inline)) static inline float float_add(float a, float b) {
  return softfloat_value(SOFTFLOAT_TWO_WORDS("float_add", softfloat_bits(a),
                                             softfloat_bits(b), "a1"));
}

/* *A0 -= D0:D1 (0x5d2e2): changes A1. */
__attribute__((always_inline)) static inline double double_subtract_in_place(
    double* x, double y) {
  return SOFTFLOAT_DOUBLE_IN_PLACE("double_subtract_in_place", x, y, "a1");
}

/* D0:D1 - *A1 (0x5d2f8). */
__attribute__((always_inline)) static inline double double_subtract_pointer(
    double a, double* b) {
  return SOFTFLOAT_PAIR_AND_POINTER("double_subtract_pointer", a, b);
}

/* *A1 - *A0 (0x5d306). */
__attribute__((always_inline)) static inline double
double_subtract_reversed_pointers(double* a, double* b) {
  return SOFTFLOAT_TWO_POINTERS("double_subtract_reversed_pointers", a, b);
}

/* *A1 - D0:D1 (0x5d30a). */
__attribute__((always_inline)) static inline double
double_subtract_reversed_pointer(double a, double* b) {
  return SOFTFLOAT_PAIR_AND_POINTER("double_subtract_reversed_pointer", a, b);
}

/* A0:A1 - D0:D1 (0x5d30e). */
__attribute__((always_inline)) static inline double double_subtract_reversed(
    double a, double b) {
  return SOFTFLOAT_TWO_PAIRS("double_subtract_reversed", a, b);
}

/* *A0 += D0:D1 (0x5d338): changes A1. */
__attribute__((always_inline)) static inline double double_add_in_place(
    double* x, double y) {
  return SOFTFLOAT_DOUBLE_IN_PLACE("double_add_in_place", x, y, "a1");
}

/* *A0 + *A1 (0x5d34a). */
__attribute__((always_inline)) static inline double double_add_pointers(
    double* a, double* b) {
  return SOFTFLOAT_TWO_POINTERS("double_add_pointers", a, b);
}

/* D0:D1 + *A1 (0x5d34e). */
__attribute__((always_inline)) static inline double double_add_pointer(
    double a, double* b) {
  return SOFTFLOAT_PAIR_AND_POINTER("double_add_pointer", a, b);
}

/* D0:D1 + A0:A1 (0x5d352). */
__attribute__((always_inline)) static inline double double_add(double a,
                                                               double b) {
  return SOFTFLOAT_TWO_PAIRS("double_add", a, b);
}

/* D0 compared with D1, 1, 0 or -1 (0x5d65c): changes D1 and A1. */
__attribute__((always_inline)) static inline int32_t float_compare(float a,
                                                                   float b) {
  return SOFTFLOAT_TWO_WORDS("float_compare", softfloat_bits(a),
                             softfloat_bits(b), "a1");
}

/* Negative when A0:A1 < D0:D1 (0x5d6c6): changes D1 and A0 and keeps
 * A1, so b's halves are bound apart. */
__attribute__((always_inline)) static inline int32_t double_compare(double a,
                                                                    double b) {
  const softfloat_double x = {.value = a};
  const softfloat_double y = {.value = b};
  register uint32_t d0 __asm__("d0") = x.word[0];
  register uint32_t d1 __asm__("d1") = x.word[1];
  register uint32_t a0 __asm__("a0") = y.word[0];
  register uint32_t a1 __asm__("a1") = y.word[1];

  __asm__ volatile("jsr double_compare"
                   : "+r"(d0), "+r"(d1), "+r"(a0)
                   : "r"(a1)
                   : "cc", "memory");
  return (int32_t)d0;
}

/* *A0 /= D1 (0x5d7da): changes D1. */
__attribute__((always_inline)) static inline float float_divide_in_place(
    float* x, float y) {
  return SOFTFLOAT_FLOAT_IN_PLACE("float_divide_in_place", x, y);
}

/* D1 / D0 (0x5d7e4): changes D1. */
__attribute__((always_inline)) static inline float float_divide_reversed(
    float a, float b) {
  return softfloat_value(SOFTFLOAT_TWO_WORDS(
      "float_divide_reversed", softfloat_bits(a), softfloat_bits(b)));
}

/* D0 / D1 (0x5d7e6): changes D1. */
__attribute__((always_inline)) static inline float float_divide(float a,
                                                                float b) {
  return softfloat_value(SOFTFLOAT_TWO_WORDS("float_divide", softfloat_bits(a),
                                             softfloat_bits(b)));
}

/* *A0 /= D0:D1 (0x5da3e): changes A1. */
__attribute__((always_inline)) static inline double double_divide_in_place(
    double* x, double y) {
  return SOFTFLOAT_DOUBLE_IN_PLACE("double_divide_in_place", x, y, "a1");
}

/* D0:D1 / A0:A1 (0x5da58). */
__attribute__((always_inline)) static inline double double_divide(double a,
                                                                  double b) {
  return SOFTFLOAT_TWO_PAIRS("double_divide", a, b);
}

/* *A1 / *A0 (0x5da62). */
__attribute__((always_inline)) static inline double
double_divide_reversed_pointers(double* a, double* b) {
  return SOFTFLOAT_TWO_POINTERS("double_divide_reversed_pointers", a, b);
}

/* *A1 / D0:D1 (0x5da66). */
__attribute__((always_inline)) static inline double
double_divide_reversed_pointer(double a, double* b) {
  return SOFTFLOAT_PAIR_AND_POINTER("double_divide_reversed_pointer", a, b);
}

/* A0:A1 / D0:D1 (0x5da6a). */
__attribute__((always_inline)) static inline double double_divide_reversed(
    double a, double b) {
  return SOFTFLOAT_TWO_PAIRS("double_divide_reversed", a, b);
}

/* Double to float (0x5dcd2): changes D1 and A0. */
__attribute__((always_inline)) static inline float double_to_float(double d) {
  return softfloat_value(SOFTFLOAT_PAIR_TO_WORD("double_to_float", d, "a0"));
}

/* Double to signed integer (0x5ddde): changes D1 and A0. */
__attribute__((always_inline)) static inline int32_t double_to_integer(
    double d) {
  return (int32_t)SOFTFLOAT_PAIR_TO_WORD("double_to_integer", d, "a0");
}

/* Double to unsigned integer (0x5de9e): changes D1 and A1. */
__attribute__((always_inline)) static inline uint32_t double_to_unsigned(
    double d) {
  return SOFTFLOAT_PAIR_TO_WORD("double_to_unsigned", d, "a1");
}

/* Float to double, in D0:D1 (0x5df24): changes A0. */
__attribute__((always_inline)) static inline double float_to_double(float f) {
  return SOFTFLOAT_WORD_TO_PAIR("float_to_double", softfloat_bits(f), "a0");
}

/* Float to signed integer (0x5dfbc): changes D1 and A1. */
__attribute__((always_inline)) static inline int32_t float_to_integer(float f) {
  return (int32_t)SOFTFLOAT_ONE_WORD("float_to_integer", softfloat_bits(f),
                                     "d1", "a1");
}

/* Float to unsigned integer (0x5e04e): changes D1 and A1. */
__attribute__((always_inline)) static inline uint32_t float_to_unsigned(
    float f) {
  return SOFTFLOAT_ONE_WORD("float_to_unsigned", softfloat_bits(f), "d1", "a1");
}

/* Signed integer to double (0x5e08e): changes A0 and A1. */
__attribute__((always_inline)) static inline double integer_to_double(
    int32_t i) {
  return SOFTFLOAT_WORD_TO_PAIR("integer_to_double", (uint32_t)i, "a0", "a1");
}

/* Unsigned integer to double (0x5e108): changes A0 and A1. */
__attribute__((always_inline)) static inline double unsigned_to_double(
    uint32_t u) {
  return SOFTFLOAT_WORD_TO_PAIR("unsigned_to_double", u, "a0", "a1");
}

/* Signed integer to float (0x5e168): changes D1 and A1. */
__attribute__((always_inline)) static inline float integer_to_float(int32_t i) {
  return softfloat_value(
      SOFTFLOAT_ONE_WORD("integer_to_float", (uint32_t)i, "d1", "a1"));
}

/* Unsigned integer to float (0x5e20e): changes D1, A0 and A1. */
__attribute__((always_inline)) static inline float unsigned_to_float(
    uint32_t u) {
  return softfloat_value(
      SOFTFLOAT_ONE_WORD("unsigned_to_float", u, "d1", "a0", "a1"));
}

/* *A0 *= D1 (0x5e316): changes D1. */
__attribute__((always_inline)) static inline float float_multiply_in_place(
    float* x, float y) {
  return SOFTFLOAT_FLOAT_IN_PLACE("float_multiply_in_place", x, y);
}

/* D0 x D1 (0x5e320): changes D1. */
__attribute__((always_inline)) static inline float float_multiply(float a,
                                                                  float b) {
  return softfloat_value(SOFTFLOAT_TWO_WORDS(
      "float_multiply", softfloat_bits(a), softfloat_bits(b)));
}

/* *A0 *= D0:D1 (0x5e5e6): changes A1. */
__attribute__((always_inline)) static inline double double_multiply_in_place(
    double* x, double y) {
  return SOFTFLOAT_DOUBLE_IN_PLACE("double_multiply_in_place", x, y, "a1");
}

/* *A0 x *A1 (0x5e5f8). */
__attribute__((always_inline)) static inline double double_multiply_pointers(
    double* a, double* b) {
  return SOFTFLOAT_TWO_POINTERS("double_multiply_pointers", a, b);
}

/* D0:D1 x *A1 (0x5e5fc). */
__attribute__((always_inline)) static inline double double_multiply_pointer(
    double a, double* b) {
  return SOFTFLOAT_PAIR_AND_POINTER("double_multiply_pointer", a, b);
}

/* D0:D1 x A0:A1 (0x5e600). */
__attribute__((always_inline)) static inline double double_multiply(double a,
                                                                    double b) {
  return SOFTFLOAT_TWO_PAIRS("double_multiply", a, b);
}

/* The installer's copies, byte for byte the primary's but for their
 * absolute addresses (installer::0x10893a on), change what the
 * primary's do. */

/* float_compare's copy (installer::0x10893a): changes D1 and A1. */
__attribute__((always_inline)) static inline int32_t installer_float_compare(
    float a, float b) {
  return SOFTFLOAT_TWO_WORDS("installer_float_compare", softfloat_bits(a),
                             softfloat_bits(b), "a1");
}

/* float_to_integer's copy (installer::0x1089d4): changes D1 and A1. */
__attribute__((always_inline)) static inline int32_t installer_float_to_integer(
    float f) {
  return (int32_t)SOFTFLOAT_ONE_WORD("installer_float_to_integer",
                                     softfloat_bits(f), "d1", "a1");
}

/* float_to_unsigned's copy (installer::0x108a66): changes D1 and A1. */
__attribute__((always_inline)) static inline uint32_t
installer_float_to_unsigned(float f) {
  return SOFTFLOAT_ONE_WORD("installer_float_to_unsigned", softfloat_bits(f),
                            "d1", "a1");
}

/* float_multiply_in_place's copy (installer::0x108b02): changes D1. */
__attribute__((always_inline)) static inline float
installer_float_multiply_in_place(float* x, float y) {
  return SOFTFLOAT_FLOAT_IN_PLACE("installer_float_multiply_in_place", x, y);
}

/* float_multiply's copy (installer::0x108b0c): changes D1. */
__attribute__((always_inline)) static inline float installer_float_multiply(
    float a, float b) {
  return softfloat_value(SOFTFLOAT_TWO_WORDS(
      "installer_float_multiply", softfloat_bits(a), softfloat_bits(b)));
}
