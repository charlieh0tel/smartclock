/* The clock record, written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"; docs/firmware/modules.md,
 * `services/clock`).  The exported C includes this file after its own,
 * so the types and prototypes it declares are in scope; each function
 * here keeps its generated prototype.  It is the record's
 * initialization, its tick and its readers and setters
 * (docs/firmware/structures.md, "The clock record"). */

/* Sets the clock's date and time, then the lifetime record's date. */
void clock_set(time_of_day time, uint32_t date) {
  set_timedate(date, clock_time_bits(time));
  lifetime_update(date);
}

/* Sets the GPS-UTC offset, in seconds, and marks it valid, in
 * supervisor mode. */
void set_gps_utc_offset(uint16_t seconds) {
  uint16_t sr = cpu_enter_supervisor();

  clock.gps_utc_offset = seconds;
  clock.gps_utc_offset_valid = 1;
  clock_record_checksum(&clock);
  cpu_sr_write(sr);
}

/* Advances the clock a second and checksums its record. */
void clock_tick(void) {
  clock_record_advance_utc(&clock);
  clock_record_advance(&clock);
  clock_record_checksum(&clock);
}

/* The clock's date, the T2 time code's. */
uint32_t get_clock_date(void) { return clock.date; }

/* The clock's time, the T2 time code's. */
uint32_t get_clock_time(void) { return clock_time_bits(clock.time); }

/* The GPS date, with get_gps_time the T1 time code's GPS time. */
uint32_t get_gps_date(void) { return clock.gps_date; }

/* The GPS time. */
uint32_t get_gps_time(void) { return clock_time_bits(clock.gps_time); }

/* Seconds since the clock started. */
uint32_t get_uptime(void) { return clock.uptime; }

/* Whether the uptime has reached a start, and through elapsed the
 * seconds since it; a start in the future, which a restarted clock
 * leaves, is moved to now.  The GPS task's timers use it. */
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
 * interrupt's tick cannot come between them (get_date_time+0xe to
 * get_date_time+0x1a). */
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
 * unless clock.set.  :PTIM:DATE? passes the first,
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

/* The same of the GPS date and time. */
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
 * power-up: clock.set, which set_timedate sets and clock_init clears. */
uint8_t time_valid(void) { return clock.set; }

/* Whether the GPS-UTC offset is valid, and the offset through a pointer,
 * NULL to skip it. */
uint8_t get_gps_utc_offset(int16_t* seconds) {
  if (seconds) {
    *seconds = clock.gps_utc_offset;
  }
  return clock.gps_utc_offset_valid;
}
