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

/* A driver's flow byte (duartb_driver.flow, sci_driver.flow). */
static const uint8_t DUART_FLOW_XOFF_SENT = 0x20;

/* A character as either copy of an echo routine reads it: the image's
 * at the start of a 2-byte slot (drtb_putc, 0x1ddfa; sci_putc,
 * 0x201b4; installer::0x10386c), gcc's at the end of its 4-byte one.
 * A port's echo holds the image's routine where the image's
 * reconfiguration stored it (0x1dd54), gcc's where the C's did. */
typedef struct duart_echo_slot {
  char image;       /* +0x00: where the image's routines read it */
  uint8_t field_01; /* unread: the rest of the 2-byte slot */
  uint8_t field_02; /* unread */
  char gcc;         /* +0x03: where gcc's read it */
} duart_echo_slot;

/* Echoes a character through a port's echo, whichever copy it is. */
static void duart_echo(putc_function* echo, char character) {
  typedef void echo_either(duart_echo_slot slot);

  ((echo_either*)echo)((duart_echo_slot){.image = character, .gcc = character});
}

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
      duart_echo(*echo, character);
      continue;
    }
    switch (character) {
      case DUART_BACKSPACE:
        if (count != left) {
          next--;
          left++;
          duart_echo(*echo, character);
        }
        break;
      case DUART_LF:
      case DUART_CR:
        if ((*last_terminator == DUART_CR && character == DUART_LF) ||
            (*last_terminator == DUART_LF && character == DUART_CR)) {
          *last_terminator = 0;
        } else {
          duart_echo(*echo, DUART_CR);
          duart_echo(*echo, DUART_LF);
          count -= left;
          left = 0;
          *last_terminator = character;
        }
        break;
      case DUART_CTRL_X:
        if (count != left) {
          next = buffer;
          left = count;
          duart_echo(*echo, DUART_CR);
          duart_echo(*echo, DUART_LF);
        }
        break;
      default:
        duart_echo(*echo, CARET);
        duart_echo(*echo, character + CONTROL_TO_LETTER);
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
