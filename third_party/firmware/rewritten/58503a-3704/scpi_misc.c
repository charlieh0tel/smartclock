/* scpi_misc.c's functions that are this image's own: the rest are
 * common/scpi_misc.c's, the same in every image (docs/firmware/rewrite.md,
 * "Shared C").  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype. */

/* :RTCM?, for a source that may ask for a time code: fails with no
 * condition, since rtcm_supported's bit 1 is never set; were it set, the
 * time code's T2 test is sent and waited for, and the response is an
 * empty text.  For another source, or when tcode_request refuses, -221,
 * Settings conflict (0x2df10).  The image pushes rtcm_supported four
 * arguments, which it does not read. */
int32_t scpi_query_RTCM(void* parameters, void* response, scpi_call* call) {
  /* The bit of rtcm_supported's result that says RTCM is. */
  static const uint8_t RTCM_SUPPORTED = 0x02;

  if (tcode_query_allowed(return_zero(call->input)) == 0) {
    return scpi_query_failed(STATUS_SETTINGS_CONFLICT, call);
  }
  if ((rtcm_supported() & RTCM_SUPPORTED) == 0) {
    return scpi_query_failed(STATUS_NO_CONDITION, call);
  }
  if (!scpi_tcode_send(TIMECODE_T2_TEST, response, call)) {
    return scpi_query_failed(STATUS_SETTINGS_CONFLICT, call);
  }
  return 0;
}
