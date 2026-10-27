/* What the MC68681 DUART's two channels share, written by hand in place
 * of the decompiler's (docs/firmware/README.md, "Hand-written C";
 * docs/firmware/modules.md, `devices/duart`).  The exported C includes
 * this file after its own, so the types and prototypes it declares are
 * in scope; each function here keeps its generated prototype.  It is the
 * early set-up, the output port and the GP port's select, duart_test,
 * which runs both channels' self-tests (duarta.c's and duartb.c's), and
 * the stop.  The drivers' state is
 * docs/firmware/structures.md's "The DUART's shadows"; the registers are
 * mc68681.h's.  A kernel service's error is its result's low byte, which
 * is all the image tests. */

/* Sets or clears output port bits: the register holds each pin's
 * complement, so a bit kept set in the shadow is reset in the port
 * (MC68681 4.2.12).  While duart_shadow.field_0c is set the shadow is
 * duart_shadow.output_reset, kept in supervisor mode; else
 * selftest.lamp_mask, as the reset code keeps it.  Nothing calls it: the
 * Z3805A's duart_op0_set and duart_op1_set hold its first branch for one
 * bit each (0x1de4e). */
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

/* Preloads the DUART's counter with 0xffff, both halves, shadowed. */
static void duart_counter_preload(void) {
  static const uint8_t COUNTER_PRELOAD = 0xff; /* each half */

  duart_shadow.ctur = COUNTER_PRELOAD;
  duart_shadow.ctlr = COUNTER_PRELOAD;
  duart.cur_ctur = duart_shadow.ctur;
  duart.clr_ctlr = duart_shadow.ctlr;
}

/* The DUART self-test's counter test: counts ten ticks and fails if
 * the count left is out of range (0x1e3da). */
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
}

/* Whether the interrupt vector register holds a value written to it. */
static bool duart_ivr_holds(uint8_t value) {
  duart.ivr = value;
  return duart.ivr == value;
}

/* Tests the interrupt vector register with two patterns, then sets the
 * vector. */
static void duart_ivr_test(void) {
  if (!duart_ivr_holds(DUART_IVR_PATTERN_1)) {
    selftest.uart |= SELFTEST_UART_REGISTER;
  }
  if (!duart_ivr_holds(DUART_IVR_PATTERN_2)) {
    selftest.uart |= SELFTEST_UART_REGISTER;
  }
  duart.ivr = DUART_IVR_VECTOR;
}

/* The DUART self-test: its interrupt vector register, then its counter
 * and both channels' loopbacks (0x1e4b6). */
void duart_test(void) {
  uint16_t sr;

  selftest.uart = 0;
  sr = set_interrupt_mask(CPU_IPL_7);
  duart_ivr_test();
  set_interrupt_mask(sr);
  if (selftest.uart == 0) {
    duart_counter_test();
    duart_loopback_test_a();
    duart_loopback_test_b();
  }
}

/* At reset: tests the DUART's interrupt vector register and sets
 * channel B up for the console, 9600 8N1, then checks it in loopback
 * (0x1ecae). */
void duart_early_init(void) {
  selftest.uart = 0;
  duart_ivr_test();
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

/* The DUART's input port (MC68681 4.3.13); nothing calls it (0x1de46). */
uint8_t duart_input_port_read(void) { return duart.ip_opcr; }

/* Resets the output port's bits 2 to 7 in reset and sets those in set,
 * as duart_output_bit_set does, in supervisor mode (0x1df78). */
void duart_output_bits(board_led reset, board_led set) {
  uint16_t sr = enter_supervisor_mode();
  uint8_t resets = (reset | duart_shadow.output_reset) & DUART_OP_BITS;

  duart.stop_ropbc = resets;
  duart_shadow.output_reset =
      (reset & DUART_OP_BITS | duart_shadow.output_reset) &
      ~(set & DUART_OP_BITS);
  duart.start_sopbc = ~duart_shadow.output_reset & DUART_OP_BITS;
  set_interrupt_mask(sr);
}
