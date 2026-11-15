/* From the Z3816A 4001's rewritten/scpi.h, the 58503B 1.01.04's own beside
 * common/'s: the SCPI engine's port object in this image's ROM and its port
 * layer's number; common/scpi.h has what the SCPI groups share. */

/* The console port's object in ROM: console_port_read, its method table
 * at 0x38970, then the prompts (structures.md, "The SCPI lexer's token
 * and port"), which scpi_console_port returns (0x386c4); the export names
 * no label there. */
static scpi_port* const SCPI_CONSOLE_PORT = (scpi_port*)0x389b4;

/* The port layer's number for the SCI, the SCPI session's port here, as
 * the Z3815A's and the Z3816A's SCPI_PORT_SCI: the one port each of the layer's
 * switches has a case for (port_report_errors's, 0x38a24), where the 58503A's
 * is DUART channel B's, 2. */
static const int16_t SCPI_PORT_SCI = 1;

/* The request source a command on the SCI gives, as the Z3815A's and
 * the Z3816A's: status_report_from reports it to the SCI
 * session's status model; the shared request_source names it by the
 * others' images, where nothing passes it. */
static const request_source REQUEST_SOURCE_SCI = REQUEST_SOURCE_SILENT_1;

/* The GPS UTC setting :DIAGnostic:GPSystem:UTC and :SOURce:PTIMe:UTC hand
 * pllc: a number past the four settings, 0 to 3, is taken as 1; a
 * keyword's value as given (0x3a43e to 0x3a454, 0x3e446 to 0x3e45c). */
static uint8_t scpi_gps_utc_setting(const scpi_word_parameter* given) {
  static const int16_t GPS_UTC_LAST = 3;
  static const uint8_t GPS_UTC_OUT_OF_RANGE = 1;

  if (given->kind == SCPI_PARAMETER_DECIMAL &&
      (given->value < 0 || given->value > GPS_UTC_LAST)) {
    return GPS_UTC_OUT_OF_RANGE;
  }
  return given->value;
}
