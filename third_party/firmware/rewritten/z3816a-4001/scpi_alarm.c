/* From the 58503A 3704's rewritten/scpi_alarm.c, the Z3816A 4001's own beside
 * common/'s: of its functions, those the Z3816A's code carries (identical or
 * relocated by image_diff, or changed with the same instructions), with the
 * static helpers they use, each the same here but where its comment says
 * otherwise (docs/firmware/z3816a-4001.md, "The link check").  Its comments'
 * addresses are the Z3816A's.  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype. */

/* :ALARm:EXTRactor: an undefined header (0x3a3b4). */
int32_t scpi_set_ALARm_EXTRactor(void* parameters, void* response,
                                 scpi_call* call) {
  status_report_from(STATUS_UNDEFINED_HEADER, scpi_input_source(call->input));
  return 0;
}

/* :ALARm:ACO, the alarm cut-off, and its query: an undefined header
 * (0x4000c, 0x4002c). */
int32_t scpi_set_ALARm_ACO(void* parameters, void* response, scpi_call* call) {
  status_report_from(STATUS_UNDEFINED_HEADER, scpi_input_source(call->input));
  return 0;
}

int32_t scpi_query_ALARm_ACO(void* parameters, void* response,
                             scpi_call* call) {
  return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
}

/* :ALARm:HARDware?: the hardware alarm's bits, alarm_hardware, which
 * status_summary_update keeps (0x3b6ee).  The Z3815A 4010's but for the
 * byte read in place of its word's getter. */
int32_t scpi_query_ALARm_HARDware(void* parameters, void* response,
                                  scpi_call* call) {
  uint32_t* word = response;

  *word = alarm_hardware;
  return 0;
}
