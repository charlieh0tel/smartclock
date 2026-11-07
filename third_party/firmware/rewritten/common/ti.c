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
