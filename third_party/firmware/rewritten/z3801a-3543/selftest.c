/* The self-tests, *TST? and :DIAGnostic:TEST? (097-59551-02,
 * "Assessing Receiver Health"), written by hand in place of the
 * decompiler's (docs/firmware/README.md, "Hand-written C";
 * docs/firmware/modules.md, `applications/selftest`).  The table at
 * 0x18160 runs test n, a selftest_test_number, and the list at 0x17ce8
 * all of them; each leaves its failures in its selftest_results word
 * (docs/firmware/structures.md, "The self-tests' results").
 * selftest_print_failure's table at 0x183b8 runs the *_with_lamp checks
 * at power-up.  The exported C includes this file after its own, so the
 * types and prototypes it declares are in scope; each function here
 * keeps its generated prototype. */

/* The RAM test's lines, D15-D0 and A18-A1, as selftest_ram prints
 * them. */
#define SELFTEST_RAM_DATA_LINES 16

#define SELFTEST_RAM_ADDRESS_LINES 18

/* selftest_ram's line of text, the original's 60 bytes. */
#define SELFTEST_LINE_LENGTH 60

/* selftest_print_failure's "FAIL: " and the longest check's name,
 * "INT_REF", with its end: the original's 14 bytes. */
#define SELFTEST_FAILURE_LENGTH 14

/* Clears selftest.running and the abort flag, and marks the self-tests
 * passed (0x1810c). */
void selftest_init(void) {
  selftest.running = 0;
  selftest.abort = 0;
  selftest.passed = 1;
}

/* Clears the abort flag before a test (0x17fcc). */
void selftest_begin(void) { selftest.abort = 0; }

/* Clears the abort flag after a test (0x17fd4). */
void selftest_end(void) { selftest.abort = 0; }

/* Whether the running test has been asked to stop (0x180d6). */
bool selftest_aborted(void) { return selftest.abort != 0; }

/* Whether a test may report: on its first pass, run 0, or when it was
 * not aborted (0x17fdc). */
bool selftest_may_report(char run) { return run == 0 || selftest.abort == 0; }

/* If a test is running, aborts it and waits until it stops; whether one
 * was (0x18122). */
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

/* Self-test 9's count: fails unless one to three internal 1PPS came in
 * 1024 ticks (0x181ac). */
void internal_pps_test(void) {
  static const uint32_t TICKS = 1024;

  selftest.internal_reference = 0;
  uint8_t before = internal_pps_count;
  psos_pause(TICKS);
  /* the count wraps; the difference is signed (0x181de) */
  int8_t count = (int8_t)(internal_pps_count - before);
  if (count < MONITORS_PPS_LEAST || count > MONITORS_PPS_MOST) {
    selftest.internal_reference |= SELFTEST_FAILED;
  }
}

/* Lights every lamp, then none, then each in turn, then restores them
 * (0x181f6). */
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
 * and -15 V within -16 to -14, the bounds hmon_check_supplies applies:
 * 10 percent of 5 V, 1 V of 15 (0x1824e). */
void supplies_test(void) {
  selftest.power = 0;
  float volts = read_adc_5v();
  if (float_compare(volts, MONITORS_PLUS_5V_LEAST) < 0 ||
      float_compare(volts, MONITORS_PLUS_5V_MOST) > 0) {
    selftest.power |= SELFTEST_POWER_5V;
  }
  volts = read_adc_p15v();
  if (float_compare(volts, MONITORS_PLUS_15V_LEAST) < 0 ||
      float_compare(volts, MONITORS_PLUS_15V_MOST) > 0) {
    selftest.power |= SELFTEST_POWER_P15V;
  }
  volts = read_adc_m15v();
  if (float_compare(volts, MONITORS_MINUS_15V_LEAST) < 0 ||
      float_compare(volts, MONITORS_MINUS_15V_MOST) > 0) {
    selftest.power |= SELFTEST_POWER_M15V;
  }
}

/* The EEPROM test: whether slot nv_slot's record is bad (0x1897c). */
void selftest_nv_slot(void) {
  selftest.eeprom = 0;
  selftest.eeprom = nv_slot_valid();
}

/* Reads the ADC once and checks its reply: 1 when bit 10 is clear,
 * bits 4 and 3 repeat bits 0 and 1, and the byte in bits 9 to 2 is
 * neither 0 nor 0xff (0x18598). */
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

/* Runs the ADC, hex DAC, FPGA and EFC tests, each with interrupts held
 * off, a failure bit for each (0x186fa). */
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

/* Clears the QSPI's failures and runs its tests (0x18996). */
void selftest_qspi_run(void) {
  selftest.qspi = 0;
  qspi_tests_run();
}

/* The self-test of the CPU, entry 2: runs cpu_register_test, and
 * reports a failure while debugging (0x1779e). */
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
 * (0x176b4). */
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
 * good, reported while debugging (0x1781e). */
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
 * channels, each failure reported while debugging (0x179c6). */
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
 * (0x1789e). */
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
 * failure while debugging (0x17946). */
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
 * reported while debugging (0x175e8). */
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
 * reported while debugging (0x1764e). */
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
 * its bits while debugging (0x17a9c). */
void selftest_gps(char_slot run) {
  if (run.value == 1) {
    /* its result unused (0x17aaa) */
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
 * while debugging (0x17c34). */
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

/* The failures as selftest_summary_bit flags in selftest.summary;
 * reports the self-test failure condition set or cleared (0x17502). */
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

/* Runs each self-test in the list at 0x17ce8 with run 0, the second
 * slot's char 1 (0x174b4). */
void selftest_run_all(void) {
  for (selftest_entry* test = selftest_list; *test != NULL; test++) {
    (*test)((char_slot){.value = 0, .next = 1});
  }
}

/* Runs each self-test in the list at 0x17ce8 with run 1, counting
 * watchdog_pll before the first and after each (0x174d4). */
void selftest_run_all_counted(void) {
  watchdog_pll++;
  for (selftest_entry* test = selftest_list; *test != NULL; test++) {
    (*test)((char_slot){.value = 1, .next = 1});
    watchdog_pll++;
  }
}

/* Self-test 0, every test: on the second pass runs them all, counting
 * watchdog_pll, then summarizes and reports the failures while
 * debugging (0x1759a). */
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

/* Runs test n of the table at 0x18160 twice, run 0 then 1, between
 * selftest_begin and selftest_end, with selftest.running set
 * (0x17ff4). */
void selftest_execute(selftest_test_number test) {
  selftest.running = 1;
  watchdog_pll++;
  /* The test indexes the table sign-extended, as the image's (0x18008),
   * so that one past 0x7f reads below it. */
  selftest_test* entry = selftest_table[(int8_t)test];
  selftest_begin();
  entry((char_slot){.value = 0});
  entry((char_slot){.value = 1});
  selftest_end();
  watchdog_pll++;
  selftest.running = 0;
}

/* Runs self-test n for *TST? or :DIAGnostic:TEST? and leaves its result
 * and details for the reply; one past 13 runs nothing (0x1877c). */
void selftest_run(uint32_t number) {
  /* the test is the argument's low byte (0x18784) */
  const selftest_test_number test = (selftest_test_number)number;

  selftest.in_run = 1;
  selftest.result = 0;
  selftest.details[0] = 0;
  selftest.details[1] = 0;
  selftest.test = test;
  if (test > SELFTEST_TEST_INTERPOLATOR_SAMPLE) {
    selftest.in_run = 0;
    return;
  }
  selftest_execute(test);
  switch (test) {
    case SELFTEST_TEST_ALL:
      selftest.result = selftest.summary;
      break;
    case SELFTEST_TEST_DISPLAY:
      selftest.result = 0;
      break;
    case SELFTEST_TEST_PROCESSOR:
      selftest.result = (uint16_t)selftest.cpu;
      break;
    case SELFTEST_TEST_RAM:
      selftest.result = selftest.ram;
      selftest.details[0] = selftest.ram_data_bits;
      selftest.details[1] = selftest.ram_address_lines;
      break;
    case SELFTEST_TEST_EEPROM:
      selftest.result = (uint16_t)selftest.eeprom;
      break;
    case SELFTEST_TEST_UART:
      selftest.result = (uint16_t)selftest.uart;
      break;
    case SELFTEST_TEST_QSPI:
      selftest.result = (uint16_t)selftest.qspi;
      break;
    case SELFTEST_TEST_FPGA:
      selftest.result = (uint16_t)selftest.fpga;
      break;
    case SELFTEST_TEST_INTERPOLATOR:
      selftest.result = (uint16_t)selftest.interpolator;
      break;
    case SELFTEST_TEST_INTERNAL_REFERENCE:
      selftest.result = (uint16_t)selftest.internal_reference;
      break;
    case SELFTEST_TEST_GPS:
      selftest.result = (uint16_t)selftest.gps;
      break;
    case SELFTEST_TEST_POWER:
      selftest.result = (uint16_t)selftest.power;
      break;
    case SELFTEST_TEST_INTERPOLATOR_CALIBRATION:
      selftest.result = (uint16_t)selftest.interpolator;
      selftest.details[0] = selftest.interpolator_tries;
      selftest.details[1] = selftest.interpolator_calibrated;
      break;
    case SELFTEST_TEST_INTERPOLATOR_SAMPLE:
      selftest.result = (uint16_t)selftest.interpolator;
      selftest.details[0] = selftest.interpolator_readings[0];
      selftest.details[1] = selftest.interpolator_readings[1];
      break;
  }
}

/* "PROC": selftest.cpu, the reset code's CPU test (0x182f4). */
uint32_t selftest_cpu_result(void) { return (uint16_t)selftest.cpu; }

/* "RAM": selftest.ram, the reset code's RAM test (0x182fe). */
uint32_t selftest_ram_result(void) { return selftest.ram; }

/* "EEPROM": selftest.eeprom (0x189ea). */
uint32_t selftest_eeprom_result(void) { return (uint16_t)selftest.eeprom; }

/* "UART": duart_test, lighting OP6 on a failure; selftest.uart
 * (0x18a52). */
uint32_t selftest_uart_with_lamp(void) {
  duart_test();
  if (selftest.uart != 0) {
    duart_output_bits(BOARD_LED_DUART, 0);
  }
  return (uint16_t)selftest.uart;
}

/* "QSPI": the QSPI tests, lighting OP7 on a failure; selftest.qspi
 * (0x189a4). */
uint32_t selftest_qspi_with_lamp(void) {
  selftest_qspi_run();
  if (selftest.qspi != 0) {
    duart_output_bits(BOARD_LED_SELFTEST, 0);
  }
  return (uint16_t)selftest.qspi;
}

/* "FPGA": counter_selftest, lighting OP7 on a failure; selftest.fpga
 * (0x189ce). */
uint32_t selftest_fpga_with_lamp(void) {
  counter_selftest();
  if (selftest.fpga != 0) {
    duart_output_bits(BOARD_LED_SELFTEST, 0);
  }
  return (uint16_t)selftest.fpga;
}

/* "INTERP": the interpolator test, lighting OP7 on a failure;
 * selftest.interpolator (0x189f4). */
uint32_t selftest_interpolator_with_lamp(void) {
  selftest.interpolator = 0;
  interpolator_selftest();
  if (selftest.interpolator != 0) {
    duart_output_bits(BOARD_LED_SELFTEST, 0);
  }
  return (uint16_t)selftest.interpolator;
}

/* "INT_REF": the internal 1PPS test, lighting OP7 on a failure;
 * selftest.internal_reference (0x18a1e). */
uint32_t selftest_internal_reference_with_lamp(void) {
  selftest.internal_reference = 0;
  internal_pps_test();
  if (selftest.internal_reference != 0) {
    duart_output_bits(BOARD_LED_SELFTEST, 0);
  }
  return (uint16_t)selftest.internal_reference;
}

/* "GPS": gps_selftest_run, lighting OP7 on a failure; selftest.gps
 * (0x18a78). */
uint32_t selftest_gps_with_lamp(void) {
  gps_selftest_run();
  if (selftest.gps != 0) {
    duart_output_bits(BOARD_LED_SELFTEST, 0);
  }
  return (uint16_t)selftest.gps;
}

/* "POWER": supplies_test, lighting OP7 on a failure; selftest.power
 * (0x18a9e). */
uint32_t selftest_power_with_lamp(void) {
  supplies_test();
  if (selftest.power != 0) {
    duart_output_bits(BOARD_LED_SELFTEST, 0);
  }
  return (uint16_t)selftest.power;
}

/* A power-up check: runs its test and returns its failures, 0 if
 * none. */
typedef uint32_t selftest_check_function(void);

/* An entry of selftest_print_failure's table. */
typedef struct selftest_check {
  selftest_check_function* run; /* +0x00 */
  char* name;                   /* +0x04: what a failure prints */
} selftest_check;

/* The power-up checks in order, the image's table at 0x183b8, which
 * ends with a null entry. */
static const selftest_check selftest_checks[] = {
    {lamp_test_command, "DISP"},
    {selftest_cpu_result, "PROC"},
    {selftest_ram_result, "RAM"},
    {selftest_eeprom_result, "EEPROM"},
    {selftest_uart_with_lamp, "UART"},
    {selftest_qspi_with_lamp, "QSPI"},
    {selftest_fpga_with_lamp, "FPGA"},
    {selftest_interpolator_with_lamp, "INTERP"},
    {selftest_internal_reference_with_lamp, "INT_REF"},
    {selftest_gps_with_lamp, "GPS"},
    {selftest_power_with_lamp, "POWER"},
};

/* The power-up self-test: runs each check, printing the name and code
 * of each that fails, then summarizes and logs a failure; clear_eeprom
 * clears the EEPROM's result first (0x18308). */
void selftest_print_failure(char clear_eeprom) {
  static const uint32_t CHECKS =
      sizeof(selftest_checks) / sizeof(selftest_check);
  char text[SELFTEST_FAILURE_LENGTH];

  selftest.passed = 1;
  if (clear_eeprom != 0) {
    selftest.eeprom = 0;
  }
  selftest.running = 1;
  for (uint32_t check = 0; check < CHECKS; check++) {
    const uint32_t code = selftest_checks[check].run();
    if (code != 0) {
      selftest.passed = 0;
      libc_copy(text, "FAIL: ");
      strcat(text, selftest_checks[check].name);
      debug_printf("%s, error code: %02X\r\n", text, code);
    }
    watchdog_pll++;
  }
  selftest_summary();
  selftest.result = selftest.summary;
  selftest.test = SELFTEST_TEST_ALL;
  selftest.running = 0;
  if (selftest.passed == 0) {
    log_event(LOG_SELFTEST_FAILED);
  }
}
