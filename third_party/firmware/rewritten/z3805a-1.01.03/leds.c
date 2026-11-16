/* From the 58503B 1.01.04's rewritten/leds.c and the Z3815A 4010's
 * rewritten/leds.c, the Z3805A 1.01.03's own beside common/'s: of its
 * functions, those the Z3805A 1.01.03's code carries identical or
 * relocated (docs/firmware/z3805a-1.01.03.md, "The link list").  Its
 * comments' addresses are the Z3805A 1.01.03's, but where they name
 * another image.  Each keeps its generated prototype. */

/* Writes selftest.latch to the latch, or diag_tmode_data while
 * :DIAGnostic:TMODe:STATe is on; true for the latter.  The primary
 * writes the byte at output_latch's address, as the 58503A's does its
 * byte latch, where the boot code writes it as a word (0x5b4, 0x5ca) and
 * the Z3815A's primary does too. */
static bool latch_output(void) {
  volatile uint8_t* const latch = (volatile uint8_t*)&output_latch;

  if (diag_tmode_state != 0) {
    *latch = diag_tmode_data;
    return true;
  }
  *latch = selftest.latch;
  return false;
}

/* Asks hmon_task to write the lamps on its next pass (0x24f50). */
void lamps_request(void) { lamps_requested = 1; }

/* Writes lamp_bits to the latch's lamps, those of lamp_blink flipped
 * when blink is set, but not while :DIAGnostic:TMODe:STATe is on or
 * lamp_test runs (0x24f5a). */
void lamps_write(uint8_t blink) {
  if (diag_tmode_state != 0 || selftest.lamp_testing != 0) {
    return;
  }
  latch_lamps_write(blink != 0 ? lamp_blink ^ lamp_bits : lamp_bits);
}

/* Sets the bits given in selftest.latch, or clears them, and writes it,
 * the 58503A's (0x13610).  Returns what the image leaves in D0: the
 * bits, or their complement when clearing, or diag_tmode_data in test
 * mode. */
latch_bit latch_bits_set(latch_bit bits, char set) {
  latch_bit result;

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
 * and writes it, the 58503A's (0x13652).  Returns set, or
 * diag_tmode_data in test mode, as the image leaves D0. */
uint8_t latch_bits_write(latch_bit set, latch_bit clear) {
  selftest.latch = (selftest.latch & ~clear) | set;
  return latch_output() ? diag_tmode_data : set;
}

/* Writes selftest.latch to the latch again; nothing calls it
 * (0x136c8). */
void latch_rewrite(void) { latch_output(); }

/* Writes a value's digits in a base, 0 to 9 then A to Z, at text: width
 * of them, zeros leading, for a width of 1 to 32, else as many as it
 * takes, at most 32; exception_name's only caller (0x13706).  The
 * digit's test against ten is a signed byte's, as the image's is. */
void unsigned_to_digits(char* text, uint32_t value, uint16_t base,
                        uint16_t width) {
  static const uint16_t DIGITS_MOST = 32;
  static const int8_t DECIMAL_DIGITS = 10;
  char digits[DIGITS_MOST + 1];
  const int16_t count =
      width == 0 || width > DIGITS_MOST ? DIGITS_MOST : (int16_t)width;
  int16_t first = 0;

  digits[count] = 0;
  for (int16_t index = count - 1; index >= 0; index--) {
    const int8_t digit = value % base;

    digits[index] =
        digit + (digit < DECIMAL_DIGITS ? '0' : 'A' - DECIMAL_DIGITS);
    value /= base;
    if (value == 0 && width == 0) {
      first = index;
      break;
    }
  }
  const char* from = &digits[first];

  while ((*text++ = *from++) != 0) {
  }
}

/* Resets and sets the board's lamps on the DUART's output port: hmon_task's
 * heartbeat and the power-on self-tests' failures (0x136f4). */
void board_leds_change(board_led reset, board_led set) {
  duart_output_bits(reset, set);
}

/* Sets the latch's lamps from lamps' five bits, the rest of
 * selftest.latch kept, and writes it (0x1368c). */
void latch_lamps_write(latch_bit lamps) {
  selftest.latch =
      (selftest.latch & (lamps | ~LATCH_LAMPS)) | (lamps & LATCH_LAMPS);
  latch_output();
}

/* lamp_bits from five flags, led_enabled, led_active and led_alarm and
 * the loop's GPS lock and holdover, with lamp_blink cleared (0x24f02). */
void lamps_compose(void) {
  uint8_t lamps = led_enabled != 0 ? LATCH_ENABLED : 0;

  if (led_active != 0) {
    lamps |= LATCH_ACTIVE;
  }
  if (led_alarm != 0) {
    lamps |= LATCH_ALARM;
  }
  if (pll.led_gps_lock != 0) {
    lamps |= LATCH_GPS_LOCK;
  }
  if (pll.led_holdover != 0) {
    lamps |= LATCH_HOLDOVER;
  }
  lamp_bits = lamps;
  lamp_blink = 0;
}

/* A lamp's state: 2 blinking, while lamp_blink has a bit of lamps, else
 * 1 lit, while lamp_bits has one, else 0; nothing calls it (0x24f92). */
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
