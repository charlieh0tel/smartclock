/* calendar.c's functions that are this image's own: the rest are
 * common/calendar.c's, the same in every image (docs/firmware/rewrite.md,
 * "Shared C").  From the 58503A 3704's rewritten/calendar.c: of its functions,
 * those the Z3816A's code carries (identical or relocated by image_diff, or
 * changed with the same instructions), with the static helpers they use, each
 * the same here but where its comment says otherwise
 * (docs/firmware/z3816a-4001.md, "The link check").  Its comments' addresses
 * are the Z3816A's.  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype. */

/* A date's month and day, its low word, month << 8 | day. */
static uint16_t calendar_month_day(uint8_t month, uint8_t day) {
  return month << CLOCK_FIELD_BITS | day;
}

/* A day count as a date, none before 1901-01-01 (0x20e72).  The year is
 * the count's quarter-days over 1461, which reaches the next year a day
 * early on a leap year's December 31; the 58503A takes that day's 0 as
 * the year before's December 31 (0x20ebc), where the Z3805A's makes it
 * January 0. */
uint32_t days_to_date(uint32_t days) {
  /* The Modified Julian Day of 1900-12-31; days_to_date counts its
   * years from it and makes no date before 1901-01-01. */
  static const uint16_t MJD_1900_12_31 = 15384;
  static const uint16_t DAYS_PER_4_YEARS = 4 * 365 + 1;
  static const uint8_t LAST_DAY_OF_DECEMBER = 31;

  if (days <= MJD_1900_12_31 + 1) {
    days = MJD_1900_12_31 + 1;
  }
  uint32_t since = days - MJD_1900_12_31;
  uint16_t years = since * CLOCK_YEARS_PER_LEAP / DAYS_PER_4_YEARS;
  uint32_t day = since - (years / CLOCK_YEARS_PER_LEAP +
                          (uint32_t)years * CLOCK_DAYS_PER_YEAR);

  if (day == 0) {
    return clock_pack(years + CLOCK_YEAR_1900, CLOCK_DECEMBER,
                      LAST_DAY_OF_DECEMBER);
  }
  uint16_t year = years + CLOCK_YEAR_1900 + 1;
  bool leap = is_leap_year(year);
  uint8_t month = 1;

  while (month < CLOCK_MONTHS_PER_YEAR &&
         clock_days_before_month(month + 1, leap) < day) {
    month++;
  }
  return clock_pack(year, month, day - clock_days_before_month(month, leap));
}

/* Takes the GPS-UTC offset from a GPS date and time, as 0 while
 * get_gps_utc_offset says it is not valid (0x2158c).  The 58503A tests
 * the offset's validity the right way round (0x215b2), where the
 * Z3805A's uses it only while not valid. */
void gps_to_utc(uint32_t* date, time_of_day* time) {
  packed_datetime value = {.time = *time, .date = *date};
  packed_datetime change = {.date = 0};
  int16_t offset;

  if (!get_gps_utc_offset(&offset)) {
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

/* Whether a date is the first day of a quarter, January, April, July or
 * October 1. */
static bool calendar_starts_quarter(uint16_t month_day) {
  return month_day == calendar_month_day(1, 1) ||
         month_day == calendar_month_day(CLOCK_APRIL, 1) ||
         month_day == calendar_month_day(CLOCK_JULY, 1) ||
         month_day == calendar_month_day(CLOCK_OCTOBER, 1);
}

/* Whether a date is the first or the last day of a quarter: a leap
 * second's day or the day after it (0x21612). */
uint8_t date_at_quarter_edge(uint32_t date) {
  uint16_t month_day = date;

  return calendar_starts_quarter(month_day) ||
         month_day == calendar_month_day(CLOCK_MARCH, 31) ||
         month_day == calendar_month_day(CLOCK_JUNE, 30) ||
         month_day == calendar_month_day(CLOCK_SEPTEMBER, 30) ||
         month_day == calendar_month_day(CLOCK_DECEMBER, 31);
}

/* Whether a date is the first day of a quarter (0x2168c). */
uint8_t date_starts_quarter(uint32_t date) {
  return calendar_starts_quarter(date);
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

/* Applies nv_settings.time_zone to a UTC date and time (0x217c8). */
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
 * (0x2187e). */
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
