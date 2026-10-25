/* The installer's copies of the SCPI parameters' parsers and matching
 * (`applications/scpi/parameters`; scpi_parameters.c has the
 * primary's), whose bodies, scpi_parameters.h's, both share.  Written by
 * hand in place of the decompiler's (docs/firmware/README.md,
 * "Hand-written C"); each keeps its generated prototype. */

/* A string token's text, through value; STATUS_SCPI_INVALID_STRING if
 * the text holds a NUL (installer::0x104046). */
uint32_t installer_scpi_parse_text(scpi_token_string* token, void* unused,
                                   char** value) {
  return scpi_parameters_text(token, value);
}

/* The suffix, then the number as a short (installer::0x104088). */
int32_t installer_scpi_parse_short(scpi_number* number, void* unused,
                                   int16_t* value, scpi_unit* units) {
  return scpi_parameters_short(number, value, units);
}

/* Finds the node's next parameter that takes a token of the kind and
 * marks it with the kind (installer::0x1054f4). */
int32_t installer_scpi_parameter_match(scpi_parameter** next, int32_t kind,
                                       scpi_call* call, int16_t* offset) {
  return scpi_parameters_match(next, kind, call, offset);
}

/* Marks the node's parameters left over left out, unless one is
 * required (installer::0x10557a). */
uint32_t installer_scpi_parameters_finish(scpi_parameter** next,
                                          scpi_call* call, int16_t* offset) {
  return scpi_parameters_left_out(next, call, offset);
}

/* A string token's text and length, as its handler takes them; 0
 * (installer::0x10406c). */
uint32_t installer_scpi_parse_string(uint16_t* token, void* unused,
                                     uint32_t* value) {
  return scpi_parameters_string((scpi_token_string*)token,
                                (scpi_engine_string_value*)value);
}

/* The suffix, then the number as an unsigned short
 * (installer::0x1040c8). */
int32_t installer_scpi_parse_unsigned_short(scpi_number* number, void* unused,
                                            uint16_t* value, scpi_unit* units) {
  return scpi_parameters_unsigned_short(number, value, units);
}

/* The suffix, then the number as a long (installer::0x104108). */
int32_t installer_scpi_parse_long(scpi_number* number, void* unused,
                                  int32_t* value, scpi_unit* units) {
  return scpi_parameters_long(number, value, units);
}

/* The suffix, then the number as an unsigned long
 * (installer::0x104148). */
int32_t installer_scpi_parse_unsigned_long(scpi_number* number, void* unused,
                                           uint32_t* value, scpi_unit* units) {
  return scpi_parameters_unsigned_long(number, value, units);
}
