/* From the Z3815A 4010's rewritten/scpi_format.c, the Z3817A 3752's own beside
 * common/'s: of its functions, those the Z3817A's code carries identical or
 * relocated (docs/firmware/z3817a-3752.md, "The function check").  Its
 * comments' addresses are the Z3817A's, but where they name another image. Each
 * keeps its generated prototype. */

/* Writes a query's response items through their formatters, separated
 * by commas, first emptying the echo of the output's source: the
 * Z3815A's echo counts by source, the SCPI port's and the SCI session's,
 * indexed from the byte before scpi_echo_length, which scpi_input_source's
 * 1, this image's every source, reaches (0x391ce). */
int32_t scpi_format_response(scpi_port* output, scpi_response_item* item,
                             scpi_call* call) {
  ((uint8_t*)&scpi_echo_length -
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
