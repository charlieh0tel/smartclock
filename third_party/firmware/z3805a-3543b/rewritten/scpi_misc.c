/* The root's four-letter SCPI entries that work (docs/scpi/README.md):
 * :DANA?, which reads the error queue, and :RTCM?, which answers that
 * RTCM corrections are not supported, written by hand in place of the
 * decompiler's (docs/firmware/README.md, "Hand-written C";
 * docs/firmware/modules.md, `applications/scpi/handlers/misc`).  The
 * exported C includes this file after its own, so the types and
 * prototypes it declares are in scope; each function here keeps its
 * generated prototype. */

/* :DANA?'s response: the next error of the queue, as :SYSTem:ERRor?
 * answers it. */
typedef struct scpi_unsupported_error {
  int16_t code; /* +0x00 */
  uint16_t unused;
  char* message; /* +0x04 */
} scpi_unsupported_error;

/* :DANA?: takes the next error from the call's queue, its message the
 * queue's, else the code's own, else empty (0x3060e). */
int32_t scpi_query_DANA(void* parameters, void* response, scpi_call* call) {
  scpi_unsupported_error* error = response;
  int32_t code;
  char* message = 0;

  error_queue_read(call->status, &code, (uint32_t*)&message);
  error->code = code;
  if (message == 0) {
    message = error_message_lookup(code);
  }
  if (message == 0) {
    message = "";
  }
  error->message = message;
  return 0;
}

/* 0: RTCM corrections are not supported (0x29b08). */
uint8_t rtcm_supported(void) { return 0; }

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
