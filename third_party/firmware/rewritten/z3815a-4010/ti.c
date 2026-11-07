/* ti.c's functions that are this image's own: the rest are
 * common/ti.c's, the same in every image (docs/firmware/rewrite.md,
 * "Shared C").  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype.  This wave takes the interpolator's calibration, check and
 * sample, the counter's reading, calibration and sync, and the
 * interpolator and counter self-tests, which read the selftest record
 * the types wave types; and the FPGA register loads (counter_arm,
 * counter_load_registers, interpolator_prepare,
 * counter_configuration_init), which run over eight registers here, not
 * the 58503A's four, over this image's counter_configuration_fields, and
 * the alarm's request, which counter_arm carries out as it does the
 * sync's. */

/* Writes a configuration's registers, as many as fconf_regs holds, to
 * the FPGA and marks them loaded. */
static void ti_registers_load(counter_configuration_fields* configuration) {
  for (uint8_t reg = 0; reg < sizeof fconf_regs; reg++) {
    fconf_regs[reg] = configuration->fpga_registers[reg];
    fpga_write(reg, fconf_regs[reg]);
  }
  configuration->registers_state = COUNTER_WORK_DONE;
}

/* Writes the configuration's registers if it asks for them. */
static void ti_registers_load_wanted(
    counter_configuration_fields* configuration) {
  if (configuration->registers_mode == COUNTER_REGISTERS_WHEN_WANTED &&
      configuration->registers_state == COUNTER_WORK_WANTED) {
    ti_registers_load(configuration);
  }
}

/* The counter's configuration and the interpolator's calibration at
 * their defaults, and the reading armed (0x49b14). */
void counter_init(void) {
  counter_configuration_init(&counter_configuration);
  interpolator_calibration_init(&interpolator_calibration_state);
  interval_reading.armed = 1;
}

/* The interpolator's readings at its references: the low in 1 to 25,
 * the high in 201 to 225, the high 200 above the low. */
static const uint8_t TI_LOW_MINIMUM = 1;

static const uint8_t TI_LOW_MAXIMUM = 25;

static const uint8_t TI_HIGH_MINIMUM = 201;

static const uint8_t TI_HIGH_MAXIMUM = 225;

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

/* Whether both references read in their ranges. */
static bool ti_readings_in_range(const interpolator_calibration* calibration) {
  return ti_low_in_range(calibration->reading_low) &&
         ti_high_in_range(calibration->reading_high);
}

/* Sets the interpolator's reference back to the high one, as
 * counter_configuration_init's fields have it: the Z3815A's interpolator
 * routines end so, as the 58503A's do. */
static void ti_reference_high(void) {
  fpga_set_bit(FPGA_BIT_REFERENCE, INTERPOLATOR_REFERENCE_HIGH);
}

/* interpolator_calibrate's trim: the DACs until the references read in
 * their ranges, 200 apart, or within one of it after 20 nudges; false,
 * and the calibration started afresh, if not. */
static uint8_t ti_calibrate(interpolator_calibration* calibration) {
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

/* Trims the DACs as ti_calibrate does, then sets the reference back to
 * the high one (0x48d4e). */
uint8_t interpolator_calibrate(interpolator_calibration* calibration) {
  const uint8_t calibrated = ti_calibrate(calibration);

  ti_reference_high();
  return calibrated;
}

/* interpolator_verify's check: both DACs set and both references read in
 * their ranges, again with the last good DACs if not; 1 if so, 0 for a
 * calibration never initialized.  On a failure it returns the caller's
 * D2's low byte, which interpolator_verify passes it. */
static uint8_t ti_verify(interpolator_calibration* calibration,
                         uint8_t caller_d2) {
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

/* Checks the calibration as ti_verify does, then sets the reference
 * back to the high one (0x48f92).  On a failure it returns D2's low
 * byte as its caller left it, as the Z3805A's does
 * (docs/firmware/quirks.md). */
uint8_t interpolator_verify(interpolator_calibration* calibration) {
  const uint8_t verified =
      ti_verify(calibration, CPU_CALLERS_REGISTER(uint32_t, "d2"));

  ti_reference_high();
  return verified;
}

/* Takes both references' readings with the calibration's DACs, both in
 * the middle the first time; "First-time interp test." Then sets the
 * reference back to the high one (0x4907c). */
void interpolator_sample(interpolator_calibration* calibration) {
  interpolator_prepare();
  if (!calibration->initialized) {
    calibration->hdac_2 = TI_HDAC_START;
    calibration->hdac_3 = TI_HDAC_START;
    task_dmessage("First-time interp test. \r\n");
  }
  ti_measure_both(calibration);
  ti_reference_high();
}

/* The interpolator's calibration from its defaults, up to 20 tries; the
 * tries kept, and, if none took, the flag set and interpolator_verify's
 * result returned (0x491a4). */
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

/* The interpolator's self-test: failed unless it calibrates (0x491fc). */
void interpolator_selftest(void) {
  selftest.interpolator = !interpolator_selftest_calibrate();
}

/* Self-test 13: samples the interpolator with the calibrated DACs and
 * keeps both references' readings (0x49216). */
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
 * (0x49b36). */
void counter_selftest(void) {
  fpga_set_bit(FPGA_BIT_PRESET, TI_PRESET_ON);
  selftest.fpga = counter_read_raw() != 0;
}

/* Sets fconf_regs[3]'s sync bits, fpga_bits entry 7, to a sync_kind,
 * through 0 (0x495e4). */
void counter_set_sync(uint8_t kind) {
  static const uint8_t SYNC_MASK = 0x03;

  fconf_regs[3] &= ~SYNC_MASK;
  fpga_write(3, fconf_regs[3]);
  fconf_regs[3] |= kind;
  fpga_write(3, fconf_regs[3]);
}

/* The interval's reading, into the reading: the count plus the
 * interpolator's fraction of a tick, in seconds, less the configuration's
 * calibration, plus :DIAGnostic:TOFFset; on an error SCPI's NaN, and each
 * error printed (0x498be). */
void counter_read(interval_reading_fields* reading,
                  counter_configuration_fields* configuration) {
  static const double SECONDS_PER_TICK = 1e-7;
  static const uint16_t READING_TAG = 0xfff6;

  if (!interval_reading.armed) {
    reading->errors |= COUNTER_MEASUREMENT_ABORTED;
  }
  if (reading->errors == 0) {
    if (counter_wait_ready(TI_STATUS_COUNTED) &&
        counter_wait_ready(TI_STATUS_INTERPOLATED)) {
      const int64_t count = counter_read_raw();
      const uint8_t interpolated = interpolator_read();
      const double fraction = double_divide_reversed(
          integer_to_double(configuration->interpolator_scale),
          integer_to_double(interpolated - configuration->interpolator_offset));

      reading->value = int64_to_double(count);
      double_add_in_place(&reading->value, fraction);
      double_multiply_in_place(&reading->value, SECONDS_PER_TICK);
      double_subtract_in_place(&reading->value, configuration->calibration);
      double_add_in_place(&reading->value,
                          float_to_double(scpi_value_DIAGnostic_TOFFset));
      reading->tag = READING_TAG;
      reading->valid = 1;
    } else {
      reading->errors |= COUNTER_GATE_FAILURE | COUNTER_MEASUREMENT_ABORTED;
    }
  }
  if (reading->errors == 0) {
    return;
  }
  reading->value = TI_NOT_A_NUMBER;
  reading->valid = 0;
  if (reading->errors & COUNTER_GATE_FAILURE) {
    task_dmessage("Gate failure\r\n");
  }
  if (reading->errors & COUNTER_RESULT_UNCOMPUTABLE) {
    task_dmessage("Result uncomputable\r\n");
  }
  if (reading->errors & COUNTER_MEASUREMENT_ABORTED) {
    task_dmessage("Measurement aborted\r\n");
  }
  if (reading->errors & COUNTER_CALIBRATION_FAILURE) {
    task_dmessage("Interpolator calibration failure\r\n");
  }
}

/* Runs interpolator_calibrate, up to 20 times, when the configuration
 * asks, and keeps the low reading and the spread as the reading's
 * offset and scale (0x490f0). */
void counter_calibrate(interval_reading_fields* reading,
                       counter_configuration_fields* configuration) {
  uint8_t calibrated = 0;

  if (!interval_reading.armed) {
    reading->errors |= COUNTER_MEASUREMENT_ABORTED;
  }
  if (reading->errors != 0 ||
      !(configuration->calibrate_mode == COUNTER_CALIBRATE_ALWAYS ||
        (configuration->calibrate_mode == COUNTER_CALIBRATE_WHEN_WANTED &&
         configuration->calibrate_state == COUNTER_WORK_WANTED))) {
    return;
  }
  for (int32_t tries = 0;
       tries < TI_CALIBRATE_TRIES && interval_reading.armed && !calibrated;
       tries++) {
    calibrated = interpolator_calibrate(&interpolator_calibration_state);
    if (!calibrated) {
      calibrated = interpolator_verify(&interpolator_calibration_state);
    }
  }
  if (interpolator_calibration_state.calibrated == 1) {
    configuration->interpolator_offset =
        interpolator_calibration_state.reading_low;
    configuration->interpolator_scale =
        interpolator_calibration_state.reading_high -
        interpolator_calibration_state.reading_low;
    configuration->calibrate_state = COUNTER_WORK_DONE;
  } else {
    configuration->interpolator_scale = TI_SPREAD;
    configuration->interpolator_offset = 0;
    reading->errors |= COUNTER_CALIBRATION_FAILURE;
  }
}

/* Loads the FPGA's registers if the configuration asks and sets it up
 * to measure the interpolator's references; 1.  The 58503A's, over this
 * image's eight registers (0x48cca). */
uint32_t interpolator_prepare(void) {
  ti_registers_load_wanted(&counter_configuration);
  fpga_set_bit(FPGA_BIT_PRESET, TI_PRESET_ON);
  fpga_set_field(FPGA_BIT_MODE, FPGA_MODE_REFERENCES);
  fpga_set_bit(FPGA_BIT_START, 0);
  fpga_set_field(FPGA_BIT_ADDRESS, 0);
  return 1;
}

/* Asks counter_arm to set FPGA_BIT_ALARM to a value, the alarm's request
 * as sync_request is the sync's (0x494d8). */
void counter_alarm_request(uint8_t alarm) {
  counter_configuration.alarm = alarm;
  counter_configuration.alarm_mode = COUNTER_ALARM_WHEN_WANTED;
  counter_configuration.alarm_state = COUNTER_WORK_WANTED;
}

/* Sets FPGA_BIT_ALARM's field to a value, counter_arm's (0x494f2). */
void counter_set_alarm(uint8_t alarm) { fpga_set_field(FPGA_BIT_ALARM, alarm); }

/* Asks counter_arm to sync the counter count times by a kind, the
 * loop's steps and the console's clk_synch and sync_imm (0x495c4). */
void sync_request(counter_sync_kind kind, int32_t count) {
  counter_configuration.sync_kind = kind;
  counter_configuration.sync_count = count;
  counter_configuration.sync_mode = COUNTER_SYNC_WHEN_WANTED;
  counter_configuration.sync_state = COUNTER_WORK_WANTED;
}

/* Reloads the FPGA's registers if the configuration asks, presets the
 * counter, syncs it if asked, sets the alarm if asked, and releases it to
 * count: the 58503A's with the alarm's pair after the sync's (0x4988c to
 * 0x498a6) (0x497ca). */
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
  ti_reference_high();
  if (configuration->sync_mode == COUNTER_SYNC_WHEN_WANTED &&
      configuration->sync_state == COUNTER_WORK_WANTED) {
    counter_set_sync(configuration->sync_kind);
    configuration->sync_count--;
    if (configuration->sync_count < 1) {
      configuration->sync_state = COUNTER_WORK_DONE;
    }
  }
  if (configuration->alarm_mode == COUNTER_ALARM_WHEN_WANTED &&
      configuration->alarm_state == COUNTER_WORK_WANTED) {
    counter_set_alarm(configuration->alarm);
    configuration->alarm_state = COUNTER_WORK_DONE;
  }
  fpga_set_bit(FPGA_BIT_PRESET, 0);
}

/* Writes counter_configuration's eight registers to the FPGA and marks
 * them loaded; 0 (0x49a2c). */
int32_t counter_load_registers(void) {
  ti_registers_load(&counter_configuration);
  return 0;
}

/* The counter's configuration's defaults, its registers into fconf_regs
 * too, and the reading marked invalid: the 58503A's with its own
 * calibration, all eight registers' fields and the alarm's pair done
 * (0x49a62). */
void counter_configuration_init(counter_configuration_fields* configuration) {
  /* The fields by fpga_bit_table entry: the count, preset and the high
   * reference flagged, the rest 0, the alarm's and the sync's among them.
   * Even, since gcc fills it by words and longs, misaligned at an odd
   * address. */
  uint8_t __attribute__((aligned(2))) fields[] = {
      [FPGA_BIT_MODE] = FPGA_MODE_COUNT,
      [FPGA_BIT_PRESET] = TI_PRESET_ON,
      [FPGA_BIT_REFERENCE] = INTERPOLATOR_REFERENCE_HIGH,
      [FPGA_BIT_COUNTER_SYNC] = 0};

  fpga_fields_to_registers(fields, configuration->fpga_registers);
  fpga_fields_to_registers(fields, fconf_regs);
  configuration->calibration = 0.3932162; /* the image's, 0x3fd92a7447d0792a */
  configuration->interpolator_scale = TI_SPREAD;
  configuration->calibrate_mode = COUNTER_CALIBRATE_ALWAYS;
  configuration->calibrate_state = COUNTER_WORK_WANTED;
  configuration->registers_mode = COUNTER_REGISTERS_WHEN_WANTED;
  configuration->registers_state = COUNTER_WORK_WANTED;
  configuration->alarm_mode = COUNTER_ALARM_WHEN_WANTED;
  configuration->alarm_state = COUNTER_WORK_DONE;
  interval_reading.value = TI_NOT_A_NUMBER;
  interval_reading.valid = 0;
}
