/* installer/installer_scpi_format.c's functions that are this image's own:
 * the rest are common/installer/installer_scpi_format.c's, the same in
 * every image (docs/firmware/rewrite.md, "Shared C").  Written by hand in
 * place of the decompiler's (docs/firmware/README.md, "Hand-written C");
 * each keeps its generated prototype. */

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
