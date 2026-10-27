/* time_of_day and the types beside it, as the images' smartclock.h declare
 * them (each includes this header where they stand):
 * docs/firmware/rewrite.md, "Shared C".  Where a comment cites code, it
 * is the Z3805A 3543B's, which the 58503A's copy of the comment came
 * from. */

/* A time of day as a long holds it, hour << 16 | minute << 8 | second,
 * by its bytes, which the clock routines add and carry one at a time
 * (datetime_add, time_next_second). */
typedef struct time_of_day {
  uint8_t reserved; /* 0 */
  uint8_t hour;
  uint8_t minute;
  uint8_t second;
} time_of_day;

/* A date as a long holds it, year << 16 | month << 8 | day, where a
 * routine keeps it in memory and reads or writes it by its parts:
 * leap_opportunity_date sets the month and day bytes
 * (leap_opportunity_date+0x42 to leap_opportunity_date+0x6c), diag_get_time and
 * timecode_update print the year word and the month and day bytes
 * (diag_get_time+0x44 to diag_get_time+0x54, timecode_update+0x1b4 to
 * timecode_update+0x1c4), and pulse_settings_done builds one from three longs
 * (pulse_settings_done+0x2c to pulse_settings_done+0x3c). */
typedef struct calendar_date {
  uint16_t year; /* +0x00: the full year, 1994 on */
  uint8_t month; /* +0x02: 1 to 12 */
  uint8_t day;   /* +0x03: 1 to 31 */
} calendar_date;
