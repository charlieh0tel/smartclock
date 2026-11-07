/* timers.c's functions that are this image's own: the rest are
 * common/timers.c's, the same in every image (docs/firmware/rewrite.md,
 * "Shared C").  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype. */

/* Input capture 2's interrupt, which no vector names: clears its flag
 * and returns through the kernel, trap #5, which does not come back
 * (0x45cf8). */
void gpt_input_capture_2_interrupt(void) {
  gpt.tflg &= ~TFLG_ICF2;
  for (;;) {
    __m68k_trap(5);
  }
}

/* Forces the PWMA pin low for a nonzero argument, high for zero,
 * through PWMC's F1A (MC68331UM D.2.14); holdover and the loop's stages
 * call it. */
void pwma_force_level(char low) {
  if (low == 0) {
    gpt.pwmc |= PWMC_F1A;
  } else {
    gpt.pwmc &= ~PWMC_F1A;
  }
}
