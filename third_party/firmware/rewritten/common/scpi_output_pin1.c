/* The :OUTPut:PIN1 handlers, none of which the firmware supports, the same
 * in each image that includes this file (docs/firmware/rewrite.md, "Shared
 * C", "A file for some images").  An image whose tree has no :OUTPut:PIN1
 * leaves the file out.  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype. */

/* :OUTPut:PIN1:DELay, refused. */
int32_t scpi_set_OUTPut_PIN1_DELay(void* parameters, void* response,
                                   scpi_call* call) {
  return STATUS_SCPI_UNDEFINED_HEADER;
}

/* :OUTPut:PIN1:DELay?, which fails as the four-letter stubs do, the
 * call passed for the response (scpi_unsupported.c). */
int32_t scpi_query_OUTPut_PIN1_DELay(void* parameters, void* response,
                                     scpi_call* call) {
  return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
}

/* :OUTPut:PIN1:DELay:ALIGnment, which reports an undefined header and
 * succeeds. */
int32_t scpi_set_OUTPut_PIN1_DELay_ALIGnment(void* parameters, void* response,
                                             scpi_call* call) {
  status_report_from(STATUS_UNDEFINED_HEADER, scpi_input_source(call->input));
  return 0;
}

/* :OUTPut:PIN1:DELay:ALIGnment?, failing as :DELay? does. */
int32_t scpi_query_OUTPut_PIN1_DELay_ALIGnment(void* parameters, void* response,
                                               scpi_call* call) {
  return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
}

/* :OUTPut:PIN1:FREQuency, refused. */
int32_t scpi_set_OUTPut_PIN1_FREQuency(void* parameters, void* response,
                                       scpi_call* call) {
  return STATUS_SCPI_UNDEFINED_HEADER;
}

/* :OUTPut:PIN1:FREQuency?, failing as :DELay? does. */
int32_t scpi_query_OUTPut_PIN1_FREQuency(void* parameters, void* response,
                                         scpi_call* call) {
  return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
}
