/* The SCPI engine's port object in this image's ROM; common/scpi.h has
 * what the SCPI groups share. */

/* The console port's object in ROM: console_port_read, its method table
 * at 0x2904c, then the prompts (structures.md, "The SCPI lexer's token
 * and port"); the export names no label there. */
static scpi_port* const SCPI_CONSOLE_PORT = (scpi_port*)0x29090;

/* The port layer's number for DUART channel B, the SCPI console: the one
 * port each of its switches has a case for (0x290ce to 0x29246), and the
 * path word of the :COMMunicate nodes for it, duartb_settings': ALARm,
 * BOOT, DRTB, PFORth, SERial and TOD; REMote and SCI give 1, the rest 0
 * (docs/firmware/58503a-3704.md, "SCPI on more than one port"). */
static const int16_t SCPI_PORT_DUART_B = 2;

/* The SCPI task's event duartb_transmit_interrupt signals at the end of
 * an asynchronous text, as pllc does when a request is done; :PTIMe:TCODe?
 * and :RTCM? wait on it. */
static const uint32_t SCPI_TCODE_SENT_EVENT = 0x20;

/* Has the time code sent in a format and waits until DUART channel B's
 * transmit interrupt signals it gone, answering an empty text with the
 * query word cleared, so that scpi_interpret formats no response; false
 * when tcode_request refuses. */
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
