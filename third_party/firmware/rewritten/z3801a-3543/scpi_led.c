/* scpi_led.c's functions that are this image's own: the rest are
 * common/scpi_led.c's, the same in every image (docs/firmware/rewrite.md,
 * "Shared C").  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype. */

/* :LED:ALARm:USER, "Provides a way to directly set Alarm" (097-z3801-01,
 * p. 4-5): to pllc, through led_alarm's own descriptor, so that it sets
 * the Alarm LED's flag itself, not led_alarm_user.  The Z3805A's refuses
 * the command (0x2c85a). */
int32_t scpi_set_LED_ALARm_USER(void* parameters, void* response,
                                scpi_call* call) {
  scpi_word_parameter* given = parameters;

  pllc_request_a(&scpi_param_LED_ALARm, 0, (uint8_t)given->value,
                 return_zero(call->input));
  return 0;
}

/* :LED:ALARm:USER?: led_alarm, as :LED:ALARm? gives it (0x2c88a). */
int32_t scpi_query_LED_ALARm_USER(void* parameters, void* response,
                                  scpi_call* call) {
  int16_t* value = response;

  *value = return_byte(led_alarm);
  return 0;
}

/* :LED:GPSLock?, "Queries Lock LED", the loop's (0x2c8ee). */
int32_t scpi_query_LED_GPSLock(void* parameters, void* response,
                               scpi_call* call) {
  int16_t* value = response;

  *value = return_byte(pll.led_gps_lock);
  return 0;
}

/* :LED:HOLDover?, "Queries Holdover LED", the loop's (0x2c908). */
int32_t scpi_query_LED_HOLDover(void* parameters, void* response,
                                scpi_call* call) {
  int16_t* value = response;

  *value = return_byte(pll.led_holdover);
  return 0;
}

/* :LED:TMHValid?, the loop's, in no manual (docs/scpi/undocumented.md)
 * (0x2c922). */
int32_t scpi_query_LED_TMHValid(void* parameters, void* response,
                                scpi_call* call) {
  int16_t* value = response;

  *value = return_byte(pll.led_tmh_valid);
  return 0;
}
