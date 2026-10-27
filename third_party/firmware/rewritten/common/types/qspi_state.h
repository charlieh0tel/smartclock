/* qspi_state and the types beside it, as the images' smartclock.h declare
 * them (each includes this header where they stand):
 * docs/firmware/rewrite.md, "Shared C".  Where a comment cites code, it
 * is the Z3805A 3543B's, which the 58503A's copy of the comment came
 * from. */

/* The QSPI's lock and its transfer, at qspi: qspi_acquire takes the
 * lock if it is free, else queues its task and waits for 0x400, which
 * qspi_release or the interrupt signals the next waiting task.  The
 * interrupt copies the QSPI's receive RAM, qsm.rr at 0xfffd00
 * (MC68331UM Table D-13), to the caller's buffer. */
typedef struct qspi_state {
  uint8_t field_00; /* +0x00: qspi_send and qspi_transfer set it; nothing reads
                       it */
  uint8_t write;    /* +0x01: the next waiting slot, wrapping at 16 */
  uint8_t read;     /* +0x02: the next task to hand the lock to */
  uint8_t field_03;
  uint32_t waiting[16]; /* +0x04: the tasks queued for the lock */
  uint8_t busy;         /* +0x44: the lock is held */
  uint8_t field_45;
  uint16_t*
      receive_buffer; /* +0x46: where the interrupt copies the receive RAM */
  uint16_t receive_count; /* +0x4a: how many words */
  uint8_t copy_pending;   /* +0x4c: the interrupt is to copy them */
  uint8_t running; /* +0x4d: qspi_start waits while it is set; the interrupt
                      clears it */
} qspi_state;
