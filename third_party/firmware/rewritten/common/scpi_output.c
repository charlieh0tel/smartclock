/* The SCPI :OUTPut tree (docs/scpi/README.md): its handlers, and the
 * hooks and actions pllc runs for its settings, written by hand in place
 * of the decompiler's (docs/firmware/README.md, "Hand-written C";
 * docs/firmware/modules.md, `applications/scpi/handlers/output`).  The
 * exported C includes this file after its own, so the types and
 * prototypes it declares are in scope; each function here keeps its
 * generated prototype.  No manual lists :OUTPut for the Z3801A
 * (097-z3801-01, table 4-2); this firmware refuses its pins' settings
 * and keeps :STATe, which it does nothing with. */

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
  status_report_from(STATUS_UNDEFINED_HEADER, return_zero(call->input));
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

/* :OUTPut:STATe: hands pllc the new state. */
int32_t scpi_set_OUTPut_STATe(void* parameters, void* response,
                              scpi_call* call) {
  scpi_word_parameter* given = parameters;

  pllc_request_a(&scpi_param_OUTPut_STATe, 0, given->value,
                 return_zero(call->input));
  return 0;
}

/* Programs the output's state; this firmware does nothing. */
void output_state_program(uint8_t state) {}
