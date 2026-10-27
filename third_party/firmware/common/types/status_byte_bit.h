/* status_byte_bit and the types beside it, as the images' smartclock.h declare
 * them (each includes this header where they stand):
 * docs/firmware/rewrite.md, "Shared C".  Where a comment cites code, it
 * is the Z3805A 3543B's, which the 58503A's copy of the comment came
 * from. */

/* The Status Byte's bits the firmware names (structures.md, "The SCPI
 * session"): RQS, which status_update_rqs keeps and the service request
 * enable cannot set. */
typedef enum __attribute__((packed)) status_byte_bit {
  STATUS_BYTE_REQUEST_SERVICE = 0x40,
} status_byte_bit;
