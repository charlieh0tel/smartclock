/* pll_stages.c's functions that are this image's own: the rest are
 * common/pll_stages.c's, the same in every image (docs/firmware/rewrite.md,
 * "Shared C").  This image's n3 lock pll stage and its slew, the OCXO
 * calibration's measurement, which steps the EFC over its range and
 * measures the frequency at each point, and the gain it gives the loop;
 * and the 58503A 3704's stages as this image changes them: coarse and
 * fine, the normal loop, holdover and its recovery, and the GPS
 * reference's view (docs/firmware/loop.md; docs/firmware/z3815a-4010.md).
 * Written by hand in place of the decompiler's (docs/firmware/README.md,
 * "Hand-written C"); each keeps its generated prototype.  The floating
 * arithmetic is calls to the image's soft-float routines
 * (docs/firmware/softfloat.md): float_subtract_reversed(a, b) is b - a,
 * float_divide_reversed(a, b) and double_divide_reversed(a, b) b / a. */

/* The EFC's full range, 2^20 DAC counts, as a float; the calibration's
 * nine steps of a ninth of it and its tenth points; and the ADC's two
 * points, a tenth and nine tenths of the range, eight tenths apart. */
static const int32_t PLL_STAGES_CALIBRATION_STEP = 116508;

static const float PLL_STAGES_CALIBRATION_STEP_FLOAT = 116508.0f;

static const float PLL_STAGES_CALIBRATION_HALF_STEP = 58254.0f;

static const uint8_t PLL_STAGES_CALIBRATION_STEPS = 9;

static const uint8_t PLL_STAGES_CALIBRATION_POINTS = 10;

static const float PLL_STAGES_CALIBRATION_STEPS_FLOAT = 9.0f;

static const float PLL_STAGES_EFC_LOW = 104857.5f;

static const float PLL_STAGES_EFC_HIGH = 943717.5f;

static const float PLL_STAGES_ADC_SPAN = -838860.0f;

/* Readings the calibration lets settle after the GPS reference is valid
 * and after each point, and readings at each ADC point. */
static const uint8_t PLL_STAGES_CALIBRATION_SETTLE = 10;

/* The calibration's bounds: a, b and :DADC out of them take these
 * values. */
static const float PLL_STAGES_EFC_A_LEAST = 1e-15f;

static const float PLL_STAGES_EFC_A_MOST = 1e-11f;

static const float PLL_STAGES_EFC_A_DEFAULT = 5e-13f;

static const float PLL_STAGES_EFC_B_LEAST = 1e-30f;

static const float PLL_STAGES_EFC_B_MOST = 1e-22f;

static const float PLL_STAGES_EFC_B_DEFAULT = 3.3e-24f;

static const float PLL_STAGES_DAC_PER_ADC_LEAST = 15.0f;

static const float PLL_STAGES_DAC_PER_ADC_MOST = 17.0f;

static const float PLL_STAGES_DAC_PER_ADC_DEFAULT = 16.0f;

/* The EFC's gain at an EFC, a + b * efc^2 by the OCXO calibration, given
 * the loop (0x52872). */
void efc_gain_from_calibration(float efc) {
  efc_gain_set(float_add(
      float_multiply(
          float_multiply(scpi_value_DIAGnostic_ROSCillator_EFControl_BSLOPe,
                         efc),
          efc),
      scpi_value_DIAGnostic_ROSCillator_EFControl_ASLOPe));
}

/* Fits the OCXO calibration's a and b, the frequency's change per DAC
 * count at each step's middle against the middle's square, by least
 * squares over the nine steps, each step's printed (0x528a4). */
void efc_calibration_fit(void) {
  float sum_gain = 0.0f;
  float sum_square = 0.0f;
  float sum_product = 0.0f;
  float sum_square_square = 0.0f;
  char line[80];

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
    debug_sprintf(line, "delta_f= %e volt2= %e\r\n", float_to_double(gain),
                  float_to_double(square));
    task_dmessage(line);
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

/* A calibration value kept within its bounds, else its default, logged;
 * then logged. */
static void pll_stages_calibration_bound(float* value, float least, float most,
                                         float fallback, char* out_of_range,
                                         char* format) {
  if (float_compare(*value, least) < 0 || float_compare(*value, most) > 0) {
    *value = fallback;
    log_write(out_of_range);
  }
  log_write(format, float_to_double(*value));
}

/* The OCXO calibration's measurement, one step a call while its stage
 * runs; 1 when it is done or a point's reading fails, else 0 (0x52a34).
 * From the start, the EFC at 0 and its compensation off, it waits for a
 * valid GPS reference and ocxo_warm, lets ten readings settle, then
 * at each tenth of the EFC's range takes two readings, the frequency
 * offset from the difference of the second's and the first's over 2;
 * fits a and b; reads the ADC at a tenth and nine tenths of the range
 * for :DADC; and bounds and logs the three, saves them and gives the loop
 * the gain.  Its done hook, efc_calibration_done, is called with no
 * arguments, so that it reads the caller's caller's D2 and D3 as its
 * index and source (docs/firmware/quirks.md). */
uint8_t efc_calibration_measure(uint8_t running) {
  int32_t index = CPU_CALLERS_REGISTER(int32_t, "d2");
  int32_t source = CPU_CALLERS_REGISTER(int32_t, "d3");
  float reading = 0.0f;
  char line[80];

  if (running == 0 || efc_calibration.step > EFC_CALIBRATION_CHECK) {
    efc_calibration.step = EFC_CALIBRATION_START;
  }
  switch (efc_calibration.step) {
    case EFC_CALIBRATION_START:
      scpi_value_DIAGnostic_ROSCillator_EFControl_DADC =
          PLL_STAGES_DAC_PER_ADC_DEFAULT;
      efc_compensation = 0;
      diag_efc_mode = EFC_MODE_USER;
      efc_calibration_cleared = 0;
      efc_calibration.point = 0;
      efc_set(0.0f);
      interval_next_reading(INTERVAL_ARM_SYNC, NULL);
      efc_calibration.step = EFC_CALIBRATION_WAIT_REFERENCE;
      break;
    case EFC_CALIBRATION_WAIT_REFERENCE:
      interval_next_reading(INTERVAL_ARM_PLAIN, NULL);
      if (gps.references[0].valid != GPS_REFERENCE_INVALID &&
          ocxo_warm() != 0) {
        efc_calibration.settle = 0;
        efc_calibration.step = EFC_CALIBRATION_SETTLE;
      }
      break;
    case EFC_CALIBRATION_SETTLE:
      interval_next_reading(INTERVAL_ARM_PLAIN, NULL);
      if (efc_calibration.settle++ > PLL_STAGES_CALIBRATION_SETTLE) {
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
      if (++efc_calibration.point > PLL_STAGES_CALIBRATION_SETTLE) {
        efc_calibration.point = 0;
        efc_calibration.step = EFC_CALIBRATION_ADC_HIGH;
        efc_calibration.adc_low = efc_adc_reading;
        efc_set(PLL_STAGES_EFC_HIGH);
      }
      break;
    case EFC_CALIBRATION_ADC_HIGH:
      interval_next_reading(INTERVAL_ARM_CALIBRATE, &reading);
      if (++efc_calibration.point > PLL_STAGES_CALIBRATION_SETTLE) {
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
        efc_calibration_done(index, source);
      }
      break;
    case EFC_CALIBRATION_CHECK:
      efc_compensation = PUCK_COMPENSATION_ON;
      pll_stages_calibration_bound(
          &scpi_value_DIAGnostic_ROSCillator_EFControl_ASLOPe,
          PLL_STAGES_EFC_A_LEAST, PLL_STAGES_EFC_A_MOST,
          PLL_STAGES_EFC_A_DEFAULT, "OCXO cal a out of range",
          "OCXO cal, a= %e");
      pll_stages_calibration_bound(
          &scpi_value_DIAGnostic_ROSCillator_EFControl_BSLOPe,
          PLL_STAGES_EFC_B_LEAST, PLL_STAGES_EFC_B_MOST,
          PLL_STAGES_EFC_B_DEFAULT, "OCXO cal b out of range",
          "OCXO cal, b= %e");
      pll_stages_calibration_bound(
          &scpi_value_DIAGnostic_ROSCillator_EFControl_DADC,
          PLL_STAGES_DAC_PER_ADC_LEAST, PLL_STAGES_DAC_PER_ADC_MOST,
          PLL_STAGES_DAC_PER_ADC_DEFAULT, "OCXO cal dac to adc out of range",
          "OCXO cal, dac/adc= %e");
      efc_calibration_done(index, source);
      efc_gain_from_calibration(pll.efc);
      return 1;
  }
  return 0;
}

/* Whether n3 lock pll has logged a card seating error (0x51724). */
uint8_t n3_lock_seating_error(void) { return n3_lock.seating_error; }

/* Where n3 lock pll and its slew pass their offset, an rts: it goes
 * nowhere (0x51730). */
void n3_lock_offset_nothing(int32_t offset) {}

/* Returns 1: moveq #1 and rts (0x5172c); the monitors and
 * gps_alternate_status take it, so that the external 1PPS is never dead
 * and the alternate receiver's status always set. */
uint32_t return_one(void) { return 1; }

/* A double with its sign flipped, as the image does with bchg #31
 * (0x53114). */
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

/* Debug-prints a label and a double, through a float, as the image does
 * each of its steps; in line, as the image's are: called apart, its
 * formats did not compare as the image's in the function check. */
__attribute__((always_inline)) static inline void pll_stages_cubic_print(
    const char* format, double value) {
  char message[80];

  debug_sprintf(message, (char*)format,
                float_to_double(double_to_float(value)));
  task_dmessage(message);
}

/* The EFC that changes the frequency by delta_f, from pll.efc, by the
 * EFC calibration's slopes A and B, the frequency f(e) = A e + B e^3 / 3
 * (0x52eb0).  With B zero, pll.efc + delta_f / A.  Else the real root of
 * e^3 + p e = q, p = 3A/B, q = 3(delta_f + A efc)/B + efc^3, by Cardano:
 * with h = q/2, k = A/B and D = h^2 + k^3, cbrt(h + sqrt D) +
 * cbrt(h - sqrt D), each cube root of a negative the negated root of its
 * magnitude; with D not positive, three real roots, pll.efc itself.  It
 * debug-prints delta_f, the first root and the sum, "e1" or "e2" by the
 * first's sign, "e3" or "e4" by the second's, then the sum again as
 * "e1". */
float efc_for_frequency_change(float delta_f) {
  const float a = scpi_value_DIAGnostic_ROSCillator_EFControl_ASLOPe;
  const float b = scpi_value_DIAGnostic_ROSCillator_EFControl_BSLOPe;
  double efc = float_to_double(pll.efc);
  char message[80];

  debug_sprintf(message, "delta_f = %e\r\n", float_to_double(delta_f));
  task_dmessage(message);
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

  if (double_compare(
          0.0, double_add(double_multiply_pointers(&h, &h),
                          double_multiply_pointer(
                              double_multiply_pointers(&k, &k), &k))) < 1) {
    return double_to_float(efc);
  }
  double s = sqrt(double_add(
      double_multiply_pointers(&h, &h),
      double_multiply_pointer(double_multiply_pointers(&k, &k), &k)));
  double root = 0.0;
  double sum = double_add_pointers(&h, &s);

  if (double_compare(0.0, sum) > 0) {
    root = pll_stages_cube_root(sum);
    pll_stages_cubic_print("e1= %e\r\n", root);
  } else {
    root = pll_stages_negate_double(
        pll_stages_cube_root(pll_stages_negate_double(sum)));
    pll_stages_cubic_print("e2= %e\r\n", root);
  }
  if (double_compare(0.0, double_subtract_reversed_pointers(&s, &h)) > 0) {
    double_add_in_place(
        &root, pll_stages_cube_root(double_subtract_reversed_pointers(&s, &h)));
    pll_stages_cubic_print("e3= %e\r\n", root);
  } else {
    double_subtract_in_place(
        &root, pll_stages_cube_root(double_subtract_reversed_pointers(&h, &s)));
    pll_stages_cubic_print("e4= %e\r\n", root);
  }
  pll_stages_cubic_print("e1= %e\r\n", root);
  return double_to_float(root);
}

/* The EFC's ends n3 lock pll sets it to, and the passes it holds one
 * while input_data_get's bit 4, the switch word's bit 11, is set before
 * it clamps instead. */
static const float PLL_STAGES_N3_EFC_LEAST = 1.0f;

static const float PLL_STAGES_N3_EFC_MOST = 1048559.0f;

static const float PLL_STAGES_N3_EFC_RANGE = 1048576.0f;

static const uint16_t PLL_STAGES_N3_EFC_LIMIT_PASSES = 20;

static const uint16_t PLL_STAGES_N3_EFC_LIMIT_BIT = 0x10;

/* Sets the EFC for n3 lock pll (0x51732): within its range clamped, at or
 * past an end the end, but while input_data_get's bit 4 is set, for 20
 * passes, after which it is clamped as within. */
void n3_lock_efc_set(float efc) {
  if (float_compare(0.0f, efc) < 0 &&
      float_compare(efc, PLL_STAGES_N3_EFC_RANGE) < 0) {
    efc_set(efc_clamp(efc));
    n3_lock.efc_limit_count = 0;
    return;
  }
  float end = float_compare(0.0f, efc) < 0 ? PLL_STAGES_N3_EFC_MOST
                                           : PLL_STAGES_N3_EFC_LEAST;

  if ((input_data_get() & PLL_STAGES_N3_EFC_LIMIT_BIT) == 0) {
    efc_set(end);
    n3_lock.efc_limit_count = 0;
  } else if (n3_lock.efc_limit_count < PLL_STAGES_N3_EFC_LIMIT_PASSES) {
    efc_set(end);
    n3_lock.efc_limit_count++;
  } else {
    efc_set(efc_clamp(efc));
  }
}

/* A TI reading in seconds as the debug messages print it, in ns. */
static const float PLL_STAGES_NANOSECONDS = 1e+09f;

/* n3 lock pll's bounds and gains: a reading below 1e-10 s is 0; a first
 * reading within 1 us goes to measure the frequency, else to sync; a
 * measurement within 10 us is restarted; the three readings' frequency
 * offsets agree within 1e-5 or the stage exits, and a mean within 2e-9
 * is corrected and measured again; a slew within 2 us syncs again; the
 * TI average is clamped above at 10 us; and 300 averages in a row
 * between 1 and 3 ns are a card seating error. */
static const float PLL_STAGES_N3_TI_FLOOR = 1e-10f;

static const float PLL_STAGES_N3_SYNC_BOUND = 1e-06f;

static const double PLL_STAGES_N3_MEASURE_BOUND = 1e-05;

static const float PLL_STAGES_N3_AGREE_BOUND = 1e-05f;

static const double PLL_STAGES_N3_FREQUENCY_BOUND = 2e-09;

static const double PLL_STAGES_N3_SLEW_BOUND = 2e-06;

static const float PLL_STAGES_N3_TFOM = 1e-06f;

static const float PLL_STAGES_N3_FREQUENCY_TOLERANCE = 1e-07f;

static const float PLL_STAGES_N3_AVERAGE_MOST = 1e-05f;

static const float PLL_STAGES_N3_SEATING_LEAST = 1e-09f;

static const float PLL_STAGES_N3_SEATING_MOST = 3e-09f;

static const uint16_t PLL_STAGES_N3_SEATING_COUNT = 300;

/* The passes the loop steers and the passes after them that measure the
 * phase offset, as the stage starts. */
static const int32_t PLL_STAGES_N3_PLL_PASSES = 60;

static const int32_t PLL_STAGES_N3_PHASE_PASSES = 1;

/* Its passes' count's step and the count past which pll.field_3e9 is
 * cleared. */
static const uint32_t PLL_STAGES_N3_PASS_STEP = 2;

static const uint32_t PLL_STAGES_N3_SETTLED_PASSES = 10;

/* The gains' divisors, of efc_gains' reciprocal TI and tau. */
static const float PLL_STAGES_N3_GAIN_DIVISOR = 2.5f;

static const float PLL_STAGES_N3_TAU_DIVISOR = 4.0f;

/* The phase offset's filter, its new reading's weight and the old's; and
 * n3_lock.phase's unit, ps. */
static const float PLL_STAGES_N3_PHASE_NEW = 0.2f;

static const float PLL_STAGES_N3_PHASE_OLD = 0.8f;

static const float PLL_STAGES_N3_PHASE_UNIT = -1e-12f;

/* The value counter_alarm_request asks FPGA_BIT_ALARM for as the loop steers,
 * measures the phase offset and stops; its meaning is not established. */
static const uint8_t PLL_STAGES_N3_COUNTER_STEER = 0xc;

static const uint8_t PLL_STAGES_N3_COUNTER_MEASURE = 8;

static const uint8_t PLL_STAGES_N3_COUNTER_STOP = 0;

/* The GPS time's low byte at which the leap second's acknowledgment is
 * asked for without a clock mismatch. */
static const uint8_t PLL_STAGES_N3_LEAP_SECOND = 14;

/* The loop's debug line's loop value. */
static const int32_t PLL_STAGES_N3_LOOP = 10;

/* n3 lock pll's result: running, and exited. */
static const uint8_t PLL_STAGES_N3_RUNNING = 0;

static const uint8_t PLL_STAGES_N3_EXITED = 2;

/* A reading's magnitude below PLL_STAGES_N3_TI_FLOOR taken for 0. */
static void pll_stages_n3_floor(float* ti) {
  if (float_compare(float_absolute_on_stack(*ti), PLL_STAGES_N3_TI_FLOOR) < 0) {
    *ti = 0.0f;
  }
}

/* Whether a float is zero, of either sign, as the image tests it, its
 * bits shifted left one (0x52258). */
static bool pll_stages_n3_zero(float value) {
  uint32_t bits;

  memcpy(&bits, &value, sizeof bits);
  return (bits << 1) == 0;
}

/* A failed reading: back to wait2. */
static void pll_stages_n3_failed(void) {
  pll.n3_lock_state = PLL_N3_LOCK_WAIT2;
  n3_lock.reading_valid = 0;
}

/* n3 lock pll's gains, from efc_gains'. */
static void pll_stages_n3_gains(void) {
  n3_lock.gain_ti =
      float_divide(efc_gains.reciprocal_ti, PLL_STAGES_N3_GAIN_DIVISOR);
  n3_lock.gain_tau = float_divide(
      float_divide(
          float_divide(efc_gains.reciprocal_tau, PLL_STAGES_N3_GAIN_DIVISOR),
          PLL_STAGES_N3_GAIN_DIVISOR),
      PLL_STAGES_N3_TAU_DIVISOR);
}

/* Prints a debug line while the loop's debugging is on. */
static void pll_stages_n3_debug(char* line) {
  if (pll_debug_enabled != 0) {
    task_dmessage(line);
  }
}

/* One of a measurement's three readings, into ti, then the next state; a
 * failed one back to wait2. */
static bool pll_stages_n3_measure(float* ti, char* format, char* line) {
  if (interval_next_reading(INTERVAL_ARM_CALIBRATE, ti) == 0) {
    pll_stages_n3_failed();
    return false;
  }
  pll_stages_n3_floor(ti);
  if (pll_debug_enabled != 0) {
    debug_sprintf(line, format, float_to_double(*ti));
    task_dmessage(line);
  }
  n3_lock.reading_valid = 1;
  return true;
}

/* The loop's pass of the pll state: a TI reading, less the phase offset,
 * the loop's TI; while it is within the limit threshold or the limit is
 * off, each two readings' average, clamped above at 10 us and checked
 * for a card seating error, steers the EFC by the gains, the loop's
 * integral and the EFC's base; else back to wait2.  Then the leap
 * second's acknowledgment is asked for (0x52324 to 0x52656). */
static void pll_stages_n3_steer(float* ti, float current, char* line) {
  if (interval_next_reading(INTERVAL_ARM_PLAIN, ti) == 0 ||
      switch_word_bit_4_copy == 0) {
    counter_alarm_request(PLL_STAGES_N3_COUNTER_MEASURE);
    pll_stages_n3_failed();
    return;
  }
  n3_lock.reading_valid = 1;
  pll_stages_n3_floor(ti);
  if (pll.aging_state != PLL_AGING_COLLECTING) {
    pll.tfom = PLL_STAGES_N3_TFOM;
    pll.efc_history.loop_settled = 1;
  }
  interval_mean = float_subtract(*ti, n3_lock.phase_offset);
  if (float_compare(float_absolute_on_stack(*ti), nv_settings.limit_threshold) <
          0 ||
      nv_settings.holdover_limit_state != HOLDOVER_LIMIT_ON) {
    n3_lock.reading_valid = 1;
    interval_mean_flag = n3_lock.reading_valid;
    float_add_in_place(&n3_lock.ti_average, interval_mean);
    if (n3_lock.averaging < 1) {
      n3_lock.averaging++;
    } else {
      float_divide_in_place(&n3_lock.ti_average, 2.0f);
      if (float_compare(n3_lock.ti_average, PLL_STAGES_N3_SEATING_LEAST) > 0 &&
          float_compare(PLL_STAGES_N3_SEATING_MOST, n3_lock.ti_average) > 0) {
        n3_lock.seating_count++;
      } else {
        n3_lock.seating_count = 0;
      }
      if (n3_lock.seating_count < PLL_STAGES_N3_SEATING_COUNT) {
        n3_lock.seating_error = 0;
      } else {
        n3_lock.seating_count = PLL_STAGES_N3_SEATING_COUNT;
        if (n3_lock.seating_error == 0) {
          n3_lock.seating_error = 1;
          log_write("There is a card seating error");
        }
      }
      /* The lower clamp compares -1e-5 with the average's negation, so
       * that it holds only where the upper did: a negative average is
       * never clamped (0x52470; docs/firmware/quirks.md). */
      if (float_compare(n3_lock.ti_average, PLL_STAGES_N3_AVERAGE_MOST) > 0) {
        n3_lock.ti_average = PLL_STAGES_N3_AVERAGE_MOST;
      } else if (float_compare(pll_stages_negate(PLL_STAGES_N3_AVERAGE_MOST),
                               pll_stages_negate(n3_lock.ti_average)) > 0) {
        n3_lock.ti_average = pll_stages_negate(PLL_STAGES_N3_AVERAGE_MOST);
      }
      efc_gain_from_calibration(pll.efc);
      pll_stages_n3_gains();
      float_add_in_place(
          &n3_lock.frequency_sum,
          float_multiply(float_multiply(n3_lock.ti_average, n3_lock.gain_tau),
                         2.0f));
      n3_lock_efc_set(float_add(
          float_add(float_multiply(n3_lock.gain_ti, n3_lock.ti_average),
                    pll.efc_base),
          n3_lock.frequency_sum));
      if (pll_debug_enabled != 0) {
        uint32_t date;
        time_of_day time;

        get_date_time(&date, &time);
        debug_sprintf(
            line,
            "%d/%d/%d %02d:%02d:%02d efc= %d ti= %.1f loop= %d curr= %.2f\r\n",
            (uint32_t)(uint8_t)(date >> 8), (uint32_t)(uint8_t)date,
            (uint32_t)(uint16_t)(date >> 16), (uint32_t)time.hour,
            (uint32_t)time.minute, (uint32_t)time.second,
            float_to_integer(pll.efc),
            float_to_double(
                float_multiply(n3_lock.ti_average, PLL_STAGES_NANOSECONDS)),
            PLL_STAGES_N3_LOOP, float_to_double(current));
        task_dmessage(line);
      }
      n3_lock.averaging = 0;
      n3_lock.ti_average = 0.0f;
    }
  } else {
    n3_lock.reading_valid = 0;
    interval_mean_flag = 0;
    *ti = 0.0f;
    pll.n3_lock_state = PLL_N3_LOCK_WAIT2;
  }
  if (gps_reference_good != 0 && leap_ack_pending() == 0 &&
      (clock_gps_mismatch != 0 ||
       (uint8_t)get_gps_time() == PLL_STAGES_N3_LEAP_SECOND) &&
      ((clock_check_against_gps() == 0 && clock_gps_mismatch == 0) ||
       gps_month_changed() != 0)) {
    pll.leap_ack_requested = 1;
  }
  n3_lock.field_17 = 0;
}

/* The pll state's passes after the loop's, which measure the phase
 * offset, filtered, from the TI less n3_lock.phase in ps; the last
 * starts the loop's passes again (0x52234 to 0x522ec). */
static void pll_stages_n3_phase(float* ti, char* line) {
  if ((int32_t)n3_lock.pass >= n3_lock.phase_passes + n3_lock.pll_passes) {
    counter_alarm_request(PLL_STAGES_N3_COUNTER_MEASURE);
    n3_lock.pass = 0;
  }
  if (interval_next_reading(INTERVAL_ARM_CALIBRATE, ti) == 0) {
    counter_alarm_request(PLL_STAGES_N3_COUNTER_MEASURE);
    pll_stages_n3_failed();
  } else {
    n3_lock.reading_valid = 1;
    pll_stages_n3_floor(ti);
    float offset = float_add(float_multiply(integer_to_float(n3_lock.phase),
                                            PLL_STAGES_N3_PHASE_UNIT),
                             *ti);

    if (pll_stages_n3_zero(n3_lock.phase_offset)) {
      n3_lock.phase_offset = offset;
    } else {
      n3_lock.phase_offset = float_add(
          float_multiply(n3_lock.phase_offset, PLL_STAGES_N3_PHASE_OLD),
          float_multiply(offset, PLL_STAGES_N3_PHASE_NEW));
    }
  }
  if (pll_debug_enabled != 0) {
    debug_sprintf(line, "s3_phase_offset= %e\r\n",
                  float_to_double(n3_lock.phase_offset));
    task_dmessage(line);
  }
}

/* The n3 lock pll stage, one state a pass, as pllp_task steps it; 2 once
 * it has exited, else 0 (0x519ce).  Started, or past its last state, it
 * sets itself up: the GPS's 1PPS reference held valid, the loop's passes,
 * the TFOM and tolerance, the gains, no offset.  wait1 waits for an odd
 * GPS second; wait2 takes a reading, measuring the frequency within 1 us,
 * else syncing; sync1 and sync2 sync the counter (sync2 the EFC's base
 * taken); leapsec and check time wait for the leap second's
 * acknowledgment and the clock's check against GPS; measure1 to measure3
 * take three TIs; calc freq takes the frequency offsets from them,
 * correcting a mean beyond 2e-9 and measuring again, else slewing or
 * syncing, or exits when they disagree; slew slews the phase until
 * fine_sync_adjust has done; state 11 takes one reading more; the pll
 * state steers the loop and measures the phase offset by turns; exit1 and
 * exit2 stop the counter and release the reference.  Whenever the switch
 * word's bit 4 copy is clear it exits.  The floating comparisons are the
 * image's, whose float_compare(a, b) is negative for a < b. */
uint8_t n3_lock_pll(uint8_t running) {
  uint8_t result = PLL_STAGES_N3_RUNNING;
  float current = 0.0f;
  float ti;
  float step;
  char line[101];

  if (running == 0 || (int8_t)pll.n3_lock_state > PLL_N3_LOCK_EXIT2) {
    n3_lock.seating_count = 0;
    gps.references[1].valid = MONITORS_REFERENCE_VALID;
    n3_lock.pll_passes = PLL_STAGES_N3_PLL_PASSES;
    n3_lock.phase_passes = PLL_STAGES_N3_PHASE_PASSES;
    pll.field_3e9 = 1;
    pll.frequency_adjusted = 0;
    pll.tfom = PLL_STAGES_N3_TFOM;
    n3_lock.averaging = 0;
    n3_lock.field_17 = 0;
    n3_lock.frequency_sum = 0.0f;
    pll.frequency_tolerance = PLL_STAGES_N3_FREQUENCY_TOLERANCE;
    pll.n3_lock_state = PLL_N3_LOCK_WAIT1;
    n3_lock.reading_valid = 1;
    n3_lock.phase_offset = 0.0f;
    pll_stages_n3_gains();
    n3_lock.slew_offset = 0.0f;
    n3_lock_offset_nothing(0);
    n3_lock.passes = 0;
    n3_lock.field_3c = 0;
    n3_lock.seating_error = 0;
  }
  n3_lock.passes += PLL_STAGES_N3_PASS_STEP;
  switch (pll.n3_lock_state) {
    default:
    case PLL_N3_LOCK_WAIT1:
      interval_wait_reading();
      {
        uint32_t time = get_clock_time();

        if ((gps_seconds(get_clock_date(), time) & 1) != 0) {
          counter_alarm_request(PLL_STAGES_N3_COUNTER_MEASURE);
          pll.n3_lock_state = PLL_N3_LOCK_WAIT2;
        }
      }
      interval_rearm(INTERVAL_ARM_CALIBRATE);
      pll_stages_n3_debug("wait1\r\n");
      break;
    case PLL_N3_LOCK_WAIT2:
      if (interval_next_reading(INTERVAL_ARM_CALIBRATE, &ti) == 0) {
        n3_lock.reading_valid = 0;
      } else {
        pll_stages_n3_floor(&ti);
        if (float_compare(float_absolute_on_stack(ti),
                          PLL_STAGES_N3_SYNC_BOUND) <= 0) {
          pll.n3_lock_state = PLL_N3_LOCK_MEASURE1;
        } else {
          pll.n3_lock_state = PLL_N3_LOCK_SYNC1;
        }
        n3_lock.reading_valid = 1;
      }
      pll_stages_n3_debug("wait2\r\n");
      break;
    case PLL_N3_LOCK_SYNC1:
    case PLL_N3_LOCK_SYNC2:
      interval_wait_reading();
      sync_request(COUNTER_SYNC_FULL, 1);
      interval_rearm(INTERVAL_ARM_CALIBRATE);
      pll_stages_n3_debug("syncing...\r\n");
      if (pll.n3_lock_state == PLL_N3_LOCK_SYNC1) {
        pll.n3_lock_state = PLL_N3_LOCK_LEAPSEC;
      } else {
        pll.efc_base = pll_efc_less_temperature(current);
        pll.n3_lock_state = PLL_N3_LOCK_SLEW;
      }
      break;
    case PLL_N3_LOCK_LEAPSEC:
      interval_next_reading(INTERVAL_ARM_PLAIN, NULL);
      if (leap_ack_pending() == 0 &&
          (clock_check_against_gps() != 0 || clock_gps_mismatch == 0)) {
        pll.n3_lock_state = PLL_N3_LOCK_CHECK_TIME;
        pll.leap_ack_requested = 1;
      }
      pll_stages_n3_debug("check time...\r\n");
      break;
    case PLL_N3_LOCK_CHECK_TIME:
      interval_next_reading(INTERVAL_ARM_PLAIN, NULL);
      if (pll.leap_ack_requested == 0 && pll.leap_ack_done != 0) {
        pll.n3_lock_state = PLL_N3_LOCK_MEASURE1;
      }
      break;
    case PLL_N3_LOCK_MEASURE1:
      if (interval_next_reading(INTERVAL_ARM_CALIBRATE, &pll.ti0) == 0) {
        pll_stages_n3_failed();
        break;
      }
      pll_stages_n3_floor(&pll.ti0);
      if (pll_debug_enabled != 0) {
        debug_sprintf(line, "efc= %f\r\n", float_to_double(pll.efc));
        task_dmessage(line);
        debug_sprintf(line, "ti0= %e\r\n", float_to_double(pll.ti0));
        task_dmessage(line);
      }
      /* ti is the frame's local, which this pass has not set: what the
       * last call left there (docs/firmware/quirks.md). */
      if (float_compare(*(volatile float*)&ti, 1.0f) <= 0) {
        pll.n3_lock_state = PLL_N3_LOCK_MEASURE2;
      } else {
        pll.n3_lock_state = PLL_N3_LOCK_MEASURE1;
      }
      if (double_compare(float_to_double(float_absolute_on_stack(pll.ti0)),
                         PLL_STAGES_N3_MEASURE_BOUND) < 0) {
        pll.n3_lock_state = PLL_N3_LOCK_WAIT2;
      }
      n3_lock.reading_valid = 1;
      break;
    case PLL_N3_LOCK_MEASURE2:
      if (pll_stages_n3_measure(&pll.ti1, "ti1= %e\r\n", line)) {
        pll.n3_lock_state = PLL_N3_LOCK_MEASURE3;
      }
      break;
    case PLL_N3_LOCK_MEASURE3:
      if (pll_stages_n3_measure(&pll.ti2, "ti2= %e\r\n", line)) {
        pll.n3_lock_state = PLL_N3_LOCK_CALC_FREQ;
      }
      break;
    case PLL_N3_LOCK_CALC_FREQ: {
      n3_lock.frequency_offset_1 =
          float_divide(float_subtract(pll.ti0, pll.ti1), 2.0f);
      n3_lock.frequency_offset_2 =
          float_divide(float_subtract(pll.ti1, pll.ti2), 2.0f);
      float mean = float_divide(
          float_add(n3_lock.frequency_offset_1, n3_lock.frequency_offset_2),
          2.0f);

      if (float_compare(
              float_absolute_on_stack(float_subtract(
                  n3_lock.frequency_offset_1, n3_lock.frequency_offset_2)),
              PLL_STAGES_N3_AGREE_BOUND) > 0) {
        pll.n3_lock_state = PLL_N3_LOCK_EXIT1;
        pll_stages_n3_debug("exit because of bad fequency measurement\r\n");
      } else if (double_compare(float_to_double(float_absolute_on_stack(mean)),
                                PLL_STAGES_N3_FREQUENCY_BOUND) < 0) {
        step = efc_for_frequency_change(pll_stages_negate(mean));
        n3_lock_efc_set(step);
        pll.n3_lock_state = PLL_N3_LOCK_MEASURE1;
      } else {
        pll.efc_base = pll_efc_less_temperature(current);
        if (float_compare(float_absolute_on_stack(pll.ti2),
                          PLL_STAGES_N3_SYNC_BOUND) < 0) {
          pll.n3_lock_state = PLL_N3_LOCK_SLEW;
        } else {
          pll.n3_lock_state = PLL_N3_LOCK_SYNC1;
        }
      }
      if (pll_debug_enabled != 0) {
        debug_sprintf(line, "freq_off= %e %e %e\r\n", float_to_double(mean),
                      float_to_double(n3_lock.frequency_offset_1),
                      float_to_double(n3_lock.frequency_offset_2));
        task_dmessage(line);
      }
      break;
    }
    case PLL_N3_LOCK_SLEW:
      if (interval_next_reading(INTERVAL_ARM_CALIBRATE, &ti) == 0) {
        pll_stages_n3_failed();
        break;
      }
      pll_stages_n3_floor(&ti);
      if (fine_sync_adjust(ti) == 0) {
        pll.efc_base = pll_efc_less_temperature(current);
        n3_lock.averaging = 0;
        pll.n3_lock_state = PLL_N3_LOCK_STATE_11;
      } else if (double_compare(float_to_double(ti), PLL_STAGES_N3_SLEW_BOUND) <
                 0) {
        pll.n3_lock_state = PLL_N3_LOCK_SYNC1;
      }
      n3_lock.reading_valid = 1;
      if (pll_debug_enabled != 0) {
        debug_sprintf(line, "slew ti= %e\r\n", float_to_double(ti));
        task_dmessage(line);
      }
      break;
    case PLL_N3_LOCK_STATE_11:
      counter_alarm_request(PLL_STAGES_N3_COUNTER_STEER);
      if (interval_next_reading(INTERVAL_ARM_CALIBRATE, &ti) == 0) {
        counter_alarm_request(PLL_STAGES_N3_COUNTER_MEASURE);
        pll_stages_n3_failed();
      } else {
        /* Its result is not taken. */
        float_compare(float_absolute_on_stack(ti), PLL_STAGES_N3_TI_FLOOR);
        pll.n3_lock_state = PLL_N3_LOCK_PLL;
        n3_lock.pass = (uint16_t)(n3_lock.pll_passes + 1);
        n3_lock.reading_valid = 1;
      }
      break;
    case PLL_N3_LOCK_PLL:
      if (n3_lock.passes > PLL_STAGES_N3_SETTLED_PASSES) {
        pll.field_3e9 = 0;
      }
      if ((int32_t)n3_lock.pass == n3_lock.pll_passes) {
        counter_alarm_request(PLL_STAGES_N3_COUNTER_STEER);
      }
      if ((int32_t)n3_lock.pass < n3_lock.pll_passes + 1) {
        pll_stages_n3_steer(&ti, current, line);
      } else {
        pll_stages_n3_phase(&ti, line);
      }
      n3_lock.pass++;
      break;
    case PLL_N3_LOCK_EXIT1:
      interval_mean_flag = 1;
      n3_lock.slew_offset = 0.0f;
      n3_lock_offset_nothing(0);
      interval_wait_reading();
      counter_alarm_request(PLL_STAGES_N3_COUNTER_STOP);
      interval_rearm(INTERVAL_ARM_CALIBRATE);
      pll.n3_lock_state = PLL_N3_LOCK_EXIT2;
      pll_stages_n3_debug("exit1\r\n");
      break;
    case PLL_N3_LOCK_EXIT2:
      interval_next_reading(INTERVAL_ARM_CALIBRATE, &interval_mean);
      pll_stages_n3_floor(&interval_mean);
      result = PLL_STAGES_N3_EXITED;
      gps.references[1].valid = GPS_REFERENCE_INVALID;
      pll_stages_n3_debug("exit2\r\n");
      break;
  }
  if (switch_word_bit_4_copy == 0 && pll.n3_lock_state != PLL_N3_LOCK_EXIT2) {
    pll.n3_lock_state = PLL_N3_LOCK_EXIT1;
  }
  return result;
}

/* n3_lock_slew's bounds: a TI within 100 ns, the unit its stopping TI's
 * offset term takes, 1 / 3145728000, and the half of v^2 / 2a. */
static const float PLL_STAGES_SLEW_TI = 1e-07f;

static const double PLL_STAGES_SLEW_TI_DOUBLE = 1e-07;

static const double PLL_STAGES_SLEW_UNIT = 3.178914388020834e-10;

static const float PLL_STAGES_SLEW_HALF = 0.5f;

/* A slew of n3_lock.slew_offset ("curr offset") toward a TI ("slew ti"),
 * a step a call, which nothing calls: n3_lock_pll's slew state slews by
 * fine_sync_adjust, and clears the offset as it starts and exits
 * (0x517e0).  1, the offset given n3_lock_offset_nothing as 0, once the
 * TI is within 100 ns and the offset within a step; else 0, the offset
 * moved a step toward 0 while the TI is within 100 ns or within the TI at
 * which the offset, so slowed, would stop ("zero_ti": 100 ns plus
 * offset^2 times scale over twice the step, in PLL_STAGES_SLEW_UNIT),
 * else a step the TI's way, bounded by offset_most; and given
 * n3_lock_offset_nothing, which does nothing with it.  What the offset,
 * offset_most and scale measure is not established.  Slewed in, it
 * prints as zero_ti what the caller left in D4, never set
 * (docs/firmware/quirks.md). */
uint8_t n3_lock_slew(float ti, uint16_t offset_most, float step,
                     uint8_t scale) {
  uint32_t callers_d4 = CPU_CALLERS_REGISTER(uint32_t, "d4");
  float zero_ti;
  uint8_t slewed = 0;

  memcpy(&zero_ti, &callers_d4, sizeof zero_ti);
  if (float_compare(float_absolute_on_stack(ti), PLL_STAGES_SLEW_TI) < 0 &&
      float_compare(float_absolute_on_stack(n3_lock.slew_offset), step) < 0) {
    n3_lock_offset_nothing(0);
    slewed = 1;
  } else {
    zero_ti = double_to_float(double_add(
        PLL_STAGES_SLEW_TI_DOUBLE,
        double_divide_reversed(
            float_to_double(step),
            double_multiply(
                float_to_double(float_multiply(
                    float_multiply(float_multiply(n3_lock.slew_offset,
                                                  n3_lock.slew_offset),
                                   unsigned_to_float(scale)),
                    PLL_STAGES_SLEW_HALF)),
                PLL_STAGES_SLEW_UNIT))));
    if (float_compare(float_absolute_on_stack(ti), zero_ti) < 0 ||
        float_compare(float_absolute_on_stack(ti), PLL_STAGES_SLEW_TI) < 0) {
      if (float_compare(n3_lock.slew_offset, 0.0f) > 0) {
        float_subtract_in_place(&n3_lock.slew_offset, step);
      } else {
        float_add_in_place(&n3_lock.slew_offset, step);
      }
    } else {
      if (float_compare(ti, 0.0f) > 0) {
        float_add_in_place(&n3_lock.slew_offset, step);
      } else {
        float_subtract_in_place(&n3_lock.slew_offset, step);
      }
      if (float_compare(float_absolute_on_stack(n3_lock.slew_offset),
                        unsigned_to_float(offset_most)) > 0) {
        n3_lock.slew_offset =
            float_compare(n3_lock.slew_offset, 0.0f) > 0
                ? unsigned_to_float(offset_most)
                : pll_stages_negate(unsigned_to_float(offset_most));
      }
    }
    n3_lock_offset_nothing(float_to_integer(n3_lock.slew_offset));
  }
  if (pll_debug_enabled != 0) {
    char line[100];

    debug_sprintf(line, "curr offset= %f slew ti= %e zero_ti= %e\r\n",
                  float_to_double(n3_lock.slew_offset), float_to_double(ti),
                  float_to_double(zero_ti));
    task_dmessage(line);
  }
  return slewed;
}

/* Readings to a block of the normal loop's and holdover's steering, and
 * the same as a float. */
static const uint8_t PLL_STAGES_BLOCK_SECONDS = 10;

static const float PLL_STAGES_BLOCK_LENGTH = 10.0f;

/* Readings before the clock is checked against GPS, as power-up and
 * holdover recovery count them, and between checks after a mismatch. */
static const uint8_t PLL_STAGES_CLOCK_CHECK_DELAY = 25;

static const uint8_t PLL_STAGES_CLOCK_RECHECK_DELAY = 2;

/* A measurement's failures, pll.fine_retries, past which coarse and fine
 * give up. */
static const uint16_t PLL_STAGES_MOST_RETRIES = 9;

/* fine's readings to its fit, at most (fine_readings). */
#define PLL_STAGES_FINE_READINGS 50

/* The hold causes on which a stage asks for the output inactive,
 * through output_inactive_request_bit_11. */
static const pll_hold_cause PLL_STAGES_REPORTED_HOLDS =
    HOLD_CAUSE_TI_LIMIT_EXCEEDED | HOLD_CAUSE_TI_ERROR |
    HOLD_CAUSE_HARDWARE_FAILURE;

/* Calls output_inactive_request_bit_11 while a TI or hardware hold cause is
 * set, as enter_holdover, holdover and holdover_recovery do as they leave; its
 * result is not taken. */
static void pll_stages_report_holds(void) {
  if ((pll.hold_causes & PLL_STAGES_REPORTED_HOLDS) != HOLD_CAUSE_NONE) {
    output_inactive_request_bit_11();
  }
}

/* Slews the phase toward a TI: done within 100 ns, else fine_sync_adjust's
 * one step of 100 ns and a slew again.  The 58503A's fine-slews for fewer
 * than 4 steps; here for fewer than 1, after none, so never. */
static pll_stage_result pll_stages_fine_slew(float ti) {
  static const uint8_t FINE_SLEW_STEPS = 1;

  pll.fine_index = fine_sync_adjust(ti);
  if (pll.fine_index == 0) {
    return PLL_STAGE_RESULT_DONE;
  }
  if (pll.fine_index < FINE_SLEW_STEPS) {
    pll.fine_state = PLL_FINE_FINE_SLEW;
  } else {
    pll.fine_retries = 0;
    pll.fine_state = PLL_FINE_SLEW;
  }
  return PLL_STAGE_RESULT_RUNNING;
}

/* A failed measurement of fine's, its message printed: failed past the
 * most retries. */
static pll_stage_result pll_stages_fine_retry(char* message) {
  task_dmessage(message);
  pll.fine_retries++;
  return pll.fine_retries > PLL_STAGES_MOST_RETRIES ? PLL_STAGE_RESULT_FAILED
                                                    : PLL_STAGE_RESULT_RUNNING;
}

/* fine's first reading, fine_readings[0]. */
static const uint8_t PLL_STAGES_FINE_FIRST = 1;

/* Starts fine's fit again from its first reading. */
static void pll_stages_fine_fit_restart(void) {
  pll.fine_index = PLL_STAGES_FINE_FIRST;
  fine_sum_ti = 0.0f;
  fine_sum_i_ti = 0.0f;
  fine_sum_i = 0.0f;
  fine_sum_i_squared = 0.0f;
}

/* Adds reading i, pll.fine_d, to fine's least-squares line through the
 * readings against their index, TI = x0 + y0 i, as the interval's mean
 * too: its sums, then the slope y0 (0 when the sums give none) and the
 * intercept x0; the largest TI magnitude kept in fine_ti_max. */
static void pll_stages_fine_fit_add(void) {
  uint8_t i = pll.fine_index;

  interval_mean = pll.fine_d;
  interval_mean_flag = 1;
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

/* After the fit's readings, their mean as interval_mean, and whether the
 * first lies within 300 ns of the line; measures again, a retry counted,
 * if not. */
static void pll_stages_fine_fit_check(void) {
  static const float BAD_TI = 3e-07f;

  interval_mean =
      float_divide_reversed(unsigned_to_float(pll.fine_index), fine_sum_ti);
  interval_mean_flag = 1;
  pll.fine_index = PLL_STAGES_FINE_FIRST;
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

/* After the fit, the TI's change across the readings, per second, into
 * pll.fine_d.  Out of power-up, failed when the largest TI passes
 * holdover's limit, its threshold and its frequency tolerance over its
 * duration, unless the limit is off, the holdover long or its limit
 * overridden.  Then a frequency within 2e-9 slews the phase; one within
 * the frequency tolerance, or any in power-up, a long holdover or with
 * the limit overridden, sets the EFC from it (efc_for_frequency_change) and
 * syncs if the TI asks; another fails. */
static pll_stage_result pll_stages_fine_frequency(void) {
  static const float PER_SECOND = 0.02f;
  static const float SLEW_FREQUENCY = 2e-09f;
  bool held = pll.holdover_duration <
                  (uint32_t)nv_settings.holdover_measurement_threshold &&
              pll.ti_limit_override == 0;

  float_subtract_in_place(&pll.fine_d, double_to_float(interval_reading.value));
  float_multiply_in_place(&pll.fine_d, PER_SECOND);
  if (pll.stage != PLL_STAGE_POWERUP) {
    float limit =
        float_add(float_multiply(unsigned_to_float(pll.holdover_duration),
                                 pll.frequency_tolerance),
                  nv_settings.holdover_limit_threshold);

    if (float_compare(limit, fine_ti_max) < 0 && held &&
        nv_settings.holdover_limit_state == HOLDOVER_LIMIT_ON) {
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
  efc_set(efc_clamp(efc_for_frequency_change(fine_y0)));
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

/* The coarse frequency setting: syncs the counter, then measures the TI
 * across a second and sets the EFC from the change (efc_for_frequency_change)
 * until it is within 100 ns (0x4e890). */
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
      efc_set(efc_clamp(efc_for_frequency_change(pll.fine_d)));
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

/* The fine frequency setting: syncs the outputs if the TI asks, after a
 * sync goes on within 10 us or syncs again, fits a line to up to 50
 * readings for the frequency, fewer once its slope passes 500 ns over
 * the readings so far, then sets the EFC from it and measures again, or
 * slews the phase once the frequency is within 2e-9; a sync asked for
 * starts it again but while it fine-slews (0x4ea54). */
pll_stage_result fine(void) {
  static const float SYNC_GO_ON = 1e-05f;
  static const float FIT_SPAN = 5e-07f;
  pll_stage_result result = PLL_STAGE_RESULT_RUNNING;
  float ti;

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
    case PLL_FINE_SYNC_2: {
      char message[80];
      bool go_on;

      interval_next_reading(INTERVAL_ARM_CALIBRATE, &ti);
      go_on = float_compare(float_absolute_on_stack(ti), SYNC_GO_ON) < 0;
      if (pll_debug_enabled != 0) {
        debug_sprintf(
            message,
            go_on ? "sync go on ti= %.1f\r\n" : "sync do again ti= %.1f\r\n",
            double_multiply((double)PLL_STAGES_NANOSECONDS,
                            float_to_double(ti)));
        task_dmessage(message);
      }
      pll.fine_state = go_on ? PLL_FINE_MEASURE_1 : PLL_FINE_SYNC_MEASURE;
      break;
    }
    case PLL_FINE_MEASURE_1:
      interval_next_reading(INTERVAL_ARM_PLAIN, NULL);
      pll.fine_state = PLL_FINE_MEASURE_DELAY;
      pll.fine_retries = 0;
      pll_stages_fine_fit_restart();
      fine_ti_max = 0.0f;
      fine_y0 = 0.0f;
      break;
    case PLL_FINE_MEASURE_DELAY:
      if (pll.fine_index <= PLL_STAGES_FINE_READINGS &&
          float_compare(float_divide_reversed(unsigned_to_float(pll.fine_index),
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
        interval_mean = double_to_float(interval_reading.value);
        interval_mean_flag = 1;
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

/* A normal loop's one-second reading, valid or not.  A failed one, while
 * the reference is questionable, marks the block measurement_failed, and
 * with both 1PPS good and the reading not replaced by a wait is a
 * measurement failure (field_0f), its message printed.  A good one with
 * GPS, not questionable, is summed into the block within the hold
 * threshold, else printed; questionable, it marks the block.  Then, with
 * GPS, not questionable, and no leap second acknowledgment pending, the
 * clock is checked against GPS at second 14 or after a mismatch, and a
 * check that has just set the clock, or gps_month_changed's, asks gps_leap_ack
 * to measure GPS-UTC again. */
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
      gps_reference_questionable == 0 &&
      (clock_gps_mismatch != 0 ||
       (get_gps_time() & SECOND_MASK) == CHECK_SECOND) &&
      (((char)clock_check_against_gps() == 0 && clock_gps_mismatch == 0) ||
       (char)gps_month_changed() != 0)) {
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
 * into pll.ti_filtered; filters the loop's EFC step toward the aging
 * fit's slope now, pll.aging_term, over the time constant, bounded by the
 * step limit, and integrates it with the filtered TI; and sets the EFC
 * from the filtered TI, the base, the integral and the temperature
 * correction.  The 58503A's takes the slope from the fit itself and does
 * not filter it. */
static void pll_stages_steer(pll_stages_gains gains, float current) {
  static const float ONE = 1.0f;
  static const float MINUS_BLOCK_LENGTH = -10.0f;
  float error = float_subtract(loop_steering.ti_sum, ti_setpoint);

  pll.ti_filtered = float_add(
      float_multiply(float_subtract(ONE, gains.filter_weight), pll.ti_filtered),
      float_multiply(gains.filter_weight, error));
  float kept = float_divide(
      float_multiply(float_add(pll.loop_time_constant, MINUS_BLOCK_LENGTH),
                     pll.loop_efc_step),
      pll.loop_time_constant);

  pll.loop_efc_step = float_add(
      kept,
      float_divide(float_multiply(pll.aging_term, PLL_STAGES_BLOCK_LENGTH),
                   pll.loop_time_constant));
  float slope = pll.loop_efc_step;

  if (float_compare(slope, efc_gains.step_limit) > 0) {
    slope = efc_gains.step_limit;
  } else if (float_compare(pll_stages_negate(efc_gains.step_limit), slope) >
             0) {
    slope = pll_stages_negate(efc_gains.step_limit);
  }
  float drift = float_add(float_divide(float_divide(slope, gains.integral),
                                       PLL_EFC_HISTORY_ENTRY_SECONDS),
                          pll.ti_filtered);

  float_add_in_place(&loop_steering.integral,
                     float_multiply(float_multiply(drift, gains.integral),
                                    PLL_STAGES_BLOCK_LENGTH));
  float efc =
      float_add(float_add(float_multiply(gains.proportional, pll.ti_filtered),
                          pll.efc_base),
                loop_steering.integral);

  efc_set(efc_clamp(float_add(
      efc, float_multiply(scpi_value_DIAGnostic_ROSCillator_TCOefficient,
                          current))));
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

/* The normal loop, stage 7, the 58503A's pll_normal, which the seeding
 * paired with startup_pll ("pll_normal - Error with measurement"): a
 * proportional-integral loop on 10-second blocks at max_loop_time, its
 * gains kept in loop_steering, the OCXO calibration's gain given the
 * loop each block's first second.  A block with no reading summed steers
 * nothing (0x4ddea).  Its state, pll.normal_state, it reaches through a
 * pointer in its frame. */
pll_stage_result pll_normal(bool same_stage) {
  static const float NORMAL_TFOM = 1e-06f;
  static const float FREQUENCY_TOLERANCE = 1e-07f;
  float ti;
  float current = adc_average(ADC_OVEN_CURRENT);

  if (same_stage == 0 || (int8_t)pll.normal_state > PLL_NORMAL_RUNNING) {
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
    pll_stages_loop_gain();
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
 * limit, the threshold plus the tolerance a second; else 1 (0x4cc30). */
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

/* Counts a second of holdover, and reports it past the status threshold,
 * once; the 58503A also sets its PWMA output high (0x4cde6). */
void holdover_duration_count(void) {
  pll.holdover_duration++;
  if (pll.holdover_duration > (uint32_t)nv_settings.holdover_status_threshold &&
      holdover_duration_exceeded == 0) {
    holdover_duration_exceeded = 1;
    status_happening(STATUS_HOLDOVER_THRESHOLD_SET);
  }
}

/* The log event for holdover on the GPS's loss: on the external
 * reference, by its 1PPS; else by the GPS reference's state (the jump
 * table at 0x4cd28), any other state the GPS's 1PPS, dead or lost. */
static log_event_code pll_stages_gps_holdover_event(void) {
  if (pll.external_reference != 0) {
    return gps_pps_dead != 0 ? LOG_HOLDOVER_INVALID_EXT_PPS : LOG_HOLDOVER_EXT;
  }
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

/* Enters holdover: its duration and its start's uptime restarted but
 * from recovery, manual when a manual hold cause or no automatic
 * recovery asks it, a new holdover logged by its cause, and the
 * interval's mean dropped for any cause but the manual one (0x4cc8c). */
void enter_holdover(void) {
  if (pll.stage != PLL_STAGE_HOLDOVER_RECOVERY) {
    pll.holdover_duration = 0;
    pll.holdover_start_uptime = get_uptime();
  }
  pll.manual = (pll.hold_causes & HOLD_CAUSE_MANUAL) != HOLD_CAUSE_NONE ||
               holdover_recovery_auto == 0;
  pll.stage = PLL_STAGE_HOLDOVER;
  if (pll.holdover_duration == 0) {
    log_event(pll_stages_holdover_event());
  }
  if ((pll.hold_causes & ~HOLD_CAUSE_MANUAL) != HOLD_CAUSE_NONE) {
    interval_mean_flag = 0;
  }
  pll_stages_report_holds();
}

/* The EFC the aging fit predicts at the uptime, with y in entries and
 * half an entry, a + b y, and c ln y past the first entry, and the
 * temperature correction.  The 58503A's y has no half entry. */
static float pll_stages_predicted_efc(float current) {
  static const double ENTRY_SECONDS_DOUBLE = 2700.0;
  static const double HALF_ENTRY = 0.5;
  static const float ONE = 1.0f;
  float age = double_to_float(double_add(
      HALF_ENTRY, double_divide_reversed(
                      ENTRY_SECONDS_DOUBLE,
                      float_to_double(unsigned_to_float(get_uptime())))));

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

/* The holdover stage: sets the EFC by the aging fit, and each 10 seconds
 * the TFOM from the HQ and the holdover's age, and the hold causes from
 * the TI's block: done once none is left and the holdover is not manual
 * (0x4ce18).  Unlike the 58503A's it keeps no start date and time, sets
 * the EFC through efc_set, and calls output_inactive_request_bit_11 for a TI or
 * hardware cause as it leaves. */
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
    pll.holdover_state = PLL_HOLDOVER_HOLDING;
    holdover_steering.ti_mean = 0.0f;
    holdover_steering.field_06 = 0;
    holdover_steering.seconds = 0;
    holdover_steering.readings = 0;
    pll_stages_report_holds();
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
    float_add_in_place(&holdover_steering.ti_mean,
                       float_divide(ti, PLL_STAGES_BLOCK_LENGTH));
    holdover_steering.readings++;
  }
  holdover_steering.seconds++;
  if (holdover_steering.seconds < PLL_STAGES_BLOCK_SECONDS) {
    pll_stages_report_holds();
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
  efc_set(efc_clamp(pll_stages_predicted_efc(current)));
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
  pll_stages_report_holds();
  return result;
}

/* The holdover recovery stage: once a reading is good, the fine
 * frequency setting, then the clock checked against GPS and the leap
 * second's acknowledgment; done when the clock agrees, unless the
 * offset is unknown or, after more than 250 s of holdover, the TI is
 * past 500 ns, which restarts the loop's time constant; done, it ends
 * the TI limit's override (0x4d350).  The 58503A's waits 350 s, and
 * calls no output_inactive_request_bit_11. */
pll_stage_result holdover_recovery(bool same_stage) {
  static const uint8_t MOST_FAILURES = 5;
  static const float SHORT_HOLDOVER = 250.0f;
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
    } else if (float_compare(unsigned_to_float(pll.holdover_duration),
                             SHORT_HOLDOVER) > 0 ||
               double_compare(float_to_double(float_absolute_on_stack(ti)),
                              CLOSE_TI) < 0) {
      pll.loop_time_constant = PLL_FIRST_TIME_CONSTANT;
    }
  }
  if (result == PLL_STAGE_RESULT_DONE) {
    pll.ti_limit_override = 0;
  }
  pll_stages_report_holds();
  return result;
}

/* Whether the GPS reference is questionable, gps.references[0].valid 2,
 * and good: valid with its 1PPS alive, questionable in a PLL stage, or
 * while PLL_CLOCK_FROM_GPS_HELD is set; its coming and going reported as
 * satellites sufficient (revisions.md, "Reference state") (0x4e41c). */
void gps_reference_update(void) {
  gps_reference_questionable =
      gps.references[0].valid == MONITORS_REFERENCE_QUESTIONABLE;
  bool good =
      (gps.references[0].valid == MONITORS_REFERENCE_VALID &&
       gps_pps_dead == 0) ||
      (gps_reference_questionable && (pll.stage == PLL_STAGE_NORMAL_PLL ||
                                      pll.stage == PLL_STAGE_STARTUP_PLL)) ||
      *PLL_CLOCK_FROM_GPS_HELD != 0;

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

/* For a TI of 100 ns or more either way, asks for one 100 ns step against
 * it and gives 1; 0 within 100 ns (0x4e48e).  The 58503A's asks for one
 * step a 100 ns up to 300 ns, and gives 4 past; here the bound is 90 ns,
 * which no TI that reaches it meets, so that every step is one
 * (docs/firmware/quirks.md). */
uint8_t fine_sync_adjust(float ti) {
  static const float STEP = 1e-07f;
  static const float MOST = 9e-08f;

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
    return 1;
  }
  if (float_compare(ti, MOST) < 0) {
    uint32_t steps = float_to_unsigned(float_divide(ti, STEP));

    sync_request(COUNTER_SYNC_POSITIVE, (uint8_t)steps);
    return steps;
  }
  sync_request(COUNTER_SYNC_POSITIVE, 1);
  return 1;
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

/* Starts the start-up loop afresh: the filtered TI and the integral
 * cleared, the EFC's base taken. */
static void pll_stages_startup_restart(float current) {
  pll.ti_filtered = 0.0f;
  loop_steering.integral = 0.0f;
  pll.efc_base = pll_efc_less_temperature(current);
}

/* The start-up loop, stage 5, the 58503A's startup_pll: a
 * proportional-integral loop on 10-second blocks whose time constant
 * grows 5 s a block to max_loop_time, settling once 15 blocks in a row
 * are within 150 ns and then waiting for 7200 s of uptime; a block past
 * 500 ns starts it settling again at the first time constant; done at the
 * full time constant.  A block with no reading summed steers nothing; the
 * OCXO calibration's gain is given the loop each block's first second
 * (0x4d652).  The 58503A's waits for the EFC history's first entries
 * instead of the uptime and does not start again. */
pll_stage_result startup_pll(bool same_stage) {
  static const int32_t SETTLED_BLOCKS = 15;
  static const double SETTLED_TI = 1.5e-07;
  static const double LOST_TI = 5e-07;
  static const float SETTLED_TFOM = 1e-06f;
  static const uint32_t SETTLED_UPTIME = 7200;
  static const float TAU_STEP = 5.0f;
  static const float MINUS_TAU_STEP = -5.0f;
  pll_stage_result result = PLL_STAGE_RESULT_RUNNING;
  float ti;
  float current = adc_average(ADC_OVEN_CURRENT);

  if (same_stage == 0 || (int8_t)pll.startup_state > PLL_STARTUP_LENGTHENING) {
    pll.frequency_adjusted = 0;
    pll_stages_startup_restart(current);
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
  pll_stages_loop_gain();
  if (loop_steering.seconds < PLL_STAGES_BLOCK_SECONDS) {
    return pll_stages_check_causes(result);
  }
  if (pll_stages_loop_mean()) {
    double magnitude =
        float_to_double(float_absolute_on_stack(loop_steering.ti_sum));

    switch (pll.startup_state) {
      case PLL_STARTUP_SETTLING:
        if (loop_steering.large_offset_blocks < SETTLED_BLOCKS) {
          if (double_compare(magnitude, SETTLED_TI) <= 0) {
            loop_steering.large_offset_blocks = 0;
          } else {
            loop_steering.large_offset_blocks++;
          }
        } else {
          if (pll.aging_state != PLL_AGING_COLLECTING) {
            pll.tfom = SETTLED_TFOM;
            pll.efc_history.loop_settled = 1;
          }
          pll.startup_state = PLL_STARTUP_WAITING_FOR_HISTORY;
        }
        if (double_compare(magnitude, LOST_TI) < 0) {
          pll_stages_startup_restart(current);
          pll.loop_time_constant = PLL_FIRST_TIME_CONSTANT;
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
          pll.loop_time_constant = max_loop_time;
          pll_stages_rescale_integral(float_add(max_loop_time, MINUS_TAU_STEP));
          result = PLL_STAGE_RESULT_DONE;
        } else if (double_compare(magnitude, LOST_TI) <= 0) {
          pll_stages_startup_restart(current);
          pll.startup_state = PLL_STARTUP_SETTLING;
          pll.loop_time_constant = PLL_FIRST_TIME_CONSTANT;
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

/* Logs when the reference became valid, once, with the GPS's date and
 * time: the GPS's ("GPS reference valid at", log_first_satellite), or
 * on the external reference, the external's, with the time while the GPS
 * reference is valid. */
static void pll_stages_reference_logged(void) {
  if (gps_reference_logged != 0 || gps_reference_good == 0) {
    return;
  }
  if (pll.external_reference == 0) {
    log_first_satellite(oncore_date_pack(gps.status.date), gps.status.time);
  } else if (gps.references[0].valid == MONITORS_REFERENCE_VALID) {
    log_ext_reference_valid(oncore_date_pack(gps.status.date), gps.status.time);
  } else {
    log_event(LOG_EXT_REFERENCE_VALID);
  }
  gps_reference_logged = 1;
}

/* The hold causes that keep power-up waiting for GPS: the temporary mode
 * and the EFC control mode. */
static const pll_hold_cause PLL_STAGES_POWERUP_HOLDS =
    HOLD_CAUSE_EFC_CONTROL | HOLD_CAUSE_TEMPORARY_MODE;

/* The power-up stage, stage 1, the 58503A's powerup: clears the EFC
 * history, the aging term and the loop's EFC step, waits for the OCXO
 * (ocxo_warm) and for GPS, sets the frequency finely and the clock from
 * GPS, then waits out a near leap second and the leap second's
 * acknowledgment; back to waiting for GPS while it is lost, or while the
 * temporary or EFC control mode holds it (0x4f76a).  Unlike the 58503A's
 * it goes from GPS to the fine setting, leaving coarse to a state nothing
 * sets, raises powerup_flag where the 58503A enables the outer oven, sets
 * no clock while PLL_CLOCK_FROM_GPS_HELD is set, and on the external reference
 * logs the external's validity. */
pll_stage_result powerup(bool same_stage) {
  static const float COARSE_TFOM = 0.001f;
  static const float FINE_TFOM = 2e-06f;
  pll_stage_result result = PLL_STAGE_RESULT_RUNNING;

  if (same_stage == 0 || (int8_t)pll.powerup_state > PLL_POWERUP_LEAP_SECONDS) {
    /* The history and the two floats after it, the aging term and the
     * loop's EFC step (0x4f794). */
    memset(&pll.efc_history, 0,
           offsetof(pll_state, ti_filtered) - offsetof(pll_state, efc_history));
    pll.aging_state = PLL_AGING_START;
    pll.efc_history.loop_settled = 0;
    first_satellite_logged = 0;
    gps_reference_logged = 0;
    pll.powerup_state = PLL_POWERUP_START;
  }
  switch (pll.powerup_state) {
    case PLL_POWERUP_INNER_OVEN:
      interval_next_reading(INTERVAL_ARM_PLAIN, NULL);
      if (ocxo_warm() != 0) {
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
          if (*PLL_CLOCK_FROM_GPS_HELD == 0) {
            clock_set(gps.status.time, oncore_date_pack(gps.status.date));
          }
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

/* Syncs the outputs to GPS when the TI passes the limit threshold or a
 * sync is asked for; whether it did (0x4e55e). */
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

/* Whether the TI's least-squares slope over 10 readings or more, as an
 * EFC correction, leaves the EFC in its range; nothing calls it
 * (0x4cba6). */
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
