/* The :SOURce:ROSCillator:HOLDover:HYSTeresis handlers, which the firmware
 * does not support, the same in each image that includes this file
 * (docs/firmware/rewrite.md, "Shared C", "A file for some images").  An
 * image whose tree has no such command leaves the file out.  Written by
 * hand in place of the decompiler's (docs/firmware/README.md,
 * "Hand-written C"); each keeps its generated prototype. */

/* :HOLDover:HYSTeresis: not in this firmware. */
int32_t scpi_set_SOURce_ROSCillator_HOLDover_HYSTeresis(void* parameters,
                                                        void* response,
                                                        scpi_call* call) {
  return STATUS_SCPI_UNDEFINED_HEADER;
}

/* :HOLDover:HYSTeresis?: not in this firmware. */
int32_t scpi_query_SOURce_ROSCillator_HOLDover_HYSTeresis(void* parameters,
                                                          void* response,
                                                          scpi_call* call) {
  return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
}
