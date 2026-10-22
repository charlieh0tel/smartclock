/* The installer's copies of the SCPI response formatters and writer
 * (`applications/scpi/format`; scpi_format.c has the primary's): the
 * formatters through scpi_format.h's bodies, which both share, the
 * writer on the installer's console port and echoes.  Written by hand in
 * place of the decompiler's (docs/firmware/README.md, "Hand-written C");
 * each keeps its generated prototype. */

/* A string in double quotes, each quote doubled (installer::0x10453a). */
int32_t installer_scpi_format_string(scpi_port* output, char** value) {
  return scpi_engine_format_string(output, value);
}

/* Text of a given length, a scpi_token_string's layout; ends the
 * response (installer::0x1045a8). */
int32_t installer_scpi_format_counted_text(scpi_port* output,
                                           scpi_token_string* text) {
  return scpi_engine_format_counted_text(output, text);
}

/* A sign, + or -, before a long's digits (installer::0x104612). */
int32_t installer_scpi_format_long(scpi_port* output, int32_t* value) {
  return scpi_engine_format_long(output, value);
}

/* A short, as a long (installer::0x104692). */
int32_t installer_scpi_format_short(scpi_port* output, int16_t* value) {
  return scpi_engine_format_short(output, value);
}

/* + before an unsigned long's digits (installer::0x1046b2). */
int32_t installer_scpi_format_unsigned_long(scpi_port* output,
                                            uint32_t* value) {
  return scpi_engine_format_unsigned_long(output, value);
}

/* Writes a query's response items through their formatters, separated
 * by commas, first emptying the console's echo (installer::0x1072ea). */
int32_t installer_scpi_format_response(scpi_port* output,
                                       scpi_response_item* item,
                                       scpi_call* call) {
  if (output == installer_scpi_console_port()) {
    installer_scpi_echo_length = 0;
  } else {
    installer_scpi_port_echo_length = 0;
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
