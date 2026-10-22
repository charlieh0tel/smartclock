/* The root's four-letter SCPI entries the firmware does not support
 * (:CALA? to :RWHO?, docs/scpi/README.md), each failing through
 * scpi_query_failed, and :RTCM?'s and :DANA?'s answers, written by hand
 * in place of the decompiler's (docs/firmware/README.md, "Hand-written
 * C"; docs/firmware/modules.md, `applications/scpi/handlers/stubs`).
 * The exported C includes this file after its own, so the types and
 * prototypes it declares are in scope; each function here keeps its
 * generated prototype.
 *
 * Each stub hands scpi_query_failed the call where it takes the
 * response, as the image does (0x2a1be): emptying that "text" zeroes the
 * call's query word, scpi_call.query, at +4.  Some report the engine's
 * undefined header, STATUS_SCPI_UNDEFINED_HEADER, and the rest the
 * status table's, STATUS_UNDEFINED_HEADER; both are -113. */

/* :CALA? (0x2a1be). */
int32_t scpi_query_CALA(void* parameters, void* response, void* call) {
  return scpi_query_failed(STATUS_SCPI_UNDEFINED_HEADER, call);
}

/* :CEQU? (0x2a1d0). */
int32_t scpi_query_CEQU(void* parameters, void* response, void* call) {
  return scpi_query_failed(STATUS_SCPI_UNDEFINED_HEADER, call);
}

/* :FMHO? (0x2fc14). */
int32_t scpi_query_FMHO(void* parameters, void* response, void* call) {
  return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
}

/* :IPSU? (0x2d096). */
int32_t scpi_query_IPSU(void* parameters, void* response, void* call) {
  return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
}

/* :R1PO? (0x2c15e). */
int32_t scpi_query_R1PO(void* parameters, void* response, void* call) {
  return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
}

/* :RACD? (0x2c316). */
int32_t scpi_query_RACD(void* parameters, void* response, void* call) {
  return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
}

/* :RAST? (0x2a4c0). */
int32_t scpi_query_RAST(void* parameters, void* response, void* call) {
  return scpi_query_failed(STATUS_SCPI_UNDEFINED_HEADER, call);
}

/* :RAT1? (0x2fb64). */
int32_t scpi_query_RAT1(void* parameters, void* response, void* call) {
  return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
}

/* :RAT2? (0x2fbb0). */
int32_t scpi_query_RAT2(void* parameters, void* response, void* call) {
  return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
}

/* :RAT3? (0x2fb76). */
int32_t scpi_query_RAT3(void* parameters, void* response, void* call) {
  return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
}

/* :REAL? (0x2c1a2). */
int32_t scpi_query_REAL(void* parameters, void* response, void* call) {
  return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
}

/* :REQU? (0x3066e). */
int32_t scpi_query_REQU(void* parameters, void* response, void* call) {
  return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
}

/* :RFOU? (0x2c1ca). */
int32_t scpi_query_RFOU(void* parameters, void* response, void* call) {
  return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
}

/* :RLOC? (0x2ce92). */
int32_t scpi_query_RLOC(void* parameters, void* response, void* call) {
  return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
}

/* :RMHO? (0x2fd72). */
int32_t scpi_query_RMHO(void* parameters, void* response, void* call) {
  return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
}

/* :RPHS? (0x2cebe). */
int32_t scpi_query_RPHS(void* parameters, void* response, void* call) {
  return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
}

/* :RSPR? (0x2d01e). */
int32_t scpi_query_RSPR(void* parameters, void* response, void* call) {
  return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
}

/* :RSST? (0x30048). */
int32_t scpi_query_RSST(void* parameters, void* response, void* call) {
  return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
}

/* :RSSU? (0x2c1dc). */
int32_t scpi_query_RSSU(void* parameters, void* response, void* call) {
  return scpi_query_failed(STATUS_SCPI_UNDEFINED_HEADER, call);
}

/* :RTAD? (0x2e152). */
int32_t scpi_query_RTAD(void* parameters, void* response, void* call) {
  return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
}

/* :RTSA? (0x2d3ee). */
int32_t scpi_query_RTSA(void* parameters, void* response, void* call) {
  return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
}

/* :RTZO? (0x2e2ec). */
int32_t scpi_query_RTZO(void* parameters, void* response, void* call) {
  return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
}

/* :RVST? (0x307ee). */
int32_t scpi_query_RVST(void* parameters, void* response, void* call) {
  return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
}

/* :RWHO? (0x2a344). */
int32_t scpi_query_RWHO(void* parameters, void* response, void* call) {
  return scpi_query_failed(STATUS_SCPI_UNDEFINED_HEADER, call);
}

/* :DANA?'s response: the next error of the queue, as :SYSTem:ERRor?
 * answers it. */
typedef struct scpi_unsupported_error {
  int16_t code; /* +0x00 */
  uint16_t unused;
  char* message; /* +0x04 */
} scpi_unsupported_error;

/* :DANA?: takes the next error from the call's queue, its message the
 * queue's, else the code's own, else empty (0x3060e). */
int32_t scpi_query_DANA(void* parameters, void* response, void* call) {
  scpi_call* record = call;
  scpi_unsupported_error* error = response;
  int32_t code;
  char* message = 0;

  error_queue_read(record->status, &code, (uint32_t*)&message);
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
int32_t scpi_query_RTCM(void* parameters, void* response, void* call) {
  /* The bit of rtcm_supported's result that says RTCM is. */
  static const uint8_t RTCM_SUPPORTED = 0x02;
  /* The task event the SCPI task waits on for a request's end, as
   * request_and_wait does (structures.md, "The pllc task's SCPI
   * requests"). */
  static const uint32_t RTCM_DONE_EVENT = 0x20;
  scpi_call* record = call;
  int16_t* value = response;
  uint32_t events;

  if (scpi_console_port() != record->input) {
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
