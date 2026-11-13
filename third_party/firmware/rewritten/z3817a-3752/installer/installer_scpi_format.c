/* From the Z3815A 4010's rewritten/installer/installer_scpi_format.c, the
 * Z3817A 3752's own beside common/'s: of its functions, those the Z3817A's code
 * carries identical or relocated (docs/firmware/z3817a-3752.md, "The function
 * check").  Its comments' addresses are the Z3817A's, but where they name
 * another image.  Each keeps its generated prototype. */

/* Formats a response's items to the output port, separated by commas,
 * after clearing the echo count of the port's source, its
 * installer_scpi_echo_length entry, where the 58503A's is the byte after
 * its prompt text; the first item's error, or 0 (installer::0x1073d2). */
int32_t installer_scpi_format_response(scpi_port* output,
                                       scpi_response_item* item,
                                       scpi_call* call) {
  installer_scpi_echo_length[(uint8_t)installer_scpi_input_source(output)] = 0;
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
