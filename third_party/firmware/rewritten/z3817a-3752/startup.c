/* startup.c's functions that are this image's own: the rest are
 * common/startup.c's, the same in every image (docs/firmware/rewrite.md,
 * "Shared C").  From the Z3815A 4010's rewritten/startup.c, those the
 * Z3817A's code carries identical or relocated
 * (docs/firmware/z3817a-3752.md, "The function check"), with
 * primary_start, the other images' written as this image's.  Its
 * comments' addresses are the Z3817A's, but where they name another
 * image.  Each keeps its generated prototype. */

/* The reset's path, the other images' but that it sets both DUART channels up
 * (duart_channels_init, 0x22f3a) before it starts the kernel: the
 * exception hook cleared, the watchdog serviced, the QSM and GPT set up,
 * system_init, the kernel's configuration copied, the watchdog serviced
 * again, and the kernel started with trap #6 (0x22efa). */
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
  duart_channels_init();
  primary_trap_6();
}

/* Enters the installer with trap #11, which does not come back
 * (restart.md), as the Z3805A's does, with no front panel to blank, as
 * the 58503A's has (0x230c2). */
void enter_installer(void) { __asm__ volatile("trap #11"); }

/* Sets the GPT up, startup_gpt_init_both_pwm_gp_inputs's body, where the
 * 58503A's makes 0xbc of port GP outputs and sets bit 7 (0x22196). */
void gpt_init(void) { startup_gpt_init_both_pwm_gp_inputs(); }
