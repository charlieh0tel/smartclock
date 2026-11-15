/* From the Z3816A 4001's rewritten/pll_reading.c, the 58503B 1.01.04's own
 * beside common/'s: of its functions, those the 58503B's code carries
 * identical or relocated (docs/firmware/58503b-1.01.04.md, "The link list").
 * Its comments' addresses are the 58503B's, but where they name another
 * image.  Each keeps its generated prototype. */

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

/* Waits for the counter's reading, or, while a 1PPS is dead, for the
 * periodic interrupt's tick (0x4a0dc), the reading then invalid and
 * interval_reading_replaced set; an invalid reading armed with both 1PPS
 * good is happening 'H'.  Updates the GPS reference's state and the EFC's
 * history (0x4a274): the 58503A's, the tick's wait a routine of its own. */
void interval_wait_reading(void) {
  if (!pll_reading_pps_dead()) {
    interval_reading_replaced = 0;
    counter_read(&interval_reading, &counter_configuration);
    if (!interval_reading.valid && !interval_armed_without_pps &&
        !pll_reading_pps_dead()) {
      status_happening(STATUS_TIME_INTERVAL_ERROR);
    }
  } else {
    interval_tick_wait = 1;
    interval_reading_replaced = 1;
    pps_tick_wait();
    interval_tick_wait = 0;
    interval_reading.valid = 0;
  }
  gps_reference_update();
  efc_history_update();
}

/* The next one-second reading, into *reading as a float if given, and
 * its valid flag; then the next armed, as interval_rearm (0x4a0f4).  The
 * image has the two halves' code inline. */
uint8_t interval_next_reading(interval_arm_mode arm_mode, float* reading) {
  interval_wait_reading();
  uint8_t valid = interval_reading.valid;

  if (reading != NULL) {
    *reading = double_to_float(interval_reading.value);
  }
  interval_rearm(arm_mode);
  return valid;
}
