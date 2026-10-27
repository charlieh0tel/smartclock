/* packed_datetime and the types beside it, as the images' smartclock.h declare
 * them (each includes this header where they stand):
 * docs/firmware/rewrite.md, "Shared C".  Where a comment cites code, it
 * is the Z3805A 3543B's, which the 58503A's copy of the comment came
 * from. */

/* A date and time as datetime_add and datetime_subtract take them: the
 * time's hours, minutes and seconds in its low three bytes, and the date
 * packed as date_to_days reads it, or, in their first argument, a count
 * of days to add or take away. */
typedef struct packed_datetime {
  time_of_day time;
  uint32_t date;
} packed_datetime;
