/* exp (`platform/math`), the image's own: the other images' C library
 * has none; common/math.c has the rest of the mathematics.  Written by
 * hand in place of the decompiler's (docs/firmware/README.md,
 * "Hand-written C"); it keeps its generated prototype. */

/* e to the x, by Cody and Waite's method: x less n ln 2, n its nearest
 * integer in ln 2s and ln 2 in a high part and a low, g; then 0.5 +
 * g P(g^2) / (Q(g^2) - g P(g^2)), times 2^(n + 1) by its exponent field.
 * Within ln 2 / 2 of 0 g is x and n 0, and below 2^-54 the result is 1.
 * A NaN raises, -infinity gives 0 and +infinity itself; past the double's
 * range it raises an overflow or an underflow, and a result too small for
 * its exponent field an underflow, with its mantissa (0x709ac). */
double exp(double x) {
  /* The image's constants, 0x70c64 to 0x70ccc: 1 / ln 2, ln 2 / 2, the
   * largest and least arguments, the argument below which e^x is 1, the
   * polynomials' coefficients; then ln 2 in a high part and a low,
   * LIBC_LN2_HIGH and LIBC_LN2_LOW. */
  static const double INVERSE_LN2 = 1.4426950408889634;
  static const double LN2_HALF = 0.34657359027997264;
  static const double ARGUMENT_MOST = 709.7827128933839;
  static const double ARGUMENT_LEAST = -744.4400719213812;
  static const double ARGUMENT_TINY = 5.551115123125783e-17;
  static const double P0 = 0.25;
  static const double P1 = 0.007575318015942278;
  static const double P2 = 3.1555192765684645e-05;
  static const double Q0 = 0.5;
  static const double Q1 = 0.056817302698551224;
  static const double Q2 = 0.000631218943743985;
  static const double Q3 = 7.510402839987004e-07;
  /* The high word's exponent field and the mantissa's top 20 bits. */
  static const uint32_t EXPONENT_MASK = (uint32_t)LIBC_EXPONENT_MAX
                                        << LIBC_EXPONENT_SHIFT;
  static const uint32_t MANTISSA_HIGH_MASK =
      (uint32_t)(LIBC_MANTISSA_MASK >> CPU_LONG_BITS);
  /* float_raise's underflow takes the result denormalized: the shift
   * that denormalizes it, its high and low words and a word of the bits
   * below them, whether any is set (float_raise+0x54).  A shift past the
   * mantissa's bits gives zero (float_raise+0xb4). */
  static const int32_t UNDERFLOW_TO_ZERO = 100;
  static const int32_t UNDERFLOW_INEXACT = 1;
  union libc_double v = {.value = x};

  if ((v.word.high & EXPONENT_MASK) == EXPONENT_MASK) {
    if (((v.word.high & MANTISSA_HIGH_MASK) | v.word.low) != 0) {
      v.bits =
          float_raise(LIBC_RAISE_CODE(LIBC_RAISE_EXP, LIBC_CAUSE_SIGNALING_NAN),
                      v.word.high, v.word.low);
    } else if ((v.word.high & LIBC_SIGN) != 0) {
      v.bits = 0;
    }
    return v.value;
  }
  if (double_compare(ARGUMENT_MOST, x) > 0) {
    v.bits = float_raise(LIBC_RAISE_CODE(LIBC_RAISE_EXP, LIBC_CAUSE_OVERFLOW));
    return v.value;
  }
  if (double_compare(x, ARGUMENT_LEAST) > 0) {
    v.bits = float_raise(LIBC_RAISE_CODE(LIBC_RAISE_EXP, LIBC_CAUSE_UNDERFLOW),
                         UNDERFLOW_TO_ZERO, 0, 0, UNDERFLOW_INEXACT);
    return v.value;
  }
  const uint32_t sign = v.word.high & LIBC_SIGN;

  v.word.high ^= sign;
  if (double_compare(v.value, ARGUMENT_TINY) > 0) {
    return 1.0;
  }
  int32_t n = 0;
  double g = x;

  if (double_compare(LN2_HALF, v.value) > 0) {
    n = double_to_integer(
        double_add(0.5, double_multiply(INVERSE_LN2, v.value)));
    const double xn = integer_to_double(n);
    union libc_double reduced = {
        .value = double_subtract_reversed(
            double_multiply(LIBC_LN2_LOW, xn),
            double_subtract_reversed(double_multiply(LIBC_LN2_HIGH, xn),
                                     v.value))};

    reduced.word.high ^= sign;
    g = reduced.value;
    if (sign != 0) {
      n = -n;
    }
  }
  const double z = double_multiply(g, g);
  double p = double_add(double_multiply(z, P2), P1);

  p = double_add(double_multiply(p, z), P0);
  p = double_multiply(p, g);
  double q = double_add(double_multiply(z, Q3), Q2);

  q = double_add(double_multiply(q, z), Q1);
  q = double_add(double_multiply(q, z), Q0);
  union libc_double result = {
      .value = double_add(
          0.5, double_divide_reversed(double_subtract_reversed(p, q), p))};
  const int32_t field =
      (int32_t)((result.word.high & EXPONENT_MASK) >> LIBC_EXPONENT_SHIFT) + 1 +
      n;

  result.word.high &= MANTISSA_HIGH_MASK;
  if (field > 0) {
    result.word.high |= (uint32_t)field << LIBC_EXPONENT_SHIFT;
    return result.value;
  }
  result.bits =
      float_raise(LIBC_RAISE_CODE(LIBC_RAISE_EXP, LIBC_CAUSE_UNDERFLOW),
                  1 - field, result.word.high, result.word.low, 0);
  return result.value;
}
