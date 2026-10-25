/* The loop's one-second reading of the time-interval counter
 * (interval_*, docs/firmware/interval.md's "One reading"), written by
 * hand in place of the decompiler's (docs/firmware/README.md, "Hand-
 * written C"; docs/firmware/modules.md, `applications/pll/reading`).
 * The exported C includes this file after its own, so the types and
 * prototypes it declares are in scope; each function here keeps its
 * generated prototype. */

/* Whether either 1PPS, the GPS's or the internal one, is dead, so that
 * no reading comes. */
static bool pll_reading_pps_dead(void) {
  return gps_pps_dead || internal_pps_dead;
}

/* Arms the next reading, as its interval_arm_mode says (0x451fa). */
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
