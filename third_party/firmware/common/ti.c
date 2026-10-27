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
 * fpga_bits (fpga_bit_table), the entries fpga_set_bit and fpga_set_field
 * take: 0 fconf_regs[0]'s mode, mask 0x03 (an fpga_mode); 1
 * fconf_regs[0]'s preset flag, 0x40; 2 fconf_regs[1]'s start flag,
 * 0x01; 3 fconf_regs[1]'s reference flag, 0x02 (0 the low, 1 the
 * high); 4 fconf_regs[1]'s address bits, 0xf8; then the image's own,
 * fconf_regs[3]'s sync among them.  The calls below name each. */

/* Sets the FPGA up again and reloads the counter's registers. */
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

static const int32_t TI_SPREAD = 200;

/* The tries counter_calibrate and interpolator_selftest_calibrate give
 * interpolator_calibrate. */
static const int32_t TI_CALIBRATE_TRIES = 20;

/* The nonzero the image sets the preset bit with, through fpga_set_bit
 * and counter_configuration_init's fields. */
static const uint8_t TI_PRESET_ON = 8;

/* The interpolator's calibration's defaults, both DACs in the middle. */
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

/* The counter's configuration and the interpolator's calibration at
 * their defaults, and the reading armed. */
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

/* Writes counter_configuration's four registers to the FPGA.
 * The result is what the loop's last compare leaves in D0, 0; its one
 * caller, counter_reset, ignores it. */
int32_t counter_load_registers(void) {
  ti_registers_load(&counter_configuration);
  return 0;
}

/* Asks counter_arm to sync the counter count times by a kind, the
 * loop's steps and the console's clk_synch and sync_imm. */
void sync_request(counter_sync_kind kind, int32_t count) {
  counter_configuration.sync_kind = kind;
  counter_configuration.sync_count = count;
  counter_configuration.sync_mode = COUNTER_SYNC_WHEN_WANTED;
  counter_configuration.sync_state = COUNTER_WORK_WANTED;
}

/* Polls the FPGA's status register for a bit, up to 512 times 10 ticks;
 * the reading is disarmed if it never comes or a 1PPS dies. */
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

/* The counter's count less 0x555555, which the FPGA presets it to. */
uint64_t counter_offset_apply(int32_t high, uint32_t low) {
  static const uint64_t PRESET = 0x555555;

  return ((uint64_t)high << 32 | low) - PRESET;
}

/* The counter's 24-bit count of 100 ns ticks, from FPGA registers 0x16
 * (its high byte) down to 0x12, less its preset. */
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
 * byte. */
uint32_t interpolator_read(void) {
  return ~(uint32_t)fpga_read_byte(FPGA_REGISTER_INTERPOLATOR);
}

/* The interval's reading, into the reading: the count plus the
 * interpolator's fraction of a tick, in seconds, less the configuration's
 * calibration, plus :DIAGnostic:TOFFset; on an error SCPI's NaN, and each
 * error printed. */
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
 * to measure the interpolator's references; 1. */
uint32_t interpolator_prepare(void) {
  ti_registers_load_wanted(&counter_configuration);
  fpga_set_bit(FPGA_BIT_PRESET, TI_PRESET_ON);
  fpga_set_field(FPGA_BIT_MODE, FPGA_MODE_REFERENCES);
  fpga_set_bit(FPGA_BIT_START, 0);
  fpga_set_field(FPGA_BIT_ADDRESS, 0);
  return 1;
}

/* Pulses a measurement of one of the interpolator's references and
 * returns its reading. */
uint8_t interpolator_measure(interpolator_reference reference) {
  fpga_set_bit(FPGA_BIT_REFERENCE, reference);
  fpga_set_bit(FPGA_BIT_PRESET, TI_PRESET_ON);
  fpga_set_bit(FPGA_BIT_START, 1);
  fpga_set_bit(FPGA_BIT_PRESET, 0);
  fpga_set_bit(FPGA_BIT_START, 0);
  counter_wait_ready(TI_STATUS_INTERPOLATED);
  return interpolator_read();
}

/* The low reference's reading with its DAC at a value, a dac_measure. */
uint8_t interpolator_measure_low(char_slot dac) {
  hdac_set(dac.value, TI_HDAC_SLOT_LOW);
  return interpolator_measure(INTERPOLATOR_REFERENCE_LOW);
}

/* The high reference's reading with its DAC at a value, a dac_measure. */
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
 * DAC. */
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
 * low to high or the DAC reaches minimum or maximum; returns the DAC. */
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
 * unless it is at either end of its range; returns the DAC. */
uint8_t hdac_nudge(int32_t error, uint8_t hdac) {
  if (hdac == 0 || hdac >= TI_HDAC_MAXIMUM || error == 0) {
    return hdac;
  }
  hdac += error < 0 ? 1 : -1;
  hdac_set(hdac, TI_HDAC_SLOT_LOW);
  return hdac;
}

/* Runs interpolator_calibrate, up to 20 times, when the configuration
 * asks, and keeps the low reading and the spread as the reading's
 * offset and scale. */
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
