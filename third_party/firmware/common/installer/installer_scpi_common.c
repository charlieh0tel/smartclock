/* The installer's handlers of the SCPI common commands, *CLS and *IDN?
 * (`applications/scpi/handlers/common`; scpi_common.c has the
 * primary's), on the installer's status model and identification.
 * Written by hand in place of the decompiler's (docs/firmware/README.md,
 * "Hand-written C"); each keeps its generated prototype. */

/* *CLS: clears every event word and empties the error queue, the
 * call's status model's. */
int32_t installer_scpi_set_common_CLS(void* parameters, void* response,
                                      scpi_call* call) {
  int32_t code;

  installer_status_clear_events(call->status);
  do {
    installer_error_queue_clear(call->status, &code);
  } while (code != 0);
  return 0;
}
