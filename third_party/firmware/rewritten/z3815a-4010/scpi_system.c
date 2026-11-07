/* scpi_system.c's functions that are this image's own: the rest are
 * common/scpi_system.c's, the same in every image (docs/firmware/rewrite.md,
 * "Shared C").  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype. */

/* :PRINt?: requests scpi_param_SYSTem_PRINt with no value, its source
 * scpi_input_source's, answers 0, and prints the status screen: for a query
 * from scpi_console_port's object, scpi_input_source's 0, status_screen_
 * snapshots[0] to channel B, through duartb_port_write and duartb_printf;
 * for any other, 1, the second to device 0, through console_write and
 * printf (0x44a7a). */
int32_t scpi_query_SYSTem_PRINt(void* parameters, void* response,
                                scpi_call* call) {
  uint16_t* answer = response;

  pllc_request_c(&scpi_param_SYSTem_PRINt, 0, 0, 0,
                 scpi_input_source(call->input));
  *answer = 0;
  if (scpi_input_source(call->input) == 0) {
    status_screen_print(0, (write_function*)duartb_port_write,
                        (printf_function*)duartb_printf);
  } else {
    /* printf returns an int32_t, a long, and printf_function an int */
    status_screen_print(1, (write_function*)console_write,
                        (printf_function*)printf);
  }
  return 0;
}

/* A port's boolean or choice parameter, as the 58503A's. */
typedef struct scpi_system_port_word {
  int16_t port;   /* +0x00 */
  int16_t kind;   /* +0x02 */
  uint16_t value; /* +0x04 */
} scpi_system_port_word;

/* :COMMunicate:ALARm:PROMpt: whether the port prompts, the port layer's
 * (0x443c2). */
int32_t scpi_set_SYSTem_COMMunicate_ALARm_PROMpt(void* parameters,
                                                 void* response,
                                                 scpi_call* call) {
  const scpi_system_port_word* given = parameters;

  port_prompt_set(given->port, given->value);
  return 0;
}

/* :COMMunicate:ALARm:PROMpt? (0x443dc). */
int32_t scpi_query_SYSTem_COMMunicate_ALARm_PROMpt(void* parameters,
                                                   void* response,
                                                   scpi_call* call) {
  const int16_t* port = parameters;
  uint16_t* state = response;

  *state = port_prompt_get(*port);
  return 0;
}

/* scpi_system.c's functions that are this image's own: the rest are
 * common/scpi_system.c's, the same in every image (docs/firmware/rewrite.md,
 * "Shared C").  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype. */

/* :COMMunicate?'s choices, which :COMMunicate:ALARm? gives for a port:
 * SERial1, DUART channel B, duartb_settings', and SERial2, the SCI,
 * sci_settings'.  A :COMMunicate node's path word is the port layer's
 * number instead, SCPI_PORT_DUART_B or SCPI_PORT_SCI (scpi.h). */
enum {
  SCPI_SYSTEM_SERIAL1 = 0,
  SCPI_SYSTEM_SERIAL2 = 1,
};

static const int32_t SCPI_SYSTEM_SEVEN_BITS = 7;

static const int32_t SCPI_SYSTEM_EIGHT_BITS = 8;

static const int32_t SCPI_SYSTEM_TWO_STOP_BITS = 2;

/* A port's number, or a query's optional MINimum or MAXimum. */
typedef struct scpi_system_port_number {
  int16_t port;                      /* +0x00 */
  scpi_engine_long_parameter number; /* +0x02 */
} scpi_system_port_number;

/* The ALARm port's handlers take its path word, a port number of the
 * port layer's.  Those below, to :PON, set or give a setting of DUART
 * channel B's, SCPI_PORT_DUART_B, alone, another port's being an
 * undefined header; those after :PON take either port. */

/* :COMMunicate?: SERial1, the first of its choices, for source 0, the
 * console's; else SERial2 (0x4418e). */
int32_t scpi_query_SYSTem_COMMunicate(void* parameters, void* response,
                                      scpi_call* call) {
  uint16_t* choice = response;

  *choice = (int8_t)scpi_input_source(call->input) == REQUEST_SOURCE_SCPI
                ? SCPI_SYSTEM_SERIAL1
                : SCPI_SYSTEM_SERIAL2;
  return 0;
}

/* :COMMunicate:ALARm:ADDRess?: not in this firmware (0x441f2). */
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
    pllc_request_a(setting, 0, value, scpi_input_source(call->input));
  } else {
    status_report_from(STATUS_UNDEFINED_HEADER, scpi_input_source(call->input));
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

/* :COMMunicate:ALARm:CONTrol:HPACe, which duartb_settings.field_05
 * keeps; nothing else reads it (0x442c8). */
int32_t scpi_set_SYSTem_COMMunicate_ALARm_CONTrol_HPACe(void* parameters,
                                                        void* response,
                                                        scpi_call* call) {
  const scpi_system_port_word* given = parameters;

  scpi_system_duartb_set(given->port,
                         &scpi_param_SYSTem_COMMunicate_SER1_CONTrol_HPACe,
                         given->value, call);
  return 0;
}

/* :COMMunicate:ALARm:CONTrol:HPACe? (0x44320). */
int32_t scpi_query_SYSTem_COMMunicate_ALARm_CONTrol_HPACe(void* parameters,
                                                          void* response,
                                                          scpi_call* call) {
  const int16_t* port = parameters;

  return scpi_system_duartb_query(*port, duartb_settings.field_05, response,
                                  call);
}

/* :COMMunicate:ALARm:RECeive:BITS: seven or eight data bits; another
 * port the engine's undefined header as its result (0x4453a). */
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
                 eight_bits, scpi_input_source(call->input));
  return 0;
}

/* :COMMunicate:ALARm:RECeive:SBITs: one or two stop bits; another port
 * the engine's undefined header as its result (0x447f8). */
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
                 two_stop_bits, scpi_input_source(call->input));
  return 0;
}

/* :PON: :SYSTem:PON's action, by pllc (0x44a2e). */
int32_t scpi_set_SYSTem_PON(void* parameters, void* response, scpi_call* call) {
  pllc_request_b(&scpi_action_SYSTem_PON, 0, scpi_input_source(call->input));
  return 0;
}

/* The ALARm port's handlers below that take either port: DUART channel
 * B's, SCPI_PORT_DUART_B, duartb_settings', or the SCI's, SCPI_PORT_SCI,
 * sci_settings', where the 58503A's take DUART channel B's alone. */

/* A setting of the port's, sign-extended as the image reads its byte,
 * the other port's left unread; else an undefined header. */
static int32_t scpi_system_port_query(int16_t port,
                                      const uint8_t* duartb_setting,
                                      const uint8_t* sci_setting,
                                      int16_t* value, scpi_call* call) {
  if (port == SCPI_PORT_DUART_B) {
    *value = (int8_t)*duartb_setting;
  } else if (port == SCPI_PORT_SCI) {
    *value = (int8_t)*sci_setting;
  } else {
    return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
  }
  return 0;
}

/* :COMMunicate:ALARm?: the port's :COMMunicate? choice, SERial1 for
 * DUART channel B, SERial2 for the SCI (0x441b6). */
int32_t scpi_query_SYSTem_COMMunicate_ALARm(void* parameters, void* response,
                                            scpi_call* call) {
  const int16_t* port = parameters;
  uint16_t* choice = response;

  if (*port == SCPI_PORT_DUART_B) {
    *choice = SCPI_SYSTEM_SERIAL1;
  } else if (*port == SCPI_PORT_SCI) {
    *choice = SCPI_SYSTEM_SERIAL2;
  } else {
    return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
  }
  return 0;
}

/* :COMMunicate:ALARm:FDUPlex? (0x4427e). */
int32_t scpi_query_SYSTem_COMMunicate_ALARm_FDUPlex(void* parameters,
                                                    void* response,
                                                    scpi_call* call) {
  const int16_t* port = parameters;

  return scpi_system_port_query(*port, &duartb_settings.echo,
                                &sci_settings.echo, response, call);
}

/* :COMMunicate:ALARm:RECeive:PACE? (0x446a4). */
int32_t scpi_query_SYSTem_COMMunicate_ALARm_RECeive_PACE(void* parameters,
                                                         void* response,
                                                         scpi_call* call) {
  const int16_t* port = parameters;

  return scpi_system_port_query(*port, &duartb_settings.xon_xoff,
                                &sci_settings.xon_xoff, response, call);
}

/* The rates :BAUD takes, each a serial_baud's (0x443fc, 0x444ae). */
static const int32_t SCPI_SYSTEM_BAUD_1200 = 1200;

static const int32_t SCPI_SYSTEM_BAUD_2400 = 2400;

static const int32_t SCPI_SYSTEM_BAUD_9600 = 9600;

static const int32_t SCPI_SYSTEM_BAUD_19200 = 19200;

static const int32_t SCPI_SYSTEM_ONE_STOP_BIT = 1;

/* :COMMunicate:ALARm:RECeive:BAUD?, or its minimum or maximum, either
 * port's; a code out of range answers nothing (0x444ae, its jump table
 * at 0x44510). */
int32_t scpi_query_SYSTem_COMMunicate_ALARm_RECeive_BAUD(void* parameters,
                                                         void* response,
                                                         scpi_call* call) {
  const scpi_system_port_number* given = parameters;
  int32_t* rate = response;
  serial_baud baud;

  if (given->number.kind == SCPI_VALUE_BOUND) {
    *rate = given->number.bound == SCPI_BOUND_MINIMUM ? SCPI_SYSTEM_BAUD_1200
                                                      : SCPI_SYSTEM_BAUD_19200;
    return 0;
  }
  if (given->port == SCPI_PORT_DUART_B) {
    baud = duartb_settings.baud;
  } else if (given->port == SCPI_PORT_SCI) {
    baud = sci_settings.baud;
  } else {
    return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
  }
  switch (baud) {
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

/* :COMMunicate:SERial:RECeive:PARity:TYPE's choices, the node's list. */
enum {
  SCPI_SYSTEM_PARITY_EVEN = 0,
  SCPI_SYSTEM_PARITY_NONE = 1,
  SCPI_SYSTEM_PARITY_ODD = 2,
  SCPI_SYSTEM_PARITY_ONE = 3,
};

/* :COMMunicate:ALARm:RECeive:PARity:TYPE?, either port's; a code out of
 * range answers nothing (0x4478c, its jump table at 0x447d8). */
int32_t scpi_query_SYSTem_COMMunicate_ALARm_RECeive_PARity_TYPE(
    void* parameters, void* response, scpi_call* call) {
  const int16_t* port = parameters;
  uint16_t* choice = response;
  serial_parity parity;

  if (*port == SCPI_PORT_DUART_B) {
    parity = duartb_settings.parity;
  } else if (*port == SCPI_PORT_SCI) {
    parity = sci_settings.parity;
  } else {
    return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
  }
  switch (parity) {
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

/* :COMMunicate:ALARm:RECeive:BITS?, or its minimum or maximum: DUART
 * channel B's data bits; the SCI's, which has none of its own, seven
 * with parity and eight without, whatever the bound; a code out of range
 * answers nothing (0x44592, its jump tables at 0x445d2 and 0x44604). */
int32_t scpi_query_SYSTem_COMMunicate_ALARm_RECeive_BITS(void* parameters,
                                                         void* response,
                                                         scpi_call* call) {
  const scpi_system_port_number* given = parameters;
  int32_t* bits = response;

  if (given->port == SCPI_PORT_SCI) {
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
  } else if (given->port != SCPI_PORT_DUART_B) {
    return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
  } else if (given->number.kind == SCPI_VALUE_BOUND) {
    *bits = given->number.bound == SCPI_BOUND_MINIMUM ? SCPI_SYSTEM_SEVEN_BITS
                                                      : SCPI_SYSTEM_EIGHT_BITS;
  } else if (duartb_settings.eight_bits == 0) {
    *bits = SCPI_SYSTEM_SEVEN_BITS;
  } else if (duartb_settings.eight_bits == 1) {
    *bits = SCPI_SYSTEM_EIGHT_BITS;
  }
  return 0;
}

/* :COMMunicate:ALARm:RECeive:SBITs?, or its minimum or maximum: DUART
 * channel B's stop bits; the SCI's one, whatever the bound; a code out
 * of range answers nothing (0x44850, its jump table at 0x44890). */
int32_t scpi_query_SYSTem_COMMunicate_ALARm_RECeive_SBITs(void* parameters,
                                                          void* response,
                                                          scpi_call* call) {
  const scpi_system_port_number* given = parameters;
  int32_t* bits = response;

  if (given->port == SCPI_PORT_SCI) {
    *bits = SCPI_SYSTEM_ONE_STOP_BIT;
  } else if (given->port != SCPI_PORT_DUART_B) {
    return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
  } else if (given->number.kind == SCPI_VALUE_BOUND) {
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

/* :LANGuage: from the SCI, the console, "PFORTH" or "INSTALL" ends the
 * SCPI session for the pForth console or the installer; another is an
 * illegal parameter value, "PRIMARY" too.  From source 0, DUART channel
 * B's, an illegal parameter value whatever the text, where the 58503A's
 * takes the two there.  The text is put in upper case in place, its
 * index a byte (0x44932). */
int32_t scpi_set_SYSTem_LANGuage(void* parameters, void* response,
                                 scpi_call* call) {
  scpi_text_parameter* given = parameters;

  for (int8_t i = 0; given->text[i] != 0; i++) {
    given->text[i] = (char)toupper(given->text[i]);
  }
  const request_source source = scpi_input_source(call->input);

  if ((int8_t)source == REQUEST_SOURCE_SCPI) {
    status_report_from(STATUS_ILLEGAL_PARAMETER_VALUE, source);
  }
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

/* Sends a setting of the port's to pllc, DUART channel B's or the SCI's
 * descriptor, else reports an undefined header. */
static void scpi_system_port_set(int16_t port, scpi_setting_param* duartb,
                                 scpi_setting_param* sci, uint8_t value,
                                 const scpi_call* call) {
  if (port == SCPI_PORT_DUART_B) {
    pllc_request_a(duartb, 0, value, scpi_input_source(call->input));
  } else if (port == SCPI_PORT_SCI) {
    pllc_request_a(sci, 0, value, scpi_input_source(call->input));
  } else {
    status_report_from(STATUS_UNDEFINED_HEADER, scpi_input_source(call->input));
  }
}

/* :COMMunicate:ALARm:RECeive:BAUD: either port's rate, a number taken up
 * to the next rate the port has, or MINimum or MAXimum (0x443fc). */
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
  scpi_system_port_set(
      given->port, &scpi_param_SYSTem_COMMunicate_SER1_RECeive_BAUD,
      &scpi_param_SYSTem_COMMunicate_SER2_RECeive_BAUD, baud, call);
  return 0;
}

/* :COMMunicate:ALARm:RECeive:PARity:TYPE: either port's parity, the
 * choice's, ONE, mark parity, for DUART channel B too, where the
 * Z3805A's refuses it for SERial1 (0x446ea, its jump table at
 * 0x44712).  A choice past the list sends D2 as the caller left it
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
  scpi_system_port_set(
      given->port, &scpi_param_SYSTem_COMMunicate_SER1_RECeive_PARity_TYPE,
      &scpi_param_SYSTem_COMMunicate_SER2_RECeive_PARity_TYPE, parity, call);
  return 0;
}

/* :COMMunicate:ALARm:FDUPlex: either port's echo, where the 58503A's
 * takes DUART channel B's alone (0x44204). */
int32_t scpi_set_SYSTem_COMMunicate_ALARm_FDUPlex(void* parameters,
                                                  void* response,
                                                  scpi_call* call) {
  const scpi_system_port_word* given = parameters;

  scpi_system_port_set(given->port, &scpi_param_SYSTem_COMMunicate_FDUPlex,
                       &scpi_param_SYSTem_COMMunicate_SER2_FDUPlex,
                       given->value, call);
  return 0;
}

/* :COMMunicate:ALARm:RECeive:PACE: either port's XON/XOFF, where the
 * 58503A's takes DUART channel B's alone (0x4462a). */
int32_t scpi_set_SYSTem_COMMunicate_ALARm_RECeive_PACE(void* parameters,
                                                       void* response,
                                                       scpi_call* call) {
  const scpi_system_port_word* given = parameters;

  scpi_system_port_set(
      given->port, &scpi_param_SYSTem_COMMunicate_SER1_RECeive_PACE,
      &scpi_param_SYSTem_COMMunicate_SER2_RECeive_PACE, given->value, call);
  return 0;
}

/* :COMMunicate:ALARm:PRESet: requests either port's settings preset,
 * DUART channel B's through scpi_action_SER1_PRESet, whose action is
 * duartb_settings_preset, and the SCI's through scpi_action_SER2_PRESet,
 * sci_settings_preset's; where the 58503A's presets DUART channel B's
 * alone (0x44350). */
int32_t scpi_set_SYSTem_COMMunicate_ALARm_PRESet(void* parameters,
                                                 void* response,
                                                 scpi_call* call) {
  const int16_t* port = parameters;

  if (*port == SCPI_PORT_DUART_B) {
    pllc_request_b(&scpi_action_SER1_PRESet, 0, scpi_input_source(call->input));
  } else if (*port == SCPI_PORT_SCI) {
    pllc_request_b(&scpi_action_SER2_PRESet, 0, scpi_input_source(call->input));
  } else {
    status_report_from(STATUS_UNDEFINED_HEADER, scpi_input_source(call->input));
  }
  return 0;
}

/* :LANGuage?: "PRIMARY" to the SCI's session, the console, which takes
 * :LANGuage, and "SECONDARY" to DUART channel B's, where the Z3805A's
 * answers the console alone (0x449f8). */
int32_t scpi_query_SYSTem_LANGuage(void* parameters, void* response,
                                   scpi_call* call) {
  char** language = response;

  if ((int8_t)scpi_input_source(call->input) == REQUEST_SOURCE_SCI) {
    *language = "PRIMARY";
  } else {
    *language = "SECONDARY";
  }
  return 0;
}
