/* The EFC's history and the curv task's aging fit (docs/firmware/loop.md,
 * "The aging fit"; docs/firmware/structures.md, "The loop's state"),
 * written by hand in place of the decompiler's (docs/firmware/README.md,
 * "Hand-written C"; docs/firmware/modules.md, `applications/pll/aging`):
 * the 58503A 3704's over this image's own pll_state, whose history is a
 * ring of 32 entries.  common/pll_aging.c has the curv task and the log
 * fit, the same in each image.  The exported C includes this file after
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

static const double PLL_AGING_SAMPLES_DOUBLE = 2700.0;

/* An entry's time over this is in days (32 entries of 2700 s a day). */
static const float PLL_AGING_ENTRIES_PER_DAY = 32.0f;

/* The HQ, the predicted holdover uncertainty, before there is a fit
 * (efc_history_update, 0x4a5d6) and of a constant fit (aging_fit). */
static const float PLL_AGING_UNFITTED_UNCERTAINTY = 0.000432f;

/* An entry's weight: of a partial sum or the first hours, of most of
 * its samples, of them all; 0 is an empty entry. */
static const uint8_t PLL_AGING_WEIGHT_PARTIAL = 1;

static const uint8_t PLL_AGING_WEIGHT_MOST = 2;

static const uint8_t PLL_AGING_WEIGHT_FULL = 3;

/* An entry's log term from its time t, over 1: ln(t - 1) + t ln(t / (t -
 * 1)) - 1 (0x4a7d8). */
static float pll_aging_log_term(float time) {
  static const float MINUS_ONE = -1.0f;
  static const double MINUS_ONE_DOUBLE = -1.0;

  float ratio = float_divide_reversed(float_add(time, MINUS_ONE), time);
  double scaled =
      double_multiply(log(float_to_double(ratio)), float_to_double(time));
  double sum =
      double_add(log(float_to_double(float_add(time, MINUS_ONE))), scaled);

  return double_to_float(double_add(MINUS_ONE_DOUBLE, sum));
}

/* The 58503A's moved a so the curve met the newest entry; this image's
 * is an rts, so a stays the fit's (0x4b062). */
void fit_evaluate_latest(aging_curve_state* curve, float* fitted) {}

/* The least and greatest time of the entries of the curve's weight or
 * more, truncated, and their indexes (0x4afc6). */
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

/* The history less the curve's log term, regressed on time, into the
 * curve's a, b (with_slope) and rms (0x4b2c4). */
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

/* The log coefficient's first value and step, from a line's change over
 * the span, at least ten times its rms, and the line's rms (0x4b064). */
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

/* Fits the history as a line in time, by linear_regression (0x4ae46). */
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

/* Fits the history as a constant, its newest entry of the weight, where
 * the 58503A's took their mean; PLL_AGING_FIT_FAILED and 0 when no entry
 * has the weight (0x4aec6). */
int32_t fit_constant(aging_curve_state* curve) {
  int32_t count = 0;
  int32_t failed = 0;

  fit_span(curve);
  for (int32_t i = 0; i < PLL_AGING_ENTRIES; i++) {
    if (pll.efc_history.weights[i] >= curve->minimum_weight) {
      count++;
    }
  }
  if (count != 0) {
    curve->a = pll.efc_history.averages[curve->newest_index];
  } else {
    curve->a = 0.0f;
    failed = PLL_AGING_FIT_FAILED;
  }
  curve->b = 0.0f;
  curve->c = 0.0f;
  curve->rms = 0.0f;
  return failed;
}

/* A line from the means of the history's newer and older halves; its
 * rms over all 32 entries (0x4ace8).  Nothing calls it in this image.
 * Its halves are 16 entries, but it keeps the 58503A's scale for 64, the
 * slope over 1024, and the newest entry's distance from the middle,
 * 32.5, which for halves of 16 are 256 and 16.5, a fault kept
 * (docs/firmware/quirks.md, "Wrong results"). */
int32_t fit_linear_halves(aging_curve_state* curve) {
  static const uint32_t HALF = PLL_AGING_ENTRIES / 2;
  static const float HALVES_SCALE = 1024.0f;
  static const float ENTRIES = 32.0f;
  static const float MIDDLE_OFFSET = -32.5f;
  static const float DEGREES_OF_FREEDOM = 30.0f;
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

/* The HQ's rate: the noise of the residuals' successive differences
 * times scale, over 1e-11, over the EFC's counts per unit fractional
 * frequency.  The 58503A's divided by 1.6e12 for the counts and, for a
 * line, by 16 for the 1e-11. */
static double pll_aging_rate(int32_t steps, float variation, double scale) {
  static const float HALF_DEGREE = -0.5f;
  static const double UNIT = 1e-11;
  double noise = sqrt(float_to_double(float_divide_reversed(
      float_add(integer_to_float(steps), HALF_DEGREE), variation)));
  double scaled = double_divide_reversed(UNIT, double_multiply(noise, scale));

  return double_divide_reversed(float_to_double(efc_gains.reciprocal_fine),
                                scaled);
}

/* The HQ: from half the summed squares of successive residuals'
 * differences, over the fitted span, the rms's and the aging's terms
 * combined, for modes 2, 3 and 4, in the EFC's counts per unit
 * fractional frequency at the OCXO calibration's gain, which it sets
 * first (0x4b884).  In others the image stores its frame's unset local
 * (0x4bc00), which is volatile so that it is read from the frame as the
 * image reads it, whatever the compiler (docs/firmware/quirks.md, "Reads
 * of what nothing set"). */
void fit_quality(aging_curve_state* curve) {
  static const double HALF = 0.5;
  static const float SECONDS_PER_DAY = 86400.0f;
  static const double AGING_EXPONENT = 1.5;
  static const double LOG_RATE_SCALE = 0.00025;
  static const float LOG_SPAN_WEIGHT = 345600.0f;
  static const double LINE_RATE_SCALE = 5.5e-05;
  static const float LINE_SPAN_WEIGHT = 259200.0f;
  static const double MARGIN = 2.5;
  float variation = 0.0f;
  int32_t steps = 0;
  float aging_term;
  float span_weight;
  volatile float uncertainty;

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

  efc_gain_from_calibration(pll.efc);
  if (curve->mode == AGING_FIT_LOG) {
    double rate = pll_aging_rate(steps, variation, LOG_RATE_SCALE);
    double growth =
        pow(float_to_double(float_divide(span, PLL_AGING_SAMPLES_FLOAT)),
            AGING_EXPONENT);

    aging_term = double_to_float(double_divide_reversed(growth, rate));
    span_weight = LOG_SPAN_WEIGHT;
  } else if (curve->mode == AGING_FIT_LINE || curve->mode == AGING_FIT_HALVES) {
    double rate = pll_aging_rate(steps, variation, LINE_RATE_SCALE);
    double growth = pow(
        double_divide_reversed(PLL_AGING_SAMPLES_DOUBLE, float_to_double(span)),
        AGING_EXPONENT);

    aging_term = double_to_float(double_divide_reversed(growth, rate));
    span_weight = LINE_SPAN_WEIGHT;
  } else {
    curve->predicted_uncertainty = uncertainty;
    return;
  }
  double span_factor = sqrt(float_to_double(float_divide(span_weight, span)));
  double rms_rate = float_to_double(float_divide(
      float_multiply(curve->rms, SECONDS_PER_DAY), efc_gains.reciprocal_fine));
  float rms_term = double_to_float(double_multiply(span_factor, rms_rate));
  float sum = float_add(float_multiply(rms_term, rms_term),
                        float_multiply(aging_term, aging_term));

  uncertainty =
      double_to_float(double_multiply(MARGIN, sqrt(float_to_double(sum))));
  curve->predicted_uncertainty = uncertainty;
}

/* aging_fit's thresholds of entries over the history's 32, which no
 * count reaches, a fault kept (docs/firmware/quirks.md, "Wrong
 * results"). */
static const int32_t PLL_AGING_MORE_THAN_HELD = 100;

/* Fits whichever of a line and a log curve fits better, the log curve
 * unless the line's rms is under three quarters of it and 14 entries
 * or more are well weighted. */
static uint32_t pll_aging_fit_best(uint8_t minimum_weight,
                                   int32_t well_weighted) {
  static const float LINE_ADVANTAGE = 0.75f;
  static const int32_t ENOUGH_FOR_LINE = 14;
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
 * a line and its HQ once the oldest well weighted entry is past half a
 * day and more than 16 are, else the newest entry as a constant; the
 * 58503A's other fits are behind counts of more than the history holds.
 * Then measured_uncertainty_update takes the fit for the
 * measured time uncertainty (0x4aa94). */
void aging_fit(void) {
  /* An entry's time of half a day (32 entries a day), and the well
   * weighted entries a line takes. */
  static const uint32_t HALF_DAY = 16;
  static const int32_t ENOUGH_FOR_LINE = 16;
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
    if (pll.efc_history.weights[i] >= PLL_AGING_WEIGHT_PARTIAL) {
      entries++;
    }
  }
  if (aging_curve.oldest_time > HALF_DAY && well_weighted > ENOUGH_FOR_LINE) {
    aging_curve.minimum_weight = PLL_AGING_WEIGHT_MOST;
    failed = fit_linear(&aging_curve);
    aging_curve.mode = AGING_FIT_LINE;
    fit_quality(&aging_curve);
  } else if (well_weighted > PLL_AGING_MORE_THAN_HELD) {
    failed = pll_aging_fit_best(PLL_AGING_WEIGHT_MOST, well_weighted);
  } else if (entries > PLL_AGING_MORE_THAN_HELD) {
    failed = pll_aging_fit_best(PLL_AGING_WEIGHT_PARTIAL, well_weighted);
  } else {
    if (well_weighted > PLL_AGING_MORE_THAN_HELD) {
      aging_curve.minimum_weight = PLL_AGING_WEIGHT_MOST;
      failed = fit_linear(&aging_curve);
      aging_curve.mode = AGING_FIT_FEW;
    } else {
      aging_curve.minimum_weight = PLL_AGING_WEIGHT_PARTIAL;
      failed = fit_constant(&aging_curve);
      aging_curve.mode = AGING_FIT_CONSTANT;
    }
    aging_curve.predicted_uncertainty = PLL_AGING_UNFITTED_UNCERTAINTY;
  }
  measured_uncertainty_update();
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

/* Takes the curv task's fit into pll when it has signaled one is done,
 * with the aging term, the fit's slope at the present uptime, and then
 * ptest_update (0x4a9d4). */
void aging_fit_collect(void) {
  if ((uint8_t)psos_get_v(PLL_AGING_FIT_EVENT, 0, NULL) == 0) {
    pll.efc_history.fit_a = aging_curve.a;
    pll.efc_history.fit_b = aging_curve.b;
    pll.efc_history.fit_c = aging_curve.c;
    pll.predicted_uncertainty = aging_curve.predicted_uncertainty;
    float uptime = unsigned_to_float(get_uptime());

    pll.aging_term = float_add(
        float_divide(
            float_multiply(pll.efc_history.fit_c, PLL_AGING_SAMPLES_FLOAT),
            uptime),
        pll.efc_history.fit_b);
    ptest_update();
  }
}

/* Once a second: takes a fit the curv task has done, and while
 * collecting sums the EFC less its temperature correction, in normal
 * PLL, in start-up PLL past settling, or while standby_from_unlock and the
 * switch word's bit 11 are set, and each 2700 seconds of uptime stores
 * their average as an entry of the history, the entry it replaces kept,
 * with its weight, time and log term, and signals the curv task
 * (0x4a588).  Unlike the 58503A's, it sums each sample less the first,
 * efc_history_base, and adds it back to the average, and a weight is
 * partial until 35100 s of uptime, not 10800; an interval with nothing
 * summed calls ptest_check. */
void efc_history_update(void) {
  /* Of an entry's samples, a quarter or more summed make its weight
   * more than partial, as does an uptime of 13 entries. */
  static const uint16_t QUARTER_SAMPLES = 675;
  static const uint32_t PARTIAL_UPTIME = 35100;
  static const float MINUS_ONE = -1.0f;
  char message[100];

  aging_fit_collect();
  switch (pll.aging_state) {
    case PLL_AGING_START:
      pll.aging_state = PLL_AGING_SETTLING;
      pll.predicted_uncertainty = PLL_AGING_UNFITTED_UNCERTAINTY;
      return;
    case PLL_AGING_SETTLING:
      if (pll.efc_history.loop_settled == 1) {
        pll.efc_history.next_entry_uptime = get_uptime() + PLL_AGING_SAMPLES;
        pll.aging_state = PLL_AGING_COLLECTING;
      }
      return;
    case PLL_AGING_COLLECTING:
      break;
    default:
      return;
  }

  float current = adc_average(ADC_OVEN_CURRENT);

  if (pll.stage == PLL_STAGE_NORMAL_PLL ||
      (standby_from_unlock() != 0 && switch_word_bit_11 != 0) ||
      (pll.stage == PLL_STAGE_STARTUP_PLL &&
       pll.startup_state != PLL_STARTUP_SETTLING)) {
    if (pll.efc_history.summed_count == 0) {
      efc_history_base = pll_efc_less_temperature(current);
      pll.efc_history.efc_sum = 0.0f;
    }
    float_add_in_place(
        &pll.efc_history.efc_sum,
        float_subtract(pll_efc_less_temperature(current), efc_history_base));
    pll.efc_history.summed_count++;
  } else if (pll.efc_history.summed_count == 0) {
    float_add_in_place(&pll.efc_history.efc_sum,
                       pll_efc_less_temperature(current));
  }
  pll.efc_history.sample_count++;

  uint32_t uptime = get_uptime();
  if (uptime < pll.efc_history.next_entry_uptime) {
    return;
  }
  pll.efc_history.next_entry_uptime += PLL_AGING_SAMPLES;

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
    ptest_check();
  } else {
    uint8_t entry = pll.efc_history.stop_ptr;

    pll.efc_history.replaced_time = pll.efc_history.times[entry];
    pll.efc_history.replaced_average = pll.efc_history.averages[entry];
    pll.efc_history.replaced_weight = pll.efc_history.weights[entry];
    if ((pll.efc_history.summed_count < QUARTER_SAMPLES &&
         pll.efc_history.summed_count != 0) ||
        get_uptime() < PARTIAL_UPTIME) {
      pll.efc_history.weights[entry] = PLL_AGING_WEIGHT_PARTIAL;
    } else if (pll.efc_history.summed_count == PLL_AGING_SAMPLES) {
      pll.efc_history.weights[entry] = PLL_AGING_WEIGHT_FULL;
    } else if (pll.efc_history.summed_count >= QUARTER_SAMPLES) {
      pll.efc_history.weights[entry] = PLL_AGING_WEIGHT_MOST;
    }
    pll.efc_history.averages[entry] = float_add(
        float_divide_reversed(unsigned_to_float(pll.efc_history.summed_count),
                              pll.efc_history.efc_sum),
        efc_history_base);
    float time = double_to_float(double_divide_reversed(
        PLL_AGING_SAMPLES_DOUBLE, unsigned_to_double(uptime)));

    pll.efc_history.times[entry] = time;
    pll.efc_history.logs[entry] =
        uptime <= PLL_AGING_SAMPLES ? MINUS_ONE : pll_aging_log_term(time);
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
  efc_history_base = 0.0f;
  pll.efc_history.sample_count = 0;
  pll.efc_history.summed_count = 0;
  pll.efc_history.efc_sum = 0.0f;
}

/* The means, Sxx, Sxy and Syy of the entries of the record's weight or
 * more, and from them the line, or with_slope 0 the mean, and its rms;
 * PLL_AGING_FIT_FAILED and zeros with two entries or fewer, or with
 * all their x the same (0x4b5d0). */
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
 * PLL_AGING_FIT_FAILED and zeros if two share an x or the points are
 * in line.  With the parabola's curvature zero it gives the line
 * through the outer two's crossing of zero instead, and fails if their
 * y are equal, which the 58503A's tests before any (0x4b35a). */
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
      pll_is_zero(numerator)) {
    fit->vertex_x = 0.0f;
    fit->vertex_y = 0.0f;
    return PLL_AGING_FIT_FAILED;
  }
  float curvature = float_divide(numerator, denominator);

  if (pll_is_zero(curvature)) {
    if (float_compare(y2, y0) == 0) {
      fit->vertex_x = 0.0f;
      fit->vertex_y = 0.0f;
      return PLL_AGING_FIT_FAILED;
    }
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
    float scaled = float_multiply(float_subtract(x1, fit->vertex_x), curvature);
    float squared = float_multiply(scaled, float_subtract(x1, fit->vertex_x));

    fit->vertex_y = float_subtract_reversed(squared, y1);
  }
  return 0;
}

/* For :KENneth:MEASured? and :HOLDover:TUNCertainty:MEASured?: the
 * measured time uncertainty and its resolution, and whether it is valid
 * (0x4bc0e). */
uint8_t kenneth_measured_get(float* value, int8_t* resolution) {
  static const int8_t RESOLUTION = -7;

  *resolution = RESOLUTION;
  *value = pll.measured_uncertainty;
  return pll.measured_uncertainty_valid;
}
