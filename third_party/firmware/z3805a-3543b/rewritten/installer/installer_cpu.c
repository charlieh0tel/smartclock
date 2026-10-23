/* The installer's copies of C's wrapper of the status register
 * (`platform/cpu`; cpu.c has the primary's), which enter supervisor
 * mode through the installer's trap #7.  Written by hand in place of
 * the decompiler's (docs/firmware/README.md, "Hand-written C"); each
 * keeps its generated prototype. */

/* Masks every interrupt in supervisor mode; returns the status register
 * as it was (installer::0x100e80). */
uint16_t installer_enter_supervisor_mode(void) {
  const uint16_t sr = installer_enter_supervisor();

  cpu_sr_write(sr | CPU_IPL_7);
  return sr;
}

/* Sets the interrupt mask, in supervisor mode, to mask's; returns the
 * status register as it was (installer::0x100e90). */
uint16_t installer_set_interrupt_mask(uint16_t mask) {
  /* First, so that the status register it returns keeps the caller's
   * condition codes, as the image's does. */
  const uint16_t saved = cpu_installer_enter_supervisor();

  return cpu_interrupt_mask_set(saved, mask);
}
