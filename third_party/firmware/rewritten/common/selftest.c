/* The functions of selftest.c the images share, the same in each
 * (docs/firmware/rewrite.md, "Shared C"); each image's own
 * rewritten/selftest.c describes the module and holds the rest.  Written by
 * hand in place of the decompiler's (docs/firmware/README.md,
 * "Hand-written C"); each keeps its generated prototype. */

/* selftest_report_fail's line of text, the original's 80 bytes. */
#define SELFTEST_REPORT_LENGTH 80

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
