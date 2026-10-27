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
 * queue's, else the code's own, else empty. */
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

/* 0: RTCM corrections are not supported. */
uint8_t rtcm_supported(void) { return 0; }
