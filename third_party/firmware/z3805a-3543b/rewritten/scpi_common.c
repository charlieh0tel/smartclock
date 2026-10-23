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

/* The greatest *ESE or *SRE mask. */
static const int32_t SCPI_COMMON_MASK_MAXIMUM = 0xff;

/* Sets a register's enable, through pllc, from a mask within 0 to
 * 0xff, else reports -222. */
static void scpi_common_enable_set(int16_t index,
                                   const scpi_word_parameter* mask,
                                   scpi_port* input) {
  if (scpi_check_range(0, mask->value, SCPI_COMMON_MASK_MAXIMUM,
                       return_zero(input)) != 0) {
    pllc_request_c(&scpi_param_STATus_ENABle, index, mask->value, 0,
                   return_zero(input));
    status_register_set_enable(status_object, index,
                               nv_settings.status_enable[index]);
  }
}

/* A register's enable, reporting a device-specific error where it
 * differs from nv_settings'. */
static void scpi_common_enable_query(int16_t index, int16_t* enable,
                                     scpi_port* input) {
  status_register_get_enable(status_object, index, (uint16_t*)enable);
  if (nv_settings.status_enable[index] != *enable) {
    status_report_from(STATUS_DEVICE_SPECIFIC_ERROR, return_zero(input));
  }
}

/* *CLS: clears every event word and empties the error queue
 * (0x2a18c). */
int32_t scpi_set_common_CLS(void* parameters, void* response, scpi_call* call) {
  int32_t code;

  status_clear_events(status_object);
  do {
    error_queue_clear(call->status, &code);
  } while (code != 0);
  return 0;
}

/* *ESE: the Standard Event register's enable (0x2a1e2). */
int32_t scpi_set_common_ESE(void* parameters, void* response, scpi_call* call) {
  scpi_common_enable_set(SCPI_COMMON_STANDARD_EVENT, parameters, call->input);
  return 0;
}

/* *ESE? (0x2a268). */
int32_t scpi_query_common_ESE(void* parameters, void* response,
                              scpi_call* call) {
  scpi_common_enable_query(SCPI_COMMON_STANDARD_EVENT, response, call->input);
  return 0;
}

/* *ESR?: the Standard Event register, cleared as read (0x2a2ae). */
int32_t scpi_query_common_ESR(void* parameters, void* response,
                              scpi_call* call) {
  status_read_event(status_object, SCPI_COMMON_STANDARD_EVENT, response);
  return 0;
}

/* *IDN?: the maker, model, serial number and firmware revision with the
 * hardware suffix, in the source's text buffer, as counted text
 * (0x2a2ca). */
int32_t scpi_query_common_IDN(void* parameters, void* response,
                              scpi_call* call) {
  scpi_token_string* identification = response;
  char* text = scpi_response_texts[(uint8_t)return_zero(call->input)];

  sprintf(text, "HEWLETT-PACKARD,%s,%s,%s-%c", idn_model, idn_serial,
          RECORDS_FIRMWARE_REVISION, idn_suffix);
  identification->text = text;
  identification->length = scpi_engine_length(text);
  return 0;
}

/* *OPC: nothing; every command completes before the next (0x2a356). */
int32_t scpi_set_common_OPC(void* parameters, void* response, scpi_call* call) {
  return 0;
}

/* *OPC?: "1" (0x2a35a). */
int32_t scpi_query_common_OPC(void* parameters, void* response,
                              scpi_call* call) {
  const char** text = response;

  *text = "1";
  return 0;
}

/* *RST: nothing (0x2a368). */
int32_t scpi_set_common_RST(void* parameters, void* response, scpi_call* call) {
  return 0;
}

/* *SRE: the service request enable (0x2a36c). */
int32_t scpi_set_common_SRE(void* parameters, void* response, scpi_call* call) {
  scpi_common_enable_set(SCPI_COMMON_STATUS_BYTE, parameters, call->input);
  return 0;
}

/* *SRE? (0x2a3ee). */
int32_t scpi_query_common_SRE(void* parameters, void* response,
                              scpi_call* call) {
  scpi_common_enable_query(SCPI_COMMON_STATUS_BYTE, response, call->input);
  return 0;
}

/* *STB?: the Status Byte (0x2a432). */
int32_t scpi_query_common_STB(void* parameters, void* response,
                              scpi_call* call) {
  status_read_event(status_object, SCPI_COMMON_STATUS_BYTE, response);
  return 0;
}

/* *TST?: runs the self-tests, test 0, through pllc; their result, 0
 * for passed (0x2a44c). */
int32_t scpi_query_common_TST(void* parameters, void* response,
                              scpi_call* call) {
  static const uint8_t ALL_TESTS = 0;
  uint32_t* result = response;

  pllc_request_b(&scpi_action_self_test, ALL_TESTS, return_zero(call->input));
  *result = selftest.result;
  return 0;
}

/* *WAI: nothing (0x2a47c). */
int32_t scpi_set_common_WAI(void* parameters, void* response, scpi_call* call) {
  return 0;
}
