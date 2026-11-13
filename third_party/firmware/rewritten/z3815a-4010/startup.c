/* startup.c's functions that are this image's own: the rest are
 * common/startup.c's, the same in every image (docs/firmware/rewrite.md,
 * "Shared C").  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype. */

/* Sets the GPT up, startup_gpt_init_both_pwm_gp_inputs's body, where the
 * 58503A's makes 0xbc of port GP outputs and sets bit 7 (0x221ae). */
void gpt_init(void) { startup_gpt_init_both_pwm_gp_inputs(); }

/* Enters the installer with trap #11, which does not come back
 * (restart.md), as the Z3805A's does, with no front panel to blank, as
 * the 58503A's has (0x230d2). */
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
