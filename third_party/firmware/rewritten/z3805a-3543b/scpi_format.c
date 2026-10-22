/* The SCPI response formatters, written by hand in place of the
 * decompiler's (docs/firmware/README.md, "Hand-written C";
 * docs/firmware/modules.md, `applications/scpi/format`).  The exported C
 * includes this file after its own, so the types and prototypes it
 * declares are in scope; each function here keeps its generated
 * prototype.  scpi_format.h holds the bodies its installer copies
 * share. */

/* What scpi_real_to_text is given: 15 significant digits for a double,
 * 6 for a float, and SCPI_REAL_EVERY_DIGIT (scpi.h) for the resolution
 * (its exponent digits and point in scpi_engine_real_to_text); and the
 * text's length
 * with every digit, the sign, the point, "E", the exponent's sign and
 * three digits.  The text's size is an array's, so #define. */
static const int16_t SCPI_REAL_DOUBLE_DIGITS = 15;

static const int16_t SCPI_REAL_FLOAT_DIGITS = 6;

static const int16_t SCPI_REAL_TEXT_OVERHEAD = 4 + 3;

#define SCPI_REAL_TEXT_SIZE 24

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

/* A string as it is (0x2636e). */
int32_t scpi_format_text(scpi_port* output, char** value) {
  return scpi_engine_format_text(output, value);
}

/* A string in double quotes, each quote doubled (0x2639e). */
int32_t scpi_format_string(scpi_port* output, char** value) {
  return scpi_engine_format_string(output, value);
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
  return scpi_engine_format_counted_text(output, text);
}

/* The keyword for a value, from the node's list: the value's index, or
 * the keyword the list maps it from (0x26460). */
int32_t scpi_format_choice(scpi_port* output, int16_t* value,
                           scpi_choices* choices) {
  if (choices == 0 || choices->keywords == 0) {
    return STATUS_SCPI_NO_LIST;
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
  return STATUS_SCPI_FORMAT_FAILED;
}

/* A sign, + or -, before a long's digits (0x26522). */
int32_t scpi_format_long(scpi_port* output, int32_t* value) {
  return scpi_engine_format_long(output, value);
}

/* A short, through scpi_format_long (0x265a2). */
int32_t scpi_format_short(scpi_port* output, int16_t* value) {
  return scpi_engine_format_short(output, value);
}

/* + before an unsigned long's digits (0x265c2). */
int32_t scpi_format_unsigned_long(scpi_port* output, uint32_t* value) {
  return scpi_engine_format_unsigned_long(output, value);
}

/* An unsigned short, through scpi_format_unsigned_long (0x2661c). */
int32_t scpi_format_unsigned_short(scpi_port* output, uint16_t* value) {
  return scpi_engine_format_unsigned_short(output, value);
}

/* A definite-length block: #, the count of the length's digits, the
 * length, then the data (0x2663e). */
int32_t scpi_format_block(scpi_port* output, scpi_block* block) {
  static const char BLOCK_START = '#';
  char header[SCPI_DIGITS_SIZE];
  int16_t start;

  if (scpi_unsigned_to_digits(block->length, header, &start) != 0) {
    return STATUS_SCPI_FORMAT_FAILED;
  }
  char count = SCPI_DIGITS_END - start;

  header[--start] = '0' + count;
  header[--start] = BLOCK_START;
  scpi_engine_write(output, &header[start], (char)(count + 2));
  output->methods->write_block(output, block->data, block->length);
  return 0;
}

/* Doubles, comma-separated, by scpi_format_double (0x266d0). */
int32_t scpi_format_double_array(scpi_port* output, scpi_array* doubles) {
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
int32_t scpi_format_short_array(scpi_port* output, scpi_array* shorts) {
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
                                     scpi_resolution_array* doubles) {
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
int32_t scpi_format_array(scpi_port* output, scpi_array_record* array) {
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

/* A block or resolution array response with the routine that releases
 * it once written, as scpi_format_resolution_array_record takes it. */
typedef struct scpi_format_released_array {
  int8_t kind; /* +0x00: the jump table at 0x26836 */
  uint8_t unused;
  void* array;           /* +0x02: a scpi_block or scpi_resolution_array */
  void (*release)(void); /* +0x06 */
} scpi_format_released_array;

_Static_assert(sizeof(scpi_format_released_array) == 10,
               "scpi_format_released_array");

/* scpi_format_resolution_array_record's kinds. */
enum {
  SCPI_RELEASED_BLOCK = 0,
  SCPI_RELEASED_RESOLUTION_ARRAY = 1,
};

/* By its kind, a block or a resolution array, 0 for another kind, then
 * the record's release, whatever the kind (0x26816). */
int32_t scpi_format_resolution_array_record(scpi_port* output, char* record) {
  scpi_format_released_array* response = (scpi_format_released_array*)record;
  int32_t error;

  switch (response->kind) {
    case SCPI_RELEASED_BLOCK:
      error = scpi_format_block(output, response->array);
      break;
    case SCPI_RELEASED_RESOLUTION_ARRAY:
      error = scpi_format_resolution_array(output, response->array);
      break;
    default:
      error = 0;
      break;
  }
  response->release();
  return error;
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
                                     scpi_precise_double* value) {
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
                                     scpi_float_resolution* real) {
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

/* Writes a query's response items through their formatters, separated
 * by commas, first emptying the console's echo (0x29690). */
int32_t scpi_format_response(scpi_port* output, scpi_response_item* item,
                             scpi_call* call) {
  if (output == scpi_console_port()) {
    scpi_echo_length = 0;
  } else {
    scpi_port_echo_length = 0;
  }
  for (int16_t offset = 0;; item++) {
    int32_t error =
        item->format(output, call->response + offset, item->choices, call);

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
