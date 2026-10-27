/* The SCPI :DOUGlas tree (docs/scpi/README.md): its handlers, and the
 * hooks and actions pllc runs for its settings, written by hand in place
 * of the decompiler's (docs/firmware/README.md, "Hand-written C";
 * docs/firmware/modules.md, `applications/scpi/handlers/douglas`).  The
 * exported C includes this file after its own, so the types and
 * prototypes it declares are in scope; each function here keeps its
 * generated prototype.  :DOUGlas is the position survey's
 * (docs/scpi/undocumented.md, ":DOUGlas"), the Z3801A's
 * :GPS:POSition:SURVey (097-z3801-01, table 4-2, page 4-7). */

/* :DOUGlas:PROGress?: the survey's progress as text; -221, Settings
 * conflict, when none is running. */
int32_t scpi_query_DOUGlas_PROGress(void* parameters, void* response,
                                    scpi_call* call) {
  char** text = response;
  char* progress = scpi_response_texts[(uint8_t)return_zero(call->input)];
  uint16_t sr = set_interrupt_mask(CPU_IPL_1);
  /* The image tests the result's low byte (scpi_query_DOUGlas_PROGress+0x44).
   */
  uint8_t running = survey_progress_text(progress);

  set_interrupt_mask(sr);
  if (running == 0) {
    return scpi_query_failed(STATUS_SETTINGS_CONFLICT, call);
  }
  *text = progress;
  return 0;
}

/* :DOUGlas:STATe:POWerup: whether a survey starts at power-up, to pllc. */
int32_t scpi_set_DOUGlas_STATe_POWerup(void* parameters, void* response,
                                       scpi_call* call) {
  scpi_word_parameter* state = parameters;

  pllc_request_a(&scpi_param_DOUGlas_STATe_POWerup, 0, (uint8_t)state->value,
                 return_zero(call->input));
  return 0;
}
