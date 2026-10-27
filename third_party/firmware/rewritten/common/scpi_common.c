/* The SCPI common commands, IEEE 488.2's *CLS to *WAI
 * (docs/scpi/README.md): their handlers, written by hand in place of the
 * decompiler's (docs/firmware/README.md, "Hand-written C";
 * docs/firmware/modules.md, `applications/scpi/handlers/common`).
 * 097-z3801-01, table 4-2, lists the commands.  The exported C includes
 * this file after its own, so the types and prototypes it declares are
 * in scope; each function here keeps its generated prototype. */

/* The registers the common commands read and enable
 * (status_register_index STATUS_BYTE and STANDARD_EVENT). */
static const int16_t SCPI_COMMON_STATUS_BYTE = 0;

static const int16_t SCPI_COMMON_STANDARD_EVENT = 1;

/* *CLS: clears every event word and empties the error queue. */
int32_t scpi_set_common_CLS(void* parameters, void* response, scpi_call* call) {
  int32_t code;

  status_clear_events(status_object);
  do {
    error_queue_clear(call->status, &code);
  } while (code != 0);
  return 0;
}

/* *ESR?: the Standard Event register, cleared as read. */
int32_t scpi_query_common_ESR(void* parameters, void* response,
                              scpi_call* call) {
  status_read_event(status_object, SCPI_COMMON_STANDARD_EVENT, response);
  return 0;
}

/* *OPC: nothing; every command completes before the next. */
int32_t scpi_set_common_OPC(void* parameters, void* response, scpi_call* call) {
  return 0;
}

/* *OPC?: "1". */
int32_t scpi_query_common_OPC(void* parameters, void* response,
                              scpi_call* call) {
  const char** text = response;

  *text = "1";
  return 0;
}

/* *RST: nothing. */
int32_t scpi_set_common_RST(void* parameters, void* response, scpi_call* call) {
  return 0;
}

/* *STB?: the Status Byte. */
int32_t scpi_query_common_STB(void* parameters, void* response,
                              scpi_call* call) {
  status_read_event(status_object, SCPI_COMMON_STATUS_BYTE, response);
  return 0;
}

/* *WAI: nothing. */
int32_t scpi_set_common_WAI(void* parameters, void* response, scpi_call* call) {
  return 0;
}
