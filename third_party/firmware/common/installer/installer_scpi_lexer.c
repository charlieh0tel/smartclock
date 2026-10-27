/* The installer's copies of the SCPI lexer's routines
 * (`applications/scpi/lexer`; scpi_lexer.c has the primary's), whose
 * bodies, scpi.h's and scpi_lexer.h's, both share.  Written by hand in place of
 * the decompiler's (docs/firmware/README.md, "Hand-written C"); each keeps its
 * generated prototype. */

/* Scans a number's exponent, E and a signed integer, after its
 * mantissa; an E with no digits after it starts the suffix. */
int32_t installer_scpi_scan_exponent(int32_t character, scpi_number* number,
                                     scpi_port* input) {
  return scpi_engine_scan_exponent(installer_scpi_char_classes, character,
                                   number, input);
}

/* A letter in upper case, by the lexer's classes. */
uint32_t installer_scpi_char_upper(uint32_t character) {
  return scpi_engine_upper(installer_scpi_char_classes, character);
}

/* How many characters of a unit's reversed name match the suffix's end,
 * 0 unless all of the name does. */
int16_t installer_scpi_suffix_compare_tail(char* suffix, int32_t length,
                                           char* name) {
  return scpi_compare_suffix_tail(suffix, length, name);
}

/* A unit multiplier's power of ten by its one or two letters;
 * SCPI_NO_MULTIPLIER for none. */
int8_t installer_scpi_multiplier_exponent(int32_t name) {
  return scpi_engine_multiplier(name);
}

/* The unit a suffix names, its index plus one, and its power of ten,
 * allowing a one- or two-letter multiplier before a unit that takes
 * one; 0 for none. */
int16_t installer_scpi_match_suffix(int32_t length, char* suffix,
                                    scpi_unit* units, int16_t* scale) {
  return scpi_engine_match_suffix(length, suffix, units, scale);
}

/* Matches a number's suffix against the node's units, setting its scale
 * and unit. */
int32_t installer_scpi_apply_suffix(scpi_number* number, scpi_unit* units) {
  return scpi_engine_apply_suffix(number, units);
}

/* The SCPI lexer: the next token from the input, its kind or an error. */
int32_t installer_scpi_scan_token(scpi_port* input, scpi_token* token,
                                  scpi_arena* arena) {
  return scpi_engine_scan_token(installer_scpi_char_classes, input, token,
                                arena);
}

/* Reads to the next newline or an end, then gives the last value back. */
void installer_scpi_skip_to_terminator(scpi_port* input) {
  scpi_engine_skip_to_terminator(installer_scpi_char_classes, input);
}

/* Scans a decimal number's mantissa, exponent and units suffix into a
 * parsed number; SCPI_TOKEN_DECIMAL, or an error. */
int32_t installer_scpi_scan_number(scpi_number* number, int32_t have_character,
                                   int32_t character, scpi_port* input) {
  return scpi_engine_scan_number(installer_scpi_char_classes, number,
                                 have_character, character, input);
}
