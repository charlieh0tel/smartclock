/* The loop's reading of the time-interval counter, once a pass (interval_*,
 * docs/firmware/z3817a-3752.md, "One reading"), the Z3817A 3752's own: the
 * 58503A 3704's rewritten/pll_reading.c, but that with the pulse dead it
 * waits two of the periodic interrupt's ticks in PP2S (interval_wait_tick,
 * "The pulse"), and that no arm calibrates an interpolator.  Each keeps its
 * generated prototype. */

/* Waits for the event the periodic interrupt signals 0x199 ticks after the
 * internal 1 PPS (structures.md), up to 0x2800 ticks; in PP2S, a pass two
 * seconds, a second time if the first came (0x4a2f6). */
void interval_wait_tick(void) {
  static const uint32_t PPS_TICK_EVENT = 0x200;
  static const uint32_t PPS_TICK_TIMEOUT = 0x2800;
  const uint8_t result = psos_wait_v(PPS_TICK_EVENT, 0, PPS_TICK_TIMEOUT, 0);

  if (pll.pulse_mode == REFERENCE_PULSE_PP2S && result == 0) {
    psos_wait_v(PPS_TICK_EVENT, 0, PPS_TICK_TIMEOUT, 0);
  }
}

/* Waits for the counter's reading, or, while a 1PPS is dead, for the
 * periodic interrupt's tick, the reading then invalid and
 * interval_reading_replaced set; an invalid reading armed with both 1PPS
 * good is happening 'H'.  Updates the GPS reference's state and the EFC's
 * history (0x4a490). */
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
    interval_wait_tick();
    interval_tick_wait = 0;
    interval_reading.valid = 0;
  }
  gps_reference_update();
  efc_history_update();
}

/* Arms the next reading, syncing the counter to the 1PPS first for
 * INTERVAL_ARM_SYNC; INTERVAL_ARM_CALIBRATE arms plainly, there being no
 * interpolator (0x4a43c) (0x4a42e). */
void interval_rearm(interval_arm_mode arm_mode) {
  interval_reading.armed = 1;
  interval_reading.errors = 0;
  if (arm_mode == INTERVAL_ARM_SYNC) {
    sync_request(COUNTER_SYNC_FULL, 1);
  }
  interval_armed_without_pps = pll_reading_pps_dead();
  counter_arm(&interval_reading, &counter_configuration);
}

/* The next reading, into *reading as a float if given, and its valid flag;
 * then the next armed, as interval_rearm (0x4a338).  The image has the two
 * halves' code inline. */
uint8_t interval_next_reading(interval_arm_mode arm_mode, float* reading) {
  interval_wait_reading();
  const uint8_t valid = interval_reading.valid;

  if (reading != NULL) {
    *reading = double_to_float(interval_reading.value);
  }
  interval_rearm(arm_mode);
  return valid;
}
