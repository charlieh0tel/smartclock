/* scpi_output.c's functions that are this image's own: the rest are
 * common/scpi_output.c's, the same in every image (docs/firmware/rewrite.md,
 * "Shared C").  From the 58503A 3704's rewritten/scpi_output.c: of its
 * functions, those the Z3816A's code carries (identical or relocated by
 * image_diff, or changed with the same instructions), with the static helpers
 * they use, each the same here but where its comment says otherwise
 * (docs/firmware/z3816a-4001.md, "The link check").  Its comments' addresses
 * are the Z3816A's.  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype. */

/* :OUTPut:STATe?, as a signed byte (0x3d4d6). */
int32_t scpi_query_OUTPut_STATe(void* parameters, void* response,
                                scpi_call* call) {
  int16_t* value = response;

  *value = (int8_t)nv_settings.output_state;
  return 0;
}

/* :OUTPut:STATe's done hook (0x464cc). */
void output_state_done(void) { output_state_program(nv_settings.output_state); }
