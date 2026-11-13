/* From the Z3815A 4010's rewritten/duart.c, the Z3817A 3752's own beside
 * common/'s: of its functions, those the Z3817A's code carries identical or
 * relocated (docs/firmware/z3817a-3752.md, "The function check").  Its
 * comments' addresses are the Z3817A's, but where they name another image.  And
 * the DUART's init, its self-test and counter test, this image's own, read
 * beside the Z3815A's.  Each keeps its generated prototype. */

/* Sets or clears output port bits: the register holds each pin's
 * complement, so a bit kept set in the shadow is reset in the port
 * (MC68681 4.2.12).  While duart_shadow.field_0c is set the shadow is
 * duart_shadow.output_reset, kept in supervisor mode; else
 * selftest.lamp_mask, as the reset code keeps it.  Nothing calls it
 * here; the Z3805A's duart_op0_set and
 * duart_op1_set hold its first branch for one bit each (0x2d020). */
void duart_output_bit_set(uint8_t bits, char set) {
  if (duart_shadow.field_0c == 0) {
    if (set) {
      selftest.lamp_mask |= bits;
      duart.stop_ropbc = bits;
    } else {
      selftest.lamp_mask &= ~bits;
      duart.start_sopbc = bits;
    }
    return;
  }
  const uint16_t sr = enter_supervisor_mode();

  if (set) {
    duart_shadow.output_reset |= bits;
    duart.stop_ropbc = bits;
  } else {
    duart_shadow.output_reset &= ~bits;
    duart.start_sopbc = bits;
  }
  set_interrupt_mask(sr);
}

/* The DUART's input port (MC68681 4.3.13); nothing calls it (0x2d010). */
uint8_t duart_input_port_read(void) { return duart.ip_opcr; }

/* The output port bits duart_output_bits drives here, OP6 and OP7, the
 * DUART's and the self-tests' lamps (0x2d160), where the 58503A's drives
 * OP2 to OP7. */
static const uint8_t DUART_OUTPUT_BITS_LAMPS =
    BOARD_LED_DUART | BOARD_LED_SELFTEST;

/* Resets the output port's OP6 and OP7 in reset and sets those in set,
 * in supervisor mode; the shadow takes reset's bits before the reset
 * register is written, where the 58503A's writes the register first
 * (0x2d14a). */
void duart_output_bits(board_led reset, board_led set) {
  uint16_t sr = enter_supervisor_mode();

  duart_shadow.output_reset |= reset & DUART_OUTPUT_BITS_LAMPS;
  duart.stop_ropbc = duart_shadow.output_reset & DUART_OUTPUT_BITS_LAMPS;
  duart_shadow.output_reset &= ~(set & DUART_OUTPUT_BITS_LAMPS);
  duart.start_sopbc = ~duart_shadow.output_reset & DUART_OUTPUT_BITS_LAMPS;
  set_interrupt_mask(sr);
}

/* The DUART's interrupt vector register tested with two patterns, a
 * failure set in selftest.uart, then its vector set. */
static void duart_ivr_check(void) {
  duart.ivr = DUART_IVR_PATTERN_1;
  if (duart.ivr != DUART_IVR_PATTERN_1) {
    selftest.uart |= SELFTEST_UART_REGISTER;
  }
  duart.ivr = DUART_IVR_PATTERN_2;
  if (duart.ivr != DUART_IVR_PATTERN_2) {
    selftest.uart |= SELFTEST_UART_REGISTER;
  }
  duart.ivr = DUART_IVR_VECTOR;
}

/* The 58503A's boot_duart_early_init in the primary, over selftest.uart:
 * tests the interrupt vector register and sets its vector, then, if it
 * held the patterns, sets channel B up for 9600 8N1, the counter clocked
 * from channel B's transmitter, and checks it in loopback (0x2dbfc). */
void duart_early_init(void) {
  selftest.uart = 0;
  duart_ivr_check();
  if (selftest.uart == 0) {
    duart.b.cr = CR_RESET_RECEIVER;
    duart.b.cr = CR_RESET_TRANSMITTER;
    duart.b.cr = CR_RESET_MR_POINTER;
    duart.b.mr = MR1_PARITY_NONE | DUART_MR1_EIGHT_BITS;
    duart.b.mr = DUART_MR2_ONE_STOP_BIT;
    duart.b.sr_csr = DUART_CSR_9600 << DUART_CSR_CLOCK_BITS | DUART_CSR_9600;
    duart.ipcr_acr = ACR_BRG_SET_2 | DUART_ACR_COUNTER_TXCB;
    duartb_loopback_check();
  }
}

/* The output port bits duart_output_bit_set keeps (0x2d018); nothing
 * calls it. */
uint8_t duart_output_bits_get(void) { return duart_shadow.output_reset; }

/* The DUART's counter preloaded with 0xffff: each half. */
static const uint8_t DUART_COUNTER_PRELOAD = 0xff;

/* Preloads the DUART's counter with 0xffff, both halves, shadowed. */
static void duart_counter_preload(void) {
  duart_shadow.ctur = DUART_COUNTER_PRELOAD;
  duart_shadow.ctlr = DUART_COUNTER_PRELOAD;
  duart.cur_ctur = duart_shadow.ctur;
  duart.clr_ctlr = duart_shadow.ctlr;
}

/* The DUART self-test's counter test: counts ten ticks, fails if the
 * count left is out of range, and keeps it in duart_counter_count
 * (0x2d5c0).  The Z3815A 4010's but for that store. */
void duart_counter_test(void) {
  /* The counter test: the 68681's counter, preloaded with 0xffff,
   * counts down from the crystal over 16 for ten ticks, at the highest
   * priority. */
  static const uint32_t COUNTER_PAUSES = 10;
  static const uint16_t COUNTER_LEAST = 0xf533;
  static const uint16_t COUNTER_MOST = 0xf8cc;
  static const uint32_t PRIORITY_HIGHEST = 0xff;
  uint8_t priority;
  uint8_t upper;
  uint8_t lower;
  uint16_t count;

  (void)duart.stop_ropbc;
  duart_counter_preload();
  psos_priority(OS_TASK_SELF, PRIORITY_HIGHEST, &priority);
  duart_shadow.acr =
      (duart_shadow.acr & ~ACR_COUNTER_MASK) | DUART_ACR_COUNTER_CRYSTAL_16;
  duart.ipcr_acr = duart_shadow.acr;
  (void)duart.start_sopbc;
  psos_pause(COUNTER_PAUSES);
  (void)duart.stop_ropbc;
  upper = duart.cur_ctur;
  lower = duart.clr_ctlr;
  duart_shadow.acr =
      (duart_shadow.acr & ~ACR_COUNTER_MASK) | DUART_ACR_COUNTER_TXCB;
  duart.ipcr_acr = duart_shadow.acr;
  duart_counter_preload();
  task_set_priority(OS_TASK_SELF, priority);
  count = upper << CPU_BYTE_BITS | lower;
  if (count > COUNTER_MOST || count < COUNTER_LEAST) {
    selftest.uart |= SELFTEST_UART_COUNTER;
  }
  duart_counter_count = count;
}

/* Logs a loopback string's failure with the first 8 bytes of what came
 * back, as two big-endian longs in hexadecimal. */
static void duart_test_log_received(char* format, const char* received) {
  enum { LOGGED_BYTES = 8 }; /* an array size */
  union {
    uint8_t bytes[LOGGED_BYTES];
    uint32_t longs[2];
  } head;

  for (int32_t index = 0; index < LOGGED_BYTES; index++) {
    head.bytes[index] = received[index];
  }
  log_write(format, head.longs[0], head.longs[1]);
}

/* The DUART self-test: its interrupt vector register, interrupts held
 * off, then while it holds its counter and both channels' loopbacks;
 * a failure is logged with its code, and the count or the string that
 * failed (0x2d6a2).  The Z3815A 4010's tests the register alone and logs
 * nothing. */
void duart_test(void) {
  uint16_t sr;

  selftest.uart = 0;
  sr = set_interrupt_mask(CPU_IPL_7);
  duart_ivr_check();
  set_interrupt_mask(sr);
  if (selftest.uart == 0) {
    duart_counter_test();
    duart_loopback_test_a();
    duart_loopback_test_b();
  }
  if (selftest.uart == 0) {
    return;
  }
  log_write("DUART fail, code = %04X", (uint16_t)selftest.uart);
  if ((selftest.uart & SELFTEST_UART_COUNTER) != 0) {
    log_write("DUART counter fail, count = %d", (uint32_t)duart_counter_count);
  }
  if ((selftest.uart & SELFTEST_UART_CHANNEL_A_DATA) != 0) {
    duart_test_log_received("DUARTA string fail, %08X%08X",
                            duarta_loopback_received);
  }
  if ((selftest.uart & SELFTEST_UART_CHANNEL_B_DATA) != 0) {
    duart_test_log_received("DUARTB string fail, %08X%08X",
                            duartb_loopback_received);
  }
}

/* The DUART's init, which primary_start calls (0x2ddda): the loopback
 * tests' flags and duart_shadow cleared, its counter halves 0xff; both
 * channels reset and set to 8N1, channel A at 9600 baud and B at 19200,
 * the counter from TxCB; the vector, the output port configuration and
 * the output port from selftest.lamp_mask; the counter preloaded, both
 * receive interrupts and both channels on.  Then a spin, both receivers
 * drained and both drivers and rings cleared, and duart_shadow.field_0c
 * set, which keeps duart_output_bit_set's bits in the shadow. */
void duart_channels_init(void) {
  static const uint8_t MODE_1 = MR1_PARITY_NONE | DUART_MR1_EIGHT_BITS;
  static const uint8_t CLOCK_A =
      DUART_CSR_9600 << DUART_CSR_CLOCK_BITS | DUART_CSR_9600;
  static const uint8_t CLOCK_B =
      DUART_CSR_19200 << DUART_CSR_CLOCK_BITS | DUART_CSR_19200;
  static const uint8_t AUXILIARY = ACR_BRG_SET_2 | DUART_ACR_COUNTER_TXCB;
  static const uint8_t RECEIVE_INTERRUPTS = IMR_RXRDYB | IMR_RXRDYA;
  static const uint8_t LEAST_FREE_NONE = 0xff;
  /* The spin before the receivers are drained (0x2df10 to 0x2df1a). */
  static const uint32_t SETTLE_SPINS = 100000;

  duarta.loopback_test = 0;
  duartb.loopback_test = 0;
  duart_shadow.mr1a = 0;
  duart_shadow.mr2a = 0;
  duart_shadow.csra = 0;
  duart_shadow.acr = 0;
  duart_shadow.imr = 0;
  duart_shadow.ctur = DUART_COUNTER_PRELOAD;
  duart_shadow.ctlr = DUART_COUNTER_PRELOAD;
  duart_shadow.mr1b = 0;
  duart_shadow.mr2b = 0;
  duart_shadow.csrb = 0;
  duart_shadow.opcr = 0;
  duart.a.cr = CR_RESET_RECEIVER;
  duart.a.cr = CR_RESET_TRANSMITTER;
  duart.b.cr = CR_RESET_RECEIVER;
  duart.b.cr = CR_RESET_TRANSMITTER;
  duart_shadow.mr1a = MODE_1;
  duart_shadow.mr2a = DUART_MR2_ONE_STOP_BIT;
  duart.a.cr = CR_RESET_MR_POINTER;
  duart.a.mr = duart_shadow.mr1a;
  duart.a.mr = duart_shadow.mr2a;
  duart_shadow.mr1b = MODE_1;
  duart_shadow.mr2b = DUART_MR2_ONE_STOP_BIT;
  duart.b.cr = CR_RESET_MR_POINTER;
  duart.b.mr = duart_shadow.mr1b;
  duart.b.mr = duart_shadow.mr2b;
  duart_shadow.csra = CLOCK_A;
  duart.a.sr_csr = duart_shadow.csra;
  duart_shadow.csrb = CLOCK_B;
  duart.b.sr_csr = duart_shadow.csrb;
  duart_shadow.acr = AUXILIARY;
  duart.ipcr_acr = duart_shadow.acr;
  duart.ivr = DUART_IVR_VECTOR;
  duart_shadow.opcr = 0;
  duart.ip_opcr = duart_shadow.opcr;
  duart_shadow.output_reset = selftest.lamp_mask;
  duart.stop_ropbc = duart_shadow.output_reset;
  duart.start_sopbc = ~duart_shadow.output_reset;
  (void)duart.stop_ropbc;
  duart.cur_ctur = duart_shadow.ctur;
  duart.clr_ctlr = duart_shadow.ctlr;
  duart_shadow.imr = RECEIVE_INTERRUPTS;
  duart.isr_imr = duart_shadow.imr;
  duart.a.cr = CR_TX_ENABLE | CR_RX_ENABLE;
  duart.b.cr = CR_TX_ENABLE | CR_RX_ENABLE;
  for (volatile uint32_t spins = 0; ++spins < SETTLE_SPINS;) {
  }
  while ((duart.a.sr_csr & SR_RXRDY) != 0) {
    (void)duart.a.rb_tb;
  }
  while ((duart.b.sr_csr & SR_RXRDY) != 0) {
    (void)duart.b.rb_tb;
  }
  duarta.transmit_next = (uint8_t*)loopback_idle_text;
  duarta.transmit_count = 0;
  duarta_ring.write = 0;
  duarta_ring.read = 0;
  duarta.overrun = 0;
  duarta.least_free = LEAST_FREE_NONE;
  duartb.transmit_next = loopback_idle_text;
  duartb.transmit_count = 0;
  duartb.field_07 = 0;
  duartb_ring.write = 0;
  duartb_ring.read = 0;
  duartb_receive_errors = 0;
  duartb.least_free = LEAST_FREE_NONE;
  duarta.held = 0;
  duartb.held = 0;
  duart_shadow.field_0c = 1;
}
