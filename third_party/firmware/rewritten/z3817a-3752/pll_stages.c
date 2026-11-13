/* From the Z3815A 4010's rewritten/pll_stages.c, the Z3817A 3752's own beside
 * common/'s: of its functions, those the Z3817A's code carries identical or
 * relocated (docs/firmware/z3817a-3752.md, "The function check"); then the
 * loop's stages as this image changes them, from the Z3815A's and the 58503A
 * 3704's, each written from the image's code: power-up, holdover and its
 * recovery, and the start-up and normal loops on blocks of pll.block_length
 * readings ("The pulse", "The update law").  Its comments' addresses are the
 * Z3817A's, but where they name another image.  Each keeps its generated
 * prototype.  The floating arithmetic is calls to the image's soft-float
 * routines (docs/firmware/softfloat.md): float_subtract_reversed(a, b) is b -
 * a, float_divide_reversed(a, b) b / a. */

/* The EFC's gain at an EFC, a + b * efc^2 by the OCXO calibration, given
 * the loop (0x4d76c). */
void efc_gain_from_calibration(float efc) {
  efc_gain_set(float_add(
      float_multiply(
          float_multiply(scpi_value_DIAGnostic_ROSCillator_EFControl_BSLOPe,
                         efc),
          efc),
      scpi_value_DIAGnostic_ROSCillator_EFControl_ASLOPe));
}

/* An rts: here the one that ends 0x4d4b0, the reference check, which
 * nothing calls on its own (0x4d6c8).  The Z3815A's n3 lock pll and its
 * slew pass their offset to its copy; this image has neither stage. */
void n3_lock_offset_nothing(int32_t offset) {}

/* Whether the GPS reference is questionable, gps.references[0].valid 2
 * with the external record not valid, and good: either record valid
 * (the external's first, 0x4a14e) with the 1PPS alive, or the GPS's
 * questionable in a PLL stage; its coming and going reported as
 * satellites sufficient (revisions.md, "Reference state") (0x4a13a). */
void gps_reference_update(void) {
  gps_reference_validity valid;

  if (gps.references[GPS_RECEIVER_ALTERNATE].valid == GPS_REFERENCE_VALID) {
    valid = GPS_REFERENCE_VALID;
    gps_reference_questionable = 0;
  } else {
    valid = gps.references[GPS_RECEIVER_ONCORE].valid;
    gps_reference_questionable = valid == GPS_REFERENCE_QUESTIONABLE;
  }
  bool good =
      (valid == GPS_REFERENCE_VALID && gps_pps_dead == 0) ||
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

/* 0 when, in a holdover younger than the measurement threshold and with
 * no frequency correction applied, the TI ti has reached the hold
 * limit, the threshold plus the tolerance a second; else 1 (0x489b4). */
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

/* Clears holdover's one TI statistic, the largest magnitude, where the
 * other images keep the readings' sums and count beside it (0x48974). */
void holdover_ti_reset(void) { holdover_ti_largest = 0.0f; }

/* Keeps the largest magnitude of holdover's TI readings (0x4897c). */
void holdover_ti_add(float ti) {
  if (float_compare(float_absolute_on_stack(ti), holdover_ti_largest) > 0) {
    holdover_ti_largest = float_absolute_on_stack(ti);
  }
}

/* The largest TI magnitude holdover has seen (0x489ac). */
float holdover_ti_max(void) { return holdover_ti_largest; }

/* For a TI of 100 ns or more, asks for that many 100 ns steps against
 * it, the steps; one and 4 for 300 ns or more; 0 within 100 ns.  The
 * 58503A's, but that any step clears interval_mean_flag (0x4a27e)
 * (0x4a1b8). */
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

/* Syncs the outputs to the reference when the TI passes the limit
 * threshold or a sync is asked for, clearing interval_mean_flag; whether
 * it did (0x4a292). */
uint8_t sync_perform(float ti) {
  if (pll.sync_requested == 0 &&
      float_compare(float_absolute_on_stack(ti), nv_settings.limit_threshold) <=
          0) {
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

/* A TI reading in seconds as coarse's and fine's debug messages print
 * it, in ns. */
static const float PLL_STAGES_FINE_NANOSECONDS = 1e+09f;

/* A measurement's failures, pll.fine_retries, past which coarse and fine
 * give up. */
static const uint16_t PLL_STAGES_FINE_MOST_RETRIES = 9;

/* The seconds a pass is, by the pulse's mode: a reading each 2 s in
 * PP2S (docs/firmware/z3817a-3752.md, "The pulse"). */
static uint32_t pll_stages_pass_seconds(uint32_t passes) {
  return pll.pulse_mode == REFERENCE_PULSE_PP2S ? passes * 2 : passes;
}

/* The coarse frequency setting: syncs the counter, then measures the TI
 * across a pass, halved in PP2S (0x4a5f6), and sets the EFC from the
 * change (efc_for_frequency_change) until it is within 100 ns.  The
 * 58503A's states (0x4a516). */
pll_stage_result coarse(void) {
  static const float CLOSE_ENOUGH = 1e-07f;
  static const float PP2S_SECONDS = 2.0f;
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
        if (pll.fine_retries > PLL_STAGES_FINE_MOST_RETRIES) {
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
        if (pll.fine_retries <= PLL_STAGES_FINE_MOST_RETRIES) {
          pll.coarse_state = PLL_COARSE_FIRST;
        } else {
          result = PLL_STAGE_RESULT_FAILED;
        }
        interval_rearm(INTERVAL_ARM_CALIBRATE);
        break;
      }
      float_subtract_in_place(&pll.fine_d,
                              double_to_float(interval_reading.value));
      if (pll.pulse_mode == REFERENCE_PULSE_PP2S) {
        float_divide_in_place(&pll.fine_d, PP2S_SECONDS);
      }
      if (float_compare(CLOSE_ENOUGH, pll.fine_d) > 0 &&
          float_compare(pll.fine_d, pll_stages_negate(CLOSE_ENOUGH)) > 0) {
        result = PLL_STAGE_RESULT_DONE;
        interval_rearm(INTERVAL_ARM_CALIBRATE);
        break;
      }
      efc_set(efc_clamp(efc_for_frequency_change(pll.fine_d)));
      if (pll_debug_enabled != 0) {
        char message[80];
        int32_t efc = float_to_integer(pll.efc);
        double d = float_to_double(
            float_multiply(pll.fine_d, PLL_STAGES_FINE_NANOSECONDS));

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

/* :DIAGnostic:ROSCillator:TCOefficient times a value: the loop stages'
 * temperature term (0x4b234). */
float tc_coefficient_product(float value) {
  return float_multiply(scpi_value_DIAGnostic_ROSCillator_TCOefficient, value);
}

/* The EFC's full range, 2^20 DAC counts, in the calibration's nine steps
 * of a ninth of it and its ten points; and the ADC's two points, a tenth
 * and nine tenths of the range, eight tenths apart. */
static const int32_t PLL_STAGES_CALIBRATION_STEP = 116508;

static const float PLL_STAGES_CALIBRATION_STEP_FLOAT = 116508.0f;

static const float PLL_STAGES_CALIBRATION_HALF_STEP = 58254.0f;

static const uint8_t PLL_STAGES_CALIBRATION_STEPS = 9;

static const uint8_t PLL_STAGES_CALIBRATION_POINTS = 10;

static const float PLL_STAGES_CALIBRATION_STEPS_FLOAT = 9.0f;

static const float PLL_STAGES_EFC_LOW = 104857.5f;

static const float PLL_STAGES_EFC_HIGH = 943717.5f;

static const float PLL_STAGES_ADC_SPAN = -838860.0f;

/* Fits the OCXO calibration's a and b, the frequency's change per DAC
 * count at each step's middle against the middle's square, by least
 * squares over the nine steps: the Z3815A's, with no step printed
 * (0x4d79e). */
void efc_calibration_fit(void) {
  float sum_gain = 0.0f;
  float sum_square = 0.0f;
  float sum_product = 0.0f;
  float sum_square_square = 0.0f;

  for (int32_t i = 0; i < PLL_STAGES_CALIBRATION_STEPS; i++) {
    float gain =
        float_divide(float_subtract(efc_calibration.frequency_offset[i + 1],
                                    efc_calibration.frequency_offset[i]),
                     PLL_STAGES_CALIBRATION_STEP_FLOAT);
    float square = float_multiply(
        float_add(float_multiply(integer_to_float(i),
                                 PLL_STAGES_CALIBRATION_STEP_FLOAT),
                  PLL_STAGES_CALIBRATION_HALF_STEP),
        float_add(float_multiply(integer_to_float(i),
                                 PLL_STAGES_CALIBRATION_STEP_FLOAT),
                  PLL_STAGES_CALIBRATION_HALF_STEP));

    sum_gain = float_add(sum_gain, gain);
    sum_square = float_add(sum_square, square);
    float_add_in_place(&sum_product, float_multiply(gain, square));
    sum_square_square =
        float_add(sum_square_square, float_multiply(square, square));
  }
  scpi_value_DIAGnostic_ROSCillator_EFControl_BSLOPe = float_divide(
      float_subtract(
          float_multiply(sum_gain, sum_square),
          float_multiply(sum_product, PLL_STAGES_CALIBRATION_STEPS_FLOAT)),
      float_subtract(float_multiply(sum_square, sum_square),
                     float_multiply(sum_square_square,
                                    PLL_STAGES_CALIBRATION_STEPS_FLOAT)));
  scpi_value_DIAGnostic_ROSCillator_EFControl_ASLOPe = float_divide(
      float_subtract_reversed(
          float_multiply(scpi_value_DIAGnostic_ROSCillator_EFControl_BSLOPe,
                         sum_square),
          sum_gain),
      PLL_STAGES_CALIBRATION_STEPS_FLOAT);
}

/* Fits the OCXO calibration's a alone, the least-squares slope of the ten
 * points' readings against each point's EFC, in doubles, b 0, and a 0
 * when the sums give no slope; nothing calls it (0x4d8f8).  Both sums'
 * differences are taken the other way about, (sum_efc^2 - 10 sum_squares)
 * and (sum_offsets sum_efc - 10 sum_products), so the slope is the usual
 * one. */
void efc_calibration_fit_linear(void) {
  static const double POINTS = 10.0;
  double sum_offsets = 0.0;
  double sum_efc = 0.0;
  double sum_products = 0.0;
  double sum_squares = 0.0;

  for (int32_t i = 0; i < PLL_STAGES_CALIBRATION_POINTS; i++) {
    double efc = float_to_double(
        float_multiply(integer_to_float(i), PLL_STAGES_CALIBRATION_STEP_FLOAT));

    double_add_in_place(&sum_offsets,
                        float_to_double(efc_calibration.frequency_offset[i]));
    double_add_in_place(&sum_efc, efc);
    double_add_in_place(
        &sum_products,
        double_multiply_pointer(
            float_to_double(efc_calibration.frequency_offset[i]), &efc));
    double_add_in_place(&sum_squares, double_multiply_pointers(&efc, &efc));
  }
  scpi_value_DIAGnostic_ROSCillator_EFControl_BSLOPe = 0.0f;
  union libc_double spread = {
      .value = double_subtract_reversed(
          double_multiply_pointer(POINTS, &sum_squares),
          double_multiply_pointers(&sum_efc, &sum_efc))};

  if (((spread.word.high << 1) | spread.word.low) == 0) {
    scpi_value_DIAGnostic_ROSCillator_EFControl_ASLOPe = 0.0f;
    return;
  }
  scpi_value_DIAGnostic_ROSCillator_EFControl_ASLOPe =
      double_to_float(double_divide_pointer(
          double_subtract_reversed(
              double_multiply_pointer(POINTS, &sum_products),
              double_multiply_pointers(&sum_offsets, &sum_efc)),
          &spread.value));
}

/* The OCXO calibration's measurement, one step a call while its stage
 * runs; 1 when it is done or a point's reading fails, else 0 (0x4da40).
 * From the start, the EFC at 0, the puck's corrections off and the EFC
 * mode normal, it waits for a good GPS reference and the oven warmed up,
 * lets ten readings settle, then at each tenth of the EFC's range takes
 * two readings, the frequency offset from the difference of the second's
 * and the first's over 2; fits a and b; reads the ADC at a tenth and
 * nine tenths of the range for :DADC; then turns the corrections on,
 * saves the three and gives the loop the gain.  The Z3815A's steps, but
 * that the reference lost starts it again (0x4da8a), and that the three
 * are neither bounded nor logged and saved once.  Its done hook,
 * efc_calibration_done, is called with no arguments, so that it reads the
 * caller's caller's D2 and D3 as its index and source
 * (docs/firmware/quirks.md). */
uint8_t efc_calibration_measure(uint8_t running) {
  static const float DAC_PER_ADC_DEFAULT = 16.0f;
  static const uint8_t SETTLE = 10;
  int32_t index = CPU_CALLERS_REGISTER(int32_t, "d2");
  int32_t source = CPU_CALLERS_REGISTER(int32_t, "d3");
  float reading = 0.0f;
  char line[80];

  if (running == 0 || (int8_t)efc_calibration.step > EFC_CALIBRATION_CHECK ||
      (gps_reference_good == 0 &&
       efc_calibration.step != EFC_CALIBRATION_WAIT_REFERENCE)) {
    efc_calibration.step = EFC_CALIBRATION_START;
  }
  switch (efc_calibration.step) {
    case EFC_CALIBRATION_WAIT_REFERENCE:
      interval_next_reading(INTERVAL_ARM_PLAIN, NULL);
      if (gps_reference_good != 0 && oven_warmed_up()) {
        efc_calibration.settle = 0;
        efc_calibration.step = EFC_CALIBRATION_SETTLE;
      }
      break;
    case EFC_CALIBRATION_SETTLE:
      interval_next_reading(INTERVAL_ARM_PLAIN, NULL);
      if ((int8_t)efc_calibration.settle++ > SETTLE) {
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
        efc_calibration.step = EFC_CALIBRATION_START;
      }
      break;
    case EFC_CALIBRATION_SECOND_READING:
      if (interval_next_reading(INTERVAL_ARM_CALIBRATE, &reading) != 0) {
        efc_calibration.step = EFC_CALIBRATION_POINT;
      } else {
        efc_calibration.step = EFC_CALIBRATION_START;
      }
      break;
    case EFC_CALIBRATION_POINT:
      interval_wait_reading();
      if (interval_reading.valid == 0) {
        task_dmessage("efc cal - Error with measurement 2\r\n");
        return 1;
      }
      efc_calibration.frequency_offset[efc_calibration.point] = float_divide(
          float_subtract_reversed(
              double_to_float(interval_reading.value),
              efc_calibration.frequency_offset[efc_calibration.point]),
          2.0f);
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
      efc_calibration_fit();
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
      efc_calibration.step = EFC_CALIBRATION_ADC_LOW;
      break;
    case EFC_CALIBRATION_ADC_LOW:
      interval_next_reading(INTERVAL_ARM_CALIBRATE, &reading);
      if (++efc_calibration.point > SETTLE) {
        efc_calibration.point = 0;
        efc_calibration.step = EFC_CALIBRATION_ADC_HIGH;
        efc_calibration.adc_low = efc_adc_reading;
        efc_set(PLL_STAGES_EFC_HIGH);
      }
      break;
    case EFC_CALIBRATION_ADC_HIGH:
      interval_next_reading(INTERVAL_ARM_CALIBRATE, &reading);
      if (++efc_calibration.point > SETTLE) {
        efc_calibration.step = EFC_CALIBRATION_CHECK;
        efc_calibration.adc_high = efc_adc_reading;
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
    case EFC_CALIBRATION_CHECK:
      s3_test_mode_set(1);
      efc_calibration_done(index, source);
      efc_gain_from_calibration(pll.efc);
      return 1;
    case EFC_CALIBRATION_START:
    default:
      scpi_value_DIAGnostic_ROSCillator_EFControl_DADC = DAC_PER_ADC_DEFAULT;
      s3_test_mode_set(0);
      diag_efc_mode = EFC_MODE_NORMAL;
      scpi_value_DIAGnostic_ROSCillator_EFControl_DATA = 0;
      efc_calibration.point = 0;
      efc_set(0.0f);
      interval_next_reading(INTERVAL_ARM_SYNC, NULL);
      efc_calibration.step = EFC_CALIBRATION_WAIT_REFERENCE;
      break;
  }
  return 0;
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
 * (0x4dda6).  With B zero, pll.efc + delta_f / A.  Else the real root of
 * e^3 + p e = q, p = 3A/B, q = 3(delta_f + A efc)/B + efc^3, by Cardano:
 * with h = q/2, k = A/B and D = h^2 + k^3, cbrt(h + sqrt D) +
 * cbrt(h - sqrt D), each cube root of a negative the negated root of its
 * magnitude; with D not positive, three real roots, pll.efc itself.  The
 * Z3815A's, with nothing printed. */
float efc_for_frequency_change(float delta_f) {
  double efc = float_to_double(pll.efc);
  union libc_double slope = {
      .value =
          float_to_double(scpi_value_DIAGnostic_ROSCillator_EFControl_BSLOPe)};

  if (((slope.word.high << 1) | slope.word.low) == 0) {
    return double_to_float(double_add_pointer(
        float_to_double(float_divide(
            delta_f, scpi_value_DIAGnostic_ROSCillator_EFControl_ASLOPe)),
        &efc));
  }
  double k = float_to_double(
      float_divide(scpi_value_DIAGnostic_ROSCillator_EFControl_ASLOPe,
                   scpi_value_DIAGnostic_ROSCillator_EFControl_BSLOPe));
  double q = double_add(
      double_multiply(
          double_add(
              float_to_double(delta_f),
              double_multiply_pointer(
                  float_to_double(
                      scpi_value_DIAGnostic_ROSCillator_EFControl_ASLOPe),
                  &efc)),
          double_divide_reversed(
              float_to_double(
                  scpi_value_DIAGnostic_ROSCillator_EFControl_BSLOPe),
              3.0)),
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

  if (double_compare(0.0, double_add_pointers(&h, &s)) > 0) {
    root = pll_stages_cube_root(double_add_pointers(&h, &s));
  } else {
    root = pll_stages_negate_double(pll_stages_cube_root(
        pll_stages_negate_double(double_add_pointers(&h, &s))));
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

/* A failed measurement of fine's, its message printed: failed past the
 * most retries. */
static pll_stage_result pll_stages_fine_retry(char* message) {
  task_dmessage(message);
  pll.fine_retries++;
  return pll.fine_retries > PLL_STAGES_FINE_MOST_RETRIES
             ? PLL_STAGE_RESULT_FAILED
             : PLL_STAGE_RESULT_RUNNING;
}

/* Starts fine's fit again from its first reading; the largest TI is
 * kept. */
static void pll_stages_fine_fit_restart(void) {
  pll.fine_index = 1;
  fine_sum_ti = 0.0f;
  fine_sum_i_ti = 0.0f;
  fine_sum_i = 0.0f;
  fine_sum_i_squared = 0.0f;
}

/* Slews the phase toward a TI: done within 100 ns, else fine_sync_adjust's
 * steps of 100 ns, one a pass, or a slew again when it is 300 ns or
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
 * readings against their time t, i passes in seconds (0x4a860), TI = x0
 * + y0 t: its sums, then the slope y0, the fractional frequency (0 when
 * the sums give no slope), and the intercept x0, over i readings; the
 * largest TI magnitude kept in fine_ti_max. */
static void pll_stages_fine_fit_add(void) {
  uint8_t i = pll.fine_index;
  uint32_t t = pll_stages_pass_seconds(i);

  if (float_compare(float_absolute_on_stack(pll.fine_d), fine_ti_max) > 0) {
    fine_ti_max = float_absolute_on_stack(pll.fine_d);
  }
  fine_readings[i - 1] = pll.fine_d;
  float_add_in_place(&fine_sum_i, integer_to_float(t));
  float_add_in_place(&fine_sum_i_squared, integer_to_float(t * t));
  float_add_in_place(&fine_sum_ti, pll.fine_d);
  float_add_in_place(&fine_sum_i_ti,
                     float_multiply(integer_to_float(t), pll.fine_d));
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
    double ti = double_multiply((double)PLL_STAGES_FINE_NANOSECONDS,
                                float_to_double(pll.fine_d));

    debug_sprintf(message, "i= %d, ti= %.1f, y0= %.2e, x0= %.2e\r\n",
                  (uint32_t)i, ti, y0, x0);
    task_dmessage(message);
  }
}

/* After the fit's readings, their mean as interval_mean, and whether the
 * first lies within 300 ns of the line; measures again, a retry counted,
 * if not.  The image's check is a loop over the readings that leaves
 * after the first either way, so only the first is checked, and none
 * with fewer than two readings (0x4ab6e). */
static void pll_stages_fine_fit_check(void) {
  static const float BAD_TI = 3e-07f;

  interval_mean =
      float_divide_reversed(unsigned_to_float(pll.fine_index), fine_sum_ti);
  interval_mean_flag = 1;
  pll.fine_index = 1;
  if (pll.fine_index >= pll.fine_readings) {
    return;
  }
  float fitted = float_add(
      float_multiply(integer_to_float(pll_stages_pass_seconds(pll.fine_index)),
                     fine_y0),
      fine_x0);
  float residual =
      float_subtract_reversed(fitted, fine_readings[pll.fine_index - 1]);

  if (float_compare(float_absolute_on_stack(residual), BAD_TI) > 0) {
    task_dmessage("Bad TI in frequency measurement\r\n");
    pll.fine_state = PLL_FINE_MEASURE_1;
    pll.fine_retries++;
  }
}

/* After the fit, the frequency y0.  Out of power-up, failed when the
 * largest TI passes holdover's limit, its threshold and its frequency
 * tolerance over its duration, unless the limit is off, overridden
 * (pll.ti_limit_override) or the holdover long.  Then a frequency within
 * 2e-9 slews the phase; one within the frequency tolerance, or any in
 * power-up or a long holdover, sets the EFC from it
 * (efc_for_frequency_change) and syncs if the TI asks; another fails.
 * The Z3815A's tests, but that the tolerance's is not lifted by the
 * override (0x4ac48), and pll.fine_d is not computed. */
static pll_stage_result pll_stages_fine_frequency(void) {
  static const float SLEW_FREQUENCY = 2e-09f;
  bool long_holdover = (uint32_t)nv_settings.holdover_measurement_threshold <=
                       pll.holdover_duration;

  if (pll.stage != PLL_STAGE_POWERUP) {
    float limit =
        float_add(float_multiply(unsigned_to_float(pll.holdover_duration),
                                 pll.frequency_tolerance),
                  nv_settings.holdover_limit_threshold);

    if (float_compare(limit, fine_ti_max) < 0 && !long_holdover &&
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
      !long_holdover) {
    task_dmessage("fine - frequency too far off\r\n");
    return PLL_STAGE_RESULT_FAILED;
  }
  efc_set(efc_clamp(efc_for_frequency_change(fine_y0)));
  pll.frequency_adjusted = 1;
  if (pll_debug_enabled != 0) {
    char message[80];
    int32_t efc = float_to_integer(pll.efc);
    double y0 =
        float_to_double(float_multiply(fine_y0, PLL_STAGES_FINE_NANOSECONDS));

    debug_sprintf(message, "y0 = %f, efc =  %d\r\n", y0, efc);
    task_dmessage(message);
  }
  pll.fine_state = sync_perform(double_to_float(interval_reading.value))
                       ? PLL_FINE_SYNC_1
                       : PLL_FINE_SYNC_2;
  return PLL_STAGE_RESULT_RUNNING;
}

/* The fine frequency setting: syncs the outputs if the TI asks, fits a
 * line to 50 readings, 25 in PP2S (0x4a81e), for the frequency, fewer
 * once its slope passes 500 ns over the seconds so far, their mean the
 * interval's, then sets the EFC from it and measures again, or slews the
 * phase once the frequency is within 2e-9; a sync asked for starts it
 * again but while it fine-slews (0x4a6f2). */
pll_stage_result fine(void) {
  static const float FIT_SPAN = 5e-07f;
  /* fine's readings to its fit, in 1PPS and in PP2S */
  static const uint8_t READINGS = 50;
  static const uint8_t READINGS_PP2S = 25;
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
      pll.fine_readings =
          pll.pulse_mode == REFERENCE_PULSE_PP2S ? READINGS_PP2S : READINGS;
      pll.fine_retries = 0;
      pll.fine_index = 1;
      fine_sum_i = 0.0f;
      fine_sum_i_squared = 0.0f;
      fine_sum_ti = 0.0f;
      fine_sum_i_ti = 0.0f;
      fine_ti_max = 0.0f;
      fine_y0 = 0.0f;
      break;
    case PLL_FINE_MEASURE_DELAY:
      if (pll.fine_index <= pll.fine_readings &&
          float_compare(
              float_divide_reversed(
                  integer_to_float(pll_stages_pass_seconds(pll.fine_index)),
                  FIT_SPAN),
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

/* The current the stages give the temperature correction,
 * tc_coefficient_product: 0.0, this image having no oscillator-current
 * channel (docs/firmware/z3817a-3752.md, "The update law"; 0x48bec,
 * 0x49c12). */
static const float PLL_STAGES_CURRENT = 0.0f;

static const float PLL_STAGES_NANOSECONDS = 1e+09f;

static const float PLL_STAGES_ONE = 1.0f;

/* The EFC less its temperature correction, which is 0 here (0x48bec). */
static float pll_stages_efc_less_temperature(void) {
  return float_subtract_reversed(tc_coefficient_product(PLL_STAGES_CURRENT),
                                 pll.efc);
}

/* The aging fit's y at the uptime: entries of 2700 s, and half an entry
 * (0x48c02). */
static float pll_stages_aging_age(void) {
  static const float HALF_ENTRY = 0.5f;

  return float_add(
      float_divide(unsigned_to_float(get_uptime()), PLL_ENTRY_SECONDS),
      HALF_ENTRY);
}

/* Counts holdover's seconds by the uptime since it started, and reports
 * it past the status threshold, once (0x48b1a). */
void holdover_duration_count(void) {
  pll.holdover_duration = get_uptime() - pll.holdover_start_uptime;
  if (pll.holdover_duration > (uint32_t)nv_settings.holdover_status_threshold &&
      holdover_duration_exceeded == 0) {
    holdover_duration_exceeded = 1;
    status_happening(STATUS_HOLDOVER_THRESHOLD_SET);
  }
}

/* The log event for holdover on the GPS's loss, by the GPS reference's
 * state (the jump table at 0x48a8e), any other state the GPS's 1PPS,
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
 * order (0x48a5e to 0x48af8). */
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

/* Enters holdover: but from recovery, its duration and its start's
 * uptime restarted and the holdover logged by its cause; manual when a
 * manual hold cause or no automatic recovery asks it; and the interval's
 * mean dropped for any cause but the manual one (0x48a10). */
void enter_holdover(void) {
  const bool started = pll.stage != PLL_STAGE_HOLDOVER_RECOVERY;

  if (started) {
    pll.holdover_duration = 0;
    pll.holdover_start_uptime = get_uptime();
  }
  pll.manual = (pll.hold_causes & HOLD_CAUSE_MANUAL) != HOLD_CAUSE_NONE ||
               holdover_recovery_auto == 0;
  pll.stage = PLL_STAGE_HOLDOVER;
  if (started) {
    log_event(pll_stages_holdover_event());
  }
  if ((pll.hold_causes & ~HOLD_CAUSE_MANUAL) != HOLD_CAUSE_NONE) {
    interval_mean_flag = 0;
  }
}

/* holdover's start: the aging fit's a, when it has none, from the EFC
 * history's entry being summed or else the EFC; and the offset of the EFC
 * from the fit's value now, a + b y + c ln y past the first entry, a + b
 * y after it, a before, with its step, a fifteenth (0x48bb8 to 0x48cfa). */
static void pll_stages_holdover_start(void) {
  static const float OFFSET_STEPS = 15.0f;
  efc_ring* const history = &pll.efc_history;

  if (pll_is_zero(history->fit_a)) {
    if (history->summed_count != 0) {
      history->fit_a = float_add(
          float_divide_reversed(unsigned_to_float(history->summed_count),
                                history->efc_sum),
          history->efc_origin);
    } else {
      history->fit_a = pll_stages_efc_less_temperature();
    }
  }
  float age = pll_stages_aging_age();
  float efc;

  if (float_compare(age, PLL_STAGES_ONE) > 0) {
    float line = float_subtract(pll_stages_efc_less_temperature(),
                                float_multiply(history->fit_b, age));

    efc = float_subtract(
        line, float_multiply(double_to_float(log(float_to_double(age))),
                             history->fit_c));
  } else if (float_compare(age, 0.0f) > 0) {
    efc = float_subtract(pll_stages_efc_less_temperature(),
                         float_multiply(history->fit_b, age));
  } else {
    efc = pll_stages_efc_less_temperature();
  }
  pll.holdover_offset = float_subtract(efc, history->fit_a);
  pll.holdover_offset_step = float_divide(pll.holdover_offset, OFFSET_STEPS);
}

/* The EFC holdover predicts: the aging fit's value at the uptime with the
 * offset, a + offset + b y, and c ln y past the first entry, and the
 * temperature correction (0x48e02 to 0x48ebc). */
static float pll_stages_holdover_efc(void) {
  efc_ring* const history = &pll.efc_history;
  float age = pll_stages_aging_age();
  float efc;

  if (float_compare(age, PLL_STAGES_ONE) > 0) {
    float log_term = float_multiply(double_to_float(log(float_to_double(age))),
                                    history->fit_c);

    efc = float_add(float_add(float_add(history->fit_a, pll.holdover_offset),
                              float_multiply(history->fit_b, age)),
                    log_term);
  } else {
    efc = float_add(float_add(history->fit_a, pll.holdover_offset),
                    float_multiply(history->fit_b, age));
  }
  return float_add(efc, tc_coefficient_product(PLL_STAGES_CURRENT));
}

/* Starts holdover's block afresh. */
static void pll_stages_holdover_block_start(void) {
  holdover_ti_mean = 0.0f;
  holdover_seconds = 0;
  holdover_readings = 0;
}

/* The holdover stage (0x48b58): sets the EFC by the aging fit and the
 * offset holdover starts with, which it works off a fifteenth a block;
 * each block, of pll.block_length readings, the TFOM from the predicted
 * uncertainty and the holdover's age, and the hold causes from the
 * block's readings: done once none is left and the holdover is not
 * manual.  The Z3815A's predicts from the fit alone, sums the readings
 * over 10 and keeps the aging limits; this image's divides the sum by
 * its readings and asks for no output inactive. */
pll_stage_result holdover(bool same_stage) {
  static const float TWO = 2.0f;
  static const float HALF = 0.5f;
  static const float SECONDS_PER_DAY_SQUARED = 7.46496e+09f;
  static const float LEAST_TFOM = 1e-06f;
  pll_stage_result result = PLL_STAGE_RESULT_RUNNING;
  bool full_block = false;
  float ti;

  if (!same_stage || (int8_t)pll.holdover_state > PLL_HOLDOVER_HOLDING) {
    pll.holdover_state = PLL_HOLDOVER_START;
  }
  if (pll.holdover_state != PLL_HOLDOVER_HOLDING) {
    holdover_ti_reset();
    pll_stages_holdover_start();
    interval_next_reading(INTERVAL_ARM_CALIBRATE, NULL);
    pll.holdover_state = PLL_HOLDOVER_HOLDING;
    pll_stages_holdover_block_start();
    return result;
  }
  holdover_duration_count();
  if (interval_next_reading(INTERVAL_ARM_CALIBRATE, &ti) != 0) {
    if (gps_reference_good != 0) {
      holdover_ti_add(ti);
      float_add_in_place(&holdover_ti_mean, ti);
      holdover_readings++;
    }
  } else if (gps_pps_dead == 0 && internal_pps_dead == 0 &&
             interval_reading_replaced == 0) {
    task_dmessage("holdover - Error with measurement\r\n");
  }
  holdover_seconds++;
  if (holdover_seconds < pll.block_length) {
    return result;
  }
  float duration_squared =
      float_multiply(unsigned_to_float(pll.holdover_duration),
                     unsigned_to_float(pll.holdover_duration));

  pll.tfom = float_multiply(
      float_multiply(
          float_divide(float_multiply(pll.predicted_uncertainty, TWO),
                       SECONDS_PER_DAY_SQUARED),
          duration_squared),
      HALF);
  if (float_compare(LEAST_TFOM, pll.tfom) > 0) {
    pll.tfom = LEAST_TFOM;
  }
  efc_set(efc_clamp(pll_stages_holdover_efc()));
  if (!pll_is_zero(pll.holdover_offset)) {
    if (float_compare(float_absolute_on_stack(pll.holdover_offset),
                      float_absolute_on_stack(pll.holdover_offset_step)) > 0) {
      float_subtract_in_place(&pll.holdover_offset, pll.holdover_offset_step);
    } else {
      pll.holdover_offset = 0.0f;
    }
  }
  if (holdover_readings != 0) {
    float_divide_in_place(&holdover_ti_mean,
                          unsigned_to_float(holdover_readings));
  }
  if (pll_debug_enabled != 0) {
    char message[100];
    uint32_t date;
    time_of_day time;

    get_date_time(&date, &time);
    double current_double = float_to_double(PLL_STAGES_CURRENT);
    double ti_ns = float_to_double(
        float_multiply(holdover_ti_mean, PLL_STAGES_NANOSECONDS));

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
  } else if (holdover_readings == pll.block_length) {
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
  if (full_block) {
    interval_mean = holdover_ti_mean;
    interval_mean_flag = 1;
  } else {
    interval_mean_flag = 0;
  }
  pll_stages_holdover_block_start();
  holdover_ti_reset();
  return result;
}

/* Readings before the clock is checked against GPS, as power-up and
 * holdover recovery count them, and between checks after a mismatch. */
static const uint8_t PLL_STAGES_CLOCK_CHECK_DELAY = 25;

static const uint8_t PLL_STAGES_CLOCK_RECHECK_DELAY = 2;

/* The holdover recovery stage (0x49126): once a reading is good, the fine
 * frequency setting, then the clock checked against GPS and the leap
 * second's acknowledgment; done when the clock agrees, unless the offset
 * is unknown, and then, after a holdover longer than half max_loop_time
 * or with the TI past 500 ns, the loop's time constant starts again from
 * initial_loop_time; done, it ends the TI limit's override.  Its failed
 * readings it counts in holdover_seconds.  The Z3815A's restarts the time
 * constant after 250 s or within 500 ns, sets a GPS command monitor hold
 * while it waits for the leap second, and asks for the output inactive. */
pll_stage_result holdover_recovery(bool same_stage) {
  static const uint8_t MOST_FAILURES = 5;
  pll_stage_result result = PLL_STAGE_RESULT_RUNNING;
  uint8_t valid = 0;
  float ti;
  bool ask_leap_ack = false;

  if (!same_stage || (int8_t)pll.recovery_state > PLL_RECOVERY_CLOCK_CHECK) {
    pll.recovery_state = PLL_RECOVERY_WAITING;
    holdover_seconds = 0;
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
        holdover_seconds++;
        if (holdover_seconds > MOST_FAILURES) {
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
  if (result == PLL_STAGE_RESULT_DONE) {
    if (leap_second_near() != 0) {
      pll.recovery_state = PLL_RECOVERY_CHECK_DELAY;
      pll.recovery_countdown = PLL_STAGES_CLOCK_CHECK_DELAY;
      result = PLL_STAGE_RESULT_RUNNING;
    } else if (get_gps_utc_offset(NULL) == 0) {
      pll.recovery_state = PLL_RECOVERY_LEAP_ACK;
      pll.leap_ack_requested = 1;
      result = PLL_STAGE_RESULT_RUNNING;
    } else if (valid == 0) {
      result = PLL_STAGE_RESULT_FAILED;
      pll.hold_causes |= HOLD_CAUSE_TI_ERROR;
    } else if (pll.holdover_duration > float_to_unsigned(max_loop_time) / 2 ||
               float_compare(float_absolute_on_stack(ti), PLL_LOST_TI) > 0) {
      pll.loop_time_constant = initial_loop_time;
    }
  }
  if (result == PLL_STAGE_RESULT_DONE) {
    pll.ti_limit_override = 0;
  }
  return result;
}

/* A reading of the start-up or normal loop (0x49492 to 0x4960e,
 * 0x49c34 to 0x49da8): failed, it marks the block questionable or, with
 * both 1PPS alive and the reading not replaced, failed, its message
 * printed; good, with the GPS reference good, it marks the block
 * questionable, or is summed when it is within the hold threshold or no
 * limit holds, else printed.  Then, with GPS, not questionable, no leap
 * second acknowledgment pending and field_3e9 clear, the clock is
 * checked against GPS at second 14 or after a mismatch, and a check that
 * has just set the clock asks gps_leap_ack to measure GPS-UTC again.  The
 * Z3815A's asks it after gps_month_changed too, and tests no field_3e9. */
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
      gps_reference_questionable == 0 && pll.field_3e9 == 0 &&
      (clock_gps_mismatch != 0 ||
       (get_gps_time() & SECOND_MASK) == CHECK_SECOND) &&
      (char)clock_check_against_gps() == 0 && clock_gps_mismatch == 0) {
    pll.leap_ack_requested = 1;
  }
}

/* Gives the loop the OCXO calibration's gain in each block's first
 * second. */
static void pll_stages_loop_gain(void) {
  static const uint8_t GAIN_SECOND = 1;

  if (loop_steering.seconds == GAIN_SECOND) {
    efc_gain_from_calibration(pll.efc);
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

static pll_stages_gains pll_stages_gains_for(float time_constant) {
  static const float FILTER_SCALE = 29.75f;
  static const float INTEGRAL_SCALE = 4.0f;

  return (pll_stages_gains){
      .proportional = float_divide(efc_gains.reciprocal_ti, time_constant),
      .integral = float_divide(
          float_divide(float_divide(efc_gains.reciprocal_tau, time_constant),
                       time_constant),
          INTEGRAL_SCALE),
      .filter_weight = float_divide(FILTER_SCALE, time_constant),
  };
}

/* Steers the EFC from a block's mean, with no setpoint: filters it into
 * pll.ti_filtered; filters the loop's EFC step toward the aging fit's
 * slope now, pll.aging_term, over the ramped time constant, bounded by
 * the step limit, and integrates it with the filtered TI, times the
 * block's length, pll.block_length; and sets the EFC from the filtered
 * TI, the base, the integral and the temperature correction
 * (docs/firmware/z3817a-3752.md, "The update law"; 0x49886 to 0x49a2a,
 * 0x49e28 to 0x49f8c).  The Z3815A's subtracts its setpoint and
 * multiplies the integral's step by 10. */
static void pll_stages_steer(pll_stages_gains gains) {
  static const float MINUS_BLOCK_SECONDS = -10.0f;
  static const float BLOCK_SECONDS = 10.0f;
  float error = loop_steering.ti_sum;

  pll.ti_filtered = float_add(
      float_multiply(float_subtract(PLL_STAGES_ONE, gains.filter_weight),
                     pll.ti_filtered),
      float_multiply(gains.filter_weight, error));
  float kept = float_divide(
      float_multiply(float_add(pll.loop_time_constant, MINUS_BLOCK_SECONDS),
                     pll.loop_efc_step),
      pll.loop_time_constant);

  pll.loop_efc_step = float_add(
      kept, float_divide(float_multiply(pll.aging_term, BLOCK_SECONDS),
                         pll.loop_time_constant));
  float slope = pll.loop_efc_step;

  if (float_compare(slope, efc_gains.step_limit) > 0) {
    slope = efc_gains.step_limit;
  } else if (float_compare(pll_stages_negate(efc_gains.step_limit), slope) >
             0) {
    slope = pll_stages_negate(efc_gains.step_limit);
  }
  float drift = float_add(
      float_divide(float_divide(slope, gains.integral), PLL_ENTRY_SECONDS),
      pll.ti_filtered);

  float_add_in_place(
      &loop_steering.integral,
      float_multiply(float_multiply(drift, unsigned_to_float(pll.block_length)),
                     gains.integral));
  float efc = float_add(
      float_add(float_add(float_multiply(gains.proportional, pll.ti_filtered),
                          pll.efc_base),
                loop_steering.integral),
      tc_coefficient_product(PLL_STAGES_CURRENT));

  efc_set(efc_clamp(efc));
}

/* Ends a loop block: prints the EFC, the block's mean TI, the time
 * constant loop_time and the current, then starts the next block. */
static void pll_stages_block_end(float loop_time) {
  if (pll_debug_enabled != 0) {
    char message[100];
    uint32_t date;
    time_of_day time;

    get_date_time(&date, &time);
    double current_double = float_to_double(PLL_STAGES_CURRENT);
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
 * filtered TI's change of scale, 1 / (tau - 5) - 1 / tau. */
static void pll_stages_rescale_integral(float previous_tau) {
  static const float MINUS_ONE = -1.0f;
  float scale = float_add(float_divide_reversed(previous_tau, PLL_STAGES_ONE),
                          float_divide(MINUS_ONE, pll.loop_time_constant));

  float_add_in_place(
      &loop_steering.integral,
      float_multiply(float_multiply(efc_gains.reciprocal_ti, pll.ti_filtered),
                     scale));
}

/* Starts the start-up loop afresh: the filtered TI and the integral
 * cleared, the EFC's base taken. */
static void pll_stages_startup_restart(void) {
  pll.ti_filtered = 0.0f;
  loop_steering.integral = 0.0f;
  pll.efc_base = pll_stages_efc_less_temperature();
}

/* The start-up loop, stage 5 (0x493da): a proportional-integral loop on
 * blocks of pll.block_length readings whose time constant grows 5 s a
 * block from initial_loop_time to max_loop_time; settling once 15 blocks
 * in a row are within 150 ns, then, unless the aging fit holds, waiting
 * for the EFC history's first three entries; a block of 500 ns or more
 * while it grows starts it settling again at initial_loop_time; done at
 * the full time constant.  It starts settling when the ramped constant
 * is initial_loop_time, else growing it.  A block with no reading summed
 * steers nothing; the OCXO calibration's gain is given the loop each
 * block's first second.  The Z3815A's always starts settling, starts
 * again from a block past 500 ns while settling, and waits for 7200 s of
 * uptime. */
pll_stage_result startup_pll(bool same_stage) {
  static const int32_t SETTLED_BLOCKS = 15;
  static const float SETTLED_TI = 1.5e-07f;
  static const float SETTLED_TFOM = 1e-06f;
  static const uint8_t HISTORY_ENTRIES = 3;
  static const float TAU_STEP = 5.0f;
  static const float MINUS_TAU_STEP = -5.0f;
  pll_stage_result result = PLL_STAGE_RESULT_RUNNING;
  float ti;

  if (same_stage == 0 || (int8_t)pll.startup_state > PLL_STARTUP_LENGTHENING) {
    pll.frequency_adjusted = 0;
    pll_stages_startup_restart();
    pll_stages_loop_start();
    loop_steering.settled_blocks = 0;
    interval_next_reading(INTERVAL_ARM_CALIBRATE, NULL);
    pll.startup_state =
        float_compare(pll.loop_time_constant, initial_loop_time) == 0
            ? PLL_STARTUP_SETTLING
            : PLL_STARTUP_LENGTHENING;
    return pll_stages_check_causes(result);
  }
  uint8_t valid = interval_next_reading(INTERVAL_ARM_CALIBRATE, &ti);

  pll_stages_loop_reading(valid, ti,
                          "startup_pll - Error with measurement\r\n");
  loop_steering.seconds++;
  pll_stages_loop_gain();
  if (loop_steering.seconds < pll.block_length) {
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
        } else if (pll.aging_state != PLL_AGING_COLLECTING) {
          pll.tfom = SETTLED_TFOM;
          pll.efc_history.loop_settled = 1;
          pll.startup_state = PLL_STARTUP_WAITING_FOR_HISTORY;
        } else {
          pll.startup_state = PLL_STARTUP_LENGTHENING;
        }
        break;
      case PLL_STARTUP_WAITING_FOR_HISTORY:
        if (pll.efc_history.stop_ptr == HISTORY_ENTRIES &&
            pll.efc_history.start_ptr == 0) {
          pll.startup_state = PLL_STARTUP_LENGTHENING;
        }
        break;
      case PLL_STARTUP_LENGTHENING:
        if (float_compare(float_add(max_loop_time, MINUS_TAU_STEP),
                          pll.loop_time_constant) > 0) {
          if (float_compare(float_absolute_on_stack(loop_steering.ti_sum),
                            PLL_LOST_TI) < 0) {
            float_add_in_place(&pll.loop_time_constant, TAU_STEP);
            pll_stages_rescale_integral(
                float_add(pll.loop_time_constant, MINUS_TAU_STEP));
          } else {
            pll_stages_startup_restart();
            pll.startup_state = PLL_STARTUP_SETTLING;
            pll.loop_time_constant = initial_loop_time;
          }
        } else if (result != PLL_STAGE_RESULT_FAILED) {
          pll.loop_time_constant = max_loop_time;
          pll_stages_rescale_integral(
              float_add(pll.loop_time_constant, MINUS_TAU_STEP));
          result = PLL_STAGE_RESULT_DONE;
        }
        break;
      default:
        break;
    }
    pll_stages_steer(pll_stages_gains_for(pll.loop_time_constant));
  }
  pll_stages_block_end(pll.loop_time_constant);
  return pll_stages_check_causes(result);
}

/* The normal loop, stage 6 (0x49b1e): a proportional-integral loop on
 * blocks of pll.block_length readings at max_loop_time, the ramped
 * constant set to it as it starts, its gains kept in loop_steering, the
 * OCXO calibration's gain given the loop each block's first second.  A
 * block with no reading summed steers nothing.  Its state,
 * pll.normal_state, it reaches through a pointer in its frame. */
pll_stage_result pll_normal(bool same_stage) {
  static const float NORMAL_TFOM = 1e-06f;
  static const float FREQUENCY_TOLERANCE = 1e-07f;
  float ti;

  if (same_stage == 0 || (int8_t)pll.normal_state > PLL_NORMAL_RUNNING) {
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
    pll.efc_base = pll_stages_efc_less_temperature();
    pll.frequency_tolerance = FREQUENCY_TOLERANCE;
  } else {
    uint8_t valid = interval_next_reading(INTERVAL_ARM_CALIBRATE, &ti);

    pll_stages_loop_reading(valid, ti,
                            "pll_normal - Error with measurement\r\n");
    loop_steering.seconds++;
    pll_stages_loop_gain();
    if (loop_steering.seconds >= pll.block_length) {
      if (pll_stages_loop_mean()) {
        pll_stages_gains gains = {
            .proportional = loop_steering.proportional_gain,
            .integral = loop_steering.integral_gain,
            .filter_weight = loop_steering.filter_weight,
        };

        pll_stages_steer(gains);
      }
      pll_stages_block_end(max_loop_time);
    }
  }
  return pll_stages_check_causes(PLL_STAGE_RESULT_RUNNING);
}

/* The hold causes that keep power-up waiting for GPS: the temporary
 * mode, the 1PPS selection and the EFC control mode (0x4b4ec). */
static const pll_hold_cause PLL_STAGES_POWERUP_HOLDS =
    HOLD_CAUSE_EFC_CONTROL | HOLD_CAUSE_1PPS_SELECTION |
    HOLD_CAUSE_TEMPORARY_MODE;

/* The hold causes power-up clears as it starts afresh (0x4b43c). */
static const pll_hold_cause PLL_STAGES_POWERUP_CLEARED =
    HOLD_CAUSE_GPS | HOLD_CAUSE_TI_ERROR | HOLD_CAUSE_TI_LIMIT_EXCEEDED |
    HOLD_CAUSE_HARDWARE_FAILURE | HOLD_CAUSE_GPS_COMMAND_MONITOR;

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

/* The power-up stage, stage 1 (0x4b3d6): clears the EFC history, the
 * aging term, the loop's EFC step and the hold causes power-up lifts,
 * waits for the oven (oven_warmed_up) and for GPS, sets the frequency
 * finely and the clock from GPS, then waits out a near leap second and
 * the leap second's acknowledgment; back to waiting for GPS while it is
 * lost, or while the temporary, 1PPS selection or EFC control mode holds
 * it; done, it ends the TI limit's override.  As the Z3815A's it goes
 * from GPS to the fine setting, leaving coarse to a state nothing sets,
 * and raises powerup_flag; it logs the GPS reference valid once the
 * reference is good, whichever record made it so (0x4b68c). */
pll_stage_result powerup(bool same_stage) {
  static const float COARSE_TFOM = 0.001f;
  static const float FINE_TFOM = 2e-06f;
  pll_stage_result result = PLL_STAGE_RESULT_RUNNING;

  if (same_stage == 0 || (int8_t)pll.powerup_state > PLL_POWERUP_LEAP_SECONDS) {
    /* The history and the two floats after it, the aging term and the
     * loop's EFC step (0x4b418). */
    memset(&pll.efc_history, 0,
           offsetof(pll_state, ti_filtered) - offsetof(pll_state, efc_history));
    pll.aging_state = PLL_AGING_START;
    pll.efc_history.loop_settled = 0;
    first_satellite_logged = 0;
    gps_reference_logged = 0;
    pll.hold_causes &= ~PLL_STAGES_POWERUP_CLEARED;
    pll.powerup_state = PLL_POWERUP_START;
  }
  switch (pll.powerup_state) {
    case PLL_POWERUP_INNER_OVEN:
      interval_next_reading(INTERVAL_ARM_PLAIN, NULL);
      if (oven_warmed_up()) {
        pll.powerup_state = PLL_POWERUP_OUTER_OVEN;
        powerup_flag = 1;
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
          if (leap_second_near() != 0) {
            pll.powerup_countdown = PLL_STAGES_CLOCK_CHECK_DELAY;
            pll.powerup_state = PLL_POWERUP_WAITING;
          } else {
            pll.powerup_state = PLL_POWERUP_LEAP_SECONDS;
            pll.leap_ack_requested = 1;
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
  if (gps_reference_logged == 0 && gps_reference_good != 0) {
    log_first_satellite(oncore_date_pack(gps.status.date), gps.status.time);
    gps_reference_logged = 1;
  }
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
  if (result == PLL_STAGE_RESULT_DONE) {
    pll.ti_limit_override = 0;
  }
  return result;
}
