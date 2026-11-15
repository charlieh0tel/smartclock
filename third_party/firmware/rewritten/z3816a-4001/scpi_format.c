/* scpi_format.c's functions that are this image's own: the rest are
 * common/scpi_format.c's, the same in every image (docs/firmware/rewrite.md,
 * "Shared C").  From the 58503A 3704's rewritten/scpi_format.c: of its
 * functions, those the Z3816A's code carries (identical or relocated by
 * image_diff, or changed with the same instructions), with the static helpers
 * they use, each the same here but where its comment says otherwise
 * (docs/firmware/z3816a-4001.md, "The link check").  Its comments' addresses
 * are the Z3816A's.  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype. */

/* Writes a query's response items through their formatters, separated
 * by commas, first emptying the echo of the output's source, its count
 * the byte by the source of those from 0x103324, scpi_input_source's
 * REQUEST_SOURCE_SCI, the SCI's, scpi_echo_length, where the Z3805A's
 * compares the port with the console's (0x38b8e). */
int32_t scpi_format_response(scpi_port* output, scpi_response_item* item,
                             scpi_call* call) {
  (&scpi_echo_length - REQUEST_SOURCE_SCI)[(uint8_t)scpi_input_source(output)] =
      0;
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
