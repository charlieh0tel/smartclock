/* DUART channel A, the link to the GPS engine: its driver (the
 * firmware's drta_ and duarta_ routines), its interrupt and its self-
 * tests; its state is docs/firmware/structures.md's "DUART channel A's
 * driver", the registers mc68681.h's, written by hand in place of the
 * decompiler's (docs/firmware/README.md, "Hand-written C";
 * docs/firmware/modules.md, `devices/duarta`).  The exported C includes
 * this file after its own, so the types and prototypes it declares are
 * in scope; each function here keeps its generated prototype. */

/* The loopback test on channel A, the GPS receiver's (0x1dfc8). */
void duart_loopback_test_a(void) {
  const duart_loopback_channel channel = {
      &duart.a,     &duart_shadow.mr2a,    IMR_RXRDYA | IMR_TXRDYA,
      &duarta.held, &duarta.loopback_test, SELFTEST_UART_CHANNEL_A_TIMEOUT,
  };

  duart_loopback_test(&channel);
}

/* The loopback test's receive on channel A (0x1e3c6). */
void duart_test_rx_a(void) {
  duart_test_receive(&duart.a, SELFTEST_UART_CHANNEL_A_DATA);
}

/* Reads a byte from the GPS receiver's ring on DUART channel A, waiting
 * for one; reports, and hands back through overrun, whether the
 * receiver overran since the last, in this image's spelling (0x1df40). */
uint8_t drta_get_byte(char* overrun) {
  if (overrun != 0) {
    *overrun = duarta.overrun;
  }
  if (duarta.overrun) {
    task_debug_message("DRTA receiver overrun ocurred\r\n");
  }
  duarta.overrun = 0;
  psos_get_v(SIG_SIQ, 0, 0);
  if (duarta_ring.write == duarta_ring.read &&
      (uint8_t)psos_wait_v(SIG_SIQ, 0, 0, 0) != 0) {
    fatal_error("wait_v SIG_SIQ error in drta_get_byte");
  }
  return duarta_ring.data[duarta_ring.read++];
}
