/* The C library's mathematics, written by hand in place of the
 * decompiler's (docs/firmware/README.md, "Hand-written C";
 * docs/firmware/modules.md, `platform/math`).  The exported C includes
 * this file after its own, so the types and prototypes it declares are
 * in scope; each function here keeps its generated prototype. */

static const uint32_t LIBC_INFINITY_HIGH = 0x7ff00000u;

static const int32_t LIBC_MANTISSA_BITS = 52;

/* The biased exponent of [0.5, 1), what frexp and the routines
 * that reduce their argument to it give a mantissa. */
static const int32_t LIBC_HALF_EXPONENT = 0x3fe;

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

/* |x|; a NaN raises. */
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

/* value as a mantissa in [0.5, 1) and a power of two.  A
 * subnormal it normalizes first, an exponent field of 1 less its
 * shift (fabs+0x4a-fabs+0x5c). */
double frexp(double value, int32_t* exponent) {
  static const uint32_t MANTISSA_HIGH_MASK = 0x000fffffu;
  union libc_double v = {.value = value};
  uint32_t sign = v.word.high & LIBC_SIGN;
  uint32_t magnitude = v.word.high & ~LIBC_SIGN;
  int16_t field = magnitude >> LIBC_EXPONENT_SHIFT;

  if (field == 0) {
    uint64_t mantissa = v.bits & ~((uint64_t)LIBC_SIGN << CPU_LONG_BITS);

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
 * sign. */
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
                                      << CPU_LONG_BITS};

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
 * it is an odd one; zero is not an integer to it. */
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
 * +0. */
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

/* The natural log: of the argument reduced to [0.5, 1), and
 * then to [sqrt(1/2), sqrt(2)), z = 2(r - 1)/(r + 1) and a rational
 * function of z^2, plus the exponent times ln 2 in two parts.  Of zero
 * -infinity, with errno ERANGE. */
double log(double x) {
  /* sqrt(1/2), where log and pow choose how to reduce their argument. */
  static const double SQRT_HALF = 0.7071067811865476;
  /* ln 2 in a high part and a low, and the rational approximation's
   * coefficients: the image's log_ln2_high to log_b2. */
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
  bool zero = (v.bits & ~((uint64_t)LIBC_SIGN << CPU_LONG_BITS)) == 0;

  if (zero || (v.word.high & LIBC_SIGN) != 0) {
    double result = libc_from_bits(
        float_raise(LIBC_RAISE_CODE(LIBC_RAISE_LOG, LIBC_CAUSE_OPERAND_ERROR)));

    if (zero) {
      union libc_double minus_infinity = {
          .bits = (uint64_t)(LIBC_SIGN | LIBC_INFINITY_HIGH) << CPU_LONG_BITS};

      result = minus_infinity.value;
      errno = LIBC_ERANGE;
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

/* The square root: of the argument reduced to [0.5, 1), a
 * linear seed and three Newton steps, the second folding in the
 * halving; an odd exponent then multiplied by sqrt(1/2), in two parts,
 * and the exponent halved. */
double sqrt(double x) {
  /* The seed's coefficients and sqrt(1/2) in a high part and a low:
   * the image's sqrt_seed_a to sqrt_half_low. */
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
  if ((v.bits & ~((uint64_t)LIBC_SIGN << CPU_LONG_BITS)) == 0 ||
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

/* Whether a double's bits are a zero, of either sign. */
static bool libc_is_zero(union libc_double x) {
  return ((x.word.high & ~LIBC_SIGN) | x.word.low) == 0;
}

/* Whether a double's bits are an infinity, of either sign. */
static bool libc_is_infinity(union libc_double x) {
  return (x.word.high & ~LIBC_SIGN) == LIBC_INFINITY_HIGH && x.word.low == 0;
}

/* x to the power y, by Cody and Waite's method: log2 x in
 * sixteenths, from x's mantissa reduced by a power of 2^(1/16) and a
 * polynomial; y times it in high and low parts, each a multiple of
 * 1/16 and the rest; and 2 to that by a power of 2^(1/16) times a
 * polynomial, the exponent added last.  A negative x it takes only to
 * an integer power, the result negated for an odd one.  Its special
 * cases, a zero, an infinity or a NaN, it settles first. */
double pow(double x, double y) {
  /* 2^(-k/16), k from 0 to 16, and for an odd k at (k + 1) / 2 the rest
   * of 2^(-k/16) that it leaves out: the image's pow_two_powers
   * and pow_two_power_tails. */
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
   * - 1: the image's pow_log_p1 to pow_exp2_q7 and log2_e_minus_1. */
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
   * image's pow_error_result. */
  static const union libc_double ERROR_RESULT = {.bits = 0x7fffffffffffffffu};
  static const int32_t POW_SIXTEENTHS = 16;
  static const int32_t POW_SQRT_HALF = 8;
  /* float_raise's underflow takes the result denormalized: the shift
   * that denormalizes it, its high and low words and a word of the
   * bits below them, whether any is set (float_raise+0x54).  A shift past the
   * mantissa's bits, more than 0x37, gives zero (float_raise+0xb4). */
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
                                      << CPU_LONG_BITS};
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

/* Raises a floating-point exception for the soft-float library and the
 * math routines: records its cause in float_status and returns the
 * double to give, or, where float_control enables the cause, reports it
 *.  Variadic in name: arguments is its code and the longs
 * after it where its caller pushed them, of which it reads those the
 * cause needs, and D1 as its caller left it.  float_raise is this,
 * keeping A0 and A1 for its caller. */
static __attribute__((used, noipa)) uint64_t
libc_float_raise_kept(const uint32_t* arguments) {
  const uint32_t callers_d1 = CPU_CALLERS_REGISTER(uint32_t, "d1");
  const uint32_t code = arguments[0];
  const libc_raise_state state = {&float_status, float_control, &errno};
  const libc_raised raised = libc_float_raise(code, callers_d1, &arguments[1],
                                              LIBC_CALLERS_FRAME(), state);

  if (raised.report) {
    float_exception_report(code, raised.address);
  }
  return (uint64_t)raised.high << CPU_LONG_BITS | raised.low;
}

CPU_KEEPING_A0_A1(float_raise, libc_float_raise_kept);

/* Builds float_raise's report, "fp <cause>: <routine>", in
 * float_exception_message and stops with fatal_error_with_code
 * (libc.md, "Exception codes"); never returns.  The image
 * pushes only the message, so that fatal_error_with_code's other
 * arguments are its return address, code and address, which it prints
 * as the code; float_raise pushes the address before the code
 * (float_raise+0x350). */
void float_exception_report(uint32_t code, uint32_t address) {
  libc_float_exception_message(float_exception_message, code);
  fatal_error_with_code((char*)float_exception_message,
                        (uint32_t)__builtin_return_address(0), code, address);
  for (;;) {
  }
}
