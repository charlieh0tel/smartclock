/* serial_ring and the types beside it, as the images' smartclock.h declare
 * them (each includes this header where they stand):
 * docs/firmware/rewrite.md, "Shared C".  Where a comment cites code, it
 * is the Z3805A 3543B's, which the 58503A's copy of the comment came
 * from. */

/* A DUART channel's receive ring: its interrupt stores at write, its
 * reader takes at read, each index wrapping in a byte; empty when the
 * two are equal. */
typedef struct serial_ring {
  uint8_t write;
  uint8_t read;
  uint8_t data[256];
} serial_ring;

/* DUART channel A's driver, the GPS receiver's link, at duarta; only
 * the GPS task sends on it.  Its receive ring is at duarta_ring. */
typedef struct duarta_driver {
  uint8_t* transmit_next; /* +0x00: drta_send_message's next byte */
  int16_t transmit_count; /* +0x04: its characters left; at 0 the GPS task is
                             signaled 0x800 */
  uint8_t overrun;        /* +0x06: the receiver overran ("DRTA receiver
                             overrun ocurred") */
  uint8_t loopback_test;  /* +0x07: set while duart_loopback_test_a runs */
  uint8_t held; /* +0x08: set while that test has the port, so a send leaves
                   the transmit interrupt off */
  uint8_t least_free; /* +0x09: the receive ring's least free space seen */
} duarta_driver;

/* A serial port's baud code, in its settings record (SERial1's and
 * SERial2's :BAUD; sci_baud_set and duartb_reconfigure). */
typedef enum __attribute__((packed)) serial_baud {
  SERIAL_BAUD_1200 = 0,
  SERIAL_BAUD_2400 = 1,
  SERIAL_BAUD_9600 = 2,
  SERIAL_BAUD_19200 = 3,
} serial_baud;
