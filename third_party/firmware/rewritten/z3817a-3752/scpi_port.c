/* From the Z3815A 4010's rewritten/scpi_port.c, the Z3817A 3752's own beside
 * common/'s: of its functions, those the Z3817A's code carries identical or
 * relocated, and the port layer's, changed from the Z3815A's, most of them the
 * 58503A's code: console_port_read, status_update_rqs and port_status_report to
 * port_update_rqs (docs/firmware/z3817a-3752.md, "The function check").  Its
 * comments' addresses are the Z3817A's, but where they name another image. Each
 * keeps its generated prototype. */

/* Sets scpi_prompt_enabled, port_prompt_set's for port 1, the SCI
 * (0x3962c). */
void scpi_prompt_set(uint8_t enabled) { scpi_prompt_enabled = enabled; }

/* scpi_prompt_enabled, port_prompt_get's for port 1, the SCI
 * (0x39636). */
uint8_t scpi_prompt_get(void) { return scpi_prompt_enabled; }

/* Writes a character to the session's port, the SCI, device 0, through
 * console_write, keeping it in the echo buffer, or CR LF for an end
 * (0x397f4), the Z3815A's on DUART channel B. */
void scpi_console_putc(scpi_port* port, int32_t character) {
  if (character >= SCPI_PORT_CHARACTERS) {
    console_write("\r\n");
    return;
  }
  char text[2] = {character, 0};

  console_write(text);
  scpi_echo_text[scpi_echo_length++] = character;
}

/* The session's object, port_object's for port 1 (0x3963e). */
scpi_port* scpi_console_port(void) { return SCPI_CONSOLE_PORT; }

/* Prompts again when the status model that changed is the session's,
 * scpi_status_model, where the Z3805A's compares status_object
 * (0x39942). */
void status_prompt_refresh(status_model* status, int32_t error) {
  if (status == scpi_status_model) {
    scpi_prompt(error);
  }
}

/* The session's object, as scpi_console_port: sci_scpi_session_init's
 * last instruction, which image_diff pairs with the Z3815A's
 * sci_scpi_port (0x396e6). */
scpi_port* sci_scpi_port(void) { return SCPI_CONSOLE_PORT; }

/* The session's read: the next character of the line, reading a line
 * once the last is used, SCPI_PORT_END for its end or a read error,
 * which port_report_errors reports for the SCI; the last again once
 * scpi_input_ended has marked it (0x39732).  The 58503A's on port 1. */
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
    port_report_errors(SCPI_PORT_SCI, errors);
  }
  scpi_last_input = character;
  return character;
}

/* The session's request for service, from its status byte and enable
 * (0x398bc). */
void status_update_rqs(void) {
  port_update_rqs(&status_byte, service_request_enable, SCPI_PORT_SCI);
}

/* sci_scpi_status_report(code) for the SCI, its result; for another port
 * the port less 1, what the switch's compare leaves in D0 (0x39958). */
uint32_t port_status_report(char port, int16_t code) {
  if (port != SCPI_PORT_SCI) {
    return port - SCPI_PORT_SCI;
  }
  return sci_scpi_status_report(code);
}

/* For the SCI, reports the condition of each of port_error_reports'
 * entries whose mask the receive status has a bit of; the last entry's
 * report's result, or 0 when it has none of its bits, or for another
 * port the port less 1, as port_status_report (0x39982), as the
 * installer's. */
uint32_t port_report_errors(char port, uint8_t status) {
  uint32_t result = port - SCPI_PORT_SCI;

  if (result != 0) {
    return result;
  }
  for (uint8_t index = 0;
       index < sizeof port_error_reports / sizeof *port_error_reports;
       index++) {
    result = 0;
    if ((port_error_reports[index].mask & status) != 0) {
      result = port_status_report(port, port_error_reports[index].condition);
    }
  }
  return result;
}

/* The port's prompt setting; 0 for another port (0x399de). */
uint8_t port_prompt_get(char port) {
  return port == SCPI_PORT_SCI ? scpi_prompt_get() : 0;
}

/* Sets the port's prompt setting (0x39a06). */
void port_prompt_set(char port, uint8_t enabled) {
  if (port == SCPI_PORT_SCI) {
    scpi_prompt_set(enabled);
  }
}

/* Opens the port's session; its port object, or null for another port
 * (0x39a30). */
scpi_port* port_session_init(char port) {
  return port == SCPI_PORT_SCI ? sci_scpi_session_init() : NULL;
}

/* The port's object, or null for another port (0x39a58). */
scpi_port* port_object(char port) {
  return port == SCPI_PORT_SCI ? scpi_console_port() : NULL;
}

/* Sets RQS in a status byte while another bit of it is enabled, else
 * clears it; for the SCI keeps it in status_request_service too
 * (0x39a80), where the 58503A's also lights the alarm and drives its
 * outputs. */
void port_update_rqs(uint8_t* status_byte, uint8_t enable, char port) {
  const bool request =
      (*status_byte & enable & ~STATUS_BYTE_REQUEST_SERVICE) != 0;

  if (request) {
    *status_byte |= STATUS_BYTE_REQUEST_SERVICE;
  } else {
    *status_byte &= ~STATUS_BYTE_REQUEST_SERVICE;
  }
  if (port == SCPI_PORT_SCI) {
    status_request_service = request;
  }
}
