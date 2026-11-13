/* From the Z3815A 4010's rewritten/scpi_alarm.c, the Z3817A 3752's own beside
 * common/'s: of its functions, those the Z3817A's code carries identical or
 * relocated (docs/firmware/z3817a-3752.md, "The function check").  Its
 * comments' addresses are the Z3817A's, but where they name another image. Each
 * keeps its generated prototype. */

/* :ALARm?: the alarm's text, alarm_text's into scpi_alarm_text, or
 * "No Alarm" with none (0x3aeac). */
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

/* :ALARm:ACO, the alarm cut-off, and its query: an undefined header
 * (0x412ca, 0x412ea). */
int32_t scpi_set_ALARm_ACO(void* parameters, void* response, scpi_call* call) {
  status_report_from(STATUS_UNDEFINED_HEADER, scpi_input_source(call->input));
  return 0;
}

/* :ALARm:EXTRactor, the set handler of every node the tree keeps but
 * does not serve, :ANT1, :AZEL and :LED:STANdby among them: an undefined
 * header, the 58503A's :ALARm:EXTRactor at its place (0x3ae8c). */
int32_t scpi_set_ALARm_EXTRactor(void* parameters, void* response,
                                 scpi_call* call) {
  status_report_from(STATUS_UNDEFINED_HEADER, scpi_input_source(call->input));
  return 0;
}
