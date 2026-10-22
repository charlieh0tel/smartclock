/* The SCPI :LED tree (docs/scpi/README.md; 097-z3801-01, table 4-2):
 * its handlers, written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"; docs/firmware/modules.md,
 * `applications/scpi/handlers/led`).  The exported C includes this file
 * after its own, so the types and prototypes it declares are in scope;
 * each function here keeps its generated prototype.  Each query answers
 * its LED's byte through return_byte, a short; each setter hands pllc
 * the new state. */

/* An :LED setter's parameters: a boolean's kind word and value. */
typedef struct scpi_led_parameters {
  int16_t kind;   /* +0x00 */
  uint16_t state; /* +0x02: 0 off, 1 on */
} scpi_led_parameters;

/* :LED:ACTive, "Sets or queries Active LED" (0x2d920). */
int32_t scpi_set_LED_ACTive(void* parameters, void* response, scpi_call* call) {
  scpi_led_parameters* given = parameters;

  pllc_request_a(&scpi_param_LED_ACTive, 0, given->state,
                 return_zero(call->input));
  return 0;
}

/* :LED:ACTive? (0x2d950). */
int32_t scpi_query_LED_ACTive(void* parameters, void* response,
                              scpi_call* call) {
  int16_t* value = response;

  *value = return_byte(led_active);
  return 0;
}

/* :LED:ALARm?, "Query Alarm LED" (0x2d96a). */
int32_t scpi_query_LED_ALARm(void* parameters, void* response,
                             scpi_call* call) {
  int16_t* value = response;

  *value = return_byte(led_alarm);
  return 0;
}

/* :LED:ALARm:MINor?, which the firmware does not support: it fails as
 * the four-letter stubs do, the call passed for the response
 * (scpi_unsupported.c) (0x2d984). */
int32_t scpi_query_LED_ALARm_MINor(void* parameters, void* response,
                                   scpi_call* call) {
  return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
}

/* :LED:ALARm:USER, "Provides a way to directly set Alarm", which this
 * firmware refuses (0x2d996). */
int32_t scpi_set_LED_ALARm_USER(void* parameters, void* response,
                                scpi_call* call) {
  return STATUS_SCPI_UNDEFINED_HEADER;
}

/* :LED:ALARm:USER?, refused as its setter is (0x2d99a). */
int32_t scpi_query_LED_ALARm_USER(void* parameters, void* response,
                                  scpi_call* call) {
  return STATUS_SCPI_UNDEFINED_HEADER;
}

/* :LED:ENABled, "Sets or queries Enabled LED" (0x2d99e). */
int32_t scpi_set_LED_ENABled(void* parameters, void* response,
                             scpi_call* call) {
  scpi_led_parameters* given = parameters;

  pllc_request_a(&scpi_param_LED_ENABled, 0, given->state,
                 return_zero(call->input));
  return 0;
}

/* :LED:ENABled? (0x2d9ce). */
int32_t scpi_query_LED_ENABled(void* parameters, void* response,
                               scpi_call* call) {
  int16_t* value = response;

  *value = return_byte(led_enabled);
  return 0;
}

/* :LED:GPSLock?, "Queries Lock LED", the loop's (0x2d9e8). */
int32_t scpi_query_LED_GPSLock(void* parameters, void* response,
                               scpi_call* call) {
  int16_t* value = response;

  *value = return_byte(pll.led_gps_lock);
  return 0;
}

/* :LED:HOLDover?, "Queries Holdover LED", the loop's (0x2da02). */
int32_t scpi_query_LED_HOLDover(void* parameters, void* response,
                                scpi_call* call) {
  int16_t* value = response;

  *value = return_byte(pll.led_holdover);
  return 0;
}

/* :LED:TMHValid?, the loop's, in no manual (docs/scpi/undocumented.md)
 * (0x2da1c). */
int32_t scpi_query_LED_TMHValid(void* parameters, void* response,
                                scpi_call* call) {
  int16_t* value = response;

  *value = return_byte(pll.led_tmh_valid);
  return 0;
}
