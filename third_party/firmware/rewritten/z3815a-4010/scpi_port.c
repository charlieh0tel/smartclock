/* scpi_port.c's functions that are this image's own: the rest are
 * common/scpi_port.c's, the same in every image (docs/firmware/rewrite.md,
 * "Shared C").  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype.
 *
 * The Z3815A's port layer is the 58503A's with a second port: each
 * operation a switch on the port number with a case for SCPI_PORT_SCI,
 * the SCI session's, and for SCPI_PORT_DUART_B (scpi.h). */

/* Writes a character to the SCPI port, device 3, DUART channel B, here,
 * keeping it in the echo buffer, or CR LF for an end (0x3c1ea). */
void scpi_console_putc(scpi_port* port, int32_t character) {
  if (character >= SCPI_PORT_CHARACTERS) {
    duartb_port_write("\r\n");
    return;
  }
  char text[2] = {character, 0};

  duartb_port_write(text);
  scpi_echo_text[scpi_echo_length++] = character;
}

/* Prompts again when the status model that changed is a session's: the
 * SCPI port's, then the SCI's (0x3c63e).  Its second test is an entry of
 * its own (0x3c656), which nothing calls. */
void status_prompt_refresh(status_model* status, int32_t error) {
  if (status == scpi_status_model) {
    scpi_prompt(error);
  }
  if (status == sci_scpi_status_model) {
    sci_scpi_prompt(error);
  }
}

/* The SCPI port's object, port_object's for port 2 (0x3c034). */
scpi_port* scpi_console_port(void) { return SCPI_CONSOLE_PORT; }

/* Sets scpi_prompt_enabled, port_prompt_set's for port 2 (0x3c022). */
void scpi_prompt_set(uint8_t enabled) { scpi_prompt_enabled = enabled; }

/* scpi_prompt_enabled, port_prompt_get's for port 2 (0x3c02c). */
uint8_t scpi_prompt_get(void) { return scpi_prompt_enabled; }

/* The SCPI port's read: the next character of the line, reading a line
 * from DUART channel B once the last is used, SCPI_PORT_END for its end
 * or a read error, which port_report_errors reports; the last again once
 * scpi_input_ended has marked it (0x3c128). */
int16_t console_port_read(void) {
  if (scpi_input_repeat != 0) {
    scpi_input_repeat = 0;
    return scpi_last_input;
  }
  uint8_t errors = 0;

  if (scpi_input_next > scpi_input_end) {
    if (scpi_prompt_enabled != 0) {
      duartb_port_write(scpi_prompt_text);
    }
    scpi_input_next = scpi_input_line;
    scpi_input_end =
        scpi_input_line +
        duartb_port_read(scpi_input_line, sizeof scpi_input_line - 1, &errors);
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
    port_report_errors(SCPI_PORT_DUART_B, errors);
  }
  scpi_last_input = character;
  return character;
}

/* The SCPI port's request for service, from its status byte and enable
 * (0x3c2b2). */
void status_update_rqs(void) {
  port_update_rqs(&status_byte, service_request_enable, SCPI_PORT_DUART_B);
}

/* Reports a code to the port's session: sci_scpi_status_report's for
 * port 1, status_report's for port 2, their result; for another port the
 * port less 2, what the switch's compare leaves in D0 (0x3c670). */
uint32_t port_status_report(char port, int16_t code) {
  switch (port) {
    case SCPI_PORT_SCI:
      return sci_scpi_status_report(code);
    case SCPI_PORT_DUART_B:
      return status_report(code);
    default:
      return port - SCPI_PORT_DUART_B;
  }
}

/* Reports the condition of each of the port's session's error report
 * entries whose mask the receive status has a bit of: sci_scpi_error_
 * reports' for port 1, port_error_reports' for port 2; 0, or for another
 * port the port less 2, as port_status_report (0x3c6ac). */
uint32_t port_report_errors(char port, uint8_t status) {
  port_error_report* reports;

  switch (port) {
    case SCPI_PORT_SCI:
      reports = sci_scpi_error_reports;
      break;
    case SCPI_PORT_DUART_B:
      reports = port_error_reports;
      break;
    default:
      return port - SCPI_PORT_DUART_B;
  }
  for (uint8_t index = 0;
       index < sizeof port_error_reports / sizeof *port_error_reports;
       index++) {
    if ((reports[index].mask & status) != 0) {
      port_status_report(port, reports[index].condition);
    }
  }
  return 0;
}

/* The port's prompt setting; 0 for another port (0x3c742). */
uint8_t port_prompt_get(char port) {
  switch (port) {
    case SCPI_PORT_SCI:
      return sci_scpi_prompt_get();
    case SCPI_PORT_DUART_B:
      return scpi_prompt_get();
    default:
      return 0;
  }
}

/* Sets the port's prompt setting (0x3c772). */
void port_prompt_set(char port, uint8_t enabled) {
  switch (port) {
    case SCPI_PORT_SCI:
      sci_scpi_prompt_set(enabled);
      break;
    case SCPI_PORT_DUART_B:
      scpi_prompt_set(enabled);
      break;
  }
}

/* Opens the port's session; its port object, or null for another port
 * (0x3c7ae). */
scpi_port* port_session_init(char port) {
  switch (port) {
    case SCPI_PORT_SCI:
      return sci_scpi_session_init();
    case SCPI_PORT_DUART_B:
      return scpi_session_init();
    default:
      return NULL;
  }
}

/* The port's object, or null for another port (0x3c7de). */
scpi_port* port_object(char port) {
  switch (port) {
    case SCPI_PORT_SCI:
      return sci_scpi_port();
    case SCPI_PORT_DUART_B:
      return scpi_console_port();
    default:
      return NULL;
  }
}

/* Sets RQS in a status byte while another bit of it is enabled, else
 * clears it; for port 2 also keeps that in status_request_service.  The
 * 58503A's drives the alarm here too; this image's does not (0x3c80e). */
void port_update_rqs(uint8_t* status_byte, uint8_t enable, char port) {
  const bool request =
      (*status_byte & enable & ~STATUS_BYTE_REQUEST_SERVICE) != 0;

  if (request) {
    *status_byte |= STATUS_BYTE_REQUEST_SERVICE;
  } else {
    *status_byte &= ~STATUS_BYTE_REQUEST_SERVICE;
  }
  if (port == SCPI_PORT_DUART_B) {
    status_request_service = request;
  }
}

/* The SCI session's port: the same routines as the SCPI port's over its
 * own cells, reading and writing the console, device 0. */

/* Sets sci_scpi_prompt_enabled, port_prompt_set's for port 1 (0x3c32c). */
void sci_scpi_prompt_set(uint8_t enabled) { sci_scpi_prompt_enabled = enabled; }

/* sci_scpi_prompt_enabled, port_prompt_get's for port 1 (0x3c336). */
uint8_t sci_scpi_prompt_get(void) { return sci_scpi_prompt_enabled; }

/* The SCI session's object, port_object's for port 1 (0x3c33e). */
scpi_port* sci_scpi_port(void) { return SCI_SCPI_PORT; }

/* Makes the SCI session's prompt, with the latest error when there is
 * one, as scpi_prompt does the SCPI port's (0x3c346). */
void sci_scpi_prompt(int32_t error) {
  if (error != 0) {
    sprintf(sci_scpi_prompt_text, "E%+04d> ", error);
    return;
  }
  char* from = "scpi > ";
  char* to = sci_scpi_prompt_text;

  while ((*to++ = *from++) != 0) {
  }
}

/* Closes the SCI session's port, device 0 (0x3c3ee). */
void sci_scpi_session_close(void) { scpi_port_close(); }

/* The SCI session's read of up to length characters into a buffer,
 * stopping at an end, which the next read gives again; the characters
 * read (0x3c3fa). */
int32_t sci_scpi_read(scpi_port* port, uint8_t* buffer, int32_t length) {
  int32_t count = 0;

  while (count < length) {
    int16_t character = sci_scpi_port_read();

    if (character >= SCPI_PORT_CHARACTERS) {
      sci_scpi_input_ended();
      break;
    }
    buffer[count++] = character;
  }
  return count;
}

/* The SCI session's read: as console_port_read, from the console, its
 * errors port 1's (0x3c432). */
int16_t sci_scpi_port_read(void) {
  if (sci_scpi_input_repeat != 0) {
    sci_scpi_input_repeat = 0;
    return sci_scpi_last_input;
  }
  uint8_t errors = 0;

  if (sci_scpi_input_next > sci_scpi_input_end) {
    if (sci_scpi_prompt_enabled != 0) {
      console_write(sci_scpi_prompt_text);
    }
    sci_scpi_input_next = sci_scpi_input_line;
    sci_scpi_input_end = sci_scpi_input_line +
                         console_read(sci_scpi_input_line,
                                      sizeof sci_scpi_input_line - 1, &errors);
  }
  int16_t character;

  if (errors == 0) {
    character = *sci_scpi_input_next++;
    if (character == 0) {
      character = SCPI_PORT_END;
    }
  } else {
    sci_scpi_input_end = sci_scpi_input_line;
    sci_scpi_input_next = sci_scpi_input_line + 1;
    sci_scpi_input_line[0] = 0;
    character = SCPI_PORT_END;
    port_report_errors(SCPI_PORT_SCI, errors);
  }
  sci_scpi_last_input = character;
  return character;
}

/* Has the SCI session's next read give the last character again
 * (0x3c4ea). */
void sci_scpi_input_ended(void) { sci_scpi_input_repeat = 1; }

/* Writes a character to the console, keeping it in the SCI session's echo
 * buffer, or CR LF for an end (0x3c4f4). */
void sci_scpi_putc(scpi_port* port, int32_t character) {
  if (character >= SCPI_PORT_CHARACTERS) {
    console_write("\r\n");
    return;
  }
  char text[2] = {character, 0};

  console_write(text);
  sci_scpi_echo_text[sci_scpi_echo_length++] = character;
}

/* sci_scpi_write's loop: up to n characters of s, to its NUL (0x3c564). */
void sci_scpi_write_loop(scpi_port* port, char* s, int32_t n) {
  for (; n > 0 && *s != 0; n--, s++) {
    sci_scpi_putc(port, *s);
  }
}

/* Sets bits of the SCI session's status byte, a method of its port
 * (0x3c574). */
void sci_status_byte_set_bits(scpi_port* port, int32_t bits) {
  sci_status_byte |= bits;
  sci_status_update_rqs();
}

/* Clears bits of the SCI session's status byte (0x3c586). */
void sci_status_byte_clear_bits(scpi_port* port, int32_t bits) {
  sci_status_byte &= ~bits;
  sci_status_update_rqs();
}

/* Sets the SCI session's service request enable (0x3c59a). */
void sci_status_sre_set(scpi_port* port, int32_t bits) {
  sci_service_request_enable = bits;
  sci_status_update_rqs();
}

/* The SCI session's service request enable (0x3c5ac). */
uint8_t sci_status_sre_get(void) { return sci_service_request_enable; }

/* The SCI session's status byte (0x3c5b4). */
uint8_t sci_status_byte_get(void) { return sci_status_byte; }

/* The SCI session's request for service, from its status byte and enable
 * (0x3c5bc). */
void sci_status_update_rqs(void) {
  port_update_rqs(&sci_status_byte, sci_service_request_enable, SCPI_PORT_SCI);
}
