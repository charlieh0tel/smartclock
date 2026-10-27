/* installer/installer_scpi_port.c's functions that are this image's own: the
 * rest are common/installer/installer_scpi_port.c's, the same in every image
 * (docs/firmware/rewrite.md, "Shared C").  Written by hand in place of the
 * decompiler's (docs/firmware/README.md, "Hand-written C"); each keeps its
 * generated prototype. */

/* Prompts again when the status model that changed is the main one
 * (installer::0x1076f4). */
void installer_status_prompt_refresh(status_model* status, int32_t error) {
  if (status == installer_status_object) {
    installer_scpi_prompt(error);
  }
}

/* Sets RQS in the status byte while another bit of it is enabled.
 * Unlike status_update_rqs it keeps no status_request_service
 * (installer::0x107656). */
void installer_status_update_rqs(void) {
  if ((installer_status_byte & installer_service_request_enable &
       ~STATUS_BYTE_REQUEST_SERVICE) != 0) {
    installer_status_byte |= STATUS_BYTE_REQUEST_SERVICE;
  } else {
    installer_status_byte &= ~STATUS_BYTE_REQUEST_SERVICE;
  }
}

/* The console port's read: the next character of the line, reading a
 * line once the last is used, SCPI_PORT_END for its end or a read
 * error, which it reports; the last again once
 * installer_scpi_input_ended has marked it.  Unlike console_port_read
 * it prompts with no switch, and its conditions are the installer's
 * (installer::0x1074b6). */
int16_t installer_console_port_read(void) {
  if (installer_scpi_input_repeat != 0) {
    installer_scpi_input_repeat = 0;
    return installer_scpi_last_input;
  }
  uint8_t errors = 0;

  if (installer_scpi_input_next > installer_scpi_input_end) {
    installer_console_write(installer_scpi_prompt_text);
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
    if (errors & SCPI_CONSOLE_BREAK_OR_FRAMING) {
      installer_status_report(INSTALLER_STATUS_FRAMING_ERROR);
    }
    if (errors & SCPI_CONSOLE_OVERRUN) {
      installer_status_report(INSTALLER_STATUS_INPUT_OVERRUN);
    }
    if (errors & SCPI_CONSOLE_PARITY) {
      installer_status_report(INSTALLER_STATUS_PARITY_ERROR);
    }
    if (errors & SCPI_CONSOLE_COMMUNICATION) {
      installer_status_report(INSTALLER_STATUS_COMMUNICATION_ERROR);
    }
  }
  installer_scpi_last_input = character;
  return character;
}

/* The port's read of up to length characters into a buffer, stopping
 * at an end, which the next read gives again; the characters read
 * (installer::0x10747e). */
int32_t installer_scpi_console_read(scpi_port* port, uint8_t* buffer,
                                    int32_t length) {
  int32_t count = 0;

  while (count < length) {
    int16_t character = installer_console_port_read();

    if (character >= SCPI_PORT_CHARACTERS) {
      installer_scpi_input_ended();
      break;
    }
    buffer[count++] = character;
  }
  return count;
}

/* Sets bits of the status byte, a method of the port
 * (installer::0x10760e). */
void installer_status_byte_set_bits(scpi_port* port, int32_t bits) {
  installer_status_byte |= bits;
  installer_status_update_rqs();
}

/* Clears bits of the status byte, a method of the port
 * (installer::0x107620). */
void installer_status_byte_clear_bits(scpi_port* port, int32_t bits) {
  installer_status_byte &= ~bits;
  installer_status_update_rqs();
}

/* Sets the service request enable, a method of the port
 * (installer::0x107634). */
void installer_status_sre_set(scpi_port* port, int32_t enable) {
  installer_service_request_enable = enable;
  installer_status_update_rqs();
}
