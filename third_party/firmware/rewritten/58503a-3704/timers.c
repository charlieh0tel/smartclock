/* pwma_force_level: this image's own, split from
 * common/timers.c where it differs between the images (rewrite.md, "Shared
 * C", rule 6). */

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
