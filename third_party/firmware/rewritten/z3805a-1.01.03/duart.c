/* The DUART, both channels: its self-test, written by hand in place of
 * the decompiler's (docs/firmware/README.md, "Hand-written C";
 * docs/firmware/z3805a-1.01.03.md, "The export and the checks"), read
 * from this image's code.  The exported C includes this file after its
 * own, so the types and prototypes it declares are in scope; each
 * function here keeps its generated prototype.
 * With them, from the Z3815A 4010's C, the functions this image's code
 * carries identical or relocated (docs/firmware/z3805a-1.01.03.md, "The
 * link list"), their comments' addresses this image's but where they
 * name another image. */

/* The characters of a loopback string a failure's log entry gives: two
 * longs' worth.  An array size, so #define. */
#define DUART_LOOPBACK_LOGGED 8

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

/* Logs a failed loopback's first received characters as two longs. */
static void duart_test_log_received(char* format, const char* received) {
  union {
    char characters[DUART_LOOPBACK_LOGGED];
    uint32_t longs[DUART_LOOPBACK_LOGGED / sizeof(uint32_t)];
  } logged;

  for (int32_t index = 0; index < DUART_LOOPBACK_LOGGED; index++) {
    logged.characters[index] = received[index];
  }
  log_write(format, logged.longs[0], logged.longs[1]);
}

/* The DUART self-test: its interrupt vector register with interrupts
 * masked, then its counter and both channels' loopbacks; with log set,
 * a failure goes to the log, with the counter's reading and each failed
 * channel's received string.  The decompiler's passes the log the
 * strings' longs as words (0x1e76a; 0x1e832, 0x1e862). */
void duart_test(char log) {
  selftest.uart = 0;
  uint16_t sr = set_interrupt_mask(CPU_IPL_7);

  duart_ivr_check();
  set_interrupt_mask(sr);
  if (selftest.uart == 0) {
    duart_counter_test();
    duart_loopback_test_a();
    duart_loopback_test_b();
  }
  if (selftest.uart == 0 || !log) {
    return;
  }
  log_write("DUART fail, code = %04X", (uint16_t)selftest.uart);
  if ((selftest.uart & SELFTEST_UART_COUNTER) != 0) {
    log_write("DUART counter fail, count = %d", duart_counter_reading);
  }
  if ((selftest.uart & SELFTEST_UART_CHANNEL_A_DATA) != 0) {
    duart_test_log_received("DUARTA string fail, %08X%08X",
                            loopback_received_a);
  }
  if ((selftest.uart & SELFTEST_UART_CHANNEL_B_DATA) != 0) {
    duart_test_log_received("DUARTB string fail, %08X%08X",
                            loopback_received_b);
  }
}

/* Sets or clears output port bits: the register holds each pin's
 * complement, so a bit kept set in the shadow is reset in the port
 * (MC68681 4.2.12).  While duart_shadow.field_0c is set the shadow is
 * duart_shadow.output_reset, kept in supervisor mode; else
 * selftest.lamp_mask, as the reset code keeps it.  The monitors call
 * it, for OP1, OP2 and OP5 (monitors.c); the Z3805A's duart_op0_set and
 * duart_op1_set hold its first branch for one bit each (0x1e0e8). */
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

/* The DUART's input port (MC68681 4.3.13); nothing calls it (0x1e0d8). */
uint8_t duart_input_port_read(void) { return duart.ip_opcr; }

/* The 58503A's boot_duart_early_init in the primary, over selftest.uart:
 * tests the interrupt vector register and sets its vector, then, if it
 * held the patterns, sets channel B up for 9600 8N1, the counter clocked
 * from channel B's transmitter, and checks it in loopback (0x1eeae). */
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

/* Resets the output port's bits 2 to 7 in reset and sets those in set,
 * as duart_output_bit_set does (0x1e212). */
void duart_output_bits(board_led reset, board_led set) {
  const uint16_t sr = enter_supervisor_mode();
  uint8_t resets = (reset | duart_shadow.output_reset) & DUART_OP_BITS;

  duart.stop_ropbc = resets;
  duart_shadow.output_reset =
      (reset & DUART_OP_BITS | duart_shadow.output_reset) &
      ~(set & DUART_OP_BITS);
  duart.start_sopbc = ~duart_shadow.output_reset & DUART_OP_BITS;
  set_interrupt_mask(sr);
}

/* The output port's bit 0, OP0, by duart_shadow.output_reset. */
static const uint8_t DUART_OP0 = 0x01;

/* Drives OP0: for a nonzero level clears its bit of
 * duart_shadow.output_reset and sets the output port's bit, else sets
 * the shadow's and resets the port's, interrupts not held off; nothing
 * calls it (0x1e0ae). */
void duart_op0_write(char level) {
  if (level != 0) {
    duart_shadow.output_reset &= ~DUART_OP0;
    duart.start_sopbc = DUART_OP0;
  } else {
    duart_shadow.output_reset |= DUART_OP0;
    duart.stop_ropbc = DUART_OP0;
  }
}

/* The output port's bits as last reset, duart_shadow.output_reset;
 * nothing calls it (0x1e0e0). */
uint8_t duart_output_reset_read(void) { return duart_shadow.output_reset; }

/* Preloads the DUART's counter with 0xffff, both halves, shadowed. */
static void duart_counter_preload(void) {
  static const uint8_t COUNTER_PRELOAD = 0xff; /* each half */

  duart_shadow.ctur = COUNTER_PRELOAD;
  duart_shadow.ctlr = COUNTER_PRELOAD;
  duart.cur_ctur = duart_shadow.ctur;
  duart.clr_ctlr = duart_shadow.ctlr;
}

/* The DUART self-test's counter test: counts ten ticks, fails if the
 * count left is out of range, and keeps it in duart_counter_reading
 * (0x1e688). */
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
  duart_counter_reading = count;
}

/* Stops both DUART channels and masks its interrupts. */
void duart_stop(void) {
  duart.a.cr = CR_RX_DISABLE;
  duart.a.cr = CR_TX_DISABLE;
  duart.b.cr = CR_RX_DISABLE;
  duart.b.cr = CR_TX_DISABLE;
  duart.isr_imr = 0;
}
