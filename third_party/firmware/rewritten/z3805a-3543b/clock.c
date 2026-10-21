/* The clock and calendar, written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"): dates, times and their
 * conversions, the leap second's clock events, the clock record's readers
 * (docs/firmware/structures.md, "The clock record") and the exceptions'
 * report.  The exported C includes this file after
 * its own, so the types and prototypes it declares are in scope; each
 * function here keeps its generated prototype.
 *
 * A time is a long, hour << 16 | minute << 8 | second, or a time_of_day
 * where the code works on it byte by byte; a date year << 16 | month << 8
 * | day, or a calendar_date (docs/firmware/structures.md, "A date and
 * time").  A day count is a Modified Julian Day: date_to_days of
 * 1901-01-01 is 15385. */

#include <stddef.h>

enum {
	CLOCK_SECONDS_PER_MINUTE = 60,
	CLOCK_MINUTES_PER_HOUR = 60,
	CLOCK_HOURS_PER_DAY = 24,
	CLOCK_SECONDS_PER_HOUR = CLOCK_SECONDS_PER_MINUTE * CLOCK_MINUTES_PER_HOUR,
	CLOCK_SECONDS_PER_DAY = CLOCK_SECONDS_PER_HOUR * CLOCK_HOURS_PER_DAY,
	CLOCK_LAST_HOUR = CLOCK_HOURS_PER_DAY - 1,
	CLOCK_LAST_MINUTE = CLOCK_MINUTES_PER_HOUR - 1,
	CLOCK_FIELD_BITS = 8,
	CLOCK_FIELD_MASK = 0xff,
	CLOCK_HIGH_SHIFT = 2 * CLOCK_FIELD_BITS,

	CLOCK_MONTHS_PER_YEAR = 12,
	CLOCK_FEBRUARY = 2,
	CLOCK_MARCH = 3,
	CLOCK_JUNE = 6,
	CLOCK_SEPTEMBER = 9,
	CLOCK_DECEMBER = 12,
	CLOCK_FEBRUARY_DAYS = 28,
	CLOCK_DAYS_PER_YEAR = 365,
	CLOCK_DAYS_PER_4_YEARS = 4 * CLOCK_DAYS_PER_YEAR + 1,
	CLOCK_YEARS_PER_LEAP = 4,

	/* The Modified Julian Days of 1899-12-31 and 1900-12-31; days_to_date
	 * counts its years from the second and makes no date before
	 * 1901-01-01. */
	CLOCK_MJD_1899_12_31 = 15019,
	CLOCK_MJD_1900_12_31 = 15384,
	CLOCK_YEAR_1900 = 1900,
	CLOCK_YEAR_1901 = 1901,
	CLOCK_YEAR_1904 = 1904,
	/* The GPS epoch, 1980-01-06, and the first year whose seconds
	 * gps_seconds counts. */
	CLOCK_GPS_EPOCH_YEAR = 1980,
	CLOCK_GPS_EPOCH_DAY = 6,

	/* A minute 59 or 61 seconds long, nv_settings.leap_minute_length. */
	CLOCK_LEAP_MINUTE_SHORT = CLOCK_SECONDS_PER_MINUTE - 1,
	CLOCK_LEAP_MINUTE_LONG = CLOCK_SECONDS_PER_MINUTE + 1,
	/* leap_second_near is true past 23:59:45. */
	CLOCK_LEAP_NEAR_SECOND = 45,

	/* The status register's interrupt mask, all levels masked. */
	CLOCK_SR_INTERRUPTS_MASKED = 0x700,

	/* leap_event_schedule_utc's event comes this many seconds before the
	 * offset steps (0x1070a). */
	CLOCK_LEAP_EVENT_LEAD = 5,

	/* An exception frame's format and vector word: the format in the top
	 * four bits, the vector's offset in the rest; exception_names names
	 * the vectors from 2, bus error, to 58 (offset 0xe8). */
	CLOCK_VECTOR_FORMAT_MASK = 0xf000,
	CLOCK_VECTOR_OFFSET_MASK = 0x0fff,
	CLOCK_VECTOR_OFFSET_FIRST_NAMED = 2 * 4,
	CLOCK_VECTOR_OFFSET_LAST = 0xe8,
};

/* A time of day as seconds since midnight (0x1111e). */
int32_t hms_to_seconds(uint32_t time)
{
	uint32_t hour = time >> CLOCK_HIGH_SHIFT & CLOCK_FIELD_MASK;
	uint32_t minute = time >> CLOCK_FIELD_BITS & CLOCK_FIELD_MASK;
	uint32_t second = time & CLOCK_FIELD_MASK;

	return (hour * CLOCK_MINUTES_PER_HOUR + minute) * CLOCK_SECONDS_PER_MINUTE + second;
}

/* The same, a second copy in the image (0x11152). */
int32_t time_to_seconds(uint32_t time)
{
	return hms_to_seconds(time);
}

/* The same, a third copy, which nothing calls (0x10e3a). */
int32_t time_packed_to_seconds(uint32_t time)
{
	return hms_to_seconds(time);
}

static uint32_t clock_pack(uint32_t high, uint32_t middle, uint32_t low)
{
	return high << CLOCK_HIGH_SHIFT | (middle & CLOCK_FIELD_MASK) << CLOCK_FIELD_BITS |
	       (low & CLOCK_FIELD_MASK);
}

static uint16_t clock_year(uint32_t date)
{
	return date >> CLOCK_HIGH_SHIFT;
}

static uint8_t clock_middle(uint32_t packed)
{
	return packed >> CLOCK_FIELD_BITS;
}

static uint8_t clock_low(uint32_t packed)
{
	return packed;
}

/* A time_of_day as the long its bytes make, and back. */
static uint32_t clock_time_bits(time_of_day time)
{
	return (uint32_t)time.reserved << (3 * CLOCK_FIELD_BITS) | clock_pack(time.hour, time.minute,
									     time.second);
}

static time_of_day clock_time_of(uint32_t bits)
{
	return (time_of_day){
		.reserved = bits >> (3 * CLOCK_FIELD_BITS),
		.hour = bits >> CLOCK_HIGH_SHIFT,
		.minute = clock_middle(bits),
		.second = clock_low(bits),
	};
}

/* The days in a year before a month, 1 to 12.  The image indexes its
 * tables by the month unchecked: month 0 reads the word before the table,
 * a month past 12 the words after it. */
static uint16_t clock_days_before_month(uint8_t month, bool leap)
{
	const int16_t *table = leap ? days_before_month_leap : days_before_month;

	return table[month - 1];
}

/* The days from an epoch's year's start to a date, the date's own day
 * counted: the date_to_days arithmetic, its epoch year and first leap year
 * the caller's.  The leap years before the date's are counted as C divides,
 * toward zero, and kept to a word, as the image keeps them. */
static uint32_t clock_days_since(uint16_t year, uint8_t month, uint8_t day, uint16_t epoch_year,
				 uint16_t first_leap_year)
{
	uint16_t leap_years =
		((int32_t)year - first_leap_year + CLOCK_YEARS_PER_LEAP - 1) / CLOCK_YEARS_PER_LEAP;
	uint32_t days = (uint32_t)year * CLOCK_DAYS_PER_YEAR -
			(uint32_t)epoch_year * CLOCK_DAYS_PER_YEAR + leap_years +
			clock_days_before_month(month, false) + day;

	if (is_leap_year(year) && month > CLOCK_FEBRUARY)
		days++;
	return days;
}

/* 1 for a leap year, by the Gregorian rule (0x10db6). */
uint32_t is_leap_year(uint16_t year)
{
	return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

/* A date's day of the year, the es message's %03d (0x10de4). */
int16_t day_of_year(uint32_t date)
{
	uint8_t month = clock_middle(date);
	bool leap = is_leap_year(clock_year(date));

	return clock_days_before_month(month, leap) + clock_low(date);
}

/* A day count as a date, none before 1901-01-01 (0x10e72).  A leap year's
 * December 31 comes out as the next year's January 0: the year is the
 * count's quarter-days over 1461, which reaches the next year a day
 * early. */
uint32_t days_to_date(uint32_t days)
{
	if (days <= CLOCK_MJD_1900_12_31 + 1)
		days = CLOCK_MJD_1900_12_31 + 1;
	uint32_t since = days - CLOCK_MJD_1900_12_31;
	uint16_t years = since * CLOCK_YEARS_PER_LEAP / CLOCK_DAYS_PER_4_YEARS;
	uint32_t day = since - (years / CLOCK_YEARS_PER_LEAP + (uint32_t)years * CLOCK_DAYS_PER_YEAR);
	uint16_t year = years + CLOCK_YEAR_1901;
	bool leap = is_leap_year(year);
	uint8_t month = 1;

	while (month < CLOCK_MONTHS_PER_YEAR && clock_days_before_month(month + 1, leap) < day)
		month++;
	return clock_pack(year, month, day - clock_days_before_month(month, leap));
}

/* A date as a day count, for datetime_add (0x10f46). */
int32_t date_to_days(uint32_t date)
{
	return clock_days_since(clock_year(date), clock_middle(date), clock_low(date),
				CLOCK_YEAR_1900, CLOCK_YEAR_1904) +
	       CLOCK_MJD_1899_12_31;
}

/* The same of an Oncore date, month << 24 | day << 16 | year
 * (oncore_date_fields), which nothing calls (0x10fbc). */
int32_t date_to_mjd(uint32_t date)
{
	uint8_t month = date >> (3 * CLOCK_FIELD_BITS);
	uint8_t day = date >> CLOCK_HIGH_SHIFT;
	uint16_t year = date;

	return clock_days_since(year, month, day, CLOCK_YEAR_1900, CLOCK_YEAR_1904) +
	       CLOCK_MJD_1899_12_31;
}

/* The days in a month, 28 or 29 for February; January's for a month not 1
 * to 12 (0x11032). */
uint8_t days_in_month(uint16_t year, uint8_t month)
{
	if (month == CLOCK_FEBRUARY)
		return is_leap_year(year) ? CLOCK_FEBRUARY_DAYS + 1 : CLOCK_FEBRUARY_DAYS;
	if (month == 0 || month > CLOCK_MONTHS_PER_YEAR)
		return month_lengths[0];
	return month_lengths[month - 1];
}

/* A date with its overflowing months and days carried into the next
 * (0x11076). */
uint32_t date_normalize(uint32_t date)
{
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
uint32_t oncore_date_unpack(uint32_t date)
{
	return (uint32_t)clock_middle(date) << (3 * CLOCK_FIELD_BITS) |
	       (uint32_t)clock_low(date) << CLOCK_HIGH_SHIFT | clock_year(date);
}

/* @@Ba's month, day and year as a date (0x110fa). */
uint32_t oncore_date_pack(oncore_date_fields fields)
{
	return clock_pack(fields.year, fields.month, fields.day);
}

/* Seconds as a time of day, the hours not wrapped past 24 but kept to a
 * byte (0x11186). */
uint32_t seconds_to_hms(uint32_t seconds)
{
	uint8_t hour = seconds / CLOCK_SECONDS_PER_HOUR;
	uint32_t rest = seconds % CLOCK_SECONDS_PER_HOUR;

	return clock_pack(hour, rest / CLOCK_SECONDS_PER_MINUTE, rest % CLOCK_SECONDS_PER_MINUTE);
}

/* The seconds from the GPS epoch, 1980-01-06, to a date and time, 0 before
 * 1980; the T1 time code's #H%08X (0x111cc). */
int32_t gps_seconds(uint32_t date, uint32_t time)
{
	uint16_t year = clock_year(date);

	if (year < CLOCK_GPS_EPOCH_YEAR)
		return 0;
	int32_t days = clock_days_since(year, clock_middle(date), clock_low(date),
					CLOCK_GPS_EPOCH_YEAR, CLOCK_GPS_EPOCH_YEAR) -
		       CLOCK_GPS_EPOCH_DAY;

	return days * CLOCK_SECONDS_PER_DAY + hms_to_seconds(time);
}

/* The pending leap second's GPS seconds, at the end of its date's last
 * minute, for :PTIM:LEAP:GPST?; 0 with none pending (0x11284). */
int32_t leap_gps_time(void)
{
	uint16_t sr = set_interrupt_mask(CLOCK_SR_INTERRUPTS_MASKED);
	uint8_t pending = nv_settings.leap_pending;
	int32_t minute_length = nv_settings.leap_minute_length;
	uint32_t date = nv_settings.leap_date;
	int16_t offset;

	set_interrupt_mask(sr);
	if (!pending)
		return 0;
	int32_t seconds = gps_seconds(date, 0);

	if (!get_gps_utc_offset(&offset))
		offset = 0;
	if (minute_length == CLOCK_LEAP_MINUTE_LONG)
		return offset + seconds + CLOCK_SECONDS_PER_DAY;
	return offset + seconds + CLOCK_SECONDS_PER_DAY - 1;
}

/* Advances a date and time by a second, the minute 59 or 61 seconds long at
 * a pending leap second's 23:59 when the clock keeps UTC (0x11300). */
void time_next_second(uint32_t *date, time_of_day *value)
{
	time_of_day time = *value;
	uint8_t minute_length = CLOCK_SECONDS_PER_MINUTE;

	if (nv_settings.gps_utc && nv_settings.leap_pending && nv_settings.leap_date == *date &&
	    time.hour == CLOCK_LAST_HOUR && time.minute == CLOCK_LAST_MINUTE)
		minute_length = nv_settings.leap_minute_length;
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

/* Adds the first date and time to the second, the first's date a count of
 * days, carrying seconds into minutes, hours and days (0x11396). */
void datetime_add(packed_datetime *change, packed_datetime *value)
{
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
void datetime_subtract(packed_datetime *change, packed_datetime *value)
{
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
int32_t datetime_compare(uint32_t *first, uint32_t *second)
{
	int32_t days = date_to_days(first[1]) - date_to_days(second[1]);

	if (days != 0)
		return days;
	return time_to_seconds(first[0]) - time_to_seconds(second[0]);
}

/* The GPS-UTC offset as a change to a date and time, and whether to add it
 * (or take it away): its magnitude as hours, minutes and seconds. */
static bool clock_offset_change(int16_t offset, packed_datetime *change)
{
	change->time = clock_time_of(seconds_to_hms(offset < 1 ? -(int32_t)offset : offset));
	return offset >= 1;
}

/* Adds the GPS-UTC offset to a UTC date and time; nothing calls it
 * (0x114fa). */
void utc_to_gps(uint32_t *date, time_of_day *time)
{
	packed_datetime value = { .time = *time, .date = *date };
	packed_datetime change = { .date = 0 };
	int16_t offset;

	if (!get_gps_utc_offset(&offset))
		offset = 0;
	if (clock_offset_change(offset, &change))
		datetime_add(&change, &value);
	else
		datetime_subtract(&change, &value);
	*date = value.date;
	*time = value.time;
}

/* Takes the GPS-UTC offset from a GPS date and time.  The image takes it
 * as 0 while get_gps_utc_offset says it is valid, the other way round from
 * utc_to_gps, so that it uses the offset only while not valid (0x11580; the test at
 * 0x115a6). */
void gps_to_utc(uint32_t *date, time_of_day *time)
{
	packed_datetime value = { .time = *time, .date = *date };
	packed_datetime change = { .date = 0 };
	int16_t offset;

	if (get_gps_utc_offset(&offset))
		offset = 0;
	if (clock_offset_change(offset, &change))
		datetime_subtract(&change, &value);
	else
		datetime_add(&change, &value);
	*date = value.date;
	*time = value.time;
}

/* The last day of the GPS date's quarter, when a leap second may come:
 * March 31, June 30, September 30 or December 31; March 31 for a month not
 * 1 to 12 (0x11606). */
uint32_t leap_opportunity_date(void)
{
	uint32_t gps_date = get_gps_date();
	uint8_t month = clock_middle(gps_date);
	calendar_date date = { .year = clock_year(gps_date), .month = CLOCK_MARCH, .day = 31 };

	if (month >= 4 && month <= CLOCK_JUNE)
		date = (calendar_date){ .year = date.year, .month = CLOCK_JUNE, .day = 30 };
	else if (month > CLOCK_JUNE && month <= CLOCK_SEPTEMBER)
		date = (calendar_date){ .year = date.year, .month = CLOCK_SEPTEMBER, .day = 30 };
	else if (month > CLOCK_SEPTEMBER && month <= CLOCK_DECEMBER)
		date = (calendar_date){ .year = date.year, .month = CLOCK_DECEMBER, .day = 31 };
	return clock_pack(date.year, date.month, date.day);
}

/* Whether the GPS clock, as UTC unless the clock keeps GPS time, is past
 * 23:59:45 on the last day of a quarter (0x11680). */
uint8_t leap_second_near(void)
{
	uint32_t date;
	time_of_day time;

	get_gps_datetime(&date, &time);
	if (nv_settings.gps_utc == 0)
		gps_to_utc(&date, &time);
	uint16_t month_day = date;

	bool quarter_end = month_day == (CLOCK_MARCH << CLOCK_FIELD_BITS | 31) ||
			   month_day == (CLOCK_JUNE << CLOCK_FIELD_BITS | 30) ||
			   month_day == (CLOCK_SEPTEMBER << CLOCK_FIELD_BITS | 30) ||
			   month_day == (CLOCK_DECEMBER << CLOCK_FIELD_BITS | 31);

	return quarter_end && time.hour == CLOCK_LAST_HOUR && time.minute == CLOCK_LAST_MINUTE &&
	       time.second > CLOCK_LEAP_NEAR_SECOND;
}

/* nv_settings.time_zone, hours and minutes, as a change to a date and time,
 * and whether it is negative: div's quotient and remainder of its minutes,
 * each made positive, with either's sign the change's. */
static bool clock_time_zone_change(packed_datetime *change)
{
	uint64_t hours_minutes = div(nv_settings.time_zone[0] * CLOCK_MINUTES_PER_HOUR +
					     nv_settings.time_zone[1],
				     CLOCK_MINUTES_PER_HOUR);
	int32_t hours = hours_minutes >> 32;
	int32_t minutes = (uint32_t)hours_minutes;
	bool negative = hours < 0 || minutes < 0;

	change->time = (time_of_day){
		.hour = hours < 0 ? -hours : hours,
		.minute = minutes < 0 ? -minutes : minutes,
	};
	return negative;
}

/* Applies nv_settings.time_zone to a UTC date and time (0x11700). */
void utc_to_local(uint32_t *date, time_of_day *time)
{
	packed_datetime value = { .time = *time, .date = *date };
	packed_datetime change = { .date = 0 };

	if (clock_time_zone_change(&change))
		datetime_subtract(&change, &value);
	else
		datetime_add(&change, &value);
	*date = value.date;
	*time = value.time;
}

/* Takes nv_settings.time_zone off a local date and time; nothing calls it
 * (0x117b6). */
void local_to_utc(uint32_t *date, time_of_day *time)
{
	packed_datetime value = { .time = *time, .date = *date };
	packed_datetime change = { .date = 0 };

	if (clock_time_zone_change(&change))
		datetime_add(&change, &value);
	else
		datetime_subtract(&change, &value);
	*date = value.date;
	*time = value.time;
}

/* Queues the clock event that makes the leap date's last minute 59 or 61
 * seconds, in GPS time; NULL for a 60-second minute or any other
 * (0x105ee). */
klok_entry *leap_event_schedule_gps(uint32_t date, int32_t minute_length)
{
	klok_entry event = leap_event_gps_template;

	event.date = date;
	if (minute_length == CLOCK_LEAP_MINUTE_SHORT)
		event.function = leap_minute_59_arm;
	else if (minute_length == CLOCK_LEAP_MINUTE_LONG)
		event.function = leap_minute_61_arm;
	else
		return NULL;
	return clk_do(&event);
}

/* Queues the clock event for a 59- or 61-second minute in UTC: at 24:00:00
 * of the leap date moved by the GPS-UTC offset as it will be, which
 * offset_step_time keeps, less 5 seconds; NULL for any other minute
 * (0x10656). */
klok_entry *leap_event_schedule_utc(uint32_t date, int32_t minute_length)
{
	klok_entry event = leap_event_utc_template;
	packed_datetime when = { .time = { .hour = CLOCK_HOURS_PER_DAY }, .date = date };
	int16_t offset;

	if (!get_gps_utc_offset(&offset))
		offset = 0;
	if (minute_length == CLOCK_LEAP_MINUTE_SHORT)
		offset--;
	else if (minute_length == CLOCK_LEAP_MINUTE_LONG)
		offset++;
	bool forward = offset >= 0;

	if (!forward)
		offset = -offset;
	packed_datetime change = { .time = clock_time_of(seconds_to_hms(offset)), .date = 0 };

	if (forward)
		datetime_add(&change, &when);
	else
		datetime_subtract(&change, &when);
	offset_step_time = clock_time_bits(when.time);
	change = (packed_datetime){ .time = { .second = CLOCK_LEAP_EVENT_LEAD }, .date = 0 };
	datetime_subtract(&change, &when);
	event.time = clock_time_bits(when.time);
	event.date = when.date;
	if (minute_length == CLOCK_LEAP_MINUTE_SHORT)
		event.function = offset_step_down_arm;
	else if (minute_length == CLOCK_LEAP_MINUTE_LONG)
		event.function = offset_step_up_arm;
	else
		return NULL;
	return clk_do(&event);
}

/* Sets the clock's date and time, then the lifetime record's date
 * (0x10b00). */
void clock_set(time_of_day time, uint32_t date)
{
	set_timedate(date, clock_time_bits(time));
	lifetime_update(date);
}

/* Advances the clock a second and checksums its record (0x134e8). */
void clock_tick(void)
{
	clock_record_advance_utc(&clock);
	clock_record_advance(&clock);
	clock_record_checksum(&clock);
}

/* leap_event_schedule_utc's events for a 59- and a 61-second minute: the
 * step clock_record_advance adds to the GPS-UTC offset at
 * offset_step_time (0x136a2, 0x136bc). */
void offset_step_down_arm(void)
{
	offset_step = -1;
	offset_step_pending = 1;
	clock_change_armed = 1;
}

void offset_step_up_arm(void)
{
	offset_step = 1;
	offset_step_pending = 1;
	clock_change_armed = 1;
}

/* The clock's date, the T2 time code's (0x136d6). */
uint32_t get_clock_date(void)
{
	return clock.date;
}

/* The clock's time, the T2 time code's (0x136de). */
uint32_t get_clock_time(void)
{
	return clock_time_bits(clock.time);
}

/* The GPS date, with get_gps_time the T1 time code's GPS time (0x136e6). */
uint32_t get_gps_date(void)
{
	return clock.gps_date;
}

/* The GPS time (0x136ee). */
uint32_t get_gps_time(void)
{
	return clock_time_bits(clock.gps_time);
}

/* Seconds since the clock started (0x136f6). */
uint32_t get_uptime(void)
{
	return clock.uptime;
}

/* A time and its date read as one: the time, a single long read, read
 * again after the date until it has not changed, so that the 1PPS
 * interrupt's tick cannot come between them (0x1370c to 0x13718). */
static uint32_t clock_read(const time_of_day *time, const uint32_t *date, uint32_t *date_read)
{
	const volatile uint32_t *time_bits = (const volatile uint32_t *)time;
	const volatile uint32_t *date_bits = date;
	uint32_t bits;

	do {
		bits = *time_bits;
		*date_read = *date_bits;
	} while (bits != *time_bits);
	return bits;
}

/* The clock's date and time through pointers, either NULL to skip it; 1
 * unless clock.invalid (0x136fe).  :PTIM:DATE? passes the first,
 * :PTIM:TIME? the second. */
int32_t get_date_time(uint32_t *date, time_of_day *time)
{
	uint32_t date_read;
	uint32_t time_read = clock_read(&clock.time, &clock.date, &date_read);

	if (date)
		*date = date_read;
	if (time)
		*time = clock_time_of(time_read);
	return clock.invalid == 0;
}

/* The same of the GPS date and time (0x13740). */
bool get_gps_datetime(uint32_t *date, time_of_day *time)
{
	uint32_t date_read;
	uint32_t time_read = clock_read(&clock.gps_time, &clock.gps_date, &date_read);

	if (date)
		*date = date_read;
	if (time)
		*time = clock_time_of(time_read);
	return clock.invalid == 0;
}

/* clock.invalid: nonzero while the clock has no date or time; the time
 * code's v is 0 while it is 0 (0x1399a). */
uint8_t time_valid(void)
{
	return clock.invalid;
}

/* Whether the GPS-UTC offset is valid, and the offset through a pointer,
 * NULL to skip it (0x139ce). */
uint8_t get_gps_utc_offset(int16_t *seconds)
{
	if (seconds)
		*seconds = clock.gps_utc_offset;
	return clock.gps_utc_offset_valid;
}

/* Clears the exception hook and its per-task switch (0x118a8). */
void exception_hook_reset(void)
{
	exception_hook_per_task = 0;
	exception_hook = NULL;
}

/* An exception's name, from its frame's format and vector word: the
 * vector's offset, 4 times its number, indexes exception_names as bytes
 * from vector 2's, the first named, unchecked below it and unaligned as
 * the image's is (0x11a2a). */
char *exception_name(uint16_t vector)
{
	uint16_t offset = vector & CLOCK_VECTOR_OFFSET_MASK;

	if (offset > CLOCK_VECTOR_OFFSET_LAST)
		return "Oooh! That tickles. I don't know that vector.";
	return *(char **)((char *)exception_names + offset - CLOCK_VECTOR_OFFSET_FIRST_NAMED);
}

/* Records an exception in last_exception, with the running task's name
 * while the kernel runs; prints it while debug_enabled; and stops with the
 * interrupts masked (0x11942).  The fault address is kept and printed only
 * for a frame whose format is not 0. */
void print_exception(uint16_t sr, uint32_t pc, uint16_t vector, uint32_t address)
{
	bool has_address = (vector & CLOCK_VECTOR_FORMAT_MASK) != 0;

	last_exception.pending = 1;
	last_exception.name[0] = 0;
	last_exception.sr = sr;
	last_exception.pc = pc;
	last_exception.vector = vector;
	last_exception.address = has_address ? address : 0;
	if (kernel_running) {
		const char *name = task_name((void *)psos_my_pid());
		char *to = last_exception.name;

		while ((*to++ = *name++) != 0)
			;
	}
	if (debug_enabled) {
		printf_polled("\r\n");
		printf_polled("%s: %s", last_exception.name, exception_name(vector));
		printf_polled(": PC = %#8x, SR = %#04x", pc, (uint32_t)sr);
		if (has_address)
			printf_polled(", address = %#8x", address);
		printf_polled((char *)reset_to_recover_text);
		if (kernel_running)
			psos_pause(1);
	}
	set_interrupt_mask(CLOCK_SR_INTERRUPTS_MASKED);
	for (;;)
		;
}
