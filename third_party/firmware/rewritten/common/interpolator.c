/* The interpolator's routines that the images with one share, the same in
 * each image that includes this file (docs/firmware/rewrite.md, "Shared
 * C", "A file for some images"): its calibration's defaults, a reference's
 * reading with its DAC at a value, and the self-tests' two entries that
 * run its tests.  An image without the interpolator leaves the file out.
 * Written by hand in place of the decompiler's (docs/firmware/README.md,
 * "Hand-written C"); each keeps its generated prototype. */

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

/* Entry 12, IMRC: on the second pass runs interpolator_selftest. */
void selftest_interpolator_calibration(char_slot run) {
  if (run.value == 1) {
    interpolator_selftest();
  }
}

/* Entry 13, QMRC: on the second pass runs interpolator_sample_test. */
void selftest_interpolator_sample(char_slot run) {
  if (run.value == 1) {
    interpolator_sample_test();
  }
}
