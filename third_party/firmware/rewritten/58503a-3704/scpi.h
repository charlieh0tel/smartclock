/* The SCPI engine's port object in this image's ROM; common/scpi.h has
 * what the SCPI groups share. */

/* The console port's object in ROM: console_port_read, its method table
 * at 0x2904c, then the prompts (structures.md, "The SCPI lexer's token
 * and port"); the export names no label there. */
static scpi_port* const SCPI_CONSOLE_PORT = (scpi_port*)0x29090;
