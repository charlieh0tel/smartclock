/* From the Z3805A 3543B's rewritten/sci.c and duart.h, the Z3817A 3752's own
 * beside common/'s: of the SCI port's driver, whose code is the Z3805A's SCI
 * driver's on the installer's layout (sci_driver), the routines the function
 * check finds the decompiler's reading wrong for: the polled writes, which set
 * the status register as the decompiler cannot, the line reader, duart.h's
 * duart_gets, the reconfiguration, the SCI device's control, whose error in D7
 * the decompiler cannot set, and the locked write, the Z3815A 4010's with
 * sci_puts' length (docs/firmware/z3817a-3752.md, "The function check"); and
 * sci_puts, read beside the installer's copy.  Its comments' addresses are the
 * Z3817A's.  Each keeps its generated prototype. */

/* A no-op echo, a bare rts: the SCI's init and sci_reconfigure put it in
 * sci.echo when the settings' echo is off (0x2e6fe). */
void sci_echo_none(char c) {}

/* Waits for the SCI's transmitter at IPL 3 and writes a character, its
 * top bit set for mark parity; returns the word written (0x2e750). */
uint16_t sci_putc_polled(char c) {
  static const uint16_t MARK_BIT = 0x80;
  uint16_t sr = cpu_interrupts_at(CPU_IPL_3);
  uint16_t data = c;

  while ((qsm.scsr & SCSR_TDRE) == 0) {
  }
  if (sci.mark_bit) {
    data |= MARK_BIT;
  }
  qsm.scdr = data;
  cpu_sr_write(sr);
  return data;
}

/* Reads a line from the SCI, a character with its top bit set taken as
 * printable (0x2e8ae). */
int32_t sci_gets(char* buffer, int32_t length) {
  return duart_gets(buffer, length, sci_getc, &sci.echo, &sci.last_terminator,
                    &sci.receive_errors, &sci.read_errors,
                    "SCI receiver error occurred\r\n", false);
}

/* Applies the serial settings to the SCI: waits for its transmitter to
 * drain, then sets baud rate, parity, flow control and echo, as the
 * SCI's I/O control function (0x2f0b8).  sci_reconfigure is this, then
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

/* The signal sci_puts waits on for its string to be out, 0x80 ("wait_v
 * SIG_SOQ_SCI error in _sci_puts"), where psos-signals.h's SIG_SOQ_SCI is
 * the Z3805A's. */
static const uint32_t SCI_SIGNAL_OUTPUT_DONE = 0x80;

/* Writes length characters of text to the SCI by interrupt, waiting
 * until they are out; returns how many went, none for no length
 * (0x2e7a2).  The installer's installer_sci_puts; the Z3805A 3543B's
 * writes to the NUL. */
int32_t sci_puts(char* text, uint16_t length) {
  if (length == 0) {
    return 0;
  }
  psos_get_v(SCI_SIGNAL_OUTPUT_DONE, 0, 0);
  sci.transmit_next = text;
  sci.transmit_count = length;
  if (!sci.reconfiguring) {
    qsm.sccr1 |= SCCR1_TIE;
  }
  if ((uint8_t)psos_wait_v(SCI_SIGNAL_OUTPUT_DONE, 0, 0, 0) != 0) {
    fatal_error("wait_v SIG_SOQ_SCI error in _sci_puts");
  }
  const int32_t count = sci.transmit_next - text;

  sci.transmit_count = 0;
  return count;
}

/* Writes a character to the SCI under its write lock, if it can take
 * it (0x2f27e). */
void sci_putc(char c) {
  char text[2] = {c, 0};

  /* sci_lock's result is positive when another holds the lock */
  if (sci_lock(1) <= 0) {
    sci_puts(text, 1);
    sci_unlock(1);
  }
}

/* Writes a string to the console, the SCI, by polling, in supervisor
 * mode; returns its length (0x2f3b4). */
int32_t console_write_polled(char* text) {
  int32_t count = 0;
  uint16_t sr = cpu_enter_supervisor();

  for (; *text != 0; text++, count++) {
    sci_putc_polled(*text);
  }
  cpu_sr_write(sr);
  return count;
}

/* Formats into a buffer of LIBC_SHORT_PRINTF_LENGTH through vsprintf and
 * writes it to the SCI by polling (0x2f3ea). */
void sci_printf_polled(char* format, ...) {
  char text[LIBC_SHORT_PRINTF_LENGTH];
  va_list arguments;

  va_start(arguments, format);
  vsprintf(text, format, arguments);
  va_end(arguments);
  console_write_polled(text);
}

/* Sets the SCI's baud rate from a settings code, 9600 for a code out of
 * range, and keeps a character's time at it in timer counts (0x2e700). */
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

/* Reads a character from the SCI's ring, waiting for one, and sends XON
 * by polling once the ring has room again (0x2e816). */
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

/* Writes length bytes of a buffer to the SCI's device, minor 1, which
 * sci_write sends counted; returns how many went; nothing calls it
 * (0x2f30c). */
uint32_t sci_port_write_counted(void* buffer, uint16_t length) {
  uint32_t count = length;

  psos_io_write(SCI_DEVICE_MINOR_1, buffer, &count);
  return count;
}

/* 1 unless an XOFF received holds the SCI's transmission, else 0;
 * nothing calls it (0x2f410). */
int32_t sci_transmit_allowed(void) {
  return (sci.flow & DUART_FLOW_XOFF_RECEIVED) == 0;
}

/* Starts a block out of the SCI by interrupt without waiting, if an
 * asynchronous send is allowed and the port is free (0x2f2b2). */
void sci_send_async(char* block, uint8_t count) {
  if (sci.async_allowed && !sci.reconfiguring && !sci.in_transmit_interrupt) {
    sci.async_next = block;
    sci.async_count = count;
    sci.async_pending = 1;
    qsm.sccr1 |= SCCR1_TIE;
  }
}

/* Frees the SCI so that it can send the time code, unless an XOFF holds
 * it or another task is writing: takes and gives back the device's
 * counted write, and allows the asynchronous send; returns whether it did
 * (0x2f44c). */
uint8_t sci_release_for_timecode(void) {
  if ((sci.flow & DUART_FLOW_XOFF_RECEIVED) == 0 &&
      (uint8_t)psos_io_open(SCI_DEVICE_MINOR_1, 0) == 0) {
    sci.async_allowed = 1;
    sci.field_16 = 1;
    psos_io_close(SCI_DEVICE_MINOR_1);
    return 1;
  }
  return 0;
}
