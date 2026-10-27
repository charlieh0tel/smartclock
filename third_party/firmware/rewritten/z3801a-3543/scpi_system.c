/* scpi_system.c's functions that are this image's own: the rest are
 * common/scpi_system.c's, the same in every image (docs/firmware/rewrite.md,
 * "Shared C").  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype. */

/* The port a :COMMunicate:SERial node's path word names: SERial1,
 * DUART channel B, duartb_settings'; the Z3805A's SERial2, its SCI, is
 * not in this image, whose SERial2 nodes share SERial1's handlers, and
 * which refuses any other port with -113, Undefined header. */
enum {
  SCPI_SYSTEM_SERIAL1 = 0,
};

/* The rates :BAUD takes, each a serial_baud's (0x2f130, 0x2f1be). */
static const int32_t SCPI_SYSTEM_BAUD_1200 = 1200;

static const int32_t SCPI_SYSTEM_BAUD_2400 = 2400;

static const int32_t SCPI_SYSTEM_BAUD_9600 = 9600;

static const int32_t SCPI_SYSTEM_BAUD_19200 = 19200;

/* The data bits and stop bits the port takes. */
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

/* A setter's refusal of a port not SERial1: -113, Undefined header,
 * reported for the call's source. */
static int32_t scpi_system_refuse_port(const scpi_call* call) {
  status_report_from(STATUS_UNDEFINED_HEADER, return_zero(call->input));
  return 0;
}

/* A query's refusal of a port not SERial1. */
static int32_t scpi_system_refuse_port_query(scpi_call* call) {
  return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
}

/* A SERial1 setting's request to pllc, the call's source. */
static int32_t scpi_system_request(scpi_setting_param* setting, uint8_t value,
                                   const scpi_call* call) {
  request_source source = return_zero(call->input);

  pllc_request_a(setting, 0, value, source);
  return 0;
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

/* :COMMunicate:SERial:FDUPlex: whether SERial1 echoes what it receives
 * (0x2f060). */
int32_t scpi_set_SYSTem_COMMunicate_SER1_FDUPlex(void* parameters,
                                                 void* response,
                                                 scpi_call* call) {
  scpi_system_port_word* given = parameters;

  if (given->port != SCPI_SYSTEM_SERIAL1) {
    return scpi_system_refuse_port(call);
  }
  return scpi_system_request(&scpi_param_SYSTem_COMMunicate_SER1_FDUPlex,
                             (uint8_t)given->value, call);
}

/* :COMMunicate:SERial:FDUPlex? (0x2f0b6). */
int32_t scpi_query_SYSTem_COMMunicate_SER1_FDUPlex(void* parameters,
                                                   void* response,
                                                   scpi_call* call) {
  int16_t* port = parameters;
  int16_t* state = response;

  if (*port != SCPI_SYSTEM_SERIAL1) {
    return scpi_system_refuse_port_query(call);
  }
  *state = (int8_t)duartb_settings.echo;
  return 0;
}

/* :COMMunicate:SERial:PRESet: requests SERial1's settings preset
 * (0x2f0e4). */
int32_t scpi_set_SYSTem_COMMunicate_SER1_PRESet(void* parameters,
                                                void* response,
                                                scpi_call* call) {
  int16_t* port = parameters;

  if (*port != SCPI_SYSTEM_SERIAL1) {
    return scpi_system_refuse_port(call);
  }
  pllc_request_b(&scpi_action_SER1_PRESet, 0, return_zero(call->input));
  return 0;
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

/* :COMMunicate:SERial:RECeive:BAUD: the rate, a number taken up to the
 * next rate the port has, or MINimum or MAXimum (0x2f130). */
int32_t scpi_set_SYSTem_COMMunicate_SER1_RECeive_BAUD(void* parameters,
                                                      void* response,
                                                      scpi_call* call) {
  scpi_system_port_number* given = parameters;
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
  if (given->port != SCPI_SYSTEM_SERIAL1) {
    return scpi_system_refuse_port(call);
  }
  return scpi_system_request(&scpi_param_SYSTem_COMMunicate_SER1_RECeive_BAUD,
                             baud, call);
}

/* :COMMunicate:SERial:RECeive:BAUD?, or its minimum or maximum, which
 * any port answers; a code out of range answers nothing (0x2f1be). */
int32_t scpi_query_SYSTem_COMMunicate_SER1_RECeive_BAUD(void* parameters,
                                                        void* response,
                                                        scpi_call* call) {
  scpi_system_port_number* given = parameters;
  int32_t* rate = response;

  if (given->number.kind == SCPI_VALUE_BOUND) {
    *rate = given->number.bound == SCPI_BOUND_MINIMUM ? SCPI_SYSTEM_BAUD_1200
                                                      : SCPI_SYSTEM_BAUD_19200;
    return 0;
  }
  if (given->port != SCPI_SYSTEM_SERIAL1) {
    return scpi_system_refuse_port_query(call);
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

/* :COMMunicate:SERial1:RECeive:BITS: seven or eight data bits.  Another
 * port returns D0 as the caller left it, where the Z3805A's returns
 * -113, Undefined header (docs/firmware/quirks.md) (0x2f23a). */
int32_t scpi_set_SYSTem_COMMunicate_SER1_RECeive_BITS(void* parameters,
                                                      void* response,
                                                      scpi_call* call) {
  int32_t callers = CPU_CALLERS_REGISTER(int32_t, "d0");
  scpi_system_port_number* given = parameters;

  if (given->port != SCPI_SYSTEM_SERIAL1) {
    return callers;
  }
  uint8_t eight_bits = given->number.kind == SCPI_VALUE_BOUND
                           ? given->number.bound != SCPI_BOUND_MINIMUM
                           : given->number.value >= SCPI_SYSTEM_EIGHT_BITS;

  return scpi_system_request(&scpi_param_SYSTem_COMMunicate_SER1_RECeive_BITS,
                             eight_bits, call);
}

/* :COMMunicate:SERial:RECeive:BITS?: SERial1's, or its minimum or
 * maximum; a code out of range answers nothing (0x2f28c). */
int32_t scpi_query_SYSTem_COMMunicate_SER1_RECeive_BITS(void* parameters,
                                                        void* response,
                                                        scpi_call* call) {
  scpi_system_port_number* given = parameters;
  int32_t* bits = response;

  if (given->port != SCPI_SYSTEM_SERIAL1) {
    return scpi_system_refuse_port_query(call);
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

/* :COMMunicate:SERial:RECeive:PACE: XON or NONE, the choice's value
 * (0x2f2ea). */
int32_t scpi_set_SYSTem_COMMunicate_SER1_RECeive_PACE(void* parameters,
                                                      void* response,
                                                      scpi_call* call) {
  scpi_system_port_word* given = parameters;

  if (given->port != SCPI_SYSTEM_SERIAL1) {
    return scpi_system_refuse_port(call);
  }
  return scpi_system_request(&scpi_param_SYSTem_COMMunicate_SER1_RECeive_PACE,
                             (uint8_t)given->value, call);
}

/* :COMMunicate:SERial:RECeive:PACE? (0x2f340). */
int32_t scpi_query_SYSTem_COMMunicate_SER1_RECeive_PACE(void* parameters,
                                                        void* response,
                                                        scpi_call* call) {
  int16_t* port = parameters;
  int16_t* choice = response;

  if (*port != SCPI_SYSTEM_SERIAL1) {
    return scpi_system_refuse_port_query(call);
  }
  *choice = (int8_t)duartb_settings.xon_xoff;
  return 0;
}

/* :COMMunicate:SERial:RECeive:PARity:TYPE's choices, the node's list. */
enum {
  SCPI_SYSTEM_PARITY_EVEN = 0,
  SCPI_SYSTEM_PARITY_NONE = 1,
  SCPI_SYSTEM_PARITY_ODD = 2,
  SCPI_SYSTEM_PARITY_ONE = 3,
};

/* :COMMunicate:SERial:RECeive:PARity:TYPE: SERial1's parity by its
 * choice; ONE, mark parity, refused with -224 (0x2f36e).  A choice past
 * the list sends D2 as the caller left it (docs/firmware/quirks.md). */
int32_t scpi_set_SYSTem_COMMunicate_SER1_RECeive_PARity_TYPE(void* parameters,
                                                             void* response,
                                                             scpi_call* call) {
  uint8_t parity = CPU_CALLERS_REGISTER(uint32_t, "d2");
  scpi_system_port_word* given = parameters;

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
      if (given->port == SCPI_SYSTEM_SERIAL1) {
        status_report_from(STATUS_ILLEGAL_PARAMETER_VALUE,
                           return_zero(call->input));
        return 0;
      }
      break;
  }
  if (given->port != SCPI_SYSTEM_SERIAL1) {
    return scpi_system_refuse_port(call);
  }
  return scpi_system_request(
      &scpi_param_SYSTem_COMMunicate_SER1_RECeive_PARity_TYPE, parity, call);
}

/* :COMMunicate:SERial:RECeive:PARity:TYPE?; a code out of range
 * answers nothing (0x2f3fc). */
int32_t scpi_query_SYSTem_COMMunicate_SER1_RECeive_PARity_TYPE(
    void* parameters, void* response, scpi_call* call) {
  int16_t* port = parameters;
  uint16_t* choice = response;

  if (*port != SCPI_SYSTEM_SERIAL1) {
    return scpi_system_refuse_port_query(call);
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

/* :COMMunicate:SERial1:RECeive:SBITs: one or two stop bits.  Another
 * port returns D0 as the caller left it, as :BITS does (0x2f458). */
int32_t scpi_set_SYSTem_COMMunicate_SER1_RECeive_SBITs(void* parameters,
                                                       void* response,
                                                       scpi_call* call) {
  int32_t callers = CPU_CALLERS_REGISTER(int32_t, "d0");
  scpi_system_port_number* given = parameters;

  if (given->port != SCPI_SYSTEM_SERIAL1) {
    return callers;
  }
  uint8_t two_stop_bits =
      given->number.kind == SCPI_VALUE_BOUND
          ? given->number.bound != SCPI_BOUND_MINIMUM
          : given->number.value >= SCPI_SYSTEM_TWO_STOP_BITS;

  return scpi_system_request(&scpi_param_SYSTem_COMMunicate_SER1_RECeive_SBITs,
                             two_stop_bits, call);
}

/* :COMMunicate:SERial:RECeive:SBITs?: SERial1's, or its minimum or
 * maximum; a code out of range answers nothing (0x2f4aa). */
int32_t scpi_query_SYSTem_COMMunicate_SER1_RECeive_SBITs(void* parameters,
                                                         void* response,
                                                         scpi_call* call) {
  scpi_system_port_number* given = parameters;
  int32_t* bits = response;

  if (given->port != SCPI_SYSTEM_SERIAL1) {
    return scpi_system_refuse_port_query(call);
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
