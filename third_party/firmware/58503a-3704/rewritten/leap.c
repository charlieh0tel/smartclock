/* The leap second: its klok events, the 59- and 61-second minutes and
 * the GPS-UTC offset's step, written by hand in place of the
 * decompiler's (docs/firmware/README.md, "Hand-written C";
 * docs/firmware/modules.md, `services/leap`).  The exported C includes
 * this file after its own, so the types and prototypes it declares are
 * in scope; each function here keeps its generated prototype. */

static const uint8_t CLOCK_MARCH = 3;

static const uint8_t CLOCK_JUNE = 6;

static const uint8_t CLOCK_SEPTEMBER = 9;

static const uint8_t CLOCK_DECEMBER = 12;

static const uint8_t CLOCK_LEAP_MINUTE_LONG = 60 + 1;

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

/* Queues the clock event for a 59- or 61-second minute in UTC: at 24:00:00
 * of the leap date moved by the GPS-UTC offset as it will be, which
 * offset_step_time keeps, less 5 seconds; NULL for any other minute
 * (0x10656). */
klok_entry* leap_event_schedule_utc(uint32_t date, int32_t minute_length) {
  /* The event comes this many seconds before the offset steps
   * (0x1070a). */
  static const uint8_t LEAP_EVENT_LEAD = 5;
  klok_entry event = {.event_class = KLOK_EVENT_LEAP};
  packed_datetime when = {.time = {.hour = CLOCK_HOURS_PER_DAY}, .date = date};
  int16_t offset;

  if (!get_gps_utc_offset(&offset)) {
    offset = 0;
  }
  if (minute_length == CLOCK_LEAP_MINUTE_SHORT) {
    offset--;
  } else if (minute_length == CLOCK_LEAP_MINUTE_LONG) {
    offset++;
  }
  bool forward = offset >= 0;

  if (!forward) {
    offset = -offset;
  }
  packed_datetime change = {.time = clock_time_of(seconds_to_hms(offset)),
                            .date = 0};

  if (forward) {
    datetime_add(&change, &when);
  } else {
    datetime_subtract(&change, &when);
  }
  offset_step_time = clock_time_bits(when.time);
  change = (packed_datetime){.time = {.second = LEAP_EVENT_LEAD}, .date = 0};
  datetime_subtract(&change, &when);
  event.time = clock_time_bits(when.time);
  event.date = when.date;
  if (minute_length == CLOCK_LEAP_MINUTE_SHORT) {
    event.function = offset_step_down_arm;
  } else if (minute_length == CLOCK_LEAP_MINUTE_LONG) {
    event.function = offset_step_up_arm;
  } else {
    return NULL;
  }
  return clk_do(&event);
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

/* Cancels the leap minute's clock event, if one is queued (0x4658c). */
void leap_event_delete(void) {
  klok_entry* entry = klok_find(KLOK_EVENT_LEAP);

  if (entry != NULL && klok_cancel(entry) != 0) {
    task_debug_message("Error in deleting old leapsecond event\r\n");
  }
}

/* Makes the clock's current minute minute_length seconds long, in
 * supervisor mode, and arms the change (0x1397e). */
static void leap_minute_arm(uint8_t minute_length) {
  uint16_t sr;

  clock.minute_length = minute_length;
  leap_minute_pending = 1;
  sr = cpu_enter_supervisor();
  clock_record_checksum(&clock);
  cpu_sr_write(sr);
  clock_change_armed = 1;
}

/* leap_event_schedule_gps's events for a 59- and a 61-second minute
 * (0x13974, 0x1397a). */
void leap_minute_59_arm(void) { leap_minute_arm(CLOCK_LEAP_MINUTE_SHORT); }

void leap_minute_61_arm(void) { leap_minute_arm(CLOCK_LEAP_MINUTE_LONG); }

/* leap_event_schedule_utc's events for a 59- and a 61-second minute: the
 * step clock_record_advance adds to the GPS-UTC offset at
 * offset_step_time (0x139b0, 0x139ca). */
void offset_step_down_arm(void) {
  offset_step = -1;
  offset_step_pending = 1;
  clock_change_armed = 1;
}

void offset_step_up_arm(void) {
  offset_step = 1;
  offset_step_pending = 1;
  clock_change_armed = 1;
}
