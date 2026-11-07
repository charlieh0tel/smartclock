/* What the calendar, the clock record, the klok task, the leap second,
 * the time code and the status screen share (calendar.c, clock.c,
 * klok.c, leap.c, timecode.c, screen.c): a time's and a date's fields,
 * their packing and the units between them, the months, the calendar's
 * days before a month and the GPS-UTC offset as a change, and the klok
 * task's message size. The exported C
 * includes the hand-written headers after its own declarations and
 * before the hand-written .c files, so every one of them can use it.
 *
 * A time is a long, hour << 16 | minute << 8 | second, or a time_of_day
 * where the code works on it byte by byte; a date year << 16 | month <<
 * 8 | day, or a calendar_date (docs/firmware/structures.md, "A date and
 * time").  A day count is a Modified Julian Day: date_to_days of
 * 1901-01-01 is 15385. */

#include <stddef.h>

/* A const object is not a constant expression in C17, so the derived
 * constants spell out their values. */
static const uint8_t CLOCK_SECONDS_PER_MINUTE = 60;

static const uint8_t CLOCK_MINUTES_PER_HOUR = 60;

static const int32_t CLOCK_SECONDS_PER_HOUR = 60 * 60;

static const uint8_t CLOCK_HOURS_PER_DAY = 24;

static const int32_t CLOCK_SECONDS_PER_DAY = 60 * 60 * 24;

static const uint8_t CLOCK_LAST_HOUR = 24 - 1;

static const uint8_t CLOCK_LAST_MINUTE = 60 - 1;

static const uint8_t CLOCK_FIELD_BITS = 8;

static const uint8_t CLOCK_FIELD_MASK = 0xff;

static const uint8_t CLOCK_HIGH_SHIFT = 2 * 8;

/* A minute 59 or 61 seconds long, nv_settings.leap_minute_length. */
static const uint8_t CLOCK_LEAP_MINUTE_SHORT = 60 - 1;

static const uint8_t CLOCK_LEAP_MINUTE_LONG = 60 + 1;

static uint32_t clock_pack(uint32_t high, uint32_t middle, uint32_t low) {
  return high << CLOCK_HIGH_SHIFT |
         (middle & CLOCK_FIELD_MASK) << CLOCK_FIELD_BITS |
         (low & CLOCK_FIELD_MASK);
}

static uint16_t clock_year(uint32_t date) { return date >> CLOCK_HIGH_SHIFT; }

static uint8_t clock_middle(uint32_t packed) {
  return packed >> CLOCK_FIELD_BITS;
}

static uint8_t clock_low(uint32_t packed) { return packed; }

/* A time_of_day as the long its bytes make, and back. */
static uint32_t clock_time_bits(time_of_day time) {
  return (uint32_t)time.reserved << (3 * CLOCK_FIELD_BITS) |
         clock_pack(time.hour, time.minute, time.second);
}

/* A calendar_date as a long holds it, year << 16 | month << 8 | day. */
static calendar_date clock_date_of(uint32_t packed) {
  return (calendar_date){
      .year = clock_year(packed),
      .month = clock_middle(packed),
      .day = clock_low(packed),
  };
}

static time_of_day clock_time_of(uint32_t bits) {
  return (time_of_day){
      .reserved = bits >> (3 * CLOCK_FIELD_BITS),
      .hour = bits >> CLOCK_HIGH_SHIFT,
      .minute = clock_middle(bits),
      .second = clock_low(bits),
  };
}

/* The months the calendar's arithmetic and the leap second's dates
 * name, 1 to 12. */
static const uint8_t CLOCK_FEBRUARY = 2;

static const uint8_t CLOCK_MARCH = 3;

static const uint8_t CLOCK_APRIL = 4;

static const uint8_t CLOCK_JUNE = 6;

static const uint8_t CLOCK_JULY = 7;

static const uint8_t CLOCK_SEPTEMBER = 9;

static const uint8_t CLOCK_OCTOBER = 10;

static const uint8_t CLOCK_DECEMBER = 12;

static const uint8_t CLOCK_MONTHS_PER_YEAR = 12;

static const uint16_t CLOCK_DAYS_PER_YEAR = 365;

static const uint8_t CLOCK_YEARS_PER_LEAP = 4;

static const uint16_t CLOCK_YEAR_1900 = 1900;

/* The days in a year before a month, 1 to 12.  The image indexes its
 * tables by the month unchecked: month 0 reads the word before the table,
 * a month past 12 the words after it, so these stay the image's. */
static uint16_t clock_days_before_month(uint8_t month, bool leap) {
  const int16_t* table = leap ? days_before_month_leap : days_before_month;

  return table[month - 1];
}

/* The GPS-UTC offset as a change to a date and time, and whether to add it
 * (or take it away): its magnitude as hours, minutes and seconds. */
static bool clock_offset_change(int16_t offset, packed_datetime* change) {
  change->time = clock_time_of(seconds_to_hms(offset < 1 ? -offset : offset));
  return offset >= 1;
}

/* The klok task's error messages' text, which klok_task_create and
 * clk_do format: an array size, so #define. */
#define KLOK_MESSAGE_SIZE 80
