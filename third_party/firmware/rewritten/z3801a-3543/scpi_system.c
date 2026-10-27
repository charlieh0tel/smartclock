/* scpi_system.c's functions that are this image's own: the rest are
 * common/scpi_system.c's, the same in every image (docs/firmware/rewrite.md,
 * "Shared C").  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype. */

/* The port a :COMMunicate:SERial node's path word names: SERial1,
 * DUART channel B, duartb_settings'; the Z3805A's SERial2, its SCI, is
 * not in this image, whose SERial2 nodes share SERial1's handlers. */
enum {
  SCPI_SYSTEM_SERIAL1 = 0,
};

/* A port's boolean or choice parameter. */
typedef struct scpi_system_port_word {
  int16_t port;   /* +0x00 */
  int16_t kind;   /* +0x02 */
  uint16_t value; /* +0x04 */
} scpi_system_port_word;

/* A port's number, or a query's optional MINimum or MAXimum. */
typedef struct scpi_system_port_number {
  int16_t port;                      /* +0x00 */
  scpi_engine_long_parameter number; /* +0x02 */
} scpi_system_port_number;

/* Whether the call came in on the console port. */
static bool scpi_system_from_console(const scpi_call* call) {
  return scpi_console_port() == call->input;
}

/* :COMMunicate?: the console port's, SERial1, the first of its choices;
 * from another port nothing (0x2f034). */
int32_t scpi_query_SYSTem_COMMunicate(void* parameters, void* response,
                                      scpi_call* call) {
  uint16_t* choice = response;

  if (scpi_system_from_console(call)) {
    *choice = SCPI_SYSTEM_SERIAL1;
  }
  return 0;
}

/* :COMMunicate:SERial:ADDRess?: not in this firmware (0x2f04e). */
int32_t scpi_query_SYSTem_COMMunicate_SER1_ADDRess(void* parameters,
                                                   void* response,
                                                   scpi_call* call) {
  return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
}

/* :COMMunicate:SERial1:PROMpt: whether the console prompts; SERial2's
 * is ignored (0x2a830). */
int32_t scpi_set_SYSTem_COMMunicate_SER1_PROMpt(void* parameters,
                                                void* response,
                                                scpi_call* call) {
  scpi_system_port_word* given = parameters;

  if (given->port == SCPI_SYSTEM_SERIAL1) {
    scpi_prompt_enabled = (uint8_t)given->value;
  }
  return 0;
}

/* :COMMunicate:SERial1:PROMpt?; SERial2's answers nothing (0x2a846). */
int32_t scpi_query_SYSTem_COMMunicate_SER1_PROMpt(void* parameters,
                                                  void* response,
                                                  scpi_call* call) {
  int16_t* port = parameters;
  uint16_t* state = response;

  if (*port == SCPI_SYSTEM_SERIAL1) {
    *state = scpi_prompt_enabled;
  }
  return 0;
}

/* :LANGuage: from the console, "PFORTH" or "INSTALL" ends the SCPI
 * session for the pForth console or the installer (097-z3801-01, table
 * 4-2), and "PRIMARY", this one, does nothing; another is an illegal
 * parameter value.  The text is put in upper case in place, its index
 * a byte (0x2f57a). */
int32_t scpi_set_SYSTem_LANGuage(void* parameters, void* response,
                                 scpi_call* call) {
  scpi_text_parameter* given = parameters;

  for (int8_t i = 0; given->text[i] != 0; i++) {
    given->text[i] = (char)toupper(given->text[i]);
  }
  if (!scpi_system_from_console(call)) {
    return 0;
  }
  if (scpi_parameters_same(given->text, "PFORTH")) {
    scpi_language = SCPI_LANGUAGE_PFORTH;
    return SCPI_HANDLER_SESSION_END;
  }
  if (scpi_parameters_same(given->text, "INSTALL")) {
    scpi_language = SCPI_LANGUAGE_INSTALL;
    return SCPI_HANDLER_SESSION_END;
  }
  if (scpi_parameters_same(given->text, "PRIMARY")) {
    return 0;
  }
  request_source source = return_zero(call->input);

  status_report_from(STATUS_ILLEGAL_PARAMETER_VALUE, source);
  return 0;
}

/* :LANGuage?: "PRIMARY" to the console; to another port nothing
 * (0x2f64c). */
int32_t scpi_query_SYSTem_LANGuage(void* parameters, void* response,
                                   scpi_call* call) {
  char** language = response;

  if (scpi_system_from_console(call)) {
    *language = "PRIMARY";
  }
  return 0;
}

/* :PRINt?: requests scpi_param_SYSTem_PRINt with no value, answers 0, and on
 * the console prints the status screen (0x2f690). */
int32_t scpi_query_SYSTem_PRINt(void* parameters, void* response,
                                scpi_call* call) {
  uint16_t* answer = response;
  request_source source = return_zero(call->input);

  pllc_request_c(&scpi_param_SYSTem_PRINt, 0, 0, 0, source);
  *answer = 0;
  if (scpi_system_from_console(call)) {
    /* printf returns an int32_t, a long, and printf_function an int */
    status_screen_print(0, console_write, (printf_function*)printf);
  }
  return 0;
}
