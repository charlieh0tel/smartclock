/* The Z3805A 3543B's installer/installer_cpu.c, the Z3805A 1.01.03's own
 * beside common/'s, whose installer is the 3543B's byte for byte
 * (docs/firmware/z3805a-1.01.03.md, "The export and the checks"): of its
 * functions, those common/ does not hold, the 3543B's own
 * rewritten/z3805a-3543b/installer/installer_cpu.c's. Its comments'
 * installer addresses are the 3543B's, which are this image's; a primary
 * address is moved to this image's. Each keeps its generated prototype. */

/* installer/installer_cpu.c's functions that are this image's own: the rest are
 * common/installer/installer_cpu.c's, the same in every image
 * (docs/firmware/rewrite.md, "Shared C").  Written by hand in place of the
 * decompiler's (docs/firmware/README.md, "Hand-written C"); each keeps its
 * generated prototype. */

/* Masks every interrupt in supervisor mode; returns the status register
 * as it was (installer::0x100e80), its condition codes its caller's: the body
 * of installer_enter_supervisor_mode. */
static __attribute__((used, noipa)) uint16_t
installer_enter_supervisor_mode_with_ccr(uint32_t callers_ccr,
                                         const uint32_t* arguments) {
  return cpu_interrupts_mask_all(
      cpu_sr_at_call(cpu_installer_enter_supervisor(), callers_ccr));
}

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
