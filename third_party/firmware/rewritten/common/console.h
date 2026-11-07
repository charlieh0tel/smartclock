/* What the console's files share (console.c and each image's own
 * console.c, docs/firmware/console.md): the console task's
 * priorities.  The exported C includes the
 * hand-written headers after its own declarations and before the
 * hand-written .c files, so every one of them can use it. */

/* The console task's priority while it runs, and while it hands the port
 * back to SCPI, below the SCPI task's creator (console.md, "Leaving
 * it"). */
static const uint32_t CONSOLE_PRIORITY = 50;

static const uint32_t CONSOLE_HALT_PRIORITY = 26;

static const uint32_t CONSOLE_SCPI_CREATOR_PRIORITY = 25;
