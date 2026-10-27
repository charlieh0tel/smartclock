/* The installer's copies of the SCPI response formatters and writer
 * (`applications/scpi/format`; scpi_format.c has the primary's): the
 * formatters through scpi_format.h's bodies, which both share, the
 * writer on the installer's console port and echoes.  Written by hand in
 * place of the decompiler's (docs/firmware/README.md, "Hand-written C");
 * each keeps its generated prototype. */

/* A string in double quotes, each quote doubled. */
int32_t installer_scpi_format_string(scpi_port* output, char** value) {
  return scpi_engine_format_string(output, value);
}

/* Text of a given length, a scpi_token_string's layout; ends the
 * response. */
int32_t installer_scpi_format_counted_text(scpi_port* output,
                                           scpi_token_string* text) {
  return scpi_engine_format_counted_text(output, text);
}

/* A sign, + or -, before a long's digits. */
int32_t installer_scpi_format_long(scpi_port* output, int32_t* value) {
  return scpi_engine_format_long(output, value);
}

/* A short, as a long. */
int32_t installer_scpi_format_short(scpi_port* output, int16_t* value) {
  return scpi_engine_format_short(output, value);
}

/* + before an unsigned long's digits. */
int32_t installer_scpi_format_unsigned_long(scpi_port* output,
                                            uint32_t* value) {
  return scpi_engine_format_unsigned_long(output, value);
}

/* A string as it is. */
int32_t installer_scpi_format_text(scpi_port* output, char** value) {
  return scpi_engine_format_text(output, value);
}

/* An unsigned short, as an unsigned long. */
int32_t installer_scpi_format_unsigned_short(scpi_port* output,
                                             uint16_t* value) {
  return scpi_engine_format_unsigned_short(output, value);
}
