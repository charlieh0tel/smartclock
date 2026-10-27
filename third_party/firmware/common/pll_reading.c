/* The functions of pll_reading.c the images share, the same in each
 * (docs/firmware/rewrite.md, "Shared C"); each image's own
 * rewritten/pll_reading.c describes the module and holds the rest.  Written by
 * hand in place of the decompiler's (docs/firmware/README.md,
 * "Hand-written C"); each keeps its generated prototype. */

/* Whether either 1PPS, the GPS's or the internal one, is dead, so that
 * no reading comes. */
static bool pll_reading_pps_dead(void) {
  return gps_pps_dead || internal_pps_dead;
}

/* Arms the next reading, as its interval_arm_mode says. */
void interval_rearm(interval_arm_mode arm_mode) {
  interval_reading.armed = 1;
  interval_reading.errors = 0;
  if (arm_mode == INTERVAL_ARM_CALIBRATE) {
    counter_calibrate(&interval_reading, &counter_configuration);
  } else if (arm_mode == INTERVAL_ARM_SYNC) {
    sync_request(COUNTER_SYNC_FULL, 1);
  }
  interval_armed_without_pps = pll_reading_pps_dead();
  counter_arm(&interval_reading, &counter_configuration);
}
