/* What the calendar, the clock record, the leap second and the time code
 * share (calendar.c, clock.c, leap.c, timecode.c): a time's and a date's
 * fields, their packing and the units between them. The exported C
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

static const uint8_t CLOCK_HOURS_PER_DAY = 24;

static const int32_t CLOCK_SECONDS_PER_DAY = 60 * 60 * 24;

static const uint8_t CLOCK_LAST_HOUR = 24 - 1;

static const uint8_t CLOCK_LAST_MINUTE = 60 - 1;

static const uint8_t CLOCK_FIELD_BITS = 8;

static const uint8_t CLOCK_FIELD_MASK = 0xff;

static const uint8_t CLOCK_HIGH_SHIFT = 2 * 8;

/* A minute 59 or 61 seconds long, nv_settings.leap_minute_length. */
static const uint8_t CLOCK_LEAP_MINUTE_SHORT = 60 - 1;

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

static time_of_day clock_time_of(uint32_t bits) {
  return (time_of_day){
      .reserved = bits >> (3 * CLOCK_FIELD_BITS),
      .hour = bits >> CLOCK_HIGH_SHIFT,
      .minute = clock_middle(bits),
      .second = clock_low(bits),
  };
}
