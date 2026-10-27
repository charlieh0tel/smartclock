/* The functions of pll_aging.c the images share, the same in each
 * (docs/firmware/rewrite.md, "Shared C"); each image's own
 * rewritten/pll_aging.c describes the module and holds the rest.  Written by
 * hand in place of the decompiler's (docs/firmware/README.md,
 * "Hand-written C"); each keeps its generated prototype. */

/* The event efc_history_update signals the curv task with, and the curv
 * task pllp_task with when its fit is done. */
static const uint32_t PLL_AGING_FIT_EVENT = 0x100;

/* aging_fit_collect's and the routines' results: a fit failed. */
static const int32_t PLL_AGING_FIT_FAILED = 1;

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

/* The square root of |sum / count|, a float, as the fits take an rms. */
static float pll_aging_root_mean(float sum, float count) {
  return double_to_float(
      sqrt(float_to_double(float_abs(float_divide(sum, count)))));
}

/* The means, Sxx, Sxy and Syy of the entries of the record's weight or
 * more, and from them the line, or with_slope 0 the mean, and its rms;
 * PLL_AGING_FIT_FAILED and zeros with two entries or fewer, or with
 * all their x the same. */
int32_t linear_regression(regression_record* record, int32_t with_slope) {
  static const double FEWEST = 2.0;
  static const float TWO = 2.0f;
  float x_sum = 0.0f;
  float y_sum = 0.0f;
  float x_mean = 0.0f;
  float y_mean = 0.0f;
  float sxx = 0.0f;
  float sxy = 0.0f;
  float syy = 0.0f;
  uint16_t count = 0;

  for (int32_t i = 0; i < PLL_AGING_ENTRIES; i++) {
    if (record->weights[i] >= record->minimum_weight) {
      float_add_in_place(&x_sum, record->x[i]);
      float_add_in_place(&y_sum, record->y[i]);
      count++;
    }
  }
  if (count != 0) {
    x_mean = float_divide_reversed(unsigned_to_float(count), x_sum);
    y_mean = float_divide_reversed(unsigned_to_float(count), y_sum);
  }
  for (int32_t i = 0; i < PLL_AGING_ENTRIES; i++) {
    if (record->weights[i] >= record->minimum_weight) {
      float dx = float_subtract(record->x[i], x_mean);
      float dy = float_subtract(record->y[i], y_mean);

      sxx = float_add(sxx, float_multiply(dx, dx));
      float_add_in_place(&sxy, float_multiply(dx, dy));
      float_add_in_place(&syy, float_multiply(dy, dy));
    }
  }
  if (double_compare(FEWEST, unsigned_to_double(count)) <= 0 ||
      pll_is_zero(sxx)) {
    record->slope = 0.0f;
    record->rms = 0.0f;
    record->intercept = 0.0f;
    return PLL_AGING_FIT_FAILED;
  }
  float degrees_of_freedom = integer_to_float(count - 2);

  if (with_slope == 0) {
    record->rms = pll_aging_root_mean(syy, degrees_of_freedom);
    record->intercept = y_mean;
    record->slope = 0.0f;
  } else {
    float slope = float_divide(sxy, sxx);

    record->slope = slope;
    float explained =
        float_add(float_multiply(float_multiply(slope, slope), sxx), syy);
    float cross = float_multiply(float_multiply(record->slope, sxy), TWO);

    record->rms = pll_aging_root_mean(float_subtract(explained, cross),
                                      degrees_of_freedom);
    record->intercept =
        float_subtract_reversed(float_multiply(record->slope, x_mean), y_mean);
  }
  return 0;
}

/* The vertex of the parabola through three points, or
 * PLL_AGING_FIT_FAILED and zeros if two share an x, the points are in
 * line or the outer two's y are equal.  With the parabola's
 * curvature zero it gives the line through the outer two's crossing of
 * zero instead. */
int32_t fit_three_point(three_point_fit* fit) {
  static const float TWO = 2.0f;
  float x0 = fit->x[0];
  float x1 = fit->x[1];
  float x2 = fit->x[2];
  float y0 = fit->y[0];
  float y1 = fit->y[1];
  float y2 = fit->y[2];
  float numerator = float_subtract(
      float_multiply(float_subtract(y0, y1), float_subtract(x1, x2)),
      float_multiply(float_subtract(y1, y2), float_subtract(x0, x1)));
  float denominator = float_subtract(
      float_multiply(
          float_subtract(float_multiply(x0, x0), float_multiply(x1, x1)),
          float_subtract(x1, x2)),
      float_multiply(
          float_subtract(float_multiply(x1, x1), float_multiply(x2, x2)),
          float_subtract(x0, x1)));

  if (float_compare(x0, x1) == 0 || float_compare(x1, x2) == 0 ||
      float_compare(x2, x0) == 0 || pll_is_zero(denominator) ||
      pll_is_zero(numerator) || float_compare(y2, y0) == 0) {
    fit->vertex_x = 0.0f;
    fit->vertex_y = 0.0f;
    return PLL_AGING_FIT_FAILED;
  }
  float curvature = float_divide(numerator, denominator);

  if (pll_is_zero(curvature)) {
    fit->vertex_x = float_divide(
        float_subtract(float_multiply(x0, y2), float_multiply(x2, y0)),
        float_subtract(y2, y0));
    fit->vertex_y =
        float_divide(float_subtract(y2, y0), float_subtract(x2, x0));
  } else {
    float offset = float_divide(
        float_divide(
            float_divide(float_subtract(y0, y2), float_subtract(x0, x2)), TWO),
        curvature);

    fit->vertex_x =
        float_subtract(float_divide(float_add(x0, x2), TWO), offset);
    float from_middle = float_subtract(x1, fit->vertex_x);

    fit->vertex_y = float_subtract_reversed(
        float_multiply(float_multiply(from_middle, curvature), from_middle),
        y1);
  }
  return 0;
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
