/* The functions of fpga.c the images share, the same in each
 * (docs/firmware/rewrite.md, "Shared C"); each image's own
 * rewritten/fpga.c describes the module and holds the rest.  Written by
 * hand in place of the decompiler's (docs/firmware/README.md,
 * "Hand-written C"); each keeps its generated prototype. */

/* Verifies a configuration against the FPGA's readback, the first chip;
 * false for one past fpga_configurations' last. */
uint8_t fpga_verify(fpga_configuration_index configuration,
                    char with_task_name) {
  if (configuration > fpga_configurations.last) {
    return 0;
  }
  return fpga_verify_chip(configuration, with_task_name, FPGA_CHIP_FIRST);
}

/* Writes an FPGA register. */
void fpga_write(uint8_t reg, uint8_t value) { fpga_port[reg] = value; }

/* A byte from two FPGA registers' low nibbles, the next register's the
 * high one.  The mask and the shift drop their upper nibbles, which in
 * the Z3815A are the seconds count at the even address and the P84
 * counter's count at the odd one (docs/firmware/z3815a-trace.md,
 * "Register port" and "The P84 counter"): the firmware never reads the
 * P84 counter. */
uint8_t fpga_read_byte(fpga_register reg) {
  static const uint8_t NIBBLE = 0xf;
  static const uint8_t NIBBLE_BITS = 4;
  const uint8_t high = fpga_read(reg + 1);
  const uint8_t low = fpga_read(reg);

  return (low & NIBBLE) | high << NIBBLE_BITS;
}

/* Whether any of the bits given are set in an FPGA register; called by
 * nothing. */
bool fpga_bit_test(uint8_t bits, char reg) {
  return (bits & fpga_read(reg)) != 0;
}

/* FPGA register 0's high nibble, a count of seconds modulo 16, read
 * until two reads agree or 20 more pairs have not. */
char fpga_read_seconds(void) {
  static const int32_t RETRIES = 20;
  static const uint8_t SECONDS_SHIFT = 4;
  /* fpga_read's register 0, the counter's status */
  uint8_t first = fpga_read(FPGA_REGISTER_STATUS) >> SECONDS_SHIFT;
  uint8_t second = fpga_read(FPGA_REGISTER_STATUS) >> SECONDS_SHIFT;

  for (int32_t retries = RETRIES; first != second && retries > 0; retries--) {
    first = fpga_read(FPGA_REGISTER_STATUS) >> SECONDS_SHIFT;
    second = fpga_read(FPGA_REGISTER_STATUS) >> SECONDS_SHIFT;
  }
  return first;
}
