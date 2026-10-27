/* The 58503A's :ALARm subtree, undocumented in 097-59551-02: its text
 * and the ACO and EXTRactor nodes it refuses.  Written by hand in place
 * of the decompiler's (docs/firmware/README.md, "Hand-written C";
 * docs/firmware/modules.md, `applications/scpi/handlers/alarm`).  The
 * exported C includes this file after its own, so the types and
 * prototypes it declares are in scope; each function here keeps its
 * generated prototype. */

/* :ALARm?: the alarm's text, alarm_text's into scpi_alarm_text, or
 * "No Alarm" with none (0x2a73a). */
int32_t scpi_query_ALARm(void* parameters, void* response, scpi_call* call) {
  char** text = response;

  *text = "No Alarm";
  if (alarm_text(scpi_alarm_text) != 0) {
    *text = scpi_alarm_text;
  }
  return 0;
}

/* :ALARm:EXTRactor: an undefined header (0x2a71a). */
int32_t scpi_set_ALARm_EXTRactor(void* parameters, void* response,
                                 scpi_call* call) {
  status_report_from(STATUS_UNDEFINED_HEADER, return_zero(call->input));
  return 0;
}

/* :ALARm:ACO, the alarm cut-off, and its query: an undefined header
 * (0x306d2, 0x306f2). */
int32_t scpi_set_ALARm_ACO(void* parameters, void* response, scpi_call* call) {
  status_report_from(STATUS_UNDEFINED_HEADER, return_zero(call->input));
  return 0;
}

int32_t scpi_query_ALARm_ACO(void* parameters, void* response,
                             scpi_call* call) {
  return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
}
