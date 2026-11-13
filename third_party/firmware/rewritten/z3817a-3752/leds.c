/* From the Z3815A 4010's rewritten/leds.c, the Z3817A 3752's own beside
 * common/'s: of its functions, those the Z3817A's code carries identical or
 * relocated (docs/firmware/z3817a-3752.md, "The function check"), and the
 * lamps' writer, composer and state, this image's own, read beside the
 * 58503A 3704's (docs/firmware/z3817a-3752.md, "The lamps").  Its comments'
 * addresses are the Z3817A's, but where they name another image.  Each
 * keeps its generated prototype. */

/* Resets and sets the board's lamps on the DUART's output port: hmon_task's
 * heartbeat and the power-on self-tests' failures (0x23730). */
void board_leds_change(board_led reset, board_led set) {
  duart_output_bits(reset, set);
}

/* The latch's high byte, at output_latch's address, which the
 * primary's latch writers write alone, a byte, as the 58503A's write its
 * 8-bit latch (0x2367e, 0x236b8, 0x2371c), where the installer writes
 * the word (installer_leds.c). */
static volatile latch_bit* const latch_high_byte = (latch_bit*)&output_latch;

/* Writes selftest.latch to the latch, or diag_tmode_data while
 * :DIAGnostic:TMODe:STATe is on; true for the latter. */
static bool latch_output(void) {
  if (diag_tmode_state != 0) {
    *latch_high_byte = diag_tmode_data;
    return true;
  }
  *latch_high_byte = selftest.latch;
  return false;
}

/* Sets the bits given in selftest.latch, or clears them, and writes it
 * (0x2364c).  Returns what the image leaves in D0: the bits, or their
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
 * and writes it (0x2368e).  Returns set, or diag_tmode_data in test
 * mode, as the image leaves D0. */
uint8_t latch_bits_write(latch_bit set, latch_bit clear) {
  selftest.latch = (selftest.latch & ~clear) | set;
  return latch_output() ? diag_tmode_data : set;
}

/* Writes selftest.latch to the latch again; nothing calls it
 * (0x23704). */
void latch_rewrite(void) { latch_output(); }

/* The latch's two lamps (docs/firmware/z3817a-3752.md, "The lamps"). */
static const latch_bit LEDS_LAMPS = LATCH_LAMP_1 | LATCH_LAMP_2;

/* Sets the latch's two lamps, bits 1 and 2, from lamps, the rest of
 * selftest.latch kept, and writes it (0x236c8); the 58503A's sets bits 0
 * to 2. */
void latch_lamps_write(latch_bit lamps) {
  selftest.latch =
      (selftest.latch & (lamps | ~LEDS_LAMPS)) | (lamps & LEDS_LAMPS);
  latch_output();
}

/* Composes lamp_bits and lamp_blink, first stepping lamp_phase, 0 to 7,
 * when step is set (0x3421c), by the first that holds: led_alarm lights
 * lamp 2; with :PTESt off, the lock byte lamp 1, the holdover byte lamp 1
 * in phases 0 to 6 and lamp 2 in phase 7, and neither lamp 1 blinking;
 * :PTESt passed lights lamp 1 in phases 0 and 2, failed lamp 2 in phase 0,
 * and armed or running lamp 1 in phases 0 and 4 and lamp 2 in phase 2.
 * The 58503A 3704's lights its three lamps from the lock, holdover and
 * alarm bytes. */
void lamps_compose(uint8_t step) {
  static const uint8_t PHASE_LAST = 7;
  static const uint8_t PHASE_HOLDOVER_ALARM = 7;
  static const uint8_t PHASE_SECOND = 2;
  static const uint8_t PHASE_FOURTH = 4;
  const ptest_state ptest = ptest_state_get();
  latch_bit lamps = 0;
  latch_bit blink = 0;

  if (step != 0 && ++lamp_phase > PHASE_LAST) {
    lamp_phase = 0;
  }
  if (led_alarm != 0) {
    lamps |= LATCH_LAMP_2;
  } else if (ptest == PTEST_STATE_OFF) {
    if (pll.led_gps_lock != 0) {
      lamps |= LATCH_LAMP_1;
    } else if (pll.led_holdover != 0) {
      lamps |= lamp_phase < PHASE_HOLDOVER_ALARM ? LATCH_LAMP_1 : LATCH_LAMP_2;
    } else {
      blink |= LATCH_LAMP_1;
    }
  } else if (ptest == PTEST_STATE_PASSED) {
    if (lamp_phase == 0 || lamp_phase == PHASE_SECOND) {
      lamps |= LATCH_LAMP_1;
    }
  } else if (ptest == PTEST_STATE_FAILED) {
    if (lamp_phase == 0) {
      lamps |= LATCH_LAMP_2;
    }
  } else if (lamp_phase == 0 || lamp_phase == PHASE_FOURTH) {
    lamps |= LATCH_LAMP_1;
  } else if (lamp_phase == PHASE_SECOND) {
    lamps |= LATCH_LAMP_2;
  }
  lamp_bits = lamps;
  lamp_blink = blink;
}

/* Asks hmon_task to write the lamps on its next pass (0x342c0). */
void lamps_request(void) { lamps_requested = 1; }

/* Writes lamp_bits to the latch's lamps, those of lamp_blink flipped
 * when blink is set, but not while :DIAGnostic:TMODe:STATe is on or
 * lamp_test runs (0x342ca). */
void lamps_write(uint8_t blink) {
  if (diag_tmode_state != 0 || selftest.lamp_testing != 0) {
    return;
  }
  latch_lamps_write(blink != 0 ? lamp_blink ^ lamp_bits : lamp_bits);
}

/* A lamp's state: 2 blinking, while lamp_blink has a bit of lamps, else
 * 1 lit, while lamp_bits has one, else 0; nothing calls it (0x34302). */
uint8_t lamp_state(uint16_t lamps) {
  static const uint8_t LAMP_OFF = 0;
  static const uint8_t LAMP_LIT = 1;
  static const uint8_t LAMP_BLINKING = 2;

  if ((lamp_blink & lamps) != 0) {
    return LAMP_BLINKING;
  }
  if ((lamp_bits & lamps) != 0) {
    return LAMP_LIT;
  }
  return LAMP_OFF;
}
