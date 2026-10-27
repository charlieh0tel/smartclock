/* The SCPI :SOURce:PULSe tree (docs/scpi/README.md): its handlers, and
 * the hooks and actions pllc runs for its settings, written by hand in
 * place of the decompiler's (docs/firmware/README.md, "Hand-written C";
 * docs/firmware/modules.md, `applications/scpi/handlers/source_pulse`).
 * The exported C includes this file after its own, so the types and
 * prototypes it declares are in scope; each function here keeps its
 * generated prototype. */

/* Programs the continuous pulse; this firmware does nothing. */
void pulse_program(time_of_day time, calendar_date date, uint8_t state,
                   int32_t period) {}

/* :PULSe:CONTinuous:PERiod: the number, or MAX or MIN, to pllc. */
int32_t scpi_set_SOURce_PULSe_CONTinuous_PERiod(void* parameters,
                                                void* response,
                                                scpi_call* call) {
  scpi_long_parameter* period = parameters;

  scpi_numeric_set(&scpi_param_SOURce_PULSe_CONTinuous_PERiod, 0, period->kind,
                   period->keyword, 0, period->value, return_zero(call->input));
  return 0;
}

/* :PULSe:CONTinuous:PERiod?: the period, or its MAX or MIN, as a long. */
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

/* :PULSe:CONTinuous:STATe: the pulse on or off, to pllc. */
int32_t scpi_set_SOURce_PULSe_CONTinuous_STATe(void* parameters, void* response,
                                               scpi_call* call) {
  scpi_word_parameter* state = parameters;

  pllc_request_a(&scpi_param_SOURce_PULSe_CONTinuous_STATe, 0,
                 (uint8_t)state->value, return_zero(call->input));
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

/* :PULSe:STARt:DATE year, month, day. */
int32_t scpi_set_SOURce_PULSe_STARt_DATE(void* parameters, void* response,
                                         scpi_call* call) {
  scpi_source_pulse_start_set(&scpi_param_SOURce_PULSe_STARt_DATE, parameters,
                              return_zero(call->input));
  return 0;
}

/* :PULSe:STARt:TIME hour, minute, second. */
int32_t scpi_set_SOURce_PULSe_STARt_TIME(void* parameters, void* response,
                                         scpi_call* call) {
  scpi_source_pulse_start_set(&scpi_param_SOURce_PULSe_STARt_TIME, parameters,
                              return_zero(call->input));
  return 0;
}

/* :PULSe:REFerence:EDGE: a stub; -113, Undefined header. */
int32_t scpi_set_SOURce_PULSe_REFerence_EDGE(void* parameters, void* response,
                                             scpi_call* call) {
  status_report_from(STATUS_UNDEFINED_HEADER, return_zero(call->input));
  return 0;
}

/* :PULSe:REFerence:EDGE?: a stub; -113, Undefined header. */
int32_t scpi_query_SOURce_PULSe_REFerence_EDGE(void* parameters, void* response,
                                               scpi_call* call) {
  scpi_query_failed(STATUS_UNDEFINED_HEADER, call);
  return 0;
}
