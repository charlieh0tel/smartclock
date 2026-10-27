/* scpi_douglas.c's functions that are this image's own: the rest are
 * common/scpi_douglas.c's, the same in every image (docs/firmware/rewrite.md,
 * "Shared C").  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype. */

/* :DOUGlas:DURation's changed hook: a duration set is not infinite
 * (0x308ae). */
void douglas_duration_changed(void) {
  nv_settings.douglas_duration_infinite = 0;
}

/* :DOUGlas:STATe's done hook: requests the survey stopped or started
 * (0x46538). */
void douglas_state_done(void) {
  douglas_stop_requested = nv_settings.douglas_state != 0;
  douglas_start_requested = nv_settings.douglas_state == 0;
}

/* The keywords :DOUGlas:DURation takes, by their values in its list
 * (0x56dd6): MAX, MIN and INF. */
enum {
  SCPI_DOUGLAS_MAXIMUM = 0,
  SCPI_DOUGLAS_MINIMUM = 1,
  SCPI_DOUGLAS_INFINITY = 2,
};

/* SCPI's infinity, 9.9E37, which a duration of INF is. */
static const float SCPI_DOUGLAS_INFINITE = 9.9e37f;

/* :DOUGlas:DURation?: SCPI's infinity when infinite and asked without
 * MAX or MIN, else the duration, or its MAX or MIN (0x2cee4). */
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

/* :DOUGlas:STATe ONCE, its only keyword: starts a survey, setting the
 * state to 0, or while one runs, sends the state's request descriptor 0
 * (0x2cfae). */
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

/* :DOUGlas:STATe?: ONCE while the state is 0, else 0 (0x2d026). */
int32_t scpi_query_DOUGlas_STATe(void* parameters, void* response,
                                 scpi_call* call) {
  char** text = response;

  *text = nv_settings.douglas_state == 0 ? "ONCE" : "0";
  return 0;
}

/* :DOUGlas:STATe:POWerup? (0x2d086). */
int32_t scpi_query_DOUGlas_STATe_POWerup(void* parameters, void* response,
                                         scpi_call* call) {
  int16_t* state = response;

  /* Sign-extended, as return_byte's char gives it (0x2d094). */
  *state = (int8_t)nv_settings.douglas_state_powerup;
  return 0;
}
