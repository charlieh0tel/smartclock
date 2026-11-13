/* DUART channel A, the Oncore link, the selected reference's stream
 * (docs/firmware/z3817a-3752.md, "The Oncore link"): of its routines, those
 * the function check finds the decompiler's reading wrong for, the
 * Z3817A 3752's own (docs/firmware/z3817a-3752.md, "The function
 * check"), and its loopback test, read beside the Z3815A 4010's.  Written by
 * hand in place of the decompiler's (docs/firmware/README.md, "Hand-written
 * C"); each keeps its generated prototype. */

/* The loopback test's receive on channel A (0x2d7b2). */
void duart_test_rx_a(void) {
  duart_test_receive(&duart.a, SELFTEST_UART_CHANNEL_A_DATA);
}

/* The loopback test on channel A, the Oncore's, what came back stored in
 * duarta_loopback_received (0x2d2e0). */
void duart_loopback_test_a(void) {
  const duart_loopback_channel channel = {
      &duart.a,     &duart_shadow.mr2a,    IMR_RXRDYA | IMR_TXRDYA,
      &duarta.held, &duarta.loopback_test, SELFTEST_UART_CHANNEL_A_TIMEOUT,
  };

  duart_loopback_test(&channel, duarta_loopback_received);
}

/* Reads a byte from the GPS receiver's ring on DUART channel A, waiting
 * for one; reports, and hands back through overrun, whether the
 * receiver overran since the last, in this image's spelling (0x2d258). */
uint8_t drta_get_byte(char* overrun) {
  if (overrun != 0) {
    *overrun = duarta.overrun;
  }
  if (duarta.overrun) {
    task_debug_message("DRTA receiver overrun occurred\r\n");
  }
  duarta.overrun = 0;
  psos_get_v(SIG_SIQ, 0, 0);
  if (duarta_ring.write == duarta_ring.read &&
      (uint8_t)psos_wait_v(SIG_SIQ, 0, 0, 0) != 0) {
    fatal_error("wait_v SIG_SIQ error in drta_get_byte");
  }
  return duarta_ring.data[duarta_ring.read++];
}
