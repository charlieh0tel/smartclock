/* The installer's copies of the start-up's path (`platform/startup`;
 * startup.c has the primary's): its start, with the MC68331's modules
 * set up through startup.h's bodies, as the primary's are.  Written by
 * hand in place of the decompiler's (docs/firmware/README.md,
 * "Hand-written C"); each keeps its generated prototype. */

/* The installer's start: the exception hook cleared, the watchdog
 * serviced, the QSM and GPT set up, its system_init, and its kernel
 * started (installer::0x100c96). */
void installer_start(void) {
  installer_exception_hook_reset();
  sim.swsr = CPU_SWSR_FIRST;
  sim.swsr = CPU_SWSR_SECOND;
  installer_qsm_init();
  installer_gpt_init();
  installer_start_nothing();
  installer_system_init();
  installer_psos_config_init();
  sim.swsr = CPU_SWSR_FIRST;
  sim.swsr = CPU_SWSR_SECOND;
  installer_kernel_init();
}

/* Sets the QSM up (installer::0x100bf4). */
void installer_qsm_init(void) { startup_qsm_init(); }

/* Sets the GPT up (installer::0x100c1e). */
void installer_gpt_init(void) { startup_gpt_init(); }

/* Does nothing; installer_start calls it between installer_gpt_init and
 * installer_system_init (installer::0x100c7a). */
void installer_start_nothing(void) {}

/* Sets the periodic interrupt on or off (installer::0x100bd2). */
void installer_periodic_interrupt_enable(char enable) {
  startup_periodic_interrupt_enable(enable);
}
