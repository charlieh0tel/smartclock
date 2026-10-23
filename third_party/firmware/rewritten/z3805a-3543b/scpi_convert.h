/* What SCPI's number conversions share with the installer's copies of
 * them (scpi_convert.c, installer_scpi_convert.c): their bodies, which
 * both copies' routines call.  The exported C includes the hand-written
 * headers after its own declarations and before the hand-written .c
 * files, so every one of them can use it. */

static const uint8_t SCPI_NUMBER_ROUND_UP = 5;

/* The most integer digits a short's and a long's converters take, the
 * decimal widths of their types. */
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

/* scpi_number_to_short's and its installer copy's body: a number at a
 * scale as a short, rounded. */
static int32_t scpi_engine_number_to_short(scpi_number* number, int16_t* value,
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

/* scpi_number_to_unsigned_short's and its installer copy's body: its magnitude
 * at a scale as an unsigned short, rounded. */
static int32_t scpi_engine_number_to_unsigned_short(scpi_number* number,
                                                    uint16_t* value,
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

/* scpi_number_to_long's and its installer copy's body: a number at a scale as a
 * long, rounded. */
static int32_t scpi_engine_number_to_long(scpi_number* number, int32_t* value,
                                          int32_t scale) {
  /* A const object is not a constant expression, so the base is 10 here. */
  static const int32_t MOST_TENTHS = 0x7fffffff / 10;
  static const uint32_t MOST_POSITIVE = 0x7fffffff;
  static const uint32_t MOST_NEGATIVE = 0x80000000;

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
  if (magnitude > (number->negative ? MOST_NEGATIVE : MOST_POSITIVE)) {
    return STATUS_SCPI_DATA_OUT_OF_RANGE;
  }
  *value = number->negative ? -magnitude : magnitude;
  return 0;
}

/* scpi_number_to_unsigned_long's and its installer copy's body: its magnitude
 * at a scale as an unsigned long, rounded. */
static int32_t scpi_engine_number_to_unsigned_long(scpi_number* number,
                                                   uint32_t* value,
                                                   int32_t scale) {
  /* A const object is not a constant expression, so the base is 10 here
   * (0x262e2, 0x26310). */
  static const uint32_t MOST_TENTHS = 0xffffffff / 10;
  static const uint32_t MOST_UNITS = 0xffffffff % 10;

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

/* The index scpi_unsigned_to_digits right-aligns a value's last digit
 * at, in a buffer of scpi_format.h's SCPI_DIGITS_SIZE. */
static const int16_t SCPI_DIGITS_LAST = 16;

/* scpi_unsigned_to_digits' and its installer copy's body: a value's
 * decimal digits right-aligned in digits, to SCPI_DIGITS_LAST, and the
 * first's index. */
static int32_t scpi_engine_unsigned_to_digits(uint32_t value, char* digits,
                                              int16_t* start) {
  for (int16_t index = SCPI_DIGITS_LAST; index > 0; index--) {
    digits[index] = value % SCPI_NUMBER_BASE + '0';
    value /= SCPI_NUMBER_BASE;
    if (value == 0) {
      *start = index;
      return 0;
    }
  }
  return STATUS_SCPI_FORMAT_FAILED;
}
