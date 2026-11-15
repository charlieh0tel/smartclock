/* The outer oven's console word, the same in each image that includes this
 * file (docs/firmware/rewrite.md, "Shared C", "A file for some images").
 * An image without the word leaves the file out.  Written by hand in place
 * of the decompiler's (docs/firmware/README.md, "Hand-written C"); each
 * keeps its generated prototype. */

/* The word doven: the outer oven on or off. */
void diag_doven(int32_t on) {
  if (on == 0) {
    gpt.portgp &= ~PLL_STAGES_OUTER_OVEN_ENABLE;
  } else {
    gpt.portgp |= PLL_STAGES_OUTER_OVEN_ENABLE;
  }
}
