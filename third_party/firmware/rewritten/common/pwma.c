/* The GPT's PWMA pin forced to a level, the same in each image that
 * includes this file (docs/firmware/rewrite.md, "Shared C", "A file for
 * some images").  An image without the routine leaves the file out.
 * Written by hand in place of the decompiler's (docs/firmware/README.md,
 * "Hand-written C"); each keeps its generated prototype. */

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
