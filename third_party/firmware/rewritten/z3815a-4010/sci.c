/* The SCI port's driver, the MC68331's serial port and this image's
 * console, device 0 (docs/firmware/z3815a-4010.md, "How it differs from
 * the Z3805A"), written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"): the Z3805A's driver, its
 * baud rate, its polled and interrupt-driven writes and its reads, with
 * what this image adds -- a counted write, and a transmitting flag that
 * the output compare 2 interrupt clears a character time after the last
 * character.  The exported C includes this file after its own, so the
 * types and prototypes it declares are in scope; each function here keeps
 * its generated prototype.  The driver's state is smartclock.h's
 * sci_driver; the registers are mc68331.h's.  It uses the control
 * characters, codes and line reader duart.h defines and cpu.h's status
 * register helpers. */

/* The signal sci_puts waits on for its string to be out: 0x80 here
 * ("wait_v SIG_SOQ_SCI error in _sci_puts", 0x2fdac), where the
 * Z3805A's is psos-signals.h's SIG_SOQ_SCI, 0x200. */
static const uint32_t SCI_SIGNAL_OUTPUT_DONE = 0x80;

/* Marks the SCI transmitting or not; marking it transmitting also holds
 * off output compare 2, whose interrupt (primary_user_85) marks it not
 * transmitting once the last character is out (0x2fc24). */
void sci_transmitting_set(bool on) {
  if (on) {
    sci.transmit_busy = 1;
    if ((gpt.tmsk & TMSK_OCI2) != 0) {
      gpt.tmsk &= ~TMSK_OCI2;
    }
  }
  uint16_t sr = cpu_interrupts_raise(CPU_IPL_7);

  if (on != sci.transmitting) {
    sci.transmitting = on;
  }
  cpu_sr_write(sr);
}

/* Sets output compare 2 to interrupt a character time, sci.baud_constant
 * timer counts, from now, and clears sci.transmit_busy; returns the
 * compare's time (0x2fc6e). */
uint16_t sci_transmit_end_schedule(void) {
  gpt.tflg &= ~TFLG_OCF2;
  gpt.tmsk &= ~TMSK_OCI2;
  uint16_t sr = cpu_interrupts_raise(CPU_IPL_7);
  uint16_t at = gpt.tcnt + sci.baud_constant;

  gpt.toc[1] = at;
  gpt.tflg &= ~TFLG_OCF2;
  gpt.tmsk |= TMSK_OCI2;
  sci.transmit_busy = 0;
  cpu_sr_write(sr);
  return at;
}

/* A no-op echo: sci_init and sci_reconfigure put it in sci.echo when the
 * settings' echo is off (0x2fce4). */
void sci_echo_none(char c) {}

/* Sets the SCI's baud rate from a settings code, 9600 for a code out of
 * range, and keeps a character's time at it in timer counts (0x2fcec). */
void sci_baud_set(serial_baud baud) {
  uint16_t divisor;

  switch (baud) {
    case SERIAL_BAUD_1200:
      divisor = SCI_DIVISOR_1200;
      sci.baud_constant = SCI_CONSTANT_1200;
      break;
    case SERIAL_BAUD_2400:
      divisor = SCI_DIVISOR_2400;
      sci.baud_constant = SCI_CONSTANT_2400;
      break;
    case SERIAL_BAUD_19200:
      divisor = SCI_DIVISOR_19200;
      sci.baud_constant = SCI_CONSTANT_19200;
      break;
    default:
      divisor = SCI_DIVISOR_9600;
      sci.baud_constant = SCI_CONSTANT_9600;
      break;
  }
  qsm.sccr0 = divisor;
}

/* Waits for the SCI's transmitter and writes a character, its top bit
 * set for mark parity, marking the port transmitting; unless the
 * transmit interrupt is on, schedules the end of transmission.  Returns
 * SCCR1's TIE, or the end's time (0x2fd3c). */
uint16_t sci_putc_polled(char c) {
  uint16_t sr = cpu_interrupts_at(CPU_IPL_3);

  while ((qsm.scsr & SCSR_TDRE) == 0) {
  }
  sci_transmitting_set(true);
  qsm.scdr = sci.mark_bit ? (int16_t)c | SCI_MARK_BIT : (int16_t)c;
  uint16_t result = qsm.sccr1 & SCCR1_TIE;

  if (result == 0) {
    result = sci_transmit_end_schedule();
  }
  cpu_sr_write(sr);
  return result;
}

/* Writes length characters of text to the SCI by interrupt, waiting
 * until they are out; returns how many went (0x2fda8). */
int32_t sci_puts(char* text, uint16_t length) {
  psos_get_v(SCI_SIGNAL_OUTPUT_DONE, 0, 0);
  sci.transmit_next = text;
  sci.transmit_count = length;
  if (!sci.reconfiguring) {
    qsm.sccr1 |= SCCR1_TIE;
  }
  if ((uint8_t)psos_wait_v(SCI_SIGNAL_OUTPUT_DONE, 0, 0, 0) != 0) {
    fatal_error("wait_v SIG_SOQ_SCI error in _sci_puts");
  }
  int32_t count = sci.transmit_next - text;

  sci.transmit_count = 0;
  return count;
}

/* Reads a character from the SCI's ring, waiting for one, and sends XON
 * by polling once the ring has room again (0x2fe12). */
uint8_t sci_getc(void) {
  uint8_t character;

  psos_get_v(SIG_SIQ_SCI, 0, 0);
  if (sci_ring.write == sci_ring.read &&
      (uint8_t)psos_wait_v(SIG_SIQ_SCI, 0, 0, 0) != 0) {
    fatal_error("wait_v SIG_SIQ_SCI error in getc");
  }
  character = sci_ring.data[sci_ring.read++];
  if ((sci.flow & DUART_FLOW_XOFF_SENT) != 0 &&
      (uint8_t)(sci_ring.read - sci_ring.write) > DUART_XON_ABOVE_FREE) {
    sci.flow &= ~DUART_FLOW_XOFF_SENT;
    if (sci.xon_xoff) {
      sci_putc_polled(DUART_XON);
    }
  }
  return character;
}

/* Reads a line from the SCI, a character with its top bit set taken as
 * printable, where the Z3805A's takes it as a control character
 * (0x2feaa). */
int32_t sci_gets(char* buffer, int32_t length) {
  return duart_gets(buffer, length, sci_getc, &sci.echo, &sci.last_terminator,
                    &sci.receive_errors, &sci.read_errors,
                    "SCI receiver error ocurred\r\n", false);
}

/* Applies the serial settings to the SCI: waits for its transmitter to
 * drain, then sets baud rate, parity, flow control and echo, as the
 * SCI's I/O control function (0x306c2).  sci_reconfigure is this, then
 * no error. */
static __attribute__((used, noipa)) void sci_reconfigure_settings(void) {
  /* How long it spins for the last frame to go, after the drain. */
  static const int32_t COMPLETE_SPINS = 5000;
  static const uint16_t INTERRUPTS_AND_RECEIVER =
      SCCR1_TIE | SCCR1_RIE | SCCR1_RE;

  sci.reconfiguring = 1;
  qsm.sccr1 &= ~INTERRUPTS_AND_RECEIVER;
  for (int32_t pauses = DUART_DRAIN_PAUSES;
       pauses > 0 && (qsm.scsr & SCSR_TDRE) == 0; pauses--) {
    psos_pause(1);
  }
  qsm.sccr1 &= ~SCCR1_TE;
  for (int32_t spins = COMPLETE_SPINS; spins > 0 && (qsm.scsr & SCSR_TC) == 0;
       spins--) {
  }
  sci_baud_set(sci_settings.baud);
  switch (sci_settings.parity) {
    case SERIAL_PARITY_EVEN:
      qsm.sccr1 |= SCCR1_PE;
      qsm.sccr1 &= ~SCCR1_PT;
      sci.seven_bits = 1;
      sci.mark_bit = 0;
      break;
    case SERIAL_PARITY_ODD:
      qsm.sccr1 |= SCCR1_PT | SCCR1_PE;
      sci.seven_bits = 1;
      sci.mark_bit = 0;
      break;
    case SERIAL_PARITY_ONE:
      qsm.sccr1 &= ~SCCR1_PE;
      sci.seven_bits = 1;
      sci.mark_bit = 1;
      break;
    default:
      qsm.sccr1 &= ~SCCR1_PE;
      sci.seven_bits = 0;
      sci.mark_bit = 0;
      break;
  }
  qsm.sccr1 |= SCCR1_TE;
  if (!sci_settings.xon_xoff) {
    if (sci.xon_xoff && (sci.flow & DUART_FLOW_XOFF_SENT) != 0) {
      sci_putc_polled(DUART_XON);
    }
    sci.xon_xoff = 0;
    sci.flow &= ~(DUART_FLOW_XOFF_SENT | DUART_FLOW_XOFF_RECEIVED);
  } else {
    if (!sci.xon_xoff && (sci.flow & DUART_FLOW_XOFF_SENT) != 0) {
      sci_putc_polled(DUART_XOFF);
    }
    sci.xon_xoff = 1;
  }
  sci.echo = sci_settings.echo ? sci_putc : sci_echo_none;
  qsm.sccr1 |= INTERRUPTS_AND_RECEIVER | SCCR1_TE;
  sci.reconfiguring = 0;
}

CPU_IO_FUNCTION(sci_reconfigure, sci_reconfigure_settings);

/* Writes a character to the SCI under its write lock, if it can take
 * it (0x30888). */
void sci_putc(char c) {
  char text[2] = {c, 0};

  /* sci_lock's result is positive when another holds the lock */
  if (sci_lock(1) <= 0) {
    sci_puts(text, 1);
    sci_unlock(1);
  }
}

/* Starts a block out of the SCI by interrupt without waiting, if an
 * asynchronous send is allowed and the port is free (0x308bc). */
void sci_send_async(char* block, uint8_t count) {
  if (sci.async_allowed && !sci.reconfiguring && !sci.field_13) {
    sci.async_next = block;
    sci.async_count = count;
    sci.async_pending = 1;
    qsm.sccr1 |= SCCR1_TIE;
  }
}

/* Writes length bytes of a buffer to the SCI's device, minor 1, which
 * sci_write sends counted; returns how many went; nothing calls it
 * (0x30916). */
uint32_t sci_port_write_counted(void* buffer, uint16_t length) {
  uint32_t count = length;

  psos_io_write(SCI_DEVICE_MINOR_1, buffer, &count);
  return count;
}

/* Formats into a buffer of LIBC_SHORT_PRINTF_LENGTH through vsprintf and
 * writes it to the console, the SCI, by polling: fatal_error's printer
 * (0x309f4). */
void sci_printf_polled(char* format, ...) {
  char text[LIBC_SHORT_PRINTF_LENGTH];
  va_list arguments;

  va_start(arguments, format);
  vsprintf(text, format, arguments);
  va_end(arguments);
  console_write_polled(text);
}

/* 1 unless an XOFF received holds the SCI's transmission, else 0;
 * nothing calls it (0x30a1a). */
int32_t sci_transmit_allowed(void) {
  return (sci.flow & DUART_FLOW_XOFF_RECEIVED) == 0;
}

/* Allows the SCI's asynchronous sends while time codes are sent
 * continuously, else forbids them once none is pending; nothing calls it
 * (0x30a34). */
void sci_async_allowed_update(void) {
  if (tcode_continuous) {
    sci.async_allowed = 1;
  } else if (!sci.async_pending) {
    sci.async_allowed = 0;
  }
}

/* Frees the SCI so that it can send the time code, unless an XOFF holds
 * it or another task is writing: takes and gives back the device's
 * counted write, and allows the asynchronous send; returns whether it did
 * (0x30a56). */
uint8_t sci_release_for_timecode(void) {
  if ((sci.flow & DUART_FLOW_XOFF_RECEIVED) == 0 &&
      (uint8_t)psos_io_open(SCI_DEVICE_MINOR_1, 0) == 0) {
    sci.async_allowed = 1;
    sci.field_15 = 1;
    psos_io_close(SCI_DEVICE_MINOR_1);
    return 1;
  }
  return 0;
}
