/* From the 58503B 1.01.04's rewritten/sci.h, copied for the Z3805A 1.01.03:
 * what its copied functions use.  Its comments' addresses are the
 * Z3805A 1.01.03's, but where they name another image. */

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
