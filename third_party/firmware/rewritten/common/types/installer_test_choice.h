/* installer_test_choice, as the images' smartclock.h
 * declare it (each includes this header where it stands):
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
