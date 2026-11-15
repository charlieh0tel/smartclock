/* From the 58503A 3704's rewritten/leds.c and the Z3816A 4001's
 * rewritten/leds.c, the 58503B 1.01.04's own beside common/'s: of its
 * functions, those the 58503B's code carries identical or relocated
 * (docs/firmware/58503b-1.01.04.md, "The link list").  Its comments'
 * addresses are the 58503B's, but where they name another image.  Each keeps
 * its generated prototype. */

/* Asks hmon_task to write the lamps on its next pass (0x33432). */
void lamps_request(void) { lamps_requested = 1; }

/* lamp_bits from :LED:GPSLock?'s, :LED:HOLDover?'s and :LED:ALARm?'s
 * flags, with lamp_blink cleared (0x333fc). */
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

/* Writes lamp_bits to the latch's lamps, those of lamp_blink flipped
 * when blink is set, but not while :DIAGnostic:TMODe:STATe is on or
 * lamp_test runs (0x3343c). */
void lamps_write(uint8_t blink) {
  if (diag_tmode_state != 0 || selftest.lamp_testing != 0) {
    return;
  }
  latch_lamps_write(blink != 0 ? lamp_blink ^ lamp_bits : lamp_bits);
}

/* latch_bits_write of its arguments' heartbeat and bit 4 alone, the
 * latch's bits the board's LEDs are (0x2379e). */
void board_leds_change(latch_bit set, latch_bit clear) {
  static const latch_bit BOARD_LEDS = LATCH_BIT_4 | LATCH_HEARTBEAT;

  latch_bits_write(set & BOARD_LEDS, clear & BOARD_LEDS);
}

/* The latch's lamps, which latch_lamps_write writes. */
static const latch_bit LATCH_LAMPS =
    LATCH_ALARM | LATCH_HOLDOVER | LATCH_GPS_LOCK;

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

/* Sets the bits given in selftest.latch, or clears them, and writes it,
 * the 58503A's (0x236ba).  Returns what the image leaves in D0: the
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
 * and writes it, the 58503A's (0x236fc).  Returns set, or
 * diag_tmode_data in test mode, as the image leaves D0. */
uint8_t latch_bits_write(latch_bit set, latch_bit clear) {
  selftest.latch = (selftest.latch & ~clear) | set;
  return latch_output() ? diag_tmode_data : set;
}

/* Sets the latch's lamps from lamps' low three bits, the rest of
 * selftest.latch kept, and writes it, the 58503A's (0x23736). */
void latch_lamps_write(latch_bit lamps) {
  selftest.latch =
      (selftest.latch & (lamps | ~LATCH_LAMPS)) | (lamps & LATCH_LAMPS);
  latch_output();
}

/* Writes selftest.latch to the latch again; nothing calls it
 * (0x23772). */
void latch_rewrite(void) { latch_output(); }

/* Writes a value's digits in a base, 0 to 9 then A to Z, at text: width
 * of them, zeros leading, for a width of 1 to 32, else as many as it
 * takes, at most 32; exception_name's only caller (0x237b6).  The
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

/* A lamp's state: 2 blinking, while lamp_blink has a bit of lamps, else
 * 1 lit, while lamp_bits has one, else 0; nothing calls it (0x33474). */
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
