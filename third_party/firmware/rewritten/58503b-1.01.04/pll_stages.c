/* From the Z3815A 4010's rewritten/pll_stages.c, the 58503B 1.01.04's own
 * beside common/'s: of its functions, those the 58503B's code carries
 * identical or relocated (docs/firmware/58503b-1.01.04.md, "The link list").
 * Its comments' addresses are the 58503B's, but where they name another
 * image.  Each keeps its generated prototype. */

/* The EFC's gain at an EFC, a + b * efc^2 by the OCXO calibration, given
 * the loop (0x4d196). */
void efc_gain_from_calibration(float efc) {
  efc_gain_set(float_add(
      float_multiply(
          float_multiply(scpi_value_DIAGnostic_ROSCillator_EFControl_BSLOPe,
                         efc),
          efc),
      scpi_value_DIAGnostic_ROSCillator_EFControl_ASLOPe));
}

/* :DIAGnostic:ROSCillator:TCOefficient times a value, which the loop's
 * stages take (0x4af76). */
float tc_coefficient_product(float value) {
  return float_multiply(scpi_value_DIAGnostic_ROSCillator_TCOefficient, value);
}

/* The OCXO calibration's points: the EFC's step between them, a ninth of
 * its 20-bit range, the points, and the readings let settle before
 * each. */
static const int32_t PLL_STAGES_CALIBRATION_STEP = 116508;

static const uint8_t PLL_STAGES_CALIBRATION_POINTS = 10;

static const int8_t PLL_STAGES_CALIBRATION_SETTLE = 10;

/* A point's frequency offset, the difference of its last reading and
 * its first over the readings between. */
static const float PLL_STAGES_CALIBRATION_SPAN = 3.0f;

/* The EFC at a tenth and nine tenths of its range, where the ADC is read
 * for :DADC, and the ADC counts between them. */
static const float PLL_STAGES_EFC_LOW = 104857.5f;

static const float PLL_STAGES_EFC_HIGH = 943717.5f;

static const float PLL_STAGES_ADC_SPAN = -838860.0f;

/* :DIAG:ROSC:EFC:DADC's default, the DAC's counts per ADC count. */
static const float PLL_STAGES_DAC_PER_ADC_DEFAULT = 16.0f;

/* A reading of the OCXO calibration's that failed: an execution error
 * reported to the source, and the measurement done. */
static uint8_t pll_stages_calibration_failed(request_source source) {
  status_report_from(STATUS_EXECUTION_ERROR, source);
  return 1;
}

/* The OCXO calibration's measurement, one step a call while its stage
 * runs; 1 when it is done or a reading fails, else 0 (0x4d310).  From the
 * start, the EFC at 0 and the USER mode's EFC cleared, it waits for the
 * GPS reference and the oven (ocxo_warm, which gives 1), lets ten
 * readings settle, then at each tenth of the EFC's range takes three
 * readings, the frequency offset from the difference of the third's and
 * the first's over 3; fits a and b (efc_calibration_fit_linear), and puts
 * :ASLOPe within the oscillator type's efc_oscillators, else its
 * default, an execution error reported, logs it, clears :BSLOPe, saves
 * them and gives the loop the gain.  A failed reading reports an
 * execution error to the source; the GPS reference lost starts it again.
 * The ADC's steps for :DADC, which the Z3815A's takes, nothing goes to
 * here, and the values they read nothing sets.  Its done hook,
 * efc_calibration_done, it calls with log_write's arguments still on the
 * stack, so that it takes the format's address as its index and the
 * slope's high word as its source. */
uint8_t efc_calibration_measure(uint8_t running, request_source source) {
  float reading = 0.0f;
  uint8_t result = 0;
  char line[80];

  if (running == 0 ||
      (int8_t)efc_calibration.step > (int8_t)EFC_CALIBRATION_CHECK ||
      (gps_reference_good == 0 &&
       efc_calibration.step != EFC_CALIBRATION_WAIT_REFERENCE)) {
    efc_calibration.step = EFC_CALIBRATION_START;
  }
  switch (efc_calibration.step) {
    case EFC_CALIBRATION_WAIT_REFERENCE:
      interval_next_reading(INTERVAL_ARM_PLAIN, NULL);
      if (gps_reference_good != 0 && ocxo_warm() != 0) {
        efc_calibration.settle = 0;
        efc_calibration.step = EFC_CALIBRATION_SETTLE;
      }
      break;
    case EFC_CALIBRATION_SETTLE:
      interval_next_reading(INTERVAL_ARM_PLAIN, NULL);
      if ((int8_t)efc_calibration.settle++ > PLL_STAGES_CALIBRATION_SETTLE) {
        efc_calibration.step = EFC_CALIBRATION_FIRST_READING;
        efc_calibration.field_02 = 0;
      }
      break;
    case EFC_CALIBRATION_FIRST_READING:
      if (interval_next_reading(
              INTERVAL_ARM_CALIBRATE,
              &efc_calibration.frequency_offset[efc_calibration.point]) != 0) {
        efc_calibration.step = EFC_CALIBRATION_SECOND_READING;
      } else {
        result = pll_stages_calibration_failed(source);
      }
      break;
    case EFC_CALIBRATION_SECOND_READING:
      if (interval_next_reading(INTERVAL_ARM_CALIBRATE, &reading) != 0) {
        efc_calibration.step = EFC_CALIBRATION_THIRD_READING;
      } else {
        result = pll_stages_calibration_failed(source);
      }
      break;
    case EFC_CALIBRATION_THIRD_READING:
      if (interval_next_reading(INTERVAL_ARM_CALIBRATE, &reading) != 0) {
        efc_calibration.step = EFC_CALIBRATION_POINT;
      } else {
        result = pll_stages_calibration_failed(source);
      }
      break;
    case EFC_CALIBRATION_POINT:
      interval_wait_reading();
      if (interval_reading.valid == 0) {
        task_dmessage("efc cal - Error with measurement 2\r\n");
        result = pll_stages_calibration_failed(source);
        break;
      }
      efc_calibration.frequency_offset[efc_calibration.point] = float_divide(
          float_subtract_reversed(
              double_to_float(interval_reading.value),
              efc_calibration.frequency_offset[efc_calibration.point]),
          PLL_STAGES_CALIBRATION_SPAN);
      if (pll_debug_enabled != 0) {
        debug_sprintf(
            line, "point= %d delta_f= %e efc=  %d\r\n",
            (uint32_t)efc_calibration.point,
            float_to_double(
                efc_calibration.frequency_offset[efc_calibration.point]),
            float_to_integer(pll.efc));
        task_dmessage(line);
      }
      efc_calibration.point++;
      if (efc_calibration.point < PLL_STAGES_CALIBRATION_POINTS) {
        efc_set(integer_to_float(efc_calibration.point *
                                 PLL_STAGES_CALIBRATION_STEP));
        interval_rearm(INTERVAL_ARM_SYNC);
        efc_calibration.settle = 0;
        efc_calibration.step = EFC_CALIBRATION_SETTLE;
        break;
      }
      efc_calibration_fit_linear();
      if (pll_debug_enabled != 0) {
        debug_sprintf(
            line, "a= %e b= %e\r\n",
            float_to_double(scpi_value_DIAGnostic_ROSCillator_EFControl_ASLOPe),
            float_to_double(
                scpi_value_DIAGnostic_ROSCillator_EFControl_BSLOPe));
        task_dmessage(line);
      }
      efc_calibration.point = 0;
      efc_set(PLL_STAGES_EFC_LOW);
      efc_calibration.step = EFC_CALIBRATION_CHECK;
      break;
    case EFC_CALIBRATION_ADC_LOW:
      interval_next_reading(INTERVAL_ARM_CALIBRATE, &reading);
      if (++efc_calibration.point > PLL_STAGES_CALIBRATION_SETTLE) {
        efc_calibration.point = 0;
        efc_calibration.step = EFC_CALIBRATION_ADC_HIGH;
        efc_set(PLL_STAGES_EFC_HIGH);
      }
      break;
    case EFC_CALIBRATION_ADC_HIGH:
      interval_next_reading(INTERVAL_ARM_CALIBRATE, &reading);
      if (++efc_calibration.point > PLL_STAGES_CALIBRATION_SETTLE) {
        efc_calibration.step = EFC_CALIBRATION_CHECK;
        scpi_value_DIAGnostic_ROSCillator_EFControl_DADC =
            float_divide(integer_to_float(efc_calibration.adc_low -
                                          efc_calibration.adc_high),
                         PLL_STAGES_ADC_SPAN);
        if (pll_debug_enabled != 0) {
          debug_sprintf(line, "low= %d high= %d dac/adc= %e\r\n",
                        efc_calibration.adc_low, efc_calibration.adc_high,
                        float_to_double(
                            scpi_value_DIAGnostic_ROSCillator_EFControl_DADC));
          task_dmessage(line);
        }
      }
      break;
    case EFC_CALIBRATION_CHECK: {
      static char* const LOGGED = "OCXO cal, a= %e";
      int32_t type = (int32_t)(int8_t)oscillator_type >
                             (int32_t)(uint8_t)oscillator_type_most()
                         ? 0
                         : (int8_t)oscillator_type;
      const efc_oscillator_entry* limit = &efc_oscillators[type];

      if (float_compare(scpi_value_DIAGnostic_ROSCillator_EFControl_ASLOPe,
                        limit->efc_a_minimum) < 0 ||
          float_compare(scpi_value_DIAGnostic_ROSCillator_EFControl_ASLOPe,
                        limit->efc_a_maximum) > 0) {
        scpi_value_DIAGnostic_ROSCillator_EFControl_ASLOPe = limit->efc_a;
        log_write("OCXO cal a out of range");
        status_report_from(STATUS_EXECUTION_ERROR, source);
      }
      union libc_double slope = {
          .value = float_to_double(
              scpi_value_DIAGnostic_ROSCillator_EFControl_ASLOPe)};

      log_write(LOGGED, slope.value);
      scpi_value_DIAGnostic_ROSCillator_EFControl_BSLOPe = 0.0f;
      efc_calibration_done((int32_t)LOGGED, (int32_t)slope.word.high);
      efc_gain_from_calibration(pll.efc);
      result = 1;
      break;
    }
    case EFC_CALIBRATION_START:
    default:
      scpi_value_DIAGnostic_ROSCillator_EFControl_DADC =
          PLL_STAGES_DAC_PER_ADC_DEFAULT;
      diag_efc_mode = EFC_MODE_NORMAL;
      scpi_value_DIAGnostic_ROSCillator_EFControl_DATA = 0;
      efc_calibration.point = 0;
      efc_set(0.0f);
      interval_next_reading(INTERVAL_ARM_SYNC, NULL);
      efc_calibration.step = EFC_CALIBRATION_WAIT_REFERENCE;
      break;
  }
  return result;
}

/* A double with its sign flipped, as the image does with bchg #31. */
static double pll_stages_negate_double(double value) {
  union libc_double v = {.value = value};

  v.word.high ^= LIBC_SIGN;
  return v.value;
}

/* The real cube root of a positive value, through exp and log, as the
 * image takes it: exp(log(v) / 3). */
static double pll_stages_cube_root(double value) {
  static const double THIRD = 0.3333333333333333;

  return exp(double_multiply(log(value), THIRD));
}

/* The EFC that changes the frequency by delta_f, from pll.efc, by the
 * EFC calibration's slopes A and B, the frequency f(e) = A e + B e^3 / 3
 * (0x4d76a).  With B zero, pll.efc + delta_f / A.  Else the real root of
 * e^3 + p e = q, p = 3A/B, q = 3(delta_f + A efc)/B + efc^3, by Cardano:
 * with h = q/2, k = A/B and D = h^2 + k^3, cbrt(h + sqrt D) +
 * cbrt(h - sqrt D), each cube root of a negative the negated root of its
 * magnitude; with D not positive, three real roots, pll.efc itself.  The
 * Z3815A's, without its debug prints. */
float efc_for_frequency_change(float delta_f) {
  const float a = scpi_value_DIAGnostic_ROSCillator_EFControl_ASLOPe;
  const float b = scpi_value_DIAGnostic_ROSCillator_EFControl_BSLOPe;
  double efc = float_to_double(pll.efc);
  union libc_double slope = {.value = float_to_double(b)};

  if (((slope.word.high << 1) | slope.word.low) == 0) {
    return double_to_float(
        double_add_pointer(float_to_double(float_divide(delta_f, a)), &efc));
  }
  double k = float_to_double(float_divide(a, b));
  double q = double_add(
      double_multiply(
          double_add(float_to_double(delta_f),
                     double_multiply_pointer(float_to_double(a), &efc)),
          double_divide_reversed(float_to_double(b), 3.0)),
      double_multiply_pointer(double_multiply_pointers(&efc, &efc), &efc));
  double h = double_multiply(0.5, q);
  double discriminant =
      double_add(double_multiply_pointers(&h, &h),
                 double_multiply_pointer(double_multiply_pointers(&k, &k), &k));

  if (double_compare(0.0, discriminant) < 1) {
    return double_to_float(efc);
  }
  double s = sqrt(discriminant);
  double root = 0.0;
  double sum = double_add_pointers(&h, &s);

  if (double_compare(0.0, sum) > 0) {
    root = pll_stages_cube_root(sum);
  } else {
    root = pll_stages_negate_double(
        pll_stages_cube_root(pll_stages_negate_double(sum)));
  }
  if (double_compare(0.0, double_subtract_reversed_pointers(&s, &h)) > 0) {
    double_add_in_place(
        &root, pll_stages_cube_root(double_subtract_reversed_pointers(&s, &h)));
  } else {
    double_subtract_in_place(
        &root, pll_stages_cube_root(double_subtract_reversed_pointers(&h, &s)));
  }
  return double_to_float(root);
}

/* 0 when, in a holdover younger than the measurement threshold and with
 * no frequency correction applied, the TI ti has reached the hold
 * limit, the threshold plus the tolerance a second; else 1 (0x4870c). */
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

/* The power-up recovery stage: turns the outer oven on, waits for a
 * reading, after a failed one for another, and sets the clock from the
 * reference; done unless a hold cause, failed if one, as a
 * pll_stage_result (0x4b424).  Unlike the Z3805A's pll_check_reference it
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

/* Clears holdover's largest TI reading alone, where the other images
 * clear its sums and count too (0x486cc). */
void holdover_ti_reset(void) { holdover_steering.ti_largest = 0.0f; }

/* Keeps the largest magnitude of a TI reading in holdover; the other
 * images add it to sums and a count too (0x486d4). */
void holdover_ti_add(float ti) {
  if (float_compare(float_absolute_on_stack(ti), holdover_steering.ti_largest) >
      0) {
    holdover_steering.ti_largest = float_absolute_on_stack(ti);
  }
}

/* The log event for a GPS hold cause, by the GPS reference's state. */
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

/* Enters holdover: from any stage but recovery, its duration and its
 * start's uptime restarted and a new holdover logged by its cause; manual
 * when a manual hold cause or no automatic recovery asks it, and the
 * interval's mean dropped for any cause but the manual one (0x48768).
 * The Z3815A's logs while the duration is 0, from recovery too, and
 * reports the holds; this image has no external reference. */
void enter_holdover(void) {
  bool recovering = pll.stage == PLL_STAGE_HOLDOVER_RECOVERY;

  if (!recovering) {
    pll.holdover_duration = 0;
    pll.holdover_start_uptime = get_uptime();
  }
  pll.manual = (pll.hold_causes & HOLD_CAUSE_MANUAL) != HOLD_CAUSE_NONE ||
               holdover_recovery_auto == 0;
  pll.stage = PLL_STAGE_HOLDOVER;
  if (!recovering) {
    log_event(pll_stages_holdover_event());
  }
  if ((pll.hold_causes & ~HOLD_CAUSE_MANUAL) != HOLD_CAUSE_NONE) {
    interval_mean_flag = 0;
  }
}

/* Counts holdover's duration as the uptime since its start, and reports
 * it past the status threshold, once, with the PWMA output set high
 * (0x48872): the Z3815A's duration, the 58503A's report. */
void holdover_duration_count(void) {
  pll.holdover_duration = get_uptime() - pll.holdover_start_uptime;
  if (pll.holdover_duration > (uint32_t)nv_settings.holdover_status_threshold &&
      holdover_duration_exceeded == 0) {
    holdover_duration_exceeded = 1;
    status_happening(STATUS_HOLDOVER_THRESHOLD_SET);
    pwma_force_level(1);
  }
}

/* Whether the GPS reference is questionable, gps.references[0].valid 2,
 * and good: valid with its 1PPS alive, or questionable in a PLL stage;
 * its coming and going reported as satellites sufficient (revisions.md,
 * "Reference state") (0x49f0c).  The 58503A's. */
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

/* For a TI of 100 ns or more, asks for that many 100 ns steps against
 * it, the steps; one and 4 for 300 ns or more; 0 within 100 ns; any step
 * drops the interval's mean (0x49f76).  The 58503A's, but for the mean. */
uint8_t fine_sync_adjust(float ti) {
  static const float STEP = 1e-07f;
  static const float MOST = 3e-07f;
  uint8_t steps;

  if (float_compare(ti, STEP) < 0) {
    if (float_compare(pll_stages_negate(STEP), ti) < 0) {
      steps = 0;
    } else if (float_compare(pll_stages_negate(MOST), ti) < 0) {
      steps = float_to_unsigned(float_divide(ti, pll_stages_negate(STEP)));
      sync_request(COUNTER_SYNC_NEGATIVE, steps);
    } else {
      sync_request(COUNTER_SYNC_NEGATIVE, 1);
      steps = PLL_STAGES_SYNC_TOO_FAR;
    }
  } else if (float_compare(ti, MOST) < 0) {
    steps = float_to_unsigned(float_divide(ti, STEP));
    sync_request(COUNTER_SYNC_POSITIVE, steps);
  } else {
    sync_request(COUNTER_SYNC_POSITIVE, 1);
    steps = PLL_STAGES_SYNC_TOO_FAR;
  }
  if (steps != 0) {
    interval_mean_flag = 0;
  }
  return steps;
}

/* Syncs the outputs to GPS when the TI passes the limit threshold, or in
 * power-up 1 us, or a sync is asked for, dropping the interval's mean;
 * whether it did (0x4a050).  The other images have no power-up bound and
 * keep the mean. */
uint8_t sync_perform(float ti) {
  static const float POWERUP_BOUND = 1e-06f;

  if (pll.sync_requested == 0 &&
      float_compare(float_absolute_on_stack(ti), nv_settings.limit_threshold) <=
          0 &&
      (pll.stage != PLL_STAGE_POWERUP ||
       float_compare(float_absolute_on_stack(ti), POWERUP_BOUND) <= 0)) {
    return 0;
  }
  sync_request(COUNTER_SYNC_FULL, 1);
  pll.sync_requested = 0;
  interval_mean_flag = 0;
  if (pll_debug_enabled != 0) {
    task_dmessage("Peforming sync\r\n");
  }
  return 1;
}

/* Power-up's coarse step, which here only syncs: a reading armed with a
 * sync, then a plain one, and done (0x4a2fa).  The other images' coarse
 * measures and sets the frequency; this image's keeps their failure's
 * hold cause, for a result it never gives. */
pll_stage_result coarse(void) {
  pll_stage_result result = PLL_STAGE_RESULT_RUNNING;

  switch (pll.coarse_state) {
    case PLL_COARSE_SETTLE:
      interval_next_reading(INTERVAL_ARM_PLAIN, NULL);
      result = PLL_STAGE_RESULT_DONE;
      break;
    default:
      pll.coarse_state = PLL_COARSE_SYNC;
      /* fall through */
    case PLL_COARSE_SYNC:
      interval_next_reading(INTERVAL_ARM_SYNC, NULL);
      pll.coarse_state = PLL_COARSE_SETTLE;
      break;
  }
  if (result == PLL_STAGE_RESULT_FAILED) {
    pll.hold_causes |= HOLD_CAUSE_TI_ERROR;
  }
  return result;
}

/* A TI reading in seconds as the debug messages print it, in ns. */
static const float PLL_STAGES_NANOSECONDS = 1e+09f;

/* A measurement's failures, pll.fine_retries, past which fine gives up. */
static const uint16_t PLL_STAGES_MOST_RETRIES = 9;

/* fine's readings to its fit, at most, which it sets in pll.fine_limit
 * (0x4a482), and its first reading, fine_readings[0]. */
static const uint8_t PLL_STAGES_FINE_READINGS = 50;

static const uint8_t PLL_STAGES_FINE_FIRST = 1;

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
  pll.fine_index = PLL_STAGES_FINE_FIRST;
  fine_sum_ti = 0.0f;
  fine_sum_i_ti = 0.0f;
  fine_sum_i = 0.0f;
  fine_sum_i_squared = 0.0f;
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
                     float_multiply(integer_to_float(i), pll.fine_d));
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

/* After the fit's readings, their mean as interval_mean, and, with more
 * than one reading to the fit, whether the first lies within 300 ns of
 * the line; measures again, a retry counted, if not.  The image's check
 * is a loop over the readings that leaves after the first either way, so
 * only the first is checked. */
static void pll_stages_fine_fit_check(void) {
  static const float BAD_TI = 3e-07f;

  interval_mean =
      float_divide_reversed(unsigned_to_float(pll.fine_index), fine_sum_ti);
  interval_mean_flag = 1;
  pll.fine_index = PLL_STAGES_FINE_FIRST;
  if (pll.fine_index >= pll.fine_limit) {
    return;
  }
  float fitted = float_add(
      float_multiply(integer_to_float(pll.fine_index), fine_y0), fine_x0);
  float residual =
      float_subtract_reversed(fitted, fine_readings[pll.fine_index - 1]);

  if (float_compare(float_absolute_on_stack(residual), BAD_TI) > 0) {
    task_dmessage("Bad TI in frequency measurement\r\n");
    pll.fine_state = PLL_FINE_MEASURE_1;
    pll.fine_retries++;
  }
}

/* After the fit, the frequency, y0.  Out of power-up, failed when the
 * largest TI passes holdover's limit, its threshold and its frequency
 * tolerance over its duration, unless the limit is off, overridden or
 * the holdover long.  Then a frequency within 2e-9 slews the phase; one
 * within the frequency tolerance, or any in power-up, a long holdover or
 * with the limit overridden, moves the EFC by it and syncs if the TI
 * asks; another fails. */
static pll_stage_result pll_stages_fine_frequency(void) {
  static const float SLEW_FREQUENCY = 2e-09f;
  bool held = pll.holdover_duration <
                  (uint32_t)nv_settings.holdover_measurement_threshold &&
              pll.ti_limit_override == 0;

  if (pll.stage != PLL_STAGE_POWERUP) {
    float limit =
        float_add(float_multiply(unsigned_to_float(pll.holdover_duration),
                                 pll.frequency_tolerance),
                  nv_settings.holdover_limit_threshold);

    if (float_compare(limit, fine_ti_max) < 0 &&
        pll.holdover_duration <
            (uint32_t)nv_settings.holdover_measurement_threshold &&
        nv_settings.holdover_limit_state == HOLDOVER_LIMIT_ON &&
        pll.ti_limit_override == 0) {
      task_dmessage("fine - ti too far off\r\n");
      return PLL_STAGE_RESULT_FAILED;
    }
  }
  if (float_compare(float_absolute_on_stack(fine_y0), SLEW_FREQUENCY) < 0) {
    return pll_stages_fine_slew(double_to_float(interval_reading.value));
  }
  if (pll.stage != PLL_STAGE_POWERUP &&
      float_compare(float_absolute_on_stack(fine_y0), pll.frequency_tolerance) >
          0 &&
      held) {
    task_dmessage("fine - frequency too far off\r\n");
    return PLL_STAGE_RESULT_FAILED;
  }
  efc_set(efc_clamp(
      float_add(float_multiply(fine_y0, efc_gains.reciprocal_fine), pll.efc)));
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
 * line to up to 50 readings for the frequency, fewer once its slope
 * passes 500 ns over the readings so far, their mean the interval's,
 * then moves the EFC by it and measures again, or slews the phase once
 * the frequency is within 2e-9; a sync asked for starts it again but
 * while it fine-slews (0x4a356).  The 58503A's, but for the shorter fit,
 * its bound in pll.fine_limit, and the frequency taken from the fit's
 * slope, with the limit's override, through efc_set; the Z3815A's sets
 * the EFC by efc_for_frequency_change and checks the TI after a sync. */
pll_stage_result fine(void) {
  static const float FIT_SPAN = 5e-07f;
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
      pll.fine_limit = PLL_STAGES_FINE_READINGS;
      pll.fine_retries = 0;
      pll.fine_index = PLL_STAGES_FINE_FIRST;
      fine_sum_i = 0.0f;
      fine_sum_i_squared = 0.0f;
      fine_sum_ti = 0.0f;
      fine_sum_i_ti = 0.0f;
      fine_ti_max = 0.0f;
      fine_y0 = 0.0f;
      break;
    case PLL_FINE_MEASURE_DELAY:
      if (pll.fine_index <= pll.fine_limit &&
          float_compare(
              float_divide_reversed(integer_to_float(pll.fine_index), FIT_SPAN),
              float_absolute_on_stack(fine_y0)) > 0) {
        if (interval_next_reading(INTERVAL_ARM_CALIBRATE, &pll.fine_d) == 0) {
          result = pll_stages_fine_retry("fine - Error with measurement 1\r\n");
          pll_stages_fine_fit_restart();
        } else {
          pll_stages_fine_fit_add();
          pll.fine_index++;
        }
        break;
      }
      interval_next_reading(INTERVAL_ARM_CALIBRATE, NULL);
      pll.fine_state = PLL_FINE_MEASURE_2;
      pll_stages_fine_fit_check();
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
      pll.fine_state = PLL_FINE_START;
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

/* Readings before the clock is checked against GPS, as power-up counts
 * them, and between checks after a mismatch. */
static const uint8_t PLL_STAGES_CLOCK_CHECK_DELAY = 25;

static const uint8_t PLL_STAGES_CLOCK_RECHECK_DELAY = 2;

/* Reports the first satellite tracked, once. */
static void pll_stages_first_satellite(void) {
  uint8_t count;
  uint8_t pseudolite_count;

  if (first_satellite_logged != 0) {
    return;
  }
  uint16_t sr = set_interrupt_mask(CPU_IPL_1);

  if (!satellites_in_use(&count, &pseudolite_count, NULL, NULL)) {
    count = 0;
  }
  set_interrupt_mask(sr);
  if (count != 0) {
    status_happening(STATUS_FIRST_SATELLITE_SET);
    first_satellite_logged = 1;
  }
}

/* Logs when the GPS reference became valid, once, with the GPS's date and
 * time ("GPS reference valid at", log_first_satellite). */
static void pll_stages_reference_logged(void) {
  if (gps_reference_logged != 0 || gps_reference_good == 0) {
    return;
  }
  log_first_satellite(oncore_date_pack(gps.status.date), gps.status.time);
  gps_reference_logged = 1;
}

/* The hold causes that keep power-up waiting for GPS: the temporary mode,
 * the 1PPS selection and the EFC control mode. */
static const pll_hold_cause PLL_STAGES_POWERUP_HOLDS =
    HOLD_CAUSE_EFC_CONTROL | HOLD_CAUSE_1PPS_SELECTION |
    HOLD_CAUSE_TEMPORARY_MODE;

/* The hold causes power-up clears as it starts. */
static const pll_hold_cause PLL_STAGES_POWERUP_CLEARED_HOLDS =
    HOLD_CAUSE_GPS | HOLD_CAUSE_TI_ERROR | HOLD_CAUSE_TI_LIMIT_EXCEEDED |
    HOLD_CAUSE_HARDWARE_FAILURE | HOLD_CAUSE_GPS_COMMAND_MONITOR;

/* The power-up stage, stage 1: clears the EFC history, the aging term,
 * the loop's EFC step and the hold causes of a reading, waits for the
 * ovens and for GPS, sets the frequency finely and the clock from GPS,
 * then waits out a near leap second and the leap second's
 * acknowledgment; back to waiting for GPS while it is lost, or while the
 * temporary, 1PPS selection or EFC control mode holds it; done, it ends
 * the TI limit's override (0x4b102).  The Z3815A's goes from GPS to fine
 * as this does, leaving coarse to a state nothing sets, but keeps the
 * hold causes, waits on two modes and keeps the override; the oven's
 * test here is ocxo_warm, which gives 1. */
pll_stage_result powerup(bool same_stage) {
  static const float COARSE_TFOM = 0.001f;
  static const float FINE_TFOM = 2e-06f;
  pll_stage_result result = PLL_STAGE_RESULT_RUNNING;

  if (!same_stage || (int8_t)pll.powerup_state > PLL_POWERUP_LEAP_SECONDS) {
    /* The history and the two floats after it, the aging term and the
     * loop's EFC step (0x4b144). */
    memset(&pll.efc_history, 0,
           offsetof(pll_state, ti_filtered) - offsetof(pll_state, efc_history));
    pll.aging_state = PLL_AGING_START;
    pll.efc_history.loop_settled = 0;
    first_satellite_logged = 0;
    gps_reference_logged = 0;
    pll.hold_causes &= ~PLL_STAGES_POWERUP_CLEARED_HOLDS;
    pll.powerup_state = PLL_POWERUP_START;
  }
  switch (pll.powerup_state) {
    case PLL_POWERUP_INNER_OVEN:
      interval_next_reading(INTERVAL_ARM_PLAIN, NULL);
      if (ocxo_warm() != 0) {
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
        pll.powerup_state = PLL_POWERUP_FINE;
        pll.fine_state = PLL_FINE_START;
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
            leap_ack_ask();
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
          leap_ack_ask();
        }
      }
      break;
    case PLL_POWERUP_LEAP_SECONDS:
      interval_next_reading(INTERVAL_ARM_PLAIN, NULL);
      if (leap_ack_finished() != 0) {
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
       leap_ack_finished() != 0)) {
    pll.powerup_state = PLL_POWERUP_WAITING_FOR_GPS;
    result = PLL_STAGE_RESULT_RUNNING;
  }
  if (result == PLL_STAGE_RESULT_DONE) {
    pll.ti_limit_override = 0;
  }
  return result;
}

/* An EFC history entry's seconds, the aging fit's time unit. */
static const float PLL_STAGES_ENTRY_SECONDS = 2700.0f;

/* The aging fit's age at the uptime, y, in EFC history entries, and half
 * an entry, as a float. */
static float pll_stages_aging_age(void) {
  static const float HALF_ENTRY = 0.5f;

  return float_add(
      float_divide(unsigned_to_float(get_uptime()), PLL_STAGES_ENTRY_SECONDS),
      HALF_ENTRY);
}

/* The aging term ln y times the fit's c, the logarithm taken in double. */
static float pll_stages_aging_log_term(float age) {
  return float_multiply(double_to_float(log(float_to_double(age))),
                        pll.efc_history.fit_c);
}

/* The aging fit's age, y, past which its c ln y term counts. */
static const float PLL_STAGES_AGING_LOG_AGE = 1.0f;

/* The holdover stage: as it starts, the offset of the EFC, less its
 * temperature term and the aging fit's b y and c ln y, from the fit's a,
 * which it then takes off a fifteenth a block; then the EFC set by the
 * aging fit, its offset and the temperature term, and each block the
 * TFOM from the HQ and the holdover's age, and the hold causes from the
 * TI's block: done once none is left and the holdover is not manual
 * (0x488ba).  The block is pll.block_seconds long, and its mean the
 * valid readings' over their count.  The Z3815A's predicts the EFC from
 * the fit alone, its age and log term in double, keeps an aging limit,
 * scales the mean to the block, and reports the holds as it leaves. */
pll_stage_result holdover(bool same_stage) {
  static const float OFFSET_STEPS = 15.0f;
  static const float TWO = 2.0f;
  static const float HALF = 0.5f;
  static const float SECONDS_PER_DAY_SQUARED = 7.46496e+09f;
  static const float LEAST_TFOM = 1e-06f;
  pll_stage_result result = PLL_STAGE_RESULT_RUNNING;
  bool full_block = false;
  float ti;
  float current = adc_average(ADC_OSCILLATOR_CURRENT);

  if (!same_stage || (int8_t)pll.holdover_state > PLL_HOLDOVER_HOLDING) {
    pll.holdover_state = PLL_HOLDOVER_START;
  }
  if (pll.holdover_state != PLL_HOLDOVER_HOLDING) {
    holdover_ti_reset();
    if (pll_is_zero(pll.efc_history.fit_a)) {
      if (pll.efc_history.summed_count != 0) {
        pll.efc_history.fit_a =
            float_add(float_divide_reversed(
                          unsigned_to_float(pll.efc_history.summed_count),
                          pll.efc_history.efc_sum),
                      pll.efc_history.efc_first);
      } else {
        pll.efc_history.fit_a =
            float_subtract_reversed(tc_coefficient_product(current), pll.efc);
      }
    }
    float age = pll_stages_aging_age();
    float expected;

    if (float_compare(age, PLL_STAGES_AGING_LOG_AGE) > 0) {
      expected = float_subtract(
          float_subtract(
              float_subtract_reversed(tc_coefficient_product(current), pll.efc),
              float_multiply(pll.efc_history.fit_b, age)),
          pll_stages_aging_log_term(age));
    } else if (float_compare(age, 0.0f) > 0) {
      expected = float_subtract(
          float_subtract_reversed(tc_coefficient_product(current), pll.efc),
          float_multiply(pll.efc_history.fit_b, age));
    } else {
      expected =
          float_subtract_reversed(tc_coefficient_product(current), pll.efc);
    }
    pll.holdover_efc_offset = float_subtract(expected, pll.efc_history.fit_a);
    pll.holdover_efc_step = float_divide(pll.holdover_efc_offset, OFFSET_STEPS);
    interval_next_reading(INTERVAL_ARM_CALIBRATE, NULL);
    pll.holdover_state = PLL_HOLDOVER_HOLDING;
    holdover_steering.ti_mean = 0.0f;
    holdover_steering.seconds = 0;
    holdover_steering.readings = 0;
    return result;
  }
  holdover_duration_count();
  if (interval_next_reading(INTERVAL_ARM_CALIBRATE, &ti) == 0) {
    if (gps_pps_dead == 0 && internal_pps_dead == 0 &&
        interval_reading_replaced == 0) {
      task_dmessage("holdover - Error with measurement\r\n");
    }
  } else if (gps_reference_good != 0) {
    holdover_ti_add(ti);
    float_add_in_place(&holdover_steering.ti_mean, ti);
    holdover_steering.readings++;
  }
  holdover_steering.seconds++;
  if (holdover_steering.seconds < pll.block_seconds) {
    return result;
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
  float age = pll_stages_aging_age();
  float fitted;

  if (float_compare(age, PLL_STAGES_AGING_LOG_AGE) > 0) {
    float log_term = pll_stages_aging_log_term(age);

    fitted = float_add(
        float_add(float_add(pll.efc_history.fit_a, pll.holdover_efc_offset),
                  float_multiply(pll.efc_history.fit_b, age)),
        log_term);
  } else {
    fitted =
        float_add(float_add(pll.efc_history.fit_a, pll.holdover_efc_offset),
                  float_multiply(pll.efc_history.fit_b, age));
  }
  efc_set(efc_clamp(float_add(fitted, tc_coefficient_product(current))));
  if (!pll_is_zero(pll.holdover_efc_offset)) {
    if (float_compare(float_absolute_on_stack(pll.holdover_efc_offset),
                      float_absolute_on_stack(pll.holdover_efc_step)) > 0) {
      float_subtract_in_place(&pll.holdover_efc_offset, pll.holdover_efc_step);
    } else {
      pll.holdover_efc_offset = 0.0f;
    }
  }
  if (holdover_steering.readings != 0) {
    float_divide_in_place(&holdover_steering.ti_mean,
                          unsigned_to_float(holdover_steering.readings));
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
  } else if (holdover_steering.readings == pll.block_seconds) {
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
  holdover_steering.seconds = 0;
  holdover_steering.readings = 0;
  holdover_ti_reset();
  return result;
}

/* The holdover recovery stage: once a reading is good, the fine
 * frequency setting, then the clock checked against GPS and the leap
 * second's acknowledgment; done when the clock agrees, unless the
 * offset is unknown or, after a holdover longer than half the loop's
 * longest time constant, or with the TI past 500 ns, which restarts the
 * loop's time constant at initial_loop_time; done, it ends the TI
 * limit's override (0x48e90).  The Z3815A's waits a fixed 250 s, takes
 * the constant 150 and reports the holds as it leaves; here the leap
 * second's acknowledgment is asked for and tested through leap_ack_ask
 * and leap_ack_finished. */
pll_stage_result holdover_recovery(bool same_stage) {
  static const uint8_t MOST_FAILURES = 5;
  static const float CLOSE_TI = 5e-07f;
  pll_stage_result result = PLL_STAGE_RESULT_RUNNING;
  uint8_t valid = 0;
  float ti;

  if (!same_stage || (int8_t)pll.recovery_state > PLL_RECOVERY_CLOCK_CHECK) {
    pll.recovery_state = PLL_RECOVERY_WAITING;
    holdover_steering.seconds = 0;
    clock_gps_mismatch = 0;
  }
  switch (pll.recovery_state) {
    case PLL_RECOVERY_FINE:
      result = fine();
      if (result == PLL_STAGE_RESULT_DONE) {
        if (pll.leap_ack_again != 0) {
          pll.recovery_state = PLL_RECOVERY_LEAP_ACK;
          leap_ack_ask();
        } else {
          pll.recovery_state = PLL_RECOVERY_CLOCK_CHECK;
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
      pll.recovery_state = PLL_RECOVERY_LEAP_ACK;
      leap_ack_ask();
      break;
    case PLL_RECOVERY_LEAP_ACK:
      valid = interval_next_reading(INTERVAL_ARM_PLAIN, &ti);
      if (leap_ack_finished() != 0) {
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
      } else if (clock_gps_mismatch == 0) {
        pll.recovery_state = PLL_RECOVERY_LEAP_ACK;
        leap_ack_ask();
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
  holdover_duration_count();
  result = pll_stages_check_causes(result);
  if (result == PLL_STAGE_RESULT_DONE) {
    if (leap_second_near() != 0) {
      pll.recovery_state = PLL_RECOVERY_CHECK_DELAY;
      pll.recovery_countdown = PLL_STAGES_CLOCK_CHECK_DELAY;
      result = PLL_STAGE_RESULT_RUNNING;
    } else if (get_gps_utc_offset(NULL) == 0) {
      pll.recovery_state = PLL_RECOVERY_LEAP_ACK;
      leap_ack_ask();
      result = PLL_STAGE_RESULT_RUNNING;
    } else if (valid == 0) {
      result = PLL_STAGE_RESULT_FAILED;
      pll.hold_causes |= HOLD_CAUSE_TI_ERROR;
    } else if (float_to_unsigned(max_loop_time) / 2 < pll.holdover_duration ||
               float_compare(float_absolute_on_stack(ti), CLOSE_TI) > 0) {
      pll.loop_time_constant = initial_loop_time;
    }
  }
  if (result == PLL_STAGE_RESULT_DONE) {
    pll.ti_limit_override = 0;
  }
  return result;
}

/* A loop's one-second reading, valid or not.  A failed one, while the
 * reference is questionable, marks the block measurement_failed, and with
 * both 1PPS good and the reading not replaced by a wait is a measurement
 * failure (field_0f), its message printed.  A good one with GPS, not
 * questionable, is summed into the block within the hold threshold, else
 * printed; questionable, it marks the block.  Then, with GPS, not
 * questionable, pll.field_6f9 clear and no leap second acknowledgment
 * pending, the clock is checked against GPS at second 14 or after a
 * mismatch, and a check that has just set the clock, or
 * gps_month_changed's, asks for the leap second's acknowledgment again.
 * The Z3815A's tests no pll.field_6f9. */
static void pll_stages_loop_reading(uint8_t valid, float ti,
                                    char* failure_message) {
  static const uint32_t CHECK_SECOND = 14;
  static const uint32_t SECOND_MASK = 0xff;

  if (valid == 0) {
    if (gps_reference_questionable != 0) {
      loop_steering.measurement_failed = 1;
    }
    if (internal_pps_dead == 0 && gps_pps_dead == 0 &&
        interval_reading_replaced == 0) {
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
      gps_reference_questionable == 0 && pll.field_6f9 == 0 &&
      (clock_gps_mismatch != 0 ||
       (get_gps_time() & SECOND_MASK) == CHECK_SECOND) &&
      (((char)clock_check_against_gps() == 0 && clock_gps_mismatch == 0) ||
       (char)gps_month_changed() != 0)) {
    leap_ack_ask();
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

/* Steers the EFC from a block's mean: filters the TI into
 * pll.ti_filtered; filters the loop's EFC step toward the aging fit's
 * slope now, pll.aging_term, over the time constant, bounded by the step
 * limit, and integrates it with the filtered TI over the block's
 * pll.block_seconds; and sets the EFC from the filtered TI, the base, the
 * integral and the temperature term.  The Z3815A's filters the TI less
 * its setpoint and integrates over a block of 10 s. */
static void pll_stages_steer(pll_stages_gains gains, float current) {
  static const float ONE = 1.0f;
  static const float BLOCK_LENGTH = 10.0f;
  static const float MINUS_BLOCK_LENGTH = -10.0f;

  pll.ti_filtered = float_add(
      float_multiply(float_subtract(ONE, gains.filter_weight), pll.ti_filtered),
      float_multiply(gains.filter_weight, loop_steering.ti_sum));
  float kept = float_divide(
      float_multiply(float_add(pll.loop_time_constant, MINUS_BLOCK_LENGTH),
                     pll.loop_efc_step),
      pll.loop_time_constant);

  pll.loop_efc_step =
      float_add(kept, float_divide(float_multiply(pll.aging_term, BLOCK_LENGTH),
                                   pll.loop_time_constant));
  float slope = pll.loop_efc_step;

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
      float_multiply(
          float_multiply(drift, unsigned_to_float(pll.block_seconds)),
          gains.integral));
  float efc =
      float_add(float_add(float_multiply(gains.proportional, pll.ti_filtered),
                          pll.efc_base),
                loop_steering.integral);

  efc_set(efc_clamp(float_add(efc, tc_coefficient_product(current))));
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
  pll_stages_loop_start();
}

/* The loop's steps from a time constant to the next: integral + the
 * filtered TI's change of scale, 1 / previous_tau - 1 / tau. */
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

/* Starts the start-up loop afresh: the filtered TI and the integral
 * cleared, the EFC's base taken, the EFC less its temperature term. */
static void pll_stages_startup_restart(float current) {
  pll.ti_filtered = 0.0f;
  loop_steering.integral = 0.0f;
  pll.efc_base =
      float_subtract_reversed(tc_coefficient_product(current), pll.efc);
}

/* The start-up loop, stage 5: a proportional-integral loop on blocks of
 * pll.block_seconds whose time constant grows 5 s a block to
 * max_loop_time, settling once 15 blocks in a row are within 150 ns and
 * then waiting for 7200 s of uptime; a block past 500 ns starts it
 * settling again, at 150 s while settling and past it, at
 * initial_loop_time while lengthening; done at the full time constant.
 * A block with no reading summed steers nothing (0x4915a).  The
 * Z3815A's gives the loop the OCXO calibration's gain each block's first
 * second, restarts at the first time constant whatever the time
 * constant, and takes 10 s blocks. */
pll_stage_result startup_pll(bool same_stage) {
  static const int32_t SETTLED_BLOCKS = 15;
  static const float SETTLED_TI = 1.5e-07f;
  static const double LOST_TI = 5e-07;
  static const float LOST_TI_FLOAT = 5e-07f;
  static const float SETTLED_TFOM = 1e-06f;
  static const uint32_t SETTLED_UPTIME = 7200;
  static const float SETTLING_TIME_CONSTANT = 150.0f;
  static const float TAU_STEP = 5.0f;
  static const float MINUS_TAU_STEP = -5.0f;
  pll_stage_result result = PLL_STAGE_RESULT_RUNNING;
  float ti;
  float current = adc_average(ADC_OSCILLATOR_CURRENT);

  if (!same_stage || (int8_t)pll.startup_state > PLL_STARTUP_LENGTHENING) {
    pll.frequency_adjusted = 0;
    pll_stages_startup_restart(current);
    pll_stages_loop_start();
    loop_steering.settled_blocks = 0;
    interval_next_reading(INTERVAL_ARM_CALIBRATE, NULL);
    pll.startup_state = PLL_STARTUP_SETTLING;
    return pll_stages_check_causes(result);
  }
  uint8_t valid = interval_next_reading(INTERVAL_ARM_CALIBRATE, &ti);

  pll_stages_loop_reading(valid, ti,
                          "startup_pll - Error with measurement\r\n");
  loop_steering.seconds++;
  if (loop_steering.seconds < pll.block_seconds) {
    return pll_stages_check_causes(result);
  }
  if (pll_stages_loop_mean()) {
    switch (pll.startup_state) {
      case PLL_STARTUP_SETTLING:
        if (loop_steering.settled_blocks < SETTLED_BLOCKS) {
          if (float_compare(float_absolute_on_stack(loop_steering.ti_sum),
                            SETTLED_TI) < 0) {
            loop_steering.settled_blocks++;
          } else {
            loop_steering.settled_blocks = 0;
          }
        } else {
          if (pll.aging_state != PLL_AGING_COLLECTING) {
            pll.tfom = SETTLED_TFOM;
            pll.efc_history.loop_settled = 1;
          }
          pll.startup_state = PLL_STARTUP_WAITING_FOR_HISTORY;
        }
        if (double_compare(
                float_to_double(float_absolute_on_stack(loop_steering.ti_sum)),
                LOST_TI) < 0 &&
            float_compare(pll.loop_time_constant, SETTLING_TIME_CONSTANT) > 0) {
          pll_stages_startup_restart(current);
          pll.loop_time_constant = SETTLING_TIME_CONSTANT;
          pll.startup_state = PLL_STARTUP_SETTLING;
          loop_steering.settled_blocks = 0;
        }
        break;
      case PLL_STARTUP_WAITING_FOR_HISTORY:
        if (get_uptime() > SETTLED_UPTIME) {
          pll.startup_state = PLL_STARTUP_LENGTHENING;
        }
        break;
      case PLL_STARTUP_LENGTHENING:
        if (float_compare(float_add(max_loop_time, MINUS_TAU_STEP),
                          pll.loop_time_constant) <= 0) {
          if (result != PLL_STAGE_RESULT_FAILED) {
            pll.loop_time_constant = max_loop_time;
            pll_stages_rescale_integral(
                float_add(pll.loop_time_constant, MINUS_TAU_STEP));
            result = PLL_STAGE_RESULT_DONE;
          }
        } else if (float_compare(float_absolute_on_stack(loop_steering.ti_sum),
                                 LOST_TI_FLOAT) < 0) {
          float_add_in_place(&pll.loop_time_constant, TAU_STEP);
          pll_stages_rescale_integral(
              float_add(pll.loop_time_constant, MINUS_TAU_STEP));
        } else {
          pll_stages_startup_restart(current);
          pll.startup_state = PLL_STARTUP_SETTLING;
          loop_steering.settled_blocks = 0;
          pll.loop_time_constant = initial_loop_time;
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

/* The normal loop, stage 7: a proportional-integral loop on blocks of
 * pll.block_seconds at max_loop_time, which it takes as the loop's time
 * constant, its gains kept in loop_steering.  A block with no reading
 * summed steers nothing (0x498ee).  Its state, pll.normal_state, it
 * reaches through a pointer in its frame.  The Z3815A's leaves the time
 * constant, gives the loop the OCXO calibration's gain each block's first
 * second, and sets a frequency tolerance of 1e-7 where this sets 1e-8. */
pll_stage_result pll_normal(bool same_stage) {
  static const float NORMAL_TFOM = 1e-06f;
  static const float FREQUENCY_TOLERANCE = 1e-08f;
  float ti;
  float current = adc_average(ADC_OSCILLATOR_CURRENT);

  if (!same_stage || (int8_t)pll.normal_state > PLL_NORMAL_RUNNING) {
    pll.frequency_adjusted = 0;
    pll.tfom = NORMAL_TFOM;
    pll_stages_loop_start();
    interval_next_reading(INTERVAL_ARM_CALIBRATE, NULL);
    pll.normal_state = PLL_NORMAL_RUNNING;
    pll.loop_time_constant = max_loop_time;
    pll_stages_gains gains = pll_stages_gains_for(max_loop_time);

    loop_steering.proportional_gain = gains.proportional;
    loop_steering.integral_gain = gains.integral;
    loop_steering.filter_weight = gains.filter_weight;
    loop_steering.integral = 0.0f;
    pll.efc_base =
        float_subtract_reversed(tc_coefficient_product(current), pll.efc);
    pll.frequency_tolerance = FREQUENCY_TOLERANCE;
  } else {
    uint8_t valid = interval_next_reading(INTERVAL_ARM_CALIBRATE, &ti);

    pll_stages_loop_reading(valid, ti,
                            "pll_normal - Error with measurement\r\n");
    loop_steering.seconds++;
    if (loop_steering.seconds >= pll.block_seconds) {
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

/* The largest TI magnitude holdover has seen. */
float holdover_ti_max(void) { return holdover_steering.ti_largest; }
