/* From the 58503A 3704's rewritten/scpi_output.c and the Z3815A 4010's
 * rewritten/scpi_output.c, the Z3817A 3752's own beside common/'s: of its
 * functions, those the Z3817A's code carries identical or relocated, and
 * hmon_check_antenna_current, this image's own
 * (docs/firmware/z3817a-3752.md, "The function check").  Its comments'
 * addresses are the Z3817A's, but where they name another image.  Each keeps
 * its generated prototype. */

/* :OUTPut:STATe's done hook (0x47d06). */
void output_state_done(void) { output_state_program(nv_settings.output_state); }

/* :OUTPut:STATe?, as a signed byte (0x3e7c2). */
int32_t scpi_query_OUTPut_STATe(void* parameters, void* response,
                                scpi_call* call) {
  int16_t* value = response;

  *value = (int8_t)nv_settings.output_state;
  return 0;
}

/* :OUTPut:STATe: hands pllc the new state. */
int32_t scpi_set_OUTPut_STATe(void* parameters, void* response,
                              scpi_call* call) {
  scpi_word_parameter* given = parameters;

  pllc_request_a(&scpi_param_OUTPut_STATe, 0, given->value,
                 scpi_input_source(call->input));
  return 0;
}

/* No antenna fault, whatever the settings: the image has no antenna
 * current channel, where the Z3815A's tests the current (0x32482). */
uint8_t hmon_check_antenna_current(void) { return 0; }
