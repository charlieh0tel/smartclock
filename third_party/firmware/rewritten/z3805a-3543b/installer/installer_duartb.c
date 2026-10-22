/* The installer's copies of DUART channel B's driver (`devices/duartb`;
 * duartb.c has the primary's), which keep the installer's driver records and
 * call its routines. Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype. */

/* DUART channel B's mode registers from the serial settings: parity,
 * data bits and stop bits (installer::0x103016). */
void installer_duartb_mode_from_settings(void) {
  /* The 68681's channel B settings (MC68681 Table 4-5). */
  static const uint8_t MR1_KEEP = MR1_RXRTS | MR1_RXIRQ_FFULL | MR1_ERROR_BLOCK;
  static const uint8_t MR1_SEVEN_BITS = 2;
  static const uint8_t MR2_TWO_STOP_BITS = 0x0f;

  installer_duart_shadow.mr1b &= MR1_KEEP;
  if (installer_duartb_settings.parity == SERIAL_PARITY_NONE) {
    installer_duartb.parity = 0;
    installer_duart_shadow.mr1b |= MR1_PARITY_NONE;
  } else {
    installer_duartb.parity = 1;
    if (installer_duartb_settings.parity == SERIAL_PARITY_ODD) {
      installer_duart_shadow.mr1b |= MR1_PARITY_ODD;
    }
  }
  installer_duart_shadow.mr1b |= installer_duartb_settings.eight_bits
                                     ? DUART_MR1_EIGHT_BITS
                                     : MR1_SEVEN_BITS;
  installer_duart_shadow.mr2b =
      (installer_duart_shadow.mr2b & ~MR2_STOP_MASK) |
      (installer_duartb_settings.two_stop_bits ? MR2_TWO_STOP_BITS
                                               : DUART_MR2_ONE_STOP_BIT);
  duart.b.cr = CR_RESET_MR_POINTER;
  duart.b.mr = installer_duart_shadow.mr1b;
  duart.b.mr = installer_duart_shadow.mr2b;
  installer_duartb_applied.parity = installer_duartb_settings.parity;
  installer_duartb_applied.eight_bits = installer_duartb_settings.eight_bits;
  installer_duartb_applied.two_stop_bits =
      installer_duartb_settings.two_stop_bits;
}

/* DUART channel B's baud rate from the serial settings, 19200 for a
 * code out of range (installer::0x1030b4). */
void installer_duartb_baud_from_settings(void) {
  /* baud-rate set 2's clock selects, which ACR_BRG_SET_2 picks */
  static const uint8_t CSR_1200 = 0x6;
  static const uint8_t CSR_2400 = 0x8;
  static const uint8_t CSR_19200 = 0xc;
  uint8_t clock;

  switch (installer_duartb_settings.baud) {
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
  installer_duart_shadow.csrb = clock;
  duart.b.sr_csr = clock;
  installer_duartb_applied.baud = installer_duartb_settings.baud;
}

/* Waits for DUART channel B's transmitter and writes a character;
 * returns the status bit it waited on (installer::0x103102). */
uint8_t installer_duartb_putc_polled(uint8_t c) {
  return duart_b_putc_polled(c);
}

/* Writes a string to DUART channel B by interrupt, waiting until it is
 * out; returns how much went (installer::0x10313c). */
int32_t installer_drtb_puts(char* text) {
  char* end = text;
  uint16_t sr;

  installer_psos_get_v(SIG_SOQ, 0, 0);
  installer_duartb.transmit_next = text;
  while (*end != 0) {
    end++;
  }
  installer_duartb.transmit_count = end - text;
  sr = cpu_interrupts_raise(CPU_IPL_6);
  if (!installer_duartb.reconfiguring) {
    installer_duart_shadow.imr |= IMR_TXRDYB;
    duart.isr_imr = installer_duart_shadow.imr;
  }
  cpu_sr_write(sr);
  if ((uint8_t)installer_psos_wait_v(SIG_SOQ, 0, 0, 0) != 0) {
    installer_fatal_error("wait_v SIG_SOQ error in _drtb_puts");
  }
  return installer_duartb.transmit_next - text;
}

/* Reads a character from DUART channel B's ring, waiting for one, and
 * sends XON once the ring has room again (installer::0x1031cc). */
uint8_t installer_drtb_getc(void) {
  uint8_t character;

  installer_psos_get_v(SIG_SIQ, 0, 0);
  if (installer_duartb_ring.write == installer_duartb_ring.read &&
      (uint8_t)installer_psos_wait_v(SIG_SIQ, 0, 0, 0) != 0) {
    installer_fatal_error("wait_v SIG_SIQ error in getc");
  }
  character = installer_duartb_ring.data[installer_duartb_ring.read++];
  if ((installer_duartb.flow & DUART_FLOW_XOFF_SENT) != 0 &&
      (uint8_t)(installer_duartb_ring.read - installer_duartb_ring.write) >
          DUART_XON_ABOVE_FREE) {
    installer_duartb.flow &= ~DUART_FLOW_XOFF_SENT;
    if (installer_duartb.xon_xoff) {
      installer_drtb_putc(DUART_XON);
    }
  }
  return character;
}

/* Reads a line from DUART channel B (installer::0x103266). */
int32_t installer_drtb_gets(char* buffer, int32_t length) {
  return duart_gets(buffer, length, installer_drtb_getc, &installer_duartb.echo,
                    &installer_duartb.last_terminator,
                    &installer_duartb.receive_errors,
                    &installer_duartb.read_errors, NULL, false);
}

/* Writes a character to channel B under the write lock, if it can take
 * it (installer::0x103868). */
void installer_drtb_putc(char c) {
  char text[2] = {c, 0};

  /* mutex_acquire's result is positive when another holds it */
  if ((int8_t)installer_mutex_acquire(&installer_duartb.write_token) <= 0) {
    installer_drtb_puts(text);
    installer_mutex_release(&installer_duartb.write_token);
  }
}

/* Writes a string to channel B by polling, in supervisor mode; returns
 * its length (installer::0x103984). */
int32_t installer_console_write_polled(char* s) {
  int32_t count = 0;
  uint16_t sr = installer_enter_supervisor();

  for (; *s != 0; s++, count++) {
    installer_duartb_putc_polled(*s);
  }
  cpu_sr_write(sr);
  return count;
}

/* Takes each byte channel B received into its ring for the read
 * token's task, signaling it once at the end; a ctrl-C signals the
 * console task instead, and with XON/XOFF on, XON and XOFF start and
 * hold transmission, and a ring running out of room sends XOFF.
 * Returns the overrun bit of the status register (installer::0x103aa8). */
uint8_t installer_duartb_isr(void) {
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

    installer_duartb.receive_errors |= duart.b.sr_csr & RECEIVE_ERRORS;
    character = duart.b.rb_tb;
    if (!installer_duartb_applied.eight_bits) {
      character &= ASCII_MASK;
    }
    if (character == DUART_CTRL_C &&
        installer_duartb.read_token.task == installer_task_console) {
      if (installer_duartb.read_token.task != 0 &&
          (uint8_t)installer_psos_signal_v(installer_duartb.read_token.task,
                                           SIGINTR) != 0) {
        installer_fatal_error("DUARTB isr signaling SIGINTR");
      }
      if (installer_duartb.xon_xoff) {
        installer_duartb.flow &= ~DUART_FLOW_XOFF_RECEIVED;
      }
      if (!installer_duartb.reconfiguring) {
        installer_duart_shadow.imr |= IMR_TXRDYB;
        duart.isr_imr = installer_duart_shadow.imr;
      }
    } else if (character == DUART_XON) {
      if (installer_duartb.xon_xoff) {
        installer_duartb.flow &= ~DUART_FLOW_XOFF_RECEIVED;
        if (!installer_duartb.reconfiguring) {
          installer_duart_shadow.imr |= IMR_TXRDYB;
          duart.isr_imr = installer_duart_shadow.imr;
        }
      }
    } else if (character == DUART_XOFF) {
      if (installer_duartb.xon_xoff) {
        installer_duartb.flow |= DUART_FLOW_XOFF_RECEIVED;
        if (!installer_duartb.async_allowed) {
          installer_duart_shadow.imr &= ~IMR_TXRDYB;
          duart.isr_imr = installer_duart_shadow.imr;
        }
      }
    } else if (installer_duartb.read_token.task != 0) {
      uint8_t free_bytes;

      installer_duartb_ring.data[installer_duartb_ring.write++] = character;
      if (installer_duartb_ring.write == installer_duartb_ring.read) {
        installer_duartb.receive_errors |= SR_OVERRUN;
      }
      received = true;
      if ((uint8_t)(installer_duartb_ring.read - installer_duartb_ring.write) <
          XOFF_BELOW_FREE) {
        installer_duartb.flow |= DUART_FLOW_XOFF_SENT;
        if (installer_duartb.xon_xoff) {
          installer_duartb_putc_polled(DUART_XOFF);
        }
      }
      free_bytes = installer_duartb_ring.read - installer_duartb_ring.write;
      if (free_bytes < installer_duartb.least_free) {
        installer_duartb.least_free = free_bytes;
      }
    }
  }
  if (received && (uint8_t)installer_psos_signal_v(
                      installer_duartb.read_token.task, SIG_SIQ) != 0) {
    installer_fatal_error("DUARTB isr signaling SIG_SIQ");
  }
  status = duart.b.sr_csr;
  if ((status & SR_OVERRUN) != 0) {
    installer_duartb.receive_errors |= SR_OVERRUN;
    duart.b.cr = CR_RESET_ERROR;
  }
  return status & SR_OVERRUN;
}

/* Channel B's transmit interrupt: sends the asynchronous block's next
 * byte, or drtb_puts' unless an XOFF holds it; at drtb_puts' end turns
 * the interrupt off and signals the write token's task, and at the
 * block's end signals the SCPI task (installer::0x103c90). */
void installer_duartb_transmit_interrupt(void) {
  /* The signal it gives the SCPI task when an asynchronous send ends;
   * psos-signals.h has no name for it. */
  static const uint32_t SIGNAL_ASYNC_DONE = 0x0020;

  installer_duartb.in_transmit_interrupt = 1;
  if (installer_duartb.async_count != 0) {
    duart.b.rb_tb = *installer_duartb.async_next++;
    installer_duartb.async_count--;
  } else if (installer_duartb.transmit_count != 0 &&
             (installer_duartb.flow & DUART_FLOW_XOFF_RECEIVED) == 0) {
    duart.b.rb_tb = *installer_duartb.transmit_next++;
    installer_duartb.transmit_count--;
  } else {
    installer_duart_shadow.imr &= ~IMR_TXRDYB;
    duart.isr_imr = installer_duart_shadow.imr;
    if (installer_duartb.transmit_count == 0 &&
        installer_duartb.write_token.task != 0 &&
        (uint8_t)installer_psos_signal_v(installer_duartb.write_token.task,
                                         SIG_SOQ) != 0) {
      installer_fatal_error("DUARTB isr signaling SIG_SOQ");
    }
  }
  if (installer_duartb.async_pending && installer_duartb.async_count == 0) {
    installer_duartb.async_pending = 0;
    installer_duartb.async_allowed = 0;
    if (installer_task_scpi != 0) {
      installer_psos_signal_v(installer_task_scpi, SIGNAL_ASYNC_DONE);
    }
  }
  installer_duartb.in_transmit_interrupt = 0;
}

/* Starts a block out of channel B by interrupt without waiting, if an
 * asynchronous send is allowed and the port is free
 * (installer::0x1038a4). */
void installer_drtb_send_async(char* block, uint8_t count) {
  if (installer_duartb.async_allowed && !installer_duartb.reconfiguring &&
      !installer_duartb.in_transmit_interrupt) {
    installer_duartb.async_next = block;
    installer_duartb.async_count = count;
    installer_duartb.async_pending = 1;
    installer_duart_shadow.imr |= IMR_TXRDYB;
    duart.isr_imr = installer_duart_shadow.imr;
  }
}

/* The done hook of channel B's setting descriptors: reconfigures device
 * 0 (installer::0x103972). */
void installer_duartb_settings_done(void) {
  installer_psos_io_control(IO_DEVICE_DUART_B, 0, 0);
}
