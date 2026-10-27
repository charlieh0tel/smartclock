/* scpi_led.c's functions that are this image's own: the rest are
 * common/scpi_led.c's, the same in every image (docs/firmware/rewrite.md,
 * "Shared C").  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype. */

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

/* :LED:ALARm:USER, "Provides a way to directly set Alarm", which this
 * firmware refuses. */
int32_t scpi_set_LED_ALARm_USER(void* parameters, void* response,
                                scpi_call* call) {
  return STATUS_SCPI_UNDEFINED_HEADER;
}

/* :LED:ALARm:USER?, refused as its setter is. */
int32_t scpi_query_LED_ALARm_USER(void* parameters, void* response,
                                  scpi_call* call) {
  return STATUS_SCPI_UNDEFINED_HEADER;
}
