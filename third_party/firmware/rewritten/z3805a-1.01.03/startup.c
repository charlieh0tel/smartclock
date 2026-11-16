/* From the 58503B 1.01.04's rewritten/startup.c and the Z3815A 4010's
 * rewritten/startup.c, the Z3805A 1.01.03's own beside common/'s: of its
 * functions, those the Z3805A 1.01.03's code carries identical or
 * relocated (docs/firmware/z3805a-1.01.03.md, "The link list").  Its
 * comments' addresses are the Z3805A 1.01.03's, but where they name
 * another image.  Each keeps its generated prototype. */

/* Sets the GPT up (0x121c2). */
void gpt_init(void) { startup_gpt_init_both_pwm(); }

/* Enters the installer with trap #11, which does not come back
 * (restart.md); this image has no front panel to blank first, as the
 * 58503A's has (0x1308e). */
void enter_installer(void) { __asm__ volatile("trap #11"); }

/* The reset's path: the exception hook cleared, the watchdog serviced, the
 * QSM and GPT set up, system_init, and the kernel started with trap #6. */
void primary_start(uint32_t a, uint32_t b, uint32_t c) {
  exception_hook_reset();
  sim.swsr = CPU_SWSR_FIRST;
  sim.swsr = CPU_SWSR_SECOND;
  qsm_init();
  gpt_init();
  start_nothing();
  system_init();
  psos_config_init();
  sim.swsr = CPU_SWSR_FIRST;
  sim.swsr = CPU_SWSR_SECOND;
  primary_trap_6();
}
