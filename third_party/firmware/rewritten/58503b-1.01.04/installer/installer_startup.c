/* From the Z3815A 4010's rewritten/installer/installer_startup.c, the 58503B
 * 1.01.04's own beside common/'s: the installer's copy of gpt_init, the
 * Z3815A's installer byte for byte, which leaves port GP all inputs where the
 * 58503B's primary makes some of it outputs (startup.h;
 * docs/firmware/58503b-1.01.04.md, "The link list").  It keeps its generated
 * prototype. */

/* Sets the GPT up, as gpt_init does but for port GP
 * (installer::0x100c1e). */
void installer_gpt_init(void) { startup_gpt_init_both_pwm_gp_inputs(); }
