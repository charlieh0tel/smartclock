/* The 58503A's :ALARm subtree, undocumented in 097-59551-02: its text
 * and the ACO and EXTRactor nodes it refuses.  Written by hand in place
 * of the decompiler's (docs/firmware/README.md, "Hand-written C";
 * docs/firmware/modules.md, `applications/scpi/handlers/alarm`).  The
 * exported C includes this file after its own, so the types and
 * prototypes it declares are in scope; each function here keeps its
 * generated prototype. */

/* :ALARm?: the alarm's text, alarm_text's into scpi_alarm_text, or
 * "No Alarm" with none (0x3ddfc). */
int32_t scpi_query_ALARm(void* parameters, void* response, scpi_call* call) {
  char** text = response;

  *text = "No Alarm";
  if (alarm_text(scpi_alarm_text) != 0) {
    *text = scpi_alarm_text;
  }
  return 0;
}

/* :ALARm:ACO, the alarm cut-off, and its query: an undefined header
 * (0x44b0e, 0x44b2e). */
int32_t scpi_set_ALARm_ACO(void* parameters, void* response, scpi_call* call) {
  status_report_from(STATUS_UNDEFINED_HEADER, scpi_input_source(call->input));
  return 0;
}

int32_t scpi_query_ALARm_ACO(void* parameters, void* response,
                             scpi_call* call) {
  return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
}

/* :ANT1 and :AZEL, the set handler of both root nodes: an undefined
 * header, the 58503A's :ALARm:EXTRactor at its place (0x3dddc). */
int32_t scpi_set_ANT1(void* parameters, void* response, scpi_call* call) {
  status_report_from(STATUS_UNDEFINED_HEADER, scpi_input_source(call->input));
  return 0;
}

/* :ALARm:EXTRactor?: 1 while PORTE's bit 3 is low, else 0
 * (0x3fee0). */
int32_t scpi_query_ALARm_EXTRactor(void* parameters, void* response,
                                   scpi_call* call) {
  /* PORTE's bit the query reads (0x3fee8). */
  static const uint8_t EXTRACTOR_BIT = 1 << 3;
  int16_t* low = response;

  *low = (sim.porte0 & EXTRACTOR_BIT) == 0;
  return 0;
}

/* :ALARm:HARDware?: the hardware alarm word (0x3fefe). */
int32_t scpi_query_ALARm_HARDware(void* parameters, void* response,
                                  scpi_call* call) {
  uint32_t* word = response;

  *word = alarm_hardware_get();
  return 0;
}

/* :ALARm:OPERation?: the operation alarm word (0x3ff14). */
int32_t scpi_query_ALARm_OPERation(void* parameters, void* response,
                                   scpi_call* call) {
  uint32_t* word = response;

  *word = alarm_operation_get();
  return 0;
}
