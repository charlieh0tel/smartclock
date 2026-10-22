/* What SCPI's response formatters share with the installer's copies of
 * them (scpi_format.c, installer_scpi_format.c): their bodies, which
 * both copies' routines call.  The exported C includes the hand-written
 * headers after its own declarations and before the hand-written .c
 * files, so every one of them can use it. */

/* scpi_unsigned_to_digits' buffer: digits right-aligned to its index
 * 16, with room before them for a sign or a block's header; its size an
 * array's, so #define. */
static const int16_t SCPI_DIGITS_END = 16 + 1;

#define SCPI_DIGITS_SIZE 20

/* Writes text through the port. */
static void scpi_engine_write(scpi_port* output, char* text, int32_t length) {
  output->methods->write(output, text, length);
}

/* scpi_format_string's and its installer copy's body: a string in
 * double quotes, each quote doubled. */
static int32_t scpi_engine_format_string(scpi_port* output, char** value) {
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

/* scpi_format_counted_text's and its installer copy's body: text of a
 * given length, a scpi_token_string's layout; ends the response, which
 * scpi_interpret takes as -440, Query UNTERMINATED after indefinite
 * response, for anything after it. */
static int32_t scpi_engine_format_counted_text(scpi_port* output,
                                               scpi_token_string* text) {
  /* The length a signed word (0x2643e). */
  scpi_engine_write(output, text->text, (int16_t)text->length);
  return SCPI_STATUS_INDEFINITE_RESPONSE;
}

/* scpi_format_long's and its installer copy's body: a sign, + or -,
 * before a long's digits. */
static int32_t scpi_engine_format_long(scpi_port* output, int32_t* value) {
  char digits[SCPI_DIGITS_SIZE];
  int16_t start;
  bool negative = *value < 0;
  uint32_t magnitude = negative ? -(uint32_t)*value : (uint32_t)*value;
  int32_t error = scpi_engine_unsigned_to_digits(magnitude, digits, &start);

  if (error != 0) {
    return error;
  }
  digits[--start] = negative ? '-' : '+';
  scpi_engine_write(output, &digits[start], SCPI_DIGITS_END - start);
  return 0;
}

/* scpi_format_short's and its installer copy's body: a short, as a
 * long. */
static int32_t scpi_engine_format_short(scpi_port* output, int16_t* value) {
  int32_t wide = *value;

  return scpi_engine_format_long(output, &wide);
}

/* scpi_format_unsigned_long's and its installer copy's body: + before
 * an unsigned long's digits. */
static int32_t scpi_engine_format_unsigned_long(scpi_port* output,
                                                uint32_t* value) {
  char digits[SCPI_DIGITS_SIZE];
  int16_t start;
  int32_t error = scpi_engine_unsigned_to_digits(*value, digits, &start);

  if (error != 0) {
    return error;
  }
  digits[--start] = '+';
  scpi_engine_write(output, &digits[start], SCPI_DIGITS_END - start);
  return 0;
}
