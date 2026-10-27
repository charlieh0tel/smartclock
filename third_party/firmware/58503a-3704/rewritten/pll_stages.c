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

/* Logs the first satellite tracked, once, with the GPS's date and
 * time. */
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
    log_first_satellite(oncore_date_pack(gps.status.date),
                        clock_time_bits(gps.status.time));
  }
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
 * false, with a hold cause, when no reading was summed: a TI error if
 * one failed, else every one was past the hold threshold. */
static bool pll_stages_loop_mean(void) {
  if (loop_steering.readings == 0) {
    loop_steering.ti_sum = 0.0f;
    interval_mean_flag = 0;
    if (loop_steering.measurement_failed == 0) {
      pll.hold_causes |= HOLD_CAUSE_TI_LIMIT_EXCEEDED;
    } else {
      pll.hold_causes |= HOLD_CAUSE_TI_ERROR;
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
 * filtered TI, the base, the integral and the temperature correction;
 * then starts the next block.  loop_time is the debug message's. */
static void pll_stages_steer(pll_stages_gains gains, float current,
                             float loop_time) {
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

/* Counts a second of holdover, and reports it past the status
 * threshold, once. */
static void pll_stages_holdover_second(void) {
  pll.holdover_duration++;
  if (pll.holdover_duration > (uint32_t)nv_settings.holdover_status_threshold &&
      holdover_duration_exceeded == 0) {
    holdover_duration_exceeded = 1;
    pwma_force_level(1);
    status_happening(STATUS_HOLDOVER_THRESHOLD_SET);
  }
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
