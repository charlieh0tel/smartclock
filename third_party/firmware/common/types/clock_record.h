/* clock_record and the types beside it, as the images' smartclock.h declare
 * them (each includes this header where they stand):
 * docs/firmware/rewrite.md, "Shared C".  Where a comment cites code, it
 * is the Z3805A 3543B's, which the 58503A's copy of the comment came
 * from. */

/* The clock record at clock, clock, which the clock_record_ routines
 * reach through A6 or A4: the uptime, two dates and times, each with its
 * month table, the minute's length and the GPS-UTC offset.
 * clock_record_checksum sums +0x02 to +0x32.  0x33 bytes. */
typedef struct clock_record {
  uint16_t checksum; /* +0x00: the sum's complement, then the sum */
  uint32_t uptime;   /* +0x02: seconds since the clock started; addq.l #1 at
                        clock_record_advance+0x6, each second; cleared only by
                        clock_init */
  time_of_day time;  /* +0x06: get_date_time's time; clock_record_derive's
                        utc_to_local of gps_time */
  uint32_t date;     /* +0x0a: get_date_time's date */
  uint8_t month_days[12]; /* +0x0e: date's days a month, January first;
                             clock_set_leap_year sets February's */
  time_of_day gps_time;   /* +0x1a: get_gps_time's */
  uint32_t gps_date; /* +0x1e: get_gps_date's; year << 16 | month << 8 | day */
  uint8_t gps_month_days[12]; /* +0x22: gps_date's */
  uint8_t minute_length; /* +0x2e: seconds in this minute, 60; 59 or 61 from
                            the leap stubs at leap_minute_59_arm and
                            leap_minute_61_arm */
  uint8_t set; /* +0x2f: the clock has been set since power-up, from GPS or by
                  the console: set_timedate sets it (clock_record_store+0x64),
                  clock_init clears it (clock_init+0x6c); get_date_time returns
                  1 while clear */
  int16_t gps_utc_offset; /* +0x30: GPS less UTC in seconds, days times 86400
                             plus the difference of two times of day, negated
                             in UTC mode; a 59- or 61-second minute or
                             offset_step changes it */
  uint8_t gps_utc_offset_valid; /* +0x32: set_gps_utc_offset sets it;
                                   clock_init clears it */
} clock_record;
