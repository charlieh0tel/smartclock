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
