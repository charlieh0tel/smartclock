/* What the SCPI parameters' parsers and matching share with the
 * installer's copies of them (scpi_parameters.c,
 * installer_scpi_parameters.c): their bodies, which both copies'
 * routines call.  The exported C includes the
 * hand-written headers after its own declarations and before the
 * hand-written .c files, so every one of them can use it. */

/* The parameter buffer's word at offset, a parameter's kind (0x26b8a).
 * The buffer holds each parameter's kind word, then its value, at any
 * offset its values' sizes give. */
static void scpi_parameters_mark(scpi_call* call, int16_t offset,
                                 int16_t kind) {
  *(int16_t*)(call->parameters + offset) = kind;
}

/* scpi_parameter_match's and its installer copy's body: finds the
 * node's next parameter that takes a token of the kind, marking the
 * optional ones it passes left out, and marks it with the kind; offset
 * is the next parameter's in the call's buffer. */
static int32_t scpi_parameters_match(scpi_parameter** next, int32_t kind,
                                     scpi_call* call, int16_t* offset) {
  /* The condition for a parameter of each kind where none is allowed:
   * the image's table at 0x26c1e, scpi_parameter_kind_errors; the
   * installer's at installer::0x1055ca holds the same. */
  static const int16_t KIND_ERRORS[] = {
      [SCPI_PARAMETER_DECIMAL] = STATUS_SCPI_NUMERIC_NOT_ALLOWED,
      [SCPI_PARAMETER_STRING] = STATUS_SCPI_STRING_NOT_ALLOWED,
      [SCPI_PARAMETER_CHARACTER] = STATUS_SCPI_CHARACTER_NOT_ALLOWED,
      [SCPI_PARAMETER_BLOCK] = STATUS_SCPI_BLOCK_NOT_ALLOWED,
      [SCPI_PARAMETER_NON_DECIMAL] = STATUS_SCPI_NUMERIC_NOT_ALLOWED,
      [SCPI_PARAMETER_EXPRESSION] = STATUS_SCPI_EXPRESSION_NOT_ALLOWED,
  };
  int16_t token = kind;

  for (scpi_parameter* entry = *next;; entry = *next = entry->next) {
    if (entry == 0) {
      return STATUS_SCPI_EXTRA_PARAMETER;
    }
    if (entry->kinds->converters[token] != 0) {
      break;
    }
    if (entry->kinds->optional == 0) {
      return KIND_ERRORS[token];
    }
    scpi_parameters_mark(call, *offset, SCPI_NONE);
    *offset += entry->kinds->size + sizeof(int16_t);
  }
  scpi_parameters_mark(call, *offset, token);
  *offset += sizeof(int16_t);
  return 0;
}

/* scpi_parameters_finish's and its installer copy's body: marks the
 * node's parameters left over left out, unless one is required. */
static uint32_t scpi_parameters_left_out(scpi_parameter** next, scpi_call* call,
                                         int16_t* offset) {
  for (; *next != 0; *next = (*next)->next) {
    if ((*next)->kinds->optional == 0) {
      return STATUS_SCPI_MISSING_PARAMETER;
    }
    scpi_parameters_mark(call, *offset, SCPI_NONE);
    *offset += (*next)->kinds->size + sizeof(int16_t);
  }
  return 0;
}

/* A converted parameter's unit, the matched unit's index plus one, is
 * stored after its value, where its handler reads it (0x2512e). */
static void scpi_engine_store_unit(void* after_value, int16_t unit) {
  *(int16_t*)after_value = unit;
}

/* scpi_parse_text's and its installer copy's body: a string token's
 * text, through value; STATUS_SCPI_INVALID_STRING if the text holds a
 * NUL, which its length counts past.  Its lengths compare as the
 * image's, the token's signed. */
static uint32_t scpi_parameters_text(scpi_token_string* token, char** value) {
  if ((int32_t)libc_length(token->text) != (int16_t)token->length) {
    return STATUS_SCPI_INVALID_STRING;
  }
  *value = token->text;
  return 0;
}

/* scpi_parse_string's and its installer copy's body: a string token's
 * text and length, as its handler takes them; 0. */
static uint32_t scpi_parameters_string(scpi_token_string* token,
                                       scpi_engine_string_value* value) {
  value->text = token->text;
  value->length = token->length;
  return 0;
}

/* scpi_parse_short's and its installer copy's body: the suffix, then
 * the number as a short, and its unit after it. */
static int32_t scpi_parameters_short(scpi_number* number, int16_t* value,
                                     scpi_unit* units) {
  int32_t error = scpi_engine_apply_suffix(number, units);

  if (error != 0) {
    return error;
  }
  error = scpi_engine_number_to_short(number, value, number->scale);
  scpi_engine_store_unit(value + 1, number->unit);
  return error;
}

/* scpi_parse_unsigned_short's and its installer copy's body: the
 * suffix, then the number as an unsigned short, and its unit after
 * it. */
static int32_t scpi_parameters_unsigned_short(scpi_number* number,
                                              uint16_t* value,
                                              scpi_unit* units) {
  int32_t error = scpi_engine_apply_suffix(number, units);

  if (error != 0) {
    return error;
  }
  error = scpi_engine_number_to_unsigned_short(number, value, number->scale);
  scpi_engine_store_unit(value + 1, number->unit);
  return error;
}

/* scpi_parse_long's and its installer copy's body: the suffix, then the
 * number as a long, and its unit after it. */
static int32_t scpi_parameters_long(scpi_number* number, int32_t* value,
                                    scpi_unit* units) {
  int32_t error = scpi_engine_apply_suffix(number, units);

  if (error != 0) {
    return error;
  }
  error = scpi_engine_number_to_long(number, value, number->scale);
  scpi_engine_store_unit(value + 1, number->unit);
  return error;
}

/* scpi_parse_unsigned_long's and its installer copy's body: the suffix,
 * then the number as an unsigned long, and its unit after it. */
static int32_t scpi_parameters_unsigned_long(scpi_number* number,
                                             uint32_t* value,
                                             scpi_unit* units) {
  int32_t error = scpi_engine_apply_suffix(number, units);

  if (error != 0) {
    return error;
  }
  error = scpi_engine_number_to_unsigned_long(number, value, number->scale);
  scpi_engine_store_unit(value + 1, number->unit);
  return error;
}
