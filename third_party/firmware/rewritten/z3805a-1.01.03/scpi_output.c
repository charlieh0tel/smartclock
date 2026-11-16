/* From the 58503B 1.01.04's rewritten/scpi_output.c, the Z3805A 1.01.03's
 * own beside common/'s: of its functions, those the Z3805A 1.01.03's code
 * carries identical or relocated (docs/firmware/z3805a-1.01.03.md, "The
 * link list").  Its comments' addresses are the Z3805A 1.01.03's, but where
 * they name another image.  Each keeps its generated prototype. */

/* :OUTPut:STATe?, as a signed byte (0x2f500). */
int32_t scpi_query_OUTPut_STATe(void* parameters, void* response,
                                scpi_call* call) {
  int16_t* value = response;

  *value = (int8_t)nv_settings.output_state;
  return 0;
}

/* :OUTPut:STATe's done hook (0x4336c). */
void output_state_done(void) { output_state_program(nv_settings.output_state); }

/* Whether the OCXO is warm: always; powerup and efc_calibration_measure
 * test it (0x235e8). */
uint8_t ocxo_warm(void) { return 1; }
