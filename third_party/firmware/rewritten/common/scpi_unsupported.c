/* The root's four-letter SCPI entries the firmware does not support
 * (:CALA? to :RWHO?, docs/scpi/README.md), each failing through
 * scpi_query_failed; :DANA? and :RTCM?, which work, are scpi_misc.c's.
 * Written by hand in place of the decompiler's (docs/firmware/README.md,
 * "Hand-written C"; docs/firmware/modules.md,
 * `applications/scpi/handlers/stubs`). The exported C includes this file after
 * its own, so the types and prototypes it declares are in scope; each function
 * here keeps its generated prototype.
 *
 * Each stub hands scpi_query_failed the call, which reports
 * the code with the call's input, at +0x12, and clears its query word,
 * scpi_call.query, at +4.  Some report the engine's
 * undefined header, STATUS_SCPI_UNDEFINED_HEADER, and the rest the
 * status table's, STATUS_UNDEFINED_HEADER; both are -113. */

/* :CALA?. */
int32_t scpi_query_CALA(void* parameters, void* response, scpi_call* call) {
  return scpi_query_failed(STATUS_SCPI_UNDEFINED_HEADER, call);
}

/* :CEQU?. */
int32_t scpi_query_CEQU(void* parameters, void* response, scpi_call* call) {
  return scpi_query_failed(STATUS_SCPI_UNDEFINED_HEADER, call);
}

/* :FMHO?. */
int32_t scpi_query_FMHO(void* parameters, void* response, scpi_call* call) {
  return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
}

/* :IPSU?. */
int32_t scpi_query_IPSU(void* parameters, void* response, scpi_call* call) {
  return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
}

/* :R1PO?. */
int32_t scpi_query_R1PO(void* parameters, void* response, scpi_call* call) {
  return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
}

/* :RACD?. */
int32_t scpi_query_RACD(void* parameters, void* response, scpi_call* call) {
  return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
}

/* :RAST?. */
int32_t scpi_query_RAST(void* parameters, void* response, scpi_call* call) {
  return scpi_query_failed(STATUS_SCPI_UNDEFINED_HEADER, call);
}

/* :RAT1?. */
int32_t scpi_query_RAT1(void* parameters, void* response, scpi_call* call) {
  return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
}

/* :RAT2?. */
int32_t scpi_query_RAT2(void* parameters, void* response, scpi_call* call) {
  return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
}

/* :RAT3?. */
int32_t scpi_query_RAT3(void* parameters, void* response, scpi_call* call) {
  return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
}

/* :REAL?. */
int32_t scpi_query_REAL(void* parameters, void* response, scpi_call* call) {
  return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
}

/* :REQU?. */
int32_t scpi_query_REQU(void* parameters, void* response, scpi_call* call) {
  return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
}

/* :RFOU?. */
int32_t scpi_query_RFOU(void* parameters, void* response, scpi_call* call) {
  return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
}

/* :RLOC?. */
int32_t scpi_query_RLOC(void* parameters, void* response, scpi_call* call) {
  return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
}

/* :RMHO?. */
int32_t scpi_query_RMHO(void* parameters, void* response, scpi_call* call) {
  return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
}

/* :RPHS?. */
int32_t scpi_query_RPHS(void* parameters, void* response, scpi_call* call) {
  return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
}

/* :RSPR?. */
int32_t scpi_query_RSPR(void* parameters, void* response, scpi_call* call) {
  return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
}

/* :RSST?. */
int32_t scpi_query_RSST(void* parameters, void* response, scpi_call* call) {
  return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
}

/* :RSSU?. */
int32_t scpi_query_RSSU(void* parameters, void* response, scpi_call* call) {
  return scpi_query_failed(STATUS_SCPI_UNDEFINED_HEADER, call);
}

/* :RTAD?. */
int32_t scpi_query_RTAD(void* parameters, void* response, scpi_call* call) {
  return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
}

/* :RTSA?. */
int32_t scpi_query_RTSA(void* parameters, void* response, scpi_call* call) {
  return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
}

/* :RTZO?. */
int32_t scpi_query_RTZO(void* parameters, void* response, scpi_call* call) {
  return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
}

/* :RVST?. */
int32_t scpi_query_RVST(void* parameters, void* response, scpi_call* call) {
  return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
}

/* :RWHO?. */
int32_t scpi_query_RWHO(void* parameters, void* response, scpi_call* call) {
  return scpi_query_failed(STATUS_SCPI_UNDEFINED_HEADER, call);
}
