/* The EFC's history and the curv task's aging fit (docs/firmware/loop.md,
 * "The aging fit"; docs/firmware/structures.md, "The aging fit's
 * curve"), written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"; docs/firmware/modules.md,
 * `applications/pll/aging`).  The exported C includes this file after
 * its own, so the types and prototypes it declares are in scope; each
 * function here keeps its generated prototype.  The floating arithmetic
 * is calls to the image's soft-float routines (docs/firmware/
 * softfloat.md): float_divide_reversed(a, b) is b / a,
 * float_subtract_reversed(a, b) b - a, and double_compare(a, b) is
 * negative when b < a. */

/* Samples, one a second, to an entry; an entry's time is the uptime in
 * these. */
static const uint16_t PLL_AGING_SAMPLES = 2700;

static const float PLL_AGING_SAMPLES_FLOAT = 2700.0f;

/* An entry's time over this is in days (32 entries of 2700 s a day). */
static const float PLL_AGING_ENTRIES_PER_DAY = 32.0f;

/* The event efc_history_update signals the curv task with, and the curv
 * task pllp_task with when its fit is done. */
static const uint32_t PLL_AGING_FIT_EVENT = 0x100;

/* The HQ, the predicted holdover uncertainty, before there is a fit
 * (efc_history_update, 0x413c0) and of a fit of fewer than 10 entries
 * (aging_fit). */
static const float PLL_AGING_UNFITTED_UNCERTAINTY = 0.000432f;

/* An entry's weight: of a partial sum or the first three hours, of most
 * of its samples, of them all; 0 is an empty entry. */
static const uint8_t PLL_AGING_WEIGHT_PARTIAL = 1;

static const uint8_t PLL_AGING_WEIGHT_MOST = 2;

static const uint8_t PLL_AGING_WEIGHT_FULL = 3;

/* aging_fit_collect's and the routines' results: a fit failed. */
static const int32_t PLL_AGING_FIT_FAILED = 1;

/* An entry's log term from its time t: ln(t - 1) + t ln(t / (t - 1)) - 1,
 * or -1 at a time of 1 or less. */
static float pll_aging_log_term(float time) {
  static const float ONE = 1.0f;
  static const float MINUS_ONE = -1.0f;
  static const double MINUS_ONE_DOUBLE = -1.0;

  if (float_compare(ONE, time) >= 0) {
    return MINUS_ONE;
  }
  float ratio = float_divide_reversed(float_add(time, MINUS_ONE), time);
  double scaled =
      double_multiply(log(float_to_double(ratio)), float_to_double(time));
  double sum =
      double_add(log(float_to_double(float_add(time, MINUS_ONE))), scaled);

  return double_to_float(double_add(MINUS_ONE_DOUBLE, sum));
}

/* Sums the EFC, less its temperature correction, while the loop locks,
 * and each 2700 samples stores their average as an entry of the
 * history, with its weight, time and log term, and signals the curv
 * task (0x4137e). */
void efc_history_update(void) {
  /* Of an entry's samples, a quarter or more summed make its weight
   * more than partial, as does an uptime of three hours. */
  static const uint16_t QUARTER_SAMPLES = 675;
  static const uint32_t PARTIAL_UPTIME = 10800;
  static const uint8_t LOCKING_STARTUP_STATE = 1;
  char message[100];

  aging_fit_collect();
  if (pll.aging_state > PLL_AGING_COLLECTING) {
    return;
  }
  switch (pll.aging_state) {
    case PLL_AGING_START:
      pll.aging_state = PLL_AGING_SETTLING;
      pll.predicted_uncertainty = PLL_AGING_UNFITTED_UNCERTAINTY;
      pll.efc_history.fit_a = pll.efc;
      return;
    case PLL_AGING_SETTLING:
      if (pll.efc_history.loop_settled == 1) {
        pll.aging_state = PLL_AGING_COLLECTING;
      }
      return;
    case PLL_AGING_IDLE:
      return;
    default:
      break;
  }

  float current = adc_average(ADC_CURRENT);

  if (pll.stage == PLL_STAGE_NORMAL_PLL ||
      (pll.stage == PLL_STAGE_STARTUP_PLL &&
       pll.startup_state != LOCKING_STARTUP_STATE)) {
    if (pll.efc_history.summed_count == 0) {
      pll.efc_history.efc_sum = 0.0f;
    }
    float_add_in_place(&pll.efc_history.efc_sum,
                       pll_efc_less_temperature(current));
    pll.efc_history.summed_count++;
  } else if (pll.efc_history.summed_count == 0) {
    float_add_in_place(&pll.efc_history.efc_sum,
                       pll_efc_less_temperature(current));
  }
  pll.efc_history.sample_count++;
  if (pll.efc_history.sample_count < PLL_AGING_SAMPLES) {
    return;
  }

  if (pll.efc_history.summed_count == 0) {
    if (pll_debug_enabled != 0) {
      double tfom = float_to_double(pll.tfom);
      float time = float_divide(unsigned_to_float(get_uptime()),
                                PLL_AGING_SAMPLES_FLOAT);
      double days =
          float_to_double(float_divide(time, PLL_AGING_ENTRIES_PER_DAY));
      double average = float_to_double(
          float_divide(pll.efc_history.efc_sum, PLL_AGING_SAMPLES_FLOAT));

      debug_sprintf(message,
                    "e_avg= %.1f time= %f weight= %d log= %.2f  tfom= %.1e\r\n",
                    average, days, 0, 0.0, tfom);
      task_dmessage(message);
    }
  } else {
    uint8_t entry = pll.efc_history.stop_ptr;

    if ((pll.efc_history.summed_count < QUARTER_SAMPLES &&
         pll.efc_history.summed_count != 0) ||
        get_uptime() < PARTIAL_UPTIME) {
      pll.efc_history.weights[entry] = PLL_AGING_WEIGHT_PARTIAL;
    } else if (pll.efc_history.summed_count == PLL_AGING_SAMPLES) {
      pll.efc_history.weights[entry] = PLL_AGING_WEIGHT_FULL;
    } else if (pll.efc_history.summed_count >= QUARTER_SAMPLES) {
      pll.efc_history.weights[entry] = PLL_AGING_WEIGHT_MOST;
    }
    pll.efc_history.averages[entry] =
        float_divide_reversed(unsigned_to_float(pll.efc_history.summed_count),
                              pll.efc_history.efc_sum);
    float time =
        float_divide(unsigned_to_float(get_uptime()), PLL_AGING_SAMPLES_FLOAT);

    pll.efc_history.times[entry] = time;
    pll.efc_history.logs[entry] = pll_aging_log_term(time);
    if (pll_debug_enabled != 0) {
      double tfom = float_to_double(pll.tfom);
      double log_term = float_to_double(pll.efc_history.logs[entry]);
      uint32_t weight = pll.efc_history.weights[entry];
      double days =
          float_to_double(float_divide(time, PLL_AGING_ENTRIES_PER_DAY));
      double average = float_to_double(pll.efc_history.averages[entry]);

      debug_sprintf(message,
                    "e_avg= %.1f time= %f weight= %d log= %.2f tfom= %.1e\r\n",
                    average, days, weight, log_term, tfom);
      task_dmessage(message);
    }
    pll.efc_history.stop_ptr =
        (pll.efc_history.stop_ptr + 1) & PLL_AGING_INDEX_MASK;
    if (pll.efc_history.stop_ptr == pll.efc_history.start_ptr) {
      pll.efc_history.start_ptr =
          (pll.efc_history.start_ptr + 1) & PLL_AGING_INDEX_MASK;
    }
    curv_signal();
  }
  pll.efc_history.sample_count = 0;
  pll.efc_history.summed_count = 0;
  pll.efc_history.efc_sum = 0.0f;
}

/* Signals the curv task to fit the history again (0x4172a). */
void curv_signal(void) { psos_signal_v(task_curv, PLL_AGING_FIT_EVENT); }

/* If the curv task has signaled a fit done, copies its curve and HQ into
 * pll (0x4173e). */
void aging_fit_collect(void) {
  if ((uint8_t)psos_get_v(PLL_AGING_FIT_EVENT, 0, NULL) == 0) {
    pll.efc_history.fit_a = aging_curve.a;
    pll.efc_history.fit_b = aging_curve.b;
    pll.efc_history.fit_c = aging_curve.c;
    pll.predicted_uncertainty = aging_curve.predicted_uncertainty;
  }
}

/* The curv task: time sliced, fits the history each time it is
 * signaled, and signals pllp_task back (0x41784). */
void curv_task(void) {
  psos_mode(PSOS_MODE_TIME_SLICE, PSOS_MODE_TIME_SLICE, NULL);
  for (;;) {
    psos_wait_v(PLL_AGING_FIT_EVENT, 0, 0, NULL);
    aging_fit();
    psos_signal_v(task_pllp, PLL_AGING_FIT_EVENT);
  }
}

/* The fitted curve at the newest entry, b y + c w, through fitted, and a
 * moved so the curve meets that entry (0x41dcc). */
void fit_evaluate_latest(aging_curve_state* curve, float* fitted) {
  float slope_term =
      float_multiply(unsigned_to_float(curve->newest_time), curve->b);
  float log_term =
      float_multiply(curve->c, pll.efc_history.logs[curve->newest_index]);

  *fitted = float_add(slope_term, log_term);
  curve->a =
      float_subtract(pll.efc_history.averages[curve->newest_index], *fitted);
}

/* The least and greatest time of the entries of the curve's weight or
 * more, truncated, and their indexes (0x41d30). */
void fit_span(aging_curve_state* curve) {
  curve->oldest_time = 0;
  curve->newest_time = 0;
  for (int32_t i = 0; i < PLL_AGING_ENTRIES; i++) {
    if (pll.efc_history.weights[i] < curve->minimum_weight) {
      continue;
    }
    float time = pll.efc_history.times[i];

    if (float_compare(unsigned_to_float(curve->newest_time), time) < 0 ||
        curve->newest_time == 0) {
      curve->newest_index = i;
      curve->newest_time = float_to_unsigned(time);
    }
    if (float_compare(unsigned_to_float(curve->oldest_time), time) > 0 ||
        curve->oldest_time == 0) {
      curve->oldest_index = i;
      curve->oldest_time = float_to_unsigned(time);
    }
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
 * all their x the same (0x42390). */
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

/* The history less the curve's log term, regressed on time, into the
 * curve's a, b (with_slope) and rms (0x42092). */
int32_t fit_log_residuals(aging_curve_state* curve, int32_t with_slope) {
  float residuals[PLL_AGING_ENTRIES];
  regression_record regression;

  for (int32_t i = 0; i < PLL_AGING_ENTRIES; i++) {
    if (pll.efc_history.weights[i] >= curve->minimum_weight) {
      residuals[i] = float_subtract_reversed(
          float_multiply(curve->c, pll.efc_history.logs[i]),
          pll.efc_history.averages[i]);
    }
  }
  regression.minimum_weight = curve->minimum_weight;
  regression.x = pll.efc_history.times;
  regression.y = residuals;
  regression.weights = pll.efc_history.weights;
  int32_t failed = linear_regression(&regression, with_slope);

  curve->rms = regression.rms;
  curve->a = regression.intercept;
  curve->b = regression.slope;
  return failed;
}

/* The vertex of the parabola through three points, or
 * PLL_AGING_FIT_FAILED and zeros if two share an x, the points are in
 * line or the outer two's y are equal (0x42128).  With the parabola's
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
 * three; the routines' failures ORed (0x41f2a). */
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

/* The log coefficient's first value and step, from a line's change over
 * the span, at least ten times its rms, and the line's rms (0x41e32). */
int32_t fit_log_initial(aging_curve_state* curve) {
  static const double RMS_MULTIPLE = 10.0;
  regression_record regression;

  regression.minimum_weight = curve->minimum_weight;
  regression.x = pll.efc_history.times;
  regression.y = pll.efc_history.averages;
  regression.weights = pll.efc_history.weights;
  int32_t failed = linear_regression(&regression, 1);
  float change =
      float_multiply(unsigned_to_float(curve->newest_time - curve->oldest_time),
                     regression.slope);

  if (double_compare(
          float_to_double(float_abs(change)),
          double_multiply(RMS_MULTIPLE, float_to_double(regression.rms))) > 0) {
    change = double_to_float(
        double_multiply(RMS_MULTIPLE, float_to_double(regression.rms)));
  }
  float c = float_subtract(
      float_divide(change, pll.efc_history.averages[curve->newest_index]),
      pll.efc_history.averages[curve->oldest_index]);

  curve->c_step = c;
  curve->c = c;
  curve->rms = regression.rms;
  return failed;
}

/* Fits the history as a line in time, by linear_regression (0x41ba0). */
int32_t fit_linear(aging_curve_state* curve) {
  regression_record regression;
  float fitted = 0.0f;

  fit_span(curve);
  regression.minimum_weight = curve->minimum_weight;
  regression.x = pll.efc_history.times;
  regression.y = pll.efc_history.averages;
  regression.weights = pll.efc_history.weights;
  int32_t failed = linear_regression(&regression, 1);

  curve->a = regression.intercept;
  curve->b = regression.slope;
  curve->c = 0.0f;
  curve->rms = regression.rms;
  fit_evaluate_latest(curve, &fitted);
  return failed;
}

/* Fits the history as a constant, its entries' mean;
 * PLL_AGING_FIT_FAILED when no entry has the weight (0x41c20). */
int32_t fit_constant(aging_curve_state* curve) {
  int32_t count = 0;
  float sum = 0.0f;

  for (int32_t i = 0; i < PLL_AGING_ENTRIES; i++) {
    if (pll.efc_history.weights[i] >= curve->minimum_weight) {
      count++;
      sum = float_add(sum, pll.efc_history.averages[i]);
    }
  }
  if (count == 0) {
    curve->a = 0.0f;
  } else {
    curve->a = float_divide_reversed(integer_to_float(count), sum);
  }
  curve->b = 0.0f;
  curve->c = 0.0f;
  curve->rms = 0.0f;
  return count == 0 ? PLL_AGING_FIT_FAILED : 0;
}

/* Fits the history with a log term, searching its coefficient in steps
 * a tenth as large each pass until a pass gains under 1% of the rms or
 * fails (0x41c8e). */
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

/* A line from the means of the history's newer and older halves, for a
 * full history; its rms over all 64 entries (0x41a42). */
int32_t fit_linear_halves(aging_curve_state* curve) {
  /* The halves' means are 32 entries apart: their difference over 32
   * entries, of 32 summed each, is the slope. */
  static const uint32_t HALF = PLL_AGING_ENTRIES / 2;
  static const float HALVES_SCALE = 1024.0f;
  static const float ENTRIES = 64.0f;
  static const float MIDDLE_OFFSET = -32.5f;
  static const float DEGREES_OF_FREEDOM = 62.0f;
  float difference = 0.0f;
  float squares = 0.0f;
  float fitted = 0.0f;

  fit_span(curve);
  uint32_t newest = curve->newest_index;
  uint32_t older_end = (newest - HALF) & PLL_AGING_INDEX_MASK;

  for (uint32_t i = newest; i != older_end;
       i = (i - 1) & PLL_AGING_INDEX_MASK) {
    difference = float_add(difference, pll.efc_history.averages[i]);
  }
  float total = difference;

  for (uint32_t i = older_end; i != newest;
       i = (i - 1) & PLL_AGING_INDEX_MASK) {
    difference = float_subtract(difference, pll.efc_history.averages[i]);
    total = float_add(total, pll.efc_history.averages[i]);
  }
  curve->b = float_divide(difference, HALVES_SCALE);
  curve->c = 0.0f;
  float from_middle = float_multiply(
      float_add(pll.efc_history.times[curve->newest_index], MIDDLE_OFFSET),
      curve->b);

  curve->a = float_subtract(float_divide(total, ENTRIES), from_middle);
  for (int32_t i = 0; i < PLL_AGING_ENTRIES; i++) {
    float line =
        float_add(float_multiply(curve->b, pll.efc_history.times[i]), curve->a);
    float residual = float_subtract_reversed(line, pll.efc_history.averages[i]);

    squares = float_add(squares, float_multiply(residual, residual));
  }
  curve->rms = pll_aging_root_mean(squares, DEGREES_OF_FREEDOM);
  fit_evaluate_latest(curve, &fitted);
  return 0;
}

/* An entry's residual from the curve, a + b t + c w. */
static float pll_aging_residual(aging_curve_state* curve, uint32_t entry) {
  float line = float_add(float_multiply(curve->b, pll.efc_history.times[entry]),
                         curve->a);
  float curve_value =
      float_add(line, float_multiply(curve->c, pll.efc_history.logs[entry]));

  return float_subtract_reversed(curve_value, pll.efc_history.averages[entry]);
}

/* The HQ: from half the summed squares of successive residuals'
 * differences, over the fitted span, the rms's and the aging's terms
 * combined, for modes 2, 3 and 4 (0x42644).  In others the image stores
 * its frame's unset local (0x4298e); aging_fit calls it in none. */
void fit_quality(aging_curve_state* curve) {
  static const double HALF = 0.5;
  static const float SECONDS_PER_DAY = 86400.0f;
  static const float HALF_DEGREE = -0.5f;
  static const double AGING_EXPONENT = 1.5;
  static const double SAMPLES_DOUBLE = 2700.0;
  static const double FREQUENCY_SCALE = 1.6e12;
  static const double LOG_RATE_SCALE = 0.00025;
  static const double LOG_UNIT = 1e-11;
  static const float LOG_SPAN_WEIGHT = 345600.0f;
  static const double LINE_RATE_SCALE = 5.5e-05;
  static const double LINE_UNIT = 16.0;
  static const float LINE_SPAN_WEIGHT = 259200.0f;
  static const double MARGIN = 2.5;
  float variation = 0.0f;
  int32_t steps = 0;
  float aging_term;
  float span_weight;
  float uncertainty;

  float previous = pll_aging_residual(curve, curve->oldest_index);
  uint32_t end = (curve->newest_index + 1) & PLL_AGING_INDEX_MASK;

  for (uint32_t i = (curve->oldest_index + 1) & PLL_AGING_INDEX_MASK; i != end;
       i = (i + 1) & PLL_AGING_INDEX_MASK) {
    steps++;
    float residual = pll_aging_residual(curve, i);
    double change = float_to_double(float_subtract(residual, previous));

    variation = double_to_float(
        double_add(float_to_double(variation),
                   double_multiply(HALF, double_multiply(change, change))));
    previous = residual;
  }
  float span = float_divide(
      float_multiply(unsigned_to_float(curve->newest_time - curve->oldest_time),
                     SECONDS_PER_DAY),
      PLL_AGING_ENTRIES_PER_DAY);

  if (curve->mode == AGING_FIT_LOG) {
    double noise = sqrt(float_to_double(float_divide_reversed(
        float_add(integer_to_float(steps), HALF_DEGREE), variation)));
    double rate = double_divide_reversed(
        FREQUENCY_SCALE, double_divide_reversed(
                             LOG_UNIT, double_multiply(noise, LOG_RATE_SCALE)));
    double growth =
        pow(float_to_double(float_divide(span, PLL_AGING_SAMPLES_FLOAT)),
            AGING_EXPONENT);

    aging_term = double_to_float(double_divide_reversed(growth, rate));
    span_weight = LOG_SPAN_WEIGHT;
  } else if (curve->mode == AGING_FIT_LINE || curve->mode == AGING_FIT_HALVES) {
    double noise = sqrt(float_to_double(float_divide_reversed(
        float_add(integer_to_float(steps), HALF_DEGREE), variation)));
    double rate = double_divide_reversed(
        LINE_UNIT, double_multiply(noise, LINE_RATE_SCALE));
    double growth =
        pow(double_divide_reversed(SAMPLES_DOUBLE, float_to_double(span)),
            AGING_EXPONENT);

    aging_term = double_to_float(double_divide_reversed(growth, rate));
    span_weight = LINE_SPAN_WEIGHT;
  } else {
    curve->predicted_uncertainty = uncertainty;
    return;
  }
  double span_factor = sqrt(float_to_double(float_divide(span_weight, span)));
  double rms_rate = double_divide(
      float_to_double(float_multiply(curve->rms, SECONDS_PER_DAY)),
      FREQUENCY_SCALE);
  float rms_term = double_to_float(double_multiply(span_factor, rms_rate));
  float sum = float_add(float_multiply(rms_term, rms_term),
                        float_multiply(aging_term, aging_term));

  uncertainty =
      double_to_float(double_multiply(MARGIN, sqrt(float_to_double(sum))));
  curve->predicted_uncertainty = uncertainty;
}

/* Fits whichever of a line and a log curve fits better, the log curve
 * unless the line's rms is under three quarters of it and 16 entries
 * or more are well weighted. */
static uint32_t pll_aging_fit_best(uint8_t minimum_weight,
                                   int32_t well_weighted) {
  static const float LINE_ADVANTAGE = 0.75f;
  static const int32_t ENOUGH_FOR_LINE = 16;
  aging_curve_state candidate;

  aging_curve.minimum_weight = minimum_weight;
  candidate.minimum_weight = minimum_weight;
  uint32_t line_failed = fit_linear(&aging_curve);
  uint32_t failed = fit_logarithmic(&candidate) | line_failed;

  aging_curve.mode = AGING_FIT_LINE;
  if (float_compare(float_multiply(aging_curve.rms, LINE_ADVANTAGE),
                    candidate.rms) > 0 ||
      well_weighted < ENOUGH_FOR_LINE) {
    aging_curve.a = candidate.a;
    aging_curve.b = candidate.b;
    aging_curve.c = candidate.c;
    aging_curve.rms = candidate.rms;
    aging_curve.mode = AGING_FIT_LOG;
  }
  fit_quality(&aging_curve);
  return failed;
}

/* Fits the oscillator's aging curve to the history, for the curv task:
 * by halves when every entry is well weighted over four days, else a
 * line or a log curve by how many entries there are, and its HQ
 * (0x417c6). */
void aging_fit(void) {
  /* An entry's time over four days (32 entries a day). */
  static const uint32_t FOUR_DAYS = 128;
  static const int32_t FEW_WELL_WEIGHTED = 3;
  static const int32_t SOME_WELL_WEIGHTED = 6;
  static const int32_t FEW_ENTRIES = 3;
  static const int32_t SOME_ENTRIES = 10;
  int32_t well_weighted = 0;
  int32_t entries = 0;
  uint32_t failed;
  char message[80];

  aging_curve.minimum_weight = PLL_AGING_WEIGHT_MOST;
  fit_span(&aging_curve);
  for (int32_t i = 0; i < PLL_AGING_ENTRIES; i++) {
    if (pll.efc_history.weights[i] >= PLL_AGING_WEIGHT_MOST) {
      well_weighted++;
    }
    if (pll.efc_history.weights[i] != 0) {
      entries++;
    }
  }
  if (well_weighted == PLL_AGING_ENTRIES &&
      aging_curve.newest_time > FOUR_DAYS) {
    aging_curve.minimum_weight = PLL_AGING_WEIGHT_MOST;
    failed = fit_linear_halves(&aging_curve);
    aging_curve.mode = AGING_FIT_HALVES;
    fit_quality(&aging_curve);
  } else if (aging_curve.oldest_time <= FOUR_DAYS ||
             well_weighted < FEW_WELL_WEIGHTED) {
    if (well_weighted >= SOME_WELL_WEIGHTED) {
      failed = pll_aging_fit_best(PLL_AGING_WEIGHT_MOST, well_weighted);
    } else if (entries >= SOME_ENTRIES) {
      failed = pll_aging_fit_best(PLL_AGING_WEIGHT_PARTIAL, well_weighted);
    } else {
      aging_curve.minimum_weight = PLL_AGING_WEIGHT_PARTIAL;
      if (entries < FEW_ENTRIES) {
        failed = fit_constant(&aging_curve);
      } else {
        failed = fit_linear(&aging_curve);
      }
      aging_curve.mode =
          entries < FEW_ENTRIES ? AGING_FIT_CONSTANT : AGING_FIT_FEW;
      aging_curve.predicted_uncertainty = PLL_AGING_UNFITTED_UNCERTAINTY;
    }
  } else {
    aging_curve.minimum_weight = PLL_AGING_WEIGHT_MOST;
    failed = fit_linear(&aging_curve);
    aging_curve.mode = AGING_FIT_LINE;
    fit_quality(&aging_curve);
  }
  if (pll_debug_enabled == 0) {
    return;
  }
  if (failed == PLL_AGING_FIT_FAILED) {
    task_dmessage("failed line fit\r\n");
    return;
  }
  double rms = float_to_double(aging_curve.rms);
  double c = float_to_double(aging_curve.c);
  double b = float_to_double(aging_curve.b);
  double a = float_to_double(aging_curve.a);

  debug_sprintf(message, "pts= %d a= %.1f b= %.1f c= %.1f rms= %.1f\r\n",
                well_weighted, a, b, c, rms);
  task_dmessage(message);
  debug_sprintf(message, "HQ= %.1e mode= %d\r\n",
                float_to_double(aging_curve.predicted_uncertainty),
                (uint32_t)aging_curve.mode);
  task_dmessage(message);
}
