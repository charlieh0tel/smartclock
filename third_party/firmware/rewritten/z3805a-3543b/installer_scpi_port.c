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
