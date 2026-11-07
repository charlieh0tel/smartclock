/* The functions of pll_aging.c the images share, the same in each
 * (docs/firmware/rewrite.md, "Shared C"); each image's own
 * rewritten/pll_aging.c describes the module and holds the rest.  Written by
 * hand in place of the decompiler's (docs/firmware/README.md,
 * "Hand-written C"); each keeps its generated prototype. */

/* Signals the curv task to fit the history again. */
void curv_signal(void) { psos_signal_v(task_curv, PLL_AGING_FIT_EVENT); }

/* The curv task: time sliced, fits the history each time it is
 * signaled, and signals pllp_task back. */
void curv_task(void) {
  psos_mode(PSOS_MODE_TIME_SLICE, PSOS_MODE_TIME_SLICE, NULL);
  for (;;) {
    psos_wait_v(PLL_AGING_FIT_EVENT, 0, 0, NULL);
    aging_fit();
    psos_signal_v(task_pllp, PLL_AGING_FIT_EVENT);
  }
}

/* Steps the curve's log coefficient by c_step, the way the rms falls,
 * while it falls, then takes the vertex of the parabola through the last
 * three; the routines' failures ORed. */
uint32_t fit_log_search(aging_curve_state* curve, int32_t with_slope) {
  float rms_values[3];
  float c_values[3];
  three_point_fit three_point;

  uint32_t first_failed = fit_log_residuals(curve, with_slope);

  rms_values[1] = curve->rms;
  c_values[1] = curve->c;
  c_values[2] = float_add_in_place(&curve->c, curve->c_step);
  uint32_t failed = fit_log_residuals(curve, with_slope) | first_failed;

  rms_values[2] = curve->rms;
  if (float_compare(rms_values[1], rms_values[2]) <= 0) {
    /* Up did not help: go down, from the first two swapped. */
    float_subtract_in_place(&curve->c, curve->c_step);
    float c_up = c_values[2];
    float rms_up = rms_values[2];

    c_values[2] = c_values[1];
    c_values[1] = c_up;
    rms_values[2] = rms_values[1];
    rms_values[1] = rms_up;
    while (float_compare(rms_values[1], rms_values[2]) > 0 && failed == 0) {
      c_values[0] = c_values[1];
      c_values[1] = c_values[2];
      c_values[2] = float_subtract_in_place(&curve->c, curve->c_step);
      failed = fit_log_residuals(curve, with_slope);
      rms_values[0] = rms_values[1];
      rms_values[1] = rms_values[2];
      rms_values[2] = curve->rms;
    }
  } else {
    while (float_compare(rms_values[2], rms_values[1]) < 0) {
      c_values[0] = c_values[1];
      c_values[1] = c_values[2];
      c_values[2] = float_add_in_place(&curve->c, curve->c_step);
      failed |= fit_log_residuals(curve, with_slope);
      rms_values[0] = rms_values[1];
      rms_values[1] = rms_values[2];
      rms_values[2] = curve->rms;
    }
  }
  three_point.x = c_values;
  three_point.y = rms_values;
  uint32_t vertex_failed = fit_three_point(&three_point);

  curve->c = three_point.vertex_x;
  return vertex_failed | failed | fit_log_residuals(curve, with_slope);
}

/* Fits the history with a log term, searching its coefficient in steps
 * a tenth as large each pass until a pass gains under 1% of the rms or
 * fails. */
int32_t fit_logarithmic(aging_curve_state* curve) {
  static const float STEP_SHRINK = 10.0f;
  static const double LEAST_GAIN = 0.01;
  float fitted = 0.0f;
  uint32_t failed;

  fit_span(curve);
  fit_log_initial(curve);
  do {
    float previous_rms = curve->rms;

    failed = fit_log_search(curve, 1);
    float_divide_in_place(&curve->c_step, STEP_SHRINK);
    double gain = float_to_double(float_subtract(previous_rms, curve->rms));

    if (double_compare(gain, double_multiply(float_to_double(previous_rms),
                                             LEAST_GAIN)) >= 0) {
      break;
    }
  } while (failed == 0);
  fit_evaluate_latest(curve, &fitted);
  return failed;
}
