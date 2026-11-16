/* From the 58503B 1.01.04's rewritten/selftest.c and the Z3805A 3543B's
 * rewritten/selftest.c and the Z3815A 4010's rewritten/selftest.c, the
 * Z3805A 1.01.03's own beside common/'s: of its functions, those the
 * Z3805A 1.01.03's code carries identical or relocated
 * (docs/firmware/z3805a-1.01.03.md, "The link list").  Its comments'
 * addresses are the Z3805A 1.01.03's, but where they name another image.
 * Each keeps its generated prototype. */

/* The RAM test's lines, D15-D0 and A18-A1, as selftest_ram prints
 * them. */
#define SELFTEST_RAM_DATA_LINES 16

#define SELFTEST_RAM_ADDRESS_LINES 18

/* selftest_ram's line of text, the original's 60 bytes. */
#define SELFTEST_LINE_LENGTH 60

/* Clears selftest.running, lamp_test's flag and the abort flag, and
 * marks the self-tests passed (0x188ac). */
void selftest_init(void) {
  selftest.running = 0;
  selftest.lamp_testing = 0;
  selftest.abort = 0;
  selftest.passed = 1;
}

/* Whether the running test has been asked to stop (0x18876). */
bool selftest_aborted(void) { return selftest.abort != 0; }

/* Whether a test may report: on its first pass, run 0, or when it was
 * not aborted (0x1877c). */
bool selftest_may_report(char run) { return run == 0 || selftest.abort == 0; }

/* If a test is running, aborts it and waits until it stops; whether one
 * was (0x188c8). */
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

/* The EEPROM test: whether slot nv_slot's record is bad (0x1914c). */
void selftest_nv_slot(void) {
  selftest.eeprom = 0;
  selftest.eeprom = nv_slot_valid();
}

/* Clears the QSPI's failures and runs its tests (0x19166). */
void selftest_qspi_run(void) {
  selftest.qspi = 0;
  qspi_tests_run();
}

/* The self-test of the CPU, entry 2: runs cpu_register_test, and
 * reports a failure while debugging (0x17df4). */
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
 * (0x17d0a). */
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
 * good, reported while debugging (0x17e74). */
void selftest_eeprom(char_slot run) {
  if (run.value == 1 && !selftest_aborted()) {
    selftest_nv_slot();
  }
  if (selftest_may_report(run.value) && run.value == 1 &&
      selftest.eeprom != 0 && debug_is_enabled()) {
    selftest_report_fail(selftest.eeprom & SELFTEST_FAILED, "EEPROM:", 0);
  }
}

/* The self-test of the QSPI's devices, entry 6: the ADC, the hex DAC,
 * the FPGA and the EFC, each failure reported while debugging
 * (0x17ef4). */
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
 * failure while debugging (0x17f9c). */
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
 * reported while debugging (0x17c3e). */
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
 * reported while debugging (0x17ca4). */
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

/* The self-test of the GPS engine: runs nothing, since the loop runs
 * the engine's own, and reports each of its bits while debugging, an
 * eight-channel engine's by @@Fa's places (0x18124).  The Z3815A
 * 4010's, but eight-channel by gps.field_592's model, the GT-77's or
 * the GT-80's (0x181d4). */
void selftest_gps(char_slot run) {
  /* how far above a six-channel engine's an eight-channel one's bits
   * past the channels' sit */
  static const uint8_t EIGHT_CHANNEL_SHIFT = 2;

  if (run.value == 1) {
    /* its result unused (0x1813e) */
    selftest_aborted();
  }
  if (!selftest_may_report(run.value) || run.value != 1 || selftest.gps == 0 ||
      !debug_is_enabled()) {
    return;
  }
  const uint32_t result = selftest.gps;
  uint8_t shift = 0;

  selftest_report_fail(result & SELFTEST_GPS_CHANNEL_1,
                       "GPS: Channel 1 Correlation", 0);
  selftest_report_fail(result & SELFTEST_GPS_CHANNEL_2,
                       "GPS: Channel 2 Correlation", 0);
  selftest_report_fail(result & SELFTEST_GPS_CHANNEL_3,
                       "GPS: Channel 3 Correlation", 0);
  selftest_report_fail(result & SELFTEST_GPS_CHANNEL_4,
                       "GPS: Channel 4 Correlation", 0);
  selftest_report_fail(result & SELFTEST_GPS_CHANNEL_5,
                       "GPS: Channel 5 Correlation", 0);
  selftest_report_fail(result & SELFTEST_GPS_CHANNEL_6,
                       "GPS: Channel 6 Correlation", 0);
  if ((gps.field_592 & (GPS_FIELD_592_MODEL_GT77 | GPS_FIELD_592_MODEL_GT80)) !=
      0) {
    selftest_report_fail(result & SELFTEST_GPS_CHANNEL_6 << 1,
                         "GPS: Channel 7 Correlation", 0);
    selftest_report_fail(result & SELFTEST_GPS_CHANNEL_6 << 2,
                         "GPS: Channel 8 Correlation", 0);
    shift = EIGHT_CHANNEL_SHIFT;
  }
  selftest_report_fail(result & SELFTEST_GPS_1_KHZ << shift,
                       "GPS: 1 KHz Presence", 0);
  selftest_report_fail(result & SELFTEST_GPS_ROM_LOW_BYTE << shift,
                       "GPS: ROM LSByte", 0);
  selftest_report_fail(result & SELFTEST_GPS_ROM_HIGH_BYTE << shift,
                       "GPS: ROM MSByte", 0);
  selftest_report_fail(result & SELFTEST_GPS_RAM_LOW_BYTE << shift,
                       "GPS: RAM LSByte", 0);
  selftest_report_fail(result & SELFTEST_GPS_RAM_HIGH_BYTE << shift,
                       "GPS: RAM MSByte", 0);
  selftest_report_fail(result & SELFTEST_GPS_EEPROM << shift, "GPS: EEPROM", 0);
  selftest_report_fail(result & SELFTEST_GPS_DCXO << shift,
                       "GPS: DCXO SPI Comm", 0);
  selftest_report_fail(result & SELFTEST_GPS_RTC << shift,
                       "GPS: RTC Comm & Time", 0);
  selftest_report_fail(result & SELFTEST_GPS_NO_ANSWER, "GPS: SW problem", 0);
  selftest_report_fail(result & SELFTEST_GPS_TIMED_OUT, "GPS: Engine timeout",
                       0);
}

/* Self-test 0, every test: on the second pass runs them all, counting
 * watchdog_pll, then summarizes and reports the failures while
 * debugging (0x17bf0). */
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

/* "PROC": selftest.cpu, the reset code's CPU test (0x18a7e). */
uint32_t selftest_cpu_result(void) { return (uint16_t)selftest.cpu; }

/* "RAM": selftest.ram, the reset code's RAM test (0x18a88). */
uint32_t selftest_ram_result(void) { return selftest.ram; }

/* "EEPROM": selftest.eeprom (0x18a92). */
uint32_t selftest_eeprom_result(void) { return (uint16_t)selftest.eeprom; }

/* Checks each of the health monitor's channels with a self-test bit,
 * the supplies', against its tolerance, setting the bit in
 * selftest.power if it is out (0x18a12). */
void supplies_test(void) {
  static const uint32_t CHANNELS =
      sizeof(hmon_channels) / sizeof(hmon_channels[0]);

  selftest.power = 0;
  for (uint32_t channel = 0; channel < CHANNELS; channel++) {
    const hmon_channel_entry* entry = &hmon_channels[channel];

    if (entry->bit == 0) {
      continue;
    }
    const float reading = hmon_channel_read(channel);

    if (float_compare(reading, entry->minimum) < 0 ||
        float_compare(reading, entry->maximum) > 0) {
      selftest.power |= entry->bit;
    }
  }
}

/* The self-test of the supplies, entry 11: each of hmon_channels' bits
 * set in selftest.power reported while debugging, "POWER: " and the
 * channel's name (0x1837e). */
void selftest_power(char_slot run) {
  /* selftest_power's text, the original's 80 bytes. */
  enum { SELFTEST_POWER_TEXT_LENGTH = 80 };
  static const uint32_t CHANNELS =
      sizeof(hmon_channels) / sizeof(hmon_channels[0]);
  char text[SELFTEST_POWER_TEXT_LENGTH];

  if (run.value == 1 && !selftest_aborted()) {
    supplies_test();
  }
  if (!selftest_may_report(run.value) || run.value != 1 ||
      selftest.power == 0 || !debug_is_enabled()) {
    return;
  }
  for (uint32_t channel = 0; channel < CHANNELS; channel++) {
    const uint16_t bit = hmon_channels[channel].bit;

    if ((bit & selftest.power) != 0) {
      libc_copy(text, "POWER: ");
      strcat(text, hmon_channels[channel].name);
      selftest_report_fail(bit, text, 0);
    }
  }
}

/* The failures as selftest_summary_bit flags in selftest.summary;
 * reports the self-test failure condition set or cleared (0x17b58). */
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

/* Runs the ADC, hex DAC, FPGA and EFC tests, each with interrupts held
 * off, a failure bit for each (0x18ec2). */
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

/* One transfer to the ADC, QSPI device 1, of the word 0xe000; its reply
 * (0x18d5c). */
uint16_t qspi_adc_test_read(void) {
  static const uint16_t COMMAND = 0xe000;
  uint16_t command = COMMAND;
  uint16_t reply;
  qspi_buffers buffers = {.transmit = &command, .receive = &reply};

  qspi_transfer(QSPI_ADC, &buffers, 1);
  return reply;
}

/* Reads the ADC once and checks its reply: 1 when bit 10 is clear,
 * bits 4 and 3 repeat bits 0 and 1, and the byte in bits 9 to 2 is
 * neither 0 nor 0xff (0x18d92). */
uint8_t qspi_adc_test(void) {
  static const uint16_t BIT_10 = 0x400;
  static const uint8_t BYTE_ALL = 0xff;
  const uint16_t reply = qspi_adc_test_read();
  const uint8_t byte = (uint8_t)(reply >> 2);

  return (reply & BIT_10) == 0 && ((reply >> 4) & 1) == (reply & 1) &&
         ((reply >> 3) & 1) == ((reply >> 1) & 1) && byte != 0 &&
         byte != BYTE_ALL;
}

/* Clears the abort flag before a test (0x1876c). */
void selftest_begin(void) { selftest.abort = 0; }

/* Clears the abort flag after a test (0x18774). */
void selftest_end(void) { selftest.abort = 0; }

/* Runs test n of the table at 0x18906 twice, run 0 then 1, between
 * selftest_begin and selftest_end, with selftest.running set
 * (0x18794). */
void selftest_execute(selftest_test_number test) {
  selftest.running = 1;
  watchdog_pll++;
  /* The test indexes the table sign-extended, as the image's (0x187a8),
   * so that one past 0x7f reads below it. */
  selftest_test* entry = selftest_table[(int8_t)test];
  selftest_begin();
  entry((char_slot){.value = 0});
  entry((char_slot){.value = 1});
  selftest_end();
  watchdog_pll++;
  selftest.running = 0;
}

/* Self-test 9's count: fails unless one to three internal 1PPS came in
 * 1024 ticks (0x18952). */
void internal_pps_test(void) {
  static const uint32_t TICKS = 1024;

  selftest.internal_reference = 0;
  uint8_t before = internal_pps_count;
  psos_pause(TICKS);
  /* the count wraps; the difference is signed (0x1895e) */
  int8_t count = (int8_t)(internal_pps_count - before);
  if (count < MONITORS_PPS_LEAST || count > MONITORS_PPS_MOST) {
    selftest.internal_reference |= SELFTEST_FAILED;
  }
}

/* Runs self-test n for *TST? or :DIAGnostic:TEST? and leaves its result
 * and details for the reply (0x18f44), the 58503A 3704's tests by number,
 * each through selftest_execute; the counter's registers loaded again
 * after all of them and after the QSPI's.  A number past 13, its low
 * byte taken as signed, runs nothing and clears selftest.in_run. */
void selftest_run(uint32_t number) {
  /* the test is the argument's low byte */
  const selftest_test_number test = (selftest_test_number)number;

  selftest.in_run = 1;
  selftest.result = 0;
  selftest.details[0] = 0;
  selftest.details[1] = 0;
  selftest.test = test;
  if ((uint32_t)(int8_t)test > SELFTEST_TEST_INTERPOLATOR_SAMPLE) {
    selftest.in_run = 0;
    return;
  }
  selftest_execute(test);
  switch (test) {
    case SELFTEST_TEST_ALL:
      selftest.result = (uint16_t)selftest.summary;
      counter_load_registers();
      break;
    case SELFTEST_TEST_DISPLAY:
      selftest.result = 0;
      break;
    case SELFTEST_TEST_PROCESSOR:
      selftest.result = (uint16_t)selftest.cpu;
      break;
    case SELFTEST_TEST_RAM:
      selftest.result = (uint16_t)selftest.ram;
      selftest.details[0] = (uint16_t)selftest.ram_data_bits;
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
      counter_load_registers();
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
      selftest.result = selftest.gps;
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
      selftest.details[0] = (uint16_t)selftest.interpolator_readings[0];
      selftest.details[1] = (uint16_t)selftest.interpolator_readings[1];
      break;
    default:
      break;
  }
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

/* The self-test table's first check: the lamp test, which cannot fail
 * (0x19218). */
uint32_t lamp_test_command(void) {
  lamp_test();
  return 0;
}

/* "QSPI": the QSPI tests, lighting OP7 on a failure; selftest.qspi
 * (0x19174). */
uint32_t selftest_qspi_with_lamp(void) {
  selftest_qspi_run();
  if (selftest.qspi != 0) {
    board_leds_change(BOARD_LED_SELFTEST, 0);
  }
  return (uint16_t)selftest.qspi;
}

/* "FPGA": counter_selftest, lighting OP7 on a failure; selftest.fpga
 * (0x1919e). */
uint32_t selftest_fpga_with_lamp(void) {
  counter_selftest();
  if (selftest.fpga != 0) {
    board_leds_change(BOARD_LED_SELFTEST, 0);
  }
  return (uint16_t)selftest.fpga;
}

/* "INTERP": the interpolator test, lighting OP7 on a failure;
 * selftest.interpolator (0x191c4). */
uint32_t selftest_interpolator_with_lamp(void) {
  selftest.interpolator = 0;
  interpolator_selftest();
  if (selftest.interpolator != 0) {
    board_leds_change(BOARD_LED_SELFTEST, 0);
  }
  return (uint16_t)selftest.interpolator;
}

/* "INT_REF": the internal 1PPS test, lighting OP7 on a failure;
 * selftest.internal_reference (0x191ee). */
uint32_t selftest_internal_reference_with_lamp(void) {
  selftest.internal_reference = 0;
  internal_pps_test();
  if (selftest.internal_reference != 0) {
    board_leds_change(BOARD_LED_SELFTEST, 0);
  }
  return (uint16_t)selftest.internal_reference;
}

/* "GPS": gps_selftest_run, lighting OP7 on a failure; selftest.gps,
 * a long in the 58503A 3704, where the Z3805A's result is its word
 * (0x1926a). */
uint32_t selftest_gps_with_lamp(void) {
  gps_selftest_run();
  if (selftest.gps != 0) {
    board_leds_change(BOARD_LED_SELFTEST, 0);
  }
  return selftest.gps;
}

/* The DUART's test, and once more, logging, after 1024 ticks when it
 * fails; new in this image (0x1801c). */
void duart_test_retried(void) {
  static const uint32_t RETRY_TICKS = 0x400;

  duart_test(0);
  if (selftest.uart != 0) {
    psos_pause(RETRY_TICKS);
    watchdog_pll++;
    duart_test(1);
  }
}

/* "UART": duart_test_retried, lighting OP6 on a failure; a result with
 * more than its bit 0 set is printed and cut to that bit, which the
 * other images' do not; selftest.uart (0x19222). */
uint32_t selftest_uart_with_lamp(void) {
  static const uint16_t REPORTED_BIT = 1;

  duart_test_retried();
  if (selftest.uart != 0) {
    board_leds_change(BOARD_LED_DUART, 0);
  }
  if ((selftest.uart & ~REPORTED_BIT) != 0) {
    debug_printf("FAIL: UART, error code: %02XH\r\n", (uint16_t)selftest.uart);
    selftest.uart &= REPORTED_BIT;
  }
  return (uint16_t)selftest.uart;
}

/* "POWER": supplies_test, lighting OP7 on a failure; selftest.power
 * (0x1928e).  The seeding named it selftest_uart_with_lamp, the Z3805A
 * 3543B's, which its matches pair it with. */
uint32_t selftest_power_with_lamp(void) {
  supplies_test();
  if (selftest.power != 0) {
    board_leds_change(BOARD_LED_SELFTEST, 0);
  }
  return (uint16_t)selftest.power;
}

/* A check of the power-up self-test: nonzero for a failure, the code its
 * line prints. */
typedef uint32_t selftest_check_function(void);

/* An entry of the power-up self-test's table (0x18b5e), which a null run
 * ends. */
typedef struct selftest_check {
  selftest_check_function* run; /* +0x00 */
  char* name;                   /* +0x04 */
} selftest_check;

/* The image's table, which the C reads in place, so that a failure's log
 * entry keeps the name's address, the image's own, as its argument. */
static const selftest_check* const SELFTEST_CHECKS =
    (const selftest_check*)0x18b5e;

/* "FAIL: %s, error code: %02XH", the log entry's format, the image's
 * text by its address, which the entry keeps (0x18b08). */
static char* const SELFTEST_FAILURE_FORMAT = (char*)0x18c1b;

/* selftest_print_failure's "FAIL: " and the longest check's name,
 * "INT_REF", with its end: the original's 14 bytes. */
#define SELFTEST_FAILURE_LENGTH 14

/* The power-up self-test: runs each check, printing the name and code of
 * each that fails and logging it, then summarizes and logs a failure;
 * clear_eeprom clears the EEPROM's result first (0x18a9c). */
void selftest_print_failure(char clear_eeprom) {
  char text[SELFTEST_FAILURE_LENGTH];

  selftest.passed = 1;
  if (clear_eeprom != 0) {
    selftest.eeprom = 0;
  }
  selftest.running = 1;
  for (const selftest_check* check = SELFTEST_CHECKS; check->run != NULL;
       check++) {
    const uint32_t code = check->run();

    if (code != 0) {
      selftest.passed = 0;
      libc_copy(text, "FAIL: ");
      strcat(text, check->name);
      debug_printf("%s, error code: %02XH\r\n", text, code);
      log_write(SELFTEST_FAILURE_FORMAT, check->name, code);
    }
    watchdog_pll++;
  }
  selftest_summary();
  selftest.result = (uint16_t)selftest.summary;
  selftest.test = SELFTEST_TEST_ALL;
  selftest.running = 0;
  if (selftest.passed == 0) {
    log_event(LOG_SELFTEST_FAILED);
  }
}

/* Lights the latch's five lamps, then none, then each bit up to them in
 * turn, then asks hmon_task to restore them; lamps_write leaves the
 * latch alone meanwhile (0x1899c).  The Z3815A 4010's walks all eight of
 * the latch's low bits. */
void lamp_test(void) {
  /* How long each step lights its lamps. */
  static const uint32_t STEP_TICKS = 307;

  selftest.lamp_testing = 1;
  latch_bits_set(LATCH_LAMPS, 1);
  psos_pause(STEP_TICKS);
  latch_bits_set(LATCH_LAMPS, 0);
  psos_pause(STEP_TICKS);
  watchdog_pll++;
  for (uint16_t lamp = 1; lamp <= LATCH_LAMPS; lamp <<= 1) {
    latch_lamps_write((latch_bit)lamp);
    psos_pause(STEP_TICKS);
    watchdog_pll++;
  }
  selftest.lamp_testing = 0;
  lamps_request();
}

/* The self-test of the DUART, entry 5: its registers, counter and both
 * channels, each failure reported while debugging; a reported run then
 * keeps only the register failure (0x1804c, 0x18118). */
void selftest_uart(char_slot run) {
  if (run.value == 1 && !selftest_aborted()) {
    duart_test_retried();
  }
  if (selftest_may_report(run.value) && run.value == 1) {
    if (selftest.uart != 0 && debug_is_enabled()) {
      selftest_report_fail(selftest.uart & SELFTEST_UART_REGISTER,
                           "UART: register read/write", 0);
      selftest_report_fail(selftest.uart & SELFTEST_UART_COUNTER,
                           "UART: counter", 0);
      selftest_report_fail(selftest.uart & SELFTEST_UART_CHANNEL_A_DATA,
                           "UART: channel A data", 0);
      selftest_report_fail(selftest.uart & SELFTEST_UART_CHANNEL_A_TIMEOUT,
                           "UART: channel A timeout", 0);
      selftest_report_fail(selftest.uart & SELFTEST_UART_CHANNEL_B_DATA,
                           "UART: channel B data", 0);
      selftest_report_fail(selftest.uart & SELFTEST_UART_CHANNEL_B_TIMEOUT,
                           "UART: channel B timeout", 0);
    }
    selftest.uart &= SELFTEST_UART_REGISTER;
  }
}

/* Runs each self-test in selftest_list with run 1, counting
 * watchdog_pll before the first and after each. */
void selftest_run_all_counted(void) {
  watchdog_pll++;
  for (selftest_entry* test = selftest_list; *test != NULL; test++) {
    (*test)((char_slot){.value = 1, .next = 1});
    watchdog_pll++;
  }
}

/* Runs each self-test in selftest_list with run 0, the second slot's
 * char 1. */
void selftest_run_all(void) {
  for (selftest_entry* test = selftest_list; *test != NULL; test++) {
    (*test)((char_slot){.value = 0, .next = 1});
  }
}

/* qspi_hex_dac_test's words, the hex DAC's 36 bits, and its patterns. */
#define SELFTEST_HEX_DAC_WORDS 3

#define SELFTEST_HEX_DAC_PATTERNS 2

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
