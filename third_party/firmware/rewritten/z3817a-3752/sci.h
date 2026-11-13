/* From the Z3815A 4010's rewritten/sci.h, the Z3817A 3752's own beside
 * common/'s: what the SCI's drivers use, the primary's and the installer's
 * (sci.c, installer/installer_sci.c): SCCR0's baud divisors and a character's
 * time at each, the mark parity bit, the installer's alone, and the counted
 * write's device minor.  The exported C includes the hand-written headers after
 * its own declarations and before the hand-written .c files, so every one of
 * them can use it. */

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
