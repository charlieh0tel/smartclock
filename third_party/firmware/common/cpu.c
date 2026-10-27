/* C's wrapper of the status register, written by hand in place of the
 * decompiler's (docs/firmware/README.md, "Hand-written C";
 * docs/firmware/modules.md, `platform/cpu`): it enters supervisor
 * mode with enter_supervisor, trap #7, which returns the status register
 * as it was, and moves a new one in.  The exported C includes this file
 * after its own, so the types and prototypes it declares are in scope;
 * each function here keeps its generated prototype. */

CPU_GIVING_CALLERS_CCR(enter_supervisor_mode, enter_supervisor_mode_with_ccr);

CPU_GIVING_CALLERS_CCR(set_interrupt_mask, set_interrupt_mask_with_ccr);
