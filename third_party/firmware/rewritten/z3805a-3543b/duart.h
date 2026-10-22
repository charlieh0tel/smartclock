/* What the DUART's drivers and the SCI's share (duart.c, duarta.c,
 * duartb.c, sci.c, and the installer's installer_duart*.c): the control
 * characters, the flow byte and thresholds, the baud and parity codes and the
 * 68681's mode bits, the line reader duart_gets, which drtb_gets, sci_gets and
 * installer_drtb_gets share, and the
 * channels' self-test routines. The exported C
 * includes the hand-written headers after its own declarations and
 * before the hand-written .c files, so every one of them can use it.
 * The registers are mc68681.h's; the critical sections use cpu.h's
 * status register helpers. */

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

/* A driver's flow byte (duartb_driver.flow, sci_driver.flow). */
static const uint8_t DUART_FLOW_XOFF_SENT = 0x20;

static const uint8_t DUART_FLOW_XOFF_RECEIVED = 0x80; /* transmission held */

/* XON/XOFF threshold, in free bytes of a 256-byte ring: a read sends
 * XON once more are free. */
static const uint8_t DUART_XON_ABOVE_FREE = 0x10;

/* The 68681's channel B settings (MC68681 Table 4-5) that several
 * functions use; the rest are in the functions that use them. */
static const uint8_t DUART_MR1_EIGHT_BITS = 3;

static const uint8_t DUART_MR2_ONE_STOP_BIT = 0x07;

static const uint8_t DUART_CSR_CLOCK_BITS =
    4; /* the receiver's clock in the high nibble, the transmitter's in the low
        */
/* baud-rate set 2's clock select for 9600, which ACR_BRG_SET_2 picks */
static const uint8_t DUART_CSR_9600 = 0xb;

/* How long a reconfiguration waits for the transmitter to empty, in
 * ticks. */
static const int32_t DUART_DRAIN_PAUSES = 100;

/* A port's line reader, which drtb_gets and sci_gets share: reads into
 * a buffer of length bytes until CR or LF, or until it is full,
 * echoing through the port's echo; backspace takes back a character,
 * ctrl-X the line, and the CR or LF that is the other half of the last
 * line's end is skipped.  Returns the characters read, and moves the
 * port's receive errors to its read errors, with error_message, unless
 * it is NULL, as the installer's is (0x103266).  The SCI's compares a
 * character as a signed char, so that one with its top bit set is a
 * control character (0x1f89a), where channel B's is printable
 * (0x1d816). */
static int32_t duart_gets(char* buffer, int32_t length, uint8_t (*getc)(void),
                          putc_function* const* echo, uint8_t* last_terminator,
                          uint8_t* receive_errors, uint8_t* read_errors,
                          char* error_message, bool top_bit_control) {
  static const uint8_t FIRST_PRINTABLE = 0x20;
  static const char CARET =
      '^'; /* echoes a control character as ^ and its letter */
  static const uint8_t CONTROL_TO_LETTER = 0x40;
  int32_t left = length - 1;
  int32_t count = left;
  char* next = buffer;

  while (left != 0) {
    uint8_t character = getc();
    bool printable = top_bit_control ? (int8_t)character >= FIRST_PRINTABLE
                                     : character >= FIRST_PRINTABLE;

    if (printable || character == DUART_TAB) {
      *last_terminator = character;
      *next++ = character;
      left--;
      (*echo)(character);
      continue;
    }
    switch (character) {
      case DUART_BACKSPACE:
        if (count != left) {
          next--;
          left++;
          (*echo)(character);
        }
        break;
      case DUART_LF:
      case DUART_CR:
        if ((*last_terminator == DUART_CR && character == DUART_LF) ||
            (*last_terminator == DUART_LF && character == DUART_CR)) {
          *last_terminator = 0;
        } else {
          (*echo)(DUART_CR);
          (*echo)(DUART_LF);
          count -= left;
          left = 0;
          *last_terminator = character;
        }
        break;
      case DUART_CTRL_X:
        if (count != left) {
          next = buffer;
          left = count;
          (*echo)(DUART_CR);
          (*echo)(DUART_LF);
        }
        break;
      default:
        (*echo)(CARET);
        (*echo)(character + CONTROL_TO_LETTER);
        break;
    }
  }
  *next = 0;
  if (*receive_errors != 0) {
    if (error_message != NULL) {
      task_debug_message(error_message);
    }
    *read_errors = *receive_errors;
    *receive_errors = 0;
  } else {
    *read_errors = 0;
  }
  return count;
}

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
  /* The receive interrupt moves it on (0x1e398 reads it each time
   * round). */
  char* volatile* const expect_next = &loopback_expect_next;
  while (spins < LOOPBACK_SPINS && **expect_next != 0) {
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
