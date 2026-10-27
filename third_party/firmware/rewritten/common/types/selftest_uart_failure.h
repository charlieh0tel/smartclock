/* selftest_uart_failure and the types beside it, as the images' smartclock.h
 * declare them (each includes this header where they stand):
 * docs/firmware/rewrite.md, "Shared C".  Where a comment cites code, it
 * is the Z3805A 3543B's, which the 58503A's copy of the comment came
 * from. */

/* selftest_results.uart, the DUART test's failures, by selftest_uart's
 * texts: duart_test's IVR and counter checks (duart.c, duart_test)
 * and each channel's loopback (duarta.c, duartb.c). */
typedef enum __attribute__((packed)) selftest_uart_failure {
  SELFTEST_UART_REGISTER = 0x01,
  SELFTEST_UART_COUNTER = 0x02,
  SELFTEST_UART_CHANNEL_A_DATA = 0x04,
  SELFTEST_UART_CHANNEL_A_TIMEOUT = 0x08,
  SELFTEST_UART_CHANNEL_B_DATA = 0x10,
  SELFTEST_UART_CHANNEL_B_TIMEOUT = 0x20,
} selftest_uart_failure;
