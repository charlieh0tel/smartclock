/* calendar.c's functions that are this image's own: the rest are
 * common/calendar.c's, the same in every image (docs/firmware/rewrite.md,
 * "Shared C").  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype. */

/* A day count as a date, none before 1901-01-01 (0x10e72).  A leap year's
 * December 31 comes out as the next year's January 0: the year is the
 * count's quarter-days over 1461, which reaches the next year a day
 * early. */
uint32_t days_to_date(uint32_t days) {
  /* The Modified Julian Day of 1900-12-31; days_to_date counts its
   * years from it and makes no date before 1901-01-01. */
  static const uint16_t MJD_1900_12_31 = 15384;
  static const uint16_t YEAR_1901 = 1901;
  static const uint16_t DAYS_PER_4_YEARS = 4 * 365 + 1;

  if (days <= MJD_1900_12_31 + 1) {
    days = MJD_1900_12_31 + 1;
  }
  uint32_t since = days - MJD_1900_12_31;
  uint16_t years = since * CLOCK_YEARS_PER_LEAP / DAYS_PER_4_YEARS;
  uint32_t day = since - (years / CLOCK_YEARS_PER_LEAP +
                          (uint32_t)years * CLOCK_DAYS_PER_YEAR);
  uint16_t year = years + YEAR_1901;
  bool leap = is_leap_year(year);
  uint8_t month = 1;

  while (month < CLOCK_MONTHS_PER_YEAR &&
         clock_days_before_month(month + 1, leap) < day) {
    month++;
  }
  return clock_pack(year, month, day - clock_days_before_month(month, leap));
}

/* Takes the GPS-UTC offset from a GPS date and time.  The image takes it
 * as 0 while get_gps_utc_offset says it is valid, the other way round from
 * utc_to_gps, so that it uses the offset only while not valid (0x11580; the
 * test at 0x115a6). */
void gps_to_utc(uint32_t* date, time_of_day* time) {
  packed_datetime value = {.time = *time, .date = *date};
  packed_datetime change = {.date = 0};
  int16_t offset;

  if (get_gps_utc_offset(&offset)) {
    offset = 0;
  }
  if (clock_offset_change(offset, &change)) {
    datetime_subtract(&change, &value);
  } else {
    datetime_add(&change, &value);
  }
  *date = value.date;
  *time = value.time;
}

/* nv_settings.time_zone, hours and minutes, as a change to a date and time,
 * and whether it is negative: div's quotient and remainder of its minutes,
 * each made positive, with either's sign the change's. */
static bool clock_time_zone_change(packed_datetime* change) {
  uint64_t hours_minutes =
      div(nv_settings.time_zone[0] * CLOCK_MINUTES_PER_HOUR +
              nv_settings.time_zone[1],
          CLOCK_MINUTES_PER_HOUR);
  int32_t hours = hours_minutes >> 32;
  int32_t minutes = hours_minutes;
  bool negative = hours < 0 || minutes < 0;

  change->time = (time_of_day){
      .hour = hours < 0 ? -hours : hours,
      .minute = minutes < 0 ? -minutes : minutes,
  };
  return negative;
}

/* Applies nv_settings.time_zone to a UTC date and time (0x11700). */
void utc_to_local(uint32_t* date, time_of_day* time) {
  packed_datetime value = {.time = *time, .date = *date};
  packed_datetime change = {.date = 0};

  if (clock_time_zone_change(&change)) {
    datetime_subtract(&change, &value);
  } else {
    datetime_add(&change, &value);
  }
  *date = value.date;
  *time = value.time;
}

/* Takes nv_settings.time_zone off a local date and time; nothing calls it
 * (0x117b6). */
void local_to_utc(uint32_t* date, time_of_day* time) {
  packed_datetime value = {.time = *time, .date = *date};
  packed_datetime change = {.date = 0};

  if (clock_time_zone_change(&change)) {
    datetime_add(&change, &value);
  } else {
    datetime_subtract(&change, &value);
  }
  *date = value.date;
  *time = value.time;
}
