/* scpi_port.c's functions that are this image's own: the rest are
 * common/scpi_port.c's, the same in every image (docs/firmware/rewrite.md,
 * "Shared C").  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype.
 *
 * The 58503A puts a port layer between the SCPI task and the console:
 * each operation a switch on the port number with a case for
 * SCPI_PORT_DUART_B alone (docs/firmware/58503a-3704.md, "SCPI on more
 * than one port"). */

/* The console port's object, port_object's for port 2 (0x28db4). */
scpi_port* scpi_console_port(void) { return SCPI_CONSOLE_PORT; }

/* Sets scpi_prompt_enabled, port_prompt_set's for port 2 (0x28da2). */
void scpi_prompt_set(uint8_t enabled) { scpi_prompt_enabled = enabled; }

/* scpi_prompt_enabled, port_prompt_get's for port 2 (0x28dac). */
uint8_t scpi_prompt_get(void) { return scpi_prompt_enabled; }

/* The console port's read: the next character of the line, reading a
 * line once the last is used, SCPI_PORT_END for its end or a read
 * error, which port_report_errors reports; the last again once
 * scpi_input_ended has marked it (0x28ea8). */
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
    port_report_errors(SCPI_PORT_DUART_B, errors);
  }
  scpi_last_input = character;
  return character;
}

/* Sets bits of the status byte, a method of the port (0x28fea). */
void status_byte_set_bits(scpi_port* port, int32_t bits) {
  status_byte |= bits;
  status_update_rqs();
}

/* Clears bits of the status byte, a method of the port (0x28ffc). */
void status_byte_clear_bits(scpi_port* port, int32_t bits) {
  status_byte &= ~bits;
  status_update_rqs();
}

/* Sets the service request enable, a method of the port (0x29010). */
void status_sre_set(scpi_port* port, int32_t bits) {
  service_request_enable = bits;
  status_update_rqs();
}

/* The console's request for service and the alarm, from its status byte
 * and enable (0x29032). */
void status_update_rqs(void) {
  port_update_rqs(&status_byte, service_request_enable, SCPI_PORT_DUART_B);
}

/* status_report(code) for port 2, its result; for another port the
 * port less 2, what the switch's compare leaves in D0 (0x290ce). */
uint32_t port_status_report(char port, int16_t code) {
  if (port != SCPI_PORT_DUART_B) {
    return port - SCPI_PORT_DUART_B;
  }
  return status_report(code);
}

/* For port 2, reports the condition of each of port_error_reports'
 * entries whose mask the receive status has a bit of; 0, or for another
 * port the port less 2, as port_status_report (0x290f8). */
uint32_t port_report_errors(char port, uint8_t status) {
  if (port != SCPI_PORT_DUART_B) {
    return port - SCPI_PORT_DUART_B;
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

/* The port's prompt setting; 0 for another port (0x29154). */
uint8_t port_prompt_get(char port) {
  return port == SCPI_PORT_DUART_B ? scpi_prompt_get() : 0;
}

/* Sets the port's prompt setting (0x2917c). */
void port_prompt_set(char port, uint8_t enabled) {
  if (port == SCPI_PORT_DUART_B) {
    scpi_prompt_set(enabled);
  }
}

/* Opens the port's session; its port object, or null for another port
 * (0x291a6). */
scpi_port* port_session_init(char port) {
  return port == SCPI_PORT_DUART_B ? scpi_session_init_58503a() : NULL;
}

/* The port's object, or null for another port (0x291ce). */
scpi_port* port_object(char port) {
  return port == SCPI_PORT_DUART_B ? scpi_console_port() : NULL;
}

/* Sets RQS in a status byte while another bit of it is enabled, else
 * clears it; for port 2 that is the alarm too, kept in
 * status_request_service and led_alarm and driven out by
 * alarm_outputs_set (0x291f6; docs/firmware/58503a-3704.md, "The alarm
 * outputs"). */
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
    led_alarm = request;
    alarm_outputs_set(request);
  }
}
