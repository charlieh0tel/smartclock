/* interval_reading_fields and the types beside it, as the images' smartclock.h
 * declare them (each includes this header where they stand):
 * docs/firmware/rewrite.md, "Shared C".  Where a comment cites code, it
 * is the Z3805A 3543B's, which the 58503A's copy of the comment came
 * from. */

/* A time-interval reading, interval_reading, as counter_read leaves it.
 * 13 bytes. */
typedef struct interval_reading_fields {
  double value;  /* s; SCPI's NaN, 9.91e37, on an error */
  uint8_t valid; /* the :DIAG query's flag */
  counter_error errors;
  uint16_t tag;  /* 0xfff6, which the :DIAG query returns with it */
  uint8_t armed; /* clear, the routines report a measurement aborted */
} interval_reading_fields;
