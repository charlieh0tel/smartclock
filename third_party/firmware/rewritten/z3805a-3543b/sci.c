/* The SCI port's driver, the MC68331's serial port, written by hand in
 * place of the decompiler's (docs/firmware/README.md, "Hand-written
 * C"): its baud rate, its polled and interrupt-driven writes and its
 * reads.  The exported C includes this file after its own, so the types
 * and prototypes it declares are in scope; each function here keeps its
 * generated prototype.  The driver's state is
 * docs/firmware/structures.md's "The SCI port's driver"; the registers
 * are mc68331.h's.  It uses the control characters, codes and line reader
 * duart.h defines and cpu.h's status register helpers. */

/* A no-op echo, a bare rts: sci_init and sci_reconfigure put it in
 * sci.echo when the settings' echo is off (0x1f6c8). */
void sci_echo_none(char c) {}

/* Sets the SCI's baud rate from a settings code, 9600 for a code out of
 * range (0x1f6ca). */
void sci_baud_set(serial_baud baud) {
  /* The SCI's baud divisors for SCCR0 (MC68331UM D.4.4), and the
   * per-baud value sci_baud_set keeps beside each. */
  static const uint16_t DIVISOR_1200 = 0x1b5;
  static const uint16_t DIVISOR_2400 = 0xda;
  static const uint16_t DIVISOR_9600 = 0x37;
  static const uint16_t DIVISOR_19200 = 0x1b;
  static const uint16_t CONSTANT_1200 = 0x2222;
  static const uint16_t CONSTANT_2400 = 0x1111;
  static const uint16_t CONSTANT_9600 = 0x444;
  static const uint16_t CONSTANT_19200 = 0x222;
  uint16_t divisor;

  switch (baud) {
    case SERIAL_BAUD_1200:
      divisor = DIVISOR_1200;
      sci.baud_constant = CONSTANT_1200;
      break;
    case SERIAL_BAUD_2400:
      divisor = DIVISOR_2400;
      sci.baud_constant = CONSTANT_2400;
      break;
    case SERIAL_BAUD_19200:
      divisor = DIVISOR_19200;
      sci.baud_constant = CONSTANT_19200;
      break;
    default:
      divisor = DIVISOR_9600;
      sci.baud_constant = CONSTANT_9600;
      break;
  }
  qsm.sccr0 = divisor;
}

/* Waits for the SCI's transmitter and writes a character, its top bit
 * set for mark parity; returns the word written (0x1f71a). */
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

/* Writes a string to the SCI by interrupt, to its NUL, waiting until it
 * is out; returns how much went (0x1f76c). */
int32_t sci_puts(char* text) {
  int32_t count;

  psos_get_v(SIG_SOQ_SCI, 0, 0);
  sci.transmit_next = text;
  if (!sci.reconfiguring) {
    qsm.sccr1 |= SCCR1_TIE;
  }
  if ((uint8_t)psos_wait_v(SIG_SOQ_SCI, 0, 0, 0) != 0) {
    fatal_error("wait_v SIG_SOQ_SCI error in _sci_puts");
  }
  count = sci.transmit_next - text;
  sci.transmit_next = "";
  return count;
}

/* Reads a character from the SCI's ring, waiting for one, and sends XON
 * by polling once the ring has room again (0x1f7d0). */
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

/* Reads a line from the SCI (0x1f868). */
int32_t sci_gets(char* buffer, int32_t length) {
  return duart_gets(buffer, length, sci_getc, &sci.echo, &sci.last_terminator,
                    &sci.receive_errors, &sci.read_errors,
                    "SCI receiver error ocurred\r\n", true);
}

/* Applies the serial settings to the SCI: waits for its transmitter to
 * drain, then sets baud rate, parity, flow control and echo
 * (0x1fffa). */
void sci_reconfigure(void) {
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

/* Writes a character to the SCI under its write lock, if it can take
 * it (0x201b0). */
void sci_putc(char c) {
  char text[2] = {c, 0};

  /* sci_lock's result is positive when another holds the lock */
  if (sci_lock(1) <= 0) {
    sci_puts(text);
    sci_unlock(1);
  }
}

/* Starts a block out of the SCI by interrupt without waiting, if an
 * asynchronous send is allowed and the port is free (0x201e0). */
void sci_send_async(char* block, uint8_t count) {
  if (sci.async_allowed && !sci.reconfiguring && !sci.field_11) {
    sci.async_next = block;
    sci.async_count = count;
    sci.async_pending = 1;
    qsm.sccr1 |= SCCR1_TIE;
  }
}

/* Writes a string to the SCI's device; returns its length
 * (0x2021a). */
uint32_t sci_port_write(void* text) {
  uint32_t length;

  psos_io_write(IO_DEVICE_SCI, text, &length);
  return length;
}

/* The done hook of the SCI's setting descriptors and its PRESet:
 * reconfigures device 3 (0x202ac). */
void sci_settings_done(void) { psos_io_control(IO_DEVICE_SCI, 0, 0); }

/* Writes a string to the SCI by polling, in supervisor mode; returns
 * its length (0x202c0). */
int32_t sci_puts_polled(char* text) {
  int32_t count = 0;
  uint16_t sr = enter_supervisor();

  for (; *text != 0; text++, count++) {
    sci_putc_polled(*text);
  }
  cpu_sr_write(sr);
  return count;
}
