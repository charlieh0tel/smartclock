/* The Z3815A's lamps and latch (`devices/leds`): the 16-bit latch at
 * 0x300000, output_latch, shadowed in selftest.latch, written whole but
 * diag_tmode_data while :DIAGnostic:TMODe:STATe is on, whose low byte
 * lamp_test walks; and the board's lamps on the DUART's output port.
 * The 58503A's leds.c's latch is a byte (docs/firmware/58503a-3704.md,
 * "The front panel").  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype. */

/* The latch's low byte, which latch_lamps_write replaces and
 * lamp_test walks: the 58503A's replaces its three lamps alone
 * (0x23730). */
static const uint16_t LATCH_LAMPS_Z3815A = 0x00ff;

/* Writes selftest.latch to the latch, or diag_tmode_data while
 * :DIAGnostic:TMODe:STATe is on; true for the latter. */
static bool latch_output(void) {
  if (diag_tmode_state != 0) {
    output_latch = diag_tmode_data;
    return true;
  }
  output_latch = selftest.latch;
  return false;
}

/* Sets the bits given in selftest.latch, or clears them, and writes it
 * (0x236b4).  Returns what the image leaves in D0: the bits, or their
 * complement when clearing, or diag_tmode_data in test mode. */
uint16_t latch_bits_set(uint16_t bits, char set) {
  uint16_t result;

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
 * and writes it; nothing calls it (0x236f6).  Returns set, or
 * diag_tmode_data in test mode, as the image leaves D0's low word. */
uint16_t latch_bits_write(uint16_t set, uint16_t clear) {
  selftest.latch = (selftest.latch & ~clear) | set;
  return latch_output() ? diag_tmode_data : set;
}

/* Sets the latch's low byte from lamps', the high byte of selftest.latch
 * kept, and writes it: lamps_write and lamp_test call it
 * (0x23730). */
void latch_lamps_write(uint16_t lamps) {
  selftest.latch =
      (selftest.latch & ~LATCH_LAMPS_Z3815A) | (lamps & LATCH_LAMPS_Z3815A);
  latch_output();
}

/* Writes selftest.latch to the latch again; nothing calls it
 * (0x23770). */
void latch_rewrite(void) { latch_output(); }

/* Resets and sets the board's lamps on the DUART's output port: hmon_task's
 * heartbeat and the power-on self-tests' failures (0x2379c). */
void board_leds_change(board_led reset, board_led set) {
  duart_output_bits(reset, set);
}

/* Asks hmon_task to write the lamps on its next pass (0x36c10). */
void lamps_request(void) { lamps_requested = 1; }

/* Clears lamp_bits and lamp_blink; nothing calls it, where the 58503A's
 * lamps_compose sets them from the lamps' flags (0x36bfe). */
void lamps_clear(void) {
  lamp_bits = 0;
  lamp_blink = 0;
}

/* Writes lamp_bits to the latch's low byte, those of lamp_blink flipped
 * when blink is set, but not while :DIAGnostic:TMODe:STATe is on or
 * lamp_test runs; nothing calls it, where the 58503A's hmon_task calls
 * its lamps_write (0x36c1a). */
void lamps_write(uint8_t blink) {
  if (diag_tmode_state != 0 || selftest.lamp_testing != 0) {
    return;
  }
  latch_lamps_write(blink != 0 ? lamp_blink ^ lamp_bits : lamp_bits);
}
