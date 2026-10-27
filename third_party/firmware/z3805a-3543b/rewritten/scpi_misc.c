/* scpi_misc.c's functions that are this image's own: the rest are
 * common/scpi_misc.c's, the same in every image (docs/firmware/rewrite.md,
 * "Shared C").  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype. */

/* :RTCM?, from the console port only: fails with no condition, since
 * rtcm_supported's bit 1 is never set; were it set, the timecode's T2
 * test is requested and waited for, and the response is 0 (0x2df46). */
int32_t scpi_query_RTCM(void* parameters, void* response, scpi_call* call) {
  /* The bit of rtcm_supported's result that says RTCM is. */
  static const uint8_t RTCM_SUPPORTED = 0x02;
  /* The task event the SCPI task waits on for a request's end, as
   * request_and_wait does (structures.md, "The pllc task's SCPI
   * requests"). */
  static const uint32_t RTCM_DONE_EVENT = 0x20;
  int16_t* value = response;
  uint32_t events;

  if (scpi_console_port() != call->input) {
    return 0;
  }
  if ((rtcm_supported() & RTCM_SUPPORTED) == 0) {
    return scpi_query_failed(STATUS_NO_CONDITION, call);
  }
  /* get_v's 1 and wait_v's 0s are as the image passes them. */
  psos_get_v(RTCM_DONE_EVENT, 1, &events);
  if (tcode_request(TIMECODE_T2_TEST) == 0) {
    return scpi_query_failed(STATUS_DATA_CORRUPT_OR_STALE, call);
  }
  psos_wait_v(RTCM_DONE_EVENT, 0, 0, &events);
  *value = 0;
  return 0;
}
