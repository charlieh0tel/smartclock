/* The :DIAGnostic:SER1:EGResponse handlers, which the firmware does not
 * support, the same in each image that includes this file
 * (docs/firmware/rewrite.md, "Shared C", "A file for some images").  An
 * image whose tree has no such command leaves the file out.  Written by
 * hand in place of the decompiler's (docs/firmware/README.md,
 * "Hand-written C"); each keeps its generated prototype. */

/* :DIAGnostic:SER1:EGResponse: undefined, -113. */
int32_t scpi_set_DIAGnostic_SER1_EGResponse(void* parameters, void* response,
                                            scpi_call* call) {
  return STATUS_SCPI_UNDEFINED_HEADER;
}

/* :DIAGnostic:SER1:EGResponse?: undefined, -113. */
int32_t scpi_query_DIAGnostic_SER1_EGResponse(void* parameters, void* response,
                                              scpi_call* call) {
  return scpi_query_failed(STATUS_SCPI_UNDEFINED_HEADER, call);
}
