/* The GPS receiver's state: its channels and satellites, the receiver
 * status the loop and the screen report, the command and status
 * monitors, and the position and survey the SCPI handlers read, written
 * by hand in place of the decompiler's (docs/firmware/README.md,
 * "Hand-written C"; docs/firmware/modules.md, `applications/gps/receiver`).
 * The exported C includes this file after its own, so the types and
 * prototypes it declares are in scope; each function here keeps its
 * generated prototype.  The records are docs/firmware/structures.md's
 * `gps` and the Oncore's messages docs/firmware/gps.md's. */

/* The Oncore's channels, @@Ba's six, numbered from 1. */
static const uint8_t GPS_RECEIVER_CHANNELS =
    sizeof gps.status.channels / sizeof gps.status.channels[0];

/* Sets the receiver status to not tracking, as after a reset, and the
 * channels to code search (0x4b0d0). */
void gps_receiver_status_reset(void) {
  gps.status_code = GPS_SYNC_NOT_TRACKING;
  gps.status_code_reset = GPS_SYNC_NOT_TRACKING;
  uint16_t sr = set_interrupt_mask(CPU_IPL_1);

  gps.reference_qualified = 0;
  gps.status_reported =
      pll.field_3d5 == 0 ? gps.status_code : gps.status_code_reset;
  set_interrupt_mask(sr);
  channels_reset();
}

/* The satellite a channel, 1 to 6, tracks, when @@Ba has come and the
 * channel's satellite is available for position; else 0 (0x4f0d6). */
uint8_t channel_position_satellite(uint8_t channel) {
  oncore_channel_status* status = &gps.status.channels[channel - 1];

  if (gps.status_received != 0 &&
      (satellite_id_valid(SATELLITE_GPS, status->satellite) ||
       satellite_id_valid(SATELLITE_PSEUDOLITE, status->satellite)) &&
      status->mode == ONCORE_AVAILABLE_FOR_POSITION) {
    return status->satellite;
  }
  return 0;
}

/* Whether a channel uses the satellite for position (0x4f120). */
uint32_t satellite_used_for_position(uint8_t satellite) {
  if (satellite != 0) {
    for (uint8_t channel = 1; channel <= GPS_RECEIVER_CHANNELS; channel++) {
      if (channel_position_satellite(channel) == satellite) {
        return 1;
      }
    }
  }
  return 0;
}

/* Sets every channel to code search (0x4f14a). */
void channels_reset(void) {
  for (uint8_t channel = 0; channel < GPS_RECEIVER_CHANNELS; channel++) {
    gps.status.channels[channel].mode = ONCORE_CODE_SEARCH;
  }
}

/* A fraction of a validation's seconds, as the image computes it in
 * doubles, truncated. */
static uint32_t gps_receiver_validation_mark(uint32_t seconds,
                                             double fraction) {
  return double_to_unsigned(
      double_multiply(unsigned_to_double(seconds), fraction));
}

/* Assesses Time RAIM from @@Bn, while it is on and its status known:
 * not OK first raises an alarm detected, at raim_suspect_time, and not
 * OK for RAIM_ALARM_SECONDS an alarm; OK clears either (0x4afdc). */
void raim_monitor(void) {
  static const uint32_t RAIM_ALARM_SECONDS = 4;
  const oncore_raim_fields* raim = &gps.raim;
  int32_t elapsed;

  if (raim->raim_on != 1 || gps.raim_received == 0 ||
      raim->raim_status == ONCORE_RAIM_NEITHER ||
      raim->solution_status == ONCORE_RAIM_UNKNOWN) {
    gps.raim_assessment = GPS_RAIM_INACTIVE;
    return;
  }
  switch (gps.raim_assessment) {
    case GPS_RAIM_INACTIVE:
      if (raim->solution_status == ONCORE_RAIM_OK) {
        gps.raim_assessment = GPS_RAIM_OK;
      } else {
        gps.raim_assessment = GPS_RAIM_ALARM_DETECTED;
        raim_suspect_time = get_uptime();
        gps_counts.raim_detected++;
      }
      break;
    case GPS_RAIM_OK:
      if (raim->solution_status == ONCORE_RAIM_ALARM) {
        gps.raim_assessment = GPS_RAIM_ALARM_DETECTED;
        raim_suspect_time = get_uptime();
        gps_counts.raim_detected++;
      }
      break;
    case GPS_RAIM_ALARM_DETECTED:
      if (raim->solution_status == ONCORE_RAIM_OK) {
        gps.raim_assessment = GPS_RAIM_OK;
      } else if (!uptime_elapsed(&raim_suspect_time, &elapsed)) {
        raim_suspect_time = get_uptime();
      } else if ((uint32_t)elapsed >= RAIM_ALARM_SECONDS) {
        gps.raim_assessment = GPS_RAIM_ALARM;
        gps_counts.raim_alarms++;
      }
      break;
    case GPS_RAIM_ALARM:
      if (raim->solution_status == ONCORE_RAIM_OK) {
        gps.raim_assessment = GPS_RAIM_OK;
      }
      break;
    default:
      gps.raim_assessment = GPS_RAIM_INACTIVE;
      break;
  }
}

/* Updates the receiver status (gps_sync_status) and reference_valid
 * from the monitors, the channels, position hold and Time RAIM: once
 * qualified, 30 s of assessment before the reference is valid; a dead
 * 1PPS makes it absent.  Only the Oncore's, receiver 0; the alternate
 * receiver's status, gps_status_alternate's, it takes and leaves
 * (0x4b126). */
void gps_receiver_status_update(gps_receiver_number receiver,
                                uint8_t alternate_status) {
  static const uint32_t VALIDATION_SECONDS = 30;

  if (receiver != GPS_RECEIVER_ONCORE) {
    return;
  }
  bool no_status = gps.status_received == 0 || gps.status_monitor_failed != 0;
  bool position_satellite = false;

  if (gps.fix_mode == ONCORE_POSITION_FIX) {
    for (uint8_t channel = 1;
         !position_satellite && channel <= GPS_RECEIVER_CHANNELS; channel++) {
      position_satellite = channel_position_satellite(channel) != 0;
    }
  }
  bool holding = gps.position_hold == 1 || gps.surveying == 1;

  raim_monitor();
  gps.pps_dead = gps_pps_dead != 0 &&
                 (pll.field_3d5 != 0 || gps.fix_mode == ONCORE_POSITION_FIX);

  bool valid = false;

  if (no_status || !position_satellite || !holding ||
      gps.raim_assessment == GPS_RAIM_ALARM) {
    if (gps.status_monitor_failed != 0) {
      gps.status_code = GPS_SYNC_RECEIVER_ERROR;
    } else if (position_satellite && gps.status_received != 0) {
      gps.status_code =
          holding ? GPS_SYNC_RAIM_ERROR : GPS_SYNC_INACCURATE_POSITION;
    } else {
      gps.status_code = GPS_SYNC_NOT_TRACKING;
    }
  } else {
    switch (gps.status_code) {
      case GPS_SYNC_RECEIVER_ERROR:
      case GPS_SYNC_ABSENT:
      case GPS_SYNC_NOT_TRACKING:
      case GPS_SYNC_INACCURATE_POSITION:
      case GPS_SYNC_RAIM_ERROR:
        gps.validation_start = get_uptime();
        gps.status_code = GPS_SYNC_ASSESSING;
        break;
      case GPS_SYNC_ASSESSING:
      case GPS_SYNC_ASSESSING_27:
      case GPS_SYNC_ASSESSING_53:
      case GPS_SYNC_ASSESSING_77: {
        int32_t elapsed;

        if (!uptime_elapsed(&gps.validation_start, &elapsed)) {
          gps.status_code = GPS_SYNC_ASSESSING;
          break;
        }
        uint32_t seconds = elapsed;

        if (seconds >= VALIDATION_SECONDS) {
          valid = true;
          gps.status_code = GPS_SYNC_SYNCHRONIZED;
        } else if (seconds >=
                   gps_receiver_validation_mark(VALIDATION_SECONDS, 0.77)) {
          gps.status_code = GPS_SYNC_ASSESSING_77;
        } else if (seconds >=
                   gps_receiver_validation_mark(VALIDATION_SECONDS, 0.53)) {
          gps.status_code = GPS_SYNC_ASSESSING_53;
        } else if (seconds >=
                   gps_receiver_validation_mark(VALIDATION_SECONDS, 0.27)) {
          gps.status_code = GPS_SYNC_ASSESSING_27;
        }
        break;
      }
      case GPS_SYNC_SYNCHRONIZED:
        valid = true;
        break;
      default:
        gps.status_code = GPS_SYNC_NOT_TRACKING;
        break;
    }
  }

  gps.reference_valid_override = reference_valid_override_request;
  if (reference_valid_override_request == GPS_REFERENCE_OVERRIDE_VALID) {
    valid = true;
  } else if (reference_valid_override_request ==
             GPS_REFERENCE_OVERRIDE_INVALID) {
    valid = false;
  }
  uint16_t sr = set_interrupt_mask(CPU_IPL_1);

  if (gps.status_code < GPS_SYNC_ASSESSING ||
      gps.status_code > GPS_SYNC_ASSESSING_77) {
    gps.status_reported = gps.status_code;
  }
  gps.reference_valid = valid;
  if (gps.pps_dead != 0) {
    gps.reference_valid = 0;
    gps.status_code = GPS_SYNC_ABSENT;
  }
  gps.reference_qualified = valid;
  set_interrupt_mask(sr);
}

/* Clears the monitors' failures, reports the GPS failure cleared, and
 * enables the command monitor at the head of its query list (0x4b3e4). */
void command_monitor_init(void) {
  gps.command_monitor_state = GPS_COMMAND_MONITOR_DISABLED;
  gps.status_monitor_state = GPS_STATUS_MONITOR_DISABLED;
  gps.receiver_health_failed = 0;
  gps.command_monitor_failed = 0;
  gps.status_monitor_failed = 0;
  status_without_traim_count = 0;
  status_happening(STATUS_GPS_FAILURE_CLEARED);
  gps.command_monitor_enabled = 1;
  gps.command_monitor_error = 0;
  gps.command_monitor_query = command_monitor_queries;
  gps.status_monitor_enabled = 0;
}

/* The command monitor's step: on a command gone unanswered it fails
 * and, every 20 s while the error stands, posts its next query, moving
 * along command_monitor_queries when one is answered, rejected or timed out
 * (0x4b47a to 0x4b57a). */
static void gps_receiver_command_monitor(void) {
  static const uint32_t QUERY_INTERVAL = 20; /* s */
  int32_t elapsed;

  switch (gps.command_monitor_state) {
    case GPS_COMMAND_MONITOR_DISABLED:
      if (gps.command_monitor_enabled == 1) {
        gps.command_monitor_state = GPS_COMMAND_MONITOR_STANDBY;
      }
      break;
    case GPS_COMMAND_MONITOR_STANDBY:
      if (gps.command_monitor_enabled == 0) {
        gps.command_monitor_state = GPS_COMMAND_MONITOR_DISABLED;
      } else if (gps.command_monitor_error != 0) {
        gps.command_monitor_failed = 1;
        gps_counts.command_monitor_failures++;
        gps.command_monitor_time = get_uptime();
        gps.command_monitor_state = GPS_COMMAND_MONITOR_QUERY;
      }
      break;
    case GPS_COMMAND_MONITOR_QUERY:
      if (gps.command_monitor_error == 0) {
        gps.command_monitor_failed = 0;
        gps.command_monitor_state = GPS_COMMAND_MONITOR_STANDBY;
      } else if (gps.command_monitor_enabled == 0) {
        gps.command_monitor_state = GPS_COMMAND_MONITOR_DISABLED;
      } else if (uptime_elapsed(&gps.command_monitor_time, &elapsed) &&
                 (uint32_t)elapsed > QUERY_INTERVAL) {
        /* Its query's place in the list, whose long is the step's
         * address. */
        gps_request_post(GPS_SENDER_COMMAND_MONITOR, GPS_POST_STEP,
                         (int32_t*)gps.command_monitor_query, 0);
        gps.command_monitor_state = GPS_COMMAND_MONITOR_QUERY_ACK;
      }
      break;
    case GPS_COMMAND_MONITOR_QUERY_ACK:
      if (command_monitor_result == GPS_RESULT_PENDING) {
        if (gps.command_monitor_error == 0) {
          gps.command_monitor_failed = 0;
        }
        break;
      }
      switch (command_monitor_result) {
        case GPS_RESULT_SUCCESS:
        case GPS_RESULT_REJECT:
        case GPS_RESULT_TIMEOUT: {
          gps_script_step** query = gps.command_monitor_query;

          query++;
          if (*query == NULL) {
            query = command_monitor_queries;
          }
          gps.command_monitor_query = query;
          break;
        }
        default:
          break;
      }
      gps.command_monitor_time = get_uptime();
      gps.command_monitor_state = GPS_COMMAND_MONITOR_QUERY;
      break;
    default:
      break;
  }
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
 * the outputs again, failing on a second miss (0x4b57e to 0x4b70e). */
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

/* Runs the command and status monitors and reports the receiver's
 * health, failed when either has failed (0x4b42c). */
void command_monitor(void) {
  gps_receiver_command_monitor();
  gps_receiver_status_monitor();
  gps.receiver_health_failed =
      gps.command_monitor_failed != 0 || gps.status_monitor_failed != 0;
  status_happening(gps.receiver_health_failed != 0
                       ? STATUS_GPS_FAILURE_SET
                       : STATUS_GPS_FAILURE_CLEARED);
}

/* The sets of GPS satellites and pseudolites the channels use for
 * position, and their counts, each set if asked; false before @@Ba has
 * come (0x4f24c). */
bool satellites_in_use(uint8_t* count, uint8_t* pseudolite_count, uint32_t* set,
                       uint8_t* pseudolite_set) {
  uint32_t used = 0;
  uint8_t pseudolites_used = 0;

  if (gps.status_received == 0) {
    return false;
  }
  for (uint8_t channel = 1; channel <= GPS_RECEIVER_CHANNELS; channel++) {
    const uint8_t satellite = channel_position_satellite(channel);

    if (satellite == 0) {
      continue;
    }
    if (satellite > GPS_LAST_SATELLITE) {
      satellite_add_to_set(SATELLITE_PSEUDOLITE, satellite, &pseudolites_used);
    } else {
      satellite_add_to_set(SATELLITE_GPS, satellite, &used);
    }
  }
  *count = count_bits(used);
  *pseudolite_count = count_bits(pseudolites_used);
  if (set != NULL) {
    *set = used;
  }
  if (pseudolite_set != NULL) {
    *pseudolite_set = pseudolites_used;
  }
  return true;
}

/* The number of GPS satellites @@Bb predicts visible, at most twelve,
 * and their set if asked; false before @@Bb has come (0x4f1b6). */
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
 * (0x4f358). */
bool position_settable(void) {
  return gps.position_hold == 0 && gps.surveying == 0;
}

/* @@Ba's latitude, longitude and height above mean sea level, once it
 * has come (0x4f370). */
bool gps_position_get(int32_t* latitude, int32_t* longitude, int32_t* height) {
  bool received = gps.status_received != 0;

  if (received) {
    *latitude = gps.status.latitude;
    *longitude = gps.status.longitude;
    *height = gps.status.height_msl;
  }
  return received;
}

/* @@Ba's time and date made local, once it has come (0x4f39c). */
bool gps_time_local_get(time_of_day* time, uint32_t* date) {
  bool received = gps.status_received != 0;

  if (received) {
    time_of_day local_time = gps.status.time;
    uint32_t local_date = oncore_date_pack(gps.status.date);

    utc_to_local(&local_date, &local_time);
    *time = local_time;
    *date = local_date;
  }
  return received;
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

/* The survey's progress in whole percent, 1 for any start and 100 only
 * once complete, when a finite survey runs (0x4f3ec).  The image
 * divides before checking for no positions. */
uint32_t survey_progress_percent(uint16_t* percent) {
  static const float ONE = 1.0f;
  static const uint16_t COMPLETE = 100;

  if (!gps_receiver_surveying()) {
    return 0;
  }
  uint32_t count = gps_receiver_survey_count();
  float progress = gps_receiver_survey_percent(count);

  if (count == 0) {
    *percent = 0;
  } else if (float_compare(ONE, progress) > 0) {
    *percent = 1;
  } else if (count == (uint32_t)nv_settings.douglas_duration) {
    *percent = COMPLETE;
  } else {
    *percent = float_to_integer(progress);
  }
  return 1;
}

/* The survey's progress as text, "%+.1f", at least 0.1 once started and
 * at most 99.9 until complete, when a finite survey runs; else empty
 * (0x4f488). */
bool survey_progress_text(char* text) {
  if (!gps_receiver_surveying()) {
    *text = 0;
    return false;
  }
  uint32_t count = gps_receiver_survey_count();
  float progress = 0.0f;

  if (count != 0) {
    progress = gps_receiver_survey_percent(count);
    if (float_compare(GPS_SURVEY_LEAST, progress) > 0) {
      progress = GPS_SURVEY_LEAST;
    } else if (float_compare(progress, GPS_SURVEY_MOST_UNFINISHED) > 0 &&
               count < (uint32_t)nv_settings.douglas_duration) {
      progress = GPS_SURVEY_MOST_UNFINISHED;
    }
  }
  sprintf(text, "%+.1f", float_to_double(progress));
  return true;
}
