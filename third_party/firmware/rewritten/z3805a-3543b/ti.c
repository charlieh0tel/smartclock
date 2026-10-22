/* The time-interval counter and its interpolator, built on the FPGA,
 * written by hand in place of the decompiler's (docs/firmware/README.md,
 * "Hand-written C"): the counter's configuration (counter_*), the
 * interpolator's calibration (interpolator_*); the one-second reading
 * the loop takes from it is pll_reading.c's.
 * The exported C includes this file after its own, so the types and
 * prototypes it declares are in scope; each function here keeps its
 * generated prototype.  interval_reading and counter_configuration are
 * docs/firmware/structures.md's "The time-interval counter's records";
 * the FPGA's registers, fconf_regs, and its bits, fpga.c's fpga_bits, its
 * "Others".
 *
 * fpga_bits (0x41310), the entries fpga_set_bit and fpga_set_field
 * take: 0 fconf_regs[0]'s mode, mask 0x03 (0 to count, 3 for the
 * interpolator's references); 1 fconf_regs[0]'s preset flag, 0x40; 2
 * fconf_regs[1]'s start flag, 0x01; 3 fconf_regs[1]'s reference flag,
 * 0x02 (0 the low, 1 the high); 4 fconf_regs[1]'s address bits, 0xf8;
 * 5 fconf_regs[3]'s sync, 0x03.  The calls below name each. */

/* Sets the FPGA up again and reloads the counter's registers
 * (0x24976). */
void counter_reset(void) {
  fpga_setup();
  counter_load_registers();
}

/* SCPI's not-a-number, a reading's value on an error. */
static const double TI_NOT_A_NUMBER = 9.91e37;

/* The FPGA's status register, 0, the bits counter_wait_ready waits for:
 * counter_read waits for the count, then the interpolation, and
 * interpolator_measure for the interpolation alone. */
enum {
  TI_STATUS_INTERPOLATED = 0x01,
  TI_STATUS_COUNTED = 0x02,
};

/* The hex DAC's slots, hdac_set's second, that trim the interpolator's
 * low and high references. */
enum {
  TI_HDAC_SLOT_LOW = 2,
  TI_HDAC_SLOT_HIGH = 3,
};

/* Both DACs' start, the middle of their 6-bit range, and its top. */
static const uint8_t TI_HDAC_START = 0x1f;

static const uint8_t TI_HDAC_MAXIMUM = 0x3f;

/* The interpolator's readings at its references: the low in 1 to 25,
 * the high in 201 to 225, the high 200 above the low. */
static const uint8_t TI_LOW_MINIMUM = 1;

static const uint8_t TI_LOW_MAXIMUM = 25;

static const uint8_t TI_HIGH_MINIMUM = 201;

static const uint8_t TI_HIGH_MAXIMUM = 225;

static const int32_t TI_SPREAD = 200;

/* The tries counter_calibrate and interpolator_selftest_calibrate give
 * interpolator_calibrate. */
static const int32_t TI_CALIBRATE_TRIES = 20;

/* A mode and state pair's values (counter_configuration_fields). */
enum {
  TI_STATE_DONE = 1,
  TI_STATE_WANTED = 2,
};

/* The interpolator's calibration's defaults, both DACs in the middle
 * (0x40000). */
void interpolator_calibration_init(interpolator_calibration* calibration) {
  calibration->initialized = 0;
  calibration->calibrated = 0;
  calibration->hdac_2 = TI_HDAC_START;
  calibration->hdac_3 = TI_HDAC_START;
  calibration->result_hdac_2 = calibration->hdac_2;
  calibration->result_hdac_3 = calibration->hdac_3;
  calibration->result_valid = 0;
  calibration->reading_low = 0;
  calibration->reading_high = 0;
}

/* The counter's configuration's defaults, its registers into fconf_regs
 * too, and the reading marked invalid (0x40ed8). */
void counter_configuration_init(counter_configuration_fields* configuration) {
  /* The fields by fpga_bits entry: preset and the high reference
   * flagged, the rest 0. */
  uint8_t fields[] = {0, 8, 0, 1, 0, 0};

  fpga_fields_to_registers(fields, configuration->fpga_registers);
  fpga_fields_to_registers(fields, fconf_regs);
  configuration->calibration = 0.4000003; /* the image's, 0x3fd9999adbb8f8da */
  configuration->interpolator_scale = TI_SPREAD;
  configuration->calibrate_mode = 3;
  configuration->calibrate_state = TI_STATE_WANTED;
  configuration->registers_mode = 1;
  configuration->registers_state = TI_STATE_WANTED;
  interval_reading.value = TI_NOT_A_NUMBER;
  interval_reading.valid = 0;
}

/* The counter's configuration and the interpolator's calibration at
 * their defaults, and the reading armed (0x40f76). */
void counter_init(void) {
  counter_configuration_init(&counter_configuration);
  interpolator_calibration_init(&interpolator_calibration_state);
  interval_reading.armed = 1;
}

/* Writes a configuration's four registers to the FPGA and marks them
 * loaded. */
static void ti_registers_load(counter_configuration_fields* configuration) {
  for (uint8_t reg = 0; reg < sizeof fconf_regs; reg++) {
    fconf_regs[reg] = configuration->fpga_registers[reg];
    fpga_write(reg, fconf_regs[reg]);
  }
  configuration->registers_state = TI_STATE_DONE;
}

/* Writes the configuration's registers if it asks for them. */
static void ti_registers_load_wanted(
    counter_configuration_fields* configuration) {
  if (configuration->registers_mode == 1 &&
      configuration->registers_state == TI_STATE_WANTED) {
    ti_registers_load(configuration);
  }
}

/* Writes counter_configuration's four registers to the FPGA (0x40ea2).
 * The result is what the loop's last compare leaves in D0, 0; its one
 * caller, counter_reset, ignores it. */
int32_t counter_load_registers(void) {
  ti_registers_load(&counter_configuration);
  return 0;
}

/* Sets fconf_regs[3]'s sync bits, fpga_bits entry 5, to a
 * sync_kind, through 0 (0x40a84). */
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
  fpga_set_bit(FPGA_BIT_PRESET, 8);
  fpga_set_field(FPGA_BIT_MODE, 0); /* count */
  fpga_set_bit(FPGA_BIT_START, 0);
  fpga_set_field(FPGA_BIT_ADDRESS, 0);
  if (configuration->sync_mode == 2 &&
      configuration->sync_state == TI_STATE_WANTED) {
    counter_set_sync(configuration->sync_kind);
    configuration->sync_count--;
    if (configuration->sync_count < 1) {
      configuration->sync_state = TI_STATE_DONE;
    }
  }
  fpga_set_bit(FPGA_BIT_PRESET, 0);
}

/* Polls the FPGA's status register for a bit, up to 512 times 10 ticks;
 * the reading is disarmed if it never comes or a 1PPS dies (0x4091a). */
uint8_t counter_wait_ready(uint8_t mask) {
  static const uint32_t POLL_TICKS = 10;
  uint16_t polls = 0x200;
  bool ready = false;
  uint8_t status = fpga_read(FPGA_REGISTER_STATUS);

  while (!gps_pps_dead && !internal_pps_dead && interval_reading.armed &&
         polls != 0) {
    if (status & mask) {
      ready = true;
      break;
    }
    psos_pause(POLL_TICKS);
    polls--;
    status = fpga_read(FPGA_REGISTER_STATUS);
  }
  if (polls == 0 || gps_pps_dead || internal_pps_dead) {
    interval_reading.armed = 0;
  }
  return ready;
}

/* The counter's count less 0x555555, which the FPGA presets it to
 * (0x40bda). */
uint64_t counter_offset_apply(int32_t high, uint32_t low) {
  static const uint64_t PRESET = 0x555555;

  return ((uint64_t)high << 32 | low) - PRESET;
}

/* The counter's 24-bit count of 100 ns ticks, from FPGA registers 0x16
 * (its high byte) down to 0x12, less its preset (0x40c06). */
int64_t counter_read_raw(void) {
  /* a byte is two registers' nibbles, fpga_read_byte's */
  static const uint8_t REGISTERS_PER_BYTE = 2;
  uint32_t count = 0;

  for (uint8_t reg = FPGA_REGISTER_COUNT_HIGH; reg >= FPGA_REGISTER_COUNT_LOW;
       reg -= REGISTERS_PER_BYTE) {
    count = count << 8 | fpga_read_byte(reg);
  }
  return counter_offset_apply(0, count);
}

/* The interpolator's reading, the complement of FPGA register 0x10's
 * byte (0x40c56). */
uint32_t interpolator_read(void) {
  return ~(uint32_t)fpga_read_byte(FPGA_REGISTER_INTERPOLATOR);
}

/* The interval's reading, into the reading: the count plus the
 * interpolator's fraction of a tick, in seconds, less the configuration's
 * calibration, plus :DIAGnostic:TOFFset; on an error SCPI's NaN, and each
 * error printed (0x40d34). */
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

/* Loads the FPGA's registers if the configuration asks and sets it up
 * to measure the interpolator's references; 1 (0x401c4). */
uint32_t interpolator_prepare(void) {
  ti_registers_load_wanted(&counter_configuration);
  fpga_set_bit(FPGA_BIT_PRESET, 8);
  fpga_set_field(FPGA_BIT_MODE, 3); /* the interpolator's references */
  fpga_set_bit(FPGA_BIT_START, 0);
  fpga_set_field(FPGA_BIT_ADDRESS, 0);
  return 1;
}

/* Pulses a measurement of one of the interpolator's references and
 * returns its reading (0x40030). */
uint8_t interpolator_measure(interpolator_reference reference) {
  fpga_set_bit(FPGA_BIT_REFERENCE, reference);
  fpga_set_bit(FPGA_BIT_PRESET, 8);
  fpga_set_bit(FPGA_BIT_START, 1);
  fpga_set_bit(FPGA_BIT_PRESET, 0);
  fpga_set_bit(FPGA_BIT_START, 0);
  counter_wait_ready(TI_STATUS_INTERPOLATED);
  return interpolator_read();
}

/* The low reference's reading with its DAC at a value, a dac_measure
 * (0x4007e). */
uint8_t interpolator_measure_low(char_slot dac) {
  hdac_set(dac.value, TI_HDAC_SLOT_LOW);
  return interpolator_measure(INTERPOLATOR_REFERENCE_LOW);
}

/* The high reference's reading with its DAC at a value, a dac_measure
 * (0x40096). */
uint8_t interpolator_measure_high(char_slot dac) {
  hdac_set(dac.value, TI_HDAC_SLOT_HIGH);
  return interpolator_measure(INTERPOLATOR_REFERENCE_HIGH);
}

/* A dac_measure's reading with its DAC at a value, the byte the
 * original pushes in a 2-byte slot. */
static uint8_t ti_measure_at(dac_measure* measure, uint8_t dac) {
  return measure((char_slot){.value = dac});
}

/* Bisects a DAC between minimum and maximum, up to 10 measurements,
 * until its reading is in low to high or the range closes; returns the
 * DAC (0x400b0). */
uint8_t dac_search_bisect(uint8_t dac, uint8_t minimum, uint8_t maximum,
                          uint8_t low, uint8_t high, dac_measure* measure) {
  static const uint8_t MEASUREMENTS = 10;

  for (uint8_t left = MEASUREMENTS; left != 0; left--) {
    uint8_t reading = ti_measure_at(measure, dac);

    if ((reading >= low && reading <= high) || minimum == maximum) {
      break;
    }
    if (reading < low) {
      maximum = dac - 1;
    } else {
      minimum = dac + 1;
    }
    dac = ((uint32_t)minimum + maximum) / 2;
  }
  return dac;
}

/* Steps a DAC by one, up to 20 measurements, until its reading is in
 * low to high or the DAC reaches minimum or maximum; returns the DAC
 * (0x4012e). */
uint8_t dac_search_step(uint8_t dac, uint8_t minimum, uint8_t maximum,
                        uint8_t low, uint8_t high, dac_measure* measure) {
  static const uint8_t MEASUREMENTS = 20;

  for (uint8_t left = MEASUREMENTS; left != 0; left--) {
    uint8_t reading = ti_measure_at(measure, dac);

    if ((reading >= low && reading <= high) || dac >= maximum ||
        dac <= minimum) {
      break;
    }
    if (reading < low) {
      dac--;
    } else {
      dac++;
    }
  }
  return dac;
}

/* Moves the low reference's DAC a step against the spread's error,
 * unless it is at either end of its range; returns the DAC
 * (0x4018e). */
uint8_t hdac_nudge(int32_t error, uint8_t hdac) {
  if (hdac == 0 || hdac >= TI_HDAC_MAXIMUM || error == 0) {
    return hdac;
  }
  hdac += error < 0 ? 1 : -1;
  hdac_set(hdac, TI_HDAC_SLOT_LOW);
  return hdac;
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

/* Runs interpolator_calibrate, up to 20 times, when the configuration
 * asks, and keeps the low reading and the spread as the reading's
 * offset and scale (0x405bc). */
void counter_calibrate(interval_reading_fields* reading,
                       counter_configuration_fields* configuration) {
  uint8_t calibrated = 0;

  if (!interval_reading.armed) {
    reading->errors |= COUNTER_MEASUREMENT_ABORTED;
  }
  if (reading->errors != 0 ||
      !(configuration->calibrate_mode == 3 ||
        (configuration->calibrate_mode == 1 &&
         configuration->calibrate_state == TI_STATE_WANTED))) {
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
    configuration->calibrate_state = TI_STATE_DONE;
  } else {
    configuration->interpolator_scale = TI_SPREAD;
    configuration->interpolator_offset = 0;
    reading->errors |= COUNTER_CALIBRATION_FAILURE;
  }
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
 * (0x40f98). */
void counter_selftest(void) {
  fpga_set_bit(FPGA_BIT_PRESET, 8);
  selftest.fpga = counter_read_raw() != 0;
}
