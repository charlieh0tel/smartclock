/* From the 58503B 1.01.04's rewritten/scpi_led.c, the Z3805A 1.01.03's own
 * beside common/'s: of its functions, those the Z3805A 1.01.03's code
 * carries identical or relocated (docs/firmware/z3805a-1.01.03.md, "The
 * link list"). Its comments' addresses are the Z3805A 1.01.03's, but where
 * they name another image. Each keeps its generated prototype. */

/* :LED:GPSLock?, "Queries Lock LED", the loop's (0x2f49c). */
int32_t scpi_query_LED_GPSLock(void* parameters, void* response,
                               scpi_call* call) {
  int16_t* value = response;

  *value = return_byte(pll.led_gps_lock);
  return 0;
}

/* :LED:HOLDover?, "Queries Holdover LED", the loop's (0x2f4b6). */
int32_t scpi_query_LED_HOLDover(void* parameters, void* response,
                                scpi_call* call) {
  int16_t* value = response;

  *value = return_byte(pll.led_holdover);
  return 0;
}
