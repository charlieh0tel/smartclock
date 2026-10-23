/* The installer's handlers of the SCPI common commands, *CLS and *IDN?
 * (`applications/scpi/handlers/common`; scpi_common.c has the
 * primary's), on the installer's status model and identification.
 * Written by hand in place of the decompiler's (docs/firmware/README.md,
 * "Hand-written C"); each keeps its generated prototype. */

/* *CLS: clears every event word and empties the error queue, the
 * call's status model's (installer::0x107b90). */
int32_t installer_scpi_set_common_CLS(void* parameters, void* response,
                                      scpi_call* call) {
  int32_t code;

  installer_status_clear_events(call->status);
  do {
    installer_error_queue_clear(call->status, &code);
  } while (code != 0);
  return 0;
}

/* *IDN?: the maker, model, serial number and the installer's name, Peru,
 * with the hardware suffix, in installer_idn_response, as counted text
 * (installer::0x107bc4); docs/firmware/restart.md, "The installer". */
int32_t installer_scpi_query_common_IDN(void* parameters, void* response,
                                        scpi_call* call) {
  scpi_token_string* identification = response;

  installer_sprintf(installer_idn_response, "HEWLETT-PACKARD,%s,%s,%s-%c",
                    installer_idn_model, installer_idn_serial, "Peru",
                    installer_idn_suffix);
  identification->text = installer_idn_response;
  identification->length = scpi_engine_length(identification->text);
  return 0;
}
