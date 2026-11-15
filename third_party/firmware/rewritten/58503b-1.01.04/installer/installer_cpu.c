/* The Z3817A 3752's installer/installer_cpu.c, copied for the 58503B 1.01.04,
 * each of its functions identical or relocated in image_diff's matches
 * (docs/firmware/58503b-1.01.04.md, "The export and the checks"); its
 * comments' addresses are the 58503B's.  The Z3817A's own: from
 * common/installer/installer_cpu.c and the Z3815A 4010's
 * rewritten/installer/installer_cpu.c, the Z3817A 3752's own beside
 * common/'s: the installer's copies of C's wrapper of the status register,
 * which the decompiler cannot write, the same in both images, whose
 * installers are one byte for byte (docs/firmware/z3817a-3752.md, "The
 * function check").  Each keeps its generated prototype. */

/* Masks every interrupt in supervisor mode; returns the status register
 * as it was (installer::0x100e7e), its condition codes its caller's: the body
 * of installer_enter_supervisor_mode. */
static __attribute__((used, noipa)) uint16_t
installer_enter_supervisor_mode_with_ccr(uint32_t callers_ccr,
                                         const uint32_t* arguments) {
  return cpu_interrupts_mask_all(
      cpu_sr_at_call(cpu_installer_enter_supervisor(), callers_ccr));
}

/* Sets the interrupt mask, in supervisor mode, to mask's, the
 * argument; returns the status register as it was (installer::0x100e8e), its
 * condition codes its caller's: the body of installer_set_interrupt_mask. */
static __attribute__((used, noipa)) uint16_t
installer_set_interrupt_mask_with_ccr(uint32_t callers_ccr,
                                      const uint32_t* arguments) {
  const uint16_t mask = arguments[0];

  return cpu_interrupt_mask_set(
      cpu_sr_at_call(cpu_installer_enter_supervisor(), callers_ccr), mask);
}
