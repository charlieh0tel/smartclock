/* From the Z3817A 3752's sci.h and duart.h, the 58503B 1.01.04's own beside
 * common/'s: what the SCI's driver and the installer's copy share (sci.c,
 * installer/installer_sci.c).  The 58503B has no DUART, and its export leaves
 * out common/duart.h, whose routines name the DUART's registers
 * (docs/firmware/58503b-1.01.04.md, "The export and the checks"), so the
 * serial constants the SCI's drivers take from it in the other images are
 * here, under their names there.  The exported C includes the hand-written
 * headers after its own declarations and before the hand-written .c files, so
 * every one of them can use it. */

/* The control characters the drivers act on, which duart_gets'
 * switch tests: common/duart.h's. */
enum {
  DUART_BACKSPACE = 0x08,
  DUART_TAB = 0x09,
  DUART_LF = 0x0a,
  DUART_CR = 0x0d,
  DUART_XON = 0x11,
  DUART_XOFF = 0x13,
  DUART_CTRL_X = 0x18, /* cancels the line */
};

/* A driver's flow byte (sci_driver.flow, installer_sci_driver.flow):
 * an XOFF received, holding transmission, and one sent. */
static const uint8_t DUART_FLOW_XOFF_RECEIVED = 0x80;
static const uint8_t DUART_FLOW_XOFF_SENT = 0x20;

/* XON/XOFF threshold, in free bytes of a 256-byte ring: a read sends
 * XON once more are free. */
static const uint8_t DUART_XON_ABOVE_FREE = 0x10;

/* How many pauses a reconfiguration waits for the transmitter to
 * drain. */
static const int32_t DUART_DRAIN_PAUSES = 100;

/* The SCI's baud divisors for SCCR0 (MC68331UM D.4.4), and a character's
 * time at each in timer counts, which the end of transmission waits. */
static const uint16_t SCI_DIVISOR_1200 = 0x1b5;
static const uint16_t SCI_DIVISOR_2400 = 0xda;
static const uint16_t SCI_DIVISOR_9600 = 0x37;
static const uint16_t SCI_DIVISOR_19200 = 0x1b;
static const uint16_t SCI_CONSTANT_1200 = 0x2222;
static const uint16_t SCI_CONSTANT_2400 = 0x1111;
static const uint16_t SCI_CONSTANT_9600 = 0x444;
static const uint16_t SCI_CONSTANT_19200 = 0x222;

/* The top bit a character is sent with for mark parity. */
static const uint16_t SCI_MARK_BIT = 0x80;

/* The kernel's I/O device 0, the SCI, minor 1: the counted write, and
 * sci_lock's write token to its open and close. */
static const uint32_t SCI_DEVICE_MINOR_1 = 0x001;

/* A character as either copy of an echo routine reads it: the image's
 * at the start of a 2-byte slot (sci_putc, 0x2e7ce; installer_sci_putc,
 * installer::0x103c4e), gcc's at the end of its 4-byte one.  A port's
 * echo holds the image's routine where the image's set-up stored it
 * (0x2e5d4, 0x2e5e0), gcc's where the C's did. */
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

/* A port's line reader, which sci_gets and installer_sci_gets share:
 * reads into a buffer of length bytes until CR or LF, or until it is
 * full, echoing through the port's echo; backspace takes back a
 * character, ctrl-X the line, and the CR or LF that is the other half
 * of the last line's end is skipped.  Returns the characters read, and
 * moves the port's receive errors to its read errors, with
 * error_message, unless it is NULL.  Both take a character with its top
 * bit set as printable (0x2de16), where the Z3805A's SCI reader takes
 * it as a control character: top_bit_control. */
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
