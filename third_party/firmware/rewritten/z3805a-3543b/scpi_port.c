/* scpi_port.c's functions that are this image's own: the rest are
 * common/scpi_port.c's, the same in every image (docs/firmware/rewrite.md,
 * "Shared C").  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype. */

/* The console port's object, which queries compare a request's source
 * with (0x29b82). */
scpi_port* scpi_console_port(void) { return SCPI_CONSOLE_PORT; }

/* The console port's read: the next character of the line, reading a
 * line once the last is used, SCPI_PORT_END for its end or a read
 * error, which it reports; the last again once scpi_input_ended has
 * marked it (0x29bce). */
int16_t console_port_read(void) {
  if (scpi_input_repeat != 0) {
    scpi_input_repeat = 0;
    return scpi_last_input;
  }
  uint8_t errors = 0;

  if (scpi_input_next > scpi_input_end) {
    if (scpi_prompt_enabled != 0) {
      console_write(scpi_prompt_text);
    }
    scpi_input_next = scpi_input_line;
    scpi_input_end =
        scpi_input_line +
        console_read(scpi_input_line, sizeof scpi_input_line - 1, &errors);
  }
  int16_t character;

  if (errors == 0) {
    character = *scpi_input_next++;
    if (character == 0) {
      character = SCPI_PORT_END;
    }
  } else {
    scpi_input_end = scpi_input_line;
    scpi_input_next = scpi_input_line + 1;
    scpi_input_line[0] = 0;
    character = SCPI_PORT_END;
    if (errors & SCPI_CONSOLE_BREAK_OR_FRAMING) {
      status_report(STATUS_FRAMING_ERROR);
    }
    if (errors & SCPI_CONSOLE_OVERRUN) {
      status_report(STATUS_INPUT_OVERRUN);
    }
    if (errors & SCPI_CONSOLE_PARITY) {
      status_report(STATUS_PARITY_ERROR);
    }
    if (errors & SCPI_CONSOLE_COMMUNICATION) {
      status_report(STATUS_COMMUNICATION_ERROR);
    }
  }
  scpi_last_input = character;
  return character;
}

/* Sets bits of the status byte, a method of the port (0x29d50). */
void status_byte_set_bits(scpi_port* port, int32_t bits) {
  status_byte |= bits;
  status_update_rqs();
}

/* Clears bits of the status byte, a method of the port (0x29d62). */
void status_byte_clear_bits(scpi_port* port, int32_t bits) {
  status_byte &= ~bits;
  status_update_rqs();
}

/* Sets the service request enable, a method of the port (0x29d76). */
void status_sre_set(scpi_port* port, int32_t enable) {
  service_request_enable = enable;
  status_update_rqs();
}

/* Sets RQS in the status byte while another bit of it is enabled, and
 * status_request_service with it (0x29d98). */
void status_update_rqs(void) {
  bool request = (status_byte & service_request_enable &
                  ~STATUS_BYTE_REQUEST_SERVICE) != 0;

  if (request) {
    status_byte |= STATUS_BYTE_REQUEST_SERVICE;
  } else {
    status_byte &= ~STATUS_BYTE_REQUEST_SERVICE;
  }
  status_request_service = request;
}
