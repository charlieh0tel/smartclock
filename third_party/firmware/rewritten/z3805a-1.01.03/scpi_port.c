/* From the 58503B 1.01.04's rewritten/scpi_port.c and the Z3815A 4010's
 * rewritten/scpi_port.c, the Z3805A 1.01.03's own beside common/'s: of its
 * functions, those the Z3805A 1.01.03's code carries identical or
 * relocated (docs/firmware/z3805a-1.01.03.md, "The link list").  Its
 * comments' addresses are the Z3805A 1.01.03's, but where they name
 * another image.  Each keeps its generated prototype. */

/* The console port's object, port_object's for port 2 (0x2a1e2). */
scpi_port* scpi_console_port(void) { return &console_port; }

/* Sets scpi_prompt_enabled, port_prompt_set's for port 2 (0x2a1d0). */
void scpi_prompt_set(uint8_t enabled) { scpi_prompt_enabled = enabled; }

/* scpi_prompt_enabled, port_prompt_get's for port 2 (0x2a1da). */
uint8_t scpi_prompt_get(void) { return scpi_prompt_enabled; }

/* Prompts again when the status model that changed is the SCPI port's,
 * sci_scpi_status_model, where the Z3805A's compares status_object
 * (0x2a4fa). */
void status_prompt_refresh(status_model* status, int32_t error) {
  if (status == scpi_status_model) {
    scpi_prompt(error);
  }
}

/* The SCI session's object, port_object's for port 1 (0x2a28a). */
scpi_port* sci_scpi_port(void) { return &console_port; }

/* The port's write of a counted block, to DUART channel B (0x2a418). */
void scpi_console_write_block(scpi_port* port, char* text, int32_t length) {
  console_write_counted(text, (uint16_t)length);
}

/* The SCPI port's read: the next character of the line, reading a line
 * from DUART channel B once the last is used, SCPI_PORT_END for its end
 * or a read error, which port_report_errors reports; the last again once
 * scpi_input_ended has marked it (0x2a2d6). */
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
    port_report_errors(SCPI_PORT_DUART_B, errors);
  }
  scpi_last_input = character;
  return character;
}

/* The SCPI port's request for service, from its status byte and enable
 * (0x2a474). */
void status_update_rqs(void) {
  port_update_rqs(&status_byte, service_request_enable, SCPI_PORT_DUART_B);
}

/* Sets RQS in a status byte while another bit of it is enabled, else
 * clears it; for port 2 also keeps that in status_request_service.  The
 * 58503A's drives the alarm here too; this image's does not (0x2a638). */
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

/* Opens the port's session; its port object, or null for another port
 * (0x2a5e8). */
scpi_port* port_session_init(char port) {
  return port == SCPI_PORT_DUART_B ? sci_scpi_session_init() : NULL;
}

/* The port's object, the console's for port 2, DUART B's, or null for
 * another (0x2a610). */
scpi_port* port_object(char port) {
  return port == SCPI_PORT_DUART_B ? scpi_console_port() : NULL;
}

/* The port's prompt setting; 0 for another port (0x2a596). */
uint8_t port_prompt_get(char port) {
  return port == SCPI_PORT_DUART_B ? scpi_prompt_get() : 0;
}

/* Sets the port's prompt setting (0x2a5be). */
void port_prompt_set(char port, uint8_t enabled) {
  if (port == SCPI_PORT_DUART_B) {
    scpi_prompt_set(enabled);
  }
}

/* Writes a character to the console, keeping it in the echo's text at
 * the console's count, or CR LF for an end (0x2a398). */
void scpi_console_putc(scpi_port* port, int32_t character) {
  if (character >= SCPI_PORT_CHARACTERS) {
    console_write("\r\n");
    return;
  }
  char text[2] = {character, 0};

  console_write(text);
  scpi_echo_text[scpi_echo_length[0]++] = character;
}

/* Writes a query's response items through their formatters, separated
 * by commas, first emptying the echo of the output's source, its count
 * scpi_echo_length's byte by the source (0x29d54). */
int32_t scpi_format_response(scpi_port* output, scpi_response_item* items,
                             scpi_call* call) {
  scpi_echo_length[(uint8_t)scpi_input_source(output)] = 0;
  for (int16_t offset = 0;; items++) {
    int32_t error =
        items->format(output, call->response + offset, items->choices, call);

    if (error != 0) {
      return error;
    }
    offset += items->size;
    if (items[1].format == 0) {
      return 0;
    }
    output->methods->put(output, SCPI_SEPARATOR);
  }
}

/* Clears the session's input and the console's echo count, sets the
 * receive errors port_report_errors reports, channel B's status bits,
 * opens the port and prompts; the console's port object (0x2a21e). */
scpi_port* sci_scpi_session_init(void) {
  scpi_echo_length[0] = 0;
  sci_scpi_error_reports[0].mask = SCPI_CONSOLE_BREAK_OR_FRAMING;
  sci_scpi_error_reports[1].mask = SCPI_CONSOLE_OVERRUN;
  sci_scpi_error_reports[2].mask = SCPI_CONSOLE_PARITY;
  sci_scpi_error_reports[3].mask = SCPI_CONSOLE_COMMUNICATION;
  sci_scpi_error_reports[0].condition = STATUS_FRAMING_ERROR;
  sci_scpi_error_reports[1].condition = STATUS_INPUT_OVERRUN;
  sci_scpi_error_reports[2].condition = STATUS_PARITY_ERROR;
  sci_scpi_error_reports[3].condition = STATUS_COMMUNICATION_ERROR;
  scpi_input_repeat = 0;
  sci_scpi_input_end = scpi_input_line;
  sci_scpi_input_next = scpi_input_line;
  scpi_input_line[0] = 0;
  scpi_port_open();
  scpi_prompt(0);
  status_update_rqs();
  return sci_scpi_port();
}

/* Reports a code to the port's session, status_report's result, for
 * port 2, the switch's one case; for another port the port less 2, what
 * the switch's compare leaves in D0 (0x2a510). */
uint32_t port_status_report(char port, int16_t code) {
  if (port == SCPI_PORT_DUART_B) {
    return status_report(code);
  }
  return port - SCPI_PORT_DUART_B;
}

/* For port 2, reports the condition of each of sci_scpi_error_reports'
 * entries whose mask the receive status has a bit of; returns what the
 * last entry leaves in D0, its report's result or 0, and for another
 * port the port less 2, as port_status_report (0x2a53a). */
uint32_t port_report_errors(char port, uint8_t status) {
  uint32_t result = port - SCPI_PORT_DUART_B;

  if (port == SCPI_PORT_DUART_B) {
    for (uint8_t index = 0;
         index < sizeof sci_scpi_error_reports / sizeof *sci_scpi_error_reports;
         index++) {
      result = 0;
      if ((sci_scpi_error_reports[index].mask & status) != 0) {
        result =
            port_status_report(port, sci_scpi_error_reports[index].condition);
      }
    }
  }
  return result;
}
