/* The SCPI :OUTPut tree (docs/scpi/README.md): its handlers, and the
 * hooks and actions pllc runs for its settings, written by hand in place
 * of the decompiler's (docs/firmware/README.md, "Hand-written C";
 * docs/firmware/modules.md, `applications/scpi/handlers/output`).  The
 * exported C includes this file after its own, so the types and
 * prototypes it declares are in scope; each function here keeps its
 * generated prototype.  No manual lists :OUTPut for the Z3801A
 * (097-z3801-01, table 4-2); this firmware refuses its pins' settings
 * and keeps :STATe, which it does nothing with. */

/* :OUTPut:STATe's parameter: a boolean's kind word and value. */
typedef struct scpi_output_parameters {
  int16_t kind;   /* +0x00 */
  uint16_t state; /* +0x02 */
} scpi_output_parameters;

/* :OUTPut:PIN1:DELay, refused (0x2c148). */
int32_t scpi_set_OUTPut_PIN1_DELay(void* parameters, void* response,
                                   void* call) {
  return STATUS_SCPI_UNDEFINED_HEADER;
}

/* :OUTPut:PIN1:DELay?, which fails as the four-letter stubs do, the
 * call passed for the response (scpi_unsupported.c) (0x2c14c). */
int32_t scpi_query_OUTPut_PIN1_DELay(void* parameters, void* response,
                                     void* call) {
  return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
}

/* :OUTPut:PIN1:DELay:ALIGnment, which reports an undefined header and
 * succeeds (0x2c170). */
int32_t scpi_set_OUTPut_PIN1_DELay_ALIGnment(void* parameters, void* response,
                                             void* call) {
  status_report_from(STATUS_UNDEFINED_HEADER, return_zero());
  return 0;
}

/* :OUTPut:PIN1:DELay:ALIGnment?, failing as :DELay? does (0x2c190). */
int32_t scpi_query_OUTPut_PIN1_DELay_ALIGnment(void* parameters, void* response,
                                               void* call) {
  return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
}

/* :OUTPut:PIN1:FREQuency, refused (0x2c1b4). */
int32_t scpi_set_OUTPut_PIN1_FREQuency(void* parameters, void* response,
                                       void* call) {
  return STATUS_SCPI_UNDEFINED_HEADER;
}

/* :OUTPut:PIN1:FREQuency?, failing as :DELay? does (0x2c1b8). */
int32_t scpi_query_OUTPut_PIN1_FREQuency(void* parameters, void* response,
                                         void* call) {
  return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
}

/* :OUTPut:STATe: hands pllc the new state (0x2da36). */
int32_t scpi_set_OUTPut_STATe(void* parameters, void* response, void* call) {
  scpi_output_parameters* given = parameters;

  pllc_request_a(&scpi_param_OUTPut_STATe, 0, given->state, return_zero());
  return 0;
}

/* :OUTPut:STATe?, as a signed byte (0x2da66). */
int32_t scpi_query_OUTPut_STATe(void* parameters, void* response, void* call) {
  int16_t* value = response;

  *value = (int8_t)nv_settings.output_state;
  return 0;
}

/* :OUTPut:STATe's done hook (0x42b7c). */
void output_state_done(void) { output_state_program(nv_settings.output_state); }

/* Programs the output's state; this firmware does nothing (0x42b18). */
void output_state_program(uint8_t state) {}
