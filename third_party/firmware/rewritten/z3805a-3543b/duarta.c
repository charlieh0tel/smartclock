/* DUART channel A, the link to the GPS engine: its driver (the
 * firmware's drta_ and duarta_ routines), its interrupt and its self-
 * tests; its state is docs/firmware/structures.md's "DUART channel A's
 * driver", the registers mc68681.h's, written by hand in place of the
 * decompiler's (docs/firmware/README.md, "Hand-written C";
 * docs/firmware/modules.md, `devices/duarta`).  The exported C includes
 * this file after its own, so the types and prototypes it declares are
 * in scope; each function here keeps its generated prototype. */

/* The loopback test on channel A, the GPS receiver's (0x1e2c0). */
void duart_loopback_test_a(void) {
  /* The DUART self-test's bit in selftest.uart. */
  static const int16_t TEST_A_TIMEOUT = 0x08;
  const duart_loopback_channel channel = {
      &duart.a,     &duart_shadow.mr2a,    IMR_RXRDYA | IMR_TXRDYA,
      &duarta.held, &duarta.loopback_test, TEST_A_TIMEOUT,
  };

  duart_loopback_test(&channel);
}

/* The loopback test's receive on channel A (0x1e6be). */
void duart_test_rx_a(void) {
  /* The DUART self-test's bit in selftest.uart. */
  static const int16_t TEST_A_MISMATCH = 0x04;

  duart_test_receive(&duart.a, TEST_A_MISMATCH);
}

/* The loopback test's transmit on channel A (0x1e706). */
void duart_test_tx_a(void) { duart_test_transmit(&duart.a, IMR_TXRDYA); }

/* Sends a message to the GPS receiver on DUART channel A by interrupt,
 * waiting until it is out; only the GPS task may, and another gets 1
 * back (0x1e18a). */
uint8_t drta_send_message(char* message, int16_t length) {
  uint16_t sr;

  if (psos_my_pid() != task_gpsm) {
    return 1;
  }
  psos_get_v(SIG_SOQA, 0, 0);
  duarta.transmit_next = message;
  duarta.transmit_count = length;
  sr = set_interrupt_mask(CPU_IPL_6);
  if (!duarta.held) {
    duart_shadow.imr |= IMR_TXRDYA;
    duart.isr_imr = duart_shadow.imr;
  }
  set_interrupt_mask(sr);
  if ((uint8_t)psos_wait_v(SIG_SOQA, 0, 0, 0) != 0) {
    fatal_error("wait_v SIG_SOQA error in drta_send_message");
  }
  return 0;
}

/* Whether DUART channel A's receive ring holds a byte (0x1e222). */
bool duarta_rx_pending(void) { return duarta_ring.write != duarta_ring.read; }

/* Reads a byte from the GPS receiver's ring on DUART channel A, waiting
 * for one; reports, and hands back through overrun, whether the
 * receiver overran since the last (0x1e238). */
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

/* Channel A's receive interrupt: takes each byte the GPS receiver sent
 * into the ring, signaling the GPS task for each, and notes the ring's
 * least room and an overrun.  Returns the overrun bit of the status
 * register (0x1e766). */
uint8_t duarta_isr(void) {
  uint8_t free_bytes;
  uint8_t status;

  while ((duart.a.sr_csr & SR_RXRDY) != 0) {
    duarta_ring.data[duarta_ring.write++] = duart.a.rb_tb;
    if (task_gpsm != 0 && (uint8_t)psos_signal_v(task_gpsm, SIG_SIQ) != 0) {
      fatal_error("DUARTA isr signaling SIG_SIQ");
    }
  }
  free_bytes = duarta_ring.read - duarta_ring.write;
  if (free_bytes < duarta.least_free) {
    duarta.least_free = free_bytes;
  }
  status = duart.a.sr_csr;
  if ((status & SR_OVERRUN) != 0) {
    duarta.overrun = 1;
    duart.a.cr = CR_RESET_ERROR;
  }
  return status & SR_OVERRUN;
}

/* Channel A's transmit interrupt: sends the message's next byte, or at
 * its end turns the interrupt off and signals the GPS task
 * (0x1e9ca). */
void duarta_tx_interrupt(void) {
  if (duarta.transmit_count != 0) {
    duart.a.rb_tb = *duarta.transmit_next++;
    duarta.transmit_count--;
  } else {
    duart_shadow.imr &= ~IMR_TXRDYA;
    duart.isr_imr = duart_shadow.imr;
    psos_signal_v(task_gpsm, SIG_SOQA);
  }
}
