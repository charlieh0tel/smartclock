/* startup.c's functions that are this image's own: the rest are
 * common/startup.c's, the same in every image (docs/firmware/rewrite.md,
 * "Shared C").  From the 58503A 3704's and the Z3815A 4010's
 * rewritten/startup.c: of its functions, those the Z3816A's code carries
 * (identical or relocated by image_diff, or changed with the same
 * instructions), with the static helpers they use, each the same here but
 * where its comment says otherwise (docs/firmware/z3816a-4001.md, "The link
 * check").  Its comments' addresses are the Z3816A's.  Written by hand in
 * place of the decompiler's (docs/firmware/README.md, "Hand-written C"); each
 * keeps its generated prototype. */

/* Sets the GPT up (0x22118). */
void gpt_init(void) { startup_gpt_init_both_pwm(); }

/* Enters the installer with trap #11, which does not come back
 * (restart.md); scpi_task_run calls it for :SYSTem:LANGuage (0x23036).
 * The 58503A 3704's but that there is no front panel task to delete. */
void enter_installer(void) { __asm__ volatile("trap #11"); }

/* The reset's path: the exception hook cleared, the watchdog serviced,
 * the QSM and GPT set up, system_init, the kernel's configuration, the
 * watchdog serviced again, DUART channel A set up for the GPS engine,
 * and the kernel started with trap #6 (0x22e72).  common/startup.c's
 * but for duarta_init (0x22eb2), which common's has none of. */
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
  duarta_init();
  primary_trap_6();
}
