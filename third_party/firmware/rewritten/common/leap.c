/* The leap second: its klok events, the 59- and 61-second minutes and
 * the GPS-UTC offset's step, written by hand in place of the
 * decompiler's (docs/firmware/README.md, "Hand-written C";
 * docs/firmware/modules.md, `services/leap`).  The exported C includes
 * this file after its own, so the types and prototypes it declares are
 * in scope; each function here keeps its generated prototype. */

static const uint8_t CLOCK_LEAP_MINUTE_LONG = 60 + 1;

/* Queues the clock event for a 59- or 61-second minute in UTC: at 24:00:00
 * of the leap date moved by the GPS-UTC offset as it will be, which
 * offset_step_time keeps, less 5 seconds; NULL for any other minute. */
klok_entry* leap_event_schedule_utc(uint32_t date, int32_t minute_length) {
  /* The event comes this many seconds before the offset steps
   * (leap_event_schedule_utc+0xb4). */
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

/* Cancels the leap minute's clock event, if one is queued. */
void leap_event_delete(void) {
  klok_entry* entry = klok_find(KLOK_EVENT_LEAP);

  if (entry != NULL && klok_cancel(entry) != 0) {
    task_debug_message("Error in deleting old leapsecond event\r\n");
  }
}

/* Makes the clock's current minute minute_length seconds long, in
 * supervisor mode, and arms the change (leap_minute_61_arm+0x4). */
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
 * (leap_minute_59_arm, leap_minute_61_arm). */
void leap_minute_59_arm(void) { leap_minute_arm(CLOCK_LEAP_MINUTE_SHORT); }

void leap_minute_61_arm(void) { leap_minute_arm(CLOCK_LEAP_MINUTE_LONG); }

/* leap_event_schedule_utc's events for a 59- and a 61-second minute: the
 * step clock_record_advance adds to the GPS-UTC offset at
 * offset_step_time (offset_step_down_arm, offset_step_up_arm). */
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
