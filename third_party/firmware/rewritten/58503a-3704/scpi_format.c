/* scpi_format.c's functions that are this image's own: the rest are
 * common/scpi_format.c's, the same in every image (docs/firmware/rewrite.md,
 * "Shared C").  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype. */

/* Writes a query's response items through their formatters, separated
 * by commas, first emptying the echo of the output's source, its count
 * scpi_echo_length's byte by the source: return_zero's 0, the console's,
 * where the Z3805A's compares the port with the console's (0x28926). */
int32_t scpi_format_response(scpi_port* output, scpi_response_item* item,
                             scpi_call* call) {
  (&scpi_echo_length)[(uint8_t)return_zero(output)] = 0;
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
