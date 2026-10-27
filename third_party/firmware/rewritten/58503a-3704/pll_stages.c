/* The disciplining loop's stages, each a state machine pllp_task steps
 * once a second: power-up with its coarse and fine frequency setting,
 * the start-up and normal loops, holdover and its recovery
 * (docs/firmware/loop.md; docs/firmware/structures.md, "The loop's
 * state").  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"; docs/firmware/modules.md,
 * `applications/pll/stages`).  The exported C includes this file after
 * its own, so the types and prototypes it declares are in scope; each
 * function here keeps its generated prototype.  The floating arithmetic
 * is calls to the image's soft-float routines (docs/firmware/
 * softfloat.md): float_divide_reversed(a, b) is b / a,
 * float_subtract_reversed(a, b) b - a, and double_compare(a, b) is
 * negative when b < a. */

/* A TI reading in seconds as the debug messages print it, in ns. */
static const float PLL_STAGES_NANOSECONDS = 1e+09f;

/* Readings to a block of the loops' and holdover's steering, and the
 * same as a float. */
static const uint8_t PLL_STAGES_BLOCK_SECONDS = 10;

static const float PLL_STAGES_BLOCK_LENGTH = 10.0f;

/* The aging fit's time unit, an EFC history entry's 2700 seconds. */
static const float PLL_STAGES_ENTRY_SECONDS = 2700.0f;

/* Reports the first satellite tracked, once; the 58503A logs the GPS
 * reference's validity instead (pll_stages_reference_logged). */
static void pll_stages_first_satellite(void) {
  uint8_t count;
  uint8_t pseudolite_count;

  if (first_satellite_logged != 0) {
    return;
  }
  uint16_t sr = set_interrupt_mask(CPU_IPL_1);

  if (satellites_in_use(&count, &pseudolite_count, NULL, NULL) == 0) {
    count = 0;
  }
  set_interrupt_mask(sr);
  if (count != 0) {
    status_happening(STATUS_FIRST_SATELLITE_SET);
    first_satellite_logged = 1;
  }
}

/* Set once the GPS reference's validity is logged, which powerup clears
 * as it starts; the export declares no object for it. */
static uint8_t* const PLL_STAGES_REFERENCE_LOGGED = (uint8_t*)0x102475;

/* Logs when the GPS reference became valid, once, with the GPS's date
 * and time ("GPS reference valid at", log_first_satellite). */
static void pll_stages_reference_logged(void) {
  if (*PLL_STAGES_REFERENCE_LOGGED != 0 || gps_reference_good == 0) {
    return;
  }
  log_first_satellite(oncore_date_pack(gps.status.date),
                      clock_time_bits(gps.status.time));
  *PLL_STAGES_REFERENCE_LOGGED = 1;
}

/* Readings before the clock is checked against GPS, as power-up and
 * holdover recovery count them, and between checks after a mismatch. */
static const uint8_t PLL_STAGES_CLOCK_CHECK_DELAY = 25;

static const uint8_t PLL_STAGES_CLOCK_RECHECK_DELAY = 2;

/* A measurement's failures, pll.fine_retries, past which coarse and fine
 * give up. */
static const uint16_t PLL_STAGES_MOST_RETRIES = 9;

/* Syncs the outputs to GPS when the TI passes the limit threshold or a
 * sync is asked for; whether it did (0x45082). */
uint8_t sync_perform(float ti) {
  if (pll.sync_requested == 0 &&
      float_compare(float_absolute_on_stack(ti), nv_settings.limit_threshold) <=
          0) {
    return 0;
  }
  sync_request(COUNTER_SYNC_FULL, 1);
  pll.sync_requested = 0;
  if (pll_debug_enabled != 0) {
    task_dmessage("Peforming sync\r\n");
  }
  return 1;
}

/* A failed measurement of fine's, its message printed: failed past the
 * most retries. */
static pll_stage_result pll_stages_fine_retry(char* message) {
  task_dmessage(message);
  pll.fine_retries++;
  return pll.fine_retries > PLL_STAGES_MOST_RETRIES ? PLL_STAGE_RESULT_FAILED
                                                    : PLL_STAGE_RESULT_RUNNING;
}

/* Starts fine's fit again from its first reading; the largest TI is
 * kept. */
static void pll_stages_fine_fit_restart(void) {
  pll.fine_index = 1;
  fine_sum_i = 0.0f;
  fine_sum_i_squared = 0.0f;
  fine_sum_ti = 0.0f;
  fine_sum_i_ti = 0.0f;
}

/* Slews the phase toward a TI: done within 100 ns, else fine_sync_adjust's
 * steps of 100 ns, one a second, or a slew again when it is 300 ns or
 * more off. */
static pll_stage_result pll_stages_fine_slew(float ti) {
  pll.fine_index = fine_sync_adjust(ti);
  if (pll.fine_index == 0) {
    return PLL_STAGE_RESULT_DONE;
  }
  if (pll.fine_index < PLL_STAGES_SYNC_TOO_FAR) {
    pll.fine_state = PLL_FINE_FINE_SLEW;
  } else {
    pll.fine_retries = 0;
    pll.fine_state = PLL_FINE_SLEW;
  }
  return PLL_STAGE_RESULT_RUNNING;
}

/* After the fit, the TI's change across the readings, per second, into
 * pll.fine_d.  Out of power-up, failed when the largest TI passes
 * holdover's limit, its threshold and its frequency tolerance over its
 * duration, unless the limit is off or the holdover long.  Then a
 * frequency within 2e-9 slews the phase; one within the frequency
 * tolerance, or any in a long holdover, moves the EFC by it and syncs
 * if the TI asks; another fails. */
static pll_stage_result pll_stages_fine_frequency(void) {
  static const float PER_SECOND = 0.02f;
  static const float SLEW_FREQUENCY = 2e-09f;
  bool long_holdover = (uint32_t)nv_settings.holdover_measurement_threshold <=
                       pll.holdover_duration;

  float_subtract_in_place(&pll.fine_d, double_to_float(interval_reading.value));
  float_multiply_in_place(&pll.fine_d, PER_SECOND);
  if (pll.stage != PLL_STAGE_POWERUP) {
    float limit =
        float_add(float_multiply(unsigned_to_float(pll.holdover_duration),
                                 pll.frequency_tolerance),
                  nv_settings.holdover_limit_threshold);

    if (float_compare(limit, fine_ti_max) < 0 && !long_holdover &&
        nv_settings.holdover_limit_state == HOLDOVER_LIMIT_ON) {
      task_dmessage("fine - ti too far off\r\n");
      return PLL_STAGE_RESULT_FAILED;
    }
  }
  if (float_compare(float_absolute_on_stack(fine_y0), SLEW_FREQUENCY) < 0) {
    return pll_stages_fine_slew(double_to_float(interval_reading.value));
  }
  if (float_compare(float_absolute_on_stack(fine_y0), pll.frequency_tolerance) >
          0 &&
      !long_holdover) {
    task_dmessage("fine - frequency too far off\r\n");
    return PLL_STAGE_RESULT_FAILED;
  }
  pll.efc = efc_clamp(
      float_add(float_multiply(fine_y0, efc_gains.reciprocal_fine), pll.efc));
  pll.frequency_adjusted = 1;
  if (pll_debug_enabled != 0) {
    char message[80];
    int32_t efc = float_to_integer(pll.efc);
    double y0 =
        float_to_double(float_multiply(fine_y0, PLL_STAGES_NANOSECONDS));

    debug_sprintf(message, "y0 = %f, efc =  %d\r\n", y0, efc);
    task_dmessage(message);
  }
  pll.fine_state = sync_perform(double_to_float(interval_reading.value))
                       ? PLL_FINE_SYNC_1
                       : PLL_FINE_SYNC_2;
  return PLL_STAGE_RESULT_RUNNING;
}

/* A loop block's mean into loop_steering.ti_sum and interval_mean;
 * false when no reading was summed, with a hold cause unless the
 * reference was questionable (measurement_failed): a TI error if a
 * reading failed (field_0f), else every one was past the hold
 * threshold. */
static bool pll_stages_loop_mean(void) {
  if (loop_steering.readings == 0) {
    loop_steering.ti_sum = 0.0f;
    interval_mean_flag = 0;
    if (loop_steering.measurement_failed == 0) {
      if (loop_steering.field_0f == 0) {
        pll.hold_causes |= HOLD_CAUSE_TI_LIMIT_EXCEEDED;
      } else {
        pll.hold_causes |= HOLD_CAUSE_TI_ERROR;
      }
    }
    return false;
  }
  float_divide_in_place(&loop_steering.ti_sum,
                        unsigned_to_float(loop_steering.readings));
  interval_mean_flag = 1;
  interval_mean = loop_steering.ti_sum;
  return true;
}

/* The loops' gains from a time constant: proportional_gain,
 * integral_gain and filter_weight of loop_steering_state. */
typedef struct pll_stages_gains {
  float proportional;
  float integral;
  float filter_weight;
} pll_stages_gains;

static const float PLL_STAGES_FILTER_SCALE = 29.75f;

static const float PLL_STAGES_INTEGRAL_SCALE = 4.0f;

static pll_stages_gains pll_stages_gains_for(float time_constant) {
  return (pll_stages_gains){
      .proportional = float_divide(efc_gains.reciprocal_ti, time_constant),
      .integral = float_divide(
          float_divide(float_divide(efc_gains.reciprocal_tau, time_constant),
                       time_constant),
          PLL_STAGES_INTEGRAL_SCALE),
      .filter_weight = float_divide(PLL_STAGES_FILTER_SCALE, time_constant),
  };
}

/* Steers the EFC from a block's mean: filters the TI less its setpoint
 * into pll.ti_filtered, integrates it with the aging fit's slope now,
 * b + c / y, bounded by the step limit, and sets the EFC from the
 * filtered TI, the base, the integral and the temperature
 * correction. */
static void pll_stages_steer(pll_stages_gains gains, float current) {
  static const float ONE = 1.0f;
  float error = float_subtract(loop_steering.ti_sum, ti_setpoint);

  pll.ti_filtered = float_add(
      float_multiply(float_subtract(ONE, gains.filter_weight), pll.ti_filtered),
      float_multiply(gains.filter_weight, error));
  float now = unsigned_to_float(get_uptime());
  float slope = float_add(
      float_divide(
          float_multiply(pll.efc_history.fit_c, PLL_STAGES_ENTRY_SECONDS), now),
      pll.efc_history.fit_b);

  if (float_compare(slope, efc_gains.step_limit) > 0) {
    slope = efc_gains.step_limit;
  } else if (float_compare(pll_stages_negate(efc_gains.step_limit), slope) >
             0) {
    slope = pll_stages_negate(efc_gains.step_limit);
  }
  float drift = float_add(float_divide(float_divide(slope, gains.integral),
                                       PLL_STAGES_ENTRY_SECONDS),
                          pll.ti_filtered);

  float_add_in_place(&loop_steering.integral,
                     float_multiply(float_multiply(drift, gains.integral),
                                    PLL_STAGES_BLOCK_LENGTH));
  float efc =
      float_add(float_add(float_multiply(gains.proportional, pll.ti_filtered),
                          pll.efc_base),
                loop_steering.integral);

  pll.efc = efc_clamp(float_add(
      efc,
      float_multiply(scpi_value_DIAGnostic_ROSCillator_TCOefficient, current)));
}

/* Ends a loop block: prints the EFC, the block's mean TI, the time
 * constant loop_time and the current, then starts the next block. */
static void pll_stages_block_end(float current, float loop_time) {
  if (pll_debug_enabled != 0) {
    char message[100];
    uint32_t date;
    time_of_day time;

    get_date_time(&date, &time);
    double current_double = float_to_double(current);
    int32_t loop = float_to_integer(loop_time);
    double ti = float_to_double(
        float_multiply(loop_steering.ti_sum, PLL_STAGES_NANOSECONDS));

    debug_sprintf(
        message,
        "%d/%d/%d %02d:%02d:%02d efc= %d ti= %.1f loop= %d curr= %.2f\r\n",
        (uint32_t)clock_middle(date), (uint32_t)clock_low(date),
        (uint32_t)clock_year(date), (uint32_t)time.hour, (uint32_t)time.minute,
        (uint32_t)time.second, float_to_integer(pll.efc), ti, loop,
        current_double);
    task_dmessage(message);
  }
  loop_steering.ti_sum = 0.0f;
  loop_steering.readings = 0;
  loop_steering.seconds = 0;
  loop_steering.measurement_failed = 0;
  loop_steering.field_0f = 0;
}

/* The loop's steps from a time constant to the next: integral + the
 * filtered TI's change of scale, 1 / (tau - 5) - 1 / tau. */
static void pll_stages_rescale_integral(float previous_tau) {
  static const float ONE = 1.0f;
  static const float MINUS_ONE = -1.0f;
  float scale = float_add(float_divide_reversed(previous_tau, ONE),
                          float_divide(MINUS_ONE, pll.loop_time_constant));

  float_add_in_place(
      &loop_steering.integral,
      float_multiply(float_multiply(efc_gains.reciprocal_ti, pll.ti_filtered),
                     scale));
}

/* Whether the TI's least-squares slope over 10 readings or more, as an
 * EFC correction, leaves the EFC in its range; nothing calls it
 * (0x43888). */
uint8_t holdover_correction_valid(void) {
  /* 10 readings' sum of indexes, and 10 times their sum of squares
   * less its square. */
  static const float READINGS = 10.0f;
  static const float INDEX_SUM = 45.0f;
  static const float SLOPE_DENOMINATOR = 825.0f;
  static const float EFC_LEAST = 0.0f;
  static const int32_t FEWEST_READINGS = 10;
  float slope = float_divide(
      float_subtract(
          float_multiply(holdover_steering.ti_weighted_sum, READINGS),
          float_multiply(holdover_steering.ti_sum, INDEX_SUM)),
      SLOPE_DENOMINATOR);
  float efc = float_add(float_divide(slope, efc_gains.gain), pll.efc);

  return float_compare(efc, EFC_LEAST) >= 0 &&
         float_compare(PLL_EFC_MOST, efc) >= 0 &&
         holdover_steering.ti_count >= FEWEST_READINGS;
}

/* The EFC the aging fit predicts at the uptime, with y in entries,
 * a + b y, and c ln y past the first entry, and the temperature
 * correction. */
static float pll_stages_predicted_efc(float current) {
  static const double ENTRY_SECONDS_DOUBLE = 2700.0;
  static const float ONE = 1.0f;
  float age = double_to_float(double_divide_reversed(
      ENTRY_SECONDS_DOUBLE, float_to_double(unsigned_to_float(get_uptime()))));

  if (float_compare(age, ONE) <= 0) {
    float line = float_add(float_multiply(pll.efc_history.fit_b, age),
                           pll.efc_history.fit_a);

    return float_add(
        line, float_multiply(scpi_value_DIAGnostic_ROSCillator_TCOefficient,
                             current));
  }
  double log_term = double_multiply(log(float_to_double(age)),
                                    float_to_double(pll.efc_history.fit_c));
  float line = float_add(float_multiply(pll.efc_history.fit_b, age),
                         pll.efc_history.fit_a);
  double fitted = double_add(float_to_double(line), log_term);
  float correction =
      float_multiply(scpi_value_DIAGnostic_ROSCillator_TCOefficient, current);

  return double_to_float(double_add(float_to_double(correction), fitted));
}

/* fine's readings to its fit, and where it keeps them, from 0x102390:
 * the export declares no object for them. */
#define PLL_STAGES_FINE_READINGS 50

static float* const pll_stages_fine_readings = (float*)0x102390;

/* Adds reading i, pll.fine_d, to fine's least-squares line through the
 * readings against their index, TI = x0 + y0 i: its sums, then the slope
 * y0, the fractional frequency (0 when the sums give no slope), and the
 * intercept x0; the largest TI magnitude kept in fine_ti_max. */
static void pll_stages_fine_fit_add(void) {
  uint8_t i = pll.fine_index;

  if (float_compare(float_absolute_on_stack(pll.fine_d), fine_ti_max) > 0) {
    fine_ti_max = float_absolute_on_stack(pll.fine_d);
  }
  pll_stages_fine_readings[i - 1] = pll.fine_d;
  float_add_in_place(&fine_sum_i, integer_to_float(i));
  float_add_in_place(&fine_sum_i_squared, integer_to_float(i * i));
  float_add_in_place(&fine_sum_ti, pll.fine_d);
  float_add_in_place(&fine_sum_i_ti,
                     float_multiply(unsigned_to_float(i), pll.fine_d));
  float n_sum_i_squared =
      float_multiply(unsigned_to_float(i), fine_sum_i_squared);

  fine_y0 =
      float_subtract(float_multiply(fine_sum_i, fine_sum_i), n_sum_i_squared);
  if (pll_is_zero(fine_y0)) {
    fine_y0 = 0.0f;
  } else {
    float n_sum_i_ti = float_multiply(unsigned_to_float(i), fine_sum_i_ti);

    fine_y0 = float_divide(
        float_subtract(float_multiply(fine_sum_i, fine_sum_ti), n_sum_i_ti),
        fine_y0);
  }
  float sum_ti_less_slope =
      float_subtract_reversed(float_multiply(fine_sum_i, fine_y0), fine_sum_ti);

  fine_x0 = float_divide(sum_ti_less_slope, unsigned_to_float(i));
  if (pll_debug_enabled != 0) {
    char message[80];
    double x0 = float_to_double(fine_x0);
    double y0 = float_to_double(fine_y0);
    double ti = double_multiply((double)PLL_STAGES_NANOSECONDS,
                                float_to_double(pll.fine_d));

    debug_sprintf(message, "i= %d, ti= %.1f, y0= %.2e, x0= %.2e\r\n",
                  (uint32_t)i, ti, y0, x0);
    task_dmessage(message);
  }
}

/* After the fit's readings, their mean as interval_mean, and whether the
 * first lies within 300 ns of the line; measures again, a retry counted,
 * if not.  The image's check is a loop over the readings that leaves
 * after the first either way, so only the first is checked. */
static void pll_stages_fine_fit_check(void) {
  static const float BAD_TI = 3e-07f;

  interval_mean =
      float_divide_reversed(unsigned_to_float(pll.fine_index), fine_sum_ti);
  interval_mean_flag = 1;
  pll.fine_index = 1;
  float fitted = float_add(
      float_multiply(unsigned_to_float(pll.fine_index), fine_y0), fine_x0);
  float residual = float_subtract_reversed(
      fitted, pll_stages_fine_readings[pll.fine_index - 1]);

  if (float_compare(float_absolute_on_stack(residual), BAD_TI) > 0) {
    task_dmessage("Bad TI in frequency measurement\r\n");
    pll.fine_state = PLL_FINE_MEASURE_1;
    pll.fine_retries++;
  }
}

/* The coarse frequency setting: syncs the counter, then measures the TI
 * across a second and moves the EFC by the change until it is within
 * 100 ns (0x4530a). */
pll_stage_result coarse(void) {
  static const float CLOSE_ENOUGH = 1e-07f;
  pll_stage_result result = PLL_STAGE_RESULT_RUNNING;

  switch (pll.coarse_state) {
    case PLL_COARSE_SETTLE:
      interval_next_reading(INTERVAL_ARM_PLAIN, NULL);
      pll.coarse_state = PLL_COARSE_CALIBRATE;
      break;
    case PLL_COARSE_CALIBRATE:
      interval_next_reading(INTERVAL_ARM_CALIBRATE, NULL);
      pll.coarse_state = PLL_COARSE_FIRST;
      pll.fine_retries = 0;
      break;
    case PLL_COARSE_FIRST:
      if (interval_next_reading(INTERVAL_ARM_CALIBRATE, &pll.fine_d) == 0) {
        task_dmessage("coarse - Error with measurement 1\r\n");
        pll.fine_retries++;
        if (pll.fine_retries > PLL_STAGES_MOST_RETRIES) {
          result = PLL_STAGE_RESULT_FAILED;
        }
      } else {
        pll.coarse_state = PLL_COARSE_SECOND;
      }
      break;
    case PLL_COARSE_SECOND:
      interval_wait_reading();
      if (interval_reading.valid == 0) {
        task_dmessage("coarse - Error with measurement 2\r\n");
        pll.fine_retries++;
        if (pll.fine_retries <= PLL_STAGES_MOST_RETRIES) {
          pll.coarse_state = PLL_COARSE_FIRST;
          result = PLL_STAGE_RESULT_RUNNING;
        } else {
          result = PLL_STAGE_RESULT_FAILED;
        }
        interval_rearm(INTERVAL_ARM_CALIBRATE);
        break;
      }
      float_subtract_in_place(&pll.fine_d,
                              double_to_float(interval_reading.value));
      if (float_compare(CLOSE_ENOUGH, pll.fine_d) > 0 &&
          float_compare(pll.fine_d, pll_stages_negate(CLOSE_ENOUGH)) > 0) {
        result = PLL_STAGE_RESULT_DONE;
        interval_rearm(INTERVAL_ARM_CALIBRATE);
        break;
      }
      pll.efc = efc_clamp(float_subtract_reversed(
          float_multiply(pll.fine_d, efc_gains.reciprocal_fine), pll.efc));
      if (pll_debug_enabled != 0) {
        char message[80];
        int32_t efc = float_to_integer(pll.efc);
        double d =
            float_to_double(float_multiply(pll.fine_d, PLL_STAGES_NANOSECONDS));

        debug_sprintf(message, "d = %f, efc =  %d\r\n", d, efc);
        task_dmessage(message);
      }
      interval_rearm(INTERVAL_ARM_SYNC);
      pll.coarse_state = PLL_COARSE_SETTLE;
      break;
    case PLL_COARSE_SYNC:
    default:
      pll.coarse_state = PLL_COARSE_SYNC;
      interval_next_reading(INTERVAL_ARM_SYNC, NULL);
      pll.coarse_state = PLL_COARSE_SETTLE;
      break;
  }
  if (result == PLL_STAGE_RESULT_FAILED) {
    pll.hold_causes |= HOLD_CAUSE_TI_ERROR;
  }
  return result;
}

/* The fine frequency setting: syncs the outputs if the TI asks, fits a
 * line to 50 readings for the frequency, their mean the interval's,
 * then moves the EFC by it and measures again, or slews the phase once
 * the frequency is within 2e-9; a sync asked for starts it again but
 * while it fine-slews (0x454d4). */
pll_stage_result fine(void) {
  pll_stage_result result = PLL_STAGE_RESULT_RUNNING;

  if (pll.sync_requested != 0 && pll.fine_state != PLL_FINE_FINE_SLEW) {
    pll.fine_state = PLL_FINE_SYNC_MEASURE;
  }
  switch (pll.fine_state) {
    case PLL_FINE_SYNC_MEASURE:
      interval_wait_reading();
      if (interval_reading.valid == 0) {
        result =
            pll_stages_fine_retry("fine-sync - Error with measurement\r\n");
      } else {
        pll.fine_retries = 0;
        pll.fine_state = sync_perform(double_to_float(interval_reading.value))
                             ? PLL_FINE_SYNC_1
                             : PLL_FINE_MEASURE_1;
      }
      interval_rearm(INTERVAL_ARM_CALIBRATE);
      break;
    case PLL_FINE_SYNC_1:
      interval_next_reading(INTERVAL_ARM_PLAIN, NULL);
      pll.fine_state = PLL_FINE_SYNC_2;
      break;
    case PLL_FINE_SYNC_2:
      interval_next_reading(INTERVAL_ARM_CALIBRATE, NULL);
      pll.fine_state = PLL_FINE_MEASURE_1;
      break;
    case PLL_FINE_MEASURE_1:
      interval_next_reading(INTERVAL_ARM_PLAIN, NULL);
      pll.fine_state = PLL_FINE_MEASURE_DELAY;
      pll.fine_retries = 0;
      pll_stages_fine_fit_restart();
      fine_ti_max = 0.0f;
      break;
    case PLL_FINE_MEASURE_DELAY:
      if (pll.fine_index > PLL_STAGES_FINE_READINGS) {
        interval_next_reading(INTERVAL_ARM_CALIBRATE, NULL);
        pll.fine_state = PLL_FINE_MEASURE_2;
        pll_stages_fine_fit_check();
      } else if (interval_next_reading(INTERVAL_ARM_CALIBRATE, &pll.fine_d) ==
                 0) {
        result = pll_stages_fine_retry("fine - Error with measurement 1\r\n");
        pll_stages_fine_fit_restart();
      } else {
        pll_stages_fine_fit_add();
        pll.fine_index++;
      }
      break;
    case PLL_FINE_MEASURE_2:
      interval_wait_reading();
      if (interval_reading.valid == 0) {
        result = pll_stages_fine_retry("fine - Error with measurement 2\r\n");
        if (result != PLL_STAGE_RESULT_FAILED) {
          pll.fine_state = PLL_FINE_MEASURE_1;
        }
        interval_rearm(INTERVAL_ARM_CALIBRATE);
        break;
      }
      result = pll_stages_fine_frequency();
      interval_rearm(INTERVAL_ARM_PLAIN);
      break;
    case PLL_FINE_SLEW:
      interval_next_reading(INTERVAL_ARM_CALIBRATE, NULL);
      pll.fine_retries = 0;
      pll.fine_state = PLL_FINE_SLEW_MEASURE;
      break;
    case PLL_FINE_SLEW_MEASURE:
      interval_wait_reading();
      if (interval_reading.valid == 0) {
        result =
            pll_stages_fine_retry("fine-slew - Error with measurement\r\n");
      } else {
        result = pll_stages_fine_slew(double_to_float(interval_reading.value));
      }
      interval_rearm(INTERVAL_ARM_CALIBRATE);
      break;
    case PLL_FINE_FINE_SLEW:
      interval_next_reading(INTERVAL_ARM_PLAIN, NULL);
      pll.fine_index--;
      if (pll.fine_index == 0) {
        result = PLL_STAGE_RESULT_DONE;
      }
      break;
    case PLL_FINE_START:
    default:
      interval_next_reading(INTERVAL_ARM_CALIBRATE, NULL);
      pll.fine_retries = 0;
      pll.fine_state = PLL_FINE_SYNC_MEASURE;
      break;
  }
  if (result == PLL_STAGE_RESULT_FAILED) {
    pll.hold_causes |= HOLD_CAUSE_TI_ERROR;
  }
  return result;
}

/* The hold causes that keep power-up waiting for GPS: the temporary mode
 * and the EFC control mode. */
static const pll_hold_cause PLL_STAGES_POWERUP_HOLDS =
    HOLD_CAUSE_EFC_CONTROL | HOLD_CAUSE_TEMPORARY_MODE;

/* The power-up stage: clears the EFC history, waits for the ovens and
 * for GPS, sets the frequency coarsely and finely and the clock from
 * GPS, then waits out a near leap second and the leap second's
 * acknowledgment; back to waiting for GPS while it is lost, or while the
 * temporary or EFC control mode holds it (0x46072).  Unlike the
 * Z3805A's it leaves the outer oven's enable bit to the GPT's set-up,
 * and logs the GPS reference's validity, not the first satellite. */
pll_stage_result powerup(bool same_stage) {
  static const float COARSE_TFOM = 0.001f;
  static const float FINE_TFOM = 2e-06f;
  pll_stage_result result = PLL_STAGE_RESULT_RUNNING;

  if (!same_stage || (int8_t)pll.powerup_state > PLL_POWERUP_LEAP_SECONDS) {
    memset(&pll.efc_history, 0, sizeof pll.efc_history);
    pll.aging_state = PLL_AGING_START;
    pll.efc_history.loop_settled = 0;
    first_satellite_logged = 0;
    *PLL_STAGES_REFERENCE_LOGGED = 0;
    pll.powerup_state = PLL_POWERUP_START;
  }
  switch (pll.powerup_state) {
    case PLL_POWERUP_INNER_OVEN:
      interval_next_reading(INTERVAL_ARM_PLAIN, NULL);
      if (oven_warmed_up()) {
        pll.powerup_state = PLL_POWERUP_OUTER_OVEN;
        outer_oven_enabled = 1;
      }
      break;
    case PLL_POWERUP_OUTER_OVEN:
      interval_next_reading(INTERVAL_ARM_PLAIN, NULL);
      pll.powerup_state = PLL_POWERUP_WAITING_FOR_GPS;
      status_happening(STATUS_OVEN_WARMED_SET);
      break;
    case PLL_POWERUP_WAITING_FOR_GPS:
      interval_next_reading(INTERVAL_ARM_PLAIN, NULL);
      pll.powerup_reference_lost = 0;
      if (gps_reference_good != 0 && internal_pps_dead == 0 &&
          (pll.hold_causes & PLL_STAGES_POWERUP_HOLDS) == HOLD_CAUSE_NONE &&
          (int8_t)pll.gps_init_state > PLL_GPS_INIT_HOLD_OFF_ACK) {
        pll.powerup_state = PLL_POWERUP_COARSE;
        pll.coarse_state = PLL_COARSE_SYNC;
      }
      break;
    case PLL_POWERUP_COARSE: {
      pll_stage_result coarse_result = coarse();

      if (coarse_result == PLL_STAGE_RESULT_DONE) {
        pll.tfom = COARSE_TFOM;
        pll.powerup_state = PLL_POWERUP_FINE;
        pll.fine_state = PLL_FINE_START;
      } else if (coarse_result == PLL_STAGE_RESULT_FAILED) {
        result = PLL_STAGE_RESULT_FAILED;
      }
      break;
    }
    case PLL_POWERUP_FINE: {
      pll_stage_result fine_result = fine();

      if (fine_result == PLL_STAGE_RESULT_DONE) {
        pll.tfom = FINE_TFOM;
        if (gps_reference_good != 0 && internal_pps_dead == 0) {
          clock_set(gps.status.time, oncore_date_pack(gps.status.date));
          status_happening(STATUS_DATE_TIME_VALID_SET);
          if (leap_second_near() == 0) {
            pll.powerup_state = PLL_POWERUP_LEAP_SECONDS;
            pll.leap_ack_requested = 1;
          } else {
            pll.powerup_countdown = PLL_STAGES_CLOCK_CHECK_DELAY;
            pll.powerup_state = PLL_POWERUP_WAITING;
          }
        }
      } else if (fine_result == PLL_STAGE_RESULT_FAILED) {
        result = PLL_STAGE_RESULT_FAILED;
      }
      break;
    }
    case PLL_POWERUP_WAITING:
      interval_next_reading(INTERVAL_ARM_PLAIN, NULL);
      pll.powerup_countdown--;
      if (pll.powerup_countdown == 0 && leap_ack_pending() == 0) {
        if ((char)clock_check_against_gps_58503a() == 0 &&
            clock_gps_mismatch != 0) {
          pll.powerup_countdown = PLL_STAGES_CLOCK_RECHECK_DELAY;
        } else {
          pll.powerup_state = PLL_POWERUP_LEAP_SECONDS;
          pll.leap_ack_requested = 1;
        }
      }
      break;
    case PLL_POWERUP_LEAP_SECONDS:
      interval_next_reading(INTERVAL_ARM_PLAIN, NULL);
      if (pll.leap_ack_requested == 0 && pll.leap_ack_done != 0) {
        result = PLL_STAGE_RESULT_DONE;
      }
      break;
    case PLL_POWERUP_START:
    default:
      interval_rearm(INTERVAL_ARM_PLAIN);
      pll.powerup_state = PLL_POWERUP_INNER_OVEN;
      break;
  }
  pll_stages_first_satellite();
  pll_stages_reference_logged();
  if ((int8_t)pll.powerup_state > PLL_POWERUP_WAITING_FOR_GPS &&
      (gps_reference_good == 0 || internal_pps_dead != 0 ||
       (pll.hold_causes & PLL_STAGES_POWERUP_HOLDS) != HOLD_CAUSE_NONE)) {
    pll.powerup_reference_lost = 1;
    interval_mean_flag = 0;
  }
  if (pll.powerup_reference_lost != 0 &&
      (pll.powerup_state != PLL_POWERUP_LEAP_SECONDS ||
       pll.leap_ack_done != 0)) {
    pll.powerup_state = PLL_POWERUP_WAITING_FOR_GPS;
    result = PLL_STAGE_RESULT_RUNNING;
  }
  return result;
}

/* A loop's one-second reading, valid or not, as startup_pll and
 * pll_normal take it.  A failed one, while the reference is
 * questionable, marks the block measurement_failed, and with both 1PPS
 * good and the reading not replaced by a wait is a measurement failure
 * (field_0f), failure_message printed.  A good one with GPS, not
 * questionable, is summed into the block within the hold threshold, else
 * printed; questionable, it marks the block.  Then, with GPS, not
 * questionable, and no leap second acknowledgment pending, the clock is
 * checked against GPS at second 14 or after a mismatch, and a check that
 * has just set the clock, a second mismatch in a row (a 0 result with
 * clock_gps_mismatch cleared), asks gps_leap_ack to measure GPS-UTC
 * again. */
static void pll_stages_loop_reading(uint8_t valid, float ti,
                                    char* failure_message) {
  static const uint32_t CHECK_SECOND = 14;
  static const uint32_t SECOND_MASK = 0xff;

  if (valid == 0) {
    if (gps_reference_questionable != 0) {
      loop_steering.measurement_failed = 1;
    }
    if (internal_pps_dead == 0 && gps_pps_dead == 0 &&
        *PLL_READING_REPLACED == 0) {
      task_dmessage(failure_message);
      loop_steering.field_0f = 1;
    }
    return;
  }
  if (gps_reference_good != 0) {
    if (gps_reference_questionable != 0) {
      loop_steering.measurement_failed = 1;
    } else if (float_compare(float_absolute_on_stack(ti),
                             nv_settings.holdover_limit_threshold) < 0 ||
               nv_settings.holdover_limit_state != HOLDOVER_LIMIT_ON) {
      float_add_in_place(&loop_steering.ti_sum, ti);
      loop_steering.readings++;
    } else if (debug_enabled != 0) {
      char message[100];
      uint32_t date;
      time_of_day time;

      get_date_time(&date, &time);
      debug_sprintf(
          message, "%d/%d/%d %02d:%02d:%02d TI result: %.1fns\r\n",
          (uint32_t)clock_middle(date), (uint32_t)clock_low(date),
          (uint32_t)clock_year(date), (uint32_t)time.hour,
          (uint32_t)time.minute, (uint32_t)time.second,
          float_to_double(float_multiply(ti, PLL_STAGES_NANOSECONDS)));
      task_dmessage(message);
    }
  }
  if (gps_reference_good != 0 && leap_ack_pending() == 0 &&
      gps_reference_questionable == 0 &&
      (clock_gps_mismatch != 0 ||
       (get_gps_time() & SECOND_MASK) == CHECK_SECOND) &&
      (char)clock_check_against_gps_58503a() == 0 && clock_gps_mismatch == 0) {
    pll.leap_ack_requested = 1;
  }
}

/* Starts a loop's block afresh. */
static void pll_stages_loop_start(void) {
  loop_steering.ti_sum = 0.0f;
  loop_steering.readings = 0;
  loop_steering.seconds = 0;
  loop_steering.measurement_failed = 0;
  loop_steering.field_0f = 0;
}

/* The start-up loop: a proportional-integral loop on 10-second blocks
 * whose time constant grows 5 s a block to max_loop_time, settling once
 * 15 blocks in a row are within 150 ns and the EFC history has its
 * first entries; done at the full time constant.  A block with no
 * reading summed steers nothing (0x4429e). */
pll_stage_result startup_pll(bool same_stage) {
  static const int32_t SETTLED_BLOCKS = 15;
  static const double SETTLED_TI = 1.5e-07;
  static const float SETTLED_TFOM = 1e-06f;
  static const uint8_t FIRST_ENTRIES = 3;
  static const float TAU_STEP = 5.0f;
  static const float MINUS_TAU_STEP = -5.0f;
  pll_stage_result result = PLL_STAGE_RESULT_RUNNING;
  float ti;
  float current = adc_average(ADC_OVEN_CURRENT);

  if (!same_stage || (int8_t)pll.startup_state > PLL_STARTUP_LENGTHENING) {
    pll.frequency_adjusted = 0;
    pll.ti_filtered = 0.0f;
    loop_steering.integral = 0.0f;
    pll.efc_base = pll_efc_less_temperature(current);
    pll_stages_loop_start();
    loop_steering.large_offset_blocks = 0;
    interval_next_reading(INTERVAL_ARM_CALIBRATE, NULL);
    pll.startup_state = PLL_STARTUP_SETTLING;
    return pll_stages_check_causes(result);
  }
  uint8_t valid = interval_next_reading(INTERVAL_ARM_CALIBRATE, &ti);

  pll_stages_loop_reading(valid, ti,
                          "startup_pll - Error with measurement\r\n");
  loop_steering.seconds++;
  if (loop_steering.seconds < PLL_STAGES_BLOCK_SECONDS) {
    return pll_stages_check_causes(result);
  }
  if (pll_stages_loop_mean()) {
    switch (pll.startup_state) {
      case PLL_STARTUP_SETTLING:
        if (loop_steering.large_offset_blocks < SETTLED_BLOCKS) {
          if (double_compare(float_to_double(
                                 float_absolute_on_stack(loop_steering.ti_sum)),
                             SETTLED_TI) <= 0) {
            loop_steering.large_offset_blocks = 0;
          } else {
            loop_steering.large_offset_blocks++;
          }
        } else if (pll.aging_state == PLL_AGING_COLLECTING) {
          pll.startup_state = PLL_STARTUP_LENGTHENING;
        } else {
          pll.tfom = SETTLED_TFOM;
          pll.efc_history.loop_settled = 1;
          pll.startup_state = PLL_STARTUP_WAITING_FOR_HISTORY;
        }
        break;
      case PLL_STARTUP_WAITING_FOR_HISTORY:
        if (pll.efc_history.stop_ptr == FIRST_ENTRIES &&
            pll.efc_history.start_ptr == 0) {
          pll.startup_state = PLL_STARTUP_LENGTHENING;
        }
        break;
      case PLL_STARTUP_LENGTHENING:
        if (float_compare(float_add(max_loop_time, MINUS_TAU_STEP),
                          pll.loop_time_constant) <= 0) {
          pll.loop_time_constant = max_loop_time;
          pll_stages_rescale_integral(float_add(max_loop_time, MINUS_TAU_STEP));
          result = PLL_STAGE_RESULT_DONE;
        } else {
          float_add_in_place(&pll.loop_time_constant, TAU_STEP);
          pll_stages_rescale_integral(
              float_add(pll.loop_time_constant, MINUS_TAU_STEP));
        }
        break;
      default:
        break;
    }
    pll_stages_steer(pll_stages_gains_for(pll.loop_time_constant), current);
  }
  pll_stages_block_end(current, pll.loop_time_constant);
  return pll_stages_check_causes(result);
}

/* The normal loop: the start-up loop's at max_loop_time, its gains kept
 * in loop_steering (0x4495a). */
pll_stage_result pll_normal(bool same_stage) {
  static const float NORMAL_TFOM = 1e-06f;
  static const float FREQUENCY_TOLERANCE = 1e-08f;
  float ti;
  float current = adc_average(ADC_OVEN_CURRENT);

  if (!same_stage || (int8_t)pll.normal_state > PLL_NORMAL_RUNNING) {
    pll.frequency_adjusted = 0;
    pll.tfom = NORMAL_TFOM;
    pll_stages_loop_start();
    interval_next_reading(INTERVAL_ARM_CALIBRATE, NULL);
    pll.normal_state = PLL_NORMAL_RUNNING;
    pll_stages_gains gains = pll_stages_gains_for(max_loop_time);

    loop_steering.proportional_gain = gains.proportional;
    loop_steering.integral_gain = gains.integral;
    loop_steering.filter_weight = gains.filter_weight;
    loop_steering.integral = 0.0f;
    pll.efc_base = pll_efc_less_temperature(current);
    pll.frequency_tolerance = FREQUENCY_TOLERANCE;
  } else {
    uint8_t valid = interval_next_reading(INTERVAL_ARM_CALIBRATE, &ti);

    pll_stages_loop_reading(valid, ti,
                            "pll_normal - Error with measurement\r\n");
    loop_steering.seconds++;
    if (loop_steering.seconds >= PLL_STAGES_BLOCK_SECONDS) {
      if (pll_stages_loop_mean()) {
        pll_stages_gains gains = {
            .proportional = loop_steering.proportional_gain,
            .integral = loop_steering.integral_gain,
            .filter_weight = loop_steering.filter_weight,
        };

        pll_stages_steer(gains, current);
      }
      pll_stages_block_end(current, max_loop_time);
    }
  }
  return pll_stages_check_causes(PLL_STAGE_RESULT_RUNNING);
}

/* 0 when, in a holdover younger than the measurement threshold and with
 * no frequency correction applied, the TI ti has reached the hold
 * limit, the threshold plus the tolerance a second; else 1 (0x43912). */
uint32_t holdover_within_limit(float ti) {
  if (pll.holdover_duration <=
      (uint32_t)nv_settings.holdover_measurement_threshold) {
    float limit =
        float_add(float_multiply(unsigned_to_float(pll.holdover_duration),
                                 pll.frequency_tolerance),
                  nv_settings.holdover_limit_threshold);

    if (float_compare(limit, float_absolute_on_stack(ti)) <= 0 &&
        pll.frequency_adjusted == 0) {
      return 0;
    }
  }
  return 1;
}

/* Counts a second of holdover, and reports it past the status
 * threshold, once, with the PWMA output set high (0x43a7c). */
void holdover_duration_count(void) {
  pll.holdover_duration++;
  if (pll.holdover_duration > (uint32_t)nv_settings.holdover_status_threshold &&
      holdover_duration_exceeded == 0) {
    holdover_duration_exceeded = 1;
    status_happening(STATUS_HOLDOVER_THRESHOLD_SET);
    pwma_force_level(1);
  }
}

/* The log event for holdover on the GPS's loss, by the GPS reference's
 * state (the jump table at 0x439e4); any other state is the GPS's 1PPS,
 * dead or lost. */
static log_event_code pll_stages_gps_holdover_event(void) {
  switch (gps.references[0].state) {
    case GPS_SYNC_RECEIVER_FAILED:
      return LOG_HOLDOVER_GPS_ALARM;
    case GPS_SYNC_NOT_TRACKING:
    case GPS_SYNC_INACCURATE_POSITION:
      return LOG_HOLDOVER_NOT_TRACKING;
    case GPS_SYNC_RAIM_ERROR:
      return LOG_HOLDOVER_RAIM_ALARM;
    default:
      return gps_pps_dead != 0 ? LOG_HOLDOVER_INVALID_GPS_PPS
                               : LOG_HOLDOVER_GPS;
  }
}

/* The log event for holdover by its first hold cause in the image's
 * order. */
static log_event_code pll_stages_holdover_event(void) {
  pll_hold_cause causes = pll.hold_causes;

  if ((causes & HOLD_CAUSE_MANUAL) != HOLD_CAUSE_NONE) {
    return LOG_HOLDOVER_MANUAL;
  }
  if ((causes & HOLD_CAUSE_GPS) != HOLD_CAUSE_NONE) {
    return pll_stages_gps_holdover_event();
  }
  if ((causes & HOLD_CAUSE_TI_ERROR) != HOLD_CAUSE_NONE) {
    return LOG_HOLDOVER_TI_ERROR;
  }
  if ((causes & HOLD_CAUSE_TI_LIMIT_EXCEEDED) != HOLD_CAUSE_NONE) {
    return LOG_HOLDOVER_TI_LIMIT;
  }
  if ((causes & HOLD_CAUSE_TEMPORARY_MODE) != HOLD_CAUSE_NONE) {
    return LOG_HOLDOVER_TEMPORARY;
  }
  if ((causes & HOLD_CAUSE_HARDWARE_FAILURE) != HOLD_CAUSE_NONE) {
    return LOG_HOLDOVER_HW_ERROR;
  }
  if ((causes & HOLD_CAUSE_1PPS_SELECTION) != HOLD_CAUSE_NONE) {
    return LOG_HOLDOVER_SELECTING_PPS;
  }
  if ((causes & HOLD_CAUSE_GPS_COMMAND_MONITOR) != HOLD_CAUSE_NONE) {
    return LOG_HOLDOVER_GPS_ALARM;
  }
  return LOG_HOLDOVER_STARTED;
}

/* Enters holdover: its duration restarted but from recovery, manual
 * when a manual hold cause or no automatic recovery asks it, a new
 * holdover logged by its cause, and the interval's mean dropped for any
 * cause but the manual one (0x4396e). */
void enter_holdover(void) {
  if (pll.stage != PLL_STAGE_HOLDOVER_RECOVERY) {
    pll.holdover_duration = 0;
  }
  pll.manual = (pll.hold_causes & HOLD_CAUSE_MANUAL) != HOLD_CAUSE_NONE ||
               holdover_recovery_auto == 0;
  pll.stage = PLL_STAGE_HOLDOVER;
  if (pll.holdover_duration == 0) {
    log_event_58503a(pll_stages_holdover_event());
  }
  if ((pll.hold_causes & ~HOLD_CAUSE_MANUAL) != HOLD_CAUSE_NONE) {
    interval_mean_flag = 0;
  }
}

/* The holdover stage: steers the EFC by the aging fit, and each 10
 * seconds the TFOM from the HQ and the holdover's age, and the hold
 * causes from the TI's block: done once none is left and the holdover
 * is not manual (0x43ab8).  Unlike the Z3805A's it leaves
 * pll.ti_limit_override to holdover_recovery, prints no measurement
 * error for a reading replaced by a wait, and takes the block's mean as
 * the interval's only from a full block. */
pll_stage_result holdover(bool same_stage) {
  static const float AGING_LIMIT = 2e-09f;
  static const float AGING_LIMIT_PER_SECOND = 2.3148148e-14f;
  static const float TWO = 2.0f;
  static const float HALF = 0.5f;
  static const float SECONDS_PER_DAY_SQUARED = 7.46496e+09f;
  static const float LEAST_TFOM = 1e-06f;
  pll_stage_result result = PLL_STAGE_RESULT_RUNNING;
  bool full_block = false;
  float ti;
  float current = adc_average(ADC_OVEN_CURRENT);

  if (!same_stage || (int8_t)pll.holdover_state > PLL_HOLDOVER_HOLDING) {
    pll.holdover_state = PLL_HOLDOVER_START;
  }
  if (pll.holdover_state != PLL_HOLDOVER_HOLDING) {
    holdover_ti_reset();
    if (pll_is_zero(pll.efc_history.fit_a)) {
      pll.efc_history.fit_a = pll_efc_less_temperature(current);
    }
    holdover_steering.aging_limit = AGING_LIMIT;
    if (pll.aging_state == PLL_AGING_COLLECTING) {
      if (((pll.efc_history.stop_ptr + 1) & PLL_AGING_INDEX_MASK) ==
          pll.efc_history.start_ptr) {
        holdover_steering.aging_limit = AGING_LIMIT_PER_SECOND;
      } else {
        holdover_steering.aging_limit = AGING_LIMIT;
      }
    }
    interval_next_reading(INTERVAL_ARM_CALIBRATE, NULL);
    /* holdover_start_time holds a time_of_day as its long. */
    get_date_time(&pll.holdover_start_date,
                  (time_of_day*)&pll.holdover_start_time);
    pll.holdover_state = PLL_HOLDOVER_HOLDING;
    holdover_steering.field_06 = 0;
    holdover_steering.seconds = 0;
    holdover_steering.readings = 0;
    holdover_steering.ti_mean = 0.0f;
    return PLL_STAGE_RESULT_RUNNING;
  }
  holdover_duration_count();
  if (interval_next_reading(INTERVAL_ARM_CALIBRATE, &ti) == 0) {
    if (gps_pps_dead == 0 && internal_pps_dead == 0 &&
        *PLL_READING_REPLACED == 0) {
      task_dmessage("holdover - Error with measurement\r\n");
    }
  } else if (gps_reference_good != 0) {
    holdover_ti_add(ti);
    float_add_in_place(&holdover_steering.ti_mean,
                       float_divide(ti, PLL_STAGES_BLOCK_LENGTH));
    holdover_steering.readings++;
  }
  holdover_steering.seconds++;
  if (holdover_steering.seconds < PLL_STAGES_BLOCK_SECONDS) {
    return PLL_STAGE_RESULT_RUNNING;
  }
  float duration = unsigned_to_float(pll.holdover_duration);
  float duration_squared =
      float_multiply(duration, unsigned_to_float(pll.holdover_duration));

  pll.tfom = float_multiply(
      float_multiply(
          float_divide(float_multiply(pll.predicted_uncertainty, TWO),
                       SECONDS_PER_DAY_SQUARED),
          duration_squared),
      HALF);
  if (float_compare(LEAST_TFOM, pll.tfom) > 0) {
    pll.tfom = LEAST_TFOM;
  }
  pll.efc = efc_clamp(pll_stages_predicted_efc(current));
  if (holdover_steering.readings != PLL_STAGES_BLOCK_SECONDS &&
      holdover_steering.readings != 0) {
    float_multiply_in_place(
        &holdover_steering.ti_mean,
        float_divide_reversed(unsigned_to_float(holdover_steering.readings),
                              PLL_STAGES_BLOCK_LENGTH));
  }
  if (pll_debug_enabled != 0) {
    char message[100];
    uint32_t date;
    time_of_day time;

    get_date_time(&date, &time);
    double current_double = float_to_double(current);
    double ti_ns = float_to_double(
        float_multiply(holdover_steering.ti_mean, PLL_STAGES_NANOSECONDS));

    debug_sprintf(
        message,
        "%d/%d/%d %02d:%02d:%02d efc= %d ti= %.1f hold= 0 curr= %.2f\r\n",
        (uint32_t)clock_middle(date), (uint32_t)clock_low(date),
        (uint32_t)clock_year(date), (uint32_t)time.hour, (uint32_t)time.minute,
        (uint32_t)time.second, float_to_integer(pll.efc), ti_ns,
        current_double);
    task_dmessage(message);
  }
  if (internal_pps_dead != 0) {
    pll.hold_causes |= HOLD_CAUSE_HARDWARE_FAILURE;
  } else if (gps_reference_good == 0) {
    pll.hold_causes |= HOLD_CAUSE_GPS;
  } else if (holdover_steering.readings == PLL_STAGES_BLOCK_SECONDS) {
    pll.hold_causes &=
        ~(HOLD_CAUSE_GPS | HOLD_CAUSE_TI_ERROR | HOLD_CAUSE_HARDWARE_FAILURE);
    full_block = true;
    if (pll.ti_limit_override == 0 &&
        nv_settings.holdover_limit_state == HOLDOVER_LIMIT_ON &&
        (char)holdover_within_limit(holdover_ti_max()) == 0) {
      pll.hold_causes |= HOLD_CAUSE_TI_LIMIT_EXCEEDED;
    } else {
      pll.hold_causes &= ~HOLD_CAUSE_TI_LIMIT_EXCEEDED;
    }
    if ((pll.hold_causes & HOLD_CAUSE_GPS_COMMAND_MONITOR) != HOLD_CAUSE_NONE &&
        gps.command_monitor_failed == 0) {
      pll.hold_causes &= ~HOLD_CAUSE_GPS_COMMAND_MONITOR;
    }
  }
  if (pll.manual == 0 && pll.hold_causes == HOLD_CAUSE_NONE) {
    result = PLL_STAGE_RESULT_DONE;
  }
  interval_mean_flag = full_block;
  if (full_block) {
    interval_mean = holdover_steering.ti_mean;
  }
  holdover_steering.ti_mean = 0.0f;
  holdover_steering.field_06 = 0;
  holdover_steering.seconds = 0;
  holdover_steering.readings = 0;
  holdover_ti_reset();
  return result;
}

/* The holdover recovery stage: once a reading is good, the fine
 * frequency setting, then the clock checked against GPS and the leap
 * second's acknowledgment; done when the clock agrees, unless the
 * offset is unknown or, after more than 350 s of holdover, the TI is
 * past 500 ns, which restarts the loop's time constant; done, it ends
 * the TI limit's override (0x43fc0). */
pll_stage_result holdover_recovery(bool same_stage) {
  static const uint8_t MOST_FAILURES = 5;
  static const float SHORT_HOLDOVER = 350.0f;
  static const double CLOSE_TI = 5e-07;
  pll_stage_result result = PLL_STAGE_RESULT_RUNNING;
  uint8_t valid = 0;
  float ti;
  bool ask_leap_ack = false;

  if (!same_stage || (int8_t)pll.recovery_state > PLL_RECOVERY_CLOCK_CHECK) {
    pll.recovery_state = PLL_RECOVERY_WAITING;
    holdover_steering.seconds = 0;
    clock_gps_mismatch = 0;
  }
  switch (pll.recovery_state) {
    case PLL_RECOVERY_FINE:
      result = fine();
      if (result == PLL_STAGE_RESULT_DONE) {
        if (pll.leap_ack_again == 0) {
          pll.recovery_state = PLL_RECOVERY_CLOCK_CHECK;
        } else {
          pll.recovery_state = PLL_RECOVERY_LEAP_ACK;
          pll.leap_ack_requested = 1;
        }
        result = PLL_STAGE_RESULT_RUNNING;
      }
      break;
    case PLL_RECOVERY_CHECK_DELAY:
      valid = interval_next_reading(INTERVAL_ARM_PLAIN, &ti);
      pll.recovery_countdown--;
      if (pll.recovery_countdown != 0 || leap_ack_pending() != 0) {
        break;
      }
      if ((char)clock_check_against_gps_58503a() == 0 &&
          clock_gps_mismatch != 0) {
        pll.recovery_countdown = PLL_STAGES_CLOCK_RECHECK_DELAY;
        break;
      }
      ask_leap_ack = true;
      break;
    case PLL_RECOVERY_LEAP_ACK:
      valid = interval_next_reading(INTERVAL_ARM_PLAIN, &ti);
      if (pll.leap_ack_requested == 0 && pll.leap_ack_done != 0) {
        pll.recovery_state = PLL_RECOVERY_CLOCK_CHECK;
      } else if (gps.command_monitor_failed != 0) {
        pll.hold_causes |= HOLD_CAUSE_GPS_COMMAND_MONITOR;
        pll.leap_ack_again = 1;
      }
      break;
    case PLL_RECOVERY_CLOCK_CHECK:
      valid = interval_next_reading(INTERVAL_ARM_CALIBRATE, &ti);
      if (leap_ack_pending() != 0) {
        break;
      }
      if ((char)clock_check_against_gps_58503a() != 0) {
        result = PLL_STAGE_RESULT_DONE;
        break;
      }
      if (clock_gps_mismatch == 0) {
        ask_leap_ack = true;
      }
      break;
    case PLL_RECOVERY_WAITING:
    default:
      valid = interval_next_reading(INTERVAL_ARM_CALIBRATE, &ti);
      if (valid == 0) {
        task_dmessage("holdover recovery - Error with measurement\r\n");
        holdover_steering.seconds++;
        if (holdover_steering.seconds > MOST_FAILURES) {
          result = PLL_STAGE_RESULT_FAILED;
          pll.hold_causes |= HOLD_CAUSE_TI_ERROR;
        }
      } else {
        pll.fine_state = PLL_FINE_START;
        pll.recovery_state = PLL_RECOVERY_FINE;
      }
      break;
  }
  if (ask_leap_ack) {
    pll.recovery_state = PLL_RECOVERY_LEAP_ACK;
    pll.leap_ack_requested = 1;
  }
  holdover_duration_count();
  result = pll_stages_check_causes(result);
  if (result != PLL_STAGE_RESULT_DONE) {
    return result;
  }
  if (leap_second_near() != 0) {
    pll.recovery_state = PLL_RECOVERY_CHECK_DELAY;
    pll.recovery_countdown = PLL_STAGES_CLOCK_CHECK_DELAY;
    return PLL_STAGE_RESULT_RUNNING;
  }
  if (get_gps_utc_offset(NULL) == 0) {
    pll.recovery_state = PLL_RECOVERY_LEAP_ACK;
    pll.leap_ack_requested = 1;
    return PLL_STAGE_RESULT_RUNNING;
  }
  if (valid == 0) {
    pll.hold_causes |= HOLD_CAUSE_TI_ERROR;
    return PLL_STAGE_RESULT_FAILED;
  }
  if (float_compare(unsigned_to_float(pll.holdover_duration), SHORT_HOLDOVER) >
          0 ||
      double_compare(float_to_double(float_absolute_on_stack(ti)), CLOSE_TI) <
          0) {
    pll.loop_time_constant = PLL_FIRST_TIME_CONSTANT;
  }
  pll.ti_limit_override = 0;
  return PLL_STAGE_RESULT_DONE;
}

/* Whether the GPS reference is questionable, gps.references[0].valid 2,
 * and good: valid with its 1PPS alive, or questionable in a PLL stage;
 * its coming and going reported as satellites sufficient (revisions.md,
 * "Reference state") (0x44f48). */
void gps_reference_update(void) {
  gps_reference_questionable =
      gps.references[0].valid == GPS_REFERENCE_QUESTIONABLE;
  bool good =
      (gps.references[0].valid == GPS_REFERENCE_VALID && gps_pps_dead == 0) ||
      (gps_reference_questionable && (pll.stage == PLL_STAGE_NORMAL_PLL ||
                                      pll.stage == PLL_STAGE_STARTUP_PLL));

  if (good) {
    if (gps_reference_good == 0) {
      gps_reference_good = 1;
      status_happening(STATUS_SATELLITES_SUFFICIENT_SET);
    }
  } else if (gps_reference_good != 0) {
    gps_reference_good = 0;
    status_happening(STATUS_SATELLITES_SUFFICIENT_CLEARED);
  }
}

/* The power-up recovery stage: turns the outer oven on, waits for a
 * reading, after a failed one for another, and sets the clock from the
 * reference; done unless a hold cause, failed if one, as a
 * pll_stage_result (0x46388).  Unlike the Z3805A's pll_check_reference it
 * leaves the oven's enable bit to the GPT's set-up. */
uint8_t pll_reference_check(char again) {
  pll_stage_result result = PLL_STAGE_RESULT_RUNNING;

  if (!again || (int8_t)pll.reference_check_state > PLL_REFERENCE_AGAIN) {
    pll.reference_check_state = PLL_REFERENCE_START;
  }
  switch (pll.reference_check_state) {
    case PLL_REFERENCE_FIRST:
      interval_wait_reading();
      if (interval_reading.valid == 0) {
        counter_reset();
        pll.reference_check_state = PLL_REFERENCE_AGAIN;
      } else {
        clock_set_from_reference();
        result = pll_stages_check_causes(PLL_STAGE_RESULT_DONE);
      }
      interval_rearm(INTERVAL_ARM_CALIBRATE);
      break;
    case PLL_REFERENCE_AGAIN:
      if (interval_next_reading(INTERVAL_ARM_CALIBRATE, NULL) == 0) {
        if (internal_pps_dead == 0) {
          pll.hold_causes |= HOLD_CAUSE_TI_ERROR;
        } else {
          pll.hold_causes |= HOLD_CAUSE_HARDWARE_FAILURE;
        }
        result = PLL_STAGE_RESULT_FAILED;
      } else {
        clock_set_from_reference();
        result = pll_stages_check_causes(PLL_STAGE_RESULT_DONE);
      }
      break;
    case PLL_REFERENCE_START:
    default:
      outer_oven_enabled = 1;
      interval_rearm(INTERVAL_ARM_CALIBRATE);
      pll.reference_check_state = PLL_REFERENCE_FIRST;
      break;
  }
  return result;
}
