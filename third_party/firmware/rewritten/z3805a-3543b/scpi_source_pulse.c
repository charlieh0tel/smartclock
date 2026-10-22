/* The SCPI :SOURce:PULSe tree (docs/scpi/README.md): its handlers, and
 * the hooks and actions pllc runs for its settings, written by hand in
 * place of the decompiler's (docs/firmware/README.md, "Hand-written C";
 * docs/firmware/modules.md, `applications/scpi/handlers/source_pulse`).
 * The exported C includes this file after its own, so the types and
 * prototypes it declares are in scope; each function here keeps its
 * generated prototype. */

/* Programs the continuous pulse; this firmware does nothing (0x42b1a). */
void pulse_program(time_of_day time, calendar_date date, uint8_t state,
                   int32_t period) {}

/* :PULSe's done hook: programs the pulse from its settings, the start's
 * time and date each kept as three longs (0x42b1c). */
void pulse_settings_done(void) {
  time_of_day start_time = {
      .reserved = 0,
      .hour = (uint8_t)nv_settings.pulse_start_time[0],
      .minute = (uint8_t)nv_settings.pulse_start_time[1],
      .second = (uint8_t)nv_settings.pulse_start_time[2],
  };
  calendar_date start_date = {
      .year = (uint16_t)nv_settings.pulse_start_date[0],
      .month = (uint8_t)nv_settings.pulse_start_date[1],
      .day = (uint8_t)nv_settings.pulse_start_date[2],
  };

  pulse_program(start_time, start_date, nv_settings.pulse_state,
                nv_settings.pulse_period);
}

/* A long numeric parameter as the parsers leave it in the call's buffer:
 * its kind, then a number's value or MAX's or MIN's value in the node's
 * list, then the unit (scpi_parse_long, scpi_parse_choice). */
typedef struct scpi_source_pulse_numeric {
  int16_t kind; /* +0x00: SCPI_PARAMETER_DECIMAL, _CHARACTER, or SCPI_NONE
                   for a query's left out */
  union {
    int32_t value;   /* +0x02: a number's */
    int16_t keyword; /* +0x02: MAX 0, MIN 1 (0x53d40) */
  };
  int16_t unit; /* +0x06 */
} scpi_source_pulse_numeric;

/* :PULSe:CONTinuous:PERiod: the number, or MAX or MIN, to pllc
 * (0x2da78). */
int32_t scpi_set_SOURce_PULSe_CONTinuous_PERiod(void* parameters,
                                                void* response, void* call) {
  scpi_source_pulse_numeric* period = parameters;

  scpi_numeric_set(&scpi_param_SOURce_PULSe_CONTinuous_PERiod, 0, period->kind,
                   period->keyword, 0, period->value, return_zero());
  return 0;
}

/* :PULSe:CONTinuous:PERiod?: the period, or its MAX or MIN, as a long
 * (0x2dab2). */
int32_t scpi_query_SOURce_PULSe_CONTinuous_PERiod(void* parameters,
                                                  void* response, void* call) {
  scpi_source_pulse_numeric* bound = parameters;
  int32_t* period = response;

  *period = float_to_integer(
      scpi_numeric_get(&scpi_param_SOURce_PULSe_CONTinuous_PERiod, 0,
                       bound->kind, bound->keyword));
  return 0;
}

/* A boolean parameter: its kind, then its value (scpi_parse_boolean,
 * scpi_parse_boolean_number). */
typedef struct scpi_source_pulse_boolean {
  int16_t kind;   /* +0x00 */
  uint16_t value; /* +0x02 */
} scpi_source_pulse_boolean;

/* :PULSe:CONTinuous:STATe: the pulse on or off, to pllc (0x2dade). */
int32_t scpi_set_SOURce_PULSe_CONTinuous_STATe(void* parameters, void* response,
                                               void* call) {
  scpi_source_pulse_boolean* state = parameters;

  pllc_request_a(&scpi_param_SOURce_PULSe_CONTinuous_STATe, 0,
                 (uint8_t)state->value, return_zero());
  return 0;
}

/* :PULSe:CONTinuous:STATe? (0x2db0e). */
int32_t scpi_query_SOURce_PULSe_CONTinuous_STATe(void* parameters,
                                                 void* response, void* call) {
  int16_t* state = response;

  /* Sign-extended (0x2db14). */
  *state = (int8_t)nv_settings.pulse_state;
  return 0;
}

/* A date's or time of day's three fields, as a setter's short parameters
 * or a query's short response items: each a short and its unit word,
 * after a parameter's kind word (scpi_parse_short, scpi_format_short). */
#define SCPI_SOURCE_PULSE_FIELDS 3

typedef struct scpi_source_pulse_short_parameter {
  int16_t kind;  /* +0x00 */
  int16_t value; /* +0x02 */
  int16_t unit;  /* +0x04 */
} scpi_source_pulse_short_parameter;

typedef struct scpi_source_pulse_short_item {
  int16_t value;  /* +0x00 */
  int16_t unused; /* +0x02 */
} scpi_source_pulse_short_item;

/* The longs a setter of several values sends pllc the address of, three
 * for each source, at 0x102a0e, which the names file does not name: the
 * pllc_request's values[0], its index the last of them (structures.md,
 * "The pllc task's SCPI requests"). */
static int32_t (*const SCPI_SOURCE_PULSE_STAGED)[SCPI_SOURCE_PULSE_FIELDS] =
    (int32_t (*)[SCPI_SOURCE_PULSE_FIELDS])0x102a0e;

/* The start's date or time, its three fields, to pllc through its
 * source's staged longs. */
static void scpi_source_pulse_start_set(
    scpi_numeric_param* param, scpi_source_pulse_short_parameter* fields,
    uint8_t source) {
  int32_t* staged = SCPI_SOURCE_PULSE_STAGED[source];

  for (uint8_t field = 0; field < SCPI_SOURCE_PULSE_FIELDS; field++) {
    staged[field] = fields[field].value;
  }
  pllc_request_d(param, SCPI_SOURCE_PULSE_FIELDS - 1, (uint32_t)staged, source);
}

/* The start's date or time as response items, each long truncated to a
 * short. */
static void scpi_source_pulse_start_get(const int32_t* start,
                                        scpi_source_pulse_short_item* items) {
  for (uint8_t field = 0; field < SCPI_SOURCE_PULSE_FIELDS; field++) {
    items[field].value = (int16_t)start[field];
  }
}

/* :PULSe:STARt:DATE year, month, day (0x2db20). */
int32_t scpi_set_SOURce_PULSe_STARt_DATE(void* parameters, void* response,
                                         void* call) {
  scpi_source_pulse_start_set(&scpi_param_SOURce_PULSe_STARt_DATE, parameters,
                              return_zero());
  return 0;
}

/* :PULSe:STARt:DATE? (0x2dba2). */
int32_t scpi_query_SOURce_PULSe_STARt_DATE(void* parameters, void* response,
                                           void* call) {
  scpi_source_pulse_start_get(nv_settings.pulse_start_date, response);
  return 0;
}

/* :PULSe:STARt:TIME hour, minute, second (0x2dbc4). */
int32_t scpi_set_SOURce_PULSe_STARt_TIME(void* parameters, void* response,
                                         void* call) {
  scpi_source_pulse_start_set(&scpi_param_SOURce_PULSe_STARt_TIME, parameters,
                              return_zero());
  return 0;
}

/* :PULSe:STARt:TIME? (0x2dc46). */
int32_t scpi_query_SOURce_PULSe_STARt_TIME(void* parameters, void* response,
                                           void* call) {
  scpi_source_pulse_start_get(nv_settings.pulse_start_time, response);
  return 0;
}

/* :PULSe:REFerence:EDGE: a stub; -113, Undefined header (0x2dc9c). */
int32_t scpi_set_SOURce_PULSe_REFerence_EDGE(void* parameters, void* response,
                                             void* call) {
  status_report_from(STATUS_UNDEFINED_HEADER, return_zero());
  return 0;
}

/* :PULSe:REFerence:EDGE?: a stub; -113, Undefined header (0x2dcbc). */
int32_t scpi_query_SOURce_PULSe_REFerence_EDGE(void* parameters, void* response,
                                               void* call) {
  scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
  return 0;
}
