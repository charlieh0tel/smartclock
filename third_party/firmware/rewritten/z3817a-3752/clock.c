/* clock.c's functions that are this image's own: the rest are
 * common/clock.c's, the same in every image (docs/firmware/rewrite.md,
 * "Shared C").  From the Z3815A 4010's rewritten/clock.c, those the
 * export needs hand-written to compile, the same in both images, with
 * the static helpers they use (docs/firmware/z3817a-3752.md, "The export
 * and the checks"); and, last, the uptime's two timers, this image's own
 * (uptime_elapsed, which the other images' C defines without its restart
 * argument).  Its comments' addresses are the Z3817A's. */

/* Whether the clock record's checksum is good: clock_record_sum
 * (0x23764) sums the bytes of +0x02 to +0x32 of the record at A6 and
 * leaves Z set when the word at +0 is the sum's complement and the sum,
 * changing D0, D1, A0 and A1.  clock_init holds the record in A6
 * (0x23bca); here A6 is the frame pointer, so the asm statement saves
 * it around the call. */
static bool clock_record_valid(const clock_record* record) {
  uint8_t valid;

  __asm__ volatile(
      "move.l %%a6,-(%%sp)\n\t"
      "movea.l %[record],%%a6\n\t"
      "jsr clock_record_sum\n\t"
      "movea.l (%%sp)+,%%a6\n\t"
      "seq %[valid]"
      : [valid] "=d"(valid)
      : [record] "a"(record)
      : "d0", "d1", "a0", "a1", "cc", "memory");
  return valid != 0;
}

/* Starts the clock afresh, when forced or its record's checksum fails,
 * at no time on the lifetime record's date; either way marks it not
 * set, so that time_valid gives 0, and clears the pending leap minute
 * and offset step (0x23bc6).
 * system_init passes selftest.eeprom. */
void clock_init(int16_t force) {
  /* Days a month, January first, February's 0 for clock_set_leap_year
   * (the immediates at 0x23be0). */
  static const uint8_t MONTH_DAYS[12] = {31, 0,  31, 30, 31, 30,
                                         31, 31, 30, 31, 30, 31};
  static const uint8_t FEBRUARY = 1;

  if (force != 0 || !clock_record_valid(&clock)) {
    for (size_t month = 0; month < sizeof MONTH_DAYS; month++) {
      clock.month_days[month] = MONTH_DAYS[month];
      clock.gps_month_days[month] = MONTH_DAYS[month];
    }
    clock.uptime = 0;
    clock.gps_time = (time_of_day){.hour = 0};
    clock.gps_date = lifetime.date;
    /* The routine takes the whole date, in D7, and reads its year. */
    clock_set_leap_year((char*)&clock.gps_month_days[FEBRUARY], lifetime.date);
    clock_record_derive(&clock);
    clock.minute_length = CLOCK_SECONDS_PER_MINUTE;
    clock.gps_utc_offset = 0;
    clock.gps_utc_offset_valid = 0;
  }
  clock.set = 0;
  clock_record_checksum(&clock);
  leap_minute_pending = 0;
  offset_step_pending = 0;
  offset_step_time = 0;
  offset_step = 0;
}

/* Derives the clock's local date and time from its GPS time again and
 * checksums its record, in supervisor mode (0x23a1c). */
void clock_record_refresh(void) {
  uint16_t sr = cpu_enter_supervisor();

  clock_record_derive(&clock);
  clock_record_checksum(&clock);
  cpu_sr_write(sr);
}

/* get_uptime() less *start into *elapsed, and 1; for a clock gone back
 * before *start, 0, with *elapsed 0 and *start the uptime, or 0 for
 * restart 0 (0x51766).  Every caller passes restart 1. */
uint32_t uptime_elapsed(uint32_t* start, char restart, int32_t* elapsed) {
  const uint32_t uptime = get_uptime();

  if (uptime < *start) {
    *start = restart != 0 ? uptime : 0;
    *elapsed = 0;
    return 0;
  }
  *elapsed = uptime - *start;
  return 1;
}

/* 1 when the uptime given has gone back before either mark, the start
 * set to it and the last cleared, or is more than seconds past both;
 * else 0 (0x5179e).  The status monitor's test of its marks
 * (command_monitor, 0x51fec to 0x52012) as a routine, which nothing
 * calls. */
uint32_t uptime_marks_expired(uint32_t* last, uint32_t uptime, uint32_t* start,
                              uint32_t seconds) {
  if (uptime < *start || uptime < *last) {
    *start = uptime;
    *last = 0;
    return 1;
  }
  return uptime - *last > seconds && uptime - *start > seconds;
}
