/* From the Z3816A 4001's rewritten/scpi_format.c, the 58503B 1.01.04's own
 * beside common/'s: of its functions, those the 58503B's code carries
 * identical or relocated (docs/firmware/58503b-1.01.04.md, "The link list").
 * Its comments' addresses are the 58503B's, but where they name another
 * image.  Each keeps its generated prototype. */

/* Writes a query's response items through their formatters, separated
 * by commas, first emptying the echo of the output's source, its count
 * the byte by the source of those from 0x103a18, scpi_input_source's
 * REQUEST_SOURCE_SCI, the SCI's, sci_scpi_echo_length, where the Z3805A's
 * compares the port with the console's (0x38254). */
int32_t scpi_format_response(scpi_port* output, scpi_response_item* item,
                             scpi_call* call) {
  (&sci_scpi_echo_length -
   REQUEST_SOURCE_SCI)[(uint8_t)scpi_input_source(output)] = 0;
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
