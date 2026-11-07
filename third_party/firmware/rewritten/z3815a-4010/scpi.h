/* The SCPI engine's port objects in this image's ROM and its port layer's
 * numbers; common/scpi.h has what the SCPI groups share.
 *
 * The Z3815A runs two SCPI sessions, each its task's: scpi_task's on
 * DUART channel B, device 3, the SCPI port, and sci_scpi_task's on the
 * SCI, device 0, the console, when root_task does not give the console
 * to pForth (docs/firmware/z3815a-4010.md).  Each session's cells are
 * the same layout: its echo text, then port_error_reports, the prompt
 * setting, the last input, the repeat, the status byte and its enable,
 * the line and its pointers and the prompt text (0x103260 and 0x10348c
 * on, structures.md, "The SCPI session"). */

/* The SCPI port's object in ROM: console_port_read, its method table at
 * 0x3c2cc, then the prompts (structures.md, "The SCPI lexer's token and
 * port"); the export names no label there. */
static scpi_port* const SCPI_CONSOLE_PORT = (scpi_port*)0x3c310;

/* The SCI session's object in ROM: sci_scpi_port_read, its method table
 * at 0x3c5d6, then its own copy of the prompts. */
static scpi_port* const SCI_SCPI_PORT = (scpi_port*)0x3c61a;

/* The port layer's numbers, the two cases each of its switches has
 * (0x3c670 to 0x3c80e): the SCI, the console, and DUART channel B, the
 * SCPI port. */
static const int16_t SCPI_PORT_SCI = 1;

static const int16_t SCPI_PORT_DUART_B = 2;

/* The request source a command on the SCI gives (scpi_input_source,
 * 0x3c84e): status_report_from reports it to the SCI session's status
 * model (0x3faf0); the shared request_source names it by the others'
 * images, where nothing passes it. */
static const request_source REQUEST_SOURCE_SCI = REQUEST_SOURCE_SILENT_1;

/* The SCPI tasks' event the transmit interrupts signal at the end of an
 * asynchronous text, as pllc does when a request is done; :RTCM? waits
 * on it (0x41ff8). */
static const uint32_t SCPI_TCODE_SENT_EVENT = 0x20;

/* Has the time code sent in a format on the port of the source given,
 * the SCI's for REQUEST_SOURCE_SCI, and waits until its transmit
 * interrupt signals it gone, answering an empty text with the query
 * word cleared, so that scpi_interpret formats no response; false when
 * tcode_request refuses. */
static bool scpi_tcode_send(timecode_format format, request_source source,
                            void* response, scpi_call* call) {
  scpi_token_string* code = response;
  uint32_t events;

  /* get_v's 1 and wait_v's 0s are as the image passes them. */
  psos_get_v(SCPI_TCODE_SENT_EVENT, 1, &events);
  if (tcode_request(format, source) == 0) {
    return false;
  }
  psos_wait_v(SCPI_TCODE_SENT_EVENT, 0, 0, &events);
  code->length = 0;
  call->query = 0;
  return true;
}
