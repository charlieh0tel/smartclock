/* From the Z3816A 4001's rewritten/scpi_system.c, the 58503B 1.01.04's own
 * beside common/'s: of its functions, those the 58503B's code carries
 * identical or relocated (docs/firmware/58503b-1.01.04.md, "The link list").
 * Its comments' addresses are the 58503B's, but where they name another
 * image.  Each keeps its generated prototype. */

/* :COMMunicate:ALARm:PROMpt? (0x40138). */
int32_t scpi_query_SYSTem_COMMunicate_ALARm_PROMpt(void* parameters,
                                                   void* response,
                                                   scpi_call* call) {
  const int16_t* port = parameters;
  uint16_t* state = response;

  *state = port_prompt_get(*port);
  return 0;
}

/* :LANGuage?: "PRIMARY" (0x4056c). */
int32_t scpi_query_SYSTem_LANGuage(void* parameters, void* response,
                                   scpi_call* call) {
  char** language = response;

  *language = "PRIMARY";
  return 0;
}

/* :SYSTem:COMMunicate:ALARm:RECeive:BITS, which this image refuses as an
 * undefined header, as the Z3816A's does (0x40266). */
int32_t scpi_set_SYSTem_COMMunicate_ALARm_RECeive_BITS(void* parameters,
                                                       void* response,
                                                       scpi_call* call) {
  return STATUS_SCPI_UNDEFINED_HEADER;
}

/* :SYSTem:COMMunicate:ALARm:RECeive:SBITs, refused the same way
 * (0x40426). */
int32_t scpi_set_SYSTem_COMMunicate_ALARm_RECeive_SBITs(void* parameters,
                                                        void* response,
                                                        scpi_call* call) {
  return STATUS_SCPI_UNDEFINED_HEADER;
}

/* The rates :BAUD takes, each a serial_baud's (0x40158, 0x401e8). */
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
 * (0x400d0). */
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
 * (0x4011e). */
int32_t scpi_set_SYSTem_COMMunicate_ALARm_PROMpt(void* parameters,
                                                 void* response,
                                                 scpi_call* call) {
  const scpi_system_port_word* given = parameters;

  port_prompt_set(given->port, given->value);
  return 0;
}

/* :COMMunicate:ALARm:RECeive:BAUD: the rate, a number taken up to the
 * next rate the port has, or MINimum or MAXimum (0x40158). */
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
 * (0x402bc). */
int32_t scpi_set_SYSTem_COMMunicate_ALARm_RECeive_PACE(void* parameters,
                                                       void* response,
                                                       scpi_call* call) {
  const scpi_system_port_word* given = parameters;

  scpi_system_duartb_set(given->port,
                         &scpi_param_SYSTem_COMMunicate_SER1_RECeive_PACE,
                         given->value, call);
  return 0;
}

/* The written functions below are this image's own, each the 58503A
 * 3704's on the SCI's settings, sci_settings, and its port number,
 * SCPI_PORT_SCI, where the 58503A's are DUART channel B's, but where
 * a comment says otherwise. */

/* :COMMunicate?'s choices, SERial1 and SERial2. */
enum {
  SCPI_SYSTEM_SERIAL1 = 0,
  SCPI_SYSTEM_SERIAL2 = 1,
};

/* The rate :BAUD? gives past SCPI_SYSTEM_BAUD_9600. */
static const int32_t SCPI_SYSTEM_BAUD_19200 = 19200;

/* The data bits :BITS? gives and the stop bits :SBITs? gives. */
static const int32_t SCPI_SYSTEM_SEVEN_BITS = 7;

static const int32_t SCPI_SYSTEM_EIGHT_BITS = 8;

static const int32_t SCPI_SYSTEM_ONE_STOP_BIT = 1;

/* :COMMunicate:SERial:RECeive:PARity:TYPE's choices, the node's list. */
enum {
  SCPI_SYSTEM_PARITY_EVEN = 0,
  SCPI_SYSTEM_PARITY_NONE = 1,
  SCPI_SYSTEM_PARITY_ODD = 2,
  SCPI_SYSTEM_PARITY_ONE = 3,
};

/* A setting of the SCI's, sign-extended as the image reads its byte, else
 * an undefined header. */
static int32_t scpi_system_sci_query(int16_t port, uint8_t setting,
                                     int16_t* value, scpi_call* call) {
  if (port != SCPI_PORT_SCI) {
    return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
  }
  *value = (int8_t)setting;
  return 0;
}

/* :COMMunicate?: SERial1 for the SCI's source, the one session's; else
 * SERial2 (0x3ffc2).  The 58503A's gives SERial1 for source 0. */
int32_t scpi_query_SYSTem_COMMunicate(void* parameters, void* response,
                                      scpi_call* call) {
  uint16_t* choice = response;

  *choice = (int8_t)scpi_input_source(call->input) == REQUEST_SOURCE_SCI
                ? SCPI_SYSTEM_SERIAL1
                : SCPI_SYSTEM_SERIAL2;
  return 0;
}

/* :COMMunicate:ALARm?: 1 for the SCI, where the 58503A's gives 0 for
 * DUART channel B (0x3ffec). */
int32_t scpi_query_SYSTem_COMMunicate_ALARm(void* parameters, void* response,
                                            scpi_call* call) {
  static const uint16_t SCI = 1;
  const int16_t* port = parameters;
  uint16_t* value = response;

  if (*port != SCPI_PORT_SCI) {
    return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
  }
  *value = SCI;
  return 0;
}

/* :COMMunicate:ALARm:FDUPlex: whether the port echoes what it receives
 * (0x40016). */
int32_t scpi_set_SYSTem_COMMunicate_ALARm_FDUPlex(void* parameters,
                                                  void* response,
                                                  scpi_call* call) {
  const scpi_system_port_word* given = parameters;

  scpi_system_duartb_set(given->port, &scpi_param_SYSTem_COMMunicate_FDUPlex,
                         given->value, call);
  return 0;
}

/* :COMMunicate:ALARm:FDUPlex? (0x4006e). */
int32_t scpi_query_SYSTem_COMMunicate_ALARm_FDUPlex(void* parameters,
                                                    void* response,
                                                    scpi_call* call) {
  const int16_t* port = parameters;

  return scpi_system_sci_query(*port, sci_settings.echo, response, call);
}

/* :COMMunicate:ALARm:CONTrol:HPACe: an undefined header for every port,
 * the SCI keeping no such setting, where the 58503A's keeps DUART
 * channel B's (0x4009e). */
int32_t scpi_set_SYSTem_COMMunicate_ALARm_CONTrol_HPACe(void* parameters,
                                                        void* response,
                                                        scpi_call* call) {
  status_report_from(STATUS_UNDEFINED_HEADER, scpi_input_source(call->input));
  return 0;
}

/* :COMMunicate:ALARm:CONTrol:HPACe?, the same (0x400be). */
int32_t scpi_query_SYSTem_COMMunicate_ALARm_CONTrol_HPACe(void* parameters,
                                                          void* response,
                                                          scpi_call* call) {
  return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
}

/* :COMMunicate:ALARm:RECeive:BAUD?, or its minimum or maximum: the SCI's
 * rate; another port's an undefined header, and a code out of range
 * answers nothing (0x401e8, its jump table at 0x4023c). */
int32_t scpi_query_SYSTem_COMMunicate_ALARm_RECeive_BAUD(void* parameters,
                                                         void* response,
                                                         scpi_call* call) {
  const scpi_system_port_number* given = parameters;
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

/* :COMMunicate:ALARm:RECeive:BITS?: eight data bits with no parity,
 * seven with any other, as the SCI's parity carries them; a parity out
 * of range answers nothing.  It takes no MIN or MAX, where the 58503A's
 * gives them and reads DUART channel B's data bits (0x4026a, its jump
 * table at 0x4028e). */
int32_t scpi_query_SYSTem_COMMunicate_ALARm_RECeive_BITS(void* parameters,
                                                         void* response,
                                                         scpi_call* call) {
  const int16_t* port = parameters;
  int32_t* bits = response;

  if (*port != SCPI_PORT_SCI) {
    return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
  }
  switch (sci_settings.parity) {
    case SERIAL_PARITY_NONE:
      *bits = SCPI_SYSTEM_EIGHT_BITS;
      break;
    case SERIAL_PARITY_EVEN:
    case SERIAL_PARITY_ODD:
    case SERIAL_PARITY_ONE:
      *bits = SCPI_SYSTEM_SEVEN_BITS;
      break;
    default:
      break;
  }
  return 0;
}

/* :COMMunicate:ALARm:RECeive:PACE? (0x40314). */
int32_t scpi_query_SYSTem_COMMunicate_ALARm_RECeive_PACE(void* parameters,
                                                         void* response,
                                                         scpi_call* call) {
  const int16_t* port = parameters;

  return scpi_system_sci_query(*port, sci_settings.xon_xoff, response, call);
}

/* :COMMunicate:ALARm:RECeive:PARity:TYPE: the port's parity by its
 * choice, ONE, mark parity, among them (0x40344, its jump table at
 * 0x40360).  A choice past the list sends D2 as the caller left it
 * (docs/firmware/quirks.md). */
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
 * nothing (0x403c8, its jump table at 0x40406). */
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

/* :COMMunicate:ALARm:RECeive:SBITs?: one stop bit for the SCI, as the
 * Z3816A 4001's (0x4042a). */
int32_t scpi_query_SYSTem_COMMunicate_ALARm_RECeive_SBITs(void* parameters,
                                                          void* response,
                                                          scpi_call* call) {
  const int16_t* port = parameters;
  int32_t* bits = response;

  if (*port != SCPI_PORT_SCI) {
    return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
  }
  *bits = SCPI_SYSTEM_ONE_STOP_BIT;
  return 0;
}

/* :LANGuage: from the SCI's source, the one session's, "PFORTH" or
 * "INSTALL" ends the SCPI session for the pForth console or the
 * installer; another is an illegal parameter value, "PRIMARY" too.  The
 * text is put in upper case in place, its index a byte (0x404b2).  The
 * 58503A's takes the two from source 0. */
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

/* :PON: :SYSTem:PON's action, by pllc (0x4057a). */
int32_t scpi_set_SYSTem_PON(void* parameters, void* response, scpi_call* call) {
  pllc_request_b(&scpi_action_SYSTem_PON, 0, scpi_input_source(call->input));
  return 0;
}

/* :PRINt?: requests scpi_param_SYSTem_PRINt with no value, answers 0, and
 * prints the status screen of status_print_snapshot to the console, where
 * the 58503A's passes 0 (0x405fa). */
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
