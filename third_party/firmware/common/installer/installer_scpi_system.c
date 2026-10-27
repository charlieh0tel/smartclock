/* The installer's handlers of its SCPI :SYSTem tree, :ERRor? and
 * :LANGuage (`applications/scpi/handlers/system`; scpi_system.c and
 * scpi_misc.c have the primary's), on the installer's error queue.
 * Written by hand in place of the decompiler's (docs/firmware/README.md,
 * "Hand-written C"); each keeps its generated prototype. */

/* :ERRor?'s response: the next error of the queue. */
typedef struct installer_scpi_system_error {
  int16_t code; /* +0x00 */
  uint16_t unused;
  char* message; /* +0x04 */
} installer_scpi_system_error;

/* :ERRor?: takes the next error from the call's queue, its message the
 * queue's, else the code's own, else empty. */
int32_t installer_scpi_query_SYSTem_ERRor(void* parameters, void* response,
                                          scpi_call* call) {
  installer_scpi_system_error* error = response;
  int32_t code;
  char* message = 0;

  installer_error_queue_read(call->status, &code, (uint32_t*)&message);
  error->code = code;
  if (message == 0) {
    message = installer_error_message_lookup(code);
  }
  if (message == 0) {
    message = "";
  }
  error->message = message;
  return 0;
}

/* :LANGuage: "PRIMARY" ends the SCPI session, after which
 * installer_scpi_task restarts the processor, and "INSTALL", this one,
 * does nothing; another is an execution error.  From any port, unlike the
 * primary's.  The text is put in upper case in place, its index a byte. */
int32_t installer_scpi_set_SYSTem_LANGuage(void* parameters, void* response,
                                           scpi_call* call) {
  scpi_text_parameter* given = parameters;

  for (int8_t i = 0; given->text[i] != 0; i++) {
    given->text[i] = (char)installer_toupper(given->text[i]);
  }
  if (scpi_parameters_same(given->text, "PRIMARY")) {
    return SCPI_HANDLER_SESSION_END;
  }
  if (scpi_parameters_same(given->text, "INSTALL")) {
    return 0;
  }
  return STATUS_SCPI_EXECUTION_ERROR;
}

/* :LANGuage?: "INSTALL", to any port. */
int32_t installer_scpi_query_SYSTem_LANGuage(void* parameters, void* response,
                                             scpi_call* call) {
  char** language = response;

  *language = "INSTALL";
  return 0;
}
