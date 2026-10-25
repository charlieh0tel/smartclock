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

/* An input port bit as 0 or 1, read until ten reads in a row after the
 * first agree (MC68681 4.2.12, IP). */
static uint8_t duart_input_debounced(uint8_t bit) {
  static const int32_t AGREEING_READS = 10;
  uint8_t level = duart.ip_opcr & bit;

  for (int32_t agreeing = 0; agreeing < AGREEING_READS;) {
    const uint8_t read = duart.ip_opcr & bit;

    if (read == level) {
      agreeing++;
    } else {
      agreeing = 0;
      level = read;
    }
  }
  return level != 0;
}

/* The input port's bit 0, debounced; nothing calls it (the Z3815A
 * 4010's copy, 0x2e43a, is polled) (0x1dec4). */
uint8_t duart_input_0_debounced(void) {
  /* The input port's bit 0. */
  static const uint8_t IP0 = 0x01;

  return duart_input_debounced(IP0);
}

/* The input port's bit 1, debounced; nothing calls it (0x1df00). */
uint8_t duart_input_1_debounced(void) {
  /* The input port's bit 1. */
  static const uint8_t IP1 = 0x02;

  return duart_input_debounced(IP1);
}

/* The input port's bit 2, debounced; nothing calls it (0x1df3c). */
uint8_t duart_input_2_debounced(void) {
  /* The input port's bit 2. */
  static const uint8_t IP2 = 0x04;

  return duart_input_debounced(IP2);
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

/* Stops both DUART channels and masks its interrupts (0x1ea30). */
void duart_stop(void) {
  duart.a.cr = CR_RX_DISABLE;
  duart.a.cr = CR_TX_DISABLE;
  duart.b.cr = CR_RX_DISABLE;
  duart.b.cr = CR_TX_DISABLE;
  duart.isr_imr = 0;
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
