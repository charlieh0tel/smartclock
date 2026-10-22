/* The SCPI :DOUGlas tree (docs/scpi/README.md): its handlers, and the
 * hooks and actions pllc runs for its settings, written by hand in place
 * of the decompiler's (docs/firmware/README.md, "Hand-written C";
 * docs/firmware/modules.md, `applications/scpi/handlers/douglas`).  The
 * exported C includes this file after its own, so the types and
 * prototypes it declares are in scope; each function here keeps its
 * generated prototype.  :DOUGlas is the position survey's
 * (docs/scpi/undocumented.md, ":DOUGlas"), the Z3801A's
 * :GPS:POSition:SURVey (097-z3801-01, table 4-2, page 4-7). */

/* :DOUGlas:DURation's changed hook: a duration set is not infinite
 * (0x308ae). */
void douglas_duration_changed(void) {
  nv_settings.douglas_duration_infinite = 0;
}

/* :DOUGlas:STATe's done hook: requests the survey stopped or started
 * (0x46340). */
void douglas_state_done(void) {
  douglas_stop_requested = nv_settings.douglas_state != 0;
  douglas_start_requested = nv_settings.douglas_state == 0;
}

/* A numeric parameter as the parsers leave it in the call's buffer: its
 * kind, then a number's value or a keyword's value in the node's list,
 * then the unit (scpi_parse_float, scpi_parse_choice). */
typedef struct scpi_douglas_numeric {
  int16_t kind; /* +0x00: SCPI_PARAMETER_DECIMAL, _CHARACTER, or SCPI_NONE
                   for a query's left out */
  union {
    float value;     /* +0x02: a number's */
    int16_t keyword; /* +0x02: SCPI_DOUGLAS_MAXIMUM, _MINIMUM or _INFINITY */
  };
  int16_t unit; /* +0x06 */
} scpi_douglas_numeric;

/* The keywords :DOUGlas:DURation takes, by their values in its list
 * (0x53d28): MAX, MIN and INF. */
enum {
  SCPI_DOUGLAS_MAXIMUM = 0,
  SCPI_DOUGLAS_MINIMUM = 1,
  SCPI_DOUGLAS_INFINITY = 2,
};

/* SCPI's infinity, 9.9E37, which a duration of INF is. */
static const float SCPI_DOUGLAS_INFINITE = 9.9e37f;

/* :DOUGlas:DURation: INF, or a number that is SCPI's infinity, sets the
 * duration infinite; else the number, as a long, or MAX or MIN, to pllc
 * (0x2ced0). */
int32_t scpi_set_DOUGlas_DURation(void* parameters, void* response,
                                  scpi_call* call) {
  scpi_douglas_numeric* duration = parameters;
  uint8_t source = return_zero(call->input);

  if ((duration->kind == SCPI_PARAMETER_CHARACTER &&
       duration->keyword == SCPI_DOUGLAS_INFINITY) ||
      (duration->kind == SCPI_PARAMETER_DECIMAL &&
       float_compare(duration->value, SCPI_DOUGLAS_INFINITE) == 0)) {
    pllc_request_a(&scpi_param_DOUGlas_DURation_INFinity, 0, 1, source);
    return 0;
  }
  /* The image converts the value whatever the kind (0x2cf26). */
  int32_t seconds = float_to_integer(duration->value);

  scpi_numeric_set(&scpi_param_DOUGlas_DURation, 0, duration->kind,
                   duration->keyword, 0, seconds, source);
  return 0;
}

/* :DOUGlas:DURation?: SCPI's infinity when infinite and asked without
 * MAX or MIN, else the duration, or its MAX or MIN (0x2cf66). */
int32_t scpi_query_DOUGlas_DURation(void* parameters, void* response,
                                    scpi_call* call) {
  scpi_douglas_numeric* bound = parameters;
  float* seconds = response;

  if (bound->kind == SCPI_NONE && nv_settings.douglas_duration_infinite != 0) {
    *seconds = SCPI_DOUGLAS_INFINITE;
  } else {
    *seconds = scpi_numeric_get(&scpi_param_DOUGlas_DURation, 0, bound->kind,
                                bound->keyword);
  }
  return 0;
}

/* :DOUGlas:PROGress?: the survey's progress as text; -221, Settings
 * conflict, when none is running (0x2cfa6). */
int32_t scpi_query_DOUGlas_PROGress(void* parameters, void* response,
                                    scpi_call* call) {
  char** text = response;
  char* progress = scpi_response_texts[(uint8_t)return_zero(call->input)];
  uint16_t sr = set_interrupt_mask(CPU_IPL_1);
  /* The image tests the result's low byte (0x2cfea). */
  uint8_t running = survey_progress_text(progress);

  set_interrupt_mask(sr);
  if (running == 0) {
    return scpi_query_failed(STATUS_SETTINGS_CONFLICT, call);
  }
  *text = progress;
  return 0;
}

/* :DOUGlas:STATe ONCE, its only keyword: starts a survey, setting the
 * state to 0, or while one runs, sends the state's request descriptor 0
 * (0x2d030). */
int32_t scpi_set_DOUGlas_STATe(void* parameters, void* response,
                               scpi_call* call) {
  if (nv_settings.douglas_state == 0) {
    pllc_request_c(&scpi_param_DOUGlas_STATe_request, 0, 0, 0,
                   return_zero(call->input));
  } else {
    pllc_request_a(&scpi_param_DOUGlas_STATe, 0, 0, return_zero(call->input));
  }
  return 0;
}

/* :DOUGlas:STATe?: ONCE while the state is 0, else 0 (0x2d0a8). */
int32_t scpi_query_DOUGlas_STATe(void* parameters, void* response,
                                 scpi_call* call) {
  char** text = response;

  *text = nv_settings.douglas_state == 0 ? "ONCE" : "0";
  return 0;
}

/* A boolean parameter: its kind, then its value
 * (scpi_parse_boolean, scpi_parse_boolean_number). */
typedef struct scpi_douglas_boolean {
  int16_t kind;   /* +0x00 */
  uint16_t value; /* +0x02 */
} scpi_douglas_boolean;

/* :DOUGlas:STATe:POWerup: whether a survey starts at power-up, to pllc
 * (0x2d0d8). */
int32_t scpi_set_DOUGlas_STATe_POWerup(void* parameters, void* response,
                                       scpi_call* call) {
  scpi_douglas_boolean* state = parameters;

  pllc_request_a(&scpi_param_DOUGlas_STATe_POWerup, 0, (uint8_t)state->value,
                 return_zero(call->input));
  return 0;
}

/* :DOUGlas:STATe:POWerup? (0x2d108). */
int32_t scpi_query_DOUGlas_STATe_POWerup(void* parameters, void* response,
                                         scpi_call* call) {
  int16_t* state = response;

  /* Sign-extended, as return_byte's char gives it (0x2d116). */
  *state = (int8_t)nv_settings.douglas_state_powerup;
  return 0;
}
