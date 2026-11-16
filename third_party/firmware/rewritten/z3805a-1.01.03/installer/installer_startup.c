/* The Z3805A 3543B's installer/installer_startup.c, the Z3805A 1.01.03's
 * own beside common/'s, whose installer is the 3543B's byte for byte
 * (docs/firmware/z3805a-1.01.03.md, "The export and the checks"): of its
 * functions, those common/ does not hold, the 3543B's own
 * rewritten/z3805a-3543b/installer/installer_startup.c's.  Its comments'
 * installer addresses are the 3543B's, which are this image's; a primary
 * address is moved to this image's.  Each keeps its generated prototype. */

/* The installer's copies of the start-up's path (`platform/startup`;
 * startup.c has the primary's): its start, with the MC68331's modules
 * set up through startup.h's bodies, as the primary's are.  Written by
 * hand in place of the decompiler's (docs/firmware/README.md,
 * "Hand-written C"); each keeps its generated prototype. */

/* installer/installer_startup.c's functions that are this image's own: the rest
 * are common/installer/installer_startup.c's, the same in every image
 * (docs/firmware/rewrite.md, "Shared C").  Written by hand in place of the
 * decompiler's (docs/firmware/README.md, "Hand-written C"); each keeps its
 * generated prototype. */

/* Sets the GPT up (installer::0x100c1e). */
void installer_gpt_init(void) { startup_gpt_init(); }
