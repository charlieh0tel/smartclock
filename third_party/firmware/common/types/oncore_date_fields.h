/* oncore_date_fields and the types beside it, as the images' smartclock.h
 * declare them (each includes this header where they stand):
 * docs/firmware/rewrite.md, "Shared C".  Where a comment cites code, it
 * is the Z3805A 3543B's, which the 58503A's copy of the comment came
 * from. */

/* @@Ac, date. */
typedef struct oncore_date_fields {
  uint8_t month;
  uint8_t day;
  uint16_t year;
} oncore_date_fields;
