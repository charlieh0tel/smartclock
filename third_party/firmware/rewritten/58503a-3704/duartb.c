/* DUART channel B, the console and SCPI port: its driver (the firmware's
 * drtb_ and duartb_ routines), its interrupt, its settings' hooks and
 * its self-tests; its state is docs/firmware/structures.md's "DUART
 * channel B's driver", the registers mc68681.h's, written by hand in
 * place of the decompiler's (docs/firmware/README.md, "Hand-written C";
 * docs/firmware/modules.md, `devices/duartb`).  The exported C includes
 * this file after its own, so the types and prototypes it declares are
 * in scope; each function here keeps its generated prototype. */

/* The loopback test's receive on channel B (0x1e548). */
void duart_test_rx_b(void) {
  duart_test_receive(&duart.b, SELFTEST_UART_CHANNEL_B_DATA);
}

/* At reset, before interrupts: sends "DUART" through channel B in local
 * loopback by polling and checks each character back (0x1ea58). */
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

/* Writes a string to DUART channel B by interrupt, waiting until it is
 * out; returns how much went (0x1d50c). */
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
 * sends XON once the ring has room again (0x1d59c). */
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

/* Reads a line from DUART channel B, its receive errors the byte
 * before its ring, where the Z3805A keeps them in duartb (0x1d636). */
int32_t drtb_gets(char* buffer, int32_t length) {
  return duart_gets(buffer, length, drtb_getc, &duartb.echo,
                    &duartb.last_terminator, &duartb_receive_errors,
                    &duartb.read_errors, "DRTB receiver error\r\n", false);
}

/* Resets DUART channel B and applies the serial settings: mode, baud
 * rate, flow control and echo, as channel B's I/O control function
 * (0x1dad6).  duartb_reconfigure is this, then no error. */
static __attribute__((used, noipa)) void duartb_reconfigure_settings(void) {
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
 * it (0x1dc4e). */
void drtb_putc(char c) {
  char text[2] = {c, 0};

  /* mutex_acquire's result is positive when another holds it */
  if ((int8_t)mutex_acquire(&duartb.write_token) <= 0) {
    drtb_puts(text);
    mutex_release(&duartb.write_token);
  }
}

/* Starts a block out of channel B by interrupt without waiting, if an
 * asynchronous send is allowed and the port is free (0x1dc8a). */
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

/* With the serial port restricted, eight bits and a parity cannot both
 * be: a parity set clears eight bits (0x30758). */
void duartb_parity_restrict(void) {
  if (nv_settings.serial_restricted != 0 && duartb_settings.eight_bits == 1 &&
      duartb_settings.parity != SERIAL_PARITY_NONE) {
    duartb_settings.eight_bits = 0;
  }
}

/* With the serial port restricted, eight bits set clears the parity
 * (0x3077a). */
void duartb_eight_bits_restrict(void) {
  if (nv_settings.serial_restricted != 0 && duartb_settings.eight_bits == 1 &&
      duartb_settings.parity != SERIAL_PARITY_NONE) {
    duartb_settings.parity = SERIAL_PARITY_NONE;
  }
}

/* Whether channel B's write lock is held by another task (0x1ddc6). */
bool drtb_try_lock_write(void) {
  return (int8_t)mutex_acquire(&duartb.write_token) > 0;
}

/* The loopback test on channel B, the host port (0x1e274). */
void duart_loopback_test_b(void) {
  const duart_loopback_channel channel = {
      &duart.b,
      &duart_shadow.mr2b,
      IMR_RXRDYB | IMR_TXRDYB,
      &duartb.reconfiguring,
      &duartb.loopback_test,
      SELFTEST_UART_CHANNEL_B_TIMEOUT,
  };

  duart_loopback_test(&channel);
}

/* DUART channel B's mode registers from the serial settings: parity,
 * data bits and stop bits (0x1d3d2).  The 58503A's takes
 * SERIAL_PARITY_ONE as parity forced high, which the Z3805A's takes as
 * even. */
void duartb_mode_from_settings(void) {
  duart_shadow.mr1b &= DUART_MR1_KEEP;
  if (duartb_settings.parity == SERIAL_PARITY_NONE) {
    duartb.parity = 0;
    duart_shadow.mr1b |= MR1_PARITY_NONE;
  } else if (duartb_settings.parity == SERIAL_PARITY_ONE) {
    duartb.parity = 1;
    duart_shadow.mr1b |= MR1_PARITY_FORCE | MR1_PARITY_ODD;
  } else {
    duartb.parity = 1;
    if (duartb_settings.parity == SERIAL_PARITY_ODD) {
      duart_shadow.mr1b |= MR1_PARITY_ODD;
    }
  }
  duart_shadow.mr1b |=
      duartb_settings.eight_bits ? DUART_MR1_EIGHT_BITS : DUART_MR1_SEVEN_BITS;
  duart_shadow.mr2b = (duart_shadow.mr2b & ~MR2_STOP_MASK) |
                      (duartb_settings.two_stop_bits ? DUART_MR2_TWO_STOP_BITS
                                                     : DUART_MR2_ONE_STOP_BIT);
  duart.b.cr = CR_RESET_MR_POINTER;
  duart.b.mr = duart_shadow.mr1b;
  duart.b.mr = duart_shadow.mr2b;
  duartb_applied.parity = duartb_settings.parity;
  duartb_applied.eight_bits = duartb_settings.eight_bits;
  duartb_applied.two_stop_bits = duartb_settings.two_stop_bits;
}

/* Closes the console's device so that channel B can send the time
 * code, unless an XOFF holds it or another task is writing, and marks
 * the SCPI task to be signaled when the block is out; returns whether
 * it did (0x1dde2). */
uint8_t drtb_release_for_timecode(void) {
  /* The kernel's I/O device B, minor 1, for the close. */
  static const uint32_t DEVICE_B_MINOR_1 = 0x001;

  if ((duartb.flow & DUART_FLOW_XOFF_RECEIVED) == 0 && !drtb_try_lock_write()) {
    duartb.async_allowed = 1;
    duartb.field_4d = 1;
    psos_io_close(DEVICE_B_MINOR_1);
    return 1;
  }
  return 0;
}

/* Takes each byte channel B received into its ring for the read
 * token's task, signaling it once at the end; a ctrl-C signals the
 * console task instead, and with XON/XOFF on, XON and XOFF start and
 * hold transmission, and a ring running out of room sends XOFF.
 * Returns the overrun bit of the status register (0x1e65c). */
uint8_t duartb_isr(void) {
  bool received = false;
  uint8_t status;

  while ((duart.b.sr_csr & SR_RXRDY) != 0) {
    uint8_t character;

    duartb_receive_errors |= duart.b.sr_csr & DUART_RECEIVE_ERRORS;
    character = duart.b.rb_tb;
    if (!duartb_applied.eight_bits) {
      character &= DUART_ASCII_MASK;
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
        duartb_receive_errors |= SR_OVERRUN;
      }
      received = true;
      if ((uint8_t)(duartb_ring.read - duartb_ring.write) <
          DUART_XOFF_BELOW_FREE) {
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
    duartb_receive_errors |= SR_OVERRUN;
    duart.b.cr = CR_RESET_ERROR;
  }
  return status & SR_OVERRUN;
}

/* Channel B's transmit interrupt: sends the asynchronous block's next
 * byte, or drtb_puts' unless an XOFF holds it; at drtb_puts' end turns
 * the interrupt off and signals the write token's task, and at the
 * block's end signals the SCPI task if drtb_release_for_timecode asked
 * (0x1e876). */
void duartb_transmit_interrupt(void) {
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
    if (duartb.field_4d) {
      duartb.field_4d = 0;
      if (task_scpi != 0) {
        psos_signal_v(task_scpi, DUART_SIGNAL_ASYNC_DONE);
      }
    }
  }
  duartb.in_transmit_interrupt = 0;
}
