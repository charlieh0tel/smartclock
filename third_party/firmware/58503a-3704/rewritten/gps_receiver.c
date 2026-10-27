/* The GPS receiver's state: its channels and satellites, the receiver
 * status the loop and the screen report, the command and status
 * monitors, and the position and survey the SCPI handlers read, written
 * by hand in place of the decompiler's (docs/firmware/README.md,
 * "Hand-written C"; docs/firmware/modules.md, `applications/gps/receiver`).
 * The exported C includes this file after its own, so the types and
 * prototypes it declares are in scope; each function here keeps its
 * generated prototype.  The records are docs/firmware/structures.md's
 * `gps` and the Oncore's messages docs/firmware/gps.md's. */

/* A fraction of a validation's seconds, as the image computes it in
 * doubles, truncated. */
static uint32_t gps_receiver_validation_mark(uint32_t seconds,
                                             double fraction) {
  return double_to_unsigned(
      double_multiply(unsigned_to_double(seconds), fraction));
}

/* Starts the status monitor watching @@Ba afresh. */
static void gps_receiver_status_watch(void) {
  /* a time no @@Ba has, so that the next is new */
  static const uint32_t NO_TIME = 0xffffff;

  gps.status_last_time = NO_TIME;
  gps.status_repeat_count = 0;
  status_message_arrived = 0;
  gps.status_monitor_time = get_uptime();
  traim_message_arrived = 0;
}

/* The status monitor's step, while enabled in fix mode with @@Ba
 * requested: @@Ba must come at most 15 times its rate apart, with its
 * time advancing, and @@Bn within seven new times, else it requests
 * the outputs again, failing on a second miss (0x4be60 to 0x4c028). */
static void gps_receiver_status_monitor(void) {
  static const uint32_t RATE_PERIODS = 15;
  static const uint8_t MOST_WITHOUT_TRAIM = 7;
  static const uint8_t MOST_REPEATS = 45;
  bool watching = gps.status_monitor_enabled == 1 &&
                  gps.fix_mode == ONCORE_POSITION_FIX && gps.status_rate != 0;
  int32_t elapsed;

  switch (gps.status_monitor_state) {
    case GPS_STATUS_MONITOR_DISABLED:
      if (watching) {
        gps_receiver_status_watch();
        gps.status_monitor_state = GPS_STATUS_MONITOR_STANDBY;
      }
      break;
    case GPS_STATUS_MONITOR_STANDBY: {
      if (!watching) {
        gps.status_monitor_state = GPS_STATUS_MONITOR_DISABLED;
        break;
      }
      bool reached = uptime_elapsed(&gps.status_monitor_time, &elapsed);

      if ((reached && (uint32_t)elapsed > gps.status_rate * RATE_PERIODS) ||
          status_without_traim_count >= MOST_WITHOUT_TRAIM ||
          gps.status_repeat_count >= MOST_REPEATS) {
        if (gps.status_monitor_timed_out != 0) {
          gps.status_monitor_failed = 1;
          gps_counts.status_monitor_failures++;
          gps.status_monitor_timed_out = 0;
          gps.status_received = 0;
          gps.visibility_received = 0;
          gps.raim_received = 0;
        }
        gps_request_post(GPS_SENDER_STATUS_MONITOR, GPS_SCRIPT_OUTPUTS_ON, NULL,
                         1);
        gps.status_monitor_state = GPS_STATUS_MONITOR_RECOVERY;
        break;
      }
      if (status_message_arrived == 0 && traim_message_arrived == 0) {
        if (!reached) {
          status_message_arrived = 0;
          gps.status_monitor_time = get_uptime();
        }
        break;
      }
      if (status_message_arrived != 0) {
        gps.status_monitor_failed = 0;
        gps.status_received = 1;
        status_message_arrived = 0;
        gps.status_monitor_time = get_uptime();
        gps.status_monitor_timed_out = 0;
        uint32_t time = clock_time_bits(gps.status.time);

        if (time == gps.status_last_time) {
          gps.status_repeat_count++;
        } else {
          gps.status_repeat_count = 0;
          gps.status_last_time = time;
          status_without_traim_count =
              gps.raim.rate == 0 ? 0 : status_without_traim_count + 1;
        }
      }
      if (traim_message_arrived != 0) {
        gps.raim_received = 1;
        traim_message_arrived = 0;
        status_without_traim_count = 0;
      }
      break;
    }
    case GPS_STATUS_MONITOR_RECOVERY:
      if (status_monitor_result != GPS_RESULT_PENDING) {
        gps.status_monitor_timed_out = 1;
        gps_receiver_status_watch();
        status_without_traim_count = 0;
        gps.status_monitor_state = GPS_STATUS_MONITOR_STANDBY;
      }
      break;
    default:
      break;
  }
}

/* The number of GPS satellites @@Bb predicts visible, at most twelve,
 * and their set if asked; false before @@Bb has come (0x5026c). */
bool visible_satellites(uint8_t* count, uint32_t* set) {
  static const uint8_t MOST_VISIBLE =
      sizeof gps.visibility.satellites / sizeof gps.visibility.satellites[0];

  if (gps.visibility_received == 0) {
    return false;
  }
  uint32_t visible = 0;
  uint8_t listed =
      gps.visibility.count > MOST_VISIBLE ? MOST_VISIBLE : gps.visibility.count;

  for (uint8_t index = 0; index < listed; index++) {
    uint8_t satellite = gps.visibility.satellites[index].satellite;

    if (satellite_id_valid(SATELLITE_GPS, satellite)) {
      satellite_add_to_set(SATELLITE_GPS, satellite, &visible);
    }
  }
  *count = count_bits(visible);
  if (set != NULL) {
    *set = visible;
  }
  return true;
}

/* Whether the position may be set: neither held nor being surveyed
 * (0x50410). */
bool position_settable(void) {
  return gps.position_hold == 0 && gps.surveying == 0;
}

/* Whether a survey of finite duration is running, without position
 * hold. */
static bool gps_receiver_surveying(void) {
  return gps.position_hold == 0 && nv_settings.douglas_duration_infinite == 0;
}

/* The positions the survey has averaged, at most its duration. */
static uint32_t gps_receiver_survey_count(void) {
  return gps.average_count < (uint32_t)nv_settings.douglas_duration
             ? gps.average_count
             : nv_settings.douglas_duration;
}

/* The percentage of the survey's duration a count of positions is. */
static float gps_receiver_survey_percent(uint32_t count) {
  return float_multiply(
      float_divide(unsigned_to_float(count),
                   integer_to_float(nv_settings.douglas_duration)),
      GPS_SURVEY_PERCENT);
}
