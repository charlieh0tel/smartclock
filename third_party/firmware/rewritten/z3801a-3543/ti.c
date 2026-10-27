/* ti.c's functions that are this image's own: the rest are
 * common/ti.c's, the same in every image (docs/firmware/rewrite.md,
 * "Shared C").  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype. */

/* The interpolator's readings at its references: the low in 1 to 25,
 * the high in 201 to 225, the high 200 above the low. */
static const uint8_t TI_LOW_MINIMUM = 1;

static const uint8_t TI_LOW_MAXIMUM = 25;

static const uint8_t TI_HIGH_MINIMUM = 201;

static const uint8_t TI_HIGH_MAXIMUM = 225;

/* The counter's configuration's defaults, its registers into fconf_regs
 * too, and the reading marked invalid (0x40ed8). */
void counter_configuration_init(counter_configuration_fields* configuration) {
  /* The fields by fpga_bits entry: the count, preset, the high
   * reference and register 2's third flag set, the rest 0. */
  uint8_t fields[] = {[FPGA_BIT_MODE] = FPGA_MODE_COUNT,
                      [FPGA_BIT_PRESET] = TI_PRESET_ON,
                      [FPGA_BIT_REFERENCE] = INTERPOLATOR_REFERENCE_HIGH,
                      [FPGA_BIT_REGISTER_2_BIT_2] = 1,
                      [FPGA_BIT_SYNC] = 0};

  fpga_fields_to_registers(fields, configuration->fpga_registers);
  fpga_fields_to_registers(fields, fconf_regs);
  configuration->calibration = 0.4000003; /* the image's, 0x3fd9999adbb8f8da */
  configuration->interpolator_scale = TI_SPREAD;
  configuration->calibrate_mode = COUNTER_CALIBRATE_ALWAYS;
  configuration->calibrate_state = COUNTER_WORK_WANTED;
  configuration->registers_mode = COUNTER_REGISTERS_WHEN_WANTED;
  configuration->registers_state = COUNTER_WORK_WANTED;
  interval_reading.value = TI_NOT_A_NUMBER;
  interval_reading.valid = 0;
}

/* Sets fconf_regs[3]'s sync bits, fpga_bits entry 5, to a
 * sync_kind, through 0. */
void counter_set_sync(uint8_t kind) {
  static const uint8_t SYNC_MASK = 0x03;

  fconf_regs[3] &= ~SYNC_MASK;
  fpga_write(3, fconf_regs[3]);
  fconf_regs[3] |= kind;
  fpga_write(3, fconf_regs[3]);
}

/* Reloads the FPGA's registers if the configuration asks, presets the
 * counter, syncs it if asked, and releases it to count (0x40c6a). */
void counter_arm(interval_reading_fields* reading,
                 counter_configuration_fields* configuration) {
  if (!interval_reading.armed) {
    reading->errors |= COUNTER_MEASUREMENT_ABORTED;
  }
  if (reading->errors != 0) {
    return;
  }
  ti_registers_load_wanted(configuration);
  fpga_set_bit(FPGA_BIT_PRESET, TI_PRESET_ON);
  fpga_set_field(FPGA_BIT_MODE, FPGA_MODE_COUNT);
  fpga_set_bit(FPGA_BIT_START, 0);
  fpga_set_field(FPGA_BIT_ADDRESS, 0);
  if (configuration->sync_mode == COUNTER_SYNC_WHEN_WANTED &&
      configuration->sync_state == COUNTER_WORK_WANTED) {
    counter_set_sync(configuration->sync_kind);
    configuration->sync_count--;
    if (configuration->sync_count < 1) {
      configuration->sync_state = COUNTER_WORK_DONE;
    }
  }
  fpga_set_bit(FPGA_BIT_PRESET, 0);
}

/* Sets both DACs from the calibration and keeps both references'
 * readings in it. */
static void ti_measure_both(interpolator_calibration* calibration) {
  hdac_set(calibration->hdac_2, TI_HDAC_SLOT_LOW);
  hdac_set(calibration->hdac_3, TI_HDAC_SLOT_HIGH);
  calibration->reading_low = interpolator_measure(INTERPOLATOR_REFERENCE_LOW);
  calibration->reading_high = interpolator_measure(INTERPOLATOR_REFERENCE_HIGH);
}

static bool ti_low_in_range(uint8_t reading) {
  return reading >= TI_LOW_MINIMUM && reading <= TI_LOW_MAXIMUM;
}

static bool ti_high_in_range(uint8_t reading) {
  return reading >= TI_HIGH_MINIMUM && reading <= TI_HIGH_MAXIMUM;
}

/* Marks a calibration done and keeps its DACs as the last good. */
static void ti_calibration_keep(interpolator_calibration* calibration) {
  calibration->calibrated = 1;
  calibration->result_hdac_2 = calibration->hdac_2;
  calibration->result_hdac_3 = calibration->hdac_3;
  calibration->result_valid = 1;
}

/* Trims the DACs until the references read in their ranges, 200 apart,
 * or within one of it after 20 nudges; false, and the calibration
 * started afresh, if not (0x40248). */
uint8_t interpolator_calibrate(interpolator_calibration* calibration) {
  static const uint8_t HDAC_MINIMUM = 0;
  /* the high reference's DAC is first stepped to put its reading this
   * window below the low's, as a uint8_t wraps */
  static const uint8_t STEP_BELOW_FAR = 0x3c;
  static const uint8_t STEP_BELOW_NEAR = 0x34;
  static const uint8_t NUDGES = 20;
  bool calibrated = false;
  int32_t spread;

  interpolator_prepare();
  if (!calibration->initialized) {
    calibration->initialized = 1;
    calibration->hdac_2 = TI_HDAC_START;
    calibration->hdac_3 = TI_HDAC_START;
  }
  ti_measure_both(calibration);
  spread = calibration->reading_high - calibration->reading_low;
  if (spread == TI_SPREAD && ti_low_in_range(calibration->reading_low) &&
      ti_high_in_range(calibration->reading_high)) {
    calibration->calibrated = 1;
    return 1;
  }

  if (!ti_high_in_range(calibration->reading_high)) {
    calibration->hdac_3 = dac_search_bisect(
        calibration->hdac_3, HDAC_MINIMUM, TI_HDAC_MAXIMUM, TI_HIGH_MINIMUM,
        TI_HIGH_MAXIMUM, interpolator_measure_high);
  }
  calibration->reading_low = interpolator_measure(INTERPOLATOR_REFERENCE_LOW);
  if (!ti_low_in_range(calibration->reading_low)) {
    calibration->hdac_2 = dac_search_bisect(
        calibration->hdac_2, HDAC_MINIMUM, TI_HDAC_MAXIMUM, TI_LOW_MINIMUM,
        TI_LOW_MAXIMUM, interpolator_measure_low);
  }
  calibration->reading_low = interpolator_measure(INTERPOLATOR_REFERENCE_LOW);
  calibration->hdac_3 = dac_search_step(
      calibration->hdac_3, HDAC_MINIMUM, TI_HDAC_MAXIMUM,
      calibration->reading_low - STEP_BELOW_FAR,
      calibration->reading_low - STEP_BELOW_NEAR, interpolator_measure_high);
  calibration->reading_high = interpolator_measure(INTERPOLATOR_REFERENCE_HIGH);
  calibration->reading_low = interpolator_measure(INTERPOLATOR_REFERENCE_LOW);

  for (uint8_t nudge = 0; nudge < NUDGES && !calibrated; nudge++) {
    calibration->hdac_2 = hdac_nudge(
        calibration->reading_high - calibration->reading_low - TI_SPREAD,
        calibration->hdac_2);
    if (!ti_high_in_range(calibration->reading_high)) {
      calibration->hdac_3 = dac_search_step(
          calibration->hdac_3, HDAC_MINIMUM, TI_HDAC_MAXIMUM, TI_HIGH_MINIMUM,
          TI_HIGH_MAXIMUM, interpolator_measure_high);
    }
    calibration->reading_high =
        interpolator_measure(INTERPOLATOR_REFERENCE_HIGH);
    calibration->reading_low = interpolator_measure(INTERPOLATOR_REFERENCE_LOW);
    spread = calibration->reading_high - calibration->reading_low;
    calibrated = spread == TI_SPREAD;
  }
  if (!calibrated) {
    calibrated = spread == TI_SPREAD - 1 || spread == TI_SPREAD + 1;
  }
  if (calibrated) {
    ti_calibration_keep(calibration);
  } else {
    calibration->initialized = 0;
    calibration->calibrated = 0;
  }
  return calibrated;
}

/* Whether both references read in their ranges. */
static bool ti_readings_in_range(const interpolator_calibration* calibration) {
  return ti_low_in_range(calibration->reading_low) &&
         ti_high_in_range(calibration->reading_high);
}

/* Sets both DACs and checks both references read in their ranges,
 * again with the last good DACs if not; 1 if so, 0 for a calibration
 * never initialized (0x4047c).  On a failure it returns D2's low byte as
 * its caller left it (docs/firmware/quirks.md). */
uint8_t interpolator_verify(interpolator_calibration* calibration) {
  const uint8_t caller_d2 = CPU_CALLERS_REGISTER(uint32_t, "d2");

  if (calibration->initialized == 0) {
    return 0;
  }
  calibration->calibrated = 0;
  interpolator_prepare();
  ti_measure_both(calibration);
  if (!ti_readings_in_range(calibration)) {
    if (calibration->result_valid == 0) {
      return caller_d2;
    }
    calibration->hdac_2 = calibration->result_hdac_2;
    calibration->hdac_3 = calibration->result_hdac_3;
    ti_measure_both(calibration);
    if (!ti_readings_in_range(calibration)) {
      return caller_d2;
    }
  }
  calibration->calibrated = 1;
  return 1;
}

/* Takes both references' readings with the calibration's DACs, both in
 * the middle the first time; "First-time interp test." (0x40556). */
void interpolator_sample(interpolator_calibration* calibration) {
  interpolator_prepare();
  if (!calibration->initialized) {
    calibration->hdac_2 = TI_HDAC_START;
    calibration->hdac_3 = TI_HDAC_START;
    task_dmessage("First-time interp test. \r\n");
  }
  ti_measure_both(calibration);
}

/* The interpolator's calibration from its defaults, up to 20 tries; the
 * tries kept, and, if none took, the flag set and interpolator_verify's
 * result returned (0x40670). */
uint8_t interpolator_selftest_calibrate(void) {
  interpolator_calibration calibration;
  uint8_t calibrated;
  int32_t tries = 0;

  selftest.interpolator_calibrated = 0;
  do {
    interpolator_calibration_init(&calibration);
    calibrated = interpolator_calibrate(&calibration);
    tries++;
  } while (!calibrated && tries < TI_CALIBRATE_TRIES);
  selftest.interpolator_tries = tries;
  if (!calibrated) {
    selftest.interpolator_calibrated = 1;
    calibrated = interpolator_verify(&calibration);
  }
  return calibrated;
}

/* The interpolator's self-test: failed unless it calibrates (0x406c8). */
void interpolator_selftest(void) {
  selftest.interpolator = !interpolator_selftest_calibrate();
}

/* Self-test 13: samples the interpolator with the calibrated DACs and
 * keeps both references' readings (0x406e2). */
void interpolator_sample_test(void) {
  interpolator_calibration calibration;

  interpolator_calibration_init(&calibration);
  calibration.initialized = interpolator_calibration_state.initialized;
  if (interpolator_calibration_state.initialized) {
    calibration.hdac_2 = interpolator_calibration_state.hdac_2;
    calibration.hdac_3 = interpolator_calibration_state.hdac_3;
  }
  interpolator_sample(&calibration);
  selftest.interpolator = 0;
  selftest.interpolator_readings[0] = calibration.reading_low;
  selftest.interpolator_readings[1] = calibration.reading_high;
}

/* The FPGA's self-test: fails unless the preset counter reads zero
 * (0x40faa). */
void counter_selftest(void) {
  fpga_set_bit(FPGA_BIT_PRESET, TI_PRESET_ON);
  selftest.fpga = counter_read_raw() != 0;
}
