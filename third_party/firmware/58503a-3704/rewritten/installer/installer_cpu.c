/* The installer's copies of C's wrapper of the status register
 * (`platform/cpu`; cpu.c has the primary's), which enter supervisor
 * mode through the installer's trap #7.  Written by hand in place of
 * the decompiler's (docs/firmware/README.md, "Hand-written C"); each
 * keeps its generated prototype. */

/* Masks every interrupt in supervisor mode; returns the status register
 * as it was (installer::0x100e80), its condition codes its caller's: the body
 * of installer_enter_supervisor_mode. */
static __attribute__((used, noipa)) uint16_t
installer_enter_supervisor_mode_with_ccr(uint32_t callers_ccr,
                                         const uint32_t* arguments) {
  return cpu_interrupts_mask_all(
      cpu_sr_at_call(cpu_installer_enter_supervisor(), callers_ccr));
}

CPU_GIVING_CALLERS_CCR(installer_enter_supervisor_mode,
                       installer_enter_supervisor_mode_with_ccr);

/* Sets the interrupt mask, in supervisor mode, to mask's, the
 * argument; returns the status register as it was (installer::0x100e90), its
 * condition codes its caller's: the body of installer_set_interrupt_mask. */
static __attribute__((used, noipa)) uint16_t
installer_set_interrupt_mask_with_ccr(uint32_t callers_ccr,
                                      const uint32_t* arguments) {
  const uint16_t mask = arguments[0];

  return cpu_interrupt_mask_set(
      cpu_sr_at_call(cpu_installer_enter_supervisor(), callers_ccr), mask);
}

CPU_GIVING_CALLERS_CCR(installer_set_interrupt_mask,
                       installer_set_interrupt_mask_with_ccr);
