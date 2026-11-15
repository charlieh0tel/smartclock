/* From the Z3816A 4001's rewritten/startup.c, the 58503B 1.01.04's own beside
 * common/'s: of its functions, those the 58503B's code carries identical or
 * relocated (docs/firmware/58503b-1.01.04.md, "The link list").  Its
 * comments' addresses are the 58503B's, but where they name another image.
 * Each keeps its generated prototype. */

/* Sets the GPT up (0x221ec). */
void gpt_init(void) { startup_gpt_init_both_pwm(); }

/* Enters the installer with trap #11, which does not come back
 * (restart.md), first deleting the front panel's task and blanking the
 * display, as the 58503A's does, with a call to front_panel_poll_key
 * between; scpi_task_run calls it for :SYSTem:LANGuage (0x38dd8)
 * (0x230c2). */
void enter_installer(void) {
  if (task_fpnl != 0) {
    psos_delete(task_fpnl);
    front_panel_poll_key();
    front_panel_send_line(0, "");
  }
  __asm__ volatile("trap #11");
}

/* The reset's path: the exception hook cleared, the watchdog serviced,
 * the QSM and GPT set up, system_init, the kernel's tables, and the
 * kernel started with trap #6; common/'s, but that it sets the ACIA up
 * last, acia_init, before the kernel starts (0x22eea). */
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
  acia_init();
  primary_trap_6();
}
