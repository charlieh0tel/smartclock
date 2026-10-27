/* The SCPI :LED tree (docs/scpi/README.md; 097-z3801-01, table 4-2):
 * its handlers, written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"; docs/firmware/modules.md,
 * `applications/scpi/handlers/led`).  The exported C includes this file
 * after its own, so the types and prototypes it declares are in scope;
 * each function here keeps its generated prototype.  Each query answers
 * its LED's byte through return_byte, a short; each setter hands pllc
 * the new state. */

/* :LED:ACTive, "Sets or queries Active LED". */
int32_t scpi_set_LED_ACTive(void* parameters, void* response, scpi_call* call) {
  scpi_word_parameter* given = parameters;

  pllc_request_a(&scpi_param_LED_ACTive, 0, given->value,
                 return_zero(call->input));
  return 0;
}

/* :LED:ACTive?. */
int32_t scpi_query_LED_ACTive(void* parameters, void* response,
                              scpi_call* call) {
  int16_t* value = response;

  *value = return_byte(led_active);
  return 0;
}

/* :LED:ALARm?, "Query Alarm LED". */
int32_t scpi_query_LED_ALARm(void* parameters, void* response,
                             scpi_call* call) {
  int16_t* value = response;

  *value = return_byte(led_alarm);
  return 0;
}

/* :LED:ALARm:MINor?, which the firmware does not support: it fails as
 * the four-letter stubs do, the call passed for the response
 * (scpi_unsupported.c). */
int32_t scpi_query_LED_ALARm_MINor(void* parameters, void* response,
                                   scpi_call* call) {
  return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
}

/* :LED:ENABled, "Sets or queries Enabled LED". */
int32_t scpi_set_LED_ENABled(void* parameters, void* response,
                             scpi_call* call) {
  scpi_word_parameter* given = parameters;

  pllc_request_a(&scpi_param_LED_ENABled, 0, given->value,
                 return_zero(call->input));
  return 0;
}

/* :LED:ENABled?. */
int32_t scpi_query_LED_ENABled(void* parameters, void* response,
                               scpi_call* call) {
  int16_t* value = response;

  *value = return_byte(led_enabled);
  return 0;
}
