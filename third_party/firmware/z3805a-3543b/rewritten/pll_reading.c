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

/* Waits for the counter's reading, or, while a 1PPS is dead, for the
 * periodic interrupt's tick, the reading then invalid; an invalid
 * reading armed with both 1PPS good is happening 'H'.  Updates the
 * EFC's history (0x4513c). */
void interval_wait_reading(void) {
  /* the event the periodic interrupt signals 0x199 ticks after the last
   * 1PPS, and the ticks to wait for it (structures.md) */
  static const uint32_t PPS_TICK_EVENT = 0x200;
  static const uint32_t PPS_TICK_TIMEOUT = 0x2800;

  if (!pll_reading_pps_dead()) {
    counter_read(&interval_reading, &counter_configuration);
    if (!interval_reading.valid && !interval_armed_without_pps &&
        !pll_reading_pps_dead()) {
      status_happening(STATUS_TIME_INTERVAL_ERROR);
    }
  } else {
    interval_tick_wait = 1;
    psos_wait_v(PPS_TICK_EVENT, 0, PPS_TICK_TIMEOUT, 0);
    interval_tick_wait = 0;
    interval_reading.valid = 0;
  }
  efc_history_update();
}

/* Arms the next reading, as its interval_arm_mode says (0x450c4). */
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

/* The next one-second reading, into *reading as a float if given, and
 * its valid flag; then the next armed, as interval_rearm (0x44fbc).  The
 * image has the two halves' code inline. */
uint8_t interval_next_reading(interval_arm_mode arm_mode, float* reading) {
  uint8_t valid;

  interval_wait_reading();
  valid = interval_reading.valid;
  if (reading != NULL) {
    *reading = double_to_float(interval_reading.value);
  }
  interval_rearm(arm_mode);
  return valid;
}
