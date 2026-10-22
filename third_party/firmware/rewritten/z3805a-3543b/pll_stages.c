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

/* Readings to a block of the loops' and holdover's steering. */
static const uint8_t PLL_STAGES_BLOCK_SECONDS = 10;

/* The aging fit's time unit, an EFC history entry's 2700 seconds. */
static const float PLL_STAGES_ENTRY_SECONDS = 2700.0f;

/* A float with its sign flipped, as the image does with bchg #31
 * (0x44c42). */
static float pll_stages_negate(float value) {
  uint32_t bits;

  memcpy(&bits, &value, sizeof bits);
  bits ^= LIBC_SIGN;
  memcpy(&value, &bits, sizeof value);
  return value;
}

/* Readings before the clock is checked against GPS, as power-up and
 * holdover recovery count them, and between checks after a mismatch. */
static const uint8_t PLL_STAGES_CLOCK_CHECK_DELAY = 25;

static const uint8_t PLL_STAGES_CLOCK_RECHECK_DELAY = 2;

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

/* The power-up stage: clears the EFC history, waits for the ovens and
 * for GPS, sets the frequency coarsely and finely and the clock from
 * GPS, then waits out a near leap second and the leap second's
 * acknowledgment; back to waiting for GPS while it is lost (0x45e8e). */
pll_stage_result powerup(bool same_stage) {
  static const float COARSE_TFOM = 0.001f;
  static const float FINE_TFOM = 2e-06f;
  pll_stage_result result = PLL_STAGE_RESULT_RUNNING;

  if (!same_stage || (int8_t)pll.powerup_state > PLL_POWERUP_LEAP_SECONDS) {
    memset(&pll.efc_history, 0, sizeof pll.efc_history);
    pll.aging_state = PLL_AGING_START;
    pll.efc_history.loop_settled = 0;
    first_satellite_logged = 0;
    pll.powerup_state = PLL_POWERUP_START;
  }
  switch (pll.powerup_state) {
    case PLL_POWERUP_INNER_OVEN:
      interval_next_reading(INTERVAL_ARM_PLAIN, NULL);
      if (oven_warmed_up()) {
        pll.powerup_state = PLL_POWERUP_OUTER_OVEN;
        gpt.portgp |= PLL_STAGES_OUTER_OVEN_ENABLE;
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
          (pll.hold_causes & HOLD_CAUSE_TEMPORARY_MODE) == HOLD_CAUSE_NONE &&
          (int8_t)pll.gps_init_state > PLL_GPS_INIT_HOLD_OFF_ACK) {
        pll.powerup_state = PLL_POWERUP_COARSE;
        pll.coarse_state = 0;
      }
      break;
    case PLL_POWERUP_COARSE: {
      pll_stage_result coarse_result = coarse();

      if (coarse_result == PLL_STAGE_RESULT_DONE) {
        pll.tfom = COARSE_TFOM;
        pll.powerup_state = PLL_POWERUP_FINE;
        pll.fine_state = 0;
      } else if (coarse_result == PLL_STAGE_RESULT_FAILED) {
        result = PLL_STAGE_RESULT_FAILED;
      }
      break;
    }
    case PLL_POWERUP_FINE: {
      char fine_result = fine();

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
        if ((char)clock_check_against_gps() == 0 && clock_gps_mismatch != 0) {
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
  if ((int8_t)pll.powerup_state > PLL_POWERUP_WAITING_FOR_GPS &&
      (gps_reference_good == 0 || internal_pps_dead != 0 ||
       (pll.hold_causes & HOLD_CAUSE_TEMPORARY_MODE) != HOLD_CAUSE_NONE)) {
    pll.powerup_reference_lost = 1;
  }
  if (pll.powerup_reference_lost != 0 &&
      (pll.powerup_state != PLL_POWERUP_LEAP_SECONDS ||
       pll.leap_ack_done != 0)) {
    pll.powerup_state = PLL_POWERUP_WAITING_FOR_GPS;
    result = PLL_STAGE_RESULT_RUNNING;
  }
  return result;
}

/* A measurement's failures, pll.fine_retries, past which coarse and fine
 * give up. */
static const uint16_t PLL_STAGES_MOST_RETRIES = 9;

/* The coarse frequency setting: syncs the counter, then measures the TI
 * across a second and moves the EFC by the change until it is within
 * 100 ns (0x451c2). */
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

/* fine_sync_adjust's steps for a TI 300 ns or more off, which it meets
 * with one step and fine with another slew. */
static const uint8_t PLL_STAGES_SYNC_TOO_FAR = 4;

/* For a TI of 100 ns or more, asks for that many 100 ns steps against
 * it, the steps; one and 4 for 300 ns or more; 0 within 100 ns
 * (0x44e8e). */
uint8_t fine_sync_adjust(float ti) {
  static const float STEP = 1e-07f;
  static const float MOST = 3e-07f;

  if (float_compare(ti, STEP) < 0) {
    if (float_compare(pll_stages_negate(STEP), ti) < 0) {
      return 0;
    }
    if (float_compare(pll_stages_negate(MOST), ti) < 0) {
      uint32_t steps =
          float_to_unsigned(float_divide(ti, pll_stages_negate(STEP)));

      sync_request(COUNTER_SYNC_NEGATIVE, (uint8_t)steps);
      return steps;
    }
    sync_request(COUNTER_SYNC_NEGATIVE, 1);
    return PLL_STAGES_SYNC_TOO_FAR;
  }
  if (float_compare(ti, MOST) < 0) {
    uint32_t steps = float_to_unsigned(float_divide(ti, STEP));

    sync_request(COUNTER_SYNC_POSITIVE, (uint8_t)steps);
    return steps;
  }
  sync_request(COUNTER_SYNC_POSITIVE, 1);
  return PLL_STAGES_SYNC_TOO_FAR;
}

/* Syncs the outputs to GPS when the TI passes the limit threshold or a
 * sync is asked for; whether it did (0x44f5e). */
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

/* fine's readings to its fit, fine_readings' entries. */
static const uint8_t PLL_STAGES_FINE_READINGS =
    sizeof fine_readings / sizeof fine_readings[0];

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

/* Adds reading i, pll.fine_d, to fine's least-squares line through the
 * readings against their index, TI = x0 + y0 i: its sums, then the slope
 * y0, the fractional frequency (0 when the sums give no slope), and the
 * intercept x0; the largest TI magnitude kept in fine_ti_max. */
static void pll_stages_fine_fit_add(void) {
  uint8_t i = pll.fine_index;

  if (float_compare(float_absolute_on_stack(pll.fine_d), fine_ti_max) > 0) {
    fine_ti_max = float_absolute_on_stack(pll.fine_d);
  }
  fine_readings[i - 1] = pll.fine_d;
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

/* After the fit's readings, whether the first lies within 300 ns of the
 * line; measures again, a retry counted, if not.  The image's check is
 * a loop over the readings that leaves after the first either way
 * (0x45756), so only the first is checked. */
static void pll_stages_fine_fit_check(void) {
  static const float BAD_TI = 3e-07f;

  pll.fine_index = 1;
  float fitted = float_add(
      float_multiply(unsigned_to_float(pll.fine_index), fine_y0), fine_x0);
  float residual =
      float_subtract_reversed(fitted, fine_readings[pll.fine_index - 1]);

  if (float_compare(float_absolute_on_stack(residual), BAD_TI) > 0) {
    task_dmessage("Bad TI in frequency measurement\r\n");
    pll.fine_state = PLL_FINE_MEASURE_1;
    pll.fine_retries++;
  }
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
        nv_settings.holdover_limit_state == 0) {
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

/* The fine frequency setting: syncs the outputs if the TI asks, fits a
 * line to 50 readings for the frequency, then moves the EFC by it and
 * measures again, or slews the phase once the frequency is within 2e-9;
 * a sync asked for starts it again but while it fine-slews (0x4538c). */
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

/* A loop's one-second reading: summed into the block within the hold
 * threshold, else printed; and, with GPS and no leap second
 * acknowledgment pending, the clock checked against GPS at second 14 or
 * after a mismatch, a matching check asking for the leap second's
 * acknowledgment.  A reading that failed with both 1PPS good is a
 * measurement failure, failure_message printed. */
static void pll_stages_loop_reading(uint8_t valid, float ti,
                                    char* failure_message) {
  static const uint32_t CHECK_SECOND = 14;
  static const uint32_t SECOND_MASK = 0xff;

  if (valid == 0) {
    if (internal_pps_dead == 0 && gps_pps_dead == 0) {
      task_dmessage(failure_message);
      loop_steering.measurement_failed = 1;
    }
    return;
  }
  if (float_compare(float_absolute_on_stack(ti),
                    nv_settings.holdover_limit_threshold) < 0 ||
      nv_settings.holdover_limit_state != 0) {
    float_add_in_place(&loop_steering.ti_sum, ti);
    loop_steering.readings++;
  } else if (debug_enabled != 0) {
    char message[100];
    uint32_t date;
    time_of_day time;

    get_date_time(&date, &time);
    debug_sprintf(message, "%d/%d/%d %02d:%02d:%02d TI result: %.1fns\r\n",
                  (uint32_t)clock_middle(date), (uint32_t)clock_low(date),
                  (uint32_t)clock_year(date), (uint32_t)time.hour,
                  (uint32_t)time.minute, (uint32_t)time.second,
                  float_to_double(float_multiply(ti, PLL_STAGES_NANOSECONDS)));
    task_dmessage(message);
  }
  if (gps_reference_good != 0 && leap_ack_pending() == 0 &&
      (clock_gps_mismatch != 0 ||
       (get_gps_time() & SECOND_MASK) == CHECK_SECOND) &&
      (char)clock_check_against_gps() == 0 && clock_gps_mismatch == 0) {
    pll.leap_ack_requested = 1;
  }
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
  static const float BLOCK = 10.0f;
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

  float_add_in_place(
      &loop_steering.integral,
      float_multiply(float_multiply(drift, gains.integral), BLOCK));
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

/* The start-up loop: a proportional-integral loop on 10-second blocks
 * whose time constant grows 5 s a block to max_loop_time, settling once
 * 15 blocks in a row are within 150 ns and the EFC history has its
 * first entries; done at the full time constant (0x4429e). */
pll_stage_result startup_pll(bool same_stage) {
  static const int32_t SETTLED_BLOCKS = 15;
  static const double SETTLED_TI = 1.5e-07;
  static const float SETTLED_TFOM = 1e-06f;
  static const uint8_t FIRST_ENTRIES = 3;
  static const float TAU_STEP = 5.0f;
  static const float MINUS_TAU_STEP = -5.0f;
  pll_stage_result result = PLL_STAGE_RESULT_RUNNING;
  float ti;
  float current = adc_average(ADC_CURRENT);

  if (!same_stage || (int8_t)pll.startup_state > PLL_STARTUP_LENGTHENING) {
    pll.frequency_adjusted = 0;
    pll.ti_filtered = 0.0f;
    loop_steering.integral = 0.0f;
    pll.efc_base = pll_efc_less_temperature(current);
    loop_steering.ti_sum = 0.0f;
    loop_steering.readings = 0;
    loop_steering.seconds = 0;
    loop_steering.measurement_failed = 0;
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
  if (!pll_stages_loop_mean()) {
    result = PLL_STAGE_RESULT_FAILED;
  }
  switch (pll.startup_state) {
    case PLL_STARTUP_SETTLING:
      if (loop_steering.large_offset_blocks < SETTLED_BLOCKS) {
        if (double_compare(
                float_to_double(float_absolute_on_stack(loop_steering.ti_sum)),
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
        if (result != PLL_STAGE_RESULT_FAILED) {
          pll.loop_time_constant = max_loop_time;
          pll_stages_rescale_integral(float_add(max_loop_time, MINUS_TAU_STEP));
          result = PLL_STAGE_RESULT_DONE;
        }
      } else {
        float_add_in_place(&pll.loop_time_constant, TAU_STEP);
        pll_stages_rescale_integral(
            float_add(pll.loop_time_constant, MINUS_TAU_STEP));
      }
      break;
    default:
      break;
  }
  pll_stages_steer(pll_stages_gains_for(pll.loop_time_constant), current,
                   pll.loop_time_constant);
  return pll_stages_check_causes(result);
}

/* The normal loop: the start-up loop's at max_loop_time, its gains kept
 * in loop_steering (0x448fc). */
pll_stage_result pll_normal(bool same_stage) {
  static const float NORMAL_TFOM = 1e-06f;
  static const float FREQUENCY_TOLERANCE = 1e-08f;
  float ti;
  float current = adc_average(ADC_CURRENT);

  if (!same_stage || (int8_t)pll.normal_state > PLL_NORMAL_RUNNING) {
    pll.frequency_adjusted = 0;
    pll.tfom = NORMAL_TFOM;
    loop_steering.ti_sum = 0.0f;
    loop_steering.readings = 0;
    loop_steering.seconds = 0;
    loop_steering.measurement_failed = 0;
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
      pll_stages_loop_mean();
      pll_stages_gains gains = {
          .proportional = loop_steering.proportional_gain,
          .integral = loop_steering.integral_gain,
          .filter_weight = loop_steering.filter_weight,
      };

      pll_stages_steer(gains, current, max_loop_time);
    }
  }
  return pll_stages_check_causes(PLL_STAGE_RESULT_RUNNING);
}

/* Clears holdover's TI statistics (0x43804). */
void holdover_ti_reset(void) {
  holdover_steering.ti_sum = 0.0f;
  holdover_steering.ti_weighted_sum = 0.0f;
  holdover_steering.ti_largest = 0.0f;
  holdover_steering.ti_count = 0;
}

/* Adds a TI reading to holdover's statistics: its sum, its sum weighted
 * by index, the count and the largest magnitude (0x4381e). */
void holdover_ti_add(float ti) {
  float_add_in_place(&holdover_steering.ti_sum, ti);
  float_add_in_place(
      &holdover_steering.ti_weighted_sum,
      float_multiply(integer_to_float(holdover_steering.ti_count), ti));
  holdover_steering.ti_count++;
  if (float_compare(float_absolute_on_stack(ti), holdover_steering.ti_largest) >
      0) {
    holdover_steering.ti_largest = float_absolute_on_stack(ti);
  }
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
  static const float EFC_MOST = 1048560.0f;
  static const int32_t FEWEST_READINGS = 10;
  float slope = float_divide(
      float_subtract(
          float_multiply(holdover_steering.ti_weighted_sum, READINGS),
          float_multiply(holdover_steering.ti_sum, INDEX_SUM)),
      SLOPE_DENOMINATOR);
  float efc = float_add(float_divide(slope, efc_gains.gain), pll.efc);

  return float_compare(efc, EFC_LEAST) >= 0 &&
         float_compare(EFC_MOST, efc) >= 0 &&
         holdover_steering.ti_count >= FEWEST_READINGS;
}

/* The largest TI magnitude holdover has seen (0x43916). */
float holdover_ti_max(void) { return holdover_steering.ti_largest; }

/* 0 when, in a holdover younger than the measurement threshold and with
 * no frequency correction applied, the TI ti has reached the hold
 * limit, the threshold plus the tolerance a second; else 1 (0x4391e). */
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

/* The log event for holdover on the GPS's loss, by the receiver status
 * it last reported (the jump table at 0x439f6); any other status is the
 * GPS's 1PPS, dead or lost. */
static log_event_code pll_stages_gps_holdover_event(void) {
  switch (gps.status_reported) {
    case GPS_SYNC_RECEIVER_ERROR:
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
 * when a manual hold cause or no automatic recovery asks it, and a new
 * holdover logged by its cause (0x4397a). */
void enter_holdover(void) {
  if (pll.stage != PLL_STAGE_HOLDOVER_RECOVERY) {
    pll.holdover_duration = 0;
  }
  pll.manual = (pll.hold_causes & HOLD_CAUSE_MANUAL) != HOLD_CAUSE_NONE ||
               holdover_recovery_auto == 0;
  pll.stage = PLL_STAGE_HOLDOVER;
  if (pll.holdover_duration == 0) {
    log_event(pll_stages_holdover_event());
  }
}

/* The holdover stage: steers the EFC by the aging fit, and each 10
 * seconds the TFOM from the HQ and the holdover's age, and the hold
 * causes from the TI's block: done once none is left and the holdover
 * is not manual (0x43a90). */
pll_stage_result holdover(bool same_stage) {
  static const float AGING_LIMIT = 2e-09f;
  static const float AGING_LIMIT_PER_SECOND = 2.3148148e-14f;
  static const float BLOCK = 10.0f;
  static const float TWO = 2.0f;
  static const float HALF = 0.5f;
  static const float SECONDS_PER_DAY_SQUARED = 7.46496e+09f;
  static const float LEAST_TFOM = 1e-06f;
  pll_stage_result result = PLL_STAGE_RESULT_RUNNING;
  float ti;
  float current = adc_average(ADC_CURRENT);

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
  pll_stages_holdover_second();
  if (interval_next_reading(INTERVAL_ARM_CALIBRATE, &ti) == 0) {
    if (gps_pps_dead == 0 && internal_pps_dead == 0) {
      task_dmessage("holdover - Error with measurement\r\n");
    }
  } else if (gps_reference_good != 0) {
    holdover_ti_add(ti);
    float_add_in_place(&holdover_steering.ti_mean, float_divide(ti, BLOCK));
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
                              BLOCK));
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
    interval_mean_flag = 1;
    interval_mean = holdover_steering.ti_mean;
    if (pll.ti_limit_override == 0 && nv_settings.holdover_limit_state == 0 &&
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
    pll.ti_limit_override = 0;
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
 * offset is unknown or, after more than 500 s of holdover, the TI is
 * past 500 ns, which restarts the loop's time constant (0x43f9a). */
pll_stage_result holdover_recovery(bool same_stage) {
  static const uint8_t MOST_FAILURES = 5;
  static const float SHORT_HOLDOVER = 500.0f;
  static const double CLOSE_TI = 5e-07;
  static const float FIRST_TIME_CONSTANT = 150.0f;
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
      if ((char)clock_check_against_gps() == 0 && clock_gps_mismatch != 0) {
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
      if ((char)clock_check_against_gps() != 0) {
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
        pll.fine_state = 0;
        pll.recovery_state = PLL_RECOVERY_FINE;
      }
      break;
  }
  if (ask_leap_ack) {
    pll.recovery_state = PLL_RECOVERY_LEAP_ACK;
    pll.leap_ack_requested = 1;
  }
  pll_stages_holdover_second();
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
  if (float_compare(unsigned_to_float(pll.holdover_duration), SHORT_HOLDOVER) <=
          0 &&
      double_compare(float_to_double(float_absolute_on_stack(ti)), CLOSE_TI) >=
          0) {
    return PLL_STAGE_RESULT_DONE;
  }
  pll.loop_time_constant = FIRST_TIME_CONSTANT;
  return result;
}
