/* installer/installer_startup.c's functions that are this image's own:
 * the rest are common/installer/installer_startup.c's, the same in every
 * image (docs/firmware/rewrite.md, "Shared C").  Written by hand in place
 * of the decompiler's (docs/firmware/README.md, "Hand-written C"); each
 * keeps its generated prototype. */

/* Sets the GPT up, as gpt_init does (installer::0x100c1e). */
void installer_gpt_init(void) { startup_gpt_init_58503a(); }
