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

/* The satellite a channel, 1 to gps.channel_count, tracks, when @@Ba
 * has come fresh and the channel's satellite is available for position;
 * else 0 (0x50182). */
uint8_t channel_position_satellite(uint8_t channel) {
  oncore_channel_status* status = &gps.status.channels[channel - 1];

  if (gps.status_received == GPS_STATUS_FRESH &&
      (satellite_id_valid(SATELLITE_GPS, status->satellite) ||
       satellite_id_valid(SATELLITE_PSEUDOLITE, status->satellite)) &&
      status->mode == ONCORE_AVAILABLE_FOR_POSITION) {
    return status->satellite;
  }
  return 0;
}

/* Whether a channel uses the satellite for position (0x501ce). */
uint32_t satellite_used_for_position(uint8_t satellite) {
  if (satellite != 0) {
    for (uint8_t channel = 1; channel <= gps.channel_count; channel++) {
      if (channel_position_satellite(channel) == satellite) {
        return 1;
      }
    }
  }
  return 0;
}

/* Sets every channel to code search (0x501fc). */
void channels_reset(void) {
  for (uint8_t channel = 1; channel <= gps.channel_count; channel++) {
    gps.status.channels[channel - 1].mode = ONCORE_CODE_SEARCH;
  }
}

/* The GPS satellites, and the pseudolites, the channels use for
 * position, counted and as sets if asked; false before @@Ba has come
 * (0x50302). */
bool satellites_in_use(uint8_t* count, uint8_t* pseudolite_count, uint32_t* set,
                       uint8_t* pseudolite_set) {
  uint32_t used = 0;
  uint8_t pseudolites_used = 0;

  if (gps.status_received == GPS_STATUS_NONE) {
    return false;
  }
  for (uint8_t channel = 1; channel <= gps.channel_count; channel++) {
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

/* @@Ba's latitude, longitude and MSL height; false before it has come
 * (0x50428). */
bool gps_position_get(int32_t* latitude, int32_t* longitude, int32_t* height) {
  bool received = gps.status_received != GPS_STATUS_NONE;

  if (received) {
    *latitude = gps.status.latitude;
    *longitude = gps.status.longitude;
    *height = gps.status.height_msl;
  }
  return received;
}

/* @@Ba's time and date as local time; false before it has come
 * (0x50454). */
bool gps_time_local_get(time_of_day* time, uint32_t* date) {
  bool received = gps.status_received != GPS_STATUS_NONE;

  if (received) {
    time_of_day local_time = gps.status.time;
    uint32_t local_date = oncore_date_pack(gps.status.date);

    utc_to_local(&local_date, &local_time);
    *time = local_time;
    *date = local_date;
  }
  return received;
}

/* Assesses Time RAIM from @@Bn, while it is on and its status known:
 * not OK first raises an alarm detected, at raim_suspect_time, and not
 * OK for RAIM_ALARM_SECONDS an alarm; OK clears either (0x4b734). */
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

/* Sets a reference, the GPS's or the external 1PPS's, to not tracking,
 * valid only as its override forces (0x4b82a). */
void gps_reference_reset(char external) {
  gps_reference_record* reference = &gps.references[external != 0];

  reference->valid = reference->override == GPS_REFERENCE_OVERRIDE_VALID;
  reference->state = GPS_SYNC_NOT_TRACKING;
}

/* The GPS reference's questionable hysteresis, in seconds, and its
 * margin: none for none, one less for 1 or 2, else 1 (0x4b858). */
void gps_reference_hysteresis_set(int32_t hysteresis) {
  gps_reference_record* reference = &gps.references[GPS_RECEIVER_ONCORE];

  reference->hysteresis = hysteresis;
  if (hysteresis == 0) {
    reference->hysteresis_margin = 0;
  } else if ((uint32_t)hysteresis - 1 <= 1) {
    reference->hysteresis_margin = hysteresis - 1;
  } else {
    reference->hysteresis_margin = 1;
  }
}

/* The external 1PPS reference's: its margin one less for 1, else none,
 * so always 0 (0x4b882). */
void ext_reference_hysteresis_set(int32_t hysteresis) {
  gps_reference_record* reference = &gps.references[GPS_RECEIVER_ALTERNATE];

  reference->hysteresis = hysteresis;
  reference->hysteresis_margin = hysteresis == 1 ? hysteresis - 1 : 0;
}

/* The bits of a long the channels are packed into. */
static const uint8_t GPS_RECEIVER_WORD_BITS = 32;

/* A channel's signal strength in two bits, 0 to 3, by the thresholds
 * 30, 60 and 120. */
static uint8_t gps_receiver_strength_bits(uint8_t strength) {
  static const uint8_t WEAK = 30;
  static const uint8_t FAIR = 60;
  static const uint8_t GOOD = 120;

  if (strength <= WEAK) {
    return 0;
  }
  if (strength <= FAIR) {
    return 1;
  }
  return strength <= GOOD ? 2 : 3;
}

/* Packs each channel's satellite, mode and strength into gps.track_words
 * for the tracking log, three channels a long from the top, 10 bits each,
 * satellite 32 as 10, then moves the longs' unused bits out, so that the
 * channels run on as 80 bits and [2]'s low half is free (0x4da70). */
void gps_track_words_pack(void) {
  static const uint8_t SATELLITE_32 = 32;
  static const uint8_t SATELLITE_32_PACKED = 10;
  uint32_t* word = gps.track_words;
  uint8_t shift = GPS_RECEIVER_WORD_BITS;

  gps.track_words[0] = 0;
  gps.track_words[1] = 0;
  gps.track_words[2] = 0;
  for (uint8_t channel = 1; channel <= gps.channel_count; channel++) {
    const oncore_channel_status* status = &gps.status.channels[channel - 1];
    uint8_t satellite = status->satellite;
    uint8_t mode = status->mode;

    if (satellite == SATELLITE_32) {
      satellite = SATELLITE_32_PACKED;
    }
    if (mode >= ONCORE_FREQUENCY_ACQUIRE) {
      mode--;
    } else if (mode >= ONCORE_CODE_ACQUIRE) {
      mode = ONCORE_CODE_ACQUIRE;
    }
    uint8_t strength = gps_receiver_strength_bits(status->signal_strength);

    if (channel == 4 || channel == 7) {
      word++;
      shift = GPS_RECEIVER_WORD_BITS;
    }
    shift -= 5;
    *word |= (uint32_t)satellite << shift;
    shift -= 3;
    *word |= (uint32_t)mode << shift;
    shift -= 2;
    *word |= (uint32_t)strength << shift;
  }
  gps.track_words[0] |= gps.track_words[1] >> 30;
  gps.track_words[1] <<= 2;
  gps.track_words[1] |= gps.track_words[2] >> 28;
  gps.track_words[2] <<= 4;
}

/* A channel's mode in two bits: 0 searching, 1 acquiring to bit sync,
 * 2 at message sync, 3 beyond. */
static uint8_t gps_receiver_mode_bits(uint8_t mode) {
  if (mode >= ONCORE_SATELLITE_TIME_AVAILABLE) {
    return 3;
  }
  if (mode == ONCORE_MESSAGE_SYNC_DETECT) {
    return 2;
  }
  return mode >= ONCORE_CODE_ACQUIRE;
}

/* Packs each channel's mode and strength, two bits each, into
 * gps.channel_word from the top, and starts gps.channel_word_lowest, the
 * lowest mode each channel has had, at all ones (0x4db52). */
void gps_channel_word_pack(void) {
  static const uint16_t ALL_HIGH = 0xffff;
  uint32_t packed = 0;
  uint8_t shift = GPS_RECEIVER_WORD_BITS;

  for (uint8_t channel = 1; channel <= gps.channel_count; channel++) {
    const oncore_channel_status* status = &gps.status.channels[channel - 1];
    uint8_t mode = gps_receiver_mode_bits(status->mode);
    uint8_t strength = gps_receiver_strength_bits(status->signal_strength);

    shift -= 2;
    packed |= (uint32_t)mode << shift;
    shift -= 2;
    packed |= (uint32_t)strength << shift;
  }
  gps.channel_word = packed;
  gps.channel_word_lowest = ALL_HIGH;
}

/* Lowers each channel's two bits of gps.channel_word_lowest, from the
 * top of the word, to its mode's, when that is less (0x4dbe8). */
void gps_channel_word_lowest(void) {
  static const uint8_t HALF_WORD_BITS = 16;
  static const uint16_t TWO_BITS = 3;
  uint8_t shift = HALF_WORD_BITS;

  for (uint8_t channel = 1; channel <= gps.channel_count; channel++) {
    uint8_t bits =
        gps_receiver_mode_bits(gps.status.channels[channel - 1].mode);

    shift -= 2;
    if ((int32_t)(gps.channel_word_lowest >> shift & TWO_BITS) >
        (int32_t)bits) {
      gps.channel_word_lowest &= ~(TWO_BITS << shift);
      gps.channel_word_lowest |= bits << shift;
    }
  }
}

/* Logs the GPS reference losing track, from valid, assessing or
 * clearing, or resuming it, from another state, with the state it left
 * and the two channel words (0x4dc5c). */
void gps_log_track_change(char lost, char reason, uint32_t first,
                          uint32_t second) {
  char* format;

  if (lost != 0) {
    switch (reason) {
      case GPS_SYNC_VALID:
        format = "Track lost    [SY] %08X %08X";
        break;
      case GPS_SYNC_ASSESSING:
        format = "Track lost    [CK] %08X %08X";
        break;
      case GPS_SYNC_QUESTIONABLE_CLEARING:
        format = "Track lost    [QR] %08X %08X";
        break;
      default:
        format = "Track lost    [??] %08X %08X";
        break;
    }
  } else {
    switch (reason) {
      case GPS_SYNC_QUESTIONABLE:
        format = "Track resumed [QS] %08X %08X";
        break;
      case GPS_SYNC_QUESTIONABLE_CLEARING:
        format = "Track resumed [QR] %08X %08X";
        break;
      case GPS_SYNC_NOT_TRACKING:
        format = "Track resumed [NT] %08X %08X";
        break;
      case GPS_SYNC_INACCURATE_POSITION:
        format = "Track resumed [NP] %08X %08X";
        break;
      case GPS_SYNC_NO_UTC_DATA:
        format = "Track resumed [NU] %08X %08X";
        break;
      case GPS_SYNC_RAIM_ERROR:
        format = "Track resumed [TR] %08X %08X";
        break;
      case GPS_SYNC_RECEIVER_FAILED:
        format = "Track resumed [ER] %08X %08X";
        break;
      default:
        format = "Track resumed [??] %08X %08X";
        break;
    }
  }
  log_write(format, first, second);
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

/* The survey's progress in whole percent, 1 for any start and 100 only
 * once complete, when a finite survey runs (0x504a4).  The image
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
 * (0x50540). */
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

/* Starts the command monitor enabled and the status monitor disabled,
 * neither failed, and the receiver's health cleared (0x4bc9e). */
void command_monitor_init(void) {
  gps.command_monitor_state = GPS_COMMAND_MONITOR_DISABLED;
  gps.status_monitor_state = GPS_STATUS_MONITOR_DISABLED;
  gps.receiver_health_failed = 0;
  gps.command_monitor_failed = 0;
  gps.status_monitor_failed = 0;
  status_happening(STATUS_GPS_FAILURE_CLEARED);
  status_without_traim_count = 0;
  gps.command_monitor_enabled = 1;
  gps.command_monitor_error = 0;
  gps.command_monitor_query = command_monitor_queries;
  gps.status_monitor_enabled = 0;
}

/* The command monitor's step: after a command fails it queries the
 * engine every QUERY_INTERVAL seconds, a query of command_monitor_queries
 * in turn, until one is answered (0x4bd16 to 0x4be4a). */
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
      } else if (!uptime_elapsed(&gps.command_monitor_time, &elapsed)) {
        gps.command_monitor_time = get_uptime();
      } else if ((uint32_t)elapsed > QUERY_INTERVAL) {
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
  status_without_traim_count = 0;
  gps.status_monitor_state = GPS_STATUS_MONITOR_STANDBY;
}

/* The status monitor's step, while enabled with @@Ba requested: @@Ba
 * goes stale after three of its periods, and must come at most 36
 * periods apart, with its time advancing, and @@Bn within 20 new times,
 * else it requests the outputs again, failing on a second miss
 * (0x4be5c to 0x4c02a). */
static void gps_receiver_status_monitor(void) {
  static const uint32_t STALE_PERIODS = 3;
  static const uint32_t RATE_PERIODS = 36;
  static const uint8_t MOST_WITHOUT_TRAIM = 20;
  static const uint8_t MOST_REPEATS = 46;
  bool watching = gps.status_monitor_enabled == 1 && gps.status_rate != 0;
  int32_t elapsed;

  switch (gps.status_monitor_state) {
    case GPS_STATUS_MONITOR_DISABLED:
      if (watching) {
        gps_receiver_status_watch();
      }
      break;
    case GPS_STATUS_MONITOR_STANDBY: {
      if (!watching) {
        gps.status_monitor_state = GPS_STATUS_MONITOR_DISABLED;
        break;
      }
      bool reached = uptime_elapsed(&gps.status_monitor_time, &elapsed);

      if (gps.status_received == GPS_STATUS_FRESH && reached &&
          (uint32_t)elapsed > gps.status_rate * STALE_PERIODS) {
        gps.status_received = GPS_STATUS_STALE;
      }
      if ((reached && (uint32_t)elapsed > gps.status_rate * RATE_PERIODS) ||
          status_without_traim_count >= MOST_WITHOUT_TRAIM ||
          gps.status_repeat_count >= MOST_REPEATS) {
        if (gps.status_monitor_timed_out != 0) {
          gps.status_monitor_failed = 1;
          gps_counts.status_monitor_failures++;
          gps.status_monitor_timed_out = 0;
          gps.status_received = GPS_STATUS_NONE;
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
        gps.status_received = GPS_STATUS_FRESH;
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
      }
      break;
    default:
      break;
  }
}

/* The GPS task's monitors: the command monitor, the channels marked
 * searching while the engine is not fixing or @@Ba is not fresh, the
 * status monitor, and the receiver's health, failed while either
 * monitor is, reported as it changes (0x4bce6). */
void command_monitor(void) {
  gps_receiver_command_monitor();
  if (gps.pps_check_disabled != 0 || gps.fix_mode == ONCORE_IDLE ||
      gps.status_received != GPS_STATUS_FRESH) {
    channels_reset();
  }
  gps_receiver_status_monitor();
  bool failed =
      gps.command_monitor_failed != 0 || gps.status_monitor_failed != 0;

  if (failed == (gps.receiver_health_failed != 0)) {
    return;
  }
  gps.receiver_health_failed = failed;
  status_happening(failed ? STATUS_GPS_FAILURE_SET
                          : STATUS_GPS_FAILURE_CLEARED);
}

/* The receiver state a reference that is not qualified takes: failed
 * without @@Ba, not tracking without a satellite for position, else an
 * inaccurate position without position hold or a survey, else a Time
 * RAIM error. */
static gps_sync_status gps_receiver_unqualified(bool monitor_ok, bool tracking,
                                                bool held) {
  if (!monitor_ok) {
    return GPS_SYNC_RECEIVER_FAILED;
  }
  if (!tracking) {
    return GPS_SYNC_NOT_TRACKING;
  }
  return held ? GPS_SYNC_RAIM_ERROR : GPS_SYNC_INACCURATE_POSITION;
}

/* Logs the GPS reference's track lost or resumed as its state changes,
 * while the tracking log is on, with the channels' words packed: lost
 * going from valid, assessing or clearing to questionable or invalid,
 * resumed going from those to valid or assessing (0x4bb5c to 0x4bc16). */
static void gps_receiver_track_log(gps_sync_status previous,
                                   gps_sync_status next) {
  bool next_invalid = (int8_t)next >= 0 && next <= GPS_SYNC_RAIM_ERROR;
  bool previous_invalid =
      (int8_t)previous >= 0 && previous <= GPS_SYNC_RAIM_ERROR;
  bool previous_good = previous == GPS_SYNC_VALID ||
                       previous == GPS_SYNC_ASSESSING ||
                       previous == GPS_SYNC_QUESTIONABLE_CLEARING;

  if (next == GPS_SYNC_ASSESSING || next == GPS_SYNC_QUESTIONABLE_CLEARING ||
      next == GPS_SYNC_VALID) {
    gps_track_words_pack();
  } else {
    gps_channel_word_lowest();
  }
  if (previous_good && (next == GPS_SYNC_QUESTIONABLE || next_invalid)) {
    gps_log_track_change(1, previous, gps.track_words[0], gps.track_words[1]);
    gps_channel_word_pack();
    gps_channel_word_lowest();
  } else if ((previous == GPS_SYNC_QUESTIONABLE ||
              previous == GPS_SYNC_QUESTIONABLE_CLEARING || previous_invalid) &&
             (next == GPS_SYNC_VALID || next == GPS_SYNC_ASSESSING)) {
    gps.track_words[2] |= gps.channel_word_lowest;
    gps_log_track_change(0, previous, gps.track_words[2], gps.channel_word);
  }
}

/* Qualifies the GPS reference, gps.references[0], from the receiver's
 * status: with @@Ba fresh, a satellite for position, position hold or
 * a survey, the reference qualified (gps.field_571) and no Time RAIM
 * alarm, it is assessed for validation_time seconds and then valid; a
 * fault after that makes it questionable for its hysteresis, while
 * @@Ba is merely stale or there is no satellite; else it is invalid,
 * with the reason.  Only the Oncore's, receiver 0: the alternate's
 * status it takes and leaves (0x4b8a2). */
void gps_receiver_status_update_58503a(gps_receiver_number receiver,
                                       uint8_t alternate_status) {
  static const uint16_t VALIDATION_SECONDS = 35;
  gps_reference_record* reference = &gps.references[GPS_RECEIVER_ONCORE];

  if (receiver != GPS_RECEIVER_ONCORE) {
    return;
  }
  bool monitor_ok = gps.status_monitor_failed == 0;
  bool fixing =
      gps.pps_check_disabled == 0 && gps.fix_mode == ONCORE_POSITION_FIX;
  bool tracking = false;
  gps_status_freshness received = gps.status_received;

  if (fixing && received == GPS_STATUS_FRESH) {
    for (uint8_t channel = 1; !tracking && channel <= gps.channel_count;
         channel++) {
      tracking = channel_position_satellite(channel) != 0;
    }
  }
  bool held = gps.position_hold == 1 || gps.surveying == 1;
  uint8_t qualified = gps.field_571;

  raim_monitor();
  bool good = monitor_ok && held && qualified != 0 &&
              gps.raim_assessment != GPS_RAIM_ALARM;
  gps_sync_status previous = reference->state;
  gps_sync_status next = previous;

  reference->validation_time = VALIDATION_SECONDS;
  reference->pps_dead = gps_pps_dead != 0 && fixing;
  if (previous == GPS_SYNC_ASSESSING || previous == GPS_SYNC_QUESTIONABLE ||
      previous == GPS_SYNC_QUESTIONABLE_CLEARING) {
    uptime_elapsed(&reference->validation_start, (int32_t*)&reference->elapsed);
  }
  if (tracking && good) {
    switch (previous) {
      case GPS_SYNC_RECEIVER_FAILED:
      case GPS_SYNC_NOT_TRACKING:
      case GPS_SYNC_INACCURATE_POSITION:
      case GPS_SYNC_NO_UTC_DATA:
      case GPS_SYNC_RAIM_ERROR:
        reference->validation_start = get_uptime();
        reference->elapsed = 0;
        next = GPS_SYNC_ASSESSING;
        break;
      case GPS_SYNC_ASSESSING:
        if (reference->validation_time <= reference->elapsed) {
          next = GPS_SYNC_VALID;
        }
        break;
      case GPS_SYNC_QUESTIONABLE:
        if (reference->hysteresis_margin == 0) {
          next = GPS_SYNC_VALID;
        } else {
          reference->field_0c = reference->elapsed;
          next = GPS_SYNC_QUESTIONABLE_CLEARING;
        }
        break;
      case GPS_SYNC_QUESTIONABLE_CLEARING:
        if (reference->field_0c + reference->hysteresis_margin <=
            reference->elapsed) {
          next = GPS_SYNC_VALID;
        }
        break;
      case GPS_SYNC_VALID:
        gps.tracking_log_active = gps.tracking_log;
        break;
      default:
        next = GPS_SYNC_NOT_TRACKING;
        break;
    }
  } else {
    /* A fault the hysteresis rides out: @@Ba stale, or fresh without a
     * satellite for position. */
    bool passing = good && fixing &&
                   ((!tracking && received == GPS_STATUS_FRESH) ||
                    received == GPS_STATUS_STALE);

    if (previous == GPS_SYNC_VALID && passing && reference->hysteresis != 0) {
      reference->validation_start = get_uptime();
      reference->elapsed = 0;
      next = GPS_SYNC_QUESTIONABLE;
    } else if ((previous == GPS_SYNC_QUESTIONABLE ||
                previous == GPS_SYNC_QUESTIONABLE_CLEARING) &&
               passing &&
               (uint32_t)(reference->hysteresis -
                          reference->hysteresis_margin) > reference->elapsed) {
      next = GPS_SYNC_QUESTIONABLE;
    } else {
      next = gps_receiver_unqualified(monitor_ok, tracking, held);
    }
  }
  if (gps.tracking_log_active != 0) {
    gps_receiver_track_log(previous, next);
  }
  uint16_t sr = set_interrupt_mask(CPU_IPL_1);

  if (reference->override == GPS_REFERENCE_OVERRIDE_NONE) {
    if (next == GPS_SYNC_VALID) {
      reference->valid = GPS_REFERENCE_VALID;
    } else if (next == GPS_SYNC_QUESTIONABLE ||
               next == GPS_SYNC_QUESTIONABLE_CLEARING) {
      reference->valid = GPS_REFERENCE_QUESTIONABLE;
    } else {
      reference->valid = GPS_REFERENCE_INVALID;
    }
  } else {
    reference->valid = reference->override == GPS_REFERENCE_OVERRIDE_VALID;
  }
  reference->state = next;
  set_interrupt_mask(sr);
  if (next != gps_reference_last_state) {
    gps_reference_last_state = next;
  }
}
