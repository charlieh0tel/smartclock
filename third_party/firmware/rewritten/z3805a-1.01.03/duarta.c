/* DUART channel A, the GPS engine's link: its self-test's loopback
 * receive and loopback test, and drta_get_byte, whose overrun message is
 * this image's text, written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype.  The rest of the channel's driver is common/'s
 * (docs/firmware/rewrite.md, "Shared C"). */

/* The loopback test's receive on channel A (0x1e882). */
void duart_test_rx_a(void) {
  duart_test_receive(&duart.a, SELFTEST_UART_CHANNEL_A_DATA);
}

/* The loopback test on channel A, the GPS engine's, what comes back
 * kept at loopback_received_a (0x1e3a8). */
void duart_loopback_test_a(void) {
  const duart_loopback_channel channel = {
      &duart.a,     &duart_shadow.mr2a,    IMR_RXRDYA | IMR_TXRDYA,
      &duarta.held, &duarta.loopback_test, SELFTEST_UART_CHANNEL_A_TIMEOUT,
  };

  duart_loopback_test(&channel, loopback_received_a);
}

/* Reads a byte from the GPS receiver's ring on DUART channel A, waiting
 * for one; reports, and hands back through overrun, whether the
 * receiver overran since the last, in this image's spelling (0x1e320). */
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
