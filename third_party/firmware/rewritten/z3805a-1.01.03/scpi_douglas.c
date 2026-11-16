/* From the 58503B 1.01.04's rewritten/scpi_douglas.c, the Z3805A 1.01.03's
 * own beside common/'s: of its functions, those the Z3805A 1.01.03's code
 * carries identical or relocated (docs/firmware/z3805a-1.01.03.md, "The
 * link list").  Its comments' addresses are the Z3805A 1.01.03's, but where
 * they name another image.  Each keeps its generated prototype. */

/* SCPI's infinity, 9.9E37, which a duration of INF is. */
static const float SCPI_DOUGLAS_INFINITE = 9.9e37f;

/* :DOUGlas:DURation's keywords, by their indexes in its list: INF's. */
enum {
  SCPI_DOUGLAS_INFINITY = 2,
};

/* The duration :DOUGlas:DURation takes no number past, 2^31 seconds
 * (0x2e7ee). */
static const float SCPI_DOUGLAS_LONGEST = 2147483648.0f;

/* :DOUGlas:DURation's changed hook: a duration set is not infinite
 * (0x322dc). */
void douglas_duration_changed(void) {
  nv_settings.douglas_duration_infinite = 0;
}

/* :DOUGlas:STATe's done hook: requests the survey stopped or started
 * (0x46eae). */
void douglas_state_done(void) {
  douglas_stop_requested = nv_settings.douglas_state != 0;
  douglas_start_requested = nv_settings.douglas_state == 0;
}

/* :DOUGlas:DURation?: SCPI's infinity when infinite and asked without
 * MAX or MIN, else the duration, or its MAX or MIN (0x2e846). */
int32_t scpi_query_DOUGlas_DURation(void* parameters, void* response,
                                    scpi_call* call) {
  scpi_float_parameter* bound = parameters;
  float* seconds = response;

  if (bound->kind == SCPI_NONE && nv_settings.douglas_duration_infinite != 0) {
    *seconds = SCPI_DOUGLAS_INFINITE;
  } else {
    *seconds = scpi_numeric_get(&scpi_param_DOUGlas_DURation, 0, bound->kind,
                                bound->keyword);
  }
  return 0;
}

/* :DOUGlas:DURation: INF, or a number that is SCPI's infinity, sets the
 * duration infinite; else the number, as a long, or MAX or MIN, to pllc
 * (0x2e794).  The 58503A 3704's but that a value past
 * SCPI_DOUGLAS_LONGEST is first made it, in the parameter, whatever the
 * kind (0x2e7ea to 0x2e804). */
int32_t scpi_set_DOUGlas_DURation(void* parameters, void* response,
                                  scpi_call* call) {
  scpi_float_parameter* duration = parameters;

  if ((duration->kind == SCPI_PARAMETER_CHARACTER &&
       duration->keyword == SCPI_DOUGLAS_INFINITY) ||
      (duration->kind == SCPI_PARAMETER_DECIMAL &&
       float_compare(duration->value, SCPI_DOUGLAS_INFINITE) == 0)) {
    pllc_request_a(&scpi_param_DOUGlas_DURation_INFinity, 0, 1,
                   scpi_input_source(call->input));
    return 0;
  }
  if (float_compare(duration->value, SCPI_DOUGLAS_LONGEST) > 0) {
    duration->value = SCPI_DOUGLAS_LONGEST;
  }
  /* The image converts the value whatever the kind (0x2e80a). */
  int32_t seconds = float_to_integer(duration->value);

  scpi_numeric_set(&scpi_param_DOUGlas_DURation, 0, duration->kind,
                   duration->keyword, 0, seconds,
                   scpi_input_source(call->input));
  return 0;
}

/* :DOUGlas:STATe ONCE, its only keyword: starts a survey, setting the
 * state to 0, or while one runs, sends the state's request descriptor 0
 * (0x2e8fe). */
int32_t scpi_set_DOUGlas_STATe(void* parameters, void* response,
                               scpi_call* call) {
  if (nv_settings.douglas_state == 0) {
    pllc_request_c(&scpi_param_DOUGlas_STATe_request, 0, 0, 0,
                   scpi_input_source(call->input));
  } else {
    pllc_request_a(&scpi_param_DOUGlas_STATe, 0, 0,
                   scpi_input_source(call->input));
  }
  return 0;
}

/* :DOUGlas:STATe?: ONCE while the state is 0, else 0 (0x2e964). */
int32_t scpi_query_DOUGlas_STATe(void* parameters, void* response,
                                 scpi_call* call) {
  char** text = response;

  *text = nv_settings.douglas_state == 0 ? "ONCE" : "0";
  return 0;
}

/* :DOUGlas:STATe:POWerup? (0x2e9c4). */
int32_t scpi_query_DOUGlas_STATe_POWerup(void* parameters, void* response,
                                         scpi_call* call) {
  int16_t* state = response;

  /* Sign-extended, as return_byte's char gives it (0x2e9d2). */
  *state = (int8_t)nv_settings.douglas_state_powerup;
  return 0;
}
