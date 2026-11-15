/* From the 58503A 3704's rewritten/scpi.h, the Z3816A 4001's own beside
 * common/'s: the SCPI engine's port object in this image's ROM and its port
 * layer's number; common/scpi.h has what the SCPI groups share. */

/* The console port's object in ROM: console_port_read, its method table
 * at 0x39296, then the prompts (structures.md, "The SCPI lexer's token
 * and port"), which scpi_console_port returns (0x38ffe); the export names
 * no label there. */
static scpi_port* const SCPI_CONSOLE_PORT = (scpi_port*)0x392da;

/* The port layer's number for the SCI, the SCPI session's port here, as
 * the Z3815A's SCPI_PORT_SCI (docs/firmware/z3816a-4001.md, "The host
 * port"): the one port each of the layer's switches has a case for
 * (port_report_errors's, 0x3934a), where the 58503A's is DUART channel
 * B's, 2. */
static const int16_t SCPI_PORT_SCI = 1;

/* The request source a command on the SCI gives, as the Z3815A's
 * (scpi_input_source, 0x39480, and the installer's,
 * installer::0x1075ee): status_report_from reports it to the SCI
 * session's status model; the shared request_source names it by the
 * others' images, where nothing passes it. */
static const request_source REQUEST_SOURCE_SCI = REQUEST_SOURCE_SILENT_1;

/* The SCPI task's event the SCI's transmit interrupt signals at the end
 * of an asynchronous text, as pllc does when a request is done;
 * :PTIMe:TCODe? waits on it. */
static const uint32_t SCPI_TCODE_SENT_EVENT = 0x20;

/* Has the time code sent in a format and waits until the transmit
 * interrupt signals it gone, answering an empty text with the query word
 * cleared, so that scpi_interpret formats no response; false when
 * tcode_request refuses. */
static bool scpi_tcode_send(timecode_format format, void* response,
                            scpi_call* call) {
  scpi_token_string* code = response;
  uint32_t events;

  /* get_v's 1 and wait_v's 0s are as the image passes them. */
  psos_get_v(SCPI_TCODE_SENT_EVENT, 1, &events);
  if (tcode_request(format) == 0) {
    return false;
  }
  psos_wait_v(SCPI_TCODE_SENT_EVENT, 0, 0, &events);
  code->length = 0;
  call->query = 0;
  return true;
}
