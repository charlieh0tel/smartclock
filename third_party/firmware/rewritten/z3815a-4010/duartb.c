/* duartb.c's functions that are this image's own: the rest are
 * common/duartb.c's, the same in every image (docs/firmware/rewrite.md,
 * "Shared C").  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype. */

/* Resets DUART channel B and applies the serial settings: mode, baud
 * rate, flow control and echo, as channel B's I/O control function
 * (0x2e352).  duartb_reconfigure is this, then no error. */
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

/* Writes a string to the console, the SCI here, by polling, in supervisor
 * mode, where the other images' write channel B; returns its length
 * (0x309be). */
int32_t console_write_polled(char* s) {
  int32_t count = 0;
  uint16_t sr = cpu_enter_supervisor();

  for (; *s != 0; s++, count++) {
    sci_putc_polled(*s);
  }
  cpu_sr_write(sr);
  return count;
}

/* Reads a line from DUART channel B, its receive errors the byte
 * before its ring, as the 58503A's (0x2deb2). */
int32_t drtb_gets(char* buffer, int32_t length) {
  return duart_gets(buffer, length, drtb_getc, &duartb.echo,
                    &duartb.last_terminator, &duartb_receive_errors,
                    &duartb.read_errors, "DRTB receiver error\r\n", false);
}

/* DUART channel B, the console and SCPI port: its driver (the firmware's
 * drtb_ and duartb_ routines), its interrupt, its settings' hooks and
 * its self-tests; its state is docs/firmware/structures.md's "DUART
 * channel B's driver", the registers mc68681.h's, written by hand in
 * place of the decompiler's (docs/firmware/README.md, "Hand-written C";
 * docs/firmware/modules.md, `devices/duartb`).  The exported C includes
 * this file after its own, so the types and prototypes it declares are
 * in scope; each function here keeps its generated prototype. */

/* The loopback test's receive on channel B (0x2ee0e). */
void duart_test_rx_b(void) {
  duart_test_receive(&duart.b, SELFTEST_UART_CHANNEL_B_DATA);
}

/* At reset, before interrupts: sends "DUART" through channel B in local
 * loopback by polling and checks each character back (0x2f332). */
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
 * out; returns how much went (0x2dd88). */
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
 * sends XON once the ring has room again (0x2de18). */
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

/* Writes a character to channel B under the write lock, if it can take
 * it (0x2e4c2). */
void drtb_putc(char c) {
  char text[2] = {c, 0};

  /* mutex_acquire's result is positive when another holds it */
  if ((int8_t)mutex_acquire(&duartb.write_token) <= 0) {
    drtb_puts(text);
    mutex_release(&duartb.write_token);
  }
}

/* Starts a block out of channel B by interrupt without waiting, if an
 * asynchronous send is allowed and the port is free (0x2e4fe). */
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
 * be: a parity set clears eight bits (0x44b9e). */
void duartb_parity_restrict(void) {
  if (nv_settings.serial_restricted != 0 && duartb_settings.eight_bits == 1 &&
      duartb_settings.parity != SERIAL_PARITY_NONE) {
    duartb_settings.eight_bits = 0;
  }
}

/* With the serial port restricted, eight bits set clears the parity
 * (0x44bc0). */
void duartb_eight_bits_restrict(void) {
  if (nv_settings.serial_restricted != 0 && duartb_settings.eight_bits == 1 &&
      duartb_settings.parity != SERIAL_PARITY_NONE) {
    duartb_settings.parity = SERIAL_PARITY_NONE;
  }
}

/* Whether channel B's write lock is held by another task (0x2e69a). */
bool drtb_try_lock_write(void) {
  return (int8_t)mutex_acquire(&duartb.write_token) > 0;
}

/* The loopback test on channel B, the host port (0x2eb48). */
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
 * data bits and stop bits (0x2dc4e).  The 58503A's takes
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

/* Takes each byte channel B received into its ring for the read
 * token's task, signaling it once at the end; a ctrl-C signals the
 * console task instead, and with XON/XOFF on, XON and XOFF start and
 * hold transmission, and a ring running out of room sends XOFF.
 * Returns the overrun bit of the status register (0x2ef22). */
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

/* The kernel's I/O device 3, DUART channel B, the SCPI port, here
 * (0x2e54e), where the other images' device 3 is the SCI. */
static const io_device DUARTB_PORT_DEVICE = IO_DEVICE_SCI;

/* DUART channel B's device 3, minor 1, for drtb_release_for_timecode's
 * close (0x2e6da), where the 58503A's is device 0's. */
static const uint32_t DUARTB_PORT_MINOR_1 = IO_DEVICE_SCI | 0x001;

/* Writes a string to DUART channel B's device; returns its length, the
 * Z3805A's sci_port_write on this image's device 3 (0x2e542). */
uint32_t duartb_port_write(void* text) {
  uint32_t length;

  psos_io_write(DUARTB_PORT_DEVICE, text, &length);
  return length;
}

/* Reads up to so many characters from DUART channel B's device, and its
 * last read errors, as console_read reads the console's; the count read
 * (0x2e58a). */
uint32_t duartb_port_read(void* buffer, uint32_t length, uint8_t* errors) {
  psos_io_read(DUARTB_PORT_DEVICE, buffer, &length);
  if (errors != 0) {
    *errors = duartb.read_errors;
  }
  return length;
}

/* Opens DUART channel B's device, scpi_session_init's (0x2e5b6). */
void duartb_port_open(void) { psos_io_open(DUARTB_PORT_DEVICE, 0); }

/* Closes DUART channel B's device, scpi_session_close's (0x2e5c6). */
void duartb_port_close(void) { psos_io_close(DUARTB_PORT_DEVICE); }

/* Formats into a buffer of LIBC_PRINTF_LENGTH through vsprintf and
 * writes it to DUART channel B's device (0x2e564). */
void duartb_printf(char* format, ...) {
  char text[LIBC_PRINTF_LENGTH];
  va_list arguments;

  va_start(arguments, format);
  vsprintf(text, format, arguments);
  va_end(arguments);
  duartb_port_write(text);
}

/* The done hook of DUART channel B's PRESet: reconfigures its device 3,
 * the Z3805A's sci_settings_done (0x2e5d4). */
void duartb_port_settings_done(void) {
  psos_io_control(DUARTB_PORT_DEVICE, 0, 0);
}

/* Writes a string to DUART channel B by polling, in supervisor mode;
 * returns its length (0x2e5e8). */
int32_t duartb_puts_polled(char* s) {
  int32_t count = 0;
  uint16_t sr = cpu_enter_supervisor();

  for (; *s != 0; s++, count++) {
    duartb_putc_polled(*s);
  }
  cpu_sr_write(sr);
  return count;
}

/* Allows channel B's asynchronous sends while time codes are sent
 * continuously, else forbids them once none is pending, as
 * sci_async_allowed_update does the SCI's; tcode_continuous_done calls it
 * (0x2e644). */
void drtb_async_allowed_update(void) {
  if (tcode_continuous) {
    duartb.async_allowed = 1;
  } else if (!duartb.async_pending) {
    duartb.async_allowed = 0;
  }
}

/* Forbids channel B's asynchronous sends and turns its transmit
 * interrupt on, so that drtb_puts' characters go on; es_end's
 * (0x2e666). */
void drtb_async_forbid(void) {
  duartb.async_allowed = 0;
  duart_shadow.imr |= IMR_TXRDYB;
  duart.isr_imr = duart_shadow.imr;
}

/* Allows channel B's asynchronous sends; es_start's (0x2e680). */
void drtb_async_allow(void) { duartb.async_allowed = 1; }

/* Whether drtb_puts has characters left to send; es_status's
 * (0x2e68a). */
bool drtb_sending(void) { return duartb.transmit_count != 0; }

/* Closes DUART channel B's device so that it can send the time code,
 * unless an XOFF holds it or another task is writing, and marks the SCPI
 * task to be signaled when the block is out; returns whether it did.  The
 * 58503A's closes device 0, its console, here the SCI (0x2e6b6). */
uint8_t drtb_release_for_timecode(void) {
  if ((duartb.flow & DUART_FLOW_XOFF_RECEIVED) == 0 && !drtb_try_lock_write()) {
    duartb.async_allowed = 1;
    duartb.field_4d = 1;
    psos_io_close(DUARTB_PORT_MINOR_1);
    return 1;
  }
  return 0;
}

/* Channel B's transmit interrupt: sends the asynchronous block's next
 * byte, or drtb_puts' unless an XOFF or an allowed asynchronous block
 * holds it; at drtb_puts' end turns the interrupt off and signals the
 * write token's task, and at the block's end, unless the time code is
 * continuous, disallows blocks again, and signals the SCPI task if
 * drtb_release_for_timecode asked.  The 58503A's sends drtb_puts' bytes
 * while a block is allowed and disallows blocks at every block's end
 * (0x2f13c). */
void duartb_transmit_interrupt(void) {
  duartb.in_transmit_interrupt = 1;
  if (duartb.async_count != 0) {
    duart.b.rb_tb = *duartb.async_next++;
    duartb.async_count--;
  } else if (duartb.transmit_count != 0 && !duartb.async_allowed &&
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
    if (!tcode_continuous) {
      duartb.async_allowed = 0;
    }
    if (duartb.field_4d) {
      duartb.field_4d = 0;
      if (task_scpi != 0) {
        psos_signal_v(task_scpi, DUART_SIGNAL_ASYNC_DONE);
      }
    }
  }
  duartb.in_transmit_interrupt = 0;
}
