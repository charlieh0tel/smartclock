/* From the 58503A 3704's rewritten/scpi_alarm.c and the Z3816A 4001's
 * rewritten/scpi_alarm.c, the 58503B 1.01.04's own beside common/'s: of its
 * functions, those the 58503B's code carries identical or relocated
 * (docs/firmware/58503b-1.01.04.md, "The link list").  Its comments'
 * addresses are the 58503B's, but where they name another image.  Each keeps
 * its generated prototype. */

/* :ALARm?: the alarm's text, alarm_text's into scpi_alarm_text, or
 * "No Alarm" with none (0x39f56). */
int32_t scpi_query_ALARm(void* parameters, void* response, scpi_call* call) {
  char** text = response;

  *text = "No Alarm";
  if (alarm_text(scpi_alarm_text) != 0) {
    *text = scpi_alarm_text;
  }
  return 0;
}

int32_t scpi_query_ALARm_ACO(void* parameters, void* response,
                             scpi_call* call) {
  return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
}

/* :ALARm:EXTRactor: an undefined header (0x39f36). */
int32_t scpi_set_ALARm_EXTRactor(void* parameters, void* response,
                                 scpi_call* call) {
  status_report_from(STATUS_UNDEFINED_HEADER, scpi_input_source(call->input));
  return 0;
}

/* :ALARm:ACO, the alarm cut-off, and its query: an undefined header
 * (0x40622, 0x40642). */
int32_t scpi_set_ALARm_ACO(void* parameters, void* response, scpi_call* call) {
  status_report_from(STATUS_UNDEFINED_HEADER, scpi_input_source(call->input));
  return 0;
}
