/* scpi_output.c's functions that are this image's own: the rest are
 * common/scpi_output.c's, the same in every image (docs/firmware/rewrite.md,
 * "Shared C").  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype. */

/* :OUTPut:STATe?, as a signed byte (0x2d9f8). */
int32_t scpi_query_OUTPut_STATe(void* parameters, void* response,
                                scpi_call* call) {
  int16_t* value = response;

  *value = (int8_t)nv_settings.output_state;
  return 0;
}

/* :OUTPut:STATe's done hook (0x4800a). */
void output_state_done(void) { output_state_program(nv_settings.output_state); }

/* :OUTPut:STATe: hands pllc the new state. */
int32_t scpi_set_OUTPut_STATe(void* parameters, void* response,
                              scpi_call* call) {
  scpi_word_parameter* given = parameters;

  pllc_request_a(&scpi_param_OUTPut_STATe, 0, given->value,
                 scpi_input_source(call->input));
  return 0;
}

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
