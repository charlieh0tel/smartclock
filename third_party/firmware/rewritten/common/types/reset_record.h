/* reset_record and the types beside it, as the images' smartclock.h declare
 * them (each includes this header where they stand):
 * docs/firmware/rewrite.md, "Shared C".  Where a comment cites code, it
 * is the Z3805A 3543B's, which the 58503A's copy of the comment came
 * from. */

/* A reset-cause record, one of 32 from 0x4001c0, as reset_record_write
 * builds it; its checksum is reset_record_checksum's, over bytes 4 to
 * 0x5b. */
typedef struct reset_record {
  int16_t sequence;  /* +0x00: one past the slot's stored record's */
  uint16_t checksum; /* +0x02 */
  time_of_day time;  /* +0x04: get_date_time's time */
  uint32_t date;     /* +0x08: its date */
  char text[80];     /* +0x0c */
} reset_record;
