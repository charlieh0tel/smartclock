/* scpi_system.c's functions that are this image's own: the rest are
 * common/scpi_system.c's, the same in every image (docs/firmware/rewrite.md,
 * "Shared C").  From the 58503A 3704's rewritten/scpi_system.c: of its
 * functions, those the Z3816A's code carries (identical or relocated by
 * image_diff, or changed with the same instructions), with the static helpers
 * they use, each the same here but where its comment says otherwise
 * (docs/firmware/z3816a-4001.md, "The link check").  Its comments' addresses
 * are the Z3816A's.  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype. */

/* The rates :BAUD takes, each a serial_baud's (0x3fb1e, 0x3fbae). */
static const int32_t SCPI_SYSTEM_BAUD_1200 = 1200;

static const int32_t SCPI_SYSTEM_BAUD_2400 = 2400;

static const int32_t SCPI_SYSTEM_BAUD_9600 = 9600;

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

/* :COMMunicate:ALARm:ADDRess?: not in this firmware (0x3f9ca). */
int32_t scpi_query_SYSTem_COMMunicate_ALARm_ADDRess(void* parameters,
                                                    void* response,
                                                    scpi_call* call) {
  return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
}

/* Sends a setting of DUART channel B to pllc, else reports an undefined
 * header. */
static void scpi_system_duartb_set(int16_t port, scpi_setting_param* setting,
                                   uint8_t value, const scpi_call* call) {
  if (port == SCPI_PORT_SCI) {
    pllc_request_a(setting, 0, value, scpi_input_source(call->input));
  } else {
    status_report_from(STATUS_UNDEFINED_HEADER, scpi_input_source(call->input));
  }
}

/* :COMMunicate:ALARm:PRESet: requests the port's settings preset
 * (0x3fa96). */
int32_t scpi_set_SYSTem_COMMunicate_ALARm_PRESet(void* parameters,
                                                 void* response,
                                                 scpi_call* call) {
  const int16_t* port = parameters;

  if (*port == SCPI_PORT_SCI) {
    pllc_request_b(&scpi_action_SER1_PRESet, 0, scpi_input_source(call->input));
  } else {
    status_report_from(STATUS_UNDEFINED_HEADER, scpi_input_source(call->input));
  }
  return 0;
}

/* :COMMunicate:ALARm:PROMpt: whether the port prompts, the port layer's
 * (0x3fae4). */
int32_t scpi_set_SYSTem_COMMunicate_ALARm_PROMpt(void* parameters,
                                                 void* response,
                                                 scpi_call* call) {
  const scpi_system_port_word* given = parameters;

  port_prompt_set(given->port, given->value);
  return 0;
}

/* :COMMunicate:ALARm:PROMpt? (0x3fafe). */
int32_t scpi_query_SYSTem_COMMunicate_ALARm_PROMpt(void* parameters,
                                                   void* response,
                                                   scpi_call* call) {
  const int16_t* port = parameters;
  uint16_t* state = response;

  *state = port_prompt_get(*port);
  return 0;
}

/* :COMMunicate:ALARm:RECeive:BAUD: the rate, a number taken up to the
 * next rate the port has, or MINimum or MAXimum (0x3fb1e). */
int32_t scpi_set_SYSTem_COMMunicate_ALARm_RECeive_BAUD(void* parameters,
                                                       void* response,
                                                       scpi_call* call) {
  const scpi_system_port_number* given = parameters;
  serial_baud baud;

  if (given->number.kind == SCPI_VALUE_BOUND) {
    baud = given->number.bound == SCPI_BOUND_MINIMUM ? SERIAL_BAUD_1200
                                                     : SERIAL_BAUD_19200;
  } else if (given->number.value <= SCPI_SYSTEM_BAUD_1200) {
    baud = SERIAL_BAUD_1200;
  } else if (given->number.value <= SCPI_SYSTEM_BAUD_2400) {
    baud = SERIAL_BAUD_2400;
  } else if (given->number.value <= SCPI_SYSTEM_BAUD_9600) {
    baud = SERIAL_BAUD_9600;
  } else {
    baud = SERIAL_BAUD_19200;
  }
  scpi_system_duartb_set(given->port,
                         &scpi_param_SYSTem_COMMunicate_SER1_RECeive_BAUD, baud,
                         call);
  return 0;
}

/* :COMMunicate:ALARm:RECeive:PACE: XON or NONE, the choice's value
 * (0x3fc82). */
int32_t scpi_set_SYSTem_COMMunicate_ALARm_RECeive_PACE(void* parameters,
                                                       void* response,
                                                       scpi_call* call) {
  const scpi_system_port_word* given = parameters;

  scpi_system_duartb_set(given->port,
                         &scpi_param_SYSTem_COMMunicate_SER1_RECeive_PACE,
                         given->value, call);
  return 0;
}

/* :LANGuage?: "PRIMARY" (0x3ff44). */
int32_t scpi_query_SYSTem_LANGuage(void* parameters, void* response,
                                   scpi_call* call) {
  char** language = response;

  *language = "PRIMARY";
  return 0;
}

/* :PON: :SYSTem:PON's action, by pllc (0x3ff52). */
int32_t scpi_set_SYSTem_PON(void* parameters, void* response, scpi_call* call) {
  pllc_request_b(&scpi_action_SYSTem_PON, 0, scpi_input_source(call->input));
  return 0;
}

/* :PRINt?: requests scpi_param_SYSTem_PRINt with no value, answers 0, and
 * prints the status screen of status_print_snapshot to the console, where
 * the 58503A's passes 0 (0x3ffd2). */
int32_t scpi_query_SYSTem_PRINt(void* parameters, void* response,
                                scpi_call* call) {
  uint16_t* answer = response;

  pllc_request_c(&scpi_param_SYSTem_PRINt, 0, 0, 0,
                 scpi_input_source(call->input));
  *answer = 0;
  /* printf returns an int32_t, a long, and printf_function an int */
  status_screen_print(1, console_write, (printf_function*)printf);
  return 0;
}

/* A setting of the SCI's, the ALARm port's, sign-extended as the image
 * reads its byte, else an undefined header. */
static int32_t scpi_system_sci_query(int16_t port, uint8_t setting,
                                     int16_t* value, scpi_call* call) {
  if (port != SCPI_PORT_SCI) {
    return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
  }
  *value = (int8_t)setting;
  return 0;
}

/* :COMMunicate?: SERial1, the first of its choices, for the SCI's source,
 * else SERial2 (0x3f976).  The 58503A 3704's gives SERial1 for source 0,
 * the console's. */
int32_t scpi_query_SYSTem_COMMunicate(void* parameters, void* response,
                                      scpi_call* call) {
  static const uint16_t SERIAL1 = 0;
  static const uint16_t SERIAL2 = 1;
  uint16_t* choice = response;

  *choice = (int8_t)scpi_input_source(call->input) == REQUEST_SOURCE_SCI
                ? SERIAL1
                : SERIAL2;
  return 0;
}

/* :COMMunicate:ALARm?: 1 for the SCI (0x3f9a0), where the 58503A 3704's
 * gives 0 for DUART channel B. */
int32_t scpi_query_SYSTem_COMMunicate_ALARm(void* parameters, void* response,
                                            scpi_call* call) {
  const int16_t* port = parameters;
  uint16_t* value = response;

  if (*port != SCPI_PORT_SCI) {
    return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
  }
  *value = 1;
  return 0;
}

/* :COMMunicate:ALARm:FDUPlex: whether the port echoes what it receives
 * (0x3f9dc). */
int32_t scpi_set_SYSTem_COMMunicate_ALARm_FDUPlex(void* parameters,
                                                  void* response,
                                                  scpi_call* call) {
  const scpi_system_port_word* given = parameters;

  scpi_system_duartb_set(given->port, &scpi_param_SYSTem_COMMunicate_FDUPlex,
                         given->value, call);
  return 0;
}

/* :COMMunicate:ALARm:FDUPlex? (0x3fa34). */
int32_t scpi_query_SYSTem_COMMunicate_ALARm_FDUPlex(void* parameters,
                                                    void* response,
                                                    scpi_call* call) {
  const int16_t* port = parameters;

  return scpi_system_sci_query(*port, sci_settings.echo, response, call);
}

/* :COMMunicate:ALARm:CONTrol:HPACe: an undefined header for any port
 * (0x3fa64), where the 58503A 3704's keeps it. */
int32_t scpi_set_SYSTem_COMMunicate_ALARm_CONTrol_HPACe(void* parameters,
                                                        void* response,
                                                        scpi_call* call) {
  status_report_from(STATUS_UNDEFINED_HEADER, scpi_input_source(call->input));
  return 0;
}

/* :COMMunicate:ALARm:CONTrol:HPACe?: an undefined header (0x3fa84). */
int32_t scpi_query_SYSTem_COMMunicate_ALARm_CONTrol_HPACe(void* parameters,
                                                          void* response,
                                                          scpi_call* call) {
  return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
}

/* :COMMunicate:ALARm:RECeive:BAUD?, or its minimum or maximum: the SCI's
 * rate; another port's an undefined header, and a code out of range
 * answers nothing (0x3fbae, its jump table at 0x3fc02).  The 58503A
 * 3704's. */
int32_t scpi_query_SYSTem_COMMunicate_ALARm_RECeive_BAUD(void* parameters,
                                                         void* response,
                                                         scpi_call* call) {
  static const int32_t SCPI_SYSTEM_BAUD_19200 = 19200;
  scpi_system_port_number* given = parameters;
  int32_t* rate = response;

  if (given->number.kind == SCPI_VALUE_BOUND) {
    *rate = given->number.bound == SCPI_BOUND_MINIMUM ? SCPI_SYSTEM_BAUD_1200
                                                      : SCPI_SYSTEM_BAUD_19200;
    return 0;
  }
  if (given->port != SCPI_PORT_SCI) {
    return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
  }
  switch (sci_settings.baud) {
    case SERIAL_BAUD_1200:
      *rate = SCPI_SYSTEM_BAUD_1200;
      break;
    case SERIAL_BAUD_2400:
      *rate = SCPI_SYSTEM_BAUD_2400;
      break;
    case SERIAL_BAUD_9600:
      *rate = SCPI_SYSTEM_BAUD_9600;
      break;
    case SERIAL_BAUD_19200:
      *rate = SCPI_SYSTEM_BAUD_19200;
      break;
    default:
      break;
  }
  return 0;
}

/* :COMMunicate:ALARm:RECeive:BITS: the engine's undefined header as its
 * result for any port (0x3fc2c), the SCI's bits following its parity. */
int32_t scpi_set_SYSTem_COMMunicate_ALARm_RECeive_BITS(void* parameters,
                                                       void* response,
                                                       scpi_call* call) {
  return STATUS_SCPI_UNDEFINED_HEADER;
}

/* :COMMunicate:ALARm:RECeive:BITS?: the SCI's data bits, eight with no
 * parity, else seven; no minimum or maximum; a parity out of range
 * answers nothing (0x3fc30). */
int32_t scpi_query_SYSTem_COMMunicate_ALARm_RECeive_BITS(void* parameters,
                                                         void* response,
                                                         scpi_call* call) {
  static const int32_t SEVEN_BITS = 7;
  static const int32_t EIGHT_BITS = 8;
  const scpi_system_port_number* given = parameters;
  int32_t* bits = response;

  if (given->port != SCPI_PORT_SCI) {
    return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
  }
  switch (sci_settings.parity) {
    case SERIAL_PARITY_NONE:
      *bits = EIGHT_BITS;
      break;
    case SERIAL_PARITY_EVEN:
    case SERIAL_PARITY_ODD:
    case SERIAL_PARITY_ONE:
      *bits = SEVEN_BITS;
      break;
    default:
      break;
  }
  return 0;
}

/* :COMMunicate:ALARm:RECeive:PACE? (0x3fcda). */
int32_t scpi_query_SYSTem_COMMunicate_ALARm_RECeive_PACE(void* parameters,
                                                         void* response,
                                                         scpi_call* call) {
  const int16_t* port = parameters;

  return scpi_system_sci_query(*port, sci_settings.xon_xoff, response, call);
}

/* :COMMunicate:ALARm:RECeive:PARity:TYPE: the port's parity by its
 * choice, ONE, mark parity, among them (0x3fd0a).  A choice past the
 * list sends D2 as the caller left it (docs/firmware/quirks.md).  The
 * 58503A 3704's. */
int32_t scpi_set_SYSTem_COMMunicate_ALARm_RECeive_PARity_TYPE(void* parameters,
                                                              void* response,
                                                              scpi_call* call) {
  uint8_t parity = CPU_CALLERS_REGISTER(uint32_t, "d2");
  const scpi_system_port_word* given = parameters;

  switch (given->value) {
    case SCPI_SYSTEM_PARITY_EVEN:
      parity = SERIAL_PARITY_EVEN;
      break;
    case SCPI_SYSTEM_PARITY_NONE:
      parity = SERIAL_PARITY_NONE;
      break;
    case SCPI_SYSTEM_PARITY_ODD:
      parity = SERIAL_PARITY_ODD;
      break;
    case SCPI_SYSTEM_PARITY_ONE:
      parity = SERIAL_PARITY_ONE;
      break;
  }
  scpi_system_duartb_set(
      given->port, &scpi_param_SYSTem_COMMunicate_SER1_RECeive_PARity_TYPE,
      parity, call);
  return 0;
}

/* :COMMunicate:ALARm:RECeive:PARity:TYPE?; a code out of range answers
 * nothing (0x3fd8e).  The 58503A 3704's. */
int32_t scpi_query_SYSTem_COMMunicate_ALARm_RECeive_PARity_TYPE(
    void* parameters, void* response, scpi_call* call) {
  const int16_t* port = parameters;
  uint16_t* choice = response;

  if (*port != SCPI_PORT_SCI) {
    return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
  }
  switch (sci_settings.parity) {
    case SERIAL_PARITY_NONE:
      *choice = SCPI_SYSTEM_PARITY_NONE;
      break;
    case SERIAL_PARITY_EVEN:
      *choice = SCPI_SYSTEM_PARITY_EVEN;
      break;
    case SERIAL_PARITY_ODD:
      *choice = SCPI_SYSTEM_PARITY_ODD;
      break;
    case SERIAL_PARITY_ONE:
      *choice = SCPI_SYSTEM_PARITY_ONE;
      break;
    default:
      break;
  }
  return 0;
}

/* :COMMunicate:ALARm:RECeive:SBITs: the engine's undefined header as its
 * result for any port (0x3fdec). */
int32_t scpi_set_SYSTem_COMMunicate_ALARm_RECeive_SBITs(void* parameters,
                                                        void* response,
                                                        scpi_call* call) {
  return STATUS_SCPI_UNDEFINED_HEADER;
}

/* :COMMunicate:ALARm:RECeive:SBITs?: one stop bit for the SCI; no
 * minimum or maximum (0x3fdf0). */
int32_t scpi_query_SYSTem_COMMunicate_ALARm_RECeive_SBITs(void* parameters,
                                                          void* response,
                                                          scpi_call* call) {
  static const int32_t ONE_STOP_BIT = 1;
  const scpi_system_port_number* given = parameters;
  int32_t* bits = response;

  if (given->port != SCPI_PORT_SCI) {
    return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
  }
  *bits = ONE_STOP_BIT;
  return 0;
}

/* :LANGuage: from the SCI's source, "PFORTH" or "INSTALL" ends the SCPI
 * session for the pForth console or the installer; another is an
 * illegal parameter value, "PRIMARY" too.  The text is put in upper case
 * in place, its index a byte (0x3fe8a).  The 58503A 3704's but that it
 * takes the SCI's source, 1 (0x3fee0), where the 58503A's takes 0. */
int32_t scpi_set_SYSTem_LANGuage(void* parameters, void* response,
                                 scpi_call* call) {
  scpi_text_parameter* given = parameters;

  for (int8_t i = 0; given->text[i] != 0; i++) {
    given->text[i] = (char)toupper(given->text[i]);
  }
  const request_source source = scpi_input_source(call->input);

  if ((int8_t)source != REQUEST_SOURCE_SCI) {
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
  status_report_from(STATUS_ILLEGAL_PARAMETER_VALUE, source);
  return 0;
}
