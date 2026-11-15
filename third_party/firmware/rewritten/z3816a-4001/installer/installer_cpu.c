/* installer_cpu.c's functions that are this image's own: the rest are
 * common/installer/installer_cpu.c's, the same in every image
 * (docs/firmware/rewrite.md, "Shared C").  The bodies of the installer's
 * copies of C's wrapper of the status register, which the decompiler cannot
 * write, the same in the Z3815A 4010, the USA installer the Z3815A's copies
 * (docs/firmware/z3816a-4001.md, "The function check").  Each keeps its
 * generated prototype. */

/* Masks every interrupt in supervisor mode; returns the status register
 * as it was (installer::0x100e7a), its condition codes its caller's: the body
 * of installer_enter_supervisor_mode. */
static __attribute__((used, noipa)) uint16_t
installer_enter_supervisor_mode_with_ccr(uint32_t callers_ccr,
                                         const uint32_t* arguments) {
  return cpu_interrupts_mask_all(
      cpu_sr_at_call(cpu_installer_enter_supervisor(), callers_ccr));
}

/* Sets the interrupt mask, in supervisor mode, to mask's, the
 * argument; returns the status register as it was (installer::0x100e8a), its
 * condition codes its caller's: the body of installer_set_interrupt_mask. */
static __attribute__((used, noipa)) uint16_t
installer_set_interrupt_mask_with_ccr(uint32_t callers_ccr,
                                      const uint32_t* arguments) {
  const uint16_t mask = arguments[0];

  return cpu_interrupt_mask_set(
      cpu_sr_at_call(cpu_installer_enter_supervisor(), callers_ccr), mask);
}
