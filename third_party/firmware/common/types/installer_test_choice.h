/* installer_test_choice and the types beside it, as the images' smartclock.h
 * declare them (each includes this header where they stand):
 * docs/firmware/rewrite.md, "Shared C".  Where a comment cites code, it
 * is the Z3805A 3543B's, which the 58503A's copy of the comment came
 * from. */

/* The installer's :DIAGnostic:TEST? choices, its parameter's value,
 * a short: the jump table at installer_scpi_query_DIAGnostic_TEST+0x28, which
 * installer_scpi_query_DIAGnostic_TEST indexes by the choice less 4
 * from installer_scpi_query_DIAGnostic_TEST+0x30
 * (installer_scpi_query_DIAGnostic_TEST+0x1e); any other choice is an execution
 * error.  docs/firmware/restart.md, "The installer". */
typedef enum __attribute__((packed)) installer_test_choice {
  INSTALLER_TEST_SUMMARY = 0,  /* bits 0 to 3: checksum, CPU, RAM, DUART */
  INSTALLER_TEST_CHECKSUM = 1, /* boot_selftest.checksum's lane flags */
  INSTALLER_TEST_CPU = 2,
  INSTALLER_TEST_RAM = 3, /* and its failed data bits and address lines */
  INSTALLER_TEST_UART = 4,
} installer_test_choice;

/* The boot code's self-test results, boot_selftest: a
 * checksum word, then the fields of selftest_results' first 14 bytes in
 * their order.  The installer's :DIAGnostic:TEST? reads them. */
typedef struct boot_selftest_results {
  int16_t checksum; /* +0x00: boot_reset's byte-lane sums: 2 and 1 for the two
                       lanes of boot_memcpy+0xf462 to rom_lane_sums_low+0x3, 8
                       and 4 for interpolator_calibration_init to 0x7ffff */
  int16_t cpu;      /* +0x02: 1 if the CPU test fails */
  uint16_t
      ram; /* +0x04: bit 0 a pattern failed, bit 1 an address line aliases */
  uint16_t ram_data_bits;     /* +0x06: the bits that failed a pattern */
  uint32_t ram_address_lines; /* +0x08: the address lines that alias */
  int16_t uart;      /* +0x0c: boot_duart_early_init's IVR and loopback checks;
                        lights OP6 */
  uint8_t lamp_mask; /* +0x0e: the DUART output bits boot_reset resets; nothing
                        reads it */
  uint8_t field_0f;
} boot_selftest_results;
