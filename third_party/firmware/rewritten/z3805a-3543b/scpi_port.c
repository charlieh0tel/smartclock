/* The SCPI console port object, its method table at 0x29dd4 and the
 * status byte, written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"; docs/firmware/modules.md,
 * `applications/scpi/port`).  The exported C includes this file after
 * its own, so the types and prototypes it declares are in scope; each
 * function here keeps its generated prototype. */

/* Opens the SCPI port, device 0 (0x1dee6). */
void scpi_port_open(void) { psos_io_open(IO_DEVICE_DUART_B, 0); }

/* Closes the SCPI port, device 0 (0x1def4). */
void scpi_port_close(void) { psos_io_close(IO_DEVICE_DUART_B); }

/* What a port's read gives for the end of its input or a read error
 * (structures.md, "The SCPI session"). */
enum {
  SCPI_PORT_END = 0x101,
};

/* Makes the SCPI prompt, with the latest error when there is one
 * (0x29b0e). */
void scpi_prompt(int32_t error) {
  if (error != 0) {
    sprintf(scpi_prompt_text, "E%+04d> ", error);
    return;
  }
  char* from = "scpi > ";
  char* to = scpi_prompt_text;

  while ((*to++ = *from++) != 0) {
  }
}

/* The console port's object, which queries compare a request's source
 * with (0x29b82). */
scpi_port* scpi_console_port(void) { return SCPI_CONSOLE_PORT; }

/* Returns 0; the four methods at +0x20 to +0x2c (0x29b92). */
int32_t scpi_console_return_zero(void) { return 0; }

/* The console port's read: the next character of the line, reading a
 * line once the last is used, SCPI_PORT_END for its end or a read
 * error, which it reports; the last again once scpi_input_ended has
 * marked it (0x29bce). */
int16_t console_port_read(void) {
  /* console_read's error bits, the DUART's received break and framing
   * error, overrun and parity error (MC68681, SRA), and its own bit 0. */
  static const uint8_t CONSOLE_BREAK_OR_FRAMING = 0xc0;
  static const uint8_t CONSOLE_OVERRUN = 0x10;
  static const uint8_t CONSOLE_PARITY = 0x20;
  static const uint8_t CONSOLE_COMMUNICATION = 0x01;

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
    if (errors & CONSOLE_BREAK_OR_FRAMING) {
      status_report(STATUS_FRAMING_ERROR);
    }
    if (errors & CONSOLE_OVERRUN) {
      status_report(STATUS_INPUT_OVERRUN);
    }
    if (errors & CONSOLE_PARITY) {
      status_report(STATUS_PARITY_ERROR);
    }
    if (errors & CONSOLE_COMMUNICATION) {
      status_report(STATUS_COMMUNICATION_ERROR);
    }
  }
  scpi_last_input = character;
  return character;
}

/* Has the next read give the last character again (0x29cc6). */
void scpi_input_ended(void) { scpi_input_repeat = 1; }

/* The port's read of up to length characters into a buffer, stopping
 * at an end, which the next read gives again; the characters read
 * (0x29b96). */
int32_t scpi_console_read(scpi_port* port, uint8_t* buffer, int32_t length) {
  int32_t count = 0;

  while (count < length) {
    int16_t character = console_port_read();

    if (character >= SCPI_PORT_CHARACTERS) {
      scpi_input_ended();
      break;
    }
    buffer[count++] = character;
  }
  return count;
}

/* Writes a character to the console, keeping it in the echo buffer, or
 * CR LF for an end (0x29cd0). */
void scpi_console_putc(scpi_port* port, int32_t character) {
  if (character >= SCPI_PORT_CHARACTERS) {
    console_write("\r\n");
    return;
  }
  char text[2] = {character, 0};

  console_write(text);
  scpi_echo_text[scpi_echo_length++] = character;
}

/* scpi_console_write's loop: up to n characters of s, to its NUL
 * (0x29d40). */
void scpi_console_write_loop(scpi_port* port, char* s, int32_t n) {
  for (; n > 0 && *s != 0; n--, s++) {
    scpi_console_putc(port, *s);
  }
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

/* The service request enable, a method of the port (0x29d88). */
uint8_t status_sre_get(void) { return service_request_enable; }

/* The status byte, a method of the port (0x29d90). */
uint8_t status_byte_get(void) { return status_byte; }

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

/* Prompts again when the status model that changed is the main one
 * (0x29e3c). */
void status_prompt_refresh(status_model* status, int32_t error) {
  if (status == status_object) {
    scpi_prompt(error);
  }
}
