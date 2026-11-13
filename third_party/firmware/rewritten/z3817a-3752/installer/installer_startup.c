/* From the Z3815A 4010's rewritten/installer/installer_startup.c, the Z3817A
 * 3752's own beside common/'s: of its functions, those the Z3817A's code
 * carries identical or relocated (docs/firmware/z3817a-3752.md, "The
 * function check").  Its comments' addresses are the Z3817A's, but where
 * they name another image.  Each keeps its generated prototype. */

/* Sets the GPT up, as gpt_init does (installer::0x100c1e). */
void installer_gpt_init(void) { startup_gpt_init_both_pwm_gp_inputs(); }
