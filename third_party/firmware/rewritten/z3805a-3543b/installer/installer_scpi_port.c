/* The installer's copies of the SCPI prompt's routines
 * (`applications/scpi/port`; scpi_port.c has the primary's), on the
 * installer's prompt and status model.  Written by hand in place of the
 * decompiler's (docs/firmware/README.md, "Hand-written C"); each keeps
 * its generated prototype. */

/* Makes the SCPI prompt, with the latest error when there is one
 * (installer::0x1073f6). */
void installer_scpi_prompt(int32_t error) {
  if (error != 0) {
    installer_sprintf(installer_scpi_prompt_text, "E%+04d> ", error);
    return;
  }
  char* from = "scpi > ";
  char* to = installer_scpi_prompt_text;

  while ((*to++ = *from++) != 0) {
  }
}

/* Prompts again when the status model that changed is the main one
 * (installer::0x1076f4). */
void installer_status_prompt_refresh(status_model* status, int32_t error) {
  if (status == installer_status_object) {
    installer_scpi_prompt(error);
  }
}

/* The installer's console port's object (installer::0x10746a). */
scpi_port* installer_scpi_console_port(void) { return &installer_console_port; }

/* Opens the SCPI port, device 0 (installer::0x103958). */
void installer_scpi_port_open(void) {
  installer_psos_io_open(IO_DEVICE_DUART_B, 0);
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

/* Closes the SCPI port, device 0 (installer::0x103966). */
void installer_scpi_port_close(void) {
  installer_psos_io_close(IO_DEVICE_DUART_B);
}

/* Returns 0; the four methods at +0x20 to +0x2c of the installer's port
 * (installer::0x10747a). */
int32_t installer_scpi_console_return_zero(void) { return 0; }

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

/* Has the next read give the last character again
 * (installer::0x1075a4). */
void installer_scpi_input_ended(void) { installer_scpi_input_repeat = 1; }

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

/* Writes a character to the console, or CR LF for an end; unlike
 * scpi_console_putc it keeps no echo (installer::0x1075ae). */
void installer_scpi_console_putc(scpi_port* port, int32_t character) {
  if (character >= SCPI_PORT_CHARACTERS) {
    installer_console_write("\r\n");
    return;
  }
  char text[2] = {character, 0};

  installer_console_write(text);
}

/* installer_scpi_console_write's loop: up to n characters of s, to its
 * NUL (installer::0x1075fe). */
void installer_scpi_console_write_loop(scpi_port* port, char* s, int32_t n) {
  for (; n > 0 && *s != 0; n--, s++) {
    installer_scpi_console_putc(port, *s);
  }
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

/* The service request enable, a method of the port
 * (installer::0x107646). */
uint8_t installer_status_sre_get(void) {
  return installer_service_request_enable;
}

/* The status byte, a method of the port (installer::0x10764e). */
uint8_t installer_status_byte_get(void) { return installer_status_byte; }
