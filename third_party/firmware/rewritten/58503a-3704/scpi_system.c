/* scpi_system.c's functions that are this image's own: the rest are
 * common/scpi_system.c's, the same in every image (docs/firmware/rewrite.md,
 * "Shared C").  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype. */

/* The port a :COMMunicate:SERial node's path word names: SERial1,
 * DUART channel B, duartb_settings'; any other is SERial2, the SCI,
 * sci_settings'. */
enum {
  SCPI_SYSTEM_SERIAL1 = 0,
};

/* The rates :BAUD takes, each a serial_baud's (0x300f8, 0x30188). */
static const int32_t SCPI_SYSTEM_BAUD_1200 = 1200;

static const int32_t SCPI_SYSTEM_BAUD_2400 = 2400;

static const int32_t SCPI_SYSTEM_BAUD_9600 = 9600;

static const int32_t SCPI_SYSTEM_BAUD_19200 = 19200;

/* The data bits and stop bits the ports take. */
static const int32_t SCPI_SYSTEM_SEVEN_BITS = 7;

static const int32_t SCPI_SYSTEM_EIGHT_BITS = 8;

static const int32_t SCPI_SYSTEM_ONE_STOP_BIT = 1;

static const int32_t SCPI_SYSTEM_TWO_STOP_BITS = 2;

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

/* :COMMunicate:ALARm:RECeive:BAUD?, or its minimum or maximum: DUART
 * channel B's rate; another port's an undefined header, and a code out
 * of range answers nothing (0x30188, its jump table at 0x301dc). */
int32_t scpi_query_SYSTem_COMMunicate_ALARm_RECeive_BAUD(void* parameters,
                                                         void* response,
                                                         scpi_call* call) {
  scpi_system_port_number* given = parameters;
  int32_t* rate = response;

  if (given->number.kind == SCPI_VALUE_BOUND) {
    *rate = given->number.bound == SCPI_BOUND_MINIMUM ? SCPI_SYSTEM_BAUD_1200
                                                      : SCPI_SYSTEM_BAUD_19200;
    return 0;
  }
  if (given->port != SCPI_PORT_DUART_B) {
    return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
  }
  switch (duartb_settings.baud) {
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

/* :COMMunicate:SERial:RECeive:PARity:TYPE's choices, the node's list
 * (0x56a24). */
enum {
  SCPI_SYSTEM_PARITY_EVEN = 0,
  SCPI_SYSTEM_PARITY_NONE = 1,
  SCPI_SYSTEM_PARITY_ODD = 2,
  SCPI_SYSTEM_PARITY_ONE = 3,
};

/* The ALARm port's handlers below take its path word, a port number of
 * the port layer's; DUART channel B's, SCPI_PORT_DUART_B, alone has
 * settings, another port's being an undefined header. */

/* :COMMunicate?: SERial1, the first of its choices, for source 0, the
 * console's; else SERial2 (0x2fefe). */
int32_t scpi_query_SYSTem_COMMunicate(void* parameters, void* response,
                                      scpi_call* call) {
  static const uint16_t SERIAL2 = 1;
  uint16_t* choice = response;

  *choice = (int8_t)return_zero(call->input) == REQUEST_SOURCE_SCPI
                ? SCPI_SYSTEM_SERIAL1
                : SERIAL2;
  return 0;
}

/* :COMMunicate:ALARm?: 0 for DUART channel B (0x2ff26). */
int32_t scpi_query_SYSTem_COMMunicate_ALARm(void* parameters, void* response,
                                            scpi_call* call) {
  const int16_t* port = parameters;
  uint16_t* value = response;

  if (*port != SCPI_PORT_DUART_B) {
    return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
  }
  *value = 0;
  return 0;
}

/* :COMMunicate:ALARm:ADDRess?: not in this firmware (0x2ff4e). */
int32_t scpi_query_SYSTem_COMMunicate_ALARm_ADDRess(void* parameters,
                                                    void* response,
                                                    scpi_call* call) {
  return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
}

/* Sends a setting of DUART channel B to pllc, else reports an undefined
 * header. */
static void scpi_system_duartb_set(int16_t port, scpi_setting_param* setting,
                                   uint8_t value, const scpi_call* call) {
  if (port == SCPI_PORT_DUART_B) {
    pllc_request_a(setting, 0, value, return_zero(call->input));
  } else {
    status_report_from(STATUS_UNDEFINED_HEADER, return_zero(call->input));
  }
}

/* A setting of DUART channel B, sign-extended as the image reads its
 * byte, else an undefined header. */
static int32_t scpi_system_duartb_query(int16_t port, uint8_t setting,
                                        int16_t* value, scpi_call* call) {
  if (port != SCPI_PORT_DUART_B) {
    return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
  }
  *value = (int8_t)setting;
  return 0;
}

/* :COMMunicate:ALARm:FDUPlex: whether the port echoes what it receives
 * (0x2ff60). */
int32_t scpi_set_SYSTem_COMMunicate_ALARm_FDUPlex(void* parameters,
                                                  void* response,
                                                  scpi_call* call) {
  const scpi_system_port_word* given = parameters;

  scpi_system_duartb_set(given->port, &scpi_param_SYSTem_COMMunicate_FDUPlex,
                         given->value, call);
  return 0;
}

/* :COMMunicate:ALARm:FDUPlex? (0x2ffb8). */
int32_t scpi_query_SYSTem_COMMunicate_ALARm_FDUPlex(void* parameters,
                                                    void* response,
                                                    scpi_call* call) {
  const int16_t* port = parameters;

  return scpi_system_duartb_query(*port, duartb_settings.echo, response, call);
}

/* :COMMunicate:ALARm:CONTrol:HPACe, which duartb_settings.field_05
 * keeps; nothing else reads it (0x2ffe8). */
int32_t scpi_set_SYSTem_COMMunicate_ALARm_CONTrol_HPACe(void* parameters,
                                                        void* response,
                                                        scpi_call* call) {
  const scpi_system_port_word* given = parameters;

  scpi_system_duartb_set(given->port,
                         &scpi_param_SYSTem_COMMunicate_SER1_CONTrol_HPACe,
                         given->value, call);
  return 0;
}

/* :COMMunicate:ALARm:CONTrol:HPACe? (0x30040). */
int32_t scpi_query_SYSTem_COMMunicate_ALARm_CONTrol_HPACe(void* parameters,
                                                          void* response,
                                                          scpi_call* call) {
  const int16_t* port = parameters;

  return scpi_system_duartb_query(*port, duartb_settings.field_05, response,
                                  call);
}

/* :COMMunicate:ALARm:PRESet: requests the port's settings preset
 * (0x30070). */
int32_t scpi_set_SYSTem_COMMunicate_ALARm_PRESet(void* parameters,
                                                 void* response,
                                                 scpi_call* call) {
  const int16_t* port = parameters;

  if (*port == SCPI_PORT_DUART_B) {
    pllc_request_b(&scpi_action_SER2_PRESet, 0, return_zero(call->input));
  } else {
    status_report_from(STATUS_UNDEFINED_HEADER, return_zero(call->input));
  }
  return 0;
}

/* :COMMunicate:ALARm:PROMpt: whether the port prompts, the port layer's
 * (0x300be). */
int32_t scpi_set_SYSTem_COMMunicate_ALARm_PROMpt(void* parameters,
                                                 void* response,
                                                 scpi_call* call) {
  const scpi_system_port_word* given = parameters;

  port_prompt_set(given->port, given->value);
  return 0;
}

/* :COMMunicate:ALARm:PROMpt? (0x300d8). */
int32_t scpi_query_SYSTem_COMMunicate_ALARm_PROMpt(void* parameters,
                                                   void* response,
                                                   scpi_call* call) {
  const int16_t* port = parameters;
  uint16_t* state = response;

  *state = port_prompt_get(*port);
  return 0;
}

/* :COMMunicate:ALARm:RECeive:BAUD: the rate, a number taken up to the
 * next rate the port has, or MINimum or MAXimum (0x300f8). */
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

/* :COMMunicate:ALARm:RECeive:BITS: seven or eight data bits; another
 * port the engine's undefined header as its result (0x30206). */
int32_t scpi_set_SYSTem_COMMunicate_ALARm_RECeive_BITS(void* parameters,
                                                       void* response,
                                                       scpi_call* call) {
  const scpi_system_port_number* given = parameters;

  if (given->port != SCPI_PORT_DUART_B) {
    return STATUS_SCPI_UNDEFINED_HEADER;
  }
  uint8_t eight_bits = given->number.kind == SCPI_VALUE_BOUND
                           ? given->number.bound != SCPI_BOUND_MINIMUM
                           : given->number.value >= SCPI_SYSTEM_EIGHT_BITS;

  pllc_request_a(&scpi_param_SYSTem_COMMunicate_SER1_RECeive_BITS, 0,
                 eight_bits, return_zero(call->input));
  return 0;
}

/* :COMMunicate:ALARm:RECeive:BITS?, or its minimum or maximum; a code
 * out of range answers nothing (0x3025e). */
int32_t scpi_query_SYSTem_COMMunicate_ALARm_RECeive_BITS(void* parameters,
                                                         void* response,
                                                         scpi_call* call) {
  const scpi_system_port_number* given = parameters;
  int32_t* bits = response;

  if (given->port != SCPI_PORT_DUART_B) {
    return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
  }
  if (given->number.kind == SCPI_VALUE_BOUND) {
    *bits = given->number.bound == SCPI_BOUND_MINIMUM ? SCPI_SYSTEM_SEVEN_BITS
                                                      : SCPI_SYSTEM_EIGHT_BITS;
  } else if (duartb_settings.eight_bits == 0) {
    *bits = SCPI_SYSTEM_SEVEN_BITS;
  } else if (duartb_settings.eight_bits == 1) {
    *bits = SCPI_SYSTEM_EIGHT_BITS;
  }
  return 0;
}

/* :COMMunicate:ALARm:RECeive:PACE: XON or NONE, the choice's value
 * (0x302c0). */
int32_t scpi_set_SYSTem_COMMunicate_ALARm_RECeive_PACE(void* parameters,
                                                       void* response,
                                                       scpi_call* call) {
  const scpi_system_port_word* given = parameters;

  scpi_system_duartb_set(given->port,
                         &scpi_param_SYSTem_COMMunicate_SER1_RECeive_PACE,
                         given->value, call);
  return 0;
}

/* :COMMunicate:ALARm:RECeive:PACE? (0x30318). */
int32_t scpi_query_SYSTem_COMMunicate_ALARm_RECeive_PACE(void* parameters,
                                                         void* response,
                                                         scpi_call* call) {
  const int16_t* port = parameters;

  return scpi_system_duartb_query(*port, duartb_settings.xon_xoff, response,
                                  call);
}

/* :COMMunicate:ALARm:RECeive:PARity:TYPE: the port's parity by its
 * choice, ONE, mark parity, among them (0x30348).  A choice past the
 * list sends D2 as the caller left it (docs/firmware/quirks.md). */
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
 * nothing (0x303cc). */
int32_t scpi_query_SYSTem_COMMunicate_ALARm_RECeive_PARity_TYPE(
    void* parameters, void* response, scpi_call* call) {
  const int16_t* port = parameters;
  uint16_t* choice = response;

  if (*port != SCPI_PORT_DUART_B) {
    return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
  }
  switch (duartb_settings.parity) {
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

/* :COMMunicate:ALARm:RECeive:SBITs: one or two stop bits; another port
 * the engine's undefined header as its result (0x3042a). */
int32_t scpi_set_SYSTem_COMMunicate_ALARm_RECeive_SBITs(void* parameters,
                                                        void* response,
                                                        scpi_call* call) {
  const scpi_system_port_number* given = parameters;

  if (given->port != SCPI_PORT_DUART_B) {
    return STATUS_SCPI_UNDEFINED_HEADER;
  }
  uint8_t two_stop_bits =
      given->number.kind == SCPI_VALUE_BOUND
          ? given->number.bound != SCPI_BOUND_MINIMUM
          : given->number.value >= SCPI_SYSTEM_TWO_STOP_BITS;

  pllc_request_a(&scpi_param_SYSTem_COMMunicate_SER1_RECeive_SBITs, 0,
                 two_stop_bits, return_zero(call->input));
  return 0;
}

/* :COMMunicate:ALARm:RECeive:SBITs?, or its minimum or maximum; a code
 * out of range answers nothing (0x30482). */
int32_t scpi_query_SYSTem_COMMunicate_ALARm_RECeive_SBITs(void* parameters,
                                                          void* response,
                                                          scpi_call* call) {
  const scpi_system_port_number* given = parameters;
  int32_t* bits = response;

  if (given->port != SCPI_PORT_DUART_B) {
    return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
  }
  if (given->number.kind == SCPI_VALUE_BOUND) {
    *bits = given->number.bound == SCPI_BOUND_MINIMUM
                ? SCPI_SYSTEM_ONE_STOP_BIT
                : SCPI_SYSTEM_TWO_STOP_BITS;
  } else if (duartb_settings.two_stop_bits == 0) {
    *bits = SCPI_SYSTEM_ONE_STOP_BIT;
  } else if (duartb_settings.two_stop_bits == 1) {
    *bits = SCPI_SYSTEM_TWO_STOP_BITS;
  }
  return 0;
}

/* :LANGuage: from source 0, the console's, "PFORTH" or "INSTALL" ends
 * the SCPI session for the pForth console or the installer; another is
 * an illegal parameter value, "PRIMARY" too, which 097-59551-02 gives
 * it (5-140) and the Z3805A's takes.  The text is put in upper case in
 * place, its index a byte (0x30556). */
int32_t scpi_set_SYSTem_LANGuage(void* parameters, void* response,
                                 scpi_call* call) {
  scpi_text_parameter* given = parameters;

  for (int8_t i = 0; given->text[i] != 0; i++) {
    given->text[i] = (char)toupper(given->text[i]);
  }
  const request_source source = return_zero(call->input);

  if ((int8_t)source != REQUEST_SOURCE_SCPI) {
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

/* :LANGuage?: "PRIMARY" (0x3060c). */
int32_t scpi_query_SYSTem_LANGuage(void* parameters, void* response,
                                   scpi_call* call) {
  char** language = response;

  *language = "PRIMARY";
  return 0;
}

/* :PON: :SYSTem:PON's action, by pllc (0x3061a). */
int32_t scpi_set_SYSTem_PON(void* parameters, void* response, scpi_call* call) {
  pllc_request_b(&scpi_action_SYSTem_PON, 0, return_zero(call->input));
  return 0;
}

/* :PRINt?: requests scpi_param_SYSTem_PRINt with no value, answers 0, and
 * prints the status screen to the console (0x30666). */
int32_t scpi_query_SYSTem_PRINt(void* parameters, void* response,
                                scpi_call* call) {
  uint16_t* answer = response;

  pllc_request_c(&scpi_param_SYSTem_PRINt, 0, 0, 0, return_zero(call->input));
  *answer = 0;
  /* printf returns an int32_t, a long, and printf_function an int */
  status_screen_print(0, console_write, (printf_function*)printf);
  return 0;
}
