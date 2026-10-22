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

/* A number at a scale as a double; -1 when it does not fit (0x254ba). */
int32_t scpi_number_to_double(scpi_number* number, double* value,
                              int32_t scale) {
  /* The condition codes' Z bit, which the bridge to scpi_bcd_to_double
   * keeps in __ghidra_ccr: set when the number does not fit a double
   * (0x2507a; MC68331UM, the CCR). */
  static const uint8_t CCR_ZERO = 0x04;
  scpi_bcd bcd;

  if (scpi_number_to_bcd(number, scale, &bcd) != 0) {
    return -1;
  }
  double converted = scpi_bcd_to_double(&bcd);

  if (__ghidra_ccr & CCR_ZERO) {
    return -1;
  }
  *value = converted;
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
