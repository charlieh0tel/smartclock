/* What the SCPI :SOURce:ROSCillator tree's files share
 * (scpi_source_roscillator.c and each image's own
 * scpi_source_roscillator.c): pll_mode's result, read with interrupts
 * masked.  The exported C includes the
 * hand-written headers after its own declarations and before the
 * hand-written .c files, so every one of them can use it. */

/* pll_mode's result, read with interrupts masked. */
static pll_screen_mode scpi_source_roscillator_mode(void) {
  uint16_t sr = set_interrupt_mask(CPU_IPL_1);
  pll_screen_mode mode = pll_mode();

  set_interrupt_mask(sr);
  return mode;
}
