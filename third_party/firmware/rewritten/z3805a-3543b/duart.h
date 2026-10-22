/* What the DUART's drivers and the SCI's share (duart.c, serial_port.c,
 * sci.c): the control characters, the flow byte and thresholds, the baud
 * and parity codes and the 68681's mode bits, and the line reader
 * duart_gets, which drtb_gets and sci_gets share. The exported C
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
 * port's receive errors to its read errors.  The SCI's compares a
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
    task_debug_message(error_message);
    *read_errors = *receive_errors;
    *receive_errors = 0;
  } else {
    *read_errors = 0;
  }
  return count;
}
