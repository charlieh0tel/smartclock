/* What this image keeps of the Z3805A's SCI driver, written by hand in
 * place of the decompiler's (docs/firmware/README.md, "Hand-written
 * C"): the image has no SCI driver (docs/firmware/z3801a-3543.md, "How
 * it differs from the Z3805A"), but its no-op echo is left behind.  The
 * exported C includes this file after its own, so the types and
 * prototypes it declares are in scope; each function here keeps its
 * generated prototype. */

/* A no-op echo, a bare rts: the Z3805A's sci_init and sci_reconfigure
 * put it in sci.echo when the settings' echo is off; here nothing
 * refers to it (0x1f3ce). */
void sci_echo_none(char c) {}
