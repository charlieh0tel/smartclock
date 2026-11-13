/* The SCPI engine's port object in this image's ROM and its port layer's
 * numbers; common/scpi.h has what the SCPI groups share.
 *
 * The Z3817A runs one SCPI session, on the SCI, device 0, which it shares
 * with the pForth console, one at a time (docs/firmware/z3817a-3752.md,
 * "The ports"), where the Z3815A runs two, on DUART channel B and the SCI.
 * Its session's cells are the Z3815A's sessions' layout: the echo text
 * (scpi_echo_text, 0x102fa0), port_error_reports, the prompt setting,
 * the last input, the repeat, the status byte and its enable, the line and
 * its pointers, the prompt text and the echo's length (scpi_echo_length,
 * 0x1031cd).  The installer is the Z3815A's byte for byte, its port layer
 * the SCI's alone too. */

/* The session's object in ROM, console_port (0x3991a):
 * console_port_read (0x39732), its method table at 0x398d6, then the
 * prompts; scpi_console_port and sci_scpi_port both return it (0x3963e,
 * 0x396e6). */
static scpi_port* const SCPI_CONSOLE_PORT = &console_port;

/* The port layer's number, the one case each of its switches has
 * (0x39958 to 0x39a80): the SCI, the console's and the SCPI session's.
 * The installer's switches take it too (installer::0x107502). */
static const int16_t SCPI_PORT_SCI = 1;

/* The request source every command gives: scpi_input_source returns 1
 * whatever the input (0x39ac0), the Z3815A's source for its SCI session,
 * and status_report_from reports it and REQUEST_SOURCE_STARTUP through
 * sci_scpi_status_report, the others not at all (0x3cc5e); the shared
 * request_source names it by the others' images, where nothing passes
 * it.  The installer's returns it too (installer::0x10760a). */
static const request_source REQUEST_SOURCE_SCI = REQUEST_SOURCE_SILENT_1;
