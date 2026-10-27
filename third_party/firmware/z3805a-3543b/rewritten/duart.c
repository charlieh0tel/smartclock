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

/* Sets the GP port's bits 2 to 4 to a selection, 0 to 6, and returns
 * it shifted there (0x1d572). */
int32_t gp_port_select(char selection) {
  return duart_gp_port_select(selection);
}

/* Sets or clears one of the output port's own bits, with interrupts
 * meant to be held off (CPU_IPL_6_UNSHIFTED): the register holds each pin's
 * complement, so a bit kept set in output_reset is reset in the port
 * (MC68681 4.2.12). */
static void duart_output_bit_set(uint8_t bit, char on) {
  uint16_t sr = set_interrupt_mask(CPU_IPL_6_UNSHIFTED);

  if (!on) {
    duart_shadow.output_reset &= ~bit;
    duart.start_sopbc = bit;
  } else {
    duart_shadow.output_reset |= bit;
    duart.stop_ropbc = bit;
  }
  set_interrupt_mask(sr);
}

/* Sets or clears the output port's bit 0 (0x1dfe6). */
void duart_op0_set(char on) {
  /* The output port's bit 0, by duart_shadow.output_reset. */
  static const uint8_t OP0 = 0x01;

  duart_output_bit_set(OP0, on);
}

/* Sets or clears the output port's bit 1 (0x1e02c). */
void duart_op1_set(char on) {
  /* The output port's bit 1, by duart_shadow.output_reset. */
  static const uint8_t OP1 = 0x02;

  duart_output_bit_set(OP1, on);
}

/* Resets the output port's bits 2 to 7 in reset and sets those in set,
 * as duart_output_bit_set does (0x1e126). */
void duart_output_bits(board_led reset, board_led set) {
  uint16_t sr = set_interrupt_mask(CPU_IPL_6_UNSHIFTED);
  uint8_t resets = (reset | duart_shadow.output_reset) & DUART_OP_BITS;

  duart.stop_ropbc = resets;
  duart_shadow.output_reset =
      (reset & DUART_OP_BITS | duart_shadow.output_reset) &
      ~(set & DUART_OP_BITS);
  duart.start_sopbc = ~duart_shadow.output_reset & DUART_OP_BITS;
  set_interrupt_mask(sr);
}

/* The counter's clock from TxCB, as the reset leaves it (MC68681
 * Table 4-4). */
static const uint8_t DUART_ACR_COUNTER_TXCB = 0x20;

/* Preloads the DUART's counter with 0xffff, both halves, shadowed. */
static void duart_counter_preload(void) {
  static const uint8_t COUNTER_PRELOAD = 0xff; /* each half */

  duart_shadow.ctur = COUNTER_PRELOAD;
  duart_shadow.ctlr = COUNTER_PRELOAD;
  duart.cur_ctur = duart_shadow.ctur;
  duart.clr_ctlr = duart_shadow.ctlr;
}

/* The DUART self-test's counter test: counts ten ticks and fails if
 * the count left is out of range (0x1e574). */
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
 * and both channels' loopbacks (0x1e650). */
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
