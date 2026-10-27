/* The installer's copies of C's wrapper of the status register
 * (`platform/cpu`; cpu.c has the primary's), which enter supervisor
 * mode through the installer's trap #7.  Written by hand in place of
 * the decompiler's (docs/firmware/README.md, "Hand-written C"); each
 * keeps its generated prototype. */

CPU_GIVING_CALLERS_CCR(installer_enter_supervisor_mode,
                       installer_enter_supervisor_mode_with_ccr);

CPU_GIVING_CALLERS_CCR(installer_set_interrupt_mask,
                       installer_set_interrupt_mask_with_ccr);
