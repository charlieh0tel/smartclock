/* startup.c's functions that are this image's own: the rest are
 * common/startup.c's, the same in every image (docs/firmware/rewrite.md,
 * "Shared C").  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype. */

/* Enters the installer with trap #11, which does not come back
 * (restart.md), first deleting the front panel's task, where it runs,
 * and blanking the display; scpi_task_run calls it for
 * :SYSTem:LANGuage (0x130da). */
void enter_installer(void) {
  if (task_fpnl != 0) {
    psos_delete(task_fpnl);
    front_panel_send_line(0, "");
  }
  __asm__ volatile("trap #11");
}

/* Sets the GPT up (0x12196). */
void gpt_init(void) { startup_gpt_init_both_pwm(); }

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
