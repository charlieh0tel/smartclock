/* The SCI port's driver, the MC68331's serial port, I/O device 3 here
 * (docs/firmware/z3805a-1.01.03.md, "Layout and chip selects"): its
 * init, reconfiguration, interrupt-driven write and line reader, written
 * by hand in place of the decompiler's (docs/firmware/README.md,
 * "Hand-written C").  Each is read from this image's code: the Z3815A
 * 4010's C where the instructions are theirs, this image's own where
 * they differ.  The exported C includes this file after its own, so the
 * types and prototypes it declares are in scope; each function here
 * keeps its generated prototype.  The driver's state is smartclock.h's
 * sci_driver; the registers are mc68331.h's.  It uses the control
 * characters, codes and line reader duart.h defines and cpu.h's status
 * register helpers.
 * With them, from the 58503B 1.01.04's and the Z3815A 4010's C, the functions
 * this image's code carries identical or relocated
 * (docs/firmware/z3805a-1.01.03.md, "The link list"), their comments' addresses
 * this image's but where they name another image. */

/* The signal sci_puts waits on for its string to be out ("wait_v
 * SIG_SOQ_SCI error in _sci_puts", 0x1f7da), where psos-signals.h's
 * SIG_SOQ_SCI is 0x200. */
static const uint32_t SCI_SIGNAL_OUTPUT_DONE = 0x80;

/* SCCR1's interrupts and receiver, which a reconfiguration holds off. */
static const uint16_t SCI_INTERRUPTS_AND_RECEIVER =
    SCCR1_TIE | SCCR1_RIE | SCCR1_RE;

/* The length of sci_init's fatal message: an array size, so #define. */
#define SCI_MESSAGE_SIZE 80

/* Writes length characters of text to the SCI by interrupt, waiting
 * until they are out; returns how many went, 0 at once for none
 * (0x1f7cc). */
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
  int32_t count = sci.transmit_next - text;

  sci.transmit_count = 0;
  return count;
}

/* Reads a line from the SCI (0x1f8d8). */
int32_t sci_gets(char* buffer, int32_t length) {
  return duart_gets(buffer, length, sci_getc, &sci.echo, &sci.last_terminator,
                    &sci.receive_errors, &sci.read_errors,
                    "SCI receiver error occurred\r\n", false);
}

/* Fails with "Unable to create" or "Unable to initialize" and the
 * exchange's name when an exchange call fails. */
static void sci_exchange_check(uint32_t error, char* format) {
  /* An exchange call's error, its low byte. */
  static const uint32_t ERROR_MASK = 0xff;
  char message[SCI_MESSAGE_SIZE];

  error &= ERROR_MASK;
  if (error != 0) {
    sprintf(message, format, error);
    fatal_error(message);
  }
}

/* Creates one of the SCI's exchanges and sends it its token. */
static void sci_exchange_create(char* name, uint32_t* exchange,
                                exchange_token* token, char* create_failed,
                                char* send_failed) {
  sci_exchange_check(psos_create_x(name, 0, 0, exchange), create_failed);
  token->status = 0;
  token->exchange = *exchange;
  token->task = 0;
  token->field_0c = 0;
  sci_exchange_check(psos_send_x(*exchange, token), send_failed);
}

/* The SCI device's init, its function 0: applies sci_settings' baud
 * rate and parity, enables the port, waits, clears what it received,
 * creates sciR and sciW and sends their tokens, and clears its state
 * (0x1fe7a).  sci_init keeps the scratch registers around it. */
static __attribute__((used, noipa)) void sci_init_kept(void) {
  /* How long it spins after enabling the port. */
  static const int32_t SETTLE_SPINS = 100000;
  static const uint16_t RECEIVER_AND_TRANSMITTER =
      SCCR1_TE | SCCR1_RE | SCCR1_RIE;

  sci_baud_set(sci_settings.baud);
  if (sci_settings.parity == SERIAL_PARITY_NONE) {
    sci.seven_bits = 0;
    sci.mark_bit = 0;
  } else if (sci_settings.parity == SERIAL_PARITY_ONE) {
    sci.seven_bits = 1;
    sci.mark_bit = 1;
  } else {
    sci.seven_bits = 1;
    sci.mark_bit = 0;
    if (sci_settings.parity == SERIAL_PARITY_EVEN) {
      qsm.sccr1 = SCCR1_PE;
    } else if (sci_settings.parity == SERIAL_PARITY_ODD) {
      qsm.sccr1 = SCCR1_PT | SCCR1_PE;
    }
  }
  qsm.sccr1 |= RECEIVER_AND_TRANSMITTER;
  for (volatile int32_t spins = 0; ++spins < SETTLE_SPINS;) {
  }
  (void)qsm.scsr;
  (void)qsm.scdr;
  sci_exchange_create("sciR", &sci_exchanges.read_exchange,
                      &sci_exchanges.read_token, "Unable to create sciR: %X",
                      "Unable to initialize sciR: %X");
  sci_exchange_create("sciW", &sci_exchanges.write_exchange,
                      &sci_exchanges.write_token, "Unable to create sciW: %X",
                      "Unable to initialize sciW: %X");
  sci.async_next = "";
  sci.async_count = 0;
  sci.async_pending = 0;
  sci.transmit_next = "";
  sci.transmit_count = 0;
  sci.field_0c = 0;
  sci_ring.write = 0;
  sci_ring.read = 0;
  sci.flow = 0;
  sci.receive_errors = 0;
  sci.xon_xoff = sci_settings.xon_xoff ? 1 : 0;
  sci.reconfiguring = 0;
  sci.async_allowed = tcode_continuous ? 1 : 0;
  sci.released_for_timecode = 0;
  sci.echo = sci_settings.echo ? sci_putc : sci_echo_none;
}

CPU_KEEPING_SCRATCH(sci_init, sci_init_kept);

/* Applies the serial settings to the SCI: waits for its transmitter to
 * drain, then sets baud rate, parity, flow control and echo, as the
 * SCI's I/O control function (0x200da).  Where the Z3815A 4010's clears
 * both flow bits with flow control off, this clears the XOFF received
 * alone, and then the XOFF sent while flow control is off (0x201f2 to
 * 0x20202); it turns the interrupts back on at level 7.
 * sci_reconfigure is this, then no error. */
static __attribute__((used, noipa)) void sci_reconfigure_settings(void) {
  /* How long it spins for the last frame to go, after the drain. */
  static const int32_t COMPLETE_SPINS = 5000;

  sci.reconfiguring = 1;
  qsm.sccr1 &= ~SCI_INTERRUPTS_AND_RECEIVER;
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
  if (sci_settings.xon_xoff) {
    if (!sci.xon_xoff && (sci.flow & DUART_FLOW_XOFF_SENT) != 0) {
      sci_putc_polled(DUART_XOFF);
    }
    sci.xon_xoff = 1;
  } else {
    if (sci.xon_xoff && (sci.flow & DUART_FLOW_XOFF_SENT) != 0) {
      sci_putc_polled(DUART_XON);
    }
    sci.xon_xoff = 0;
    sci.flow &= ~DUART_FLOW_XOFF_RECEIVED;
  }
  if ((sci.flow & DUART_FLOW_XOFF_SENT) != 0 && !sci.xon_xoff) {
    sci.flow &= ~DUART_FLOW_XOFF_SENT;
  }
  sci.echo = sci_settings.echo ? sci_putc : sci_echo_none;
  uint16_t sr = cpu_interrupts_raise(CPU_IPL_7);

  qsm.sccr1 |= SCI_INTERRUPTS_AND_RECEIVER | SCCR1_TE;
  sci.reconfiguring = 0;
  cpu_sr_write(sr);
}

CPU_IO_FUNCTION(sci_reconfigure, sci_reconfigure_settings);

/* Writes a character to the SCI under its write lock, if it can take
 * it (0x202b2). */
void sci_putc(char c) {
  char text[2] = {c, 0};

  /* sci_lock's result is positive when another holds the lock */
  if (sci_lock(1) <= 0) {
    sci_puts(text, 1);
    sci_unlock(1);
  }
}

/* Waits for the SCI's transmitter at IPL 3 and writes a character, its
 * top bit set for mark parity; returns the word written (0x1f77a). */
uint16_t sci_putc_polled(char c) {
  uint16_t sr = cpu_interrupts_at(CPU_IPL_3);
  uint16_t data = c;

  while ((qsm.scsr & SCSR_TDRE) == 0) {
  }
  if (sci.mark_bit) {
    data |= SCI_MARK_BIT;
  }
  qsm.scdr = data;
  cpu_sr_write(sr);
  return data;
}

/* Formats into a buffer of LIBC_SHORT_PRINTF_LENGTH through vsprintf and
 * writes it to the SCI by polling (0x20420). */
void sci_printf_polled(char* format, ...) {
  char text[LIBC_SHORT_PRINTF_LENGTH];
  va_list arguments;

  va_start(arguments, format);
  vsprintf(text, format, arguments);
  va_end(arguments);
  sci_puts_polled(text);
}

/* Sets the SCI's baud rate from a settings code, 9600 for a code out of
 * range, and keeps a character's time at it in timer counts (0x1f72a). */
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
 * by polling once the ring has room again (0x1f840). */
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

/* 1 unless an XOFF received holds the SCI's transmission, else 0;
 * nothing calls it (0x20446). */
int32_t sci_transmit_allowed(void) {
  return (sci.flow & DUART_FLOW_XOFF_RECEIVED) == 0;
}

/* Starts a block out of the SCI by interrupt without waiting, if an
 * asynchronous send is allowed; the transmit interrupt is left off while
 * sci_reconfigure changes the port, the block pending all the same.
 * The Z3816A's starts none while the port is reconfigured or its
 * interrupt busy (0x202e6). */
void sci_send_async(char* block, uint8_t count) {
  if (!sci.async_allowed) {
    return;
  }
  sci.async_next = block;
  sci.async_count = count;
  sci.async_pending = 1;
  if (!sci.reconfiguring) {
    qsm.sccr1 |= SCCR1_TIE;
  }
}

/* Allows the SCI's asynchronous sends while time codes are sent
 * continuously, else forbids them once none is pending, the Z3816A's;
 * tcode_continuous_done calls it (0x20460). */
void sci_async_allowed_update(void) {
  if (tcode_continuous) {
    sci.async_allowed = 1;
  } else if (!sci.async_pending) {
    sci.async_allowed = 0;
  }
}

/* Writes a string to the SCI's device, the kernel's I/O device 3;
 * returns its length (0x20318). */
uint32_t sci_port_write(void* text) {
  uint32_t length;

  psos_io_write(IO_DEVICE_SCI, text, &length);
  return length;
}

/* Reads up to so many characters from the SCI's device, and its last
 * read errors; the count read (0x2038c). */
uint32_t sci_port_read(void* buffer, uint32_t length, uint8_t* errors) {
  psos_io_read(IO_DEVICE_SCI, buffer, &length);
  if (errors != 0) {
    *errors = sci.read_errors;
  }
  return length;
}

/* sci.echo with the echo off: nothing (0x1f728). */
void sci_echo_none(void) {}

/* Opens the SCI's device (0x203b8). */
void sci_port_open(void) { psos_io_open(IO_DEVICE_SCI, 0); }

/* Closes the SCI's device (0x203c8). */
void sci_port_close(void) { psos_io_close(IO_DEVICE_SCI); }

/* Formats into a buffer of LIBC_PRINTF_LENGTH through vsprintf and
 * writes it to the SCI's device (0x20366). */
void sci_printf(char* format, ...) {
  char text[LIBC_PRINTF_LENGTH];
  va_list arguments;

  va_start(arguments, format);
  vsprintf(text, format, arguments);
  va_end(arguments);
  sci_port_write(text);
}

/* The done hook of the SCI's settings: has its device take them as they
 * now stand, through its control (0x203d6). */
void sci_settings_done(void) { psos_io_control(IO_DEVICE_SCI, 0, 0); }

/* Writes a string to the SCI by polling, in supervisor mode; returns
 * its length (0x203ea). */
int32_t sci_puts_polled(char* s) {
  int32_t count = 0;
  uint16_t sr = cpu_enter_supervisor();

  for (; *s != 0; s++, count++) {
    sci_putc_polled(*s);
  }
  cpu_sr_write(sr);
  return count;
}

/* The SCI's device, minor 1: the counted write. */
static const uint32_t SCI_DEVICE_MINOR_1 = IO_DEVICE_SCI | 1;

/* Writes length bytes of a buffer to the SCI's device, minor 1, counted;
 * returns how many went (0x2033a). */
uint32_t sci_port_write_counted(void* buffer, uint16_t length) {
  uint32_t count = length;

  psos_io_write(SCI_DEVICE_MINOR_1, buffer, &count);
  return count;
}
