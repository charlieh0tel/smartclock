/* DUART channel B, the host port, I/O device 0 here: its driver's
 * routines the decompiler's reading fails, its self-test's loopback,
 * and the console's polled writes, written by hand in place of the
 * decompiler's (docs/firmware/README.md, "Hand-written C";
 * docs/firmware/z3805a-1.01.03.md, "The export and the checks").  Each
 * is read from this image's code: the Z3805A 3543B's and the Z3815A
 * 4010's C where the instructions are theirs, this image's interrupt
 * masks and fields where they differ.  The exported C includes this file
 * after its own, so the types and prototypes it declares are in scope;
 * each function here keeps its generated prototype.  It uses the line
 * reader duart.h defines, common/duart.h's constants and polled write,
 * and cpu.h's status register helpers.
 * With them, from the 58503B 1.01.04's and the Z3815A 4010's C, the functions
 * this image's code carries identical or relocated
 * (docs/firmware/z3805a-1.01.03.md, "The link list"), their comments' addresses
 * this image's but where they name another image. */

/* Reads a line from DUART channel B, its receive errors the byte
 * before its ring (0x1d888). */
int32_t drtb_gets(char* buffer, int32_t length) {
  return duart_gets(buffer, length, drtb_getc, &duartb.echo,
                    &duartb.last_terminator, &duartb_receive_errors,
                    &duartb.read_errors, "DRTB receiver error\r\n", false);
}

/* Resets DUART channel B and applies the serial settings: mode, baud
 * rate, flow control and echo, as channel B's I/O control function, its
 * interrupts held at level 7 where the Z3805A 3543B's hold them at 2
 * (0x1dd42, 0x1dda2).  duartb_reconfigure is this, then no error. */
static __attribute__((used, noipa)) void duartb_reconfigure_settings(void) {
  uint16_t sr = cpu_interrupts_raise(CPU_IPL_7);

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
  sr = cpu_interrupts_raise(CPU_IPL_7);
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

CPU_IO_FUNCTION(duartb_reconfigure, duartb_reconfigure_settings);

/* Writes a string to channel B by polling, in supervisor mode; returns
 * its length (0x1dff4). */
int32_t console_write_polled(char* s) {
  int32_t count = 0;
  uint16_t sr = cpu_enter_supervisor();

  for (; *s != 0; s++, count++) {
    duartb_putc_polled(*s);
  }
  cpu_sr_write(sr);
  return count;
}

/* Formats into a buffer of LIBC_PRINTF_LENGTH through vsprintf and
 * writes it to the console, channel B, by polling (0x1e02a). */
void printf_polled(char* format, ...) {
  char text[LIBC_PRINTF_LENGTH];
  va_list arguments;

  va_start(arguments, format);
  vsprintf(text, format, arguments);
  va_end(arguments);
  console_write_polled(text);
}

/* The loopback test on channel B, the host port, what comes back kept
 * at loopback_received_b (0x1e5b8). */
void duart_loopback_test_b(void) {
  const duart_loopback_channel channel = {
      &duart.b,
      &duart_shadow.mr2b,
      IMR_RXRDYB | IMR_TXRDYB,
      &duartb.reconfiguring,
      &duartb.loopback_test,
      SELFTEST_UART_CHANNEL_B_TIMEOUT,
  };

  duart_loopback_test(&channel, loopback_received_b);
}

/* The loopback test's receive on channel B (0x1e8ac). */
void duart_test_rx_b(void) {
  duart_test_receive(&duart.b, SELFTEST_UART_CHANNEL_B_DATA);
}

/* Writes length characters of text to DUART channel B by interrupt,
 * waiting until they are out; returns how many went, 0 at once for
 * none.  The transmit interrupt is enabled with interrupts held at
 * level 6, and left off while duartb_reconfigure changes the port.  The
 * Z3805A 3543B's writes to the NUL; the decompiler's drops the status
 * register's restore (0x1d756, 0x1d7ae). */
int32_t drtb_puts(char* text, uint16_t length) {
  if (length == 0) {
    return 0;
  }
  psos_get_v(SIG_SOQ, 0, 0);
  duartb.transmit_next = text;
  duartb.transmit_count = length;
  uint16_t sr = cpu_interrupts_raise(CPU_IPL_6);

  if (!duartb.reconfiguring) {
    duart_shadow.imr |= IMR_TXRDYB;
    duart.isr_imr = duart_shadow.imr;
  }
  cpu_sr_write(sr);
  if ((uint8_t)psos_wait_v(SIG_SOQ, 0, 0, 0) != 0) {
    fatal_error("wait_v SIG_SOQ error in _drtb_puts");
  }
  duartb.transmit_count = 0;
  return duartb.transmit_next - text;
}

/* With the serial port restricted, eight bits and a parity cannot both
 * be: a parity set clears eight bits (0x32298). */
void duartb_parity_restrict(void) {
  if (nv_settings.serial_restricted != 0 && duartb_settings.eight_bits == 1 &&
      duartb_settings.parity != SERIAL_PARITY_NONE) {
    duartb_settings.eight_bits = 0;
  }
}

/* With the serial port restricted, eight bits set clears the parity
 * (0x322ba). */
void duartb_eight_bits_restrict(void) {
  if (nv_settings.serial_restricted != 0 && duartb_settings.eight_bits == 1 &&
      duartb_settings.parity != SERIAL_PARITY_NONE) {
    duartb_settings.parity = SERIAL_PARITY_NONE;
  }
}

/* Reads a character from DUART channel B's ring, waiting for one, and
 * sends XON once the ring has room again (0x1d7ee). */
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

/* Whether channel B's write lock is held by another task (0x1e050). */
bool drtb_try_lock_write(void) {
  return (int8_t)mutex_acquire(&duartb.write_token) > 0;
}

/* DUART channel B's mode registers from the serial settings: parity,
 * data bits and stop bits (0x1d61c).  The 58503A's takes
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

/* DUART channel B's device, minor 1: the counted write. */
static const uint32_t DUARTB_DEVICE_MINOR_1 = IO_DEVICE_DUART_B | 1;

/* Writes length bytes of a buffer to DUART channel B's device, minor 1,
 * which drtb_write sends counted; returns how many went; nothing calls
 * it (0x1df4c). */
uint32_t console_write_counted(void* buffer, uint16_t length) {
  uint32_t count = length;

  psos_io_write(DUARTB_DEVICE_MINOR_1, buffer, &count);
  return count;
}

/* Takes each byte channel B received into its ring for the read
 * token's task, signaling it once at the end; a ctrl-C signals the
 * console task instead, and with XON/XOFF on, XON and XOFF start and
 * hold transmission, and a ring running out of room sends XOFF.
 * Returns the overrun bit of the status register (0x1e9c6). */
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

/* The flag drtb_release_for_timecode sets, which timecode_pps_send tests
 * each 1PPS (0x1e06c). */
uint8_t drtb_timecode_released(void) { return duartb.released_for_timecode; }

/* DUART channel B's device 0, minor 1, for drtb_release_for_timecode's
 * close (0x1e098). */
static const uint32_t DUARTB_PORT_MINOR_1 = IO_DEVICE_DUART_B | 0x001;

/* Closes DUART channel B's device so that it can send the time code,
 * unless an XOFF holds it or another task is writing, and marks the SCPI
 * task to be signaled when the block is out; returns whether it did
 * (0x1e074). */
uint8_t drtb_release_for_timecode(void) {
  if ((duartb.flow & DUART_FLOW_XOFF_RECEIVED) == 0 && !drtb_try_lock_write()) {
    duartb.async_allowed = 1;
    duartb.released_for_timecode = 1;
    psos_io_close(DUARTB_PORT_MINOR_1);
    return 1;
  }
  return 0;
}

/* Queues a block for channel B to send by interrupt without waiting, if
 * an asynchronous send is allowed, and turns the transmit interrupt on
 * unless the port is being reconfigured; unlike the Z3805A 3543B's, a
 * block is queued while the port is reconfigured, and the transmit
 * interrupt starts it after (0x1def0). */
void drtb_send_async(char* block, uint8_t count) {
  if (duartb.async_allowed) {
    duartb.async_next = block;
    duartb.async_count = count;
    duartb.async_pending = 1;
    if (!duartb.reconfiguring) {
      duart_shadow.imr |= IMR_TXRDYB;
      duart.isr_imr = duart_shadow.imr;
    }
  }
}

/* Channel B's transmit interrupt.  First, at an asynchronous block's
 * end, disallows blocks again, and signals the SCPI task if
 * drtb_release_for_timecode asked; and once drtb_puts' characters are
 * out signals the write token's task.  Then sends the asynchronous
 * block's next byte, or drtb_puts' unless an XOFF holds it, or turns
 * the interrupt off; and turns it on again for a block still pending
 * unless the port is being reconfigured (0x1ebe0). */
void duartb_transmit_interrupt(void) {
  if (duartb.async_pending && duartb.async_count == 0) {
    duartb.async_pending = 0;
    duartb.async_allowed = 0;
    if (duartb.released_for_timecode) {
      duartb.released_for_timecode = 0;
      if (task_scpi != 0) {
        psos_signal_v(task_scpi, DUART_SIGNAL_ASYNC_DONE);
      }
    }
  }
  if (duartb.puts_pending && duartb.transmit_count == 0) {
    duartb.puts_pending = 0;
    if (duartb.write_token.task != 0 &&
        (uint8_t)psos_signal_v(duartb.write_token.task, SIG_SOQ) != 0) {
      fatal_error("DUARTB isr signaling SIG_SOQ");
    }
  }
  if (duartb.async_count != 0) {
    duart.b.rb_tb = *duartb.async_next++;
    duartb.async_count--;
  } else if (duartb.transmit_count != 0 &&
             (duartb.flow & DUART_FLOW_XOFF_RECEIVED) == 0) {
    duartb.puts_pending = 1;
    duart.b.rb_tb = *duartb.transmit_next++;
    duartb.transmit_count--;
  } else {
    duart_shadow.imr &= ~IMR_TXRDYB;
    duart.isr_imr = duart_shadow.imr;
  }
  if (duartb.async_pending && !duartb.reconfiguring) {
    duart_shadow.imr |= IMR_TXRDYB;
    duart.isr_imr = duart_shadow.imr;
  }
}

/* Writes a character to channel B under the write lock, if it can take
 * it (0x1deb0). */
void drtb_putc(char c) {
  char text[2] = {c, 0};

  /* mutex_acquire's result is positive when another holds it */
  if ((int8_t)mutex_acquire(&duartb.write_token) <= 0) {
    drtb_puts(text, 1);
    mutex_release(&duartb.write_token);
  }
}

/* At reset, before interrupts: sends "DUART" through channel B in local
 * loopback by polling and checks each character back (0x1edda).  It
 * points loopback_receive_next at loopback_received_b and keeps nothing
 * there. */
void duartb_loopback_check(void) {
  char* text = "DUART";

  (void)duart.b.mr;
  duart.b.mr = MR2_MODE_LOCAL_LOOPBACK | DUART_MR2_ONE_STOP_BIT;
  loopback_receive_next = loopback_received_b;
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
