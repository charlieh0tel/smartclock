/* scpi_led.c's functions that are this image's own: the rest are
 * common/scpi_led.c's, the same in every image (docs/firmware/rewrite.md,
 * "Shared C").  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype. */

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
