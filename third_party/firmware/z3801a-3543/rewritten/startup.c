/* startup.c's functions that are this image's own: the rest are
 * common/startup.c's, the same in every image (docs/firmware/rewrite.md,
 * "Shared C").  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype. */

/* Sets the GPT up (0x12050). */
void gpt_init(void) { startup_gpt_init(); }

/* Enters the installer with trap #11, which does not come back
 * (restart.md); scpi_task calls it for :SYSTem:LANGuage (0x12f2c). */
void enter_installer(void) { __asm__ volatile("trap #11"); }
