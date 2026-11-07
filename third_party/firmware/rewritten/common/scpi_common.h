/* What the SCPI common commands' files share (scpi_common.c and each
 * image's own scpi_common.c): the status registers the commands
 * read and enable.  The exported C includes the
 * hand-written headers after its own declarations and before the
 * hand-written .c files, so every one of them can use it. */

/* The registers the common commands read and enable
 * (status_register_index STATUS_BYTE and STANDARD_EVENT). */
static const int16_t SCPI_COMMON_STATUS_BYTE = 0;

static const int16_t SCPI_COMMON_STANDARD_EVENT = 1;
