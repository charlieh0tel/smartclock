/* The installer's console driver, the SCI's, on installer_sci and
 * installer_sci_ring, as sci.c's is on sci and sci_ring: its line
 * reader, the Z3815A 4010's installer's, whose decompiler's reading the
 * function check finds otherwise.  Written by hand in place of the
 * decompiler's (docs/firmware/README.md, "Hand-written C"); each keeps
 * its generated prototype.
 * With them, from the Z3815A 4010's rewritten/installer/installer_sci.c, of
 * its functions, those the Z3816A's code carries (identical or relocated by
 * image_diff, or changed with the same instructions), with the static
 * helpers they use, each the same here but where its comment says otherwise
 * (docs/firmware/z3816a-4001.md, "The link check").  Their comments'
 * addresses are the Z3816A's. */

/* Reads a line from the SCI, a character with its top bit set taken as
 * printable, with no message for a receive error
 * (installer::0x1032cc). */
int32_t installer_sci_gets(char* buffer, int32_t length) {
  return duart_gets(buffer, length, installer_sci_getc, &installer_sci.echo,
                    &installer_sci.last_terminator,
                    &installer_sci.receive_errors, &installer_sci.read_errors,
                    NULL, false);
}

/* Reads a character from the SCI's ring, waiting for one, and sends XON
 * by polling once the ring has room again (installer::0x103234). */
uint8_t installer_sci_getc(void) {
  uint8_t character;

  installer_psos_get_v(SIG_SIQ_SCI, 0, 0);
  if (installer_sci_ring.write == installer_sci_ring.read &&
      (uint8_t)installer_psos_wait_v(SIG_SIQ_SCI, 0, 0, 0) != 0) {
    installer_fatal_error("wait_v SIG_SIQ_SCI error in getc");
  }
  character = installer_sci_ring.data[installer_sci_ring.read++];
  if ((installer_sci.flow & DUART_FLOW_XOFF_SENT) != 0 &&
      (uint8_t)(installer_sci_ring.read - installer_sci_ring.write) >
          DUART_XON_ABOVE_FREE) {
    installer_sci.flow &= ~DUART_FLOW_XOFF_SENT;
    if (installer_sci.xon_xoff) {
      installer_sci_putc_polled(DUART_XON);
    }
  }
  return character;
}

/* Writes a character to the SCI under its write lock, if it can take it
 * (installer::0x103c32). */
void installer_sci_putc(char c) {
  char text[2] = {c, 0};

  /* installer_sci_lock's result is positive when another holds the lock */
  if (installer_sci_lock(1) <= 0) {
    installer_sci_puts(text, 1);
    installer_sci_unlock(1);
  }
}

/* The output latch's bit the installer's SCI sets while it transmits and
 * clears once it is done, through installer_sci_transmitting_set
 * (installer::0x103074), where the primary's sci_transmitting_set sets
 * none; what it drives is not established. */
static const uint16_t INSTALLER_SCI_LATCH_TRANSMITTING = 0x2000;

/* Marks the SCI transmitting or not, as the primary's
 * sci_transmitting_set, and sets the latch's transmitting bit to match
 * when it changes (installer::0x103038). */
void installer_sci_transmitting_set(bool on) {
  if (on) {
    installer_sci.transmit_busy = 1;
    if ((gpt.tmsk & TMSK_OCI2) != 0) {
      gpt.tmsk &= ~TMSK_OCI2;
    }
  }
  uint16_t sr = cpu_interrupts_raise(CPU_IPL_7);

  if (on != installer_sci.transmitting) {
    installer_latch_bits_set(INSTALLER_SCI_LATCH_TRANSMITTING, on);
    installer_sci.transmitting = on;
  }
  cpu_sr_write(sr);
}

/* Sets output compare 2 to interrupt a character time,
 * installer_sci.baud_constant timer counts, from now, and clears
 * installer_sci.transmit_busy; returns the compare's time
 * (installer::0x103090). */
uint16_t installer_sci_transmit_end_schedule(void) {
  gpt.tflg &= ~TFLG_OCF2;
  gpt.tmsk &= ~TMSK_OCI2;
  uint16_t sr = cpu_interrupts_raise(CPU_IPL_7);
  uint16_t at = gpt.tcnt + installer_sci.baud_constant;

  gpt.toc[1] = at;
  gpt.tflg &= ~TFLG_OCF2;
  gpt.tmsk |= TMSK_OCI2;
  installer_sci.transmit_busy = 0;
  cpu_sr_write(sr);
  return at;
}

/* A no-op echo: installer_sci_reconfigure puts it in installer_sci.echo
 * when the settings' echo is off (installer::0x103106). */
void installer_sci_echo_none(char c) {}

/* 1 unless an XOFF received holds the SCI's transmission, else 0, as the
 * primary's sci_transmit_allowed; nothing calls it
 * (installer::0x103dc4). */
int32_t installer_sci_transmit_allowed(void) {
  return (installer_sci.flow & DUART_FLOW_XOFF_RECEIVED) == 0;
}

/* Sets the SCI's baud rate from a settings code, 9600 for a code out of
 * range, and keeps a character's time at it in timer counts, as the
 * primary's sci_baud_set (installer::0x10310e). */
void installer_sci_baud_set(serial_baud baud) {
  uint16_t divisor;

  switch (baud) {
    case SERIAL_BAUD_1200:
      divisor = SCI_DIVISOR_1200;
      installer_sci.baud_constant = SCI_CONSTANT_1200;
      break;
    case SERIAL_BAUD_2400:
      divisor = SCI_DIVISOR_2400;
      installer_sci.baud_constant = SCI_CONSTANT_2400;
      break;
    case SERIAL_BAUD_19200:
      divisor = SCI_DIVISOR_19200;
      installer_sci.baud_constant = SCI_CONSTANT_19200;
      break;
    default:
      divisor = SCI_DIVISOR_9600;
      installer_sci.baud_constant = SCI_CONSTANT_9600;
      break;
  }
  qsm.sccr0 = divisor;
}

/* Writes length bytes of a buffer to the SCI's device, minor 1, as the
 * primary's sci_port_write_counted; returns how many went; nothing calls
 * it (installer::0x103cc0). */
uint32_t installer_sci_port_write_counted(void* buffer, uint16_t length) {
  uint32_t count = length;

  installer_psos_io_write(SCI_DEVICE_MINOR_1, buffer, &count);
  return count;
}

/* Waits for the SCI's transmitter and writes a character, its top bit
 * set for mark parity, marking the port transmitting; unless the
 * transmit interrupt is on, schedules the end of transmission.  Returns
 * SCCR1's TIE, or the end's time, as the primary's sci_putc_polled
 * (installer::0x10315e). */
uint16_t installer_sci_putc_polled(char c) {
  uint16_t sr = cpu_interrupts_at(CPU_IPL_3);

  while ((qsm.scsr & SCSR_TDRE) == 0) {
  }
  installer_sci_transmitting_set(true);
  qsm.scdr = installer_sci.mark_bit ? (int16_t)c | SCI_MARK_BIT : (int16_t)c;
  uint16_t result = qsm.sccr1 & SCCR1_TIE;

  if (result == 0) {
    result = installer_sci_transmit_end_schedule();
  }
  cpu_sr_write(sr);
  return result;
}

/* The event installer_sci_puts waits on, which the transmit interrupt
 * signals when the text is out, as the primary's SCI_SIGNAL_OUTPUT_DONE
 * (installer::0x1031ce). */
static const uint32_t INSTALLER_SCI_SIGNAL_OUTPUT_DONE = 0x80;

/* Writes length characters of text to the SCI by interrupt, waiting
 * until they are out; returns how many went (installer::0x1031ca).  The
 * Z3815A 4010's but with no test for no length, as the primary's
 * sci_puts. */
int32_t installer_sci_puts(char* text, uint16_t length) {
  installer_psos_get_v(INSTALLER_SCI_SIGNAL_OUTPUT_DONE, 0, 0);
  installer_sci.transmit_next = text;
  installer_sci.transmit_count = length;
  if (!installer_sci.reconfiguring) {
    qsm.sccr1 |= SCCR1_TIE;
  }
  if ((uint8_t)installer_psos_wait_v(INSTALLER_SCI_SIGNAL_OUTPUT_DONE, 0, 0,
                                     0) != 0) {
    installer_fatal_error("wait_v SIG_SOQ_SCI error in _sci_puts");
  }
  const int32_t count = installer_sci.transmit_next - text;

  installer_sci.transmit_count = 0;
  return count;
}

/* Starts a block out of the SCI by interrupt without waiting, if an
 * asynchronous send is allowed and the port is free
 * (installer::0x103c66).  The Z3815A 4010's. */
void installer_sci_send_async(char* block, uint8_t count) {
  if (installer_sci.async_allowed && !installer_sci.reconfiguring &&
      !installer_sci.in_transmit_interrupt) {
    installer_sci.async_next = block;
    installer_sci.async_count = count;
    installer_sci.async_pending = 1;
    qsm.sccr1 |= SCCR1_TIE;
  }
}

/* Writes a string to the SCI by polling, in supervisor mode; returns its
 * length (installer::0x103d68).  The Z3815A 4010's. */
int32_t installer_console_write_polled(char* s) {
  int32_t count = 0;
  uint16_t sr = installer_enter_supervisor();

  for (; *s != 0; s++, count++) {
    installer_sci_putc_polled(*s);
  }
  cpu_sr_write(sr);
  return count;
}
