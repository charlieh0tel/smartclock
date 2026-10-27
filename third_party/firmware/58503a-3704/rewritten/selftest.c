/* The self-tests, *TST? and :DIAGnostic:TEST? (097-59551-02,
 * "Assessing Receiver Health"), written by hand in place of the
 * decompiler's (docs/firmware/README.md, "Hand-written C";
 * docs/firmware/modules.md, `applications/selftest`).  The table at
 * 0x185ce runs test n, a selftest_test_number, and the list at 0x18134
 * all of them; each leaves its failures in its selftest_results word
 * (docs/firmware/structures.md, "The self-tests' results").
 * selftest_print_failure's table at 0x18814 runs the *_with_lamp checks
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

/* Clears selftest.running, lamp_test's flag and the abort flag, and
 * marks the self-tests passed (0x18574). */
void selftest_init(void) {
  selftest.running = 0;
  selftest.lamp_testing = 0;
  selftest.abort = 0;
  selftest.passed = 1;
}

/* Clears the abort flag before a test (0x18434). */
void selftest_begin(void) { selftest.abort = 0; }

/* Clears the abort flag after a test (0x1843c). */
void selftest_end(void) { selftest.abort = 0; }

/* Whether the running test has been asked to stop (0x1853e). */
bool selftest_aborted(void) { return selftest.abort != 0; }

/* Whether a test may report: on its first pass, run 0, or when it was
 * not aborted (0x18444). */
bool selftest_may_report(char run) { return run == 0 || selftest.abort == 0; }

/* If a test is running, aborts it and waits until it stops; whether one
 * was (0x18590). */
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
 * 1024 ticks (0x1861a). */
void internal_pps_test(void) {
  static const uint32_t TICKS = 1024;

  selftest.internal_reference = 0;
  uint8_t before = internal_pps_count;
  psos_pause(TICKS);
  /* the count wraps; the difference is signed (0x1864c) */
  int8_t count = (int8_t)(internal_pps_count - before);
  if (count < MONITORS_PPS_LEAST || count > MONITORS_PPS_MOST) {
    selftest.internal_reference |= SELFTEST_FAILED;
  }
}

/* The EEPROM test: whether slot nv_slot's record is bad (0x18dda). */
void selftest_nv_slot(void) {
  selftest.eeprom = 0;
  selftest.eeprom = nv_slot_valid();
}

/* Clears the QSPI's failures and runs its tests (0x18df4). */
void selftest_qspi_run(void) {
  selftest.qspi = 0;
  qspi_tests_run();
}

/* The self-test of the CPU, entry 2: runs cpu_register_test, and
 * reports a failure while debugging (0x17af0). */
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
 * (0x17a06). */
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
 * good, reported while debugging (0x17b70). */
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
 * channels, each failure reported while debugging (0x17d18). */
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
 * (0x17bf0). */
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
 * failure while debugging (0x17c98). */
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
 * reported while debugging (0x1793a). */
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
 * reported while debugging (0x179a0). */
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

/* Self-test 0, every test: on the second pass runs them all, counting
 * watchdog_pll, then summarizes and reports the failures while
 * debugging (0x178ec). */
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

/* Runs test n of the table at 0x185ce twice, run 0 then 1, between
 * selftest_begin and selftest_end, with selftest.running set
 * (0x1845c). */
void selftest_execute(selftest_test_number test) {
  selftest.running = 1;
  watchdog_pll++;
  /* The test indexes the table sign-extended, as the image's (0x18470),
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
 * and details for the reply; one past 13 runs nothing (0x18bde). */
void selftest_run(uint32_t number) {
  /* the test is the argument's low byte (0x18be6) */
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

/* "PROC": selftest.cpu, the reset code's CPU test (0x18746). */
uint32_t selftest_cpu_result(void) { return (uint16_t)selftest.cpu; }

/* "RAM": selftest.ram, the reset code's RAM test (0x18750). */
uint32_t selftest_ram_result(void) { return selftest.ram; }

/* "EEPROM": selftest.eeprom (0x1875a). */
uint32_t selftest_eeprom_result(void) { return (uint16_t)selftest.eeprom; }

/* "UART": duart_test, lighting OP6 on a failure; selftest.uart
 * (0x18eb0). */
uint32_t selftest_uart_with_lamp(void) {
  duart_test();
  if (selftest.uart != 0) {
    duart_output_bits(BOARD_LED_DUART, 0);
  }
  return (uint16_t)selftest.uart;
}

/* "QSPI": the QSPI tests, lighting OP7 on a failure; selftest.qspi
 * (0x18e02). */
uint32_t selftest_qspi_with_lamp(void) {
  selftest_qspi_run();
  if (selftest.qspi != 0) {
    duart_output_bits(BOARD_LED_SELFTEST, 0);
  }
  return (uint16_t)selftest.qspi;
}

/* "FPGA": counter_selftest, lighting OP7 on a failure; selftest.fpga
 * (0x18e2c). */
uint32_t selftest_fpga_with_lamp(void) {
  counter_selftest();
  if (selftest.fpga != 0) {
    duart_output_bits(BOARD_LED_SELFTEST, 0);
  }
  return (uint16_t)selftest.fpga;
}

/* "INTERP": the interpolator test, lighting OP7 on a failure;
 * selftest.interpolator (0x18e52). */
uint32_t selftest_interpolator_with_lamp(void) {
  selftest.interpolator = 0;
  interpolator_selftest();
  if (selftest.interpolator != 0) {
    duart_output_bits(BOARD_LED_SELFTEST, 0);
  }
  return (uint16_t)selftest.interpolator;
}

/* "INT_REF": the internal 1PPS test, lighting OP7 on a failure;
 * selftest.internal_reference (0x18e7c). */
uint32_t selftest_internal_reference_with_lamp(void) {
  selftest.internal_reference = 0;
  internal_pps_test();
  if (selftest.internal_reference != 0) {
    duart_output_bits(BOARD_LED_SELFTEST, 0);
  }
  return (uint16_t)selftest.internal_reference;
}

/* "GPS": gps_selftest_run, lighting OP7 on a failure; selftest.gps,
 * a long in the 58503A 3704, where the Z3805A's result is its word
 * (0x18ed6). */
uint32_t selftest_gps_with_lamp(void) {
  gps_selftest_run();
  if (selftest.gps != 0) {
    duart_output_bits(BOARD_LED_SELFTEST, 0);
  }
  return selftest.gps;
}
