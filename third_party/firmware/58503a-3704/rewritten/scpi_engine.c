/* scpi_engine.c's functions that are this image's own: the rest are
 * common/scpi_engine.c's, the same in every image (docs/firmware/rewrite.md,
 * "Shared C").  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype. */

/* The source a command's input port gives: always 0; the handlers pass
 * their call's input (0x29246). */
int32_t return_zero(scpi_port* input) { return 0; }
