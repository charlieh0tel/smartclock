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

/* The interpolator's calibration from its defaults, up to 20 tries; the
 * tries kept, and, if none took, the flag set and interpolator_verify's
 * result returned (0x4069e). */
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

/* The interpolator's self-test: failed unless it calibrates (0x406f6). */
void interpolator_selftest(void) {
  selftest.interpolator = !interpolator_selftest_calibrate();
}

/* Self-test 13: samples the interpolator with the calibrated DACs and
 * keeps both references' readings (0x40710). */
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
 * (0x41034). */
void counter_selftest(void) {
  fpga_set_bit(FPGA_BIT_PRESET, TI_PRESET_ON);
  selftest.fpga = counter_read_raw() != 0;
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
