/* duartb.c's functions that are this image's own: the rest are
 * common/duartb.c's, the same in every image (docs/firmware/rewrite.md,
 * "Shared C").  From the 58503A 3704's rewritten/duartb.c: of its functions,
 * those the Z3816A's code carries (identical or relocated by image_diff, or
 * changed with the same instructions), with the static helpers they use, each
 * the same here but where its comment says otherwise
 * (docs/firmware/z3816a-4001.md, "The link check").  Its comments' addresses
 * are the Z3816A's.  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype. */

/* At reset, before interrupts: sends "DUART" through channel B in local
 * loopback by polling and checks each character back (0x2dc9a). */
void duartb_loopback_check(void) {
  char* text = "DUART";

  (void)duart.b.mr;
  duart.b.mr = MR2_MODE_LOCAL_LOOPBACK | DUART_MR2_ONE_STOP_BIT;
  loopback_send_next = text;
  loopback_expect_next = text;
  duart.b.cr = CR_TX_ENABLE | CR_RX_ENABLE;
  duart.b.rb_tb = *loopback_send_next++;
  while (*loopback_expect_next != 0 && selftest.uart == 0) {
    for (int32_t spins = DUART_LOOPBACK_CHECK_SPINS;
         spins > 0 && (duart.b.sr_csr & SR_RXRDY) == 0; spins--) {
    }
    if ((duart.b.sr_csr & SR_RXRDY) != 0) {
      char expected = *loopback_expect_next++;

      if (duart.b.rb_tb != expected) {
        selftest.uart |= SELFTEST_UART_CHANNEL_B_DATA;
      }
      if (*loopback_send_next != 0) {
        duart.b.rb_tb = *loopback_send_next++;
      }
    } else {
      selftest.uart |= SELFTEST_UART_CHANNEL_B_TIMEOUT;
    }
  }
  duart.b.cr = CR_RESET_RECEIVER;
  duart.b.cr = CR_RESET_TRANSMITTER;
  (void)duart.b.mr;
  duart.b.mr = DUART_MR2_ONE_STOP_BIT;
}

/* Channel B's loopback test, which the Z3816A's DUART self-test calls
 * after channel A's: a bare return (0x2d8be), the image keeping no
 * channel B port. */
void duart_loopback_test_b(void) {}

/* With the serial port restricted, eight bits and a parity cannot both
 * be: a parity set clears eight bits (0x40092).  The 58503A 3704's, on
 * the DUART channel B settings the image keeps and nothing else here
 * reads. */
void duartb_parity_restrict(void) {
  if (nv_settings.serial_restricted != 0 && duartb_settings.eight_bits == 1 &&
      duartb_settings.parity != SERIAL_PARITY_NONE) {
    duartb_settings.eight_bits = 0;
  }
}

/* With the serial port restricted, eight bits set clears the parity
 * (0x400b4).  The 58503A 3704's. */
void duartb_eight_bits_restrict(void) {
  if (nv_settings.serial_restricted != 0 && duartb_settings.eight_bits == 1 &&
      duartb_settings.parity != SERIAL_PARITY_NONE) {
    duartb_settings.parity = SERIAL_PARITY_NONE;
  }
}

/* The loopback test's receive on channel B (0x2da2e).  The 58503A
 * 3704's. */
void duart_test_rx_b(void) {
  duart_test_receive(&duart.b, SELFTEST_UART_CHANNEL_B_DATA);
}

/* Channel B's receive interrupt, which nothing here enables: drains
 * the receiver, keeping its errors and an overrun, which it resets, in
 * duartb_receive_errors (0x2db42).  The image's own, the 58503A 3704's
 * duartb_isr with no ring to fill. */
void duartb_isr(void) {
  while ((duart.b.sr_csr & SR_RXRDY) != 0) {
    duartb_receive_errors |= duart.b.sr_csr & DUART_RECEIVE_ERRORS;
    (void)duart.b.rb_tb;
  }
  if ((duart.b.sr_csr & SR_OVERRUN) != 0) {
    duartb_receive_errors |= SR_OVERRUN;
    duart.b.cr = CR_RESET_ERROR;
  }
}

/* Channel B's transmit interrupt: masks the interrupt in the shadow
 * alone, which nothing here writes back (0x2dbd2).  The image's own. */
void duartb_transmit_interrupt(void) { duart_shadow.imr &= ~IMR_TXRDYB; }
