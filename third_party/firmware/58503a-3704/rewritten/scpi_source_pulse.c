/* The SCPI :SOURce:PULSe tree (docs/scpi/README.md): its handlers, and
 * the hooks and actions pllc runs for its settings, written by hand in
 * place of the decompiler's (docs/firmware/README.md, "Hand-written C";
 * docs/firmware/modules.md, `applications/scpi/handlers/source_pulse`).
 * The exported C includes this file after its own, so the types and
 * prototypes it declares are in scope; each function here keeps its
 * generated prototype. */

/* Programs the continuous pulse; this firmware does nothing (0x47fa8). */
void pulse_program(time_of_day time, calendar_date date, uint8_t state,
                   int32_t period) {}

/* :PULSe's done hook: programs the pulse from its settings, the start's
 * time and date each kept as three longs (0x47faa). */
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

/* :PULSe:CONTinuous:PERiod: the number, or MAX or MIN, to pllc
 * (0x2da0a). */
int32_t scpi_set_SOURce_PULSe_CONTinuous_PERiod(void* parameters,
                                                void* response,
                                                scpi_call* call) {
  scpi_long_parameter* period = parameters;

  scpi_numeric_set(&scpi_param_SOURce_PULSe_CONTinuous_PERiod, 0, period->kind,
                   period->keyword, 0, period->value, return_zero(call->input));
  return 0;
}

/* :PULSe:CONTinuous:PERiod?: the period, or its MAX or MIN, as a long
 * (0x2da44). */
int32_t scpi_query_SOURce_PULSe_CONTinuous_PERiod(void* parameters,
                                                  void* response,
                                                  scpi_call* call) {
  scpi_long_parameter* bound = parameters;
  int32_t* period = response;

  *period = float_to_integer(
      scpi_numeric_get(&scpi_param_SOURce_PULSe_CONTinuous_PERiod, 0,
                       bound->kind, bound->keyword));
  return 0;
}

/* :PULSe:CONTinuous:STATe: the pulse on or off, to pllc (0x2da70). */
int32_t scpi_set_SOURce_PULSe_CONTinuous_STATe(void* parameters, void* response,
                                               scpi_call* call) {
  scpi_word_parameter* state = parameters;

  pllc_request_a(&scpi_param_SOURce_PULSe_CONTinuous_STATe, 0,
                 (uint8_t)state->value, return_zero(call->input));
  return 0;
}

/* :PULSe:CONTinuous:STATe? (0x2daa0). */
int32_t scpi_query_SOURce_PULSe_CONTinuous_STATe(void* parameters,
                                                 void* response,
                                                 scpi_call* call) {
  int16_t* state = response;

  /* Sign-extended (0x2daa6). */
  *state = (int8_t)nv_settings.pulse_state;
  return 0;
}

/* A date's or time of day's three fields, as a setter's short parameters
 * or a query's short response items: each a short and its unit word,
 * after a parameter's kind word (scpi_parse_short, scpi_format_short). */
#define SCPI_SOURCE_PULSE_FIELDS 3

/* The start's date or time, its three fields, to pllc through its
 * source's staged longs. */
static void scpi_source_pulse_start_set(scpi_numeric_param* param,
                                        scpi_short_parameter* fields,
                                        request_source source) {
  int32_t* staged = scpi_staged[source];

  for (uint8_t field = 0; field < SCPI_SOURCE_PULSE_FIELDS; field++) {
    staged[field] = fields[field].value;
  }
  pllc_request_d(param, SCPI_SOURCE_PULSE_FIELDS - 1, (uint32_t)staged, source);
}

/* The start's date or time as response items, each long truncated to a
 * short. */
static void scpi_source_pulse_start_get(const int32_t* start,
                                        scpi_short_item* items) {
  for (uint8_t field = 0; field < SCPI_SOURCE_PULSE_FIELDS; field++) {
    items[field].value = (int16_t)start[field];
  }
}

/* :PULSe:STARt:DATE year, month, day (0x2db20). */
int32_t scpi_set_SOURce_PULSe_STARt_DATE(void* parameters, void* response,
                                         scpi_call* call) {
  scpi_source_pulse_start_set(&scpi_param_SOURce_PULSe_STARt_DATE, parameters,
                              return_zero(call->input));
  return 0;
}

/* :PULSe:STARt:DATE? (0x2db34). */
int32_t scpi_query_SOURce_PULSe_STARt_DATE(void* parameters, void* response,
                                           scpi_call* call) {
  scpi_source_pulse_start_get(nv_settings.pulse_start_date, response);
  return 0;
}

/* :PULSe:STARt:TIME hour, minute, second (0x2dbc4). */
int32_t scpi_set_SOURce_PULSe_STARt_TIME(void* parameters, void* response,
                                         scpi_call* call) {
  scpi_source_pulse_start_set(&scpi_param_SOURce_PULSe_STARt_TIME, parameters,
                              return_zero(call->input));
  return 0;
}

/* :PULSe:STARt:TIME? (0x2dbd8). */
int32_t scpi_query_SOURce_PULSe_STARt_TIME(void* parameters, void* response,
                                           scpi_call* call) {
  scpi_source_pulse_start_get(nv_settings.pulse_start_time, response);
  return 0;
}

/* :PULSe:REFerence:EDGE: a stub; -113, Undefined header (0x2dc2e). */
int32_t scpi_set_SOURce_PULSe_REFerence_EDGE(void* parameters, void* response,
                                             scpi_call* call) {
  status_report_from(STATUS_UNDEFINED_HEADER, return_zero(call->input));
  return 0;
}

/* :PULSe:REFerence:EDGE?: a stub; -113, Undefined header (0x2dc4e). */
int32_t scpi_query_SOURce_PULSe_REFerence_EDGE(void* parameters, void* response,
                                               scpi_call* call) {
  scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
  return 0;
}
