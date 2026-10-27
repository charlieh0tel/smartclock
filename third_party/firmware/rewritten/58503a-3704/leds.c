/* The 58503A's lamps and latch (`devices/leds`): the latch at 0x300000,
 * whose low three bits are the GPS Lock, Holdover and Alarm lamps and
 * whose bits 6 and 7 the front panel's commands move, shadowed in
 * selftest.latch (docs/firmware/58503a-3704.md, "The front panel"); and
 * the board's lamps on the DUART's output port.  Written by hand in
 * place of the decompiler's (docs/firmware/README.md, "Hand-written C");
 * each keeps its generated prototype. */

/* The latch's lamps, which latch_lamps_write writes. */
static const latch_bit LATCH_LAMPS =
    LATCH_ALARM | LATCH_HOLDOVER | LATCH_GPS_LOCK;

/* Writes selftest.latch to the latch, or diag_tmode_data while
 * :DIAGnostic:TMODe:STATe is on; true for the latter. */
static bool latch_output(void) {
  if (diag_tmode_state != 0) {
    lamp_latch = diag_tmode_data;
    return true;
  }
  lamp_latch = selftest.latch;
  return false;
}

/* Sets the bits given in selftest.latch, or clears them, and writes it
 * (0x136c0).  Returns what the image leaves in D0: the bits, or their
 * complement when clearing, or diag_tmode_data in test mode. */
uint8_t latch_bits_set(latch_bit bits, char set) {
  uint8_t result;

  if (set != 0) {
    selftest.latch |= bits;
    result = bits;
  } else {
    selftest.latch &= ~bits;
    result = ~bits;
  }
  return latch_output() ? diag_tmode_data : result;
}

/* Clears the bits of clear in selftest.latch, then sets those of set,
 * and writes it (0x13702).  Returns set, or diag_tmode_data in test
 * mode, as the image leaves D0. */
uint8_t latch_bits_write(latch_bit set, latch_bit clear) {
  selftest.latch = (selftest.latch & ~clear) | set;
  return latch_output() ? diag_tmode_data : set;
}

/* Sets the latch's lamps from lamps' low three bits, the rest of
 * selftest.latch kept, and writes it (0x1373c). */
void latch_lamps_write(latch_bit lamps) {
  selftest.latch =
      (selftest.latch & (lamps | ~LATCH_LAMPS)) | (lamps & LATCH_LAMPS);
  latch_output();
}

/* Writes selftest.latch to the latch again; nothing calls it
 * (0x13778). */
void latch_rewrite(void) { latch_output(); }

/* Resets and sets the board's lamps on the DUART's output port: hmon_task's
 * heartbeat and the power-on self-tests' failures (0x137a4). */
void board_leds_change(board_led reset, board_led set) {
  duart_output_bits(reset, set);
}

/* lamp_bits from :LED:GPSLock?'s, :LED:HOLDover?'s and :LED:ALARm?'s
 * flags, with lamp_blink cleared (0x23b10). */
void lamps_compose(void) {
  uint8_t lamps = pll.led_gps_lock != 0 ? LATCH_GPS_LOCK : 0;

  if (pll.led_holdover != 0) {
    lamps |= LATCH_HOLDOVER;
  }
  if (led_alarm != 0) {
    lamps |= LATCH_ALARM;
  }
  lamp_bits = lamps;
  lamp_blink = 0;
}

/* Asks hmon_task to write the lamps on its next pass (0x23b46). */
void lamps_request(void) { lamps_requested = 1; }

/* Writes lamp_bits to the latch's lamps, those of lamp_blink flipped
 * when blink is set, but not while :DIAGnostic:TMODe:STATe is on or
 * lamp_test runs (0x23b50). */
void lamps_write(uint8_t blink) {
  if (diag_tmode_state != 0 || selftest.lamp_testing != 0) {
    return;
  }
  latch_lamps_write(blink != 0 ? lamp_blink ^ lamp_bits : lamp_bits);
}
