/* The installer's copy of the SCPI response's writer
 * (`applications/scpi/format`; scpi_format.c has the primary's), on the
 * installer's console port and echoes.  Written by hand in place of the
 * decompiler's (docs/firmware/README.md, "Hand-written C"); it keeps its
 * generated prototype. */

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
