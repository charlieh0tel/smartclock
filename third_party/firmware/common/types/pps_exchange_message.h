/* pps_exchange_message and the types beside it, as the images' smartclock.h
 * declare them (each includes this header where they stand):
 * docs/firmware/rewrite.md, "Shared C".  Where a comment cites code, it
 * is the Z3805A 3543B's, which the 58503A's copy of the comment came
 * from. */

/* The message pps_interrupt sends pps_exchange each 1PPS, at pps_message,
 * which pps_wait_time receives into the same cells.  pps_init clears
 * 0x18 bytes from it with mutex_clear. */
typedef struct pps_exchange_message {
  uint32_t field_00[2]; /* only cleared */
  uint32_t gps_time;    /* +0x08: get_gps_time's at the pulse */
  uint32_t gps_date;    /* +0x0c: get_gps_date's */
  uint32_t field_10[2]; /* only cleared */
} pps_exchange_message;
