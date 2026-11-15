/* From the 58503A 3704's rewritten/leds.c, the Z3816A 4001's own beside
 * common/'s: of its functions, those the Z3816A's code carries (identical or
 * relocated by image_diff, or changed with the same instructions), with the
 * static helpers they use, each the same here but where its comment says
 * otherwise (docs/firmware/z3816a-4001.md, "The link check").  Its comments'
 * addresses are the Z3816A's.  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype. */

/* Resets and sets the board's lamps on the DUART's output port: hmon_task's
 * heartbeat and the power-on self-tests' failures (0x235b8). */
void board_leds_change(board_led reset, board_led set) {
  duart_output_bits(reset, set);
}

/* The byte of output_latch the primary writes, its high byte, which
 * lamp_bits and diag_tmode_data's low byte go to. */
static volatile uint8_t* const LEDS_LATCH_BYTE =
    (volatile uint8_t*)&output_latch;

/* lamp_bits from latch_bits and :LED:ALARm?'s, :LED:HOLDover?'s and
 * :LED:GPSLock?'s flags, with lamp_blink cleared; the 58503A's starts
 * from none and lights GPS Lock on bit 0 (0x33d0a). */
void lamps_compose(void) {
  uint8_t lamps = latch_bits;

  if (led_alarm != 0) {
    lamps |= LAMP_ALARM;
  }
  if (pll.led_holdover != 0) {
    lamps |= LAMP_HOLDOVER;
  }
  if (pll.led_gps_lock != 0) {
    lamps |= LAMP_GPS_LOCK;
  }
  lamp_bits = lamps;
  lamp_blink = 0;
}

/* Sets the bits given in latch_bits and lamp_bits, or clears them, and
 * writes lamp_bits to the latch; nothing calls it (0x33d44). */
void latch_bits_set(uint8_t bits, char set) {
  if (set != 0) {
    latch_bits |= bits;
    lamp_bits |= bits;
  } else {
    latch_bits &= ~bits;
    lamp_bits &= ~bits;
  }
  *LEDS_LATCH_BYTE = lamp_bits;
}

/* Asks hmon_task to write the lamps on its next pass (0x33d74). */
void lamps_request(void) { lamps_requested = 1; }

/* Writes lamp_bits to the latch, those of lamp_blink flipped when blink
 * is set, but not while lamp_test runs; while :DIAGnostic:TMODe:STATe is
 * on, diag_tmode_data's low byte in its place (0x33d7e). */
void lamps_write(char blink) {
  if (diag_tmode_state != 0) {
    *LEDS_LATCH_BYTE = (uint8_t)diag_tmode_data;
  } else if (selftest.lamp_testing == 0) {
    *LEDS_LATCH_BYTE = blink != 0 ? lamp_blink ^ lamp_bits : lamp_bits;
  }
}

/* Lights every lamp, then none, then each of the latch byte's bits in
 * turn, writing the latch only while :DIAGnostic:TMODe:STATe is off,
 * then asks hmon_task to restore them; lamps_write leaves the latch
 * alone meanwhile (0x286d2).  The 58503A 3704's lamp_test, in its
 * selftest.c, but over the latch byte's eight bits, as the Z3805A
 * 3543B's, and held off by the test mode. */
void lamp_test(void) {
  static const uint8_t ALL_LAMPS = 0xff;
  static const uint16_t PAST_LAST_LAMP = 0x100;
  /* How long each step lights its lamps. */
  static const uint32_t STEP_TICKS = 307;

  selftest.lamp_testing = 1;
  if (diag_tmode_state == 0) {
    *LEDS_LATCH_BYTE = ALL_LAMPS;
  }
  psos_pause(STEP_TICKS);
  if (diag_tmode_state == 0) {
    *LEDS_LATCH_BYTE = 0;
  }
  psos_pause(STEP_TICKS);
  watchdog_pll++;
  for (uint16_t lamp = 1; lamp < PAST_LAST_LAMP; lamp <<= 1) {
    if (diag_tmode_state == 0) {
      *LEDS_LATCH_BYTE = (uint8_t)lamp;
    }
    psos_pause(STEP_TICKS);
    watchdog_pll++;
  }
  selftest.lamp_testing = 0;
  lamps_request();
}
