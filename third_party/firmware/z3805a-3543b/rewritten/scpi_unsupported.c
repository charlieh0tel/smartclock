/* The root's four-letter SCPI entries the firmware does not support
 * (:CALA? to :RWHO?, docs/scpi/README.md), each failing through
 * scpi_query_failed; :DANA? and :RTCM?, which work, are scpi_misc.c's.
 * Written by hand in place of the decompiler's (docs/firmware/README.md,
 * "Hand-written C"; docs/firmware/modules.md,
 * `applications/scpi/handlers/stubs`). The exported C includes this file after
 * its own, so the types and prototypes it declares are in scope; each function
 * here keeps its generated prototype.
 *
 * Each stub hands scpi_query_failed the call (0x2a1be), which reports
 * the code with the call's input, at +0x12, and clears its query word,
 * scpi_call.query, at +4 (0x29ade to 0x29b00).  Some report the engine's
 * undefined header, STATUS_SCPI_UNDEFINED_HEADER, and the rest the
 * status table's, STATUS_UNDEFINED_HEADER; both are -113. */

/* :CALA? (0x2a1be). */
int32_t scpi_query_CALA(void* parameters, void* response, scpi_call* call) {
  return scpi_query_failed(STATUS_SCPI_UNDEFINED_HEADER, call);
}

/* :CEQU? (0x2a1d0). */
int32_t scpi_query_CEQU(void* parameters, void* response, scpi_call* call) {
  return scpi_query_failed(STATUS_SCPI_UNDEFINED_HEADER, call);
}

/* :FMHO? (0x2fc14). */
int32_t scpi_query_FMHO(void* parameters, void* response, scpi_call* call) {
  return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
}

/* :IPSU? (0x2d096). */
int32_t scpi_query_IPSU(void* parameters, void* response, scpi_call* call) {
  return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
}

/* :R1PO? (0x2c15e). */
int32_t scpi_query_R1PO(void* parameters, void* response, scpi_call* call) {
  return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
}

/* :RACD? (0x2c316). */
int32_t scpi_query_RACD(void* parameters, void* response, scpi_call* call) {
  return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
}

/* :RAST? (0x2a4c0). */
int32_t scpi_query_RAST(void* parameters, void* response, scpi_call* call) {
  return scpi_query_failed(STATUS_SCPI_UNDEFINED_HEADER, call);
}

/* :RAT1? (0x2fb64). */
int32_t scpi_query_RAT1(void* parameters, void* response, scpi_call* call) {
  return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
}

/* :RAT2? (0x2fbb0). */
int32_t scpi_query_RAT2(void* parameters, void* response, scpi_call* call) {
  return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
}

/* :RAT3? (0x2fb76). */
int32_t scpi_query_RAT3(void* parameters, void* response, scpi_call* call) {
  return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
}

/* :REAL? (0x2c1a2). */
int32_t scpi_query_REAL(void* parameters, void* response, scpi_call* call) {
  return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
}

/* :REQU? (0x3066e). */
int32_t scpi_query_REQU(void* parameters, void* response, scpi_call* call) {
  return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
}

/* :RFOU? (0x2c1ca). */
int32_t scpi_query_RFOU(void* parameters, void* response, scpi_call* call) {
  return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
}

/* :RLOC? (0x2ce92). */
int32_t scpi_query_RLOC(void* parameters, void* response, scpi_call* call) {
  return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
}

/* :RMHO? (0x2fd72). */
int32_t scpi_query_RMHO(void* parameters, void* response, scpi_call* call) {
  return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
}

/* :RPHS? (0x2cebe). */
int32_t scpi_query_RPHS(void* parameters, void* response, scpi_call* call) {
  return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
}

/* :RSPR? (0x2d01e). */
int32_t scpi_query_RSPR(void* parameters, void* response, scpi_call* call) {
  return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
}

/* :RSST? (0x30048). */
int32_t scpi_query_RSST(void* parameters, void* response, scpi_call* call) {
  return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
}

/* :RSSU? (0x2c1dc). */
int32_t scpi_query_RSSU(void* parameters, void* response, scpi_call* call) {
  return scpi_query_failed(STATUS_SCPI_UNDEFINED_HEADER, call);
}

/* :RTAD? (0x2e152). */
int32_t scpi_query_RTAD(void* parameters, void* response, scpi_call* call) {
  return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
}

/* :RTSA? (0x2d3ee). */
int32_t scpi_query_RTSA(void* parameters, void* response, scpi_call* call) {
  return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
}

/* :RTZO? (0x2e2ec). */
int32_t scpi_query_RTZO(void* parameters, void* response, scpi_call* call) {
  return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
}

/* :RVST? (0x307ee). */
int32_t scpi_query_RVST(void* parameters, void* response, scpi_call* call) {
  return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
}

/* :RWHO? (0x2a344). */
int32_t scpi_query_RWHO(void* parameters, void* response, scpi_call* call) {
  return scpi_query_failed(STATUS_SCPI_UNDEFINED_HEADER, call);
}
