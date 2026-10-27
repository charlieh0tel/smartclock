/* The functions of selftest.c the images share, the same in each
 * (docs/firmware/rewrite.md, "Shared C"); each image's own
 * rewritten/selftest.c describes the module and holds the rest.  Written by
 * hand in place of the decompiler's (docs/firmware/README.md,
 * "Hand-written C"); each keeps its generated prototype. */

/* selftest_report_fail's line of text, the original's 80 bytes. */
#define SELFTEST_REPORT_LENGTH 80

/* qspi_hex_dac_test's words, the hex DAC's 36 bits, and its patterns. */
#define SELFTEST_HEX_DAC_WORDS 3

#define SELFTEST_HEX_DAC_PATTERNS 2

/* Pauses so many ticks, one at a time, stopping if the self-test is
 * aborted. */
void selftest_delay(uint32_t ticks) {
  for (uint32_t tick = 0; tick < ticks; tick++) {
    if (selftest_aborted()) {
      return;
    }
    psos_pause(1);
  }
}

/* Writes value's low digits bits as text, most significant first. */
void format_binary(uint32_t value, char* text, int32_t digits) {
  for (int32_t place = digits - 1; place >= 0; place--) {
    text[place] = '0' + (value & 1);
    value >>= 1;
  }
  text[digits] = '\0';
}

/* ram_pattern_test with interrupts held off, unless the self-test is
 * aborted. */
void ram_pattern_test_locked(void) {
  if (selftest_aborted()) {
    return;
  }
  uint16_t sr = set_interrupt_mask(CPU_IPL_7);
  ram_pattern_test();
  set_interrupt_mask(sr);
}

/* Shifts three 12-bit words, the first most significant, right one bit
 * as a 36-bit number; returns the bit carried from the first word into
 * the second. */
uint16_t shift_right_36(uint16_t* words) {
  static const uint16_t WORD_MASK = 0xfff;
  static const uint16_t WORD_TOP_BIT = 0x800;

  words[2] &= WORD_MASK;
  words[1] &= WORD_MASK;
  words[0] &= WORD_MASK;
  words[2] >>= 1;
  if (words[1] & 1) {
    words[2] |= WORD_TOP_BIT;
  }
  words[1] >>= 1;
  uint16_t carry = words[0] & 1;
  if (carry) {
    words[1] |= WORD_TOP_BIT;
  }
  words[0] >>= 1;
  return carry;
}

/* Sends a pattern to the hex DAC's three words, then sends it again
 * while reading back the first, which returns a bit late; 1 when both
 * patterns read back; restores the DACs. */
uint8_t qspi_hex_dac_test(void) {
  static const uint16_t PATTERN[SELFTEST_HEX_DAC_PATTERNS]
                               [SELFTEST_HEX_DAC_WORDS] = {
                                   {0x555, 0x123, 0x456},
                                   {0x2aa, 0xfed, 0xcba},
                               };
  uint16_t sent[SELFTEST_HEX_DAC_WORDS];
  uint16_t received[SELFTEST_HEX_DAC_WORDS];
  qspi_buffers buffers = {.transmit = sent, .receive = received};
  uint8_t passed = 1;

  for (uint32_t pattern = 0; pattern < SELFTEST_HEX_DAC_PATTERNS; pattern++) {
    for (uint32_t word = 0; word < SELFTEST_HEX_DAC_WORDS; word++) {
      sent[word] = PATTERN[pattern][word];
    }
    qspi_send(QSPI_HDAC, sent, SELFTEST_HEX_DAC_WORDS);
    qspi_transfer(QSPI_HDAC, &buffers, SELFTEST_HEX_DAC_WORDS);
    shift_right_36(received);
    for (uint32_t word = 0; word < SELFTEST_HEX_DAC_WORDS; word++) {
      if (sent[word] != received[word]) {
        passed = 0;
      }
    }
  }
  hdac_update();
  return passed;
}

/* Writes 0x5555 and 0xaaaa to the EFC DAC and reads each back; true
 * when both agree. */
bool qspi_efc_test(void) {
  static const uint16_t PATTERN_1 = 0x5555;
  static const uint16_t PATTERN_2 = 0xaaaa;
  uint16_t sent = PATTERN_1;
  uint16_t received;
  qspi_buffers buffers = {.transmit = &sent, .receive = &received};

  qspi_send(QSPI_EFC, &sent, 1);
  qspi_transfer(QSPI_EFC, &buffers, 1);
  bool first = sent == received;
  sent = PATTERN_2;
  qspi_send(QSPI_EFC, &sent, 1);
  qspi_transfer(QSPI_EFC, &buffers, 1);
  return sent == received && first;
}

/* Prints "NAME FAIL" when failed is nonzero and the self-test was not
 * aborted, without the line's end when continued. */
void selftest_report_fail(int32_t failed, char* name, char continued) {
  char text[SELFTEST_REPORT_LENGTH];

  if (failed == 0 || selftest_aborted()) {
    return;
  }
  sprintf(text, "%s %s", name, "FAIL");
  debug_printf(continued ? "%s" : "%s\r\n", text);
}

/* Runs the lamp test unless the self-test has been aborted, entry 1. */
void selftest_lamps(char_slot run) {
  if (run.value == 1 && !selftest_aborted()) {
    lamp_test();
  }
}

/* Entry 12, IMRC: on the second pass runs interpolator_selftest. */
void selftest_interpolator_calibration(char_slot run) {
  if (run.value == 1) {
    interpolator_selftest();
  }
}

/* Entry 13, QMRC: on the second pass runs interpolator_sample_test. */
void selftest_interpolator_sample(char_slot run) {
  if (run.value == 1) {
    interpolator_sample_test();
  }
}

/* The power-up checks, selftest_print_failure's table at
 * selftest_print_failure: each runs its test, lighting a lamp on a failure, and
 * returns its word. */

/* "DISP": runs the lamp test; 0. */
uint32_t lamp_test_command(void) {
  lamp_test();
  return 0;
}

/* Sums a ROM's two byte lanes, the even bytes and the odd, over count
 * less 1 longs (the low word of count less 2 counts dbf, rom_lane_checksum);
 * the sums' high bytes, even then odd, then their low bytes.  Called by
 * nothing. */
uint32_t rom_lane_checksum(uint8_t* rom, uint32_t count) {
  static const uint16_t HIGH_BYTE = 0xff00;
  static const uint16_t LOW_BYTE = 0x00ff;
  uint16_t even = 0;
  uint16_t odd = 0;
  uint16_t remaining = (uint16_t)(count - 2);

  do {
    even += rom[0] + rom[2];
    odd += rom[1] + rom[3];
    rom += 4;
  } while (remaining-- != 0);
  return (uint32_t)(even & HIGH_BYTE) << (2 * CPU_BYTE_BITS) |
         (uint32_t)(odd & HIGH_BYTE) << CPU_BYTE_BITS |
         (uint32_t)(even & LOW_BYTE) << CPU_BYTE_BITS | (odd & LOW_BYTE);
}
