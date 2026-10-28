/* The SCPI console port object, its method table and the
 * status byte, written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"; docs/firmware/modules.md,
 * `applications/scpi/port`).  The exported C includes this file after
 * its own, so the types and prototypes it declares are in scope; each
 * function here keeps its generated prototype. */

/* Opens the SCPI port, device 0. */
void scpi_port_open(void) { psos_io_open(IO_DEVICE_DUART_B, 0); }

/* Closes the SCPI port, device 0. */
void scpi_port_close(void) { psos_io_close(IO_DEVICE_DUART_B); }

/* Makes the SCPI prompt, with the latest error when there is one. */
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

/* Returns 0; the four methods at +0x20 to +0x2c. */
int32_t scpi_console_return_zero(void) { return 0; }

/* Has the next read give the last character again. */
void scpi_input_ended(void) { scpi_input_repeat = 1; }

/* The port's read of up to length characters into a buffer, stopping
 * at an end, which the next read gives again; the characters read. */
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
 * CR LF for an end. */
void scpi_console_putc(scpi_port* port, int32_t character) {
  if (character >= SCPI_PORT_CHARACTERS) {
    console_write("\r\n");
    return;
  }
  char text[2] = {character, 0};

  console_write(text);
  scpi_echo_text[scpi_echo_length++] = character;
}

/* scpi_console_write's loop: up to n characters of s, to its NUL. */
void scpi_console_write_loop(scpi_port* port, char* s, int32_t n) {
  for (; n > 0 && *s != 0; n--, s++) {
    scpi_console_putc(port, *s);
  }
}

/* The service request enable, a method of the port. */
uint8_t status_sre_get(void) { return service_request_enable; }

/* The status byte, a method of the port. */
uint8_t status_byte_get(void) { return status_byte; }

/* Prompts again when the status model that changed is the main one. */
void status_prompt_refresh(status_model* status, int32_t error) {
  if (status == status_object) {
    scpi_prompt(error);
  }
}

/* Sets bits of the status byte, a method of the port. */
void status_byte_set_bits(scpi_port* port, int32_t bits) {
  status_byte |= bits;
  status_update_rqs();
}

/* Clears bits of the status byte, a method of the port. */
void status_byte_clear_bits(scpi_port* port, int32_t bits) {
  status_byte &= ~bits;
  status_update_rqs();
}

/* Sets the service request enable, a method of the port. */
void status_sre_set(scpi_port* port, int32_t bits) {
  service_request_enable = bits;
  status_update_rqs();
}
