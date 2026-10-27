/* What of duart.h the images share, the same in each
 * (docs/firmware/rewrite.md, "Shared C"); each image's own
 * rewritten/duart.h holds the rest. */

/* The control characters the drivers act on, which duart_gets'
 * switch tests. */
enum {
  DUART_CTRL_C = 0x03,
  DUART_BACKSPACE = 0x08,
  DUART_TAB = 0x09,
  DUART_LF = 0x0a,
  DUART_CR = 0x0d,
  DUART_XON = 0x11,
  DUART_XOFF = 0x13,
  DUART_CTRL_X = 0x18, /* cancels the line */
};

static const uint8_t DUART_FLOW_XOFF_RECEIVED = 0x80;

/* transmission held */

/* XON/XOFF threshold, in free bytes of a 256-byte ring: a read sends
 * XON once more are free. */
static const uint8_t DUART_XON_ABOVE_FREE = 0x10;

/* The receive interrupt sends XOFF with fewer free than this. */
static const uint8_t DUART_XOFF_BELOW_FREE = 0x40;

/* A received byte's 7 bits, which the receive interrupt tests for a
 * control character. */
static const uint8_t DUART_ASCII_MASK = 0x7f;

/* The 68681's receive errors (MC68681 Table 4-5). */
static const uint8_t DUART_RECEIVE_ERRORS =
    SR_RECEIVED_BREAK | SR_FRAMING_ERROR | SR_PARITY_ERROR;

/* The signal channel B's transmit interrupt gives the SCPI task when an
 * asynchronous send ends; psos-signals.h has no name for it. */
static const uint32_t DUART_SIGNAL_ASYNC_DONE = 0x0020;

/* The 68681's channel B settings (MC68681 Table 4-5). */
static const uint8_t DUART_MR1_EIGHT_BITS = 3;

static const uint8_t DUART_MR1_SEVEN_BITS = 2;

/* MR1's bits a mode change keeps. */
static const uint8_t DUART_MR1_KEEP =
    MR1_RXRTS | MR1_RXIRQ_FFULL | MR1_ERROR_BLOCK;

static const uint8_t DUART_MR2_ONE_STOP_BIT = 0x07;

static const uint8_t DUART_MR2_TWO_STOP_BITS = 0x0f;

/* The counter clocked by the crystal over 16 (MC68681 Table 4-4). */
static const uint8_t DUART_ACR_COUNTER_CRYSTAL_16 = 0x30;

/* The output port's bits 2 to 7, by duart_shadow.output_reset. */
static const uint8_t DUART_OP_BITS = 0xfc;

/* Values the reset code's and the self-tests' checks write to the
 * interrupt vector register and read back, and the vector it is left
 * with, user vector 67, whose handler is primary_user_67. */
static const uint8_t DUART_IVR_PATTERN_1 = 0x55;

static const uint8_t DUART_IVR_PATTERN_2 = 0xaa;

static const uint8_t DUART_IVR_VECTOR = 0x43;

/* How long a channel B loopback check spins for each character back. */
static const int32_t DUART_LOOPBACK_CHECK_SPINS = 3000;

static const uint8_t DUART_CSR_CLOCK_BITS = 4;

/* the receiver's clock in the high nibble, the transmitter's in the low
 */
/* baud-rate set 2's clock selects, which ACR_BRG_SET_2 picks */
static const uint8_t DUART_CSR_1200 = 0x6;

static const uint8_t DUART_CSR_2400 = 0x8;

static const uint8_t DUART_CSR_9600 = 0xb;

static const uint8_t DUART_CSR_19200 = 0xc;

/* How long a reconfiguration waits for the transmitter to empty, in
 * ticks. */
static const int32_t DUART_DRAIN_PAUSES = 100;

/* gp_port_select's and its installer copy's body: sets the GP port's
 * bits 2 to 4 to a selection, 0 to 6, and returns it shifted there. */
static int32_t duart_gp_port_select(char selection) {
  /* The GP port's bits 2 to 4, the selection. */
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

/* duartb_putc_polled's and its installer copy's body: waits for DUART
 * channel B's transmitter and writes a character; returns the status
 * bit it waited on. */
static uint8_t duart_b_putc_polled(uint8_t c) {
  uint16_t sr = cpu_interrupts_at(CPU_IPL_2);
  uint8_t status;

  do {
    status = duart.b.sr_csr;
  } while ((status & SR_TXRDY) == 0);
  duart.b.rb_tb = c;
  cpu_sr_write(sr);
  return status & SR_TXRDY;
}

/* What duarta.c and duartb.c share, from what was duart.c, which the
 * exported C includes before them. */

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
