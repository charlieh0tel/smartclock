/* From the Z3816A 4001's rewritten/gps_receiver.c, the 58503B 1.01.04's own
 * beside common/'s: of its functions, those the 58503B's code carries
 * identical or relocated (docs/firmware/58503b-1.01.04.md, "The link list").
 * Its comments' addresses are the 58503B's, but where they name another
 * image.  Each keeps its generated prototype. */

/* The satellite a channel, 1 to gps.channel_count, tracks, when @@Ba
 * has come fresh and the channel's satellite is available for position;
 * else 0 (0x57422). */
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

/* Whether a channel uses the satellite for position (0x5746e). */
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

/* The GPS satellites, and the pseudolites, the channels use for
 * position, counted and as sets if asked; false before @@Ba has come
 * (0x575a8). */
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

/* Sets a reference, the GPS's or the external 1PPS's, to not tracking,
 * valid only as its override forces (0x51c02). */
void gps_reference_reset(char external) {
  gps_reference_record* reference = &gps.references[external != 0];

  reference->valid = reference->override == GPS_REFERENCE_OVERRIDE_VALID;
  reference->state = GPS_SYNC_NOT_TRACKING;
}

/* The GPS reference's questionable hysteresis, in seconds, and its
 * margin: none for none, one less for 1 or 2, else 1 (0x51c30). */
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
 * so always 0 (0x51c5a). */
void ext_reference_hysteresis_set(int32_t hysteresis) {
  gps_reference_record* reference = &gps.references[GPS_RECEIVER_ALTERNATE];

  reference->hysteresis = hysteresis;
  reference->hysteresis_margin = hysteresis == 1 ? hysteresis - 1 : 0;
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

/* Lowers each channel's two bits of gps.channel_word_lowest, from the
 * top of the word, to its mode's, when that is less (0x540d4). */
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

/* The number of GPS satellites @@Bb predicts visible, at most twelve,
 * and their set if asked; false before @@Bb has come (0x57512). */
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
 * (0x57710). */
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
 * once complete, when a finite survey runs (0x577d6).  The image
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
 * (0x57872). */
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

/* The bits of a long the channels are packed into. */
static const uint8_t GPS_RECEIVER_WORD_BITS = 32;

/* Packs each channel's satellite, mode and strength into gps.track_words
 * for the tracking log, three channels a long from the top, 10 bits each,
 * satellite 32 as 12, then moves the longs' unused bits out, so that the
 * channels run on as 80 bits and [2]'s low half is free (0x53f90). */
void gps_track_words_pack(void) {
  static const uint8_t SATELLITE_32 = 32;
  static const uint8_t SATELLITE_32_PACKED = 12;
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
    uint8_t strength = gps_strength_bits(status->signal_strength);

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

/* Packs each channel's mode and strength, two bits each, into
 * gps.channel_word from the top, and starts gps.channel_word_lowest, the
 * lowest mode each channel has had, at all ones (0x54056). */
void gps_channel_word_pack(void) {
  static const uint16_t ALL_HIGH = 0xffff;
  uint32_t packed = 0;
  uint8_t shift = GPS_RECEIVER_WORD_BITS;

  for (uint8_t channel = 1; channel <= gps.channel_count; channel++) {
    const oncore_channel_status* status = &gps.status.channels[channel - 1];
    uint8_t mode = gps_receiver_mode_bits(status->mode);
    uint8_t strength = gps_strength_bits(status->signal_strength);

    shift -= 2;
    packed |= (uint32_t)mode << shift;
    shift -= 2;
    packed |= (uint32_t)strength << shift;
  }
  gps.channel_word = packed;
  gps.channel_word_lowest = ALL_HIGH;
}

/* Logs the GPS reference losing track, from valid, assessing or
 * clearing, or resuming it, from another state, with the state it left
 * and the two channel words (0x54148).  This image's reference states
 * are one more than the 58503A's, the new 5 logged "OT". */
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
      case GPS_SYNC_READING_NAVIGATION:
        format = "Track resumed [NU] %08X %08X";
        break;
      case GPS_SYNC_RAIM_ERROR:
        format = "Track resumed [TR] %08X %08X";
        break;
      case GPS_SYNC_INVALID:
        format = "Track resumed [OT] %08X %08X";
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

/* Sets every channel to code search and the antenna sense to normal
 * (0x5749c). */
void channels_reset(void) {
  for (uint8_t channel = 1; channel <= gps.channel_count; channel++) {
    gps.status.channels[channel - 1].mode = ONCORE_CODE_SEARCH;
  }
  gps.antenna_sense = GPS_ANTENNA_NORMAL;
}

/* @@Ba's latitude, longitude and height, the MSL one while field_608's
 * HEIGHT_MSL bit is set, else the GPS one; false before it has come or
 * without position hold, field_5bf or field_5c0 (0x57728).  The Z3815A
 * 4010's but for those two bytes, where it tests gps.surveying. */
bool gps_position_get(int32_t* latitude, int32_t* longitude, int32_t* height) {
  if (gps.status_received == GPS_STATUS_NONE ||
      (gps.position_hold != 1 && gps.field_5bf == 0 && gps.field_5c0 == 0)) {
    return false;
  }
  *latitude = gps.status.latitude;
  *longitude = gps.status.longitude;
  *height = (gps.field_608 & GPS_FIELD_608_HEIGHT_MSL) != 0
                ? gps.status.height_msl
                : gps.status.height_gps;
  return true;
}

/* @@Ba's time and date as local time, and its fraction of a second;
 * false before it has come (0x5777a): the Z3816A 4001's, where the
 * 58503A 3704's gives no fraction. */
bool gps_time_local_get(time_of_day* time, uint32_t* fraction, uint32_t* date) {
  bool received = gps.status_received != GPS_STATUS_NONE;

  if (received) {
    time_of_day local_time = gps.status.time;
    uint32_t local_date = oncore_date_pack(gps.status.date);

    utc_to_local(&local_date, &local_time);
    *time = local_time;
    *fraction = gps.status.fraction;
    *date = local_date;
  }
  return received;
}

/* uptime_elapsed's second argument: a clock gone back restarts the start
 * at the uptime, where 0 restarts it at 0. */
static const char GPS_RECEIVER_RESTART_AT_UPTIME = 1;

/* Assesses Time RAIM from @@Bn, while it is on and its status known,
 * keeping the one-sigma estimate of each OK solution: from inactive or
 * OK an alarm solution raises an alarm detected, at raim_suspect_time,
 * keeping its date, time, the estimate and the channels; not OK for
 * RAIM_ALARM_SECONDS more makes it an alarm; OK clears either (0x51ae2).
 * The 58503A 3704's goes from inactive to an alarm detected on any
 * solution not OK and keeps none of the alarm's record. */
void raim_monitor(void) {
  static const uint32_t RAIM_ALARM_SECONDS = 4;
  oncore_raim_fields* raim = &gps.raim;
  uint8_t from_clock;
  int32_t elapsed;

  if (raim->raim_on != 1 || gps.raim_received == 0 ||
      raim->raim_status == ONCORE_RAIM_NEITHER ||
      raim->solution_status == ONCORE_RAIM_UNKNOWN) {
    gps.raim_assessment = GPS_RAIM_INACTIVE;
    return;
  }
  if (raim->solution_status == ONCORE_RAIM_OK) {
    gps.raim_last_sigma = raim->sigma;
  }
  switch (gps.raim_assessment) {
    case GPS_RAIM_INACTIVE:
    case GPS_RAIM_OK:
      if (raim->solution_status != ONCORE_RAIM_ALARM) {
        gps.raim_assessment = GPS_RAIM_OK;
        break;
      }
      gps.raim_assessment = GPS_RAIM_ALARM_DETECTED;
      raim_suspect_time = get_uptime();
      gps_counts.raim_detected++;
      gps_date_time_local(&gps.raim_alarm_date, &gps.raim_alarm_time,
                          &from_clock);
      gps.raim_alarm_sigma = gps.raim_last_sigma;
      memcpy(gps.raim_alarm_channels, raim->channels,
             sizeof gps.raim_alarm_channels);
      break;
    case GPS_RAIM_ALARM_DETECTED:
      if (raim->solution_status == ONCORE_RAIM_OK) {
        gps.raim_assessment = GPS_RAIM_OK;
      } else if (!uptime_elapsed(&raim_suspect_time,
                                 GPS_RECEIVER_RESTART_AT_UPTIME, &elapsed)) {
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

/* Starts the command monitor enabled at the head of its queries and
 * the status monitor disabled, neither failed, and the receiver's
 * health cleared (0x520a0): the 58503A 3704's, but that the status
 * monitor's failure is gps.status_monitor_timed_out here, and no count
 * of statuses without @@Bn is kept. */
void command_monitor_init(void) {
  gps.command_monitor_state = GPS_COMMAND_MONITOR_DISABLED;
  gps.status_monitor_state = GPS_STATUS_MONITOR_DISABLED;
  gps.receiver_health_failed = 0;
  gps.command_monitor_failed = 0;
  gps.status_monitor_timed_out = 0;
  status_happening(STATUS_GPS_FAILURE_CLEARED);
  gps.command_monitor_enabled = 1;
  gps.command_monitor_error = 0;
  gps.command_monitor_query = command_monitor_queries;
  gps.status_monitor_enabled = 0;
}

/* The command monitor's step: after a command fails it queries the
 * engine every QUERY_INTERVAL seconds, a query of command_monitor_queries
 * in turn, the next its send filter admits after one answered or
 * refused, until a command is answered (0x52104 to 0x52236).  The
 * 58503A 3704's skips no query. */
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
    case GPS_COMMAND_MONITOR_QUERY: {
      if (gps.command_monitor_error == 0) {
        gps.command_monitor_failed = 0;
        gps.command_monitor_state = GPS_COMMAND_MONITOR_STANDBY;
        break;
      }
      if (gps.command_monitor_enabled == 0) {
        gps.command_monitor_state = GPS_COMMAND_MONITOR_DISABLED;
        break;
      }
      bool reached = uptime_elapsed(&gps.command_monitor_time,
                                    GPS_RECEIVER_RESTART_AT_UPTIME, &elapsed);

      if (reached && (uint32_t)elapsed > QUERY_INTERVAL) {
        gps_request_post(GPS_SENDER_COMMAND_MONITOR, GPS_POST_STEP, NULL,
                         gps.command_monitor_query, 0);
        gps.command_monitor_state = GPS_COMMAND_MONITOR_QUERY_ACK;
      } else if (!reached) {
        gps.command_monitor_time = get_uptime();
      }
      break;
    }
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
        case GPS_RESULT_TIMEOUT:
          gps.command_monitor_query = monitor_query_next(
              gps.command_monitor_query, command_monitor_queries);
          break;
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

/* The status monitor's limits, s or statuses: a status within RECENT
 * of the last @@Ba (and of the last @@Bn while it is due) with at most
 * MOST_REPEATS_FRESH repeats is fresh; none within LATE of either, and
 * LATE past the watch's start, or more than MOST_REPEATS repeats, is a
 * miss. */
static const uint32_t GPS_RECEIVER_STATUS_RECENT = 4;
static const uint32_t GPS_RECEIVER_STATUS_LATE = 36;
static const uint8_t GPS_RECEIVER_MOST_REPEATS_FRESH = 4;
static const uint8_t GPS_RECEIVER_MOST_REPEATS = 45;
/* The longest @@Bn rate, s, at which its lateness counts. */
static const uint8_t GPS_RECEIVER_RAIM_RATE_MOST = 36;

/* Starts the status monitor watching afresh in a state (0x522ae,
 * 0x523ea). */
static void gps_receiver_status_watch(gps_status_monitor_state state) {
  /* a time no status has, so that the next is new */
  static const uint32_t NO_TIME = 0xffffff;

  gps.status_last_time = NO_TIME;
  gps.status_repeat_count = 0;
  gps.field_5f2 = get_uptime();
  gps.status_monitor_state = state;
}

/* The status monitor's step, while enabled with the status requested,
 * on the uptimes the decoders stamp, of the last @@Ba
 * (status_monitor_time, 0 for none) and @@Bn (field_5ee), and the
 * watch's start (field_5f2), all restarted if the clock goes back:
 * with no @@Ba the status is none; on a miss it requests the outputs
 * again, failing on a miss after that request's answer; else the
 * status is fresh while recent, then stale (0x52254 to 0x52402).  The
 * 58503A 3704's counts its own time and the decoders' fresh flags. */
static void gps_receiver_status_monitor(void) {
  uint32_t now = get_uptime();

  if (now < gps.field_5f2 || now < gps.status_monitor_time ||
      now < gps.field_5ee) {
    gps.field_5f2 = now;
    gps.status_monitor_time = 0;
    gps.field_5ee = 0;
  }
  bool watching = gps.status_monitor_enabled == 1 && gps.status_rate != 0;

  switch (gps.status_monitor_state) {
    case GPS_STATUS_MONITOR_DISABLED:
      if (watching) {
        gps_receiver_status_watch(GPS_STATUS_MONITOR_STANDBY);
      }
      break;
    case GPS_STATUS_MONITOR_STANDBY:
    case GPS_STATUS_MONITOR_RECOVERED: {
      if (!watching) {
        gps.status_monitor_state = GPS_STATUS_MONITOR_DISABLED;
        break;
      }
      bool raim_due = (gps.field_608 & GPS_FIELD_608_TIME_RAIM) != 0 &&
                      gps.raim_received != 0 && gps.raim.raim_on == 1 &&
                      gps.raim.rate != 0 &&
                      gps.raim.rate <= GPS_RECEIVER_RAIM_RATE_MOST;

      if (gps.status_monitor_time == 0) {
        gps.status_received = GPS_STATUS_NONE;
        gps.antenna_sense = GPS_ANTENNA_NORMAL;
      } else if (((now - gps.status_monitor_time > GPS_RECEIVER_STATUS_LATE ||
                   (raim_due &&
                    now - gps.field_5ee > GPS_RECEIVER_STATUS_LATE)) &&
                  now - gps.field_5f2 > GPS_RECEIVER_STATUS_LATE) ||
                 gps.status_repeat_count > GPS_RECEIVER_MOST_REPEATS) {
        if (gps.status_monitor_state == GPS_STATUS_MONITOR_STANDBY) {
          gps_request_post(GPS_SENDER_STATUS_MONITOR, GPS_SCRIPT_OUTPUTS_ON,
                           NULL, NULL, 1);
          gps.status_monitor_state = GPS_STATUS_MONITOR_RECOVERY;
        } else {
          gps.status_monitor_timed_out = 1;
          gps_counts.status_monitor_failures++;
          gps.status_received = GPS_STATUS_NONE;
          gps.visibility_received = 0;
          gps.raim_received = 0;
          gps.antenna_sense = GPS_ANTENNA_NORMAL;
          gps.status_monitor_state = GPS_STATUS_MONITOR_STANDBY;
        }
      } else if (now - gps.status_monitor_time <= GPS_RECEIVER_STATUS_RECENT &&
                 (!raim_due ||
                  now - gps.field_5ee <= GPS_RECEIVER_STATUS_RECENT) &&
                 gps.status_repeat_count <= GPS_RECEIVER_MOST_REPEATS_FRESH) {
        gps.status_received = GPS_STATUS_FRESH;
        gps.status_monitor_timed_out = 0;
        gps.status_monitor_state = GPS_STATUS_MONITOR_STANDBY;
      } else if (gps.status_received == GPS_STATUS_FRESH) {
        gps.status_received = GPS_STATUS_STALE;
        gps.status_monitor_timed_out = 0;
      }
      break;
    }
    case GPS_STATUS_MONITOR_RECOVERY:
      if (status_monitor_result != GPS_RESULT_PENDING) {
        gps_receiver_status_watch(GPS_STATUS_MONITOR_RECOVERED);
      }
      break;
    default:
      break;
  }
}

/* The GPS task's monitors: the command monitor, the channels marked
 * searching while the engine is not fixing or @@Ba is not fresh, the
 * status monitor, and the receiver's health, failed while either
 * monitor is, reported as it changes (0x520e2), as the 58503A 3704's. */
void command_monitor(void) {
  gps_receiver_command_monitor();
  if (gps.pps_check_disabled != 0 || gps.fix_mode == ONCORE_IDLE ||
      gps.status_received != GPS_STATUS_FRESH) {
    channels_reset();
  }
  gps_receiver_status_monitor();
  if (gps.command_monitor_failed != 0 || gps.status_monitor_timed_out != 0) {
    if (gps.receiver_health_failed == 0) {
      gps.receiver_health_failed = 1;
      status_happening(STATUS_GPS_FAILURE_SET);
    }
  } else if (gps.receiver_health_failed != 0) {
    gps.receiver_health_failed = 0;
    status_happening(STATUS_GPS_FAILURE_CLEARED);
  }
}

/* The poll monitor, gpsm_task's each pass (0x5346c): while
 * gps.status_monitor_failed, which a GPS step sets, is set and the
 * command monitor has not failed, it posts a query of
 * poll_monitor_queries, @@Bo's or @@Ta's, every POLL_INTERVAL seconds,
 * or POLL_INTERVAL_LONG while field_608's C/N bit and field_5c3 are set,
 * taking the next its filter admits after one answered; its query is
 * gps.field_5fe, its time field_602 (0x52440).  The Z3816A 4001's
 * 0x50102 is the same. */
void poll_monitor(void) {
  static const uint32_t POLL_INTERVAL = 30;       /* s */
  static const uint32_t POLL_INTERVAL_LONG = 756; /* s */
  bool active =
      gps.status_monitor_failed != 0 && gps.command_monitor_failed == 0;
  int32_t elapsed;

  switch (gps.field_5fd) {
    case GPS_RECEIVER_POLL_IDLE:
      if (!active) {
        return;
      }
      break;
    case GPS_RECEIVER_POLL_WAITING: {
      if (!active) {
        gps.field_5fd = GPS_RECEIVER_POLL_IDLE;
        return;
      }
      bool reached = uptime_elapsed(&gps.field_602,
                                    GPS_RECEIVER_RESTART_AT_UPTIME, &elapsed);

      if (reached) {
        uint32_t interval =
            (gps.field_608 & GPS_FIELD_608_SIGNAL_CN) != 0 && gps.field_5c3 != 0
                ? POLL_INTERVAL_LONG
                : POLL_INTERVAL;

        if ((uint32_t)elapsed > interval) {
          gps_request_post(GPS_SENDER_POLL_MONITOR, GPS_POST_STEP, NULL,
                           (gps_script_step**)gps.field_5fe, 1);
          gps.field_5fd = GPS_RECEIVER_POLL_POSTED;
          return;
        }
      }
      if (!reached) {
        gps.field_602 = get_uptime();
      }
      return;
    }
    case GPS_RECEIVER_POLL_POSTED:
      if (poll_monitor_result == GPS_RESULT_PENDING) {
        return;
      }
      if (poll_monitor_result == GPS_RESULT_SUCCESS) {
        gps.field_5fe = (uint32_t)monitor_query_next(
            (gps_script_step**)gps.field_5fe, poll_monitor_queries);
      }
      break;
    default:
      return;
  }
  gps.field_602 = get_uptime();
  gps.field_5fd = GPS_RECEIVER_POLL_WAITING;
}

/* The place of a monitor's next query in its null-ended list after the
 * place given, from the list's head again at the null: the first whose
 * message's send filter admits it, or the place given when none does;
 * that place itself at the null (0x52542). */
gps_script_step** monitor_query_next(gps_script_step** query,
                                     gps_script_step** list) {
  gps_script_step** next = query;
  uint8_t admitted;

  if (*query == NULL) {
    return query;
  }
  do {
    next++;
    if (*next == NULL) {
      next = list;
    }
    gps_script_step* step = *next;
    const oncore_message_entry* entry = oncore_message_entry_at(step->code);

    admitted = entry->filter == NULL ||
               oncore_filter_run(entry->filter, step->code, entry->encoders[0],
                                 step->arguments);
  } while (admitted == 0 && next != query);
  return next;
}

/* The satellites the channels use, by number, each with its level and
 * cell, counted, for :CHOE? (0x3d368); false before @@Ba has come
 * (0x57648). */
bool satellites_used_list(uint8_t* count, satellite_list_entry* list) {
  satellite_sets sets;

  if (gps.status_received == GPS_STATUS_NONE) {
    return false;
  }
  satellite_sets_build(gps.status.channels, &sets);
  satellite_status_list(
      SATELLITE_LIST_USED, gps.channel_count, &sets, gps.status.channels,
      (gps_visibility_record*)&gps.visibility_received, list, count);
  return true;
}

/* The receiver state a GPS reference that is not qualified takes: failed
 * without the status monitor, not tracking without a satellite for
 * position, else an inaccurate position without position hold,
 * field_5bf or field_5c0, reading the navigation message without the
 * UTC offset while the clock keeps UTC, a Time RAIM error with its
 * alarm, else invalid (0x51f26 to 0x51f54). */
static gps_sync_status gps_receiver_unqualified(bool monitor_ok, bool tracking,
                                                bool held, bool dated,
                                                bool alarm) {
  if (!monitor_ok) {
    return GPS_SYNC_RECEIVER_FAILED;
  }
  if (!tracking) {
    return GPS_SYNC_NOT_TRACKING;
  }
  if (!held) {
    return GPS_SYNC_INACCURATE_POSITION;
  }
  if (!dated) {
    return GPS_SYNC_READING_NAVIGATION;
  }
  return alarm ? GPS_SYNC_RAIM_ERROR : GPS_SYNC_INVALID;
}

/* Whether a receiver state is an invalid one, GPS_SYNC_RECEIVER_FAILED
 * to GPS_SYNC_INVALID, as a signed byte. */
static bool gps_receiver_invalid(gps_sync_status state) {
  return (int8_t)state >= 0 && state <= GPS_SYNC_INVALID;
}

/* Logs the GPS reference's track lost or resumed as its state changes,
 * while the tracking log is on, with the channels' words packed: lost
 * going from valid, assessing or clearing to questionable or invalid,
 * resumed going from those to valid or assessing (0x51f5e to 0x52018),
 * the Z3815A 4010's. */
static void gps_receiver_track_log(gps_sync_status previous,
                                   gps_sync_status next) {
  if (next == GPS_SYNC_ASSESSING || next == GPS_SYNC_QUESTIONABLE_CLEARING ||
      next == GPS_SYNC_VALID) {
    gps_track_words_pack();
  } else {
    gps_channel_word_lowest();
  }
  if ((previous == GPS_SYNC_VALID || previous == GPS_SYNC_ASSESSING ||
       previous == GPS_SYNC_QUESTIONABLE_CLEARING) &&
      (next == GPS_SYNC_QUESTIONABLE || gps_receiver_invalid(next))) {
    gps_log_track_change(1, previous, gps.track_words[0], gps.track_words[1]);
    gps_channel_word_pack();
    gps_channel_word_lowest();
  } else if ((previous == GPS_SYNC_QUESTIONABLE ||
              previous == GPS_SYNC_QUESTIONABLE_CLEARING ||
              gps_receiver_invalid(previous)) &&
             (next == GPS_SYNC_VALID || next == GPS_SYNC_ASSESSING)) {
    gps.track_words[2] |= gps.channel_word_lowest;
    gps_log_track_change(0, previous, gps.track_words[2], gps.channel_word);
  }
}

/* Qualifies the GPS reference, receiver 0, from its receiver's status,
 * nothing for another (0x51c7a): with the status fresh and a satellite
 * for position, the status monitor not failed, position hold,
 * field_5bf or field_5c0, the UTC offset while the clock keeps UTC
 * (clock_keeps_utc) and no Time RAIM alarm, it is assessed for 35
 * seconds and then valid; a fault after that makes it questionable for
 * its hysteresis, while the status is merely stale or there is no such
 * satellite; else it is invalid, with the reason; its 1PPS dead while
 * the hardware condition's bit 8 is set and the engine fixes.  The
 * Z3815A 4010's GPS reference, but for those conditions, where the
 * Z3815A's takes tps' first and ninth fields and a survey; this image's
 * qualifies no external reference. */
void gps_receiver_status_update(gps_receiver_number receiver,
                                uint8_t alternate_status) {
  static const uint16_t GPS_VALIDATION_SECONDS = 35;
  static const int16_t PPS_DEAD_CONDITION_BIT = 8;
  gps_reference_record* reference = &gps.references[GPS_RECEIVER_ONCORE];

  if (receiver != GPS_RECEIVER_ONCORE) {
    return;
  }
  const bool monitor_ok = gps.status_monitor_timed_out == 0;
  const bool fixing =
      gps.pps_check_disabled == 0 && gps.fix_mode == ONCORE_POSITION_FIX;
  const gps_status_freshness received = gps.status_received;
  bool tracking = false;

  if (fixing && received == GPS_STATUS_FRESH) {
    for (uint8_t channel = 1; !tracking && channel <= gps.channel_count;
         channel++) {
      tracking = channel_position_satellite(channel) != 0;
    }
  }
  const bool held =
      gps.position_hold == 1 || gps.field_5bf != 0 || gps.field_5c0 != 0;
  const bool dated = !clock_keeps_utc() || gps.utc_offset_received != 0;

  raim_monitor();
  const bool alarm = gps.raim_assessment == GPS_RAIM_ALARM;
  const bool good = monitor_ok && held && dated && !alarm;

  reference->validation_time = GPS_VALIDATION_SECONDS;
  reference->pps_dead =
      bit_is_set(status_hardware_condition(), PPS_DEAD_CONDITION_BIT) && fixing;
  const gps_sync_status previous = reference->state;
  gps_sync_status next = previous;

  if (previous == GPS_SYNC_ASSESSING || previous == GPS_SYNC_QUESTIONABLE ||
      previous == GPS_SYNC_QUESTIONABLE_CLEARING) {
    uptime_elapsed(&reference->validation_start, GPS_RECEIVER_RESTART_AT_UPTIME,
                   (int32_t*)&reference->elapsed);
  }
  if (tracking && good) {
    switch (previous) {
      case GPS_SYNC_RECEIVER_FAILED:
      case GPS_SYNC_NOT_TRACKING:
      case GPS_SYNC_INACCURATE_POSITION:
      case GPS_SYNC_READING_NAVIGATION:
      case GPS_SYNC_RAIM_ERROR:
      case GPS_SYNC_INVALID:
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
    /* A fault the hysteresis rides out: the status stale, or fresh
     * without a satellite for position. */
    const bool passing = good && fixing &&
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
      next = gps_receiver_unqualified(monitor_ok, tracking, held, dated, alarm);
    }
  }
  if (gps.tracking_log_active != 0) {
    gps_receiver_track_log(previous, next);
  }
  const uint16_t sr = set_interrupt_mask(CPU_IPL_1);

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

/* Clears the Sat View page's statistics but the byte after the
 * averages, as gps_init and the console's clr_satview do (0x52ede,
 * 0x2b83a): the map, the hours seen, the ranges and the last hour, the
 * averages, the average and its count, and the Doppler bins (0x5421c). */
void sky_statistics_clear(void) {
  memset(sky_statistics.map, 0, sizeof sky_statistics.map);
  memset(sky_statistics.used_range, 0,
         sizeof sky_statistics.used_range + sizeof sky_statistics.hour);
  memset(sky_statistics.unused_average, 0,
         sizeof sky_statistics.unused_average);
  sky_statistics.hours_seen = 0;
  sky_statistics.samples = 0;
  sky_statistics.average = 0.0f;
  memset(sky_statistics.doppler_bins, 0, sizeof sky_statistics.doppler_bins);
}

/* The symbols of sky_statistics' map, sky_map_symbols' indexes: none,
 * a used satellite's level class 1 to 9 or none, then one tracked but
 * not used, without a Doppler, rising, at its apex or setting, and one
 * seen both rising and setting there. */
enum {
  SKY_NONE = 0,
  SKY_LEVEL_LOWEST = 1,
  SKY_LEVEL_HIGHEST = 9,
  SKY_USED_NO_LEVEL = 10,
  SKY_UNUSED_NO_DOPPLER = 11,
  SKY_RISING = 12,
  SKY_APEX = 13,
  SKY_SETTING = 14,
  SKY_MIXED = 15,
};

/* A used satellite's level class, 1 to 9: an SS level past the
 * thresholds sky_ss_thresholds lists, a C/N level in steps of 3 dB-Hz
 * from 26, below 32 class 1 (0x54378 to 0x543c2). */
static uint8_t sky_level_class(int16_t level) {
  static const int16_t CN_LOWEST = 32;
  static const int16_t CN_BASE = 26;
  static const int16_t CN_STEP = 3;
  uint8_t level_class = SKY_LEVEL_LOWEST;

  if ((gps.field_608 & GPS_FIELD_608_SIGNAL_SS) != 0) {
    while (level_class < SKY_LEVEL_HIGHEST &&
           sky_ss_thresholds[level_class - 1] < level) {
      level_class++;
    }
    return level_class;
  }
  if (level < CN_LOWEST) {
    return SKY_LEVEL_LOWEST;
  }
  int32_t steps = (level - CN_BASE) / CN_STEP;

  return steps < SKY_LEVEL_HIGHEST ? (uint8_t)steps : SKY_LEVEL_HIGHEST;
}

/* The satellites' Doppler, Hz, the empty entry's none. */
static const int16_t SKY_NO_DOPPLER = -0x8000;

/* What a satellite on a channel puts in its map nibble, which holds
 * old, or 0 for no change (0x5434c to 0x54418): a used one its level
 * class, averaged with an old class, or SKY_USED_NO_LEVEL without a
 * level where the nibble holds no class; one tracked but not used,
 * where the nibble holds no class and is not mixed, SKY_UNUSED_NO_DOPPLER
 * without a Doppler on an empty nibble, else its Doppler's sign, within
 * APEX_HZ the apex, mixed where the nibble holds another sign. */
static uint8_t sky_symbol(const satellite_list_entry* entry, uint8_t old) {
  static const int32_t APEX_HZ = 100;
  const bool old_level = old != SKY_NONE && old <= SKY_LEVEL_HIGHEST;

  if (entry->attempting == 0) {
    if (entry->level == -1) {
      return old_level ? SKY_NONE : SKY_USED_NO_LEVEL;
    }
    uint8_t level_class = sky_level_class(entry->level);

    if (old_level) {
      level_class = (uint8_t)((old + level_class) >> 1);
    }
    return level_class;
  }
  if ((old != SKY_NONE && old <= SKY_USED_NO_LEVEL) || old == SKY_MIXED) {
    return SKY_NONE;
  }
  if (entry->cell.doppler == SKY_NO_DOPPLER) {
    return old == SKY_NONE ? SKY_UNUSED_NO_DOPPLER : SKY_NONE;
  }
  uint8_t sign;

  if (abs(entry->cell.doppler) <= APEX_HZ) {
    sign = SKY_APEX;
  } else if (entry->cell.doppler < 1) {
    sign = SKY_SETTING;
  } else {
    sign = SKY_RISING;
  }
  if (sign != old && old >= SKY_RISING && old <= SKY_SETTING) {
    sign = SKY_MIXED;
  }
  return sign;
}

/* A Doppler's bin, 6 less it in 500 Hz steps, rounded down for a
 * negative one, from 0 to 13 (0x5441e to 0x5444c). */
static uint16_t sky_doppler_bit(int16_t doppler) {
  static const int32_t STEP_HZ = 500;
  static const int16_t CENTER = 6;
  static const int16_t LAST = 13;
  int16_t bin =
      (int16_t)((doppler - (doppler < 0 ? STEP_HZ - 1 : 0)) / -STEP_HZ) +
      CENTER;

  if (bin > LAST - 1) {
    bin = LAST;
  }
  if (bin < 1) {
    bin = 0;
  }
  return (uint16_t)(1 << bin);
}

/* Gathers the Sat View page's statistics from the satellites on the
 * channels, gpsm_task's each pass (0x5349e): each with a position marks
 * its place on the map, and those with a Doppler their bins; then, by
 * the hour gps_date_time_local gives, starting an hour's figures afresh
 * as it changes, the hour's bins, the fewest and most used, and the
 * running average of those tracked but not used, over at most 7201
 * samples (0x54276).  The Z3816A 4001's 0x51f38 is the same. */
void sky_statistics_update(void) {
  static const uint16_t AZIMUTH_LAST = 359;
  static const uint8_t ELEVATION_LAST = 89;
  static const uint8_t DEGREES_PER_BIN = 5;
  static const uint8_t DEGREES_PER_BYTE = 10;
  static const uint16_t MOST_SAMPLES = 7200;
  static const uint8_t RANGE_START = 0xf0;
  static const double ROUNDING = 0.5;
  satellite_sets sets;
  satellite_list_entry list[GPS_CHANNELS];
  uint8_t count;
  uint8_t used = 0;
  uint16_t unused_bins = 0;
  uint16_t used_bins = 0;

  satellite_sets_build(gps.status.channels, &sets);
  satellite_status_list(
      SATELLITE_LIST_ON_CHANNELS, gps.channel_count, &sets, gps.status.channels,
      (gps_visibility_record*)&gps.visibility_received, list, &count);
  const satellite_list_entry* entry = list;

  for (uint8_t i = 0; i < count; i++, entry++) {
    if (entry->attempting == 0) {
      used++;
    }
    if (entry->cell.cell.status != SATELLITE_CELL_POSITION) {
      continue;
    }
    uint32_t azimuth = entry->cell.cell.azimuth < AZIMUTH_LAST
                           ? entry->cell.cell.azimuth
                           : AZIMUTH_LAST;
    uint8_t column = (uint8_t)(azimuth / DEGREES_PER_BIN);
    uint8_t elevation = entry->cell.cell.elevation < ELEVATION_LAST
                            ? entry->cell.cell.elevation
                            : ELEVATION_LAST;
    uint8_t row = elevation / DEGREES_PER_BYTE;
    bool high = (elevation / DEGREES_PER_BIN) % 2 != 0;
    uint8_t* cell = &sky_statistics.map[column][(int8_t)row];
    uint8_t old = high ? *cell >> SKY_HIGH_SHIFT : *cell;
    uint8_t symbol = sky_symbol(entry, old & SKY_LOW_NIBBLE);

    if (symbol != SKY_NONE) {
      *cell &= high ? SKY_LOW_NIBBLE : (uint8_t)~SKY_LOW_NIBBLE;
      *cell |= high ? (uint8_t)(symbol << SKY_HIGH_SHIFT) : symbol;
    }
    if (entry->cell.doppler != SKY_NO_DOPPLER) {
      if (entry->attempting == 0) {
        used_bins |= sky_doppler_bit(entry->cell.doppler);
      } else {
        unused_bins |= sky_doppler_bit(entry->cell.doppler);
      }
    }
  }
  uint32_t date;
  time_of_day time;
  uint8_t from_clock;

  gps_date_time_local(&date, &time, &from_clock);
  if (time.hour >= SKY_HOURS) {
    return;
  }
  const uint8_t hour = time.hour;

  if (hour != sky_statistics.hour) {
    sky_statistics.samples = 0;
    sky_statistics.used_range[hour] = RANGE_START;
    sky_statistics.hour = hour;
    sky_statistics.unused_average[hour] = 0;
    sky_statistics.average = 0.0f;
    sky_statistics.doppler_bins[hour].unused = 0;
    sky_statistics.doppler_bins[hour].used = 0;
  }
  sky_statistics.doppler_bins[hour].unused |= unused_bins;
  sky_statistics.doppler_bins[hour].used |= used_bins;
  uint8_t* range = &sky_statistics.used_range[hour];

  if (used < (uint32_t)(*range >> SKY_HIGH_SHIFT)) {
    *range &= SKY_LOW_NIBBLE;
    *range |= (uint8_t)(used << SKY_HIGH_SHIFT);
  } else if ((*range & SKY_LOW_NIBBLE) < used) {
    *range &= (uint8_t)~SKY_LOW_NIBBLE;
    *range |= used;
  }
  sky_statistics.hours_seen |= 1u << hour;
  if (sky_statistics.samples <= MOST_SAMPLES) {
    float total =
        float_add(integer_to_float(count - used),
                  float_multiply(unsigned_to_float(sky_statistics.samples),
                                 sky_statistics.average));

    sky_statistics.average =
        float_divide(total, integer_to_float(sky_statistics.samples + 1));
    sky_statistics.unused_average[hour] = (uint8_t)double_to_unsigned(
        double_add(ROUNDING, float_to_double(sky_statistics.average)));
    sky_statistics.samples++;
  }
}
