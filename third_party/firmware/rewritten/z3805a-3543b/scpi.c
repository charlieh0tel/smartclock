/* The SCPI engine -- the parameter converters, the lexer, the response
 * formatters, keyword and suffix matching, the arena, the numeric
 * parameters, the console port and the requests to pllc -- written by
 * hand in place of the decompiler's (docs/firmware/README.md,
 * "Hand-written C").  The exported C includes this file after its own,
 * so the types and prototypes it declares are in scope; each function
 * here keeps its generated prototype.  The structures are
 * docs/firmware/structures.md's "Numeric SCPI parameters", "A numeric
 * parameter's units", "The SCPI lexer's token and port" and "The SCPI
 * session"; docs/scpi/README.md describes the language. */

/* The negative conditions the engine's routines return, each queueing
 * the SCPI error beside it (status_negative_conditions, 0x28720). */
enum {
  SCPI_STATUS_SYNTAX_ERROR = -6,             /* -102 Syntax error */
  SCPI_STATUS_SUFFIX_NOT_ALLOWED = -9,       /* -138 Suffix not allowed */
  SCPI_STATUS_DATA_OUT_OF_RANGE = -11,       /* -222 Data out of range */
  SCPI_STATUS_ILLEGAL_PARAMETER_VALUE = -27, /* -224 Illegal parameter value */
  SCPI_STATUS_NO_LIST = -28,                 /* -310 System error */
  SCPI_STATUS_FORMAT_FAILED = -40,           /* -310 System error */
  SCPI_STATUS_NO_MEMORY = -56,               /* -310 System error */
  /* Not a condition: scpi_interpret takes it as the response's end,
   * and anything after it as -15, -440 Query UNTERMINATED after
   * indefinite response (0x294a6, 0x29446). */
  SCPI_STATUS_INDEFINITE_RESPONSE = -90,
};

/* The Status Byte's RQS bit (structures.md, "The SCPI session"), which
 * status_update_rqs keeps and the service request enable cannot set. */
static const uint8_t SCPI_STATUS_REQUEST_SERVICE = 0x40;

/* The console port's object in ROM: console_port_read, its method table
 * at 0x29dd4, then the prompts (structures.md, "The SCPI lexer's token
 * and port"); the export names no label there. */
static scpi_port* const SCPI_CONSOLE_PORT = (scpi_port*)0x29e18;

/* What a port's read gives for the end of its input or a read error
 * (structures.md, "The SCPI session"). */
enum {
  SCPI_PORT_END = 0x101,
};

/* The first value above a character, which scpi_console_putc writes as
 * CR LF. */
static const int16_t SCPI_PORT_CHARACTERS = 0x100;

/* A node's path word, or a parameter's kind word, that is none. */
enum {
  SCPI_NONE = -1,
};

/* A long parameter as scpi_long_resolve_bound takes it: its kind and,
 * for a number, its value. */
typedef struct scpi_engine_long_parameter {
  int16_t kind; /* +0x00 */
  union {
    int32_t value; /* +0x02: a number's */
    int16_t bound; /* +0x02: a bound's, SCPI_BOUND_MAXIMUM or _MINIMUM */
  };
} scpi_engine_long_parameter;

/* A numeric parameter's kind, as scpi_numeric_set, _get and
 * scpi_long_resolve_bound read it: a number, or MINimum or MAXimum, by
 * the bound after it. */
enum {
  SCPI_VALUE_NUMBER = 0,
  SCPI_VALUE_BOUND = 2,
  SCPI_BOUND_MAXIMUM = 0,
  SCPI_BOUND_MINIMUM = 1,
};

/* The condition scpi_check_range reports, 9 in status_conditions, -222
 * Data out of range (0x2f86e). */
enum {
  SCPI_CONDITION_DATA_OUT_OF_RANGE = 9,
};

/* A number's digits: scpi_number.digits, d0 d1 ... d15, the integer
 * d0d1...d15 times 10 to the exponent, so that exponent + 16 of them
 * are its integer part.  A digit from 5 rounds the last kept up. */
static const uint8_t SCPI_NUMBER_DIGITS = 16;
static const uint8_t SCPI_NUMBER_ROUND_UP = 5;
static const uint8_t SCPI_NUMBER_BASE = 10;

/* The most integer digits each converter takes, the decimal width of
 * its type. */
static const uint8_t SCPI_SHORT_DIGITS = 5;
static const uint8_t SCPI_LONG_DIGITS = 10;

/* The largest magnitude a real parameter takes (0x25ff2), SCPI's 9.9E37,
 * its infinity. */
static const double SCPI_REAL_LIMIT = 9.9e37;

/* A string parameter as scpi_parse_string gives it its handler. */
typedef struct scpi_engine_string_value {
  char* text;      /* +0x00 */
  uint16_t length; /* +0x04 */
} scpi_engine_string_value;

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

/* A converted parameter's unit, the matched unit's index plus one, is
 * stored after its value, where its handler reads it (0x25eda). */
static void scpi_engine_store_unit(void* after_value, int16_t unit) {
  *(int16_t*)after_value = unit;
}

/* A number at a scale as a double; -1 when it does not fit (0x254ba). */
int32_t scpi_number_to_double(scpi_number* number, double* value,
                              int32_t scale) {
  /* The condition codes' Z bit, which the bridge to scpi_bcd_to_double
   * keeps in __ghidra_ccr: set when the number does not fit a double
   * (0x2507a; MC68331UM, the CCR). */
  static const uint8_t CCR_ZERO = 0x04;
  scpi_engine_bcd bcd;

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

/* A number as a boolean, by scpi_number_to_boolean; none with a suffix
 * (0x25de0). */
int32_t scpi_parse_boolean_number(scpi_number* number, void* unused,
                                  uint16_t* value, void* units) {
  if (number->suffix_length != 0) {
    return SCPI_STATUS_SUFFIX_NOT_ALLOWED;
  }
  uint16_t truth;

  scpi_number_to_boolean(number, &truth, 0);
  *value = truth;
  return 0;
}

/* The suffix by scpi_apply_suffix, then scpi_number_to_short (0x25ea6). */
int32_t scpi_parse_short(scpi_number* number, void* unused, int16_t* value,
                         scpi_unit* units) {
  int32_t error = scpi_apply_suffix(number, units);

  if (error != 0) {
    return error;
  }
  error = scpi_number_to_short(number, value, number->scale);
  scpi_engine_store_unit(value + 1, number->unit);
  return error;
}

/* The suffix, then scpi_number_to_unsigned_short (0x25ee6). */
int32_t scpi_parse_unsigned_short(scpi_number* number, void* unused,
                                  uint16_t* value, scpi_unit* units) {
  int32_t error = scpi_apply_suffix(number, units);

  if (error != 0) {
    return error;
  }
  error = scpi_number_to_unsigned_short(number, value, number->scale);
  scpi_engine_store_unit(value + 1, number->unit);
  return error;
}

/* The suffix, then scpi_number_to_long (0x25f26). */
int32_t scpi_parse_long(scpi_number* number, void* unused, int32_t* value,
                        scpi_unit* units) {
  int32_t error = scpi_apply_suffix(number, units);

  if (error != 0) {
    return error;
  }
  error = scpi_number_to_long(number, value, number->scale);
  scpi_engine_store_unit(value + 1, number->unit);
  return error;
}

/* The suffix, then scpi_number_to_unsigned_long (0x25f66). */
int32_t scpi_parse_unsigned_long(scpi_number* number, void* unused,
                                 uint32_t* value, scpi_unit* units) {
  int32_t error = scpi_apply_suffix(number, units);

  if (error != 0) {
    return error;
  }
  error = scpi_number_to_unsigned_long(number, value, number->scale);
  scpi_engine_store_unit(value + 1, number->unit);
  return error;
}

/* The suffix, then a float of magnitude up to SCPI_REAL_LIMIT
 * (0x25fa6). */
int32_t scpi_parse_float(scpi_number* number, void* unused, float* value,
                         scpi_unit* units) {
  int32_t error = scpi_apply_suffix(number, units);

  if (error != 0) {
    return error;
  }
  double parsed;

  if (scpi_number_to_double(number, &parsed, number->scale) != 0 ||
      double_compare(fabs(parsed), SCPI_REAL_LIMIT) < 0) {
    return SCPI_STATUS_DATA_OUT_OF_RANGE;
  }
  *value = double_to_float(parsed);
  scpi_engine_store_unit(value + 1, number->unit);
  return 0;
}

/* The suffix, then a double of magnitude up to SCPI_REAL_LIMIT
 * (0x2602e). */
int32_t scpi_parse_double(scpi_number* number, void* unused, double* value,
                          scpi_unit* units) {
  int32_t error = scpi_apply_suffix(number, units);

  if (error != 0) {
    return error;
  }
  if (scpi_number_to_double(number, value, number->scale) != 0 ||
      double_compare(fabs(*value), SCPI_REAL_LIMIT) < 0) {
    return SCPI_STATUS_DATA_OUT_OF_RANGE;
  }
  scpi_engine_store_unit(value + 1, number->unit);
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
    return SCPI_STATUS_DATA_OUT_OF_RANGE;
  }
  uint16_t magnitude = 0;

  if (places > 0) {
    magnitude = scpi_engine_digits_value(number, places - 1);
    if (magnitude > MOST_TENTHS) {
      return SCPI_STATUS_DATA_OUT_OF_RANGE;
    }
    magnitude = magnitude * SCPI_NUMBER_BASE + number->digits[places - 1];
  }
  if (scpi_engine_rounds_up(number, places)) {
    magnitude++;
  }
  if (magnitude > (number->negative ? MOST_NEGATIVE : MOST_POSITIVE)) {
    return SCPI_STATUS_DATA_OUT_OF_RANGE;
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
    return SCPI_STATUS_DATA_OUT_OF_RANGE;
  }
  uint16_t tens = 0;
  uint16_t units = 0;

  if (places > 0) {
    tens = scpi_engine_digits_value(number, places - 1);
    if (tens > MOST_TENTHS) {
      return SCPI_STATUS_DATA_OUT_OF_RANGE;
    }
    tens *= SCPI_NUMBER_BASE;
    units = number->digits[places - 1];
  }
  if (scpi_engine_rounds_up(number, places)) {
    units++;
  }
  if (tens == MOST_TENTHS * SCPI_NUMBER_BASE && units > MOST_UNITS) {
    return SCPI_STATUS_DATA_OUT_OF_RANGE;
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
    return SCPI_STATUS_DATA_OUT_OF_RANGE;
  }
  uint32_t magnitude = 0;

  if (places > 0) {
    magnitude = scpi_engine_digits_value(number, places - 1);
    if (magnitude > MOST_TENTHS) {
      return SCPI_STATUS_DATA_OUT_OF_RANGE;
    }
    magnitude = magnitude * SCPI_NUMBER_BASE + number->digits[places - 1];
  }
  if (scpi_engine_rounds_up(number, places)) {
    magnitude++;
  }
  if (magnitude > (number->negative ? most_negative : most_positive)) {
    return SCPI_STATUS_DATA_OUT_OF_RANGE;
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
    return SCPI_STATUS_DATA_OUT_OF_RANGE;
  }
  uint32_t tens = 0;
  uint32_t units = 0;

  if (places > 0) {
    tens = scpi_engine_digits_value(number, places - 1);
    if (tens > most_tenths) {
      return SCPI_STATUS_DATA_OUT_OF_RANGE;
    }
    tens *= SCPI_NUMBER_BASE;
    units = number->digits[places - 1];
  }
  if (scpi_engine_rounds_up(number, places)) {
    units++;
  }
  if (tens == most_tenths * SCPI_NUMBER_BASE && units > most_units) {
    return SCPI_STATUS_DATA_OUT_OF_RANGE;
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

/* What scpi_real_to_text is given: 15 significant digits for a double,
 * 6 for a float, and the resolution -308 for every digit (its exponent
 * digits and point in scpi_engine_real_to_text); and the text's length
 * with every digit, the sign, the point, "E", the exponent's sign and
 * three digits.  The text's size is an array's, so #define. */
static const int16_t SCPI_REAL_DOUBLE_DIGITS = 15;
static const int16_t SCPI_REAL_FLOAT_DIGITS = 6;
static const int16_t SCPI_REAL_EVERY_DIGIT = -308;
static const int16_t SCPI_REAL_TEXT_OVERHEAD = 4 + 3;
#define SCPI_REAL_TEXT_SIZE 24

/* scpi_unsigned_to_digits' buffer: digits right-aligned to its index
 * 16, with room before them for a sign or a block's header; its size an
 * array's, so #define. */
static const int16_t SCPI_DIGITS_END = 16 + 1;
#define SCPI_DIGITS_SIZE 20

enum {
  SCPI_ARRAY_BLOCK = 0,
  SCPI_ARRAY_DOUBLES = 1,
  SCPI_ARRAY_SHORTS = 2,
};

/* The character a response's text puts between its values. */
static const char SCPI_SEPARATOR = ',';

/* A text's length. */
static int32_t scpi_engine_length(char* text) {
  int32_t length = 0;

  while (text[length] != 0) {
    length++;
  }
  return length;
}

/* A double as NR3 text, through scpi_real_to_text, which takes its bits
 * as two longs, high first. */
static uint32_t scpi_engine_real_to_text(double value, char* text,
                                         int16_t digits, int16_t resolution) {
  /* Three exponent digits, and 1 for "+0.dE" in place of "+dE" with
   * one digit. */
  static const int16_t REAL_EXPONENT_DIGITS = 3;
  static const int16_t REAL_POINT_ALWAYS = 1;
  union {
    double value;
    int32_t words[2];
  } bits = {value};

  return scpi_real_to_text(bits.words[0], bits.words[1], (uint8_t*)text, digits,
                           REAL_EXPONENT_DIGITS, resolution, REAL_POINT_ALWAYS);
}

/* Writes text through the port. */
static void scpi_engine_write(scpi_port* output, char* text, int32_t length) {
  output->methods->write(output, text, length);
}

/* A string as it is (0x2636e). */
int32_t scpi_format_text(scpi_port* output, char** value) {
  scpi_engine_write(output, *value, scpi_engine_length(*value));
  return 0;
}

/* A string in double quotes, each quote doubled (0x2639e). */
int32_t scpi_format_string(scpi_port* output, char** value) {
  static const char QUOTE = '"';

  output->methods->put(output, QUOTE);
  for (char* text = *value; *text != 0; text++) {
    output->methods->put(output, *text);
    if (*text == QUOTE) {
      output->methods->put(output, QUOTE);
    }
  }
  output->methods->put(output, QUOTE);
  return 0;
}

/* 0 or 1 (0x2640c). */
int32_t scpi_format_boolean(scpi_port* output, int16_t* value) {
  output->methods->put(output, *value == 0 ? '0' : '1');
  return 0;
}

/* Text of a given length, a scpi_token_string's layout; ends the
 * response, which scpi_interpret takes as -440, Query UNTERMINATED
 * after indefinite response, for anything after it (0x26438). */
int32_t scpi_format_counted_text(scpi_port* output, scpi_token_string* text) {
  /* The length a signed word here (0x2643e). */
  scpi_engine_write(output, text->text, (int16_t)text->length);
  return SCPI_STATUS_INDEFINITE_RESPONSE;
}

/* The keyword for a value, from the node's list: the value's index, or
 * the keyword the list maps it from (0x26460). */
int32_t scpi_format_choice(scpi_port* output, int16_t* value,
                           scpi_engine_choices* choices) {
  if (choices == 0 || choices->keywords == 0) {
    return SCPI_STATUS_NO_LIST;
  }
  if (choices->values == 0) {
    char* keyword = choices->keywords[*value];

    scpi_engine_write(output, keyword, scpi_engine_length(keyword));
    return 0;
  }
  for (int16_t index = 0; choices->keywords[index] != 0; index++) {
    if (choices->values[index] == *value) {
      scpi_engine_write(output, choices->keywords[index],
                        scpi_engine_length(choices->keywords[index]));
      return 0;
    }
  }
  return SCPI_STATUS_FORMAT_FAILED;
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
  return SCPI_STATUS_FORMAT_FAILED;
}

/* A sign, + or -, before a long's digits (0x26522). */
int32_t scpi_format_long(scpi_port* output, int32_t* value) {
  char digits[SCPI_DIGITS_SIZE];
  int16_t start;
  bool negative = *value < 0;
  uint32_t magnitude = negative ? -(uint32_t)*value : (uint32_t)*value;
  int32_t error = scpi_unsigned_to_digits(magnitude, digits, &start);

  if (error != 0) {
    return error;
  }
  digits[--start] = negative ? '-' : '+';
  scpi_engine_write(output, &digits[start], SCPI_DIGITS_END - start);
  return 0;
}

/* A short, through scpi_format_long (0x265a2). */
int32_t scpi_format_short(scpi_port* output, int16_t* value) {
  int32_t wide = *value;

  return scpi_format_long(output, &wide);
}

/* + before an unsigned long's digits (0x265c2). */
int32_t scpi_format_unsigned_long(scpi_port* output, uint32_t* value) {
  char digits[SCPI_DIGITS_SIZE];
  int16_t start;
  int32_t error = scpi_unsigned_to_digits(*value, digits, &start);

  if (error != 0) {
    return error;
  }
  digits[--start] = '+';
  scpi_engine_write(output, &digits[start], SCPI_DIGITS_END - start);
  return 0;
}

/* An unsigned short, through scpi_format_unsigned_long (0x2661c). */
int32_t scpi_format_unsigned_short(scpi_port* output, uint16_t* value) {
  uint32_t wide = *value;

  return scpi_format_unsigned_long(output, &wide);
}

/* A definite-length block: #, the count of the length's digits, the
 * length, then the data (0x2663e). */
int32_t scpi_format_block(scpi_port* output, scpi_engine_block* block) {
  static const char BLOCK_START = '#';
  char header[SCPI_DIGITS_SIZE];
  int16_t start;

  if (scpi_unsigned_to_digits(block->length, header, &start) != 0) {
    return SCPI_STATUS_FORMAT_FAILED;
  }
  char count = SCPI_DIGITS_END - start;

  header[--start] = '0' + count;
  header[--start] = BLOCK_START;
  scpi_engine_write(output, &header[start], (char)(count + 2));
  output->methods->write_block(output, block->data, block->length);
  return 0;
}

/* Doubles, comma-separated, by scpi_format_double (0x266d0). */
int32_t scpi_format_double_array(scpi_port* output,
                                 scpi_engine_array* doubles) {
  double* value = doubles->values;

  for (int32_t index = 0; index < doubles->count; index++, value++) {
    if (index != 0) {
      output->methods->put(output, SCPI_SEPARATOR);
    }
    int32_t error = scpi_format_double(output, value);

    if (error != 0) {
      return error;
    }
  }
  return 0;
}

/* Shorts, comma-separated, by scpi_format_short (0x2671c). */
int32_t scpi_format_short_array(scpi_port* output, scpi_engine_array* shorts) {
  int16_t* value = shorts->values;

  for (int32_t index = 0; index < shorts->count; index++, value++) {
    if (index != 0) {
      output->methods->put(output, SCPI_SEPARATOR);
    }
    int32_t error = scpi_format_short(output, value);

    if (error != 0) {
      return error;
    }
  }
  return 0;
}

/* Doubles, comma-separated, by scpi_format_double_resolution, each to
 * its own resolution (0x26768). */
int32_t scpi_format_resolution_array(scpi_port* output,
                                     scpi_engine_resolution_array* doubles) {
  double* value = doubles->values;
  int16_t* resolution = doubles->resolutions;

  for (int32_t index = 0; index < doubles->count;
       index++, value++, resolution++) {
    if (index != 0) {
      output->methods->put(output, SCPI_SEPARATOR);
    }
    int32_t error = scpi_format_double_resolution(output, value, resolution);

    if (error != 0) {
      return error;
    }
  }
  return 0;
}

/* By its kind, a block, a double array or a short array; 0 for another
 * kind (0x267c0). */
int32_t scpi_format_array(scpi_port* output, scpi_engine_array_record* array) {
  switch (array->kind) {
    case SCPI_ARRAY_BLOCK:
      return scpi_format_block(output, array->array);
    case SCPI_ARRAY_DOUBLES:
      return scpi_format_double_array(output, array->array);
    case SCPI_ARRAY_SHORTS:
      return scpi_format_short_array(output, array->array);
  }
  return 0;
}

/* A double, to every one of 15 digits (0x26870). */
int32_t scpi_format_double(scpi_port* output, double* value) {
  char text[SCPI_REAL_TEXT_SIZE];
  int32_t error = scpi_engine_real_to_text(
      *value, text, SCPI_REAL_DOUBLE_DIGITS, SCPI_REAL_EVERY_DIGIT);

  if (error != 0) {
    return error;
  }
  scpi_engine_write(output, text,
                    SCPI_REAL_DOUBLE_DIGITS + SCPI_REAL_TEXT_OVERHEAD);
  return 0;
}

/* A float, to every one of 6 digits (0x268c4). */
int32_t scpi_format_float(scpi_port* output, float* value) {
  char text[SCPI_REAL_TEXT_SIZE];
  int32_t error =
      scpi_engine_real_to_text(float_to_double(*value), text,
                               SCPI_REAL_FLOAT_DIGITS, SCPI_REAL_EVERY_DIGIT);

  if (error != 0) {
    return error;
  }
  scpi_engine_write(output, text,
                    SCPI_REAL_FLOAT_DIGITS + SCPI_REAL_TEXT_OVERHEAD);
  return 0;
}

/* A double to so many digits, or to 15 and a resolution, as its record
 * says (0x2692c). */
int32_t scpi_format_double_precision(scpi_port* output,
                                     scpi_engine_precise_double* value) {
  char text[SCPI_REAL_TEXT_SIZE];
  int32_t error;

  if (value->to_digits == 0) {
    error = scpi_engine_real_to_text(value->value, text,
                                     SCPI_REAL_DOUBLE_DIGITS, value->precision);
  } else {
    error = scpi_engine_real_to_text(value->value, text, value->precision,
                                     SCPI_REAL_EVERY_DIGIT);
  }
  if (error != 0) {
    return error;
  }
  scpi_engine_write(output, text, scpi_engine_length(text));
  return 0;
}

/* A double to 15 digits and the resolution given (0x269a6). */
int32_t scpi_format_double_resolution(scpi_port* output, double* value,
                                      int16_t* resolution) {
  char text[SCPI_REAL_TEXT_SIZE];
  int32_t error = scpi_engine_real_to_text(
      *value, text, SCPI_REAL_DOUBLE_DIGITS, *resolution);

  if (error != 0) {
    return error;
  }
  scpi_engine_write(output, text, scpi_engine_length(text));
  return 0;
}

/* A float to 6 digits and the resolution after it (0x26a06). */
int32_t scpi_format_float_resolution(scpi_port* output,
                                     scpi_engine_float_resolution* real) {
  char text[SCPI_REAL_TEXT_SIZE];
  int32_t error =
      scpi_engine_real_to_text(float_to_double(real->value), text,
                               SCPI_REAL_FLOAT_DIGITS, real->resolution);

  if (error != 0) {
    return error;
  }
  scpi_engine_write(output, text, scpi_engine_length(text));
  return 0;
}

/* The lexer's character classes, the table at 0x277ec, the export's
 * LAB_000277ec, by a port's value, 0 to 0x105. */
enum {
  SCPI_CLASS_LETTER = 0,
  SCPI_CLASS_DIGIT = 1,
  SCPI_CLASS_UNDERSCORE = 2,
  SCPI_CLASS_WHITE = 3, /* controls and space */
  SCPI_CLASS_HASH = 4,
  SCPI_CLASS_QUOTE = 5, /* " and ' */
  SCPI_CLASS_OTHER = 6,
  SCPI_CLASS_COLON = 7,
  SCPI_CLASS_SEMICOLON = 8,
  SCPI_CLASS_QUESTION = 9,
  SCPI_CLASS_OPEN = 10,
  SCPI_CLASS_CLOSE = 11,
  SCPI_CLASS_STAR = 15,
  SCPI_CLASS_PLUS = 16,
  SCPI_CLASS_MINUS = 17,
  SCPI_CLASS_POINT = 18,
  SCPI_CLASS_SLASH = 19,
  SCPI_CLASS_COMMA = 20,
  SCPI_CLASS_NEWLINE = 25,
  SCPI_CLASS_END = 26,        /* 0x100 and SCPI_PORT_END */
  SCPI_CLASS_PORT_ERROR = 27, /* SCPI_PORT_CLOSED to SCPI_PORT_INTERRUPTED */
  SCPI_CLASS_SKIPPED_END =
      28, /* 0x102, which scpi_skip_to_terminator reads past */
  SCPI_CLASS_DOLLAR = 30,
  SCPI_CLASS_AT = 31,
};

/* A port's values above a character: 0x102, which
 * scpi_skip_to_terminator reads past, and the three the lexer reports
 * (scpi_engine_port_error). */
enum {
  SCPI_PORT_SKIPPED_END = 0x102,
  SCPI_PORT_CLOSED = 0x103,
  SCPI_PORT_UNTERMINATED = 0x104,
  SCPI_PORT_INTERRUPTED = 0x105,
};

/* The lexer's results for them, and its own errors: negative
 * conditions but for -102 and -103, which scpi_interpret takes as the
 * input's end (0x29596) and as a failure, which it returns -1 for
 * (0x295be). */
enum {
  SCPI_STATUS_QUERY_UNTERMINATED = -12, /* -420 Query UNTERMINATED */
  SCPI_STATUS_QUERY_INTERRUPTED = -13,  /* -410 Query INTERRUPTED */
  SCPI_STATUS_INVALID_NUMBER = -17,     /* -121 Invalid character in number */
  SCPI_STATUS_TOO_MANY_DIGITS = -23,    /* -124 Too many digits */
  SCPI_STATUS_INVALID_SUFFIX = -31,     /* -131 Invalid suffix */
  SCPI_STATUS_UNDEFINED_HEADER = -32,   /* -113 Undefined header */
  SCPI_STATUS_HEADER_LEAF =
      -33, /* -113 Undefined header: a keyword past a leaf */
  SCPI_STATUS_MISSING_PARAMETER = -37,  /* -109 Missing parameter */
  SCPI_STATUS_EXTRA_PARAMETER = -38,    /* -108 Parameter not allowed */
  SCPI_STATUS_EXPONENT_TOO_LARGE = -45, /* -123 Exponent too large */
  SCPI_STATUS_SUFFIX_TOO_LONG = -53,    /* -134 Suffix too long */
  SCPI_STATUS_INPUT_CLOSED = -102,
  SCPI_STATUS_PORT_FAILED = -103,
};

/* A unit's exponent for one that takes a multiplier before it, and
 * scpi_multiplier_exponent's result for no multiplier (structures.md,
 * "A numeric parameter's units"). */
static const int8_t SCPI_NO_MULTIPLIER = -128;

/* scpi_scan_token's kinds of token, by the character class it starts
 * with (the jump table at 0x26f20). */
enum {
  SCPI_TOKEN_SEMICOLON = 1,
  SCPI_TOKEN_END = 2,         /* a newline or an end */
  SCPI_TOKEN_SKIPPED_END = 3, /* SCPI_PORT_SKIPPED_END */
  SCPI_TOKEN_WHITE = 4,
  SCPI_TOKEN_CLOSE = 5,
  SCPI_TOKEN_AT = 6,
  SCPI_TOKEN_COMMA = 9,
  SCPI_TOKEN_STAR = 10,
  SCPI_TOKEN_QUESTION = 11,
  SCPI_TOKEN_COLON = 12,
  SCPI_TOKEN_NON_DECIMAL = 13, /* #H, #Q or #B and digits */
  SCPI_TOKEN_DECIMAL = 14,
  SCPI_TOKEN_STRING = 15,
  SCPI_TOKEN_OPEN = 16,
  SCPI_TOKEN_DOLLAR = 17, /* $ and a digit 1 to 9 */
  SCPI_TOKEN_BLOCK = 18,  /* a block's header */
  SCPI_TOKEN_KEYWORD = 19,
  SCPI_TOKEN_QUERY = 20,         /* a keyword and ? */
  SCPI_TOKEN_KEYWORD_COLON = 21, /* a keyword and : */
};

/* scpi_scan_token's own errors. */
enum {
  SCPI_STATUS_MNEMONIC_TOO_LONG = -16, /* -112 Program mnemonic too long */
  SCPI_STATUS_INVALID_BLOCK = -18,     /* -161 Invalid block data */
  SCPI_STATUS_INVALID_RADIX = -19,     /* -101 Invalid character */
  SCPI_STATUS_INVALID_DIGIT = -20,     /* -121 Invalid character in number */
  SCPI_STATUS_INVALID_STRING = -21,    /* -151 Invalid string data */
  SCPI_STATUS_INVALID_CHARACTER = -39, /* -101 Invalid character */
  SCPI_STATUS_TOO_MUCH_DATA = -52,     /* -223 Too much data */
};

/* A non-decimal number's radix letters. */
enum {
  SCPI_RADIX_HEXADECIMAL = 'H',
  SCPI_RADIX_OCTAL = 'Q',
  SCPI_RADIX_BINARY = 'B',
};

/* A port value's character class. */
static uint8_t scpi_engine_class(int32_t character) {
  return (&LAB_000277ec)[character];
}

/* The next value from a port, its own routine's (0x26ef6). */
static int16_t scpi_engine_read(scpi_port* input) { return input->read(input); }

/* The lexer's result for a port's error value. */
static int32_t scpi_engine_port_error(int16_t character) {
  switch (character) {
    case SCPI_PORT_CLOSED:
      return SCPI_STATUS_INPUT_CLOSED;
    case SCPI_PORT_UNTERMINATED:
      return SCPI_STATUS_QUERY_UNTERMINATED;
    case SCPI_PORT_INTERRUPTED:
      return SCPI_STATUS_QUERY_INTERRUPTED;
  }
  return SCPI_STATUS_PORT_FAILED;
}

/* A converter of a parameter's token into its place in the call's
 * buffer, by the token's kind (0x26c68): a number's with the node's
 * units, a keyword's with its choices, a non-decimal number's and a
 * string's with neither, a block's with the context's input and arena,
 * and an expression's, from its (, with no token. */
typedef int32_t scpi_engine_table_converter(scpi_token* token, void* call,
                                            void* value, void* table);
typedef int32_t scpi_engine_plain_converter(scpi_token* token, void* call,
                                            void* value);
typedef int32_t scpi_engine_block_converter(scpi_token* token, void* call,
                                            void* value, uint32_t word,
                                            scpi_port* input,
                                            scpi_engine_arena* arena,
                                            uint32_t extra);
typedef int32_t scpi_engine_expression_converter(void* call, void* value,
                                                 uint32_t word,
                                                 scpi_port* input,
                                                 scpi_engine_arena* arena);

/* A parameter's kind, the index of its converter and of
 * scpi_parameter_kind_errors, by the kind of its token (the table at
 * 0x26ed8), from SCPI_TOKEN_NON_DECIMAL to SCPI_TOKEN_KEYWORD. */
enum {
  SCPI_PARAMETER_DECIMAL = 0,
  SCPI_PARAMETER_STRING = 1,
  SCPI_PARAMETER_CHARACTER = 2,
  SCPI_PARAMETER_BLOCK = 3,
  SCPI_PARAMETER_NON_DECIMAL = 4,
  SCPI_PARAMETER_EXPRESSION = 5,
};

static const int8_t scpi_engine_token_parameter_kinds[] = {
    SCPI_PARAMETER_NON_DECIMAL,
    SCPI_PARAMETER_DECIMAL,
    SCPI_PARAMETER_STRING,
    SCPI_PARAMETER_EXPRESSION,
    SCPI_NONE,
    SCPI_PARAMETER_BLOCK,
    SCPI_PARAMETER_CHARACTER,
};

/* scpi_parse_message_unit's own results. */
enum {
  SCPI_STATUS_PARAMETER_DOLLAR =
      -30, /* -181 Invalid outside macro definition: a $ digit as a parameter */
  SCPI_STATUS_INVALID_SEPARATOR = -43, /* -103 Invalid separator */
  SCPI_STATUS_GET_NOT_ALLOWED =
      -54, /* -105 GET not allowed: SCPI_PORT_SKIPPED_END after white space */
  SCPI_STATUS_NO_PARAMETERS = -100, /* not a condition: an empty message unit */
  SCPI_STATUS_SKIPPED_END =
      -101, /* not a condition: SCPI_PORT_SKIPPED_END first */
  SCPI_STATUS_UNREPORTED_GREATEST =
      -60, /* the results from which down it returns as they are */
};

/* The next token of the context's input. */
static int32_t scpi_engine_scan(scpi_engine_context* context,
                                scpi_token* token) {
  return scpi_scan_token(context->input, token, context->arena);
}

/* The next token, past white space. */
static int32_t scpi_engine_scan_past_white(scpi_engine_context* context,
                                           scpi_token* token) {
  int32_t kind = scpi_engine_scan(context, token);

  return kind == SCPI_TOKEN_WHITE ? scpi_engine_scan(context, token) : kind;
}

/* Whether a token kind starts a parameter. */
static bool scpi_engine_is_parameter(int32_t kind) {
  return kind >= SCPI_TOKEN_NON_DECIMAL && kind <= SCPI_TOKEN_KEYWORD;
}

/* Converts a parameter's token into its place in the call's buffer, by
 * the converter of the node's next parameter that takes it
 * (0x26c06 to 0x26d3c). */
static int32_t scpi_engine_convert(scpi_engine_context* context,
                                   scpi_token* token, int32_t kind,
                                   scpi_engine_parameter** parameter,
                                   int16_t* offset) {
  if (kind == SCPI_TOKEN_DOLLAR) {
    return SCPI_STATUS_PARAMETER_DOLLAR;
  }
  int16_t parameter_kind =
      scpi_engine_token_parameter_kinds[kind - SCPI_TOKEN_NON_DECIMAL];
  int32_t error =
      scpi_parameter_match(parameter, parameter_kind, context->call, offset);

  if (error != 0) {
    return error;
  }
  scpi_engine_call* call = context->call;
  scpi_engine_parameter_kinds* kinds = (*parameter)->kinds;
  void* converter = kinds->converters[parameter_kind];
  void* value = call->parameters + *offset;

  switch (kind) {
    case SCPI_TOKEN_DECIMAL:
      error = ((scpi_engine_table_converter*)converter)(token, call, value,
                                                        kinds->units);
      break;
    case SCPI_TOKEN_KEYWORD:
      error = ((scpi_engine_table_converter*)converter)(token, call, value,
                                                        kinds->choices);
      break;
    case SCPI_TOKEN_NON_DECIMAL:
    case SCPI_TOKEN_STRING:
      error = ((scpi_engine_plain_converter*)converter)(token, call, value);
      break;
    case SCPI_TOKEN_BLOCK:
      error = ((scpi_engine_block_converter*)converter)(
          token, call, value, context->word, context->input, context->arena,
          context->field_1e);
      break;
    case SCPI_TOKEN_OPEN:
      error = ((scpi_engine_expression_converter*)converter)(
          call, value, context->word, context->input, context->arena);
      break;
  }
  if (error != 0) {
    return error;
  }
  *offset += kinds->size;
  *parameter = (*parameter)->next;
  return 0;
}

/* Parses one program message unit: its header, then its parameters into
 * the call's buffer, token by token; *reset_path says whether the next
 * unit starts at the root, after a newline (0x26ab6).  On an error it
 * reads to the unit's end unless the error is a command error. */
int32_t scpi_parse_message_unit(scpi_engine_context* parser,
                                int16_t* reset_path) {
  scpi_engine_call* call = parser->call;
  scpi_token token;
  scpi_engine_parameter* parameter;
  int16_t offset;
  int32_t error;
  int32_t kind;

  call->parameters = 0;
  kind = scpi_engine_scan(parser, &token);
  if (kind == SCPI_TOKEN_WHITE) {
    kind = scpi_engine_scan(parser, &token);
    if (kind == SCPI_TOKEN_SKIPPED_END) {
      error = SCPI_STATUS_GET_NOT_ALLOWED;
      goto failed;
    }
  }
  if (kind == SCPI_TOKEN_COLON) {
    scpi_path_reset(parser->path, parser->main_tree);
    kind = scpi_engine_scan(parser, &token);
    if (kind < SCPI_TOKEN_KEYWORD) {
      error = SCPI_STATUS_SYNTAX_ERROR;
      goto failed;
    }
  }
  if (kind >= SCPI_TOKEN_KEYWORD) {
    /* A header, the path's keywords each with a colon after it, then
     * the node's. */
    while (kind == SCPI_TOKEN_KEYWORD_COLON) {
      /* The callees pass on, and do not read, their last
       * argument, which the image gives from D5 unset. */
      error = scpi_header_step(&token.keyword, parser->path, 0);
      if (error != 0) {
        goto failed;
      }
      kind = scpi_engine_scan(parser, &token);
    }
    if (kind < SCPI_TOKEN_KEYWORD) {
      error = SCPI_STATUS_SYNTAX_ERROR;
      goto failed;
    }
    /* scpi_header_resolve and scpi_common_header_resolve keep the
     * decompiler's prototypes, which take the call, the path and the
     * arena as pointers to integers. */
    error = scpi_header_resolve(&token.keyword.length, (int32_t*)call, &offset,
                                (uint32_t*)&parameter, (uint32_t*)parser->path,
                                0, kind == SCPI_TOKEN_QUERY,
                                (int32_t*)parser->arena, 0);
  } else if (kind == SCPI_TOKEN_STAR) {
    kind = scpi_engine_scan(parser, &token);
    if (kind != SCPI_TOKEN_KEYWORD && kind != SCPI_TOKEN_QUERY) {
      error = SCPI_STATUS_SYNTAX_ERROR;
      goto failed;
    }
    error = scpi_common_header_resolve(
        &token.keyword, (int32_t*)call, (uint16_t*)&offset,
        (int32_t*)&parameter, 0, kind == SCPI_TOKEN_QUERY,
        (int32_t*)parser->arena, parser->common_list);
  } else if (kind == SCPI_TOKEN_SKIPPED_END) {
    return SCPI_STATUS_SKIPPED_END;
  } else {
    goto terminator;
  }
  if (error != 0) {
    goto failed;
  }
  /* The parameters, after white space, separated by commas. */
  kind = scpi_engine_scan(parser, &token);
  if (kind == SCPI_TOKEN_WHITE) {
    kind = scpi_engine_scan(parser, &token);
    if (kind != SCPI_TOKEN_END && kind != SCPI_TOKEN_SEMICOLON) {
      if (!scpi_engine_is_parameter(kind)) {
        error = SCPI_STATUS_SYNTAX_ERROR;
        goto failed;
      }
      for (;;) {
        error = scpi_engine_convert(parser, &token, kind, &parameter, &offset);
        if (error != 0) {
          goto failed;
        }
        kind = scpi_engine_scan_past_white(parser, &token);
        if (kind != SCPI_TOKEN_COMMA) {
          break;
        }
        kind = scpi_engine_scan_past_white(parser, &token);
        if (!scpi_engine_is_parameter(kind)) {
          break;
        }
      }
    }
  }
terminator:
  if (kind == SCPI_TOKEN_END) {
    *reset_path = 1;
  } else if (kind == SCPI_TOKEN_SEMICOLON) {
    *reset_path = 0;
  } else {
    error = SCPI_STATUS_INVALID_SEPARATOR;
    goto failed;
  }
  if (call->parameters == 0) {
    return SCPI_STATUS_NO_PARAMETERS;
  }
  if (parameter != 0) {
    error = scpi_parameters_finish(&parameter, call, &offset);
    if (error != 0) {
      goto failed;
    }
  }
  return 0;

failed:
  /* A lexer's error is the result in place of what it broke. */
  if (kind < SCPI_TOKEN_SEMICOLON || kind > SCPI_TOKEN_KEYWORD_COLON) {
    error = kind;
  }
  if (error <= SCPI_STATUS_UNREPORTED_GREATEST ||
      error == SCPI_STATUS_QUERY_UNTERMINATED ||
      error == SCPI_STATUS_QUERY_INTERRUPTED) {
    return error;
  }
  if (condition_is_command_error(error)) {
    if (kind == SCPI_TOKEN_END || kind == SCPI_TOKEN_SKIPPED_END) {
      parser->input->methods->unread(parser->input);
    }
    return error;
  }
  while (kind > SCPI_TOKEN_SKIPPED_END || kind < SCPI_TOKEN_SEMICOLON) {
    kind = scpi_engine_scan(parser, &token);
  }
  *reset_path = kind != SCPI_TOKEN_SEMICOLON;
  return error;
}

/* A lexer's failure on a character: its port error, or else the
 * error given, the character given back. */
static int32_t scpi_engine_reject(scpi_port* input, int16_t character,
                                  int32_t error) {
  if (scpi_engine_class(character) == SCPI_CLASS_PORT_ERROR) {
    return scpi_engine_port_error(character);
  }
  input->methods->unread(input);
  return error;
}

/* A keyword from its first character: letters in upper case, digits and
 * underscores as they are, to 16 (0x26f8c). */
static int32_t scpi_engine_scan_keyword(scpi_port* input, scpi_keyword* keyword,
                                        int16_t first) {
  static const uint8_t UPPER_CASE = 0xdf;
  /* A keyword's most characters. */
  static const int16_t KEYWORD_MOST = 16;
  int16_t count = 0;
  int16_t character = first;
  uint8_t class = SCPI_CLASS_LETTER;

  for (;;) {
    keyword->text[count++] =
        class == SCPI_CLASS_LETTER ? character & UPPER_CASE : character;
    character = scpi_engine_read(input);
    class = scpi_engine_class(character);
    if (class > SCPI_CLASS_UNDERSCORE) {
      break;
    }
    if (count >= KEYWORD_MOST) {
      return SCPI_STATUS_MNEMONIC_TOO_LONG;
    }
  }
  keyword->length = count;
  if (character == '?') {
    return SCPI_TOKEN_QUERY;
  }
  if (character == ':') {
    return SCPI_TOKEN_KEYWORD_COLON;
  }
  return scpi_engine_reject(input, character, SCPI_TOKEN_KEYWORD);
}

/* A block's header after its #: a digit, 0 for an indefinite block, or
 * the count of the length's digits and the length (0x27076). */
static int32_t scpi_engine_scan_block(scpi_port* input, scpi_token_block* block,
                                      int16_t count_digit) {
  int16_t digits = count_digit - '0';

  if (digits == 0) {
    block->indefinite = 1;
    return SCPI_TOKEN_BLOCK;
  }
  block->indefinite = 0;
  uint32_t length = 0;

  for (int16_t index = 0; index < digits; index++) {
    int16_t character = scpi_engine_read(input);

    if (scpi_engine_class(character) != SCPI_CLASS_DIGIT) {
      return scpi_engine_reject(input, character, SCPI_STATUS_INVALID_BLOCK);
    }
    length = length * SCPI_NUMBER_BASE + (character - '0');
  }
  block->length = length;
  return SCPI_TOKEN_BLOCK;
}

/* A non-decimal number after its # and radix letter, base 2 to the
 * bits; digits past 32 bits make it out of range (0x27148). */
static int32_t scpi_engine_scan_radix(scpi_port* input, scpi_token* token,
                                      int16_t base, int16_t bits) {
  static const uint8_t UPPER_CASE = 0xdf;
  static const int16_t LONG_BITS = 32;
  /* The value of a letter digit in upper case, 'A' as 10. */
  static const int16_t LETTER_DIGIT_BIAS = 'A' - 10;
  /* The value's top digit, which must be clear for another. */
  uint32_t top = (uint32_t)(base - 1) << (LONG_BITS - bits);
  uint32_t value = 0;
  bool first = true;
  bool fits = true;

  for (;;) {
    int16_t character = scpi_engine_read(input);
    uint8_t class = scpi_engine_class(character);
    int16_t digit;

    if (class == SCPI_CLASS_LETTER) {
      digit = (character & UPPER_CASE) - LETTER_DIGIT_BIAS;
    } else if (class == SCPI_CLASS_DIGIT) {
      digit = character - '0';
    } else if (first) {
      return scpi_engine_reject(input, character, SCPI_STATUS_INVALID_DIGIT);
    } else {
      input->methods->unread(input);
      if (class == SCPI_CLASS_PORT_ERROR) {
        return scpi_engine_port_error(character);
      }
      if (!fits) {
        return SCPI_STATUS_DATA_OUT_OF_RANGE;
      }
      token->non_decimal.base = base;
      token->non_decimal.value = value;
      return SCPI_TOKEN_NON_DECIMAL;
    }
    if (digit >= base) {
      return SCPI_STATUS_INVALID_DIGIT;
    }
    if ((value & top) != 0) {
      fits = false;
    } else {
      value = value << bits | digit;
      first = false;
    }
  }
}

/* What follows a #: a block's header, or a non-decimal number by its
 * radix letter (0x27064). */
static int32_t scpi_engine_scan_hash(scpi_port* input, scpi_token* token) {
  static const uint8_t UPPER_CASE = 0xdf;
  static const int16_t HEXADECIMAL = 16;
  static const int16_t OCTAL = 8;
  static const int16_t BINARY = 2;
  int16_t character = scpi_engine_read(input);

  if (scpi_engine_class(character) == SCPI_CLASS_DIGIT) {
    return scpi_engine_scan_block(input, &token->block, character);
  }
  switch (character & UPPER_CASE) {
    case SCPI_RADIX_HEXADECIMAL:
      return scpi_engine_scan_radix(input, token, HEXADECIMAL, 4);
    case SCPI_RADIX_BINARY:
      return scpi_engine_scan_radix(input, token, BINARY, 1);
    case SCPI_RADIX_OCTAL:
      return scpi_engine_scan_radix(input, token, OCTAL, 3);
  }
  return scpi_engine_reject(input, character, SCPI_STATUS_INVALID_RADIX);
}

/* A quoted string, each doubled quote one, into room the arena gives:
 * its text, NUL-terminated, the room after it given back (0x27212). */
static int32_t scpi_engine_scan_string(scpi_port* input,
                                       scpi_token_string* string, int16_t quote,
                                       scpi_engine_arena* arena) {
  /* A string's most characters with its NUL, the room scpi_scan_token
   * takes for it from the arena. */
  static const int16_t STRING_SIZE = 0xff;
  char* text = arena_alloc(STRING_SIZE, arena);
  int16_t count = 0;
  int16_t character;

  if (text == 0) {
    return SCPI_STATUS_TOO_MUCH_DATA;
  }
  do {
    character = scpi_engine_read(input);
    if (character >= SCPI_PORT_CHARACTERS) {
      goto ended;
    }
    if (character == quote && (character = scpi_engine_read(input)) != quote) {
      input->methods->unread(input);
      text[count] = 0;
      string->text = arena_take(text, count + 1, arena);
      string->length = count;
      return SCPI_TOKEN_STRING;
    }
    text[count++] = character;
  } while (count < STRING_SIZE);
  /* Too long: the rest of it, to its closing quote. */
  for (;;) {
    character = scpi_engine_read(input);
    if (character >= SCPI_PORT_CHARACTERS) {
      goto ended;
    }
    if (character == quote && (character = scpi_engine_read(input)) != quote) {
      break;
    }
  }
  input->methods->unread(input);
  return SCPI_STATUS_TOO_MUCH_DATA;
ended:
  return scpi_engine_reject(input, character, SCPI_STATUS_INVALID_STRING);
}

/* The SCPI lexer: the next token from the input, its kind or an error,
 * filling the token for a keyword, a number, a string, a block's header
 * or a $ digit; a string's text from the arena (0x26ee0). */
int32_t scpi_scan_token(scpi_port* input, scpi_token* token,
                        scpi_engine_arena* arena) {
  int16_t character = scpi_engine_read(input);

  switch (scpi_engine_class(character)) {
    case SCPI_CLASS_LETTER:
      return scpi_engine_scan_keyword(input, &token->keyword, character);
    case SCPI_CLASS_DIGIT:
    case SCPI_CLASS_POINT:
      token->number.negative = 0;
      return scpi_scan_number(&token->number, 1, character, input);
    case SCPI_CLASS_PLUS:
    case SCPI_CLASS_MINUS: {
      token->number.negative = character == '-';
      int16_t next = scpi_engine_read(input);
      uint8_t class = scpi_engine_class(next);

      if (class == SCPI_CLASS_DIGIT || class == SCPI_CLASS_POINT) {
        return scpi_scan_number(&token->number, 1, next, input);
      }
      return scpi_engine_reject(input, next, SCPI_STATUS_INVALID_NUMBER);
    }
    case SCPI_CLASS_WHITE:
      do {
        character = scpi_engine_read(input);
      } while (scpi_engine_class(character) == SCPI_CLASS_WHITE);
      input->methods->unread(input);
      return SCPI_TOKEN_WHITE;
    case SCPI_CLASS_HASH:
      return scpi_engine_scan_hash(input, token);
    case SCPI_CLASS_QUOTE:
      return scpi_engine_scan_string(input, &token->string, character, arena);
    case SCPI_CLASS_DOLLAR: {
      int16_t digit = scpi_engine_read(input);

      if (scpi_engine_class(digit) == SCPI_CLASS_DIGIT && digit != '0') {
        token->digit = digit - '0';
        return SCPI_TOKEN_DOLLAR;
      }
      return scpi_engine_reject(input, digit, SCPI_STATUS_INVALID_CHARACTER);
    }
    case SCPI_CLASS_PORT_ERROR:
      return scpi_engine_port_error(character);
    case SCPI_CLASS_COLON:
      return SCPI_TOKEN_COLON;
    case SCPI_CLASS_SEMICOLON:
      return SCPI_TOKEN_SEMICOLON;
    case SCPI_CLASS_QUESTION:
      return SCPI_TOKEN_QUESTION;
    case SCPI_CLASS_OPEN:
      return SCPI_TOKEN_OPEN;
    case SCPI_CLASS_CLOSE:
      return SCPI_TOKEN_CLOSE;
    case SCPI_CLASS_STAR:
      return SCPI_TOKEN_STAR;
    case SCPI_CLASS_COMMA:
      return SCPI_TOKEN_COMMA;
    case SCPI_CLASS_NEWLINE:
    case SCPI_CLASS_END:
      return SCPI_TOKEN_END;
    case SCPI_CLASS_SKIPPED_END:
      return SCPI_TOKEN_SKIPPED_END;
    case SCPI_CLASS_AT:
      return SCPI_TOKEN_AT;
  }
  return SCPI_STATUS_INVALID_CHARACTER;
}

/* Reads to the next newline or an end, then gives the last value back
 * (0x27388). */
void scpi_skip_to_terminator(scpi_port* input) {
  scpi_port_read_function* read = input->read;
  int16_t character;

  do {
    character = read(input);
    if (character >= SCPI_PORT_CHARACTERS &&
        character != SCPI_PORT_SKIPPED_END) {
      break;
    }
  } while (scpi_engine_class(character) != SCPI_CLASS_NEWLINE);
  input->methods->unread(input);
}

/* Scans a decimal number's mantissa, exponent and units suffix into a
 * parsed number, from the character given when have_character is set,
 * else from the next; SCPI_TOKEN_DECIMAL, or an error (0x273cc).  Up to
 * 16 digits are kept, leading zeros dropped; shift is what the point's
 * place and the digits dropped take from the written exponent. */
int32_t scpi_scan_number(scpi_number* number, int32_t have_character,
                         int32_t character, scpi_port* input) {
  /* The most a number's exponent and shift take, a word's. */
  static const int16_t WORD_LEAST = -0x8000;
  static const int16_t WORD_MOST = 0x7fff;
  /* The suffix's last index it writes: 12 in suffix's 12 bytes, so
   * that a 13th character lands in scale (structures.md, "The SCPI
   * lexer's token and port"). */
  static const int16_t SUFFIX_LAST = 12;
  int16_t next = (int16_t)have_character != 0
                     ? (int16_t)character
                     : (int16_t)scpi_engine_read(input);
  uint8_t class = scpi_engine_class(next);
  int16_t digits = 0;
  int16_t point = 0;
  int16_t shift = 0;
  bool leading_zero = false;

  /* Leading zeros and the point before the first other digit. */
  for (;; next = scpi_engine_read(input), class = scpi_engine_class(next)) {
    if (next == '0') {
      shift -= point;
      leading_zero = true;
      if (shift == WORD_LEAST) {
        return SCPI_STATUS_TOO_MANY_DIGITS;
      }
    } else if (next == '.') {
      if (point != 0) {
        goto invalid;
      }
      point = 1;
    } else if (class == SCPI_CLASS_DIGIT) {
      break;
    } else if (leading_zero) {
      digits = 1;
      number->digits[0] = 0;
      break;
    } else {
      goto not_a_number;
    }
  }
  /* The digits kept. */
  for (;; next = scpi_engine_read(input), class = scpi_engine_class(next)) {
    if (class == SCPI_CLASS_DIGIT) {
      number->digits[digits++] = next - '0';
      shift -= point;
      if (shift == WORD_LEAST) {
        return SCPI_STATUS_TOO_MANY_DIGITS;
      }
      if (digits == SCPI_NUMBER_DIGITS) {
        break;
      }
    } else if (class == SCPI_CLASS_POINT) {
      if (point != 0) {
        goto invalid;
      }
      point = 1;
    } else if (digits != 0) {
      while (digits < SCPI_NUMBER_DIGITS) {
        number->digits[digits++] = 0;
        shift--;
        if (shift == WORD_LEAST) {
          return SCPI_STATUS_TOO_MANY_DIGITS;
        }
      }
      goto exponent;
    } else {
      goto not_a_number;
    }
  }
  /* The digits past 16, dropped. */
  for (;;) {
    next = scpi_engine_read(input);
    class = scpi_engine_class(next);
    if (class == SCPI_CLASS_DIGIT) {
      if (point == 0 && ++shift > WORD_MOST - 1) {
        return SCPI_STATUS_TOO_MANY_DIGITS;
      }
    } else if (class == SCPI_CLASS_POINT) {
      if (point != 0) {
        goto invalid;
      }
      point = 1;
    } else {
      break;
    }
  }
exponent:
  while (class == SCPI_CLASS_WHITE) {
    next = scpi_engine_read(input);
    class = scpi_engine_class(next);
  }
  number->suffix_length = 0;
  int32_t error = scpi_scan_exponent(next, number, input);

  if (error != 0) {
    return error;
  }
  int32_t exponent = number->exponent + shift;

  if (exponent > WORD_MOST || exponent < WORD_LEAST) {
    return SCPI_STATUS_EXPONENT_TOO_LARGE;
  }
  number->exponent = exponent;
  do {
    next = scpi_engine_read(input);
    class = scpi_engine_class(next);
  } while (class == SCPI_CLASS_WHITE);
  /* The units suffix, which starts with a letter or a slash. */
  int16_t length = number->suffix_length;

  if (class == SCPI_CLASS_SLASH || class == SCPI_CLASS_LETTER) {
    while (class == SCPI_CLASS_MINUS || class == SCPI_CLASS_SLASH ||
           class == SCPI_CLASS_DIGIT || class == SCPI_CLASS_POINT ||
           class == SCPI_CLASS_LETTER) {
      if (length > SUFFIX_LAST) {
        return SCPI_STATUS_SUFFIX_TOO_LONG;
      }
      number->suffix[length++] = scpi_char_upper(next);
      next = scpi_engine_read(input);
      class = scpi_engine_class(next);
    }
  }
  if (class == SCPI_CLASS_PORT_ERROR) {
    return scpi_engine_port_error(next);
  }
  input->methods->unread(input);
  number->suffix_length = length;
  return SCPI_TOKEN_DECIMAL;

not_a_number:
  if (class == SCPI_CLASS_PORT_ERROR) {
    return scpi_engine_port_error(next);
  }
invalid:
  input->methods->unread(input);
  return SCPI_STATUS_INVALID_NUMBER;
}

/* Scans a number's exponent, E and a signed integer, after its
 * mantissa; an E with no digits after it starts the suffix (0x2766e). */
int32_t scpi_scan_exponent(int32_t character, scpi_number* number,
                           scpi_port* input) {
  static const char EXPONENT_LETTER = 'E';
  /* The most an exponent takes before another digit, a word's
   * tenth. */
  static const int16_t EXPONENT_MOST_TENTHS = 0xccb;

  if ((int16_t)scpi_char_upper((int16_t)character) != EXPONENT_LETTER) {
    input->methods->unread(input);
    number->exponent = 0;
    return 0;
  }
  int16_t next = scpi_engine_read(input);
  uint8_t class = scpi_engine_class(next);

  if (class != SCPI_CLASS_DIGIT && class != SCPI_CLASS_PLUS &&
      class != SCPI_CLASS_MINUS && class != SCPI_CLASS_WHITE) {
    number->suffix[number->suffix_length++] = EXPONENT_LETTER;
    input->methods->unread(input);
    number->exponent = 0;
    return 0;
  }
  while (class == SCPI_CLASS_WHITE) {
    next = scpi_engine_read(input);
    class = scpi_engine_class(next);
  }
  bool negative = class == SCPI_CLASS_MINUS;

  if (class == SCPI_CLASS_MINUS || class == SCPI_CLASS_PLUS) {
    next = scpi_engine_read(input);
    class = scpi_engine_class(next);
  }
  if (class == SCPI_CLASS_DIGIT) {
    int16_t exponent = next - '0';

    for (;;) {
      next = scpi_engine_read(input);
      if (scpi_engine_class(next) != SCPI_CLASS_DIGIT) {
        break;
      }
      if (exponent > EXPONENT_MOST_TENTHS) {
        return SCPI_STATUS_EXPONENT_TOO_LARGE;
      }
      exponent = exponent * SCPI_NUMBER_BASE + next - '0';
    }
    number->exponent = negative ? -exponent : exponent;
    input->methods->unread(input);
    return 0;
  }
  if (class == SCPI_CLASS_PORT_ERROR) {
    return scpi_engine_port_error(next);
  }
  input->methods->unread(input);
  return SCPI_STATUS_INVALID_NUMBER;
}

/* A letter in upper case, by the lexer's classes (0x277d2). */
uint32_t scpi_char_upper(uint32_t character) {
  /* Clears the lower-case bit, and the high byte of the value's
   * word with it (0x277e2). */
  const uint32_t upper_case = 0xffff00df;

  if (scpi_engine_class(character) == SCPI_CLASS_LETTER) {
    character &= upper_case;
  }
  return character;
}

/* The parameter buffer's word at offset, a parameter's kind (0x27936).
 * The buffer holds each parameter's kind word, then its value, at any
 * offset its values' sizes give. */
static void scpi_engine_mark(scpi_engine_call* call, int16_t offset,
                             int16_t kind) {
  *(int16_t*)(call->parameters + offset) = kind;
}

/* Finds the node's next parameter that takes a token of the kind,
 * marking the optional ones it passes left out, and marks it with the
 * kind; offset is the next parameter's in the call's buffer (0x278f4). */
int32_t scpi_parameter_match(scpi_engine_parameter** next, int32_t kind,
                             scpi_engine_call* call, int16_t* offset) {
  int16_t token = kind;

  for (scpi_engine_parameter* entry = *next;; entry = *next = entry->next) {
    if (entry == 0) {
      return SCPI_STATUS_EXTRA_PARAMETER;
    }
    if (entry->kinds->converters[token] != 0) {
      break;
    }
    if (entry->kinds->optional == 0) {
      return scpi_parameter_kind_errors[token];
    }
    scpi_engine_mark(call, *offset, SCPI_NONE);
    *offset += entry->kinds->size + sizeof(int16_t);
  }
  scpi_engine_mark(call, *offset, token);
  *offset += sizeof(int16_t);
  return 0;
}

/* Marks the node's parameters left over left out, unless one is
 * required (0x2797a). */
uint32_t scpi_parameters_finish(scpi_engine_parameter** next,
                                scpi_engine_call* call, int16_t* offset) {
  for (; *next != 0; *next = (*next)->next) {
    if ((*next)->kinds->optional == 0) {
      return SCPI_STATUS_MISSING_PARAMETER;
    }
    scpi_engine_mark(call, *offset, SCPI_NONE);
    *offset += (*next)->kinds->size + sizeof(int16_t);
  }
  return 0;
}

/* How many characters of a unit's reversed name match the suffix's end,
 * 0 unless all of the name does (0x279d6). */
int16_t scpi_suffix_compare_tail(char* suffix, int32_t length, char* name) {
  int16_t count = length;
  int16_t matched = 0;

  while (matched < count && suffix[count - matched - 1] == name[matched]) {
    matched++;
  }
  return name[matched] == 0 ? matched : 0;
}

/* A unit multiplier's power of ten, from scpi_multipliers, by its one
 * or two letters; SCPI_NO_MULTIPLIER for none (0x27a0c). */
int8_t scpi_multiplier_exponent(int32_t name) {
  scpi_multiplier* entry = scpi_multipliers;

  do {
    if (entry->name == (uint16_t)name) {
      return entry->exponent;
    }
    entry++;
  } while (entry->name != 0);
  return SCPI_NO_MULTIPLIER;
}

/* The unit a suffix names, its index plus one, and its power of ten,
 * allowing a one- or two-letter multiplier before a unit that takes
 * one; 0 for none (0x27a2a). */
int16_t scpi_match_suffix(int32_t length, char* suffix, scpi_unit* units,
                          int16_t* scale) {
  static const int16_t MULTIPLIER_MOST = 2;
  static const uint8_t LETTER_BITS = 8;
  int16_t count = length;

  for (int16_t index = 0; units[index].name != 0; index++) {
    int16_t matched =
        scpi_suffix_compare_tail(suffix, count, units[index].name);

    if (matched == 0 || count - matched > MULTIPLIER_MOST) {
      continue;
    }
    if (count == matched) {
      *scale = units[index].exponent;
      if (*scale == SCPI_NO_MULTIPLIER) {
        *scale = 0;
      }
      return index + 1;
    }
    if (units[index].exponent == SCPI_NO_MULTIPLIER) {
      uint16_t multiplier = suffix[0];

      if (matched + 1 < count) {
        multiplier = (multiplier << LETTER_BITS) + suffix[1];
      }
      *scale = scpi_multiplier_exponent(multiplier);
      if (*scale != SCPI_NO_MULTIPLIER) {
        return index + 1;
      }
    }
  }
  return 0;
}

/* Matches a number's suffix against the node's units, setting its scale
 * and unit (0x27ae4). */
int32_t scpi_apply_suffix(scpi_number* number, scpi_unit* units) {
  if (number->suffix_length == 0) {
    number->unit = 0;
    number->scale = 0;
    return 0;
  }
  if (units == 0) {
    return SCPI_STATUS_SUFFIX_NOT_ALLOWED;
  }
  number->unit = scpi_match_suffix(number->suffix_length, number->suffix, units,
                                   &number->scale);
  if (number->unit == 0) {
    return SCPI_STATUS_INVALID_SUFFIX;
  }
  return 0;
}

/* Compares a token with a keyword pair, its short form, a NUL and the
 * rest of its long form: 0 when the token is either form, else -1 or 1
 * as it sorts before or after (0x27b62). */
int16_t scpi_keyword_compare(char* token, int32_t length, char* keyword) {
  bool short_form = true;

  for (int16_t index = 0; index < (int16_t)length;
       index++, token++, keyword++) {
    if (*keyword < *token) {
      if (*keyword != 0 || !short_form) {
        return 1;
      }
      keyword++;
      if (*keyword < *token) {
        return 1;
      }
      short_form = false;
    }
    if (*token < *keyword) {
      return -1;
    }
  }
  return *keyword == 0 ? 0 : -1;
}

/* A keyword's node in a child list, by bisection; NULL for none
 * (0x27baa). */
scpi_engine_node* scpi_node_find(scpi_engine_child_list* children,
                                 scpi_keyword* keyword) {
  int16_t low = 0;
  int16_t high = children->count - 1;

  while (low <= high) {
    int16_t middle = (high + low) >> 1;
    scpi_engine_node* node = children->children[middle];
    int16_t order =
        scpi_keyword_compare(keyword->text, keyword->length, node->keywords);

    if (order == 0) {
      return node;
    }
    if (order > 0) {
      low = middle + 1;
    } else {
      high = middle - 1;
    }
  }
  return 0;
}

/* Adds a word to a header's path. */
static void scpi_engine_path_add(scpi_engine_path* path, uint16_t word) {
  path->words[path->count++] = word;
}

/* Adds a node's word to the path, when it has one. */
static void scpi_engine_path_add_node(scpi_engine_path* path,
                                      scpi_engine_node* node) {
  if (node->path_word != SCPI_NONE) {
    scpi_engine_path_add(path, node->path_word);
  }
}

/* A keyword's node in the path's list, or else down the default
 * children, adding their words to the path; 0 for none, the path as it
 * was (0x27c22). */
scpi_engine_node* scpi_node_find_with_defaults(scpi_engine_path* header,
                                               scpi_keyword* keyword,
                                               void* unused) {
  scpi_engine_node* node = scpi_node_find(header->list, keyword);
  int16_t count = header->count;

  for (scpi_engine_child_list* list = header->list; node == 0;) {
    if (list->default_child == SCPI_NONE) {
      header->count = count;
      return 0;
    }
    scpi_engine_node* child = list->children[list->default_child];

    scpi_engine_path_add_node(header, child);
    list = child->children;
    if (list == 0) {
      header->count = count;
      return 0;
    }
    node = scpi_node_find(list, keyword);
  }
  return node;
}

/* A keyword's node with its numeric suffix taken off and added to the
 * path, the keyword ending in '*' as numbered nodes' do; or, for a
 * suffix of 1, the plain keyword's (0x27c94).  The suffix is the digits
 * at its end, read from after the character before them, or from that
 * character itself when it sorts before '0' (0x27caa to 0x27cb6). */
scpi_engine_node* scpi_node_find_numbered(scpi_engine_path* header,
                                          scpi_keyword* name, void* unused) {
  static const char NUMBERED = '*';
  static const int32_t SUFFIX_MOST = 0xffff;
  int16_t length = name->length;
  char* last = &name->text[length - 1];
  char* before;

  for (;;) {
    before = last;
    if (*last > '9') {
      break;
    }
    before = last - 1;
    if (*last < '0') {
      break;
    }
    length--;
    last = before;
  }
  int16_t suffix_digits = name->length - length;
  int32_t suffix = 0;

  for (int16_t left = suffix_digits; left > 0; left--) {
    suffix = *++before + suffix * SCPI_NUMBER_BASE - '0';
    if (suffix > SUFFIX_MOST) {
      return 0;
    }
  }
  name->text[length] = NUMBERED;
  name->length = length + 1;
  scpi_engine_node* node = scpi_node_find_with_defaults(header, name, unused);

  if (node != 0) {
    scpi_engine_path_add(header, suffix_digits < 1 ? 1 : suffix);
    return node;
  }
  if (suffix_digits < 1 || suffix != 1) {
    return 0;
  }
  name->length = length;
  node = scpi_node_find_with_defaults(header, name, unused);
  if (node == 0) {
    return 0;
  }
  scpi_engine_path_add_node(header, node);
  return node;
}

/* Moves the path one keyword down a compound header (0x27e9e). */
uint32_t scpi_header_step(scpi_keyword* keyword, scpi_engine_path* header,
                          void* unused) {
  scpi_engine_node* node =
      scpi_node_find_with_defaults(header, keyword, unused);

  if (node != 0) {
    scpi_engine_path_add_node(header, node);
  } else {
    node = scpi_node_find_numbered(header, keyword, unused);
    if (node == 0) {
      return SCPI_STATUS_UNDEFINED_HEADER;
    }
  }
  header->list = node->children;
  return header->list == 0 ? SCPI_STATUS_HEADER_LEAF : 0;
}

/* Sets a header's path back to a root list (0x27efe). */
void scpi_path_reset(scpi_engine_path* path, scpi_engine_child_list* list) {
  path->list = list;
  path->count = 0;
}

/* Looks _TRG up in a list, and whether its node has no setter
 * (0x27f7e). */
scpi_engine_node* scpi_trg_defined(scpi_engine_child_list* list,
                                   uint16_t* no_setter) {
  scpi_keyword keyword;
  char* from = "_TRG";
  char* to = keyword.text;

  while ((*to++ = *from++) != 0) {
  }
  keyword.length = scpi_engine_length(keyword.text);
  scpi_engine_node* node = scpi_node_find(list, &keyword);

  if (node != 0) {
    *no_setter = node->handlers[0].function == 0;
  }
  return node;
}

/* A size rounded up to even, as the arena hands out room. */
static uint32_t scpi_engine_even(int32_t size) { return (size + 1U) & ~1U; }

/* Calls a node's setter or query with the call's parameters, its
 * response and the call, first taking room in the arena for a query's
 * response (0x29642). */
int32_t scpi_call_handler(scpi_engine_call* call, void** response) {
  scpi_engine_handler_function* handler =
      call->node->handlers[call->query].function;

  if (call->query != 0) {
    *response = arena_alloc(call->node->response_size, call->arena);
    if (*response == 0) {
      return SCPI_STATUS_NO_MEMORY;
    }
  }
  return handler(call->parameters, *response, call);
}

/* Writes a query's response items through their formatters, separated
 * by commas, first emptying the console's echo (0x29690). */
int32_t scpi_format_response(scpi_port* output, scpi_engine_response_item* item,
                             scpi_engine_call* call) {
  if (output == scpi_console_port()) {
    scpi_echo_length = 0;
  } else {
    DAT_00102c60 = 0;
  }
  for (int16_t offset = 0;; item++) {
    int32_t error =
        item->format(output, call->response + offset, item->argument, call);

    if (error != 0) {
      return error;
    }
    offset += item->size;
    if (item[1].format == 0) {
      return 0;
    }
    output->methods->put(output, SCPI_SEPARATOR);
  }
}

/* Takes size bytes, rounded up to even, from an arena; 0 when it does
 * not hold them (0x29700). */
void* arena_alloc(int32_t size, scpi_engine_arena* arena) {
  uint8_t* start = arena->next;
  uint8_t* end = start + scpi_engine_even(size);

  if (arena->last < end) {
    return 0;
  }
  arena->next = end;
  return start;
}

/* The bytes left in an arena, and its next free byte (0x2972c). */
void* arena_remaining(int32_t* remaining, scpi_engine_arena* arena) {
  *remaining = arena->last - arena->next + 1;
  return arena->next;
}

/* Takes size bytes, rounded up to even, from a buffer: the buffer, the
 * arena's next free byte the one after them (0x29742). */
void* arena_take(void* buffer, int32_t size, scpi_engine_arena* arena) {
  uint8_t* start = buffer;

  arena->next = start + scpi_engine_even(size);
  return start;
}

/* An arena's next free byte (0x2975e). */
void* arena_current(scpi_engine_arena* arena) { return arena->next; }

/* Sets an arena's next free byte (0x29766). */
void arena_set(void* next, scpi_engine_arena* arena) { arena->next = next; }

/* Makes an arena of a buffer of size bytes, at its start, into its slot
 * (0x29770). */
void arena_init(void* buffer, uint32_t size, scpi_engine_arena** slot) {
  scpi_engine_arena* arena = buffer;
  uint8_t* bytes = buffer;

  *slot = arena;
  arena->next = bytes + sizeof *arena;
  arena->last = bytes + (size & ~1U) - 1;
}

/* 1 when value is within low..high, else reports -222 for the source
 * and 0; *ESE, *SRE and the :STATus enable and transition setters
 * (0x2979c). */
uint32_t scpi_check_range(int32_t low, int32_t value, int32_t high,
                          uint8_t source) {
  if (value < low || value > high) {
    status_report_from(SCPI_CONDITION_DATA_OUT_OF_RANGE, source);
    return 0;
  }
  return 1;
}

/* A long parameter's value: its own for a number, high or low for
 * MAXimum or MINimum, else value; called by nothing (0x297c4).  The
 * image compares a number with low and high to no effect (0x297f0). */
int32_t scpi_long_resolve_bound(int32_t low, int32_t high, int32_t value,
                                int16_t* parameter) {
  scpi_engine_long_parameter* given = (scpi_engine_long_parameter*)parameter;

  if (given->kind == SCPI_VALUE_NUMBER) {
    value = given->value;
  } else if (given->kind == SCPI_VALUE_BOUND) {
    if (given->bound == SCPI_BOUND_MAXIMUM) {
      value = high;
    } else if (given->bound == SCPI_BOUND_MINIMUM) {
      value = low;
    }
  }
  given->value = value;
  return 0;
}

/* Sends a numeric parameter's new value, or its minimum or maximum, to
 * pllc: a float's through its address as values[1], an integer's as
 * values[0], the other 0 (0x2982c). */
void scpi_numeric_set(scpi_numeric_param* param, int32_t index, int16_t kind,
                      int16_t bound, uint32_t float_value,
                      int32_t integer_value, uint8_t source) {
  uint32_t* real = 0;
  int32_t integer = 0;

  if (kind == SCPI_VALUE_BOUND) {
    uint32_t* bounds =
        bound == SCPI_BOUND_MINIMUM ? param->minimum : param->maximum;

    if (param->is_integer == 0) {
      real = &bounds[index];
    } else {
      integer = bounds[index];
    }
  } else if (param->is_integer == 0) {
    real = &float_value;
  } else {
    integer = integer_value;
  }
  pllc_request_c(param, index, integer, real, source);
}

/* A numeric parameter's value, or its minimum or maximum as its validate
 * routine settles it, as a float (0x298fc).  The value and validate's
 * result are a float's bits, or a long's for an integer parameter. */
float scpi_numeric_get(scpi_numeric_param* param, int32_t index, int16_t which,
                       int16_t bound) {
  union {
    uint32_t bits;
    float real;
  } value;

  if (which == SCPI_VALUE_BOUND) {
    uint32_t* bounds =
        bound == SCPI_BOUND_MINIMUM ? param->minimum : param->maximum;
    uint32_t limit = bounds[index];

    value.bits = param->validate(
        &limit, index, (uint32_t*)param->minimum + index,
        (uint32_t*)param->maximum + index, (scpi_step*)param->steps + index, 0);
  } else {
    value.bits = ((uint32_t*)param->value)[index];
  }
  if (param->is_integer != 0) {
    return integer_to_float(value.bits);
  }
  return value.real;
}

/* The conditions console_read's errors raise: -362 Framing error, -363
 * Input buffer overrun, -361 Parity error and -360 Communication error
 * (the kind 3 entries 0x13 to 0x16 of status_conditions, 0x2f8aa). */
enum {
  SCPI_CONDITION_COMMUNICATION_ERROR = 0x13,
  SCPI_CONDITION_PARITY_ERROR = 0x14,
  SCPI_CONDITION_FRAMING_ERROR = 0x15,
  SCPI_CONDITION_INPUT_OVERRUN = 0x16,
};

/* Returns its argument; the LED and DOUGlas queries call it (0x29a18). */
char return_byte(char b) { return b; }

/* Returns its argument; called by nothing (0x29a1e). */
uint32_t long_identity(uint32_t value) { return value; }

/* Copies so many longs with interrupts held off (0x29a24). */
void copy_longs_locked(uint32_t* source, uint32_t* destination, uint8_t count) {
  uint16_t sr = set_interrupt_mask(CPU_IPL_1);

  for (uint8_t index = 0; index < count; index++) {
    destination[index] = source[index];
  }
  set_interrupt_mask(sr);
}

/* Returns its argument; the EFC queries pass pll.efc through it
 * (0x29a62). */
float float_identity(float x) { return x; }

/* Returns 0; most SCPI query handlers call it (0x29ada). */
int32_t return_zero(void) { return 0; }

/* Reports a query's failure, the condition code, for the source
 * return_zero gives, and empties its text response (0x29ade); 72
 * queries call it with -0x20. */
int32_t scpi_query_failed(char code, void* response) {
  scpi_engine_string_value* text = response;

  status_report_from(code, return_zero());
  text->length = 0;
  return 0;
}

/* Makes the SCPI prompt, with the latest error when there is one
 * (0x29b0e). */
void scpi_prompt(int32_t error) {
  if (error != 0) {
    sprintf(scpi_prompt_text, "E%+04d> ", error);
    return;
  }
  char* from = "scpi > ";
  char* to = scpi_prompt_text;

  while ((*to++ = *from++) != 0) {
  }
}

/* Clears the SCPI input, opens the port and prompts; the console port
 * (0x29b42). */
scpi_port* scpi_session_init(void) {
  scpi_echo_length = 0;
  scpi_input_repeat = 0;
  scpi_input_end = scpi_input_line;
  scpi_input_next = scpi_input_line;
  scpi_input_line[0] = 0;
  scpi_port_open();
  scpi_prompt(0);
  status_update_rqs();
  return SCPI_CONSOLE_PORT;
}

/* The console port's object, which queries compare a request's source
 * with (0x29b82). */
scpi_port* scpi_console_port(void) { return SCPI_CONSOLE_PORT; }

/* Closes the SCPI port; scpi_task calls it when scpi_interpret returns
 * (0x29b8a). */
void scpi_session_close(void) { scpi_port_close(); }

/* Returns 0; the four methods at +0x20 to +0x2c (0x29b92). */
int32_t scpi_console_return_zero(void) { return 0; }

/* The console port's read: the next character of the line, reading a
 * line once the last is used, SCPI_PORT_END for its end or a read
 * error, which it reports; the last again once scpi_input_ended has
 * marked it (0x29bce). */
int16_t console_port_read(void) {
  /* console_read's error bits, the DUART's received break and framing
   * error, overrun and parity error (MC68681, SRA), and its own bit 0. */
  static const uint8_t CONSOLE_BREAK_OR_FRAMING = 0xc0;
  static const uint8_t CONSOLE_OVERRUN = 0x10;
  static const uint8_t CONSOLE_PARITY = 0x20;
  static const uint8_t CONSOLE_COMMUNICATION = 0x01;

  if (scpi_input_repeat != 0) {
    scpi_input_repeat = 0;
    return scpi_last_input;
  }
  uint8_t errors = 0;

  if (scpi_input_next > scpi_input_end) {
    if (scpi_prompt_enabled != 0) {
      console_write(scpi_prompt_text);
    }
    scpi_input_next = scpi_input_line;
    scpi_input_end =
        scpi_input_line +
        console_read(scpi_input_line, sizeof scpi_input_line - 1, &errors);
  }
  int16_t character;

  if (errors == 0) {
    character = *scpi_input_next++;
    if (character == 0) {
      character = SCPI_PORT_END;
    }
  } else {
    scpi_input_end = scpi_input_line;
    scpi_input_next = scpi_input_line + 1;
    scpi_input_line[0] = 0;
    character = SCPI_PORT_END;
    if (errors & CONSOLE_BREAK_OR_FRAMING) {
      status_report(SCPI_CONDITION_FRAMING_ERROR);
    }
    if (errors & CONSOLE_OVERRUN) {
      status_report(SCPI_CONDITION_INPUT_OVERRUN);
    }
    if (errors & CONSOLE_PARITY) {
      status_report(SCPI_CONDITION_PARITY_ERROR);
    }
    if (errors & CONSOLE_COMMUNICATION) {
      status_report(SCPI_CONDITION_COMMUNICATION_ERROR);
    }
  }
  scpi_last_input = character;
  return character;
}

/* Has the next read give the last character again (0x29cc6). */
void scpi_input_ended(void) { scpi_input_repeat = 1; }

/* Writes a character to the console, keeping it in the echo buffer, or
 * CR LF for an end (0x29cd0). */
void scpi_console_putc(scpi_port* port, int32_t character) {
  if (character >= SCPI_PORT_CHARACTERS) {
    console_write("\r\n");
    return;
  }
  char text[2] = {character, 0};

  console_write(text);
  scpi_echo_text[scpi_echo_length++] = character;
}

/* scpi_console_write's loop: up to n characters of s, to its NUL
 * (0x29d40). */
void scpi_console_write_loop(scpi_port* port, char* s, int32_t n) {
  for (; n > 0 && *s != 0; n--, s++) {
    scpi_console_putc(port, *s);
  }
}

/* The service request enable, a method of the port (0x29d88). */
uint8_t status_sre_get(void) { return service_request_enable; }

/* The status byte, a method of the port (0x29d90). */
uint8_t status_byte_get(void) { return status_byte; }

/* Sets RQS in the status byte while another bit of it is enabled, and
 * status_request_service with it (0x29d98). */
void status_update_rqs(void) {
  bool request = (status_byte & service_request_enable &
                  ~SCPI_STATUS_REQUEST_SERVICE) != 0;

  if (request) {
    status_byte |= SCPI_STATUS_REQUEST_SERVICE;
  } else {
    status_byte &= ~SCPI_STATUS_REQUEST_SERVICE;
  }
  status_request_service = request;
}

/* scpi_interpret's hook at each message's end and error: 0, no further
 * error (0x29e34). */
uint32_t scpi_message_done(void) { return 0; }

/* scpi_interpret's hook when the input closes: 0 (0x29e38). */
uint32_t scpi_input_closed(void) { return 0; }

/* Prompts again when the status model that changed is the main one
 * (0x29e3c). */
void status_prompt_refresh(status_model* status, int32_t error) {
  if (status == status_object) {
    scpi_prompt(error);
  }
}

/* send_x, pausing so many ticks between tries until it succeeds
 * (0x29e56). */
void send_x_retry(uint32_t exchange, void* message, uint32_t ticks) {
  while ((uint8_t)psos_send_x(exchange, message) != 0) {
    psos_pause(ticks);
  }
}

/* Sends a request and waits for the pllc task's done event, clearing a
 * stale one first (0x29e7e).  get_v's 1 and wait_v's 0s are as the
 * image passes them. */
void request_and_wait(uint32_t exchange, void* message) {
  /* The task event the pllc task signals when it has done a request
   * (structures.md, "The pllc task's SCPI requests"), and the ticks
   * send_x_retry pauses between tries. */
  static const uint32_t PLLC_DONE_EVENT = 0x20;
  static const uint32_t PLLC_RETRY_TICKS = 5;
  uint32_t events = 0;

  psos_get_v(PLLC_DONE_EVENT, 1, &events);
  send_x_retry(exchange, message, PLLC_RETRY_TICKS);
  psos_wait_v(PLLC_DONE_EVENT, 0, 0, &events);
}

/* pllc_request_a to _e: a pllc_request to pllc_exchange, by
 * request_and_wait (structures.md, "The pllc task's SCPI requests").
 * Each leaves +0x00 unset, as the image does. */

/* The descriptor, index, source and a setting's new value (0x29ec2). */
void pllc_request_a(void* param, uint8_t index, uint8_t setting,
                    uint8_t source) {
  pllc_request request;

  request.field_04 = 0;
  request.param = param;
  request.index = index;
  request.setting = setting;
  request.source = source;
  request_and_wait(pllc_exchange, &request);
}

/* The descriptor, index and source (0x29ef6). */
void pllc_request_b(void* param, uint8_t index, uint8_t source) {
  pllc_request request;

  request.field_04 = 0;
  request.param = param;
  request.index = index;
  request.source = source;
  request_and_wait(pllc_exchange, &request);
}

/* The descriptor, index, source, values[0] and values[1] through a
 * pointer (0x29f24). */
void pllc_request_c(void* param, uint8_t index, uint32_t first,
                    uint32_t* second, uint8_t source) {
  pllc_request request;

  request.field_04 = 0;
  request.param = param;
  request.index = index;
  request.source = source;
  request.values[0] = first;
  request.values[1] = *second;
  request_and_wait(pllc_exchange, &request);
}

/* The descriptor, index, source and values[0] (0x29f60). */
void pllc_request_d(void* param, uint8_t index, uint32_t value,
                    uint8_t source) {
  pllc_request request;

  request.field_04 = 0;
  request.param = param;
  request.index = index;
  request.source = source;
  request.values[0] = value;
  request_and_wait(pllc_exchange, &request);
}

/* The descriptor, index, source and both values through a pointer
 * (0x29f94). */
void pllc_request_e(void* param, uint8_t index, uint32_t* values,
                    uint8_t source) {
  pllc_request request;

  request.field_04 = 0;
  request.param = param;
  request.index = index;
  request.source = source;
  request.values[0] = values[0];
  request.values[1] = values[1];
  request_and_wait(pllc_exchange, &request);
}

/* Creates and starts the SCPI task at a priority; -1 when it exists,
 * else the kernel's error (0x29fce). */
uint32_t scpi_task_create(uint32_t priority) {
  static const uint32_t ERROR_BITS = 0xff;
  /* The SCPI task's stacks in words, supervisor and user (psos.md,
   * "spawn"). */
  static const uint32_t TASK_SUPERVISOR_STACK_WORDS = 0x300;
  static const uint32_t TASK_USER_STACK_WORDS = 0x400;
  uint32_t existing;

  if ((uint8_t)psos_ident("scpi", &existing, 0) == 0) {
    return -1;
  }
  uint32_t error = psos_create("scpi", 0, priority, TASK_SUPERVISOR_STACK_WORDS,
                               TASK_USER_STACK_WORDS, 0, &task_scpi) &
                   ERROR_BITS;

  if (error != 0) {
    return error;
  }
  return psos_start(task_scpi, scpi_task) & ERROR_BITS;
}
