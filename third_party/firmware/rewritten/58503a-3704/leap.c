/* leap.c's functions that are this image's own: the rest are
 * common/leap.c's, the same in every image (docs/firmware/rewrite.md,
 * "Shared C").  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype. */

static const uint8_t CLOCK_MARCH = 3;

static const uint8_t CLOCK_JUNE = 6;

static const uint8_t CLOCK_SEPTEMBER = 9;

static const uint8_t CLOCK_DECEMBER = 12;

/* The pending leap second's GPS seconds, at the end of its date's last
 * minute, for :PTIM:LEAP:GPST?; 0 with none pending (0x1130e). */
int32_t leap_gps_time(void) {
  uint16_t sr = set_interrupt_mask(CPU_IPL_7);
  uint8_t pending = nv_settings.leap_pending;
  int32_t minute_length = nv_settings.leap_minute_length;
  uint32_t date = nv_settings.leap_date;
  int16_t offset;

  set_interrupt_mask(sr);
  if (!pending) {
    return 0;
  }
  int32_t seconds = gps_seconds(date, 0);

  if (!get_gps_utc_offset(&offset)) {
    offset = 0;
  }
  if (minute_length == CLOCK_LEAP_MINUTE_LONG) {
    return offset + seconds + CLOCK_SECONDS_PER_DAY;
  }
  return offset + seconds + CLOCK_SECONDS_PER_DAY - 1;
}

/* Advances a date and time by a second, the minute 59 or 61 seconds long at
 * a pending leap second's 23:59 when the clock keeps UTC (0x1138a). */
void time_next_second(uint32_t* date, time_of_day* value) {
  time_of_day time = *value;
  uint8_t minute_length = CLOCK_SECONDS_PER_MINUTE;

  if (nv_settings.gps_utc && nv_settings.leap_pending &&
      nv_settings.leap_date == *date && time.hour == CLOCK_LAST_HOUR &&
      time.minute == CLOCK_LAST_MINUTE) {
    minute_length = nv_settings.leap_minute_length;
  }
  time.second++;
  if (time.second >= minute_length) {
    time.second -= minute_length;
    time.minute++;
  }
  if (time.minute > CLOCK_LAST_MINUTE) {
    time.minute -= CLOCK_MINUTES_PER_HOUR;
    time.hour++;
  }
  if (time.hour > CLOCK_LAST_HOUR) {
    time.hour -= CLOCK_HOURS_PER_DAY;
    *date = date_normalize(*date + 1);
  }
  *value = time;
}

/* Whether the GPS clock, as UTC unless the clock keeps GPS time, is past
 * 23:59:45 on the last day of a quarter (0x117c6). */
uint8_t leap_second_near(void) {
  /* leap_second_near is true past 23:59:45. */
  static const uint8_t LEAP_NEAR_SECOND = 45;
  uint32_t date;
  time_of_day time;

  get_gps_datetime(&date, &time);
  if (nv_settings.gps_utc == 0) {
    gps_to_utc(&date, &time);
  }
  uint16_t month_day = date;

  bool quarter_end = month_day == (CLOCK_MARCH << CLOCK_FIELD_BITS | 31) ||
                     month_day == (CLOCK_JUNE << CLOCK_FIELD_BITS | 30) ||
                     month_day == (CLOCK_SEPTEMBER << CLOCK_FIELD_BITS | 30) ||
                     month_day == (CLOCK_DECEMBER << CLOCK_FIELD_BITS | 31);

  return quarter_end && time.hour == CLOCK_LAST_HOUR &&
         time.minute == CLOCK_LAST_MINUTE && time.second > LEAP_NEAR_SECOND;
}

/* Queues the leap minute's clock event in UTC or GPS time, as
 * :DIAG:GPS:UTC says (0x1075a). */
klok_entry* leap_event_schedule(uint32_t date, int32_t minute_length) {
  if (!nv_settings.gps_utc) {
    return leap_event_schedule_utc(date, minute_length);
  }
  return leap_event_schedule_gps(date, minute_length);
}

/* Clears the pending leap second from the NV settings: a 60-second
 * minute on 1994-01-01 (0x46572). */
void leap_settings_clear(void) {
  static const uint16_t NO_LEAP_YEAR = 1994;

  nv_settings.leap_pending = 0;
  nv_settings.leap_minute_length = CLOCK_SECONDS_PER_MINUTE;
  nv_settings.leap_date = clock_pack(NO_LEAP_YEAR, 1, 1);
}
