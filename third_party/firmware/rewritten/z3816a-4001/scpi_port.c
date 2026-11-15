/* scpi_port.c's functions that are this image's own: the rest are
 * common/scpi_port.c's, the same in every image (docs/firmware/rewrite.md,
 * "Shared C").  From the 58503A 3704's and the Z3815A 4010's
 * rewritten/scpi_port.c: of its functions, those the Z3816A's code carries
 * (identical or relocated by image_diff, or changed with the same
 * instructions), with the static helpers they use, each the same here but
 * where its comment says otherwise (docs/firmware/z3816a-4001.md, "The link
 * check").  Its comments' addresses are the Z3816A's.  Written by hand in
 * place of the decompiler's (docs/firmware/README.md, "Hand-written C"); each
 * keeps its generated prototype. */

/* The console port's object, port_object's for port 2 (0x38ffe). */
scpi_port* scpi_console_port(void) { return SCPI_CONSOLE_PORT; }

/* Sets scpi_prompt_enabled, port_prompt_set's for port 2 (0x38fec). */
void scpi_prompt_set(uint8_t enabled) { scpi_prompt_enabled = enabled; }

/* scpi_prompt_enabled, port_prompt_get's for port 2 (0x38ff6). */
uint8_t scpi_prompt_get(void) { return scpi_prompt_enabled; }

/* The console port's read: the next character of the line, reading a
 * line once the last is used, SCPI_PORT_END for its end or a read
 * error, which port_report_errors reports; the last again once
 * scpi_input_ended has marked it (0x390f2). */
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

/* The console's request for service and the alarm, from its status byte
 * and enable (0x3927c). */
void status_update_rqs(void) {
  port_update_rqs(&status_byte, service_request_enable, SCPI_PORT_SCI);
}

/* For port 2, reports the condition of each of port_error_reports'
 * entries whose mask the receive status has a bit of; 0, or for another
 * port the port less 2, as port_status_report (0x39342). */
uint32_t port_report_errors(char port, uint8_t status) {
  if (port != SCPI_PORT_SCI) {
    return port - SCPI_PORT_SCI;
  }
  for (uint8_t index = 0;
       index < sizeof port_error_reports / sizeof *port_error_reports;
       index++) {
    if ((port_error_reports[index].mask & status) != 0) {
      port_status_report(port, port_error_reports[index].condition);
    }
  }
  return 0;
}

/* The port's prompt setting; 0 for another port (0x3939e). */
uint8_t port_prompt_get(char port) {
  return port == SCPI_PORT_SCI ? scpi_prompt_get() : 0;
}

/* Sets the port's prompt setting (0x393c6). */
void port_prompt_set(char port, uint8_t enabled) {
  if (port == SCPI_PORT_SCI) {
    scpi_prompt_set(enabled);
  }
}

/* Opens the port's session; its port object, or null for another port
 * (0x393f0). */
scpi_port* port_session_init(char port) {
  return port == SCPI_PORT_SCI ? scpi_session_init() : NULL;
}

/* Writes a character to the console, keeping it in the echo buffer, or
 * CR LF for an end (0x391b4). */
void scpi_console_putc(scpi_port* port, int32_t character) {
  if (character >= SCPI_PORT_CHARACTERS) {
    console_write("\r\n");
    return;
  }
  char text[2] = {character, 0};

  console_write(text);
  scpi_echo_text[scpi_echo_length++] = character;
}

/* Prompts again when the status model that changed is the SCPI port's,
 * scpi_status_model, where the Z3805A's compares status_object
 * (0x39302). */
void status_prompt_refresh(status_model* status, int32_t error) {
  if (status == scpi_status_model) {
    scpi_prompt(error);
  }
}

/* Sets RQS in a status byte while another bit of it is enabled, else
 * clears it; for port 2 also keeps that in status_request_service.  The
 * 58503A's drives the alarm here too; this image's does not (0x39440). */
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

/* status_report(code) for the SCI's port, 1, its result; for another
 * port the port less 1, what the switch's compare leaves in D0
 * (0x39318).  The 58503A 3704's for its port 2. */
uint32_t port_status_report(char port, int16_t code) {
  if (port != SCPI_PORT_SCI) {
    return port - SCPI_PORT_SCI;
  }
  return status_report(code);
}

/* The port's object, or null for another port than the SCI's (0x39418).
 * The 58503A 3704's for its port 2. */
scpi_port* port_object(char port) {
  return port == SCPI_PORT_SCI ? scpi_console_port() : NULL;
}
