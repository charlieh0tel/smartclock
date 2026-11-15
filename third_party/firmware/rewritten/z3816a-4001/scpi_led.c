/* scpi_led.c's functions that are this image's own: the rest are
 * common/scpi_led.c's, the same in every image (docs/firmware/rewrite.md,
 * "Shared C").  From the 58503A 3704's rewritten/scpi_led.c: of its functions,
 * those the Z3816A's code carries (identical or relocated by image_diff, or
 * changed with the same instructions), with the static helpers they use, each
 * the same here but where its comment says otherwise
 * (docs/firmware/z3816a-4001.md, "The link check").  Its comments' addresses
 * are the Z3816A's.  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype. */

/* :LED:GPSLock?, "Queries Lock LED", the loop's (0x3d472). */
int32_t scpi_query_LED_GPSLock(void* parameters, void* response,
                               scpi_call* call) {
  int16_t* value = response;

  *value = return_byte(pll.led_gps_lock);
  return 0;
}

/* :LED:HOLDover?, "Queries Holdover LED", the loop's (0x3d48c). */
int32_t scpi_query_LED_HOLDover(void* parameters, void* response,
                                scpi_call* call) {
  int16_t* value = response;

  *value = return_byte(pll.led_holdover);
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

/* :LED:ALARm:CRITical? and :ACO?: an undefined header (0x3d3ea,
 * 0x3d3fc). */
int32_t scpi_query_LED_ALARm_CRITical(void* parameters, void* response,
                                      scpi_call* call) {
  return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
}

int32_t scpi_query_LED_ALARm_ACO(void* parameters, void* response,
                                 scpi_call* call) {
  return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
}
