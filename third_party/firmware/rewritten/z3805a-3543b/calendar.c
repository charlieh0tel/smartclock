/* Date and time arithmetic: packed dates and times, day counts and the
 * Modified Julian Day, local and GPS time, written by hand in place of
 * the decompiler's (docs/firmware/README.md, "Hand-written C";
 * docs/firmware/modules.md, `services/calendar`).  The exported C
 * includes this file after its own, so the types and prototypes it
 * declares are in scope; each function here keeps its generated
 * prototype. */

static const uint8_t CLOCK_MONTHS_PER_YEAR = 12;

static const uint8_t CLOCK_FEBRUARY = 2;

static const uint16_t CLOCK_DAYS_PER_YEAR = 365;

static const uint8_t CLOCK_YEARS_PER_LEAP = 4;

/* The Modified Julian Day of 1899-12-31. */
static const uint16_t CLOCK_MJD_1899_12_31 = 15019;

static const uint16_t CLOCK_YEAR_1900 = 1900;

static const uint16_t CLOCK_YEAR_1904 = 1904;

/* A time of day as seconds since midnight (0x1111e). */
int32_t hms_to_seconds(uint32_t time) {
  uint32_t hour = time >> CLOCK_HIGH_SHIFT & CLOCK_FIELD_MASK;
  uint32_t minute = time >> CLOCK_FIELD_BITS & CLOCK_FIELD_MASK;
  uint32_t second = time & CLOCK_FIELD_MASK;

  return (hour * CLOCK_MINUTES_PER_HOUR + minute) * CLOCK_SECONDS_PER_MINUTE +
         second;
}

/* The same, a second copy in the image (0x11152). */
int32_t time_to_seconds(uint32_t time) { return hms_to_seconds(time); }

/* The same, a third copy, which nothing calls (0x10e3a). */
int32_t time_packed_to_seconds(uint32_t time) { return hms_to_seconds(time); }

/* The days in a year before a month, 1 to 12.  The image indexes its
 * tables by the month unchecked: month 0 reads the word before the table,
 * a month past 12 the words after it, so these stay the image's. */
static uint16_t clock_days_before_month(uint8_t month, bool leap) {
  const int16_t* table = leap ? days_before_month_leap : days_before_month;

  return table[month - 1];
}

/* The days from an epoch's year's start to a date, the date's own day
 * counted: the date_to_days arithmetic, its epoch year and first leap year
 * the caller's.  The leap years before the date's are counted as C divides,
 * toward zero, and kept to a word, as the image keeps them. */
static uint32_t clock_days_since(uint16_t year, uint8_t month, uint8_t day,
                                 uint16_t epoch_year,
                                 uint16_t first_leap_year) {
  uint16_t leap_years = (year - first_leap_year + CLOCK_YEARS_PER_LEAP - 1) /
                        CLOCK_YEARS_PER_LEAP;
  uint32_t days = (uint32_t)year * CLOCK_DAYS_PER_YEAR -
                  (uint32_t)epoch_year * CLOCK_DAYS_PER_YEAR + leap_years +
                  clock_days_before_month(month, false) + day;

  if (is_leap_year(year) && month > CLOCK_FEBRUARY) {
    days++;
  }
  return days;
}

/* 1 for a leap year, by the Gregorian rule (0x10db6). */
uint32_t is_leap_year(uint16_t year) {
  return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

/* A date's day of the year, the es message's %03d (0x10de4). */
int16_t day_of_year(uint32_t date) {
  uint8_t month = clock_middle(date);
  bool leap = is_leap_year(clock_year(date));

  return clock_days_before_month(month, leap) + clock_low(date);
}

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

/* A date as a day count, for datetime_add (0x10f46). */
int32_t date_to_days(uint32_t date) {
  return clock_days_since(clock_year(date), clock_middle(date), clock_low(date),
                          CLOCK_YEAR_1900, CLOCK_YEAR_1904) +
         CLOCK_MJD_1899_12_31;
}

/* The same of an Oncore date, month << 24 | day << 16 | year
 * (oncore_date_fields), which nothing calls (0x10fbc). */
int32_t date_to_mjd(uint32_t date) {
  uint8_t month = date >> (3 * CLOCK_FIELD_BITS);
  uint8_t day = date >> CLOCK_HIGH_SHIFT;
  uint16_t year = date;

  return clock_days_since(year, month, day, CLOCK_YEAR_1900, CLOCK_YEAR_1904) +
         CLOCK_MJD_1899_12_31;
}

/* The days in a month, 28 or 29 for February; January's for a month not 1
 * to 12 (0x11032). */
uint8_t days_in_month(uint16_t year, uint8_t month) {
  static const uint8_t FEBRUARY_DAYS = 28;
  /* The image's table at 0x1189c, month_lengths. */
  static const uint8_t MONTH_LENGTHS[] = {31, 28, 31, 30, 31, 30,
                                          31, 31, 30, 31, 30, 31};

  if (month == CLOCK_FEBRUARY) {
    return is_leap_year(year) ? FEBRUARY_DAYS + 1 : FEBRUARY_DAYS;
  }
  if (month == 0 || month > CLOCK_MONTHS_PER_YEAR) {
    return MONTH_LENGTHS[0];
  }
  return MONTH_LENGTHS[month - 1];
}

/* A date with its overflowing months and days carried into the next
 * (0x11076). */
uint32_t date_normalize(uint32_t date) {
  uint16_t year = clock_year(date);
  uint8_t month = clock_middle(date);
  uint8_t day = clock_low(date);

  while (month > CLOCK_MONTHS_PER_YEAR) {
    month -= CLOCK_MONTHS_PER_YEAR;
    year++;
  }
  for (uint8_t length; day > (length = days_in_month(year, month));) {
    day -= length;
    month++;
    if (month > CLOCK_MONTHS_PER_YEAR) {
      month -= CLOCK_MONTHS_PER_YEAR;
      year++;
    }
  }
  return clock_pack(year, month, day);
}

/* A date as an Oncore date, month << 24 | day << 16 | year, the inverse of
 * oncore_date_pack; nothing calls it (0x110d6). */
uint32_t oncore_date_unpack(uint32_t date) {
  return (uint32_t)clock_middle(date) << (3 * CLOCK_FIELD_BITS) |
         (uint32_t)clock_low(date) << CLOCK_HIGH_SHIFT | clock_year(date);
}

/* @@Ba's month, day and year as a date (0x110fa). */
uint32_t oncore_date_pack(oncore_date_fields fields) {
  return clock_pack(fields.year, fields.month, fields.day);
}

/* Seconds as a time of day, the hours not wrapped past 24 but kept to a
 * byte (0x11186). */
uint32_t seconds_to_hms(uint32_t seconds) {
  static const uint16_t SECONDS_PER_HOUR = 60 * 60;
  uint8_t hour = seconds / SECONDS_PER_HOUR;
  uint32_t rest = seconds % SECONDS_PER_HOUR;

  return clock_pack(hour, rest / CLOCK_SECONDS_PER_MINUTE,
                    rest % CLOCK_SECONDS_PER_MINUTE);
}

/* The seconds from the GPS epoch, 1980-01-06, to a date and time, 0 before
 * 1980; the T1 time code's #H%08X (0x111cc). */
int32_t gps_seconds(uint32_t date, uint32_t time) {
  /* The GPS epoch, 1980-01-06, and the first year whose seconds
   * gps_seconds counts. */
  static const uint16_t GPS_EPOCH_YEAR = 1980;
  static const uint8_t GPS_EPOCH_DAY = 6;
  uint16_t year = clock_year(date);

  if (year < GPS_EPOCH_YEAR) {
    return 0;
  }
  int32_t days = clock_days_since(year, clock_middle(date), clock_low(date),
                                  GPS_EPOCH_YEAR, GPS_EPOCH_YEAR) -
                 GPS_EPOCH_DAY;

  return days * CLOCK_SECONDS_PER_DAY + hms_to_seconds(time);
}

/* Adds the first date and time to the second, the first's date a count of
 * days, carrying seconds into minutes, hours and days (0x11396). */
void datetime_add(packed_datetime* change, packed_datetime* value) {
  time_of_day by = change->time;
  time_of_day time = value->time;
  uint32_t days = date_to_days(value->date) + change->date;

  for (time.second += by.second; time.second > CLOCK_SECONDS_PER_MINUTE - 1;) {
    time.second -= CLOCK_SECONDS_PER_MINUTE;
    time.minute++;
  }
  for (time.minute += by.minute; time.minute > CLOCK_LAST_MINUTE;) {
    time.minute -= CLOCK_MINUTES_PER_HOUR;
    time.hour++;
  }
  for (time.hour += by.hour; time.hour > CLOCK_LAST_HOUR;) {
    time.hour -= CLOCK_HOURS_PER_DAY;
    days++;
  }
  value->date = days_to_date(days);
  value->time = time;
}

/* Takes the first date and time from the second, borrowing through the
 * date; each field's borrow is taken while it is negative as a signed
 * byte (0x11428). */
void datetime_subtract(packed_datetime* change, packed_datetime* value) {
  time_of_day by = change->time;
  time_of_day time = value->time;
  uint32_t days = date_to_days(value->date) - change->date;

  for (time.second -= by.second; (int8_t)time.second < 0;) {
    time.second += CLOCK_SECONDS_PER_MINUTE;
    time.minute--;
  }
  for (time.minute -= by.minute; (int8_t)time.minute < 0;) {
    time.minute += CLOCK_MINUTES_PER_HOUR;
    time.hour--;
  }
  for (time.hour -= by.hour; (int8_t)time.hour < 0;) {
    time.hour += CLOCK_HOURS_PER_DAY;
    days--;
  }
  value->date = days_to_date(days);
  value->time = time;
}

/* Compares two times and dates, each a time then its date: by days, then
 * by seconds (0x114b4). */
int32_t datetime_compare(uint32_t* first, uint32_t* second) {
  int32_t days = date_to_days(first[1]) - date_to_days(second[1]);

  if (days != 0) {
    return days;
  }
  return time_to_seconds(first[0]) - time_to_seconds(second[0]);
}

/* The GPS-UTC offset as a change to a date and time, and whether to add it
 * (or take it away): its magnitude as hours, minutes and seconds. */
static bool clock_offset_change(int16_t offset, packed_datetime* change) {
  change->time = clock_time_of(seconds_to_hms(offset < 1 ? -offset : offset));
  return offset >= 1;
}

/* Adds the GPS-UTC offset to a UTC date and time; nothing calls it
 * (0x114fa). */
void utc_to_gps(uint32_t* date, time_of_day* time) {
  packed_datetime value = {.time = *time, .date = *date};
  packed_datetime change = {.date = 0};
  int16_t offset;

  if (!get_gps_utc_offset(&offset)) {
    offset = 0;
  }
  if (clock_offset_change(offset, &change)) {
    datetime_add(&change, &value);
  } else {
    datetime_subtract(&change, &value);
  }
  *date = value.date;
  *time = value.time;
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

/* A time zone's hours and minutes as a magnitude and a sign, for
 * :PTIMe:TZONe?: div's quotient and remainder of its minutes, each made
 * positive; -1 if either was negative, else 1 (0x2e0d8). */
int8_t time_zone_split(int8_t hours, int8_t minutes, int8_t* hours_out,
                       int8_t* minutes_out) {
  uint64_t hours_minutes =
      div(hours * CLOCK_MINUTES_PER_HOUR + minutes, CLOCK_MINUTES_PER_HOUR);
  int32_t whole_hours = hours_minutes >> 32;
  int32_t left_minutes = hours_minutes;
  int8_t sign = 1;

  *hours_out = whole_hours;
  *minutes_out = left_minutes;
  if (whole_hours < 0) {
    sign = -1;
    *hours_out = -whole_hours;
  }
  if (left_minutes < 0) {
    sign = -1;
    *minutes_out = -left_minutes;
  }
  return sign;
}
