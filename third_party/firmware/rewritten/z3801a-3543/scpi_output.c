/* scpi_output.c's functions that are this image's own: the rest are
 * common/scpi_output.c's, the same in every image (docs/firmware/rewrite.md,
 * "Shared C").  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype. */

/* :OUTPut:STATe?, as a signed byte (0x2c96c). */
int32_t scpi_query_OUTPut_STATe(void* parameters, void* response,
                                scpi_call* call) {
  int16_t* value = response;

  *value = (int8_t)nv_settings.output_state;
  return 0;
}

/* :OUTPut:STATe's done hook (0x42ba8). */
void output_state_done(void) { output_state_program(nv_settings.output_state); }
