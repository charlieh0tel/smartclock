/* From the Z3816A 4001's rewritten/scpi_port.c, the 58503B 1.01.04's own
 * beside common/'s: of its functions, those the 58503B's code carries
 * identical or relocated (docs/firmware/58503b-1.01.04.md, "The link list").
 * Its comments' addresses are the 58503B's, but where they name another
 * image.  Each keeps its generated prototype. */

/* The console port's object, port_object's for port 2 (0x386c4). */
scpi_port* scpi_console_port(void) { return SCPI_CONSOLE_PORT; }

/* Sets scpi_prompt_enabled, port_prompt_set's for port 2 (0x386b2). */
void scpi_prompt_set(uint8_t enabled) { scpi_prompt_enabled = enabled; }

/* scpi_prompt_enabled, port_prompt_get's for port 2 (0x386bc). */
uint8_t scpi_prompt_get(void) { return scpi_prompt_enabled; }

/* The console's request for service and the alarm, from its status byte
 * and enable (0x38956). */
void status_update_rqs(void) {
  port_update_rqs(&status_byte, service_request_enable, SCPI_PORT_SCI);
}

/* The console port's read: the next character of the line, reading a
 * line once the last is used, SCPI_PORT_END for its end or a read
 * error, which port_report_errors reports; the last again once
 * scpi_input_ended has marked it (0x387b8). */
int16_t console_port_read(void) {
  if (scpi_input_repeat != 0) {
    scpi_input_repeat = 0;
    return scpi_last_input;
  }
  uint8_t errors = 0;

  if (sci_scpi_input_next > sci_scpi_input_end) {
    if (scpi_prompt_enabled != 0) {
      console_write(scpi_prompt_text);
    }
    sci_scpi_input_next = scpi_input_line;
    sci_scpi_input_end =
        scpi_input_line +
        console_read(scpi_input_line, sizeof scpi_input_line - 1, &errors);
  }
  int16_t character;

  if (errors == 0) {
    character = *sci_scpi_input_next++;
    if (character == 0) {
      character = SCPI_PORT_END;
    }
  } else {
    sci_scpi_input_end = scpi_input_line;
    sci_scpi_input_next = scpi_input_line + 1;
    scpi_input_line[0] = 0;
    character = SCPI_PORT_END;
    port_report_errors(SCPI_PORT_SCI, errors);
  }
  scpi_last_input = character;
  return character;
}

/* For port 2, reports the condition of each of sci_scpi_error_reports'
 * entries whose mask the receive status has a bit of; 0, or for another
 * port the port less 2, as port_status_report (0x38a1c). */
uint32_t port_report_errors(char port, uint8_t status) {
  if (port != SCPI_PORT_SCI) {
    return port - SCPI_PORT_SCI;
  }
  for (uint8_t index = 0;
       index < sizeof sci_scpi_error_reports / sizeof *sci_scpi_error_reports;
       index++) {
    if ((sci_scpi_error_reports[index].mask & status) != 0) {
      port_status_report(port, sci_scpi_error_reports[index].condition);
    }
  }
  return 0;
}

/* The port's prompt setting; 0 for another port (0x38a78). */
uint8_t port_prompt_get(char port) {
  return port == SCPI_PORT_SCI ? scpi_prompt_get() : 0;
}

/* Sets the port's prompt setting (0x38aa0). */
void port_prompt_set(char port, uint8_t enabled) {
  if (port == SCPI_PORT_SCI) {
    scpi_prompt_set(enabled);
  }
}

/* Opens the port's session; its port object, or null for another port
 * (0x38aca). */
scpi_port* port_session_init(char port) {
  return port == SCPI_PORT_SCI ? sci_scpi_session_init() : NULL;
}

/* Writes a character to the console, keeping it in the echo buffer, or
 * CR LF for an end (0x3887a). */
void scpi_console_putc(scpi_port* port, int32_t character) {
  if (character >= SCPI_PORT_CHARACTERS) {
    console_write("\r\n");
    return;
  }
  char text[2] = {character, 0};

  console_write(text);
  scpi_echo_text[sci_scpi_echo_length++] = character;
}

/* Prompts again when the status model that changed is the SCPI port's,
 * sci_scpi_status_model, where the Z3805A's compares status_object
 * (0x389dc). */
void status_prompt_refresh(status_model* status, int32_t error) {
  if (status == sci_scpi_status_model) {
    scpi_prompt(error);
  }
}

/* The SCI session's object, port_object's for port 1 (0x3876c). */
scpi_port* sci_scpi_port(void) { return SCPI_CONSOLE_PORT; }

/* The port's object, the console's for the SCI's port, or null for
 * another (0x38af2). */
scpi_port* port_object(char port) {
  return port == SCPI_PORT_SCI ? scpi_console_port() : NULL;
}

/* The SCI session's status_report(code) for the SCI's port, its result;
 * for another port the port less 1, what the switch's compare leaves in
 * D0 (0x389f2).  The 58503A 3704's is DUART channel B's, port 2. */
uint32_t port_status_report(char port, int16_t code) {
  if (port != SCPI_PORT_SCI) {
    return port - SCPI_PORT_SCI;
  }
  return sci_scpi_status_report(code);
}

/* Sets RQS in a status byte while another bit of it is enabled, else
 * clears it; for the SCI's port that is the alarm too, kept in
 * status_request_service and led_alarm and driven out by
 * alarm_outputs_set (0x38b1a).  The 58503A 3704's, whose port is DUART
 * channel B's. */
void port_update_rqs(uint8_t* status_byte, uint8_t enable, char port) {
  const bool request =
      (*status_byte & enable & ~STATUS_BYTE_REQUEST_SERVICE) != 0;

  if (request) {
    *status_byte |= STATUS_BYTE_REQUEST_SERVICE;
  } else {
    *status_byte &= ~STATUS_BYTE_REQUEST_SERVICE;
  }
  if (port == SCPI_PORT_SCI) {
    led_alarm = request;
    status_request_service = request;
    alarm_outputs_set(request);
  }
}

/* The console port's write_block method: the text's bytes sent counted
 * to the SCI, the port not read (0x388fa, the method table at 0x38970's
 * +0x1c). */
void scpi_console_write_block(scpi_port* port, char* text, int32_t length) {
  sci_port_write_counted(text, length);
}
