/* The SCPI parameter parsers and the numeric get and set, written by
 * hand in place of the decompiler's (docs/firmware/README.md, "Hand-
 * written C"; docs/firmware/modules.md, `applications/scpi/parameters`).
 * The exported C includes this file after its own, so the types and
 * prototypes it declares are in scope; each function here keeps its
 * generated prototype. */

/* The largest magnitude a real parameter takes (0x25ff2), SCPI's 9.9E37,
 * its infinity. */
static const double SCPI_REAL_LIMIT = 9.9e37;

/* A converted parameter's unit, the matched unit's index plus one, is
 * stored after its value, where its handler reads it (0x25eda). */
static void scpi_engine_store_unit(void* after_value, int16_t unit) {
  *(int16_t*)after_value = unit;
}

/* A string token's text and length, as its handler takes them; 0
 * (0x25d70).  The token is a scpi_token_string, the value a
 * scpi_engine_string_value. */
uint32_t scpi_parse_string(uint16_t* token, void* unused, uint32_t* value) {
  scpi_token_string* string = (scpi_token_string*)token;
  scpi_engine_string_value* given = (scpi_engine_string_value*)value;

  given->text = string->text;
  given->length = string->length;
  return 0;
}

/* A keyword, ON or OFF, as a boolean (0x25d8c).  The token is a
 * scpi_keyword, its text ended with a NUL here. */
uint32_t scpi_parse_boolean(int16_t* token, void* unused, uint16_t* value) {
  scpi_keyword* keyword = (scpi_keyword*)token;

  keyword->text[keyword->length] = 0;
  if (scpi_parameters_same(keyword->text, "ON")) {
    *value = 1;
    return 0;
  }
  if (scpi_parameters_same(keyword->text, "OFF")) {
    *value = 0;
    return 0;
  }
  return STATUS_SCPI_ILLEGAL_PARAMETER_VALUE;
}

/* A keyword of the node's choices, as its index or the value the list
 * gives it (0x25e42).  The token is a scpi_keyword. */
uint32_t scpi_parse_choice(int16_t* token, void* unused, int16_t* value,
                           scpi_choices* choices) {
  scpi_keyword* keyword = (scpi_keyword*)token;

  if (choices == 0 || choices->keywords == 0) {
    return STATUS_SCPI_NO_LIST;
  }
  for (int16_t index = 0; choices->keywords[index] != 0; index++) {
    if (scpi_keyword_compare(keyword->text, keyword->length,
                             choices->keywords[index]) == 0) {
      *value = choices->values == 0 ? index : choices->values[index];
      return 0;
    }
  }
  return STATUS_SCPI_ILLEGAL_PARAMETER_VALUE;
}

/* A number as a boolean, by scpi_number_to_boolean; none with a suffix
 * (0x25de0). */
int32_t scpi_parse_boolean_number(scpi_number* number, void* unused,
                                  uint16_t* value, void* units) {
  if (number->suffix_length != 0) {
    return STATUS_SCPI_SUFFIX_NOT_ALLOWED;
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
    return STATUS_SCPI_DATA_OUT_OF_RANGE;
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
    return STATUS_SCPI_DATA_OUT_OF_RANGE;
  }
  scpi_engine_store_unit(value + 1, number->unit);
  return 0;
}

/* Finds the node's next parameter that takes a token of the kind,
 * marking the optional ones it passes left out, and marks it with the
 * kind; offset is the next parameter's in the call's buffer (0x278f4). */
int32_t scpi_parameter_match(scpi_parameter** next, int32_t kind,
                             scpi_call* call, int16_t* offset) {
  return scpi_parameters_match(next, kind, call, offset);
}

/* Marks the node's parameters left over left out, unless one is
 * required (0x2797a). */
uint32_t scpi_parameters_finish(scpi_parameter** next, scpi_call* call,
                                int16_t* offset) {
  return scpi_parameters_left_out(next, call, offset);
}

/* 1 when value is within low..high, else reports -222 for the source
 * and 0; *ESE, *SRE and the :STATus enable and transition setters
 * (0x2979c). */
uint32_t scpi_check_range(int32_t low, int32_t value, int32_t high,
                          request_source source) {
  if (value < low || value > high) {
    status_report_from(STATUS_DATA_OUT_OF_RANGE, source);
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
                      int32_t integer_value, request_source source) {
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

/* scpi_numeric_set for a parameter of doubles: the new value's address,
 * or its minimum's or maximum's, to pllc; called by nothing (0x298b2).
 * The parameter is a scpi_numeric_param. */
void scpi_numeric_set_double(int32_t param, int32_t index, int16_t kind,
                             int16_t bound, uint32_t* value,
                             request_source source) {
  scpi_numeric_param* numeric = (scpi_numeric_param*)param;

  if (kind == SCPI_VALUE_BOUND) {
    double* bounds =
        bound == SCPI_BOUND_MINIMUM ? numeric->minimum : numeric->maximum;

    value = (uint32_t*)&bounds[index];
  }
  pllc_request_e(numeric, index, value, source);
}

/* A double parameter's validate, which returns the value as validated
 * in D0:D1 (0x299e6), where scpi_validate_function's returns a long. */
typedef double scpi_parameters_validate_double(void* value, int index,
                                               void* minimum, void* maximum,
                                               void* step, int source);

/* scpi_numeric_get for a parameter of doubles: its value, or its
 * minimum or maximum as its validate settles it with the step for it;
 * called by nothing (0x29990). */
double scpi_numeric_get_double(scpi_numeric_param* param, int32_t index,
                               int16_t kind, int16_t bound) {
  if (kind != SCPI_VALUE_BOUND) {
    return ((double*)param->value)[index];
  }
  double* minimum = param->minimum;
  double* maximum = param->maximum;
  double* limit = bound == SCPI_BOUND_MINIMUM ? minimum : maximum;
  scpi_parameters_validate_double* validate =
      (scpi_parameters_validate_double*)param->validate;

  return validate(&limit[index], index, &minimum[index], &maximum[index],
                  (scpi_double_step*)param->group_cell + index, 0);
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
