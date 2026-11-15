/* pll_reading.c's functions that are this image's own: the rest are
 * common/pll_reading.c's, the same in every image (docs/firmware/rewrite.md,
 * "Shared C").  From the 58503A 3704's rewritten/pll_reading.c: of its
 * functions, those the Z3816A's code carries (identical or relocated by
 * image_diff, or changed with the same instructions), with the static helpers
 * they use, each the same here but where its comment says otherwise
 * (docs/firmware/z3816a-4001.md, "The link check").  Its comments' addresses
 * are the Z3816A's.  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype. */

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
 * periodic interrupt's tick, the reading then invalid; an invalid
 * reading armed with both 1PPS good a time interval error.  Updates the GPS
 * reference's qualification and the EFC's history (0x48b62).  The
 * Z3805A 3543B's but for the reference's update (0x48bde). */
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
  gps_reference_update();
  efc_history_update();
}

/* The next one-second reading, into *reading as a float if given, and
 * its valid flag; then the next armed, as interval_rearm (0x489de).  The
 * image has the two halves' code inline.  The Z3805A 3543B's. */
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
