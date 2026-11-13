/* The Z3817A 3752's DUART channel B transmit interrupt, which the export
 * needs hand-written to compile (docs/firmware/z3817a-3752.md, "The
 * export and the checks"), and the loopback test's receive on channel
 * B, which the function check finds the decompiler's reading wrong for
 * ("The function check"); and the oscillator's link on it, its receive
 * interrupt, ring and send, and its loopback tests, read beside the
 * Z3815A 4010's ("DUART channel B: the oscillator").  Written by hand in
 * place of the
 * decompiler's (docs/firmware/README.md, "Hand-written C"); each keeps its
 * generated prototype. */

/* DUART channel B's transmit interrupt: the next of duartb's characters
 * while any is left, else the channel's transmit interrupt off and
 * duartb_task signaled that the transmission is done (0x2d9f2).  The
 * Z3815A 4010's also sends the time code's block and keeps the flow
 * control. */
void duartb_transmit_interrupt(void) {
  if (duartb.transmit_count != 0) {
    duart.b.rb_tb = *duartb.transmit_next++;
    duartb.transmit_count--;
  } else {
    duart_shadow.imr &= ~IMR_TXRDYB;
    duart.isr_imr = duart_shadow.imr;
    psos_signal_v(duartb_task, SIG_SOQ);
  }
}

/* The loopback test's receive on channel B (0x2d7dc). */
void duart_test_rx_b(void) {
  duart_test_receive(&duart.b, SELFTEST_UART_CHANNEL_B_DATA);
}

/* The loopback test on channel B, the oscillator's, what came back stored
 * in duartb_loopback_received (0x2d450). */
void duart_loopback_test_b(void) {
  const duart_loopback_channel channel = {
      &duart.b,     &duart_shadow.mr2b,    IMR_RXRDYB | IMR_TXRDYB,
      &duartb.held, &duartb.loopback_test, SELFTEST_UART_CHANNEL_B_TIMEOUT,
  };

  duart_loopback_test(&channel, duartb_loopback_received);
}

/* At reset, before interrupts: sends "DUART" through channel B in local
 * loopback by polling and checks each character back (0x2db28).  The
 * Z3815A 4010's but that it points loopback_receive_next at
 * duartb_loopback_received, which nothing here stores through. */
void duartb_loopback_check(void) {
  char* text = "DUART";

  (void)duart.b.mr;
  duart.b.mr = MR2_MODE_LOCAL_LOOPBACK | DUART_MR2_ONE_STOP_BIT;
  loopback_receive_next = duartb_loopback_received;
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

/* DUART channel B's receive interrupt, from the DUART's handler
 * (0x2d8f6): each byte received, its receive errors kept in
 * duartb_receive_errors, goes into duartb_ring while duartb_task is set,
 * a full ring marked an overrun, and the ring's least free space is kept;
 * duartb_task is signaled once if any went in.  An overrun in the status
 * register is marked and reset.  Returns the status register's overrun
 * bit.  The Z3815A 4010's duartb_isr, without the console's ctrl-C or the
 * flow control. */
uint8_t duartb_receive_interrupt(void) {
  bool received = false;
  uint8_t status;

  while ((duart.b.sr_csr & SR_RXRDY) != 0) {
    uint8_t character;
    uint8_t free_bytes;

    duartb_receive_errors |= duart.b.sr_csr & DUART_RECEIVE_ERRORS;
    character = duart.b.rb_tb;
    if (duartb_task != 0) {
      duartb_ring.data[duartb_ring.write++] = character;
      if (duartb_ring.write == duartb_ring.read) {
        duartb_receive_errors |= SR_OVERRUN;
      }
      received = true;
    }
    free_bytes = duartb_ring.read - duartb_ring.write;
    if (free_bytes < duartb.least_free) {
      duartb.least_free = free_bytes;
    }
  }
  if (received && (uint8_t)psos_signal_v(duartb_task, SIG_SIQ) != 0) {
    fatal_error("DUARTB isr signaling SIG_SIQ");
  }
  status = duart.b.sr_csr;
  if ((status & SR_OVERRUN) != 0) {
    duartb_receive_errors |= SR_OVERRUN;
    duart.b.cr = CR_RESET_ERROR;
  }
  return status & SR_OVERRUN;
}

/* Sends count characters of text to DUART channel B by interrupt and
 * waits until duartb_transmit_interrupt signals them out; 0, or 1 and
 * nothing sent when called from any task but duartb_task.  While
 * duartb.held is set it leaves the transmit interrupt off.  Nothing calls
 * it (0x2dfb6). */
uint8_t drtb_send_message(char* text, int16_t count) {
  uint16_t sr;

  if (psos_my_pid() != duartb_task) {
    return 1;
  }
  psos_get_v(SIG_SOQ, 0, 0);
  duartb.transmit_next = text;
  duartb.transmit_count = count;
  sr = set_interrupt_mask(CPU_IPL_6);
  if (!duartb.held) {
    duart_shadow.imr |= IMR_TXRDYB;
    duart.isr_imr = duart_shadow.imr;
  }
  set_interrupt_mask(sr);
  if ((uint8_t)psos_wait_v(SIG_SOQ, 0, 0, 0) != 0) {
    fatal_error("wait_v SIG_SOQ error in drtb_send_message");
  }
  return 0;
}

/* Whether DUART channel B's ring holds a character (0x2e04e). */
uint8_t duartb_receive_ready(void) {
  return duartb_ring.write != duartb_ring.read;
}

/* Takes the next character of DUART channel B's ring, waiting for the
 * receive interrupt's signal while it is empty (0x2e064): first, unless
 * overrun is NULL, whether the ring overran through it, and any receive
 * errors reported while debugging and cleared. */
char duartb_getc(uint8_t* overrun) {
  if (overrun != NULL) {
    *overrun = (duartb_receive_errors & SR_OVERRUN) != 0;
  }
  if (duartb_receive_errors != 0) {
    task_debug_message("DRTB receiver error\r\n");
  }
  duartb_receive_errors = 0;
  psos_get_v(SIG_SIQ, 0, 0);
  if (duartb_ring.write == duartb_ring.read &&
      (uint8_t)psos_wait_v(SIG_SIQ, 0, 0, 0) != 0) {
    fatal_error("wait_v SIG_SIQ error in drtb_get_byte");
  }
  return duartb_ring.data[duartb_ring.read++];
}

/* With the serial port restricted, eight bits and a parity cannot both
 * be: a parity set clears eight bits (0x41350). */
void duartb_parity_restrict(void) {
  if (nv_settings.serial_restricted != 0 && duartb_settings.eight_bits == 1 &&
      duartb_settings.parity != SERIAL_PARITY_NONE) {
    duartb_settings.eight_bits = 0;
  }
}

/* With the serial port restricted, eight bits set clears the parity
 * (0x41372). */
void duartb_eight_bits_restrict(void) {
  if (nv_settings.serial_restricted != 0 && duartb_settings.eight_bits == 1 &&
      duartb_settings.parity != SERIAL_PARITY_NONE) {
    duartb_settings.parity = SERIAL_PARITY_NONE;
  }
}

/* Allows the SCI's asynchronous sends while time codes are sent
 * continuously, else forbids them once none is pending (0x2f42a): the
 * Z3815A's code for channel B's, on the SCI's driver here. */
void drtb_async_allowed_update(void) {
  if (tcode_continuous) {
    sci.async_allowed = 1;
  } else if (!sci.async_pending) {
    sci.async_allowed = 0;
  }
}

/* The DUART's output port bit 0, which drtb_async_allow resets. */
static const uint8_t DUART_OUTPUT_BIT_0 = 0x01;

/* Resets the DUART's output port bit 0 (0x2d006), the bytes of the
 * Z3815A's routine that allows channel B's asynchronous sends, which
 * writes RAM where this one writes the DUART's ROPBC; nothing calls it. */
void drtb_async_allow(void) { duart.stop_ropbc = DUART_OUTPUT_BIT_0; }
