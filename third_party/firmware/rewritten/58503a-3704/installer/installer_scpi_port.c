/* installer/installer_scpi_port.c's functions that are this image's own: the
 * rest are common/installer/installer_scpi_port.c's, the same in every image
 * (docs/firmware/rewrite.md, "Shared C").  Written by hand in place of the
 * decompiler's (docs/firmware/README.md, "Hand-written C"); each keeps its
 * generated prototype.
 *
 * The installer has the primary's port layer (scpi_port.c), each
 * operation a switch on the port number with a case for
 * SCPI_PORT_DUART_B alone, but for installer_port_status_report and
 * installer_port_update_rqs, which take no case. */

/* Sets installer_scpi_prompt_enabled (installer::0x10738a). */
void installer_scpi_prompt_set(uint8_t enabled) {
  installer_scpi_prompt_enabled = enabled;
}

/* installer_scpi_prompt_enabled (installer::0x107394). */
uint8_t installer_scpi_prompt_get(void) {
  return installer_scpi_prompt_enabled;
}

/* The port's read of up to length characters into a buffer, stopping
 * at an end, which the next read gives again; the characters read
 * (installer::0x107458). */
int32_t installer_scpi_console_read(scpi_port* port, uint8_t* buffer,
                                    int32_t length) {
  int32_t count = 0;

  while (count < length) {
    int16_t character = installer_console_port_read_58503a();

    if (character >= SCPI_PORT_CHARACTERS) {
      installer_scpi_input_ended();
      break;
    }
    buffer[count++] = character;
  }
  return count;
}

/* The console port's read, the primary's console_port_read on the
 * installer's state: the next character of the line, reading a line
 * once the last is used, SCPI_PORT_END for its end or a read error,
 * which installer_port_report_errors reports; the last again once
 * installer_scpi_input_ended has marked it (installer::0x107490).  The
 * image sets the result's low word alone, its callers reading no more. */
int16_t installer_console_port_read_58503a(void) {
  if (installer_scpi_input_repeat != 0) {
    installer_scpi_input_repeat = 0;
    return installer_scpi_last_input;
  }
  uint8_t errors = 0;

  if (installer_scpi_input_next > installer_scpi_input_end) {
    if (installer_scpi_prompt_enabled != 0) {
      installer_console_write(installer_scpi_prompt_text);
    }
    installer_scpi_input_next = installer_scpi_input_line;
    installer_scpi_input_end =
        installer_scpi_input_line +
        installer_console_read(installer_scpi_input_line,
                               sizeof installer_scpi_input_line - 1, &errors);
  }
  int16_t character;

  if (errors == 0) {
    character = *installer_scpi_input_next++;
    if (character == 0) {
      character = SCPI_PORT_END;
    }
  } else {
    installer_scpi_input_end = installer_scpi_input_line;
    installer_scpi_input_next = installer_scpi_input_line + 1;
    installer_scpi_input_line[0] = 0;
    character = SCPI_PORT_END;
    installer_port_report_errors(SCPI_PORT_DUART_B, errors);
  }
  installer_scpi_last_input = character;
  return character;
}

/* Sets bits of the status byte, a method of the port
 * (installer::0x1075b2). */
void installer_status_byte_set_bits(scpi_port* port, int32_t bits) {
  installer_status_byte |= bits;
  installer_status_update_rqs();
}

/* Clears bits of the status byte, a method of the port
 * (installer::0x1075c4). */
void installer_status_byte_clear_bits(scpi_port* port, int32_t bits) {
  installer_status_byte &= ~bits;
  installer_status_update_rqs();
}

/* Sets the service request enable, a method of the port
 * (installer::0x1075d8). */
void installer_status_sre_set(scpi_port* port, int32_t bits) {
  installer_service_request_enable = bits;
  installer_status_update_rqs();
}

/* The console's request for service, from its status byte and enable
 * (installer::0x1075fa). */
void installer_status_update_rqs(void) {
  installer_port_update_rqs(&installer_status_byte,
                            installer_service_request_enable,
                            SCPI_PORT_DUART_B);
}

/* Prompts again, whichever status model changed, where the primary's and
 * the Z3805A's installer's compare it with the main one
 * (installer::0x10767c). */
void installer_status_prompt_refresh(status_model* status, int32_t error) {
  installer_scpi_prompt(error);
}

/* installer_status_report(code), whatever the port (installer::0x10768a). */
uint32_t installer_port_status_report(uint8_t port, int16_t code) {
  return installer_status_report(code);
}

/* For port 2, reports the condition of each of
 * installer_port_error_reports' entries whose mask the receive status has
 * a bit of; 0, or for another port the port less 2, what the switch's
 * compare leaves in D0 (installer::0x107698). */
uint32_t installer_port_report_errors(char port, uint8_t status) {
  if (port != SCPI_PORT_DUART_B) {
    return port - SCPI_PORT_DUART_B;
  }
  for (uint8_t index = 0; index < sizeof installer_port_error_reports /
                                      sizeof *installer_port_error_reports;
       index++) {
    if ((installer_port_error_reports[index].mask & status) != 0) {
      installer_port_status_report(
          port, installer_port_error_reports[index].condition);
    }
  }
  return 0;
}

/* Sets the port's prompt setting (installer::0x1076f4). */
void installer_port_prompt_set(char port, uint8_t enabled) {
  if (port == SCPI_PORT_DUART_B) {
    installer_scpi_prompt_set(enabled);
  }
}

/* Opens the port's session; its port object, or null for another port
 * (installer::0x10771e). */
scpi_port* installer_port_session_init(char port) {
  return port == SCPI_PORT_DUART_B ? installer_scpi_session_init_58503a()
                                   : NULL;
}

/* The port's object, or null for another port (installer::0x107746). */
scpi_port* installer_port_object(char port) {
  return port == SCPI_PORT_DUART_B ? installer_scpi_console_port() : NULL;
}

/* Sets RQS in a status byte while another bit of it is enabled, else
 * clears it; whatever the port, the installer drives no alarm
 * (installer::0x10776e). */
void installer_port_update_rqs(uint8_t* status_byte, uint8_t enable,
                               char port) {
  if ((*status_byte & enable & ~STATUS_BYTE_REQUEST_SERVICE) != 0) {
    *status_byte |= STATUS_BYTE_REQUEST_SERVICE;
  } else {
    *status_byte &= ~STATUS_BYTE_REQUEST_SERVICE;
  }
}
