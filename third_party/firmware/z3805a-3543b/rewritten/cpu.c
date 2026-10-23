/* C's wrapper of the status register, written by hand in place of the
 * decompiler's (docs/firmware/README.md, "Hand-written C";
 * docs/firmware/modules.md, `platform/cpu`): it enters supervisor
 * mode with enter_supervisor, trap #7, which returns the status register
 * as it was, and moves a new one in.  The exported C includes this file
 * after its own, so the types and prototypes it declares are in scope;
 * each function here keeps its generated prototype. */

/* Masks every interrupt in supervisor mode; returns the status register
 * as it was (0x13368), its condition codes its caller's: the body of
 * enter_supervisor_mode. */
static __attribute__((used, noipa)) uint16_t enter_supervisor_mode_with_ccr(
    uint32_t callers_ccr, const uint32_t* arguments) {
  return cpu_interrupts_mask_all(
      cpu_sr_at_call(cpu_enter_supervisor(), callers_ccr));
}

CPU_GIVING_CALLERS_CCR(enter_supervisor_mode, enter_supervisor_mode_with_ccr);

/* Sets the interrupt mask, in supervisor mode, to mask's, the
 * argument; returns the status register as it was (0x13378), its
 * condition codes its caller's: the body of set_interrupt_mask. */
static __attribute__((used, noipa)) uint16_t
set_interrupt_mask_with_ccr(uint32_t callers_ccr, const uint32_t* arguments) {
  const uint16_t mask = arguments[0];

  return cpu_interrupt_mask_set(
      cpu_sr_at_call(cpu_enter_supervisor(), callers_ccr), mask);
}

CPU_GIVING_CALLERS_CCR(set_interrupt_mask, set_interrupt_mask_with_ccr);
