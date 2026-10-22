/* The SCPI :SYSTem tree (docs/scpi/README.md): its handlers, and the
 * hooks and actions pllc runs for its settings, written by hand in place
 * of the decompiler's (docs/firmware/README.md, "Hand-written C";
 * docs/firmware/modules.md, `applications/scpi/handlers/system`).  The
 * exported C includes this file after its own, so the types and
 * prototypes it declares are in scope; each function here keeps its
 * generated prototype.
 *
 * A :COMMunicate:SERial handler's parameters start with the port, the
 * word its SERial node adds to the path, then each parameter's kind
 * word and its value; a number's, or a query's optional MINimum or
 * MAXimum, is scpi_engine_long_parameter's. */

/* The port a :COMMunicate:SERial node's path word names: SERial1,
 * DUART channel B, duartb_settings'; any other is SERial2, the SCI,
 * sci_settings'. */
enum {
  SCPI_SYSTEM_SERIAL1 = 0,
};

/* The rates :BAUD takes, each a serial_baud's (0x3022e, 0x302c4). */
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

/* A string parameter: its kind word, then its text. */
typedef struct scpi_system_string_parameter {
  int16_t kind; /* +0x00 */
  char* text;   /* +0x02 */
} scpi_system_string_parameter;

/* Whether the call came in on the console port. */
static bool scpi_system_from_console(const scpi_call* call) {
  return scpi_console_port() == call->input;
}

/* :COMMunicate?: the console port's, SERial1, the first of its choices;
 * from another port nothing (0x3012e). */
int32_t scpi_query_SYSTem_COMMunicate(void* parameters, void* response,
                                      scpi_call* call) {
  uint16_t* choice = response;

  if (scpi_system_from_console(call)) {
    *choice = SCPI_SYSTEM_SERIAL1;
  }
  return 0;
}

/* :COMMunicate:SERial:ADDRess?: not in this firmware (0x30148). */
int32_t scpi_query_SYSTem_COMMunicate_SER1_ADDRess(void* parameters,
                                                   void* response,
                                                   scpi_call* call) {
  return scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
}

/* :COMMunicate:SERial:FDUPlex: whether the port echoes what it receives
 * (0x3015a). */
int32_t scpi_set_SYSTem_COMMunicate_SER1_FDUPlex(void* parameters,
                                                 void* response,
                                                 scpi_call* call) {
  scpi_system_port_word* given = parameters;
  scpi_setting_param* setting =
      given->port == SCPI_SYSTEM_SERIAL1
          ? &scpi_param_SYSTem_COMMunicate_SER1_FDUPlex
          : &scpi_param_SYSTem_COMMunicate_SER2_FDUPlex;
  uint8_t source = return_zero(call->input);

  pllc_request_a(setting, 0, (uint8_t)given->value, source);
  return 0;
}

/* :COMMunicate:SERial:FDUPlex? (0x301bc). */
int32_t scpi_query_SYSTem_COMMunicate_SER1_FDUPlex(void* parameters,
                                                   void* response,
                                                   scpi_call* call) {
  int16_t* port = parameters;
  int16_t* state = response;
  uint8_t echo =
      *port == SCPI_SYSTEM_SERIAL1 ? duartb_settings.echo : sci_settings.echo;

  *state = (int8_t)echo;
  return 0;
}

/* :COMMunicate:SERial:PRESet: requests the port's settings preset
 * (0x301de). */
int32_t scpi_set_SYSTem_COMMunicate_SER1_PRESet(void* parameters,
                                                void* response,
                                                scpi_call* call) {
  int16_t* port = parameters;
  scpi_action_param* action = *port == SCPI_SYSTEM_SERIAL1
                                  ? &scpi_action_SER1_PRESet
                                  : &scpi_action_SER2_PRESet;
  uint8_t source = return_zero(call->input);

  pllc_request_b(action, 0, source);
  return 0;
}

/* :COMMunicate:SERial1:PROMpt: whether the console prompts; SERial2's
 * is ignored (0x2b96c). */
int32_t scpi_set_SYSTem_COMMunicate_SER1_PROMpt(void* parameters,
                                                void* response,
                                                scpi_call* call) {
  scpi_system_port_word* given = parameters;

  if (given->port == SCPI_SYSTEM_SERIAL1) {
    scpi_prompt_enabled = (uint8_t)given->value;
  }
  return 0;
}

/* :COMMunicate:SERial1:PROMpt?; SERial2's answers nothing (0x2b982). */
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
 * next rate the port has, or MINimum or MAXimum (0x3022e). */
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
  scpi_setting_param* setting =
      given->port == SCPI_SYSTEM_SERIAL1
          ? &scpi_param_SYSTem_COMMunicate_SER1_RECeive_BAUD
          : &scpi_param_SYSTem_COMMunicate_SER2_RECeive_BAUD;
  uint8_t source = return_zero(call->input);

  pllc_request_a(setting, 0, baud, source);
  return 0;
}

/* :COMMunicate:SERial:RECeive:BAUD?, or its minimum or maximum; a code
 * out of range answers nothing (0x302c4). */
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
  serial_baud baud = given->port == SCPI_SYSTEM_SERIAL1 ? duartb_settings.baud
                                                        : sci_settings.baud;

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

/* :COMMunicate:SERial1:RECeive:BITS: seven or eight data bits;
 * SERial2's is an undefined header, its bits going with its parity
 * (0x30332). */
int32_t scpi_set_SYSTem_COMMunicate_SER1_RECeive_BITS(void* parameters,
                                                      void* response,
                                                      scpi_call* call) {
  scpi_system_port_number* given = parameters;

  if (given->port != SCPI_SYSTEM_SERIAL1) {
    return STATUS_SCPI_UNDEFINED_HEADER;
  }
  uint8_t eight_bits = given->number.kind == SCPI_VALUE_BOUND
                           ? given->number.bound != SCPI_BOUND_MINIMUM
                           : given->number.value >= SCPI_SYSTEM_EIGHT_BITS;
  uint8_t source = return_zero(call->input);

  pllc_request_a(&scpi_param_SYSTem_COMMunicate_SER1_RECeive_BITS, 0,
                 eight_bits, source);
  return 0;
}

/* :COMMunicate:SERial:RECeive:BITS?: SERial1's, or its minimum or
 * maximum; SERial2's by its parity, seven with one; a code out of range
 * answers nothing (0x30388). */
int32_t scpi_query_SYSTem_COMMunicate_SER1_RECeive_BITS(void* parameters,
                                                        void* response,
                                                        scpi_call* call) {
  scpi_system_port_number* given = parameters;
  int32_t* bits = response;

  if (given->port != SCPI_SYSTEM_SERIAL1) {
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

/* :COMMunicate:SERial:RECeive:PACE: XON or NONE, the choice's value
 * (0x303fe). */
int32_t scpi_set_SYSTem_COMMunicate_SER1_RECeive_PACE(void* parameters,
                                                      void* response,
                                                      scpi_call* call) {
  scpi_system_port_word* given = parameters;
  scpi_setting_param* setting =
      given->port == SCPI_SYSTEM_SERIAL1
          ? &scpi_param_SYSTem_COMMunicate_SER1_RECeive_PACE
          : &scpi_param_SYSTem_COMMunicate_SER2_RECeive_PACE;
  uint8_t source = return_zero(call->input);

  pllc_request_a(setting, 0, (uint8_t)given->value, source);
  return 0;
}

/* :COMMunicate:SERial:RECeive:PACE? (0x30460). */
int32_t scpi_query_SYSTem_COMMunicate_SER1_RECeive_PACE(void* parameters,
                                                        void* response,
                                                        scpi_call* call) {
  int16_t* port = parameters;
  int16_t* choice = response;
  uint8_t xon_xoff = *port == SCPI_SYSTEM_SERIAL1 ? duartb_settings.xon_xoff
                                                  : sci_settings.xon_xoff;

  *choice = (int8_t)xon_xoff;
  return 0;
}

/* :COMMunicate:SERial:RECeive:PARity:TYPE's choices, the node's list
 * (0x53ab0). */
enum {
  SCPI_SYSTEM_PARITY_EVEN = 0,
  SCPI_SYSTEM_PARITY_NONE = 1,
  SCPI_SYSTEM_PARITY_ODD = 2,
  SCPI_SYSTEM_PARITY_ONE = 3,
};

/* :COMMunicate:SERial:RECeive:PARity:TYPE?; a code out of range
 * answers nothing (0x3051a). */
int32_t scpi_query_SYSTem_COMMunicate_SER1_RECeive_PARity_TYPE(
    void* parameters, void* response, scpi_call* call) {
  int16_t* port = parameters;
  uint16_t* choice = response;
  serial_parity parity = *port == SCPI_SYSTEM_SERIAL1 ? duartb_settings.parity
                                                      : sci_settings.parity;

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

/* :COMMunicate:SERial1:RECeive:SBITs: one or two stop bits; SERial2's
 * is an undefined header, the SCI having one (0x30568). */
int32_t scpi_set_SYSTem_COMMunicate_SER1_RECeive_SBITs(void* parameters,
                                                       void* response,
                                                       scpi_call* call) {
  scpi_system_port_number* given = parameters;

  if (given->port != SCPI_SYSTEM_SERIAL1) {
    return STATUS_SCPI_UNDEFINED_HEADER;
  }
  uint8_t two_stop_bits =
      given->number.kind == SCPI_VALUE_BOUND
          ? given->number.bound != SCPI_BOUND_MINIMUM
          : given->number.value >= SCPI_SYSTEM_TWO_STOP_BITS;
  uint8_t source = return_zero(call->input);

  pllc_request_a(&scpi_param_SYSTem_COMMunicate_SER1_RECeive_SBITs, 0,
                 two_stop_bits, source);
  return 0;
}

/* :COMMunicate:SERial:RECeive:SBITs?: SERial1's, or its minimum or
 * maximum; SERial2's one; a code out of range answers nothing
 * (0x305be). */
int32_t scpi_query_SYSTem_COMMunicate_SER1_RECeive_SBITs(void* parameters,
                                                         void* response,
                                                         scpi_call* call) {
  scpi_system_port_number* given = parameters;
  int32_t* bits = response;

  if (given->port != SCPI_SYSTEM_SERIAL1) {
    *bits = SCPI_SYSTEM_ONE_STOP_BIT;
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

/* :LANGuage: from the console, "PFORTH" or "INSTALL" ends the SCPI
 * session for the pForth console or the installer (097-z3801-01, table
 * 4-2), and "PRIMARY", this one, does nothing; another is an illegal
 * parameter value.  The text is put in upper case in place, its index
 * a byte (0x30680). */
int32_t scpi_set_SYSTem_LANGuage(void* parameters, void* response,
                                 scpi_call* call) {
  /* What ends scpi_interpret's session; status_condition_code names
   * none. */
  static const int32_t SESSION_END = -60;
  scpi_system_string_parameter* given = parameters;

  for (int8_t i = 0; given->text[i] != 0; i++) {
    given->text[i] = (char)toupper(given->text[i]);
  }
  if (!scpi_system_from_console(call)) {
    return 0;
  }
  if (scpi_parameters_same(given->text, "PFORTH")) {
    scpi_language = 0; /* SCPI_LANGUAGE_PFORTH */
    return SESSION_END;
  }
  if (scpi_parameters_same(given->text, "INSTALL")) {
    scpi_language = 1; /* SCPI_LANGUAGE_INSTALL */
    return SESSION_END;
  }
  if (scpi_parameters_same(given->text, "PRIMARY")) {
    return 0;
  }
  uint8_t source = return_zero(call->input);

  status_report_from(STATUS_ILLEGAL_PARAMETER_VALUE, source);
  return 0;
}

/* :LANGuage?: "PRIMARY" to the console; to another port nothing
 * (0x30752). */
int32_t scpi_query_SYSTem_LANGuage(void* parameters, void* response,
                                   scpi_call* call) {
  char** language = response;

  if (scpi_system_from_console(call)) {
    *language = "PRIMARY";
  }
  return 0;
}

/* :PRESet: requests the factory settings (097-z3801-01, table 4-2)
 * (0x30770). */
int32_t scpi_set_SYSTem_PRESet(void* parameters, void* response,
                               scpi_call* call) {
  uint8_t source = return_zero(call->input);

  pllc_request_b(&scpi_action_SYSTem_PRESet, 0, source);
  return 0;
}

/* :PRINt?: requests scpi_param_SYSTem_PRINt with no value, answers 0, and on
 * the console prints the status screen (0x30796). */
int32_t scpi_query_SYSTem_PRINt(void* parameters, void* response,
                                scpi_call* call) {
  uint16_t* answer = response;
  uint8_t source = return_zero(call->input);

  pllc_request_c(&scpi_param_SYSTem_PRINt, 0, 0, 0, source);
  *answer = 0;
  if (scpi_system_from_console(call)) {
    /* printf returns an int32_t, a long, and printf_function an int */
    status_screen_print(0, console_write, (printf_function*)printf);
  }
  return 0;
}

/* :PRINt:LENGth?: the status screen's length (0x30800). */
int32_t scpi_query_SYSTem_PRINt_LENGth(void* parameters, void* response,
                                       scpi_call* call) {
  uint16_t* length = response;

  *length = status_screen_length();
  return 0;
}
