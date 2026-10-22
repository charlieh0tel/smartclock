/* The self-tests, *TST? and :DIAGnostic:TEST? (097-59551-02,
 * "Assessing Receiver Health"), written by hand in place of the
 * decompiler's (docs/firmware/README.md, "Hand-written C";
 * docs/firmware/modules.md, `applications/selftest`).  The table at
 * 0x18458 runs test n, a selftest_test_number, and the list at 0x17fe0
 * all of them; each leaves its failures in its selftest_results word
 * (docs/firmware/structures.md, "The self-tests' results").
 * selftest_print_failure's table at 0x186b0 runs the *_with_lamp checks
 * at power-up.  The exported C includes this file after its own, so the
 * types and prototypes it declares are in scope; each function here
 * keeps its generated prototype. */

/* The RAM test's lines, D15-D0 and A18-A1, as selftest_ram prints
 * them. */
#define SELFTEST_RAM_DATA_LINES 16
#define SELFTEST_RAM_ADDRESS_LINES 18

/* selftest_ram's line of text, the original's 60 bytes. */
#define SELFTEST_LINE_LENGTH 60

/* qspi_hex_dac_test's words, the hex DAC's 36 bits, and its patterns. */
#define SELFTEST_HEX_DAC_WORDS 3
#define SELFTEST_HEX_DAC_PATTERNS 2

/* Clears selftest.running and the abort flag, and marks the self-tests
 * passed (0x18404). */
void selftest_init(void) {
  selftest.running = 0;
  selftest.abort = 0;
  selftest.passed = 1;
}

/* Clears the abort flag before a test (0x182c4). */
void selftest_begin(void) { selftest.abort = 0; }

/* Clears the abort flag after a test (0x182cc). */
void selftest_end(void) { selftest.abort = 0; }

/* Whether the running test has been asked to stop (0x183ce). */
bool selftest_aborted(void) { return selftest.abort != 0; }

/* Whether a test may report: on its first pass, run 0, or when it was
 * not aborted (0x182d4). */
bool selftest_may_report(char run) { return run == 0 || selftest.abort == 0; }

/* Pauses so many ticks, one at a time, stopping if the self-test is
 * aborted (0x183de). */
void selftest_delay(uint32_t ticks) {
  for (uint32_t tick = 0; tick < ticks; tick++) {
    if (selftest_aborted()) {
      return;
    }
    psos_pause(1);
  }
}

/* If a test is running, aborts it and waits until it stops; whether one
 * was (0x1841a). */
uint8_t selftest_abort_wait(void) {
  if (selftest.running == 0) {
    return 0;
  }
  selftest.abort = 1;
  while (selftest.running != 0) {
    psos_pause(1);
  }
  return 1;
}

/* Writes value's low digits bits as text, most significant first
 * (0x18396). */
void format_binary(uint32_t value, char* text, int32_t digits) {
  for (int32_t place = digits - 1; place >= 0; place--) {
    text[place] = '0' + (value & 1);
    value >>= 1;
  }
  text[digits] = '\0';
}

/* Self-test 9's count: fails unless one to three internal 1PPS came in
 * 1024 ticks (0x184a4). */
void internal_pps_test(void) {
  static const uint32_t TICKS = 1024;
  static const int8_t FEWEST = 1;
  static const int8_t MOST = 3;

  selftest.internal_reference = 0;
  uint8_t before = internal_pps_count;
  psos_pause(TICKS);
  /* the count wraps; the difference is signed (0x184d6) */
  int8_t count = (int8_t)(internal_pps_count - before);
  if (count < FEWEST || count > MOST) {
    selftest.internal_reference |= SELFTEST_FAILED;
  }
}

/* Lights every lamp, then none, then each in turn, then restores them
 * (0x184ee). */
void lamp_test(void) {
  static const uint8_t ALL_LAMPS = 0xff;
  static const uint16_t PAST_LAST_LAMP = 0x100;
  /* how long each step lights its lamps */
  static const uint32_t STEP_TICKS = 307;

  lamp_latch = ALL_LAMPS;
  psos_pause(STEP_TICKS);
  lamp_latch = 0;
  psos_pause(STEP_TICKS);
  watchdog_pll++;
  for (uint16_t lamp = 1; lamp < PAST_LAST_LAMP; lamp <<= 1) {
    lamp_latch = (uint8_t)lamp;
    psos_pause(STEP_TICKS);
    watchdog_pll++;
  }
  leds_update();
}

/* Checks the supplies: 5 V within 4.5 to 5.5, +15 V within 14 to 16
 * and -15 V within -16 to -14 (0x18546). */
void supplies_test(void) {
  static const float LEAST_5V = 4.5f;
  static const float MOST_5V = 5.5f;
  static const float LEAST_P15V = 14.0f;
  static const float MOST_P15V = 16.0f;
  static const float LEAST_M15V = -16.0f;
  static const float MOST_M15V = -14.0f;

  selftest.power = 0;
  float volts = read_adc_5v();
  if (float_compare(volts, LEAST_5V) < 0 || float_compare(volts, MOST_5V) > 0) {
    selftest.power |= SELFTEST_POWER_5V;
  }
  volts = read_adc_p15v();
  if (float_compare(volts, LEAST_P15V) < 0 ||
      float_compare(volts, MOST_P15V) > 0) {
    selftest.power |= SELFTEST_POWER_P15V;
  }
  volts = read_adc_m15v();
  if (float_compare(volts, LEAST_M15V) < 0 ||
      float_compare(volts, MOST_M15V) > 0) {
    selftest.power |= SELFTEST_POWER_M15V;
  }
}

/* ram_pattern_test with interrupts held off, unless the self-test is
 * aborted (0x18c48). */
void ram_pattern_test_locked(void) {
  if (selftest_aborted()) {
    return;
  }
  uint16_t sr = set_interrupt_mask(CPU_IPL_7);
  ram_pattern_test();
  set_interrupt_mask(sr);
}

/* The EEPROM test: whether slot nv_slot's record is bad (0x18c74). */
void selftest_nv_slot(void) {
  selftest.eeprom = 0;
  selftest.eeprom = nv_slot_valid();
}

/* Shifts three 12-bit words, the first most significant, right one bit
 * as a 36-bit number; returns the bit carried from the first word into
 * the second (0x1876c). */
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
 * patterns read back; restores the DACs (0x187aa). */
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

/* Reads the ADC once and checks its reply: 1 when bit 10 is clear,
 * bits 4 and 3 repeat bits 0 and 1, and the byte in bits 9 to 2 is
 * neither 0 nor 0xff (0x18890). */
uint8_t qspi_adc_test(void) {
  static const uint16_t COMMAND = 0xe000;
  static const uint16_t BIT_10 = 0x400;
  static const uint8_t BYTE_ALL = 0xff;
  uint16_t command = COMMAND;
  uint16_t reply;
  qspi_buffers buffers = {.transmit = &command, .receive = &reply};

  qspi_transfer(QSPI_ADC, &buffers, 1);
  uint8_t byte = (uint8_t)(reply >> 2);
  bool passed = (reply & BIT_10) == 0 && ((reply >> 4) & 1) == (reply & 1) &&
                ((reply >> 3) & 1) == ((reply >> 1) & 1) && byte != 0 &&
                byte != BYTE_ALL;
  return passed;
}

/* Writes 0x5555 and 0xaaaa to the EFC DAC and reads each back; true
 * when both agree (0x1895e). */
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

/* Runs the ADC, hex DAC, FPGA and EFC tests, each with interrupts held
 * off, a failure bit for each (0x189f2). */
void qspi_tests_run(void) {
  uint16_t sr = set_interrupt_mask(CPU_IPL_7);
  if (!qspi_adc_test()) {
    selftest.qspi |= SELFTEST_QSPI_ADC;
  }
  set_interrupt_mask(sr);
  sr = set_interrupt_mask(CPU_IPL_7);
  if (!qspi_hex_dac_test()) {
    selftest.qspi |= SELFTEST_QSPI_HEX_DAC;
  }
  set_interrupt_mask(sr);
  sr = set_interrupt_mask(CPU_IPL_7);
  if (!fpga_load_test()) {
    selftest.qspi |= SELFTEST_QSPI_FPGA;
  }
  set_interrupt_mask(sr);
  sr = set_interrupt_mask(CPU_IPL_7);
  if (!qspi_efc_test()) {
    selftest.qspi |= SELFTEST_QSPI_EFC;
  }
  set_interrupt_mask(sr);
}

/* Clears the QSPI's failures and runs its tests (0x18c8e). */
void selftest_qspi_run(void) {
  selftest.qspi = 0;
  qspi_tests_run();
}

/* The self-test of the CPU, entry 2: runs cpu_register_test, and
 * reports a failure while debugging (0x17a96). */
void selftest_cpu(char_slot run) {
  if (run.value == 1 && !selftest_aborted()) {
    cpu_register_test();
  }
  if (selftest_may_report(run.value) && run.value == 1 && selftest.cpu != 0 &&
      debug_is_enabled()) {
    selftest_report_fail(selftest.cpu & SELFTEST_FAILED, "CPU:", 0);
  }
}

/* The self-test of system RAM, entry 3: its data bits and address
 * lines, each printed in binary on a failure while debugging
 * (0x179ac). */
void selftest_ram(char_slot run) {
  char lines[SELFTEST_RAM_ADDRESS_LINES + 1];
  char text[SELFTEST_LINE_LENGTH];

  if (run.value == 1 && !selftest_aborted()) {
    ram_pattern_test_locked();
  }
  if (selftest_may_report(run.value) && run.value == 1 && selftest.ram != 0 &&
      debug_is_enabled()) {
    format_binary(selftest.ram_data_bits, lines, SELFTEST_RAM_DATA_LINES);
    sprintf(text, "%s %s", "System RAM: D15-D0:", lines);
    selftest_report_fail(selftest.ram & SELFTEST_RAM_PATTERN, text, 0);
    format_binary(selftest.ram_address_lines, lines,
                  SELFTEST_RAM_ADDRESS_LINES);
    sprintf(text, "%s %s", "System RAM: adr line A18-A1:", lines);
    selftest_report_fail(selftest.ram & SELFTEST_RAM_ADDRESS_LINE, text, 0);
  }
}

/* The self-test of the EEPROM, entry 4: whether the settings' slot is
 * good, reported while debugging (0x17b16). */
void selftest_eeprom(char_slot run) {
  if (run.value == 1 && !selftest_aborted()) {
    selftest_nv_slot();
  }
  if (selftest_may_report(run.value) && run.value == 1 &&
      selftest.eeprom != 0 && debug_is_enabled()) {
    selftest_report_fail(selftest.eeprom & SELFTEST_FAILED, "EEPROM:", 0);
  }
}

/* The self-test of the DUART, entry 5: its registers, counter and both
 * channels, each failure reported while debugging (0x17cbe). */
void selftest_uart(char_slot run) {
  if (run.value == 1 && !selftest_aborted()) {
    duart_test();
  }
  if (selftest_may_report(run.value) && run.value == 1 && selftest.uart != 0 &&
      debug_is_enabled()) {
    selftest_report_fail(selftest.uart & SELFTEST_UART_REGISTER,
                         "UART: register read/write", 0);
    selftest_report_fail(selftest.uart & SELFTEST_UART_COUNTER, "UART: counter",
                         0);
    selftest_report_fail(selftest.uart & SELFTEST_UART_CHANNEL_A_DATA,
                         "UART: channel A data", 0);
    selftest_report_fail(selftest.uart & SELFTEST_UART_CHANNEL_A_TIMEOUT,
                         "UART: channel A timeout", 0);
    selftest_report_fail(selftest.uart & SELFTEST_UART_CHANNEL_B_DATA,
                         "UART: channel B data", 0);
    selftest_report_fail(selftest.uart & SELFTEST_UART_CHANNEL_B_TIMEOUT,
                         "UART: channel B timeout", 0);
  }
}

/* The self-test of the QSPI's devices, entry 6: the ADC, the hex DAC,
 * the FPGA and the EFC, each failure reported while debugging
 * (0x17b96). */
void selftest_qspi(char_slot run) {
  if (run.value == 1 && !selftest_aborted()) {
    selftest_qspi_run();
  }
  if (selftest_may_report(run.value) && run.value == 1 && selftest.qspi != 0 &&
      debug_is_enabled()) {
    selftest_report_fail(selftest.qspi & SELFTEST_QSPI_ADC, "QSPI: ADC", 0);
    selftest_report_fail(selftest.qspi & SELFTEST_QSPI_HEX_DAC, "QSPI: Hex DAC",
                         0);
    selftest_report_fail(selftest.qspi & SELFTEST_QSPI_FPGA, "QSPI: FPGA", 0);
    selftest_report_fail(selftest.qspi & SELFTEST_QSPI_EFC, "QSPI: EFC", 0);
  }
}

/* The self-test of the FPGA's counters, entry 7: runs counter_selftest
 * when run is 1 and the self-test has not been aborted, and reports a
 * failure while debugging (0x17c3e). */
void selftest_fpga(char_slot run) {
  if (run.value == 1 && !selftest_aborted()) {
    counter_selftest();
  }
  if (selftest_may_report(run.value) && run.value == 1 && selftest.fpga != 0 &&
      debug_is_enabled()) {
    selftest_report_fail(selftest.fpga & SELFTEST_FAILED,
                         "FPGA: counters not preset", 0);
  }
}

/* The self-test of the time interpolator, entry 8: its calibration,
 * reported while debugging (0x178e0). */
void selftest_interpolator(char_slot run) {
  if (run.value == 1 && !selftest_aborted()) {
    interpolator_selftest();
  }
  if (selftest_may_report(run.value) && run.value == 1 &&
      selftest.interpolator != 0 && debug_is_enabled()) {
    selftest_report_fail(selftest.interpolator & SELFTEST_FAILED,
                         "Interpolator: A, MRC mode", 0);
  }
}

/* The self-test of the internal reference, entry 9: its 1PPS count,
 * reported while debugging (0x17946). */
void selftest_internal_reference(char_slot run) {
  if (run.value == 1 && !selftest_aborted()) {
    internal_pps_test();
  }
  if (selftest_may_report(run.value) && run.value == 1 &&
      selftest.internal_reference != 0 && debug_is_enabled()) {
    selftest_report_fail(selftest.internal_reference & SELFTEST_FAILED,
                         "Internal reference: ", 0);
  }
}

/* The self-test of the GPS engine, entry 10: runs nothing, since the
 * loop runs the engine's own (gps_diag_sequence), and reports each of
 * its bits while debugging (0x17d94). */
void selftest_gps(char_slot run) {
  if (run.value == 1) {
    /* its result unused (0x17da2) */
    selftest_aborted();
  }
  if (selftest_may_report(run.value) && run.value == 1 && selftest.gps != 0 &&
      debug_is_enabled()) {
    selftest_report_fail(selftest.gps & SELFTEST_GPS_CHANNEL_1,
                         "GPS: Channel 1 Correlation", 0);
    selftest_report_fail(selftest.gps & SELFTEST_GPS_CHANNEL_2,
                         "GPS: Channel 2 Correlation", 0);
    selftest_report_fail(selftest.gps & SELFTEST_GPS_CHANNEL_3,
                         "GPS: Channel 3 Correlation", 0);
    selftest_report_fail(selftest.gps & SELFTEST_GPS_CHANNEL_4,
                         "GPS: Channel 4 Correlation", 0);
    selftest_report_fail(selftest.gps & SELFTEST_GPS_CHANNEL_5,
                         "GPS: Channel 5 Correlation", 0);
    selftest_report_fail(selftest.gps & SELFTEST_GPS_CHANNEL_6,
                         "GPS: Channel 6 Correlation", 0);
    selftest_report_fail(selftest.gps & SELFTEST_GPS_1_KHZ,
                         "GPS: 1 KHz Presence", 0);
    selftest_report_fail(selftest.gps & SELFTEST_GPS_ROM_LOW_BYTE,
                         "GPS: ROM LSByte", 0);
    selftest_report_fail(selftest.gps & SELFTEST_GPS_ROM_HIGH_BYTE,
                         "GPS: ROM MSByte", 0);
    selftest_report_fail(selftest.gps & SELFTEST_GPS_RAM_LOW_BYTE,
                         "GPS: RAM LSByte", 0);
    selftest_report_fail(selftest.gps & SELFTEST_GPS_RAM_HIGH_BYTE,
                         "GPS: RAM MSByte", 0);
    selftest_report_fail(selftest.gps & SELFTEST_GPS_EEPROM, "GPS: EEPROM", 0);
    selftest_report_fail(selftest.gps & SELFTEST_GPS_DCXO, "GPS: DCXO SPI Comm",
                         0);
    selftest_report_fail(selftest.gps & SELFTEST_GPS_RTC,
                         "GPS: RTC Comm & Time", 0);
    selftest_report_fail(selftest.gps & SELFTEST_GPS_NO_ANSWER,
                         "GPS: SW problem", 0);
    selftest_report_fail(selftest.gps & SELFTEST_GPS_TIMED_OUT,
                         "GPS: Engine timeout", 0);
  }
}

/* The self-test of the supplies, entry 11: each out of range reported
 * while debugging (0x17f2c). */
void selftest_power(char_slot run) {
  if (run.value == 1 && !selftest_aborted()) {
    supplies_test();
  }
  if (selftest_may_report(run.value) && run.value == 1 && selftest.power != 0 &&
      debug_is_enabled()) {
    selftest_report_fail(selftest.power & SELFTEST_POWER_5V, "POWER: 5V", 0);
    selftest_report_fail(selftest.power & SELFTEST_POWER_P15V, "POWER: 15V", 0);
    selftest_report_fail(selftest.power & SELFTEST_POWER_M15V, "POWER: -15V",
                         0);
  }
}

/* Runs the lamp test unless the self-test has been aborted, entry 1
 * (0x17ca4). */
void selftest_lamps(char_slot run) {
  if (run.value == 1 && !selftest_aborted()) {
    lamp_test();
  }
}

/* Entry 12, IMRC: on the second pass runs interpolator_selftest
 * (0x17fc0). */
void selftest_interpolator_calibration(char_slot run) {
  if (run.value == 1) {
    interpolator_selftest();
  }
}

/* Entry 13, QMRC: on the second pass runs interpolator_sample_test
 * (0x17fd0). */
void selftest_interpolator_sample(char_slot run) {
  if (run.value == 1) {
    interpolator_sample_test();
  }
}

/* The failures as selftest_summary_bit flags in selftest.summary;
 * reports the self-test failure condition set or cleared (0x177fa). */
void selftest_summary(void) {
  selftest.summary = 0;
  if (selftest.cpu != 0) {
    selftest.summary = SELFTEST_SUMMARY_CPU;
  }
  if (selftest.ram != 0) {
    selftest.summary |= SELFTEST_SUMMARY_RAM;
  }
  if (selftest.eeprom != 0) {
    selftest.summary |= SELFTEST_SUMMARY_EEPROM;
  }
  if (selftest.uart != 0) {
    selftest.summary |= SELFTEST_SUMMARY_UART;
  }
  if (selftest.qspi != 0) {
    selftest.summary |= SELFTEST_SUMMARY_QSPI;
  }
  if (selftest.fpga != 0) {
    selftest.summary |= SELFTEST_SUMMARY_FPGA;
  }
  if (selftest.interpolator != 0) {
    selftest.summary |= SELFTEST_SUMMARY_INTERPOLATOR;
  }
  if (selftest.internal_reference != 0) {
    selftest.summary |= SELFTEST_SUMMARY_INTERNAL_REFERENCE;
  }
  if (selftest.gps != 0) {
    selftest.summary |= SELFTEST_SUMMARY_GPS;
  }
  if (selftest.power != 0) {
    selftest.summary |= SELFTEST_SUMMARY_POWER;
  }
  status_happening(selftest.summary == 0 ? STATUS_SELFTEST_FAILURE_CLEARED
                                         : STATUS_SELFTEST_FAILURE_SET);
}

/* Runs each self-test in the list at 0x17fe0 with run 0, the second
 * slot's char 1 (0x177ac). */
void selftest_run_all(void) {
  for (selftest_entry* test = selftest_list; *test != NULL; test++) {
    (*test)((char_slot){.value = 0, .next = 1});
  }
}

/* Runs each self-test in the list at 0x17fe0 with run 1, counting
 * watchdog_pll before the first and after each (0x177cc). */
void selftest_run_all_counted(void) {
  watchdog_pll++;
  for (selftest_entry* test = selftest_list; *test != NULL; test++) {
    (*test)((char_slot){.value = 1, .next = 1});
    watchdog_pll++;
  }
}

/* Self-test 0, every test: on the second pass runs them all, counting
 * watchdog_pll, then summarizes and reports the failures while
 * debugging (0x17892). */
void selftest_all(char_slot run) {
  if (run.value == 0) {
    selftest_run_all();
    return;
  }
  selftest_run_all_counted();
  selftest_summary();
  if (!selftest_aborted() && selftest.summary != 0 && debug_is_enabled()) {
    selftest_report_fail(selftest.summary, "ALL:", 0);
  }
}

/* Runs test n of the table at 0x18458 twice, run 0 then 1, between
 * selftest_begin and selftest_end, with selftest.running set
 * (0x182ec). */
void selftest_execute(selftest_test_number test) {
  selftest.running = 1;
  watchdog_pll++;
  selftest_test* entry = selftest_table[test];
  selftest_begin();
  entry((char_slot){.value = 0});
  entry((char_slot){.value = 1});
  selftest_end();
  watchdog_pll++;
  selftest.running = 0;
}

/* The power-up checks, selftest_print_failure's table at 0x186b0: each
 * runs its test, lighting a lamp on a failure, and returns its word. */

/* "DISP": runs the lamp test; 0 (0x18d40). */
uint32_t lamp_test_command(void) {
  lamp_test();
  return 0;
}

/* "PROC": selftest.cpu, the reset code's CPU test (0x185e2). */
uint32_t selftest_cpu_result(void) { return (uint16_t)selftest.cpu; }

/* "RAM": selftest.ram, the reset code's RAM test (0x185ec). */
uint32_t selftest_ram_result(void) { return selftest.ram; }

/* "EEPROM": selftest.eeprom (0x185f6). */
uint32_t selftest_eeprom_result(void) { return (uint16_t)selftest.eeprom; }

/* "UART": duart_test, lighting OP6 on a failure; selftest.uart
 * (0x18d4a). */
uint32_t selftest_uart_with_lamp(void) {
  duart_test();
  if (selftest.uart != 0) {
    duart_output_bits(BOARD_LED_DUART, 0);
  }
  return (uint16_t)selftest.uart;
}

/* "QSPI": the QSPI tests, lighting OP7 on a failure; selftest.qspi
 * (0x18c9c). */
int16_t selftest_qspi_with_lamp(void) {
  selftest_qspi_run();
  if (selftest.qspi != 0) {
    duart_output_bits(BOARD_LED_SELFTEST, 0);
  }
  return selftest.qspi;
}

/* "FPGA": counter_selftest, lighting OP7 on a failure; selftest.fpga
 * (0x18cc6). */
uint32_t selftest_fpga_with_lamp(void) {
  counter_selftest();
  if (selftest.fpga != 0) {
    duart_output_bits(BOARD_LED_SELFTEST, 0);
  }
  return (uint16_t)selftest.fpga;
}

/* "INTERP": the interpolator test, lighting OP7 on a failure;
 * selftest.interpolator (0x18cec). */
int16_t selftest_interpolator_with_lamp(void) {
  selftest.interpolator = 0;
  interpolator_selftest();
  if (selftest.interpolator != 0) {
    duart_output_bits(BOARD_LED_SELFTEST, 0);
  }
  return selftest.interpolator;
}

/* "INT_REF": the internal 1PPS test, lighting OP7 on a failure;
 * selftest.internal_reference (0x18d16). */
int16_t selftest_internal_reference_with_lamp(void) {
  selftest.internal_reference = 0;
  internal_pps_test();
  if (selftest.internal_reference != 0) {
    duart_output_bits(BOARD_LED_SELFTEST, 0);
  }
  return selftest.internal_reference;
}

/* "GPS": gps_selftest_run, lighting OP7 on a failure; selftest.gps
 * (0x18d70). */
uint32_t selftest_gps_with_lamp(void) {
  gps_selftest_run();
  if (selftest.gps != 0) {
    duart_output_bits(BOARD_LED_SELFTEST, 0);
  }
  return (uint16_t)selftest.gps;
}

/* "POWER": supplies_test, lighting OP7 on a failure; selftest.power
 * (0x18d96). */
uint32_t selftest_power_with_lamp(void) {
  supplies_test();
  if (selftest.power != 0) {
    duart_output_bits(BOARD_LED_SELFTEST, 0);
  }
  return (uint16_t)selftest.power;
}

/* Sums a ROM's two byte lanes, the even bytes and the odd, over count
 * less 1 longs (the low word of count less 2 counts dbf, 0x17774); the
 * sums' high bytes, even then odd, then their low bytes.  Called by
 * nothing (0x17750). */
uint32_t rom_lane_checksum(uint8_t* rom, uint32_t count) {
  static const uint16_t HIGH_BYTE = 0xff00;
  static const uint16_t LOW_BYTE = 0x00ff;
  static const uint32_t BYTE_BITS = 8;
  uint16_t even = 0;
  uint16_t odd = 0;
  uint16_t remaining = (uint16_t)(count - 2);

  do {
    even += rom[0] + rom[2];
    odd += rom[1] + rom[3];
    rom += 4;
  } while (remaining-- != 0);
  return (uint32_t)(even & HIGH_BYTE) << (2 * BYTE_BITS) |
         (uint32_t)(odd & HIGH_BYTE) << BYTE_BITS |
         (uint32_t)(even & LOW_BYTE) << BYTE_BITS | (odd & LOW_BYTE);
}
