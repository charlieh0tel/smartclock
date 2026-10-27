/* scpi_port.c's functions that are this image's own: the rest are
 * common/scpi_port.c's, the same in every image (docs/firmware/rewrite.md,
 * "Shared C").  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype. */

/* The console port's object, port_object's for port 2 (0x28db4). */
scpi_port* scpi_console_port(void) { return SCPI_CONSOLE_PORT; }
