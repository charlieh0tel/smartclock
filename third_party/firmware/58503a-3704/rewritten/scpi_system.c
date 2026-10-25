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

/* The path word of the 58503A's :COMMunicate nodes for DUART channel B,
 * duartb_settings': ALARm, BOOT, DRTB, PFORth, SERial and TOD; REMote
 * and SCI give 1, the rest 0. */
static const int16_t SCPI_SYSTEM_DUART_B = 2;

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
  if (given->port != SCPI_SYSTEM_DUART_B) {
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

/* :PRESet: requests the factory settings (097-z3801-01, table 4-2)
 * (0x30640). */
int32_t scpi_set_SYSTem_PRESet(void* parameters, void* response,
                               scpi_call* call) {
  request_source source = return_zero(call->input);

  pllc_request_b(&scpi_action_SYSTem_PRESet, 0, source);
  return 0;
}

/* :PRINt:LENGth?: the status screen's length (0x306bc). */
int32_t scpi_query_SYSTem_PRINt_LENGth(void* parameters, void* response,
                                       scpi_call* call) {
  uint16_t* length = response;

  *length = status_screen_length();
  return 0;
}
