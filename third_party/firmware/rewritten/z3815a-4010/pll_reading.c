/* The loop's one-second reading of the time-interval counter
 * (interval_*, docs/firmware/interval.md's "One reading"), this image's
 * own: the 58503A 3704's, which while PLL_CLOCK_FROM_GPS_HELD is set, outside
 * the n3 lock pll stage, takes no reading but waits for the periodic
 * interrupt's tick and gives a valid one (docs/firmware/z3815a-4010.md).
 * Written by hand in place of the decompiler's (docs/firmware/README.md,
 * "Hand-written C"; docs/firmware/modules.md, `applications/pll/
 * reading`).  The exported C includes this file after its own, so the
 * types and prototypes it declares are in scope; each function here keeps
 * its generated prototype.  interval_rearm is the decompiler's. */

/* Whether the counter is read: while PLL_CLOCK_FROM_GPS_HELD, whose meaning is
 * not established, is clear, or in the n3 lock pll stage. */
static bool pll_reading_measured(void) {
  return *PLL_CLOCK_FROM_GPS_HELD == 0 || pll.stage == PLL_STAGE_N3_LOCK_PLL;
}

/* Waits for the counter's reading, or for the periodic interrupt's tick:
 * while a 1PPS is dead, the reading then invalid and
 * interval_reading_replaced set; and while the counter is not read, the
 * reading valid, and its value and *reading 0 if clear_value.  An
 * invalid reading armed with both 1PPS good is happening 'H'.  Then
 * updates the GPS reference's state and the EFC's history. */
static void pll_reading_wait(bool clear_value, float* reading) {
  /* the event the periodic interrupt signals 0x199 ticks after the last
   * 1PPS, and the ticks to wait for it (structures.md) */
  static const uint32_t PPS_TICK_EVENT = 0x200;
  static const uint32_t PPS_TICK_TIMEOUT = 0x2800;

  if (pll_reading_measured() &&
      (!pll_reading_pps_dead() || *PLL_CLOCK_FROM_GPS_HELD != 0)) {
    interval_reading_replaced = 0;
    counter_read(&interval_reading, &counter_configuration);
    if (!interval_reading.valid && !interval_armed_without_pps &&
        !pll_reading_pps_dead()) {
      status_happening(STATUS_TIME_INTERVAL_ERROR);
    }
  } else {
    bool measured = pll_reading_measured();

    interval_tick_wait = 1;
    interval_reading_replaced = measured;
    psos_wait_v(PPS_TICK_EVENT, 0, PPS_TICK_TIMEOUT, 0);
    interval_tick_wait = 0;
    interval_reading.valid = !measured;
    if (!measured && clear_value) {
      interval_reading.value = 0.0;
      if (reading != NULL) {
        *reading = 0.0f;
      }
    }
  }
  gps_reference_update();
  efc_history_update();
}

/* The wait half of interval_next_reading (0x4e7ba). */
void interval_wait_reading(void) { pll_reading_wait(false, NULL); }

/* The next one-second reading, into *reading as a float if given, and
 * its valid flag; then the next armed, as interval_rearm, but that the
 * reading is armed without a 1PPS only while the counter is read
 * (0x4e5bc).  The image has the two halves' code inline. */
uint8_t interval_next_reading(interval_arm_mode arm_mode, float* reading) {
  uint8_t valid;

  pll_reading_wait(true, reading);
  valid = interval_reading.valid;
  if (reading != NULL) {
    *reading = double_to_float(interval_reading.value);
  }
  interval_reading.armed = 1;
  interval_reading.errors = 0;
  if (arm_mode == INTERVAL_ARM_CALIBRATE) {
    counter_calibrate(&interval_reading, &counter_configuration);
  } else if (arm_mode == INTERVAL_ARM_SYNC) {
    sync_request(COUNTER_SYNC_FULL, 1);
  }
  interval_armed_without_pps = pll_reading_measured() && pll_reading_pps_dead();
  counter_arm(&interval_reading, &counter_configuration);
  return valid;
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
