/* What the MC68681 DUART's two channels share, written by hand in place
 * of the decompiler's (docs/firmware/README.md, "Hand-written C";
 * docs/firmware/modules.md, `devices/duart`).  The exported C includes
 * this file after its own, so the types and prototypes it declares are
 * in scope; each function here keeps its generated prototype.  It is the
 * early set-up, the output port and the GP port's select, the self-tests
 * and their loopbacks, and the stop.  The drivers' state is
 * docs/firmware/structures.md's "The DUART's shadows"; the registers are
 * mc68681.h's.  A kernel service's error is its result's low byte, which
 * is all the image tests. */

/* Sets the GP port's bits 2 to 4 to a selection, 0 to 6, and returns
 * it shifted there (0x1d572). */
int32_t gp_port_select(char selection) {
  /* The GP port's bits 2 to 4, gp_port_select's selection. */
  static const uint8_t GP_SELECT_SHIFT = 2;
  static const uint8_t GP_SELECT_MASK = 0x1c;
  static const uint8_t GP_SELECT_MAX = 6;

  if (selection > GP_SELECT_MAX) {
    selection = 0;
  }
  gpt.portgp &= ~GP_SELECT_MASK;
  gpt.portgp |= selection << GP_SELECT_SHIFT;
  return selection << GP_SELECT_SHIFT;
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
void duart_output_bits(uint8_t reset, uint8_t set) {
  /* The output port's bits 2 to 7, by duart_shadow.output_reset. */
  static const uint8_t OP_BITS = 0xfc;
  uint16_t sr = set_interrupt_mask(CPU_IPL_6_UNSHIFTED);
  uint8_t resets = (reset | duart_shadow.output_reset) & OP_BITS;

  duart.stop_ropbc = resets;
  duart_shadow.output_reset =
      (reset & OP_BITS | duart_shadow.output_reset) & ~(set & OP_BITS);
  duart.start_sopbc = ~duart_shadow.output_reset & OP_BITS;
  set_interrupt_mask(sr);
}

/* The DUART self-test's bits in selftest.uart that several functions
 * set; the rest are in the functions that set them. */
static const int16_t DUART_TEST_B_MISMATCH = 0x10;

static const int16_t DUART_TEST_B_TIMEOUT = 0x20;

/* A loopback test's channel: its registers and mode shadow, its
 * interrupts, the flag that keeps its driver off the transmitter, the
 * flag that hands its interrupts to the test, and its failure bit. */
typedef struct duart_loopback_channel {
  volatile duart_channel* registers;
  uint8_t* mr2;
  uint8_t interrupts;
  uint8_t* held;
  uint8_t* loopback_test;
  int16_t timeout_bit;
} duart_loopback_channel;

/* Puts a channel in local loopback and has the interrupt routines send
 * "DUART test string" through it, checking each character back; sets
 * the channel's failure bit if it does not all come back
 * (duart_loopback_test_a and duart_loopback_test_b). */
static void duart_loopback_test(const duart_loopback_channel* channel) {
  /* How long the loopback test spins for its string to come back. */
  static const int32_t LOOPBACK_SPINS = 10000;
  uint16_t sr = set_interrupt_mask(CPU_IPL_7);
  int32_t spins;

  *channel->held = 1;
  duart_shadow.imr &= ~channel->interrupts;
  duart.isr_imr = duart_shadow.imr;
  channel->registers->cr = CR_RX_DISABLE;
  set_interrupt_mask(sr);
  for (int32_t pauses = DUART_DRAIN_PAUSES;
       pauses > 0 && (channel->registers->sr_csr & SR_TXEMT) == 0; pauses--) {
    psos_pause(1);
  }
  sr = set_interrupt_mask(CPU_IPL_7);
  channel->registers->cr = CR_RESET_RECEIVER;
  channel->registers->cr = CR_RESET_TRANSMITTER;
  *channel->loopback_test = 1;
  /* reading MR1 moves the pointer on to MR2; the image starts its
   * count from what the read gives (0x1e346) */
  spins = channel->registers->mr;
  *channel->mr2 = (*channel->mr2 & ~MR2_MODE_MASK) | MR2_MODE_LOCAL_LOOPBACK;
  channel->registers->mr = *channel->mr2;
  loopback_send_next = loopback_test_text;
  loopback_expect_next = loopback_test_text;
  duart_shadow.imr |= channel->interrupts;
  duart.isr_imr = duart_shadow.imr;
  channel->registers->cr = CR_TX_ENABLE | CR_RX_ENABLE;
  set_interrupt_mask(sr);
  while (spins < LOOPBACK_SPINS && *loopback_expect_next != 0) {
    spins++;
  }
  sr = set_interrupt_mask(CPU_IPL_7);
  if (*loopback_expect_next != 0) {
    selftest.uart |= channel->timeout_bit;
  }
  loopback_send_next = loopback_idle_text;
  loopback_expect_next = loopback_idle_text;
  channel->registers->cr = CR_RESET_RECEIVER;
  channel->registers->cr = CR_RESET_TRANSMITTER;
  (void)channel->registers->mr;
  *channel->mr2 &= ~MR2_MODE_MASK;
  channel->registers->mr = *channel->mr2;
  duart_shadow.imr |= channel->interrupts;
  duart.isr_imr = duart_shadow.imr;
  channel->registers->cr = CR_TX_ENABLE | CR_RX_ENABLE;
  *channel->held = 0;
  *channel->loopback_test = 0;
  set_interrupt_mask(sr);
}

/* The loopback test on channel A, the GPS receiver's (0x1e2c0). */
void duart_loopback_test_a(void) {
  /* The DUART self-test's bit in selftest.uart. */
  static const int16_t TEST_A_TIMEOUT = 0x08;
  const duart_loopback_channel channel = {
      &duart.a,     &duart_shadow.mr2a,    IMR_RXRDYA | IMR_TXRDYA,
      &duarta.held, &duarta.loopback_test, TEST_A_TIMEOUT,
  };

  duart_loopback_test(&channel);
}

/* The loopback test on channel B, the host port (0x1e41a). */
void duart_loopback_test_b(void) {
  const duart_loopback_channel channel = {
      &duart.b,
      &duart_shadow.mr2b,
      IMR_RXRDYB | IMR_TXRDYB,
      &duartb.reconfiguring,
      &duartb.loopback_test,
      DUART_TEST_B_TIMEOUT,
  };

  duart_loopback_test(&channel);
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
  static const uint8_t ACR_COUNTER_CRYSTAL_16 = 0x30; /* MC68681 Table 4-4 */
  static const uint32_t COUNTER_PAUSES = 10;
  static const uint16_t COUNTER_LEAST = 0xf533;
  static const uint16_t COUNTER_MOST = 0xf8cc;
  static const uint32_t PRIORITY_HIGHEST = 0xff;
  static const uint8_t BYTE_BITS = 8;
  /* The DUART self-test's bit in selftest.uart. */
  static const int16_t TEST_COUNTER_FAILED = 0x02;
  uint8_t priority;
  uint8_t upper;
  uint8_t lower;
  uint16_t count;

  (void)duart.stop_ropbc;
  duart_counter_preload();
  psos_priority(0, PRIORITY_HIGHEST, &priority);
  duart_shadow.acr =
      (duart_shadow.acr & ~ACR_COUNTER_MASK) | ACR_COUNTER_CRYSTAL_16;
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
  task_set_priority(0, priority);
  count = upper << BYTE_BITS | lower;
  if (count > COUNTER_MOST || count < COUNTER_LEAST) {
    selftest.uart |= TEST_COUNTER_FAILED;
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
  /* Values the self-tests write to the interrupt vector register and
   * read back, and the vector it is left with, user vector 67, whose
   * handler is primary_user_67. */
  static const uint8_t IVR_PATTERN_1 = 0x55;
  static const uint8_t IVR_PATTERN_2 = 0xaa;
  static const uint8_t IVR_VECTOR = 0x43;
  /* The DUART self-test's bit in selftest.uart. */
  static const int16_t TEST_IVR_FAILED = 0x01;

  if (!duart_ivr_holds(IVR_PATTERN_1)) {
    selftest.uart |= TEST_IVR_FAILED;
  }
  if (!duart_ivr_holds(IVR_PATTERN_2)) {
    selftest.uart |= TEST_IVR_FAILED;
  }
  duart.ivr = IVR_VECTOR;
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

/* The loopback test's receive: checks a character against the next
 * expected. */
static void duart_test_receive(volatile duart_channel* channel,
                               int16_t mismatch_bit) {
  char expected = *loopback_expect_next++;

  /* a signed char against the byte read: one with its top bit set never matches
   */
  if (channel->rb_tb != expected) {
    selftest.uart |= mismatch_bit;
  }
}

/* The loopback test's receive on channel A (0x1e6be). */
void duart_test_rx_a(void) {
  /* The DUART self-test's bit in selftest.uart. */
  static const int16_t TEST_A_MISMATCH = 0x04;

  duart_test_receive(&duart.a, TEST_A_MISMATCH);
}

/* The loopback test's receive on channel B (0x1e6e2). */
void duart_test_rx_b(void) {
  duart_test_receive(&duart.b, DUART_TEST_B_MISMATCH);
}

/* The loopback test's transmit: sends the next character, or at the
 * string's end turns the transmit interrupt off. */
static void duart_test_transmit(volatile duart_channel* channel,
                                uint8_t interrupt) {
  if (*loopback_send_next == 0) {
    duart_shadow.imr &= ~interrupt;
    duart.isr_imr = duart_shadow.imr;
  } else {
    channel->rb_tb = *loopback_send_next++;
  }
}

/* The loopback test's transmit on channel A (0x1e706). */
void duart_test_tx_a(void) { duart_test_transmit(&duart.a, IMR_TXRDYA); }

/* The loopback test's transmit on channel B (0x1e736). */
void duart_test_tx_b(void) { duart_test_transmit(&duart.b, IMR_TXRDYB); }

/* Stops both DUART channels and masks its interrupts (0x1ebbc). */
void duart_stop(void) {
  duart.a.cr = CR_RX_DISABLE;
  duart.a.cr = CR_TX_DISABLE;
  duart.b.cr = CR_RX_DISABLE;
  duart.b.cr = CR_TX_DISABLE;
  duart.isr_imr = 0;
}

/* At reset, before interrupts: sends "DUART" through channel B in local
 * loopback by polling and checks each character back (0x1ebe4). */
void duartb_loopback_check(void) {
  /* How long duartb_loopback_check spins for each character back. */
  static const int32_t LOOPBACK_CHECK_SPINS = 3000;
  char* text = "DUART";

  (void)duart.b.mr;
  duart.b.mr = MR2_MODE_LOCAL_LOOPBACK | DUART_MR2_ONE_STOP_BIT;
  loopback_send_next = text;
  loopback_expect_next = text;
  duart.b.cr = CR_TX_ENABLE | CR_RX_ENABLE;
  duart.b.rb_tb = *loopback_send_next++;
  while (*loopback_expect_next != 0 && selftest.uart == 0) {
    for (int32_t spins = LOOPBACK_CHECK_SPINS;
         spins > 0 && (duart.b.sr_csr & SR_RXRDY) == 0; spins--) {
    }
    if ((duart.b.sr_csr & SR_RXRDY) != 0) {
      char expected = *loopback_expect_next++;

      if (duart.b.rb_tb != expected) {
        selftest.uart |= DUART_TEST_B_MISMATCH;
      }
      if (*loopback_send_next != 0) {
        duart.b.rb_tb = *loopback_send_next++;
      }
    } else {
      selftest.uart |= DUART_TEST_B_TIMEOUT;
    }
  }
  duart.b.cr = CR_RESET_RECEIVER;
  duart.b.cr = CR_RESET_TRANSMITTER;
  (void)duart.b.mr;
  duart.b.mr = DUART_MR2_ONE_STOP_BIT;
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
