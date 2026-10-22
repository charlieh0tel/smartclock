/* The clock record, written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"; docs/firmware/modules.md,
 * `services/clock`).  The exported C includes this file after its own,
 * so the types and prototypes it declares are in scope; each function
 * here keeps its generated prototype.  It is the record's
 * initialization, its tick and its readers and setters
 * (docs/firmware/structures.md, "The clock record"). */

/* Whether the clock record's checksum is good, as clock_record_sum
 * (0x134ca) leaves Z, which its bridge cannot return: the byte sum of
 * +0x02 to +0x32 and its complement. */
static bool clock_record_valid(const clock_record* record) {
  const uint8_t* byte = (const uint8_t*)&record->uptime;
  const uint8_t* end = (const uint8_t*)&record->gps_utc_offset_valid + 1;
  uint8_t sum = 0;

  while (byte < end) {
    sum += *byte++;
  }
  return record->checksum ==
         (uint16_t)((uint8_t)~sum << CLOCK_FIELD_BITS | sum);
}

/* Starts the clock afresh, when forced or its record's checksum fails,
 * at no time on the lifetime record's date; either way marks it not
 * set, so that time_valid gives 0, and clears the pending leap minute
 * and offset step (0x13908).
 * system_init passes selftest.eeprom. */
void clock_init(int16_t force) {
  /* Days a month, January first, February's 0 for clock_set_leap_year
   * (the immediates at 0x13922). */
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

/* Sets the clock's date and time, then the lifetime record's date
 * (0x10b00). */
void clock_set(time_of_day time, uint32_t date) {
  set_timedate(date, clock_time_bits(time));
  lifetime_update(date);
}

/* Derives the clock's local date and time from its GPS time again and
 * checksums its record, in supervisor mode (0x13782). */
void clock_record_refresh(void) {
  uint16_t sr = enter_supervisor();

  clock_record_derive(&clock);
  clock_record_checksum(&clock);
  cpu_sr_write(sr);
}

/* Sets the GPS-UTC offset, in seconds, and marks it valid, in
 * supervisor mode (0x139a2). */
void set_gps_utc_offset(uint16_t seconds) {
  uint16_t sr = enter_supervisor();

  clock.gps_utc_offset = seconds;
  clock.gps_utc_offset_valid = 1;
  clock_record_checksum(&clock);
  cpu_sr_write(sr);
}

/* Advances the clock a second and checksums its record (0x134e8). */
void clock_tick(void) {
  clock_record_advance_utc(&clock);
  clock_record_advance(&clock);
  clock_record_checksum(&clock);
}

/* The clock's date, the T2 time code's (0x136d6). */
uint32_t get_clock_date(void) { return clock.date; }

/* The clock's time, the T2 time code's (0x136de). */
uint32_t get_clock_time(void) { return clock_time_bits(clock.time); }

/* The GPS date, with get_gps_time the T1 time code's GPS time (0x136e6). */
uint32_t get_gps_date(void) { return clock.gps_date; }

/* The GPS time (0x136ee). */
uint32_t get_gps_time(void) { return clock_time_bits(clock.gps_time); }

/* Seconds since the clock started (0x136f6). */
uint32_t get_uptime(void) { return clock.uptime; }

/* Whether the uptime has reached a start, and through elapsed the
 * seconds since it; a start in the future, which a restarted clock
 * leaves, is moved to now (0x4aff2).  The GPS task's timers use it. */
bool uptime_elapsed(uint32_t* start, int32_t* elapsed) {
  uint32_t uptime = get_uptime();
  uint32_t started = *start;

  if (started <= uptime) {
    *elapsed = uptime - started;
  } else {
    *start = uptime;
    *elapsed = 0;
  }
  return started <= uptime;
}

/* A time and its date read as one: the time, a single long read, read
 * again after the date until it has not changed, so that the 1PPS
 * interrupt's tick cannot come between them (0x1370c to 0x13718). */
static uint32_t clock_read(const time_of_day* time, const uint32_t* date,
                           uint32_t* date_read) {
  const volatile uint32_t* time_bits = (const volatile uint32_t*)time;
  const volatile uint32_t* date_bits = date;
  uint32_t bits;

  do {
    bits = *time_bits;
    *date_read = *date_bits;
  } while (bits != *time_bits);
  return bits;
}

/* The clock's date and time through pointers, either NULL to skip it; 1
 * unless clock.set (0x136fe).  :PTIM:DATE? passes the first,
 * :PTIM:TIME? the second. */
bool get_date_time(uint32_t* date, time_of_day* time) {
  uint32_t date_read;
  uint32_t time_read = clock_read(&clock.time, &clock.date, &date_read);

  if (date) {
    *date = date_read;
  }
  if (time) {
    *time = clock_time_of(time_read);
  }
  return clock.set == 0;
}

/* The same of the GPS date and time (0x13740). */
bool get_gps_datetime(uint32_t* date, time_of_day* time) {
  uint32_t date_read;
  uint32_t time_read = clock_read(&clock.gps_time, &clock.gps_date, &date_read);

  if (date) {
    *date = date_read;
  }
  if (time) {
    *time = clock_time_of(time_read);
  }
  return clock.set == 0;
}

/* Whether the clock has been set, from GPS or by the console, since
 * power-up: clock.set, which set_timedate sets and clock_init clears
 * (0x1399a). */
uint8_t time_valid(void) { return clock.set; }

/* Whether the GPS-UTC offset is valid, and the offset through a pointer,
 * NULL to skip it (0x139ce). */
uint8_t get_gps_utc_offset(int16_t* seconds) {
  if (seconds) {
    *seconds = clock.gps_utc_offset;
  }
  return clock.gps_utc_offset_valid;
}
