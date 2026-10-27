/* qspi_buffers and the types beside it, as the images' smartclock.h declare
 * them (each includes this header where they stand):
 * docs/firmware/rewrite.md, "Shared C".  Where a comment cites code, it
 * is the Z3805A 3543B's, which the 58503A's copy of the comment came
 * from. */

/* A QSPI transfer's two buffers, on its caller's stack, which
 * qspi_transfer takes, and qspi_read hands its device's read: the words
 * to send and where the words received go.  adc_read and the FPGA's
 * routines pass one. */
typedef struct qspi_buffers {
  uint16_t* transmit; /* +0x00 */
  uint16_t* receive;  /* +0x04 */
} qspi_buffers;

/* The EEPROM lock's token, at eepx_token: the 16-byte message eeprom_init
 * sends the eepx exchange, which eeprom_lock receives into the same
 * cells and eeprom_unlock sends back.  The lock is recursive, as
 * mutex_acquire's is: the holder at +0x08, its nested holds at +0x0c. */
typedef struct eeprom_lock_token {
  uint32_t status;   /* +0x00: eeprom_init zeroes it; nothing reads it */
  uint32_t exchange; /* +0x04: eepx's id; eeprom_unlock checks it against
                        eepx_exchange ("eepx semaphore has been corrupted") */
  uint32_t owner;    /* +0x08: the task holding the lock, 0 for none */
  int32_t depth;     /* +0x0c: the holder's nested eeprom_lock calls, which
                        eeprom_unlock counts down before it sends */
} eeprom_lock_token;
