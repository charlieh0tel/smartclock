/* DUART channel B, the console and SCPI port: its driver (the firmware's
 * drtb_ and duartb_ routines), its interrupt, its settings' hooks and
 * its self-tests; its state is docs/firmware/structures.md's "DUART
 * channel B's driver", the registers mc68681.h's, written by hand in
 * place of the decompiler's (docs/firmware/README.md, "Hand-written C";
 * docs/firmware/modules.md, `devices/duartb`).  The exported C includes
 * this file after its own, so the types and prototypes it declares are
 * in scope; each function here keeps its generated prototype. */

/* The DUART self-test's bits in selftest.uart that several functions
 * set; the rest are in the functions that set them. */
static const int16_t DUART_TEST_B_MISMATCH = 0x10;

static const int16_t DUART_TEST_B_TIMEOUT = 0x20;

/* The loopback test on channel B, the host port (0x1e41a). */
void duart_loopback_test_b(void) {
  const duart_loopback_channel channel = {
      &duart.b,
      &duart_shadow.mr2b,
      IMR_RXRDYB | IMR_TXRDYB,
      &duartb.reconfiguring,
      &duartb.loopback_test,
      DUART_TEST_B_TIMEOUT,
  };

  duart_loopback_test(&channel);
}

/* The loopback test's receive on channel B (0x1e6e2). */
void duart_test_rx_b(void) {
  duart_test_receive(&duart.b, DUART_TEST_B_MISMATCH);
}

/* The loopback test's transmit on channel B (0x1e736). */
void duart_test_tx_b(void) { duart_test_transmit(&duart.b, IMR_TXRDYB); }

/* At reset, before interrupts: sends "DUART" through channel B in local
 * loopback by polling and checks each character back (0x1ebe4). */
void duartb_loopback_check(void) {
  /* How long duartb_loopback_check spins for each character back. */
  static const int32_t LOOPBACK_CHECK_SPINS = 3000;
  char* text = "DUART";

  (void)duart.b.mr;
  duart.b.mr = MR2_MODE_LOCAL_LOOPBACK | DUART_MR2_ONE_STOP_BIT;
  loopback_send_next = text;
  loopback_expect_next = text;
  duart.b.cr = CR_TX_ENABLE | CR_RX_ENABLE;
  duart.b.rb_tb = *loopback_send_next++;
  while (*loopback_expect_next != 0 && selftest.uart == 0) {
    for (int32_t spins = LOOPBACK_CHECK_SPINS;
         spins > 0 && (duart.b.sr_csr & SR_RXRDY) == 0; spins--) {
    }
    if ((duart.b.sr_csr & SR_RXRDY) != 0) {
      char expected = *loopback_expect_next++;

      if (duart.b.rb_tb != expected) {
        selftest.uart |= DUART_TEST_B_MISMATCH;
      }
      if (*loopback_send_next != 0) {
        duart.b.rb_tb = *loopback_send_next++;
      }
    } else {
      selftest.uart |= DUART_TEST_B_TIMEOUT;
    }
  }
  duart.b.cr = CR_RESET_RECEIVER;
  duart.b.cr = CR_RESET_TRANSMITTER;
  (void)duart.b.mr;
  duart.b.mr = DUART_MR2_ONE_STOP_BIT;
}

/* A no-op echo, a bare rts: duart_init and duartb_reconfigure put it in
 * duartb.echo when the settings' echo is off (0x1d570). */
void duartb_echo_none(char c) {}

/* DUART channel B's mode registers from the serial settings: parity,
 * data bits and stop bits (0x1d594). */
void duartb_mode_from_settings(void) {
  /* The 68681's channel B settings (MC68681 Table 4-5). */
  static const uint8_t MR1_KEEP = MR1_RXRTS | MR1_RXIRQ_FFULL | MR1_ERROR_BLOCK;
  static const uint8_t MR1_SEVEN_BITS = 2;
  static const uint8_t MR2_TWO_STOP_BITS = 0x0f;

  duart_shadow.mr1b &= MR1_KEEP;
  if (duartb_settings.parity == SERIAL_PARITY_NONE) {
    duartb.parity = 0;
    duart_shadow.mr1b |= MR1_PARITY_NONE;
  } else {
    duartb.parity = 1;
    if (duartb_settings.parity == SERIAL_PARITY_ODD) {
      duart_shadow.mr1b |= MR1_PARITY_ODD;
    }
  }
  duart_shadow.mr1b |=
      duartb_settings.eight_bits ? DUART_MR1_EIGHT_BITS : MR1_SEVEN_BITS;
  duart_shadow.mr2b = (duart_shadow.mr2b & ~MR2_STOP_MASK) |
                      (duartb_settings.two_stop_bits ? MR2_TWO_STOP_BITS
                                                     : DUART_MR2_ONE_STOP_BIT);
  duart.b.cr = CR_RESET_MR_POINTER;
  duart.b.mr = duart_shadow.mr1b;
  duart.b.mr = duart_shadow.mr2b;
  duartb_applied.parity = duartb_settings.parity;
  duartb_applied.eight_bits = duartb_settings.eight_bits;
  duartb_applied.two_stop_bits = duartb_settings.two_stop_bits;
}

/* DUART channel B's baud rate from the serial settings, 19200 for a
 * code out of range (0x1d632). */
void duartb_baud_from_settings(void) {
  /* baud-rate set 2's clock selects, which ACR_BRG_SET_2 picks */
  static const uint8_t CSR_1200 = 0x6;
  static const uint8_t CSR_2400 = 0x8;
  static const uint8_t CSR_19200 = 0xc;
  uint8_t clock;

  switch (duartb_settings.baud) {
    case SERIAL_BAUD_1200:
      clock = CSR_1200;
      break;
    case SERIAL_BAUD_2400:
      clock = CSR_2400;
      break;
    case SERIAL_BAUD_9600:
      clock = DUART_CSR_9600;
      break;
    default:
      clock = CSR_19200;
      break;
  }
  clock |= clock << DUART_CSR_CLOCK_BITS;
  duart_shadow.csrb = clock;
  duart.b.sr_csr = clock;
  duartb_applied.baud = duartb_settings.baud;
}

/* Waits for DUART channel B's transmitter and writes a character;
 * returns the status bit it waited on (0x1d680). */
uint8_t duartb_putc_polled(uint8_t c) {
  uint16_t sr = cpu_interrupts_at(CPU_IPL_2);
  uint8_t status;

  do {
    status = duart.b.sr_csr;
  } while ((status & SR_TXRDY) == 0);
  duart.b.rb_tb = c;
  cpu_sr_write(sr);
  return status & SR_TXRDY;
}

/* Writes a string to DUART channel B by interrupt, waiting until it is
 * out; returns how much went (0x1d6ba). */
int32_t drtb_puts(char* text) {
  char* end = text;
  uint16_t sr;

  psos_get_v(SIG_SOQ, 0, 0);
  duartb.transmit_next = text;
  while (*end != 0) {
    end++;
  }
  duartb.transmit_count = end - text;
  sr = cpu_interrupts_raise(CPU_IPL_6);
  if (!duartb.reconfiguring) {
    duart_shadow.imr |= IMR_TXRDYB;
    duart.isr_imr = duart_shadow.imr;
  }
  cpu_sr_write(sr);
  if ((uint8_t)psos_wait_v(SIG_SOQ, 0, 0, 0) != 0) {
    fatal_error("wait_v SIG_SOQ error in _drtb_puts");
  }
  return duartb.transmit_next - text;
}

/* Reads a character from DUART channel B's ring, waiting for one, and
 * sends XON once the ring has room again (0x1d74a). */
uint8_t drtb_getc(void) {
  uint8_t character;

  psos_get_v(SIG_SIQ, 0, 0);
  if (duartb_ring.write == duartb_ring.read &&
      (uint8_t)psos_wait_v(SIG_SIQ, 0, 0, 0) != 0) {
    fatal_error("wait_v SIG_SIQ error in getc");
  }
  character = duartb_ring.data[duartb_ring.read++];
  if ((duartb.flow & DUART_FLOW_XOFF_SENT) != 0 &&
      (uint8_t)(duartb_ring.read - duartb_ring.write) > DUART_XON_ABOVE_FREE) {
    duartb.flow &= ~DUART_FLOW_XOFF_SENT;
    if (duartb.xon_xoff) {
      drtb_putc(DUART_XON);
    }
  }
  return character;
}

/* Reads a line from DUART channel B (0x1d7e4). */
int32_t drtb_gets(char* buffer, int32_t length) {
  return duart_gets(buffer, length, drtb_getc, &duartb.echo,
                    &duartb.last_terminator, &duartb.receive_errors,
                    &duartb.read_errors, "DRTB receiver error\r\n", false);
}

/* Resets DUART channel B and applies the serial settings: mode, baud
 * rate, flow control and echo (0x1dc7e). */
void duartb_reconfigure(void) {
  uint16_t sr = cpu_interrupts_raise(CPU_IPL_2);

  duartb.reconfiguring = 1;
  duart_shadow.imr &= ~(IMR_RXRDYB | IMR_TXRDYB);
  duart.isr_imr = duart_shadow.imr;
  duart.b.cr = CR_RX_DISABLE;
  cpu_sr_write(sr);
  for (int32_t pauses = DUART_DRAIN_PAUSES;
       pauses > 0 && (duart.b.sr_csr & SR_TXEMT) == 0; pauses--) {
    psos_pause(1);
  }
  duart.b.cr = CR_RESET_RECEIVER;
  duart.b.cr = CR_RESET_TRANSMITTER;
  sr = cpu_interrupts_raise(CPU_IPL_2);
  duartb_mode_from_settings();
  duartb_baud_from_settings();
  duart.b.cr = CR_TX_ENABLE;
  if (!duartb_settings.xon_xoff) {
    if (duartb.xon_xoff && (duartb.flow & DUART_FLOW_XOFF_SENT) != 0) {
      duartb_putc_polled(DUART_XON);
    }
    duartb.xon_xoff = 0;
    duartb.flow &= ~(DUART_FLOW_XOFF_SENT | DUART_FLOW_XOFF_RECEIVED);
  } else {
    if (!duartb.xon_xoff && (duartb.flow & DUART_FLOW_XOFF_SENT) != 0) {
      duartb_putc_polled(DUART_XOFF);
    }
    duartb.xon_xoff = 1;
  }
  duartb.echo = duartb_settings.echo ? drtb_putc : duartb_echo_none;
  duart_shadow.imr |= IMR_RXRDYB | IMR_TXRDYB;
  duart.isr_imr = duart_shadow.imr;
  duart.b.cr = CR_RX_ENABLE;
  duartb.reconfiguring = 0;
  cpu_sr_write(sr);
}

/* Writes a character to channel B under the write lock, if it can take
 * it (0x1ddf6). */
void drtb_putc(char c) {
  char text[2] = {c, 0};

  /* mutex_acquire's result is positive when another holds it */
  if ((int8_t)mutex_acquire(&duartb.write_token) <= 0) {
    drtb_puts(text);
    mutex_release(&duartb.write_token);
  }
}

/* Starts a block out of channel B by interrupt without waiting, if an
 * asynchronous send is allowed and the port is free (0x1de32). */
void drtb_send_async(char* block, uint8_t count) {
  if (duartb.async_allowed && !duartb.reconfiguring &&
      !duartb.in_transmit_interrupt) {
    duartb.async_next = block;
    duartb.async_count = count;
    duartb.async_pending = 1;
    duart_shadow.imr |= IMR_TXRDYB;
    duart.isr_imr = duart_shadow.imr;
  }
}

/* The done hook of channel B's setting descriptors and its PRESet:
 * reconfigures device 0 (0x1df00). */
void duartb_settings_done(void) { psos_io_control(IO_DEVICE_DUART_B, 0, 0); }

/* With the serial port restricted, eight bits and a parity cannot both
 * be: a parity set clears eight bits (0x3086a). */
void duartb_parity_restrict(void) {
  if (nv_settings.serial_restricted != 0 && duartb_settings.eight_bits == 1 &&
      duartb_settings.parity != SERIAL_PARITY_NONE) {
    duartb_settings.eight_bits = 0;
  }
}

/* With the serial port restricted, eight bits set clears the parity
 * (0x3088c). */
void duartb_eight_bits_restrict(void) {
  if (nv_settings.serial_restricted != 0 && duartb_settings.eight_bits == 1 &&
      duartb_settings.parity != SERIAL_PARITY_NONE) {
    duartb_settings.parity = SERIAL_PARITY_NONE;
  }
}

/* Writes a string to channel B by polling, in supervisor mode; returns
 * its length (0x1df12). */
int32_t console_write_polled(char* s) {
  int32_t count = 0;
  uint16_t sr = enter_supervisor();

  for (; *s != 0; s++, count++) {
    duartb_putc_polled(*s);
  }
  cpu_sr_write(sr);
  return count;
}

/* Whether channel B's write lock is held by another task (0x1df6e). */
bool drtb_try_lock_write(void) {
  return (int8_t)mutex_acquire(&duartb.write_token) > 0;
}

/* Closes the console's device so that channel B can send the time
 * code, unless an XOFF holds it or another task is writing; returns
 * whether it did (0x1df8a). */
uint8_t drtb_release_for_timecode(void) {
  /* The kernel's I/O device B, minor 1, for the close. */
  static const uint32_t DEVICE_B_MINOR_1 = 0x001;

  if ((duartb.flow & DUART_FLOW_XOFF_RECEIVED) == 0 && !drtb_try_lock_write()) {
    duartb.async_allowed = 1;
    psos_io_close(DEVICE_B_MINOR_1);
    return 1;
  }
  return 0;
}

/* Takes each byte channel B received into its ring for the read
 * token's task, signaling it once at the end; a ctrl-C signals the
 * console task instead, and with XON/XOFF on, XON and XOFF start and
 * hold transmission, and a ring running out of room sends XOFF.
 * Returns the overrun bit of the status register (0x1e7f6). */
uint8_t duartb_isr(void) {
  /* The 68681's receive errors (MC68681 Table 4-5). */
  static const uint8_t RECEIVE_ERRORS =
      SR_RECEIVED_BREAK | SR_FRAMING_ERROR | SR_PARITY_ERROR;
  static const uint8_t ASCII_MASK = 0x7f;
  /* XON/XOFF threshold, in free bytes of a 256-byte ring. */
  static const uint8_t XOFF_BELOW_FREE =
      0x40; /* the interrupt sends XOFF with fewer free */
  bool received = false;
  uint8_t status;

  while ((duart.b.sr_csr & SR_RXRDY) != 0) {
    uint8_t character;

    duartb.receive_errors |= duart.b.sr_csr & RECEIVE_ERRORS;
    character = duart.b.rb_tb;
    if (!duartb_applied.eight_bits) {
      character &= ASCII_MASK;
    }
    if (character == DUART_CTRL_C && duartb.read_token.task == task_console) {
      if (duartb.read_token.task != 0 &&
          (uint8_t)psos_signal_v(duartb.read_token.task, SIGINTR) != 0) {
        fatal_error("DUARTB isr signaling SIGINTR");
      }
      if (duartb.xon_xoff) {
        duartb.flow &= ~DUART_FLOW_XOFF_RECEIVED;
      }
      if (!duartb.reconfiguring) {
        duart_shadow.imr |= IMR_TXRDYB;
        duart.isr_imr = duart_shadow.imr;
      }
    } else if (character == DUART_XON) {
      if (duartb.xon_xoff) {
        duartb.flow &= ~DUART_FLOW_XOFF_RECEIVED;
        if (!duartb.reconfiguring) {
          duart_shadow.imr |= IMR_TXRDYB;
          duart.isr_imr = duart_shadow.imr;
        }
      }
    } else if (character == DUART_XOFF) {
      if (duartb.xon_xoff) {
        duartb.flow |= DUART_FLOW_XOFF_RECEIVED;
        if (!duartb.async_allowed) {
          duart_shadow.imr &= ~IMR_TXRDYB;
          duart.isr_imr = duart_shadow.imr;
        }
      }
    } else if (duartb.read_token.task != 0) {
      uint8_t free_bytes;

      duartb_ring.data[duartb_ring.write++] = character;
      if (duartb_ring.write == duartb_ring.read) {
        duartb.receive_errors |= SR_OVERRUN;
      }
      received = true;
      if ((uint8_t)(duartb_ring.read - duartb_ring.write) < XOFF_BELOW_FREE) {
        duartb.flow |= DUART_FLOW_XOFF_SENT;
        if (duartb.xon_xoff) {
          duartb_putc_polled(DUART_XOFF);
        }
      }
      free_bytes = duartb_ring.read - duartb_ring.write;
      if (free_bytes < duartb.least_free) {
        duartb.least_free = free_bytes;
      }
    }
  }
  if (received &&
      (uint8_t)psos_signal_v(duartb.read_token.task, SIG_SIQ) != 0) {
    fatal_error("DUARTB isr signaling SIG_SIQ");
  }
  status = duart.b.sr_csr;
  if ((status & SR_OVERRUN) != 0) {
    duartb.receive_errors |= SR_OVERRUN;
    duart.b.cr = CR_RESET_ERROR;
  }
  return status & SR_OVERRUN;
}

/* Channel B's transmit interrupt: sends the asynchronous block's next
 * byte, or drtb_puts' unless an XOFF holds it; at drtb_puts' end turns
 * the interrupt off and signals the write token's task, and at the
 * block's end signals the SCPI task (0x1ea10). */
void duartb_transmit_interrupt(void) {
  /* The signal it gives the SCPI task when an asynchronous send ends;
   * psos-signals.h has no name for it. */
  static const uint32_t SIGNAL_ASYNC_DONE = 0x0020;

  duartb.in_transmit_interrupt = 1;
  if (duartb.async_count != 0) {
    duart.b.rb_tb = *duartb.async_next++;
    duartb.async_count--;
  } else if (duartb.transmit_count != 0 &&
             (duartb.flow & DUART_FLOW_XOFF_RECEIVED) == 0) {
    duart.b.rb_tb = *duartb.transmit_next++;
    duartb.transmit_count--;
  } else {
    duart_shadow.imr &= ~IMR_TXRDYB;
    duart.isr_imr = duart_shadow.imr;
    if (duartb.transmit_count == 0 && duartb.write_token.task != 0 &&
        (uint8_t)psos_signal_v(duartb.write_token.task, SIG_SOQ) != 0) {
      fatal_error("DUARTB isr signaling SIG_SOQ");
    }
  }
  if (duartb.async_pending && duartb.async_count == 0) {
    duartb.async_pending = 0;
    duartb.async_allowed = 0;
    if (task_scpi != 0) {
      psos_signal_v(task_scpi, SIGNAL_ASYNC_DONE);
    }
  }
  duartb.in_transmit_interrupt = 0;
}
