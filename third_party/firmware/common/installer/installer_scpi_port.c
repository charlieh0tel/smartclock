/* The installer's copies of the SCPI prompt's routines
 * (`applications/scpi/port`; scpi_port.c has the primary's), on the
 * installer's prompt and status model.  Written by hand in place of the
 * decompiler's (docs/firmware/README.md, "Hand-written C"); each keeps
 * its generated prototype. */

/* Makes the SCPI prompt, with the latest error when there is one. */
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

/* The installer's console port's object. */
scpi_port* installer_scpi_console_port(void) { return &installer_console_port; }

/* Opens the SCPI port, device 0. */
void installer_scpi_port_open(void) {
  installer_psos_io_open(IO_DEVICE_DUART_B, 0);
}

/* Closes the SCPI port, device 0. */
void installer_scpi_port_close(void) {
  installer_psos_io_close(IO_DEVICE_DUART_B);
}

/* Returns 0; the four methods at +0x20 to +0x2c of the installer's port. */
int32_t installer_scpi_console_return_zero(void) { return 0; }

/* Has the next read give the last character again. */
void installer_scpi_input_ended(void) { installer_scpi_input_repeat = 1; }

/* Writes a character to the console, or CR LF for an end; unlike
 * scpi_console_putc it keeps no echo. */
void installer_scpi_console_putc(scpi_port* port, int32_t character) {
  if (character >= SCPI_PORT_CHARACTERS) {
    installer_console_write("\r\n");
    return;
  }
  char text[2] = {character, 0};

  installer_console_write(text);
}

/* installer_scpi_console_write's loop: up to n characters of s, to its
 * NUL. */
void installer_scpi_console_write_loop(scpi_port* port, char* s, int32_t n) {
  for (; n > 0 && *s != 0; n--, s++) {
    installer_scpi_console_putc(port, *s);
  }
}

/* The service request enable, a method of the port. */
uint8_t installer_status_sre_get(void) {
  return installer_service_request_enable;
}

/* The status byte, a method of the port. */
uint8_t installer_status_byte_get(void) { return installer_status_byte; }
