/* SCPI numbers to and from text, written by hand in place of the
 * decompiler's (docs/firmware/README.md, "Hand-written C";
 * docs/firmware/modules.md, `applications/scpi/convert`).  The exported
 * C includes this file after its own, so the types and prototypes it
 * declares are in scope; each function here keeps its generated
 * prototype. */

static const uint8_t SCPI_NUMBER_ROUND_UP = 5;

/* The most integer digits each converter takes, the decimal width of
 * its type. */
static const uint8_t SCPI_SHORT_DIGITS = 5;

static const uint8_t SCPI_LONG_DIGITS = 10;

/* The number's places before its point at a scale: how many of its
 * digits are the integer part (0x260b2 to 0x260be). */
static int32_t scpi_engine_places(scpi_number* number, int32_t scale) {
  return number->exponent + (int16_t)scale + SCPI_NUMBER_DIGITS;
}

/* The first count digits as an integer, kept to 32 bits. */
static uint32_t scpi_engine_digits_value(scpi_number* number, int16_t count) {
  uint32_t value = 0;

  for (int16_t index = 0; index < count; index++) {
    value = value * SCPI_NUMBER_BASE + number->digits[index];
  }
  return value;
}

/* Whether the digit after the places rounds them up; places from 0. */
static bool scpi_engine_rounds_up(scpi_number* number, int32_t places) {
  return places >= 0 && number->digits[places] >= SCPI_NUMBER_ROUND_UP;
}

/* The condition codes' Z bit, which a bridge keeps in bridge_ccr: set
 * when scpi_bcd_to_double's number does not fit a double (0x2507a) and
 * when scpi_double_to_decimal's is subnormal, infinite or NaN (0x24e0e);
 * MC68331UM, the CCR. */
static const uint8_t SCPI_CCR_ZERO = 0x04;

/* A number at a scale as a double; -1 when it does not fit (0x254ba). */
int32_t scpi_number_to_double(scpi_number* number, double* value,
                              int32_t scale) {
  scpi_bcd bcd;

  if (scpi_number_to_bcd(number, scale, &bcd) != 0) {
    return -1;
  }
  double converted = scpi_bcd_to_double(&bcd);

  if (bridge_ccr & SCPI_CCR_ZERO) {
    return -1;
  }
  *value = converted;
  return 0;
}

/* Zero's text, "+0", or "+0.0" with the point, which raises the
 * exponent, the resolution, by one; where its "E" goes. */
static uint8_t* scpi_engine_zero_text(uint8_t* text, char point,
                                      int16_t* exponent) {
  text[0] = '+';
  text[1] = '0';
  if (!point) {
    return text + 2;
  }
  text[2] = '.';
  text[3] = '0';
  ++*exponent;
  return text + 4;
}

/* A double, given as its two longs, high first, as NR3 text "+d.ddEsxx":
 * so many significant digits, none below the resolution, and so many
 * exponent digits; with the point, a single digit is "+0.d" (0x2553a).
 * 1 when the text cannot be made: no digits, a double the conversion
 * refuses, or an exponent too wide. */
uint32_t scpi_real_to_text(int32_t high, int32_t low, uint8_t* text,
                           int16_t digits, int16_t exponent_width,
                           int16_t resolution, char point) {
  /* The exponent's four digits, two pairs from decimal_digit_pairs. */
  static const int8_t EXPONENT_MOST_DIGITS = 4;
  static const int16_t EXPONENT_PAIR = 100;
  const int16_t decimal_digits = sizeof(((scpi_decimal*)0)->digits);
  /* The double's bits, as its callers split them. */
  union {
    int32_t words[2];
    double value;
  } bits = {{high, low}};
  union {
    uint16_t pairs[2];
    char digits[4];
  } exponent_text;
  scpi_decimal decimal;
  bool zero = high == 0 && low == 0 && resolution != SCPI_REAL_EVERY_DIGIT;

  if (!zero) {
    if (digits <= 0) {
      return 1;
    }
    scpi_double_to_decimal(bits.value, digits, (char*)&decimal);
    if (bridge_ccr & SCPI_CCR_ZERO) {
      return 1;
    }
    /* The digits above the resolution, a word's difference, as the image
     * takes it, though it compares the exponents themselves. */
    int16_t places = decimal.exponent - resolution;

    if (decimal.exponent < resolution) {
      zero = true;
    } else {
      if (places > digits) {
        places = digits;
      }
      if (places < decimal_digits) {
        scpi_round_decimal((char*)&decimal, places);
        places = decimal.exponent - resolution;
        if (decimal.exponent <= resolution) {
          zero = true;
        } else if (places <= digits) {
          digits = places;
        }
      }
    }
  }
  int16_t exponent = resolution;
  uint8_t* end;

  if (zero) {
    end = scpi_engine_zero_text(text, point, &exponent);
  } else {
    /* The image tests the sign word's low byte. */
    text[0] = (uint8_t)decimal.sign != 0 ? '-' : '+';
    text[1] = decimal.digits[0];
    text[2] = '.';
    exponent = (text[1] == '0' ? 1 : decimal.exponent) - 1;
    if (digits >= 2) {
      for (int16_t index = 1; index < digits; index++) {
        text[2 + index] = decimal.digits[index];
      }
      end = text + 2 + digits;
    } else if (point) {
      text[3] = text[1];
      text[1] = '0';
      exponent++;
      end = text + 4;
    } else {
      end = text + 2;
    }
  }
  *end++ = 'E';
  *end = '+';
  if (exponent < 0) {
    *end = '-';
    exponent = -exponent;
  }
  end++;
  int16_t hundreds = exponent / EXPONENT_PAIR;
  int16_t units = exponent % EXPONENT_PAIR;

  if (exponent < 0) {
    /* Only -32768 is negative still, and its divu.w overflows, leaving
     * the dividend 0xffff8000, so that the hundreds' pair is its low
     * word's, the first, and the units' its high word's, the word before
     * the table (0x2562e). */
    hundreds = 0;
    units = -1;
  }
  /* Unchecked: hundreds from 100 read past the table (quirks.md). */
  exponent_text.pairs[0] = decimal_digit_pairs[hundreds];
  exponent_text.pairs[1] = decimal_digit_pairs[units];
  if (exponent_width <= 0) {
    return 1;
  }
  /* The image takes the width's low byte, signed (0x25658). */
  int8_t width = exponent_width;

  if (width > EXPONENT_MOST_DIGITS) {
    return 1;
  }
  if (width < 0) {
    /* The image checks 4 - width bytes from these digits up, past them
     * into its frame and its caller's, the farthest first, failing
     * unless every one is '0'; C cannot read them, and fails. */
    return 1;
  }
  int8_t skipped = EXPONENT_MOST_DIGITS - width;

  for (int8_t index = 0; index < skipped; index++) {
    if (exponent_text.digits[index] != '0') {
      return 1;
    }
  }
  /* The image copies the whole width, so that one above 0xff reads past
   * these digits into its frame, which C cannot say; the C copies the low
   * byte's. */
  for (int8_t index = skipped; index < EXPONENT_MOST_DIGITS; index++) {
    *end++ = exponent_text.digits[index];
  }
  *end = '\0';
  return 0;
}

/* A number at a scale as a short, rounded (0x2609a). */
int32_t scpi_number_to_short(scpi_number* number, int16_t* value,
                             int32_t scale) {
  /* A const object is not a constant expression, so the base is 10 here. */
  static const uint16_t MOST_TENTHS = 0x7fff / 10;
  static const uint16_t MOST_POSITIVE = 0x7fff;
  static const uint16_t MOST_NEGATIVE = 0x8000;

  if (number->digits[0] == 0) {
    *value = 0;
    return 0;
  }
  int32_t places = scpi_engine_places(number, scale);

  if (places > SCPI_SHORT_DIGITS) {
    return STATUS_SCPI_DATA_OUT_OF_RANGE;
  }
  uint16_t magnitude = 0;

  if (places > 0) {
    magnitude = scpi_engine_digits_value(number, places - 1);
    if (magnitude > MOST_TENTHS) {
      return STATUS_SCPI_DATA_OUT_OF_RANGE;
    }
    magnitude = magnitude * SCPI_NUMBER_BASE + number->digits[places - 1];
  }
  if (scpi_engine_rounds_up(number, places)) {
    magnitude++;
  }
  if (magnitude > (number->negative ? MOST_NEGATIVE : MOST_POSITIVE)) {
    return STATUS_SCPI_DATA_OUT_OF_RANGE;
  }
  *value = number->negative ? -magnitude : magnitude;
  return 0;
}

/* Its magnitude at a scale as an unsigned short, rounded (0x26140). */
int32_t scpi_number_to_unsigned_short(scpi_number* number, uint16_t* value,
                                      int32_t scale) {
  /* A const object is not a constant expression, so the base is 10 here. */
  static const uint16_t MOST_TENTHS = 0xffff / 10;
  static const uint16_t MOST_UNITS = 0xffff % 10;

  if (number->digits[0] == 0) {
    *value = 0;
    return 0;
  }
  int32_t places = scpi_engine_places(number, scale);

  if (places > SCPI_SHORT_DIGITS) {
    return STATUS_SCPI_DATA_OUT_OF_RANGE;
  }
  uint16_t tens = 0;
  uint16_t units = 0;

  if (places > 0) {
    tens = scpi_engine_digits_value(number, places - 1);
    if (tens > MOST_TENTHS) {
      return STATUS_SCPI_DATA_OUT_OF_RANGE;
    }
    tens *= SCPI_NUMBER_BASE;
    units = number->digits[places - 1];
  }
  if (scpi_engine_rounds_up(number, places)) {
    units++;
  }
  if (tens == MOST_TENTHS * SCPI_NUMBER_BASE && units > MOST_UNITS) {
    return STATUS_SCPI_DATA_OUT_OF_RANGE;
  }
  *value = tens + units;
  return 0;
}

/* A number at a scale as a long, rounded (0x261e4). */
int32_t scpi_number_to_long(scpi_number* number, int32_t* value,
                            int32_t scale) {
  /* A const object is not a constant expression, so the base is 10 here. */
  static const int32_t MOST_TENTHS = 0x7fffffff / 10;
  const uint32_t most_positive = 0x7fffffff, most_negative = 0x80000000;

  if (number->digits[0] == 0) {
    *value = 0;
    return 0;
  }
  int32_t places = scpi_engine_places(number, scale);

  if (places > SCPI_LONG_DIGITS) {
    return STATUS_SCPI_DATA_OUT_OF_RANGE;
  }
  uint32_t magnitude = 0;

  if (places > 0) {
    magnitude = scpi_engine_digits_value(number, places - 1);
    if (magnitude > MOST_TENTHS) {
      return STATUS_SCPI_DATA_OUT_OF_RANGE;
    }
    magnitude = magnitude * SCPI_NUMBER_BASE + number->digits[places - 1];
  }
  if (scpi_engine_rounds_up(number, places)) {
    magnitude++;
  }
  if (magnitude > (number->negative ? most_negative : most_positive)) {
    return STATUS_SCPI_DATA_OUT_OF_RANGE;
  }
  *value = number->negative ? -magnitude : magnitude;
  return 0;
}

/* Its magnitude at a scale as an unsigned long, rounded (0x2628a). */
int32_t scpi_number_to_unsigned_long(scpi_number* number, uint32_t* value,
                                     int32_t scale) {
  const uint32_t most_tenths = 0xffffffff / SCPI_NUMBER_BASE,
                 most_units = 0xffffffff % SCPI_NUMBER_BASE;

  if (number->digits[0] == 0) {
    *value = 0;
    return 0;
  }
  int32_t places = scpi_engine_places(number, scale);

  if (places > SCPI_LONG_DIGITS) {
    return STATUS_SCPI_DATA_OUT_OF_RANGE;
  }
  uint32_t tens = 0;
  uint32_t units = 0;

  if (places > 0) {
    tens = scpi_engine_digits_value(number, places - 1);
    if (tens > most_tenths) {
      return STATUS_SCPI_DATA_OUT_OF_RANGE;
    }
    tens *= SCPI_NUMBER_BASE;
    units = number->digits[places - 1];
  }
  if (scpi_engine_rounds_up(number, places)) {
    units++;
  }
  if (tens == most_tenths * SCPI_NUMBER_BASE && units > most_units) {
    return STATUS_SCPI_DATA_OUT_OF_RANGE;
  }
  *value = tens + units;
  return 0;
}

/* A number at a scale as a boolean: nonzero once rounded (0x2632c). */
void scpi_number_to_boolean(scpi_number* number, uint16_t* value,
                            int32_t scale) {
  int32_t places = scpi_engine_places(number, scale);

  *value = number->digits[0] != 0 &&
           (places > 0 ||
            (places == 0 && number->digits[0] >= SCPI_NUMBER_ROUND_UP));
}

/* A value's decimal digits right-aligned in digits, to DIGITS_LAST,
 * and the first's index (0x264e0). */
int32_t scpi_unsigned_to_digits(uint32_t value, char* digits, int16_t* start) {
  static const int16_t DIGITS_LAST = 16;

  for (int16_t index = DIGITS_LAST; index > 0; index--) {
    digits[index] = value % SCPI_NUMBER_BASE + '0';
    value /= SCPI_NUMBER_BASE;
    if (value == 0) {
      *start = index;
      return 0;
    }
  }
  return STATUS_SCPI_FORMAT_FAILED;
}
