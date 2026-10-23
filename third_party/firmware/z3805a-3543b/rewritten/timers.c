/* The MC68331's general-purpose timer as the loop and the kernel's
 * vectors use it directly, written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"; docs/firmware/modules.md,
 * `devices/timers`).  The exported C includes this file after its own,
 * so the types and prototypes it declares are in scope; each function
 * here keeps its generated prototype.  The registers are mc68331.h's. */

/* Forces the PWMA pin low for a nonzero argument, high for zero,
 * through PWMC's F1A (MC68331UM D.2.14); holdover and the loop's stages
 * call it (0x45ce4). */
void pwma_force_level(char low) {
  if (low == 0) {
    gpt.pwmc |= PWMC_F1A;
  } else {
    gpt.pwmc &= ~PWMC_F1A;
  }
}

/* Input capture 2's interrupt, which no vector names: clears its flag
 * and returns through the kernel, trap #5, which does not come back
 * (0x45cc0). */
void gpt_input_capture_2_interrupt(void) {
  gpt.tflg &= ~TFLG_ICF2;
  for (;;) {
    __m68k_trap(5);
  }
}
