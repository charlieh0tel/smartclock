/* The self-test's checks of the FPGA, written by hand in place of the
 * decompiler's (docs/firmware/README.md, "Hand-written C";
 * docs/firmware/modules.md, `applications/selftest`).  The exported C
 * includes this file after its own, so the types and prototypes it
 * declares are in scope; each function here keeps its generated
 * prototype. */

/* counter_selftest's bit in selftest.fpga: the counters read nonzero
 * after a preset. */
static const int16_t FPGA_TEST_NOT_PRESET = 1;

/* The self-test of the FPGA's counters, entry 7: runs counter_selftest
 * when run is 1 and the self-test has not been aborted, and reports a
 * failure while debugging (0x17c3e). */
void selftest_fpga(char_slot run) {
  /* selftest_test's run, 1 to run the test */
  if (run.value == 1 && !selftest_aborted()) {
    counter_selftest();
  }
  if (selftest_may_report(run.value) && run.value == 1 && selftest.fpga != 0 &&
      debug_is_enabled()) {
    selftest_report_fail(selftest.fpga & FPGA_TEST_NOT_PRESET,
                         "FPGA: counters not preset", 0);
  }
}

/* counter_selftest, lighting the failure lamp on a failure; the check
 * "FPGA" in selftest_print_failure's table; returns selftest.fpga
 * (0x18cc6). */
uint32_t selftest_fpga_with_lamp(void) {
  counter_selftest();
  if (selftest.fpga != 0) {
    /* the DUART's output port bit 7, OP7, the failure lamp */
    duart_output_bits(0x80, 0);
  }
  return (uint16_t)selftest.fpga;
}
