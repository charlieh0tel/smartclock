/* cpu.c's functions that are this image's own: the rest are
 * common/cpu.c's, the same in every image (docs/firmware/rewrite.md,
 * "Shared C").  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype. */

/* Masks every interrupt in supervisor mode; returns the status register
 * as it was (0x13368), its condition codes its caller's: the body of
 * enter_supervisor_mode. */
static __attribute__((used, noipa)) uint16_t enter_supervisor_mode_with_ccr(
    uint32_t callers_ccr, const uint32_t* arguments) {
  return cpu_interrupts_mask_all(
      cpu_sr_at_call(cpu_enter_supervisor(), callers_ccr));
}

/* Sets the interrupt mask, in supervisor mode, to mask's, the
 * argument; returns the status register as it was (0x13378), its
 * condition codes its caller's: the body of set_interrupt_mask. */
static __attribute__((used, noipa)) uint16_t
set_interrupt_mask_with_ccr(uint32_t callers_ccr, const uint32_t* arguments) {
  const uint16_t mask = arguments[0];

  return cpu_interrupt_mask_set(
      cpu_sr_at_call(cpu_enter_supervisor(), callers_ccr), mask);
}
