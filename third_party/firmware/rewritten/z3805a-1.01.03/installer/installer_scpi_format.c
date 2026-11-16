/* The Z3805A 3543B's installer/installer_scpi_format.c, the Z3805A
 * 1.01.03's own beside common/'s, whose installer is the 3543B's byte for
 * byte (docs/firmware/z3805a-1.01.03.md, "The export and the checks"): of
 * its functions, those common/ does not hold, the 3543B's own
 * rewritten/z3805a-3543b/installer/installer_scpi_format.c's.  Its
 * comments' installer addresses are the 3543B's, which are this image's; a
 * primary address is moved to this image's.  Each keeps its generated
 * prototype. */

/* The installer's copies of the SCPI response formatters and writer
 * (`applications/scpi/format`; scpi_format.c has the primary's): the
 * formatters through scpi_format.h's bodies, which both share, the
 * writer on the installer's console port and echoes.  Written by hand in
 * place of the decompiler's (docs/firmware/README.md, "Hand-written C");
 * each keeps its generated prototype. */

/* installer/installer_scpi_format.c's functions that are this image's own: the
 * rest are common/installer/installer_scpi_format.c's, the same in every image
 * (docs/firmware/rewrite.md, "Shared C").  Written by hand in place of the
 * decompiler's (docs/firmware/README.md, "Hand-written C"); each keeps its
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
