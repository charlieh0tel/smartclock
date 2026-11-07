/* The GPS task, gpsm: the steps of its own a request or script runs in
 * place of a sentence to the engine, here those that fill
 * gps_request_step's arguments for a ZDA or a GLL from the request and
 * the engine's time or position (docs/firmware/gps.md;
 * docs/firmware/z3815a-4010.md).  common/gps_task.c holds what the
 * images share.  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"; docs/firmware/modules.md,
 * `applications/gps/task`).  The exported C includes this file after its
 * own, so the types and prototypes it declares are in scope; each
 * function here keeps its generated prototype. */

/* The arguments' places: ZDA's time and its day, month and year; GLL's
 * latitude and longitude. */
static const uint8_t GPS_STEP_TIME = 0;
static const uint8_t GPS_STEP_DAY = 1;
static const uint8_t GPS_STEP_MONTH = 2;
static const uint8_t GPS_STEP_YEAR = 3;
static const uint8_t GPS_STEP_LATITUDE = 0;
static const uint8_t GPS_STEP_LONGITUDE = 1;

/* A time as ZDA's hhmmss, its seconds at most 59, into gps_request_step's
 * first argument (0x584b8). */
void gps_step_time_set(uint8_t hour, uint8_t minute, uint8_t second) {
  static const uint8_t SECOND_LAST = 59;
  static const int32_t PER_HOUR = 10000;
  static const int32_t PER_MINUTE = 100;

  long_array_set((uint32_t)hour * PER_HOUR + (uint32_t)minute * PER_MINUTE +
                     (second < SECOND_LAST ? second : SECOND_LAST),
                 gps_request_step.arguments, GPS_STEP_TIME);
}

/* GPS_STEP_TIME_ARGUMENTS: the request's hour, minute and second as ZDA's
 * time, and the engine's date, tps's, its day and month at least 1 and
 * its year at least 1994 (0x58506). */
void gps_step_time_arguments(void) {
  static const uint8_t PARTS = 3;
  static const uint16_t YEAR_FIRST = 1994;
  const oncore_date_fields* date = &gps.tps.date;
  uint8_t parts[3];

  for (uint8_t i = 0; i < PARTS; i++) {
    parts[i] = long_array_get(gps_request_step.arguments, i);
  }
  gps_step_time_set(parts[0], parts[1], parts[2]);
  long_array_set(date->day > 1 ? date->day : 1, gps_request_step.arguments,
                 GPS_STEP_DAY);
  long_array_set(date->month > 1 ? date->month : 1, gps_request_step.arguments,
                 GPS_STEP_MONTH);
  long_array_set(date->year > YEAR_FIRST ? date->year : YEAR_FIRST,
                 gps_request_step.arguments, GPS_STEP_YEAR);
}

/* GPS_STEP_DATE_ARGUMENTS: the request's month, day and year, its year at
 * most 2040, as ZDA's, with the engine's time, tps's (0x585c6). */
void gps_step_date_arguments(void) {
  static const uint8_t REQUEST_MONTH = 0;
  static const uint8_t REQUEST_YEAR = 2;
  static const uint16_t YEAR_LAST = 2040;
  const time_of_day* time = &gps.tps.time;
  const uint16_t year =
      long_array_get(gps_request_step.arguments, REQUEST_YEAR);

  long_array_set(year < YEAR_LAST ? year : YEAR_LAST,
                 gps_request_step.arguments, GPS_STEP_YEAR);
  gps_request_step.arguments[GPS_STEP_MONTH] =
      gps_request_step.arguments[REQUEST_MONTH];
  gps_step_time_set(time->hour, time->minute, time->second);
}

/* GPS_STEP_LATITUDE_ARGUMENTS and GPS_STEP_LONGITUDE_ARGUMENTS: GLL's
 * position, the request's angle with the engine's other, GGA's
 * (0x5862c). */
void gps_step_position_arguments(char longitude) {
  const nmea_gga_fields* gga = &gps.gga;

  if (longitude == 0) {
    long_array_set(
        nmea_hemisphere_sign(gga->longitude_hemisphere) * gga->longitude,
        gps_request_step.arguments, GPS_STEP_LONGITUDE);
  } else {
    gps_request_step.arguments[GPS_STEP_LONGITUDE] =
        gps_request_step.arguments[GPS_STEP_LATITUDE];
    long_array_set(
        nmea_hemisphere_sign(gga->latitude_hemisphere) * gga->latitude,
        gps_request_step.arguments, GPS_STEP_LATITUDE);
  }
}

/* Clears gps, sets the periodic sentence up from int,GSV00 with no
 * setting to query, clears both references' qualification, set to the
 * NV settings' hysteresis, and the command monitor, and takes the
 * tracking log from the NV settings (0x57c04).  The 58503A 3704's but
 * for its own: the periodic sentence's state (0x57c2c to 0x57c38) and
 * the fix mode set to a position fix (0x57c74), where the 58503A's sets
 * field_571. */
void gps_init(void) {
  memset(&gps, 0, sizeof gps);
  gps.field_5ee[0] = 0; /* again, as the image does */
  gps_commands_blocked = 0;
  gps_periodic.stage = GPS_PERIODIC_GSV;
  gps_periodic.query_code = GPS_REQUEST_NONE;
  gps_periodic.queries = 0;
  gps_periodic.sends = 0;
  gps_reference_reset(0);
  gps_reference_reset(1);
  gps_reference_hysteresis_set(nv_settings.questionable_hysteresis);
  ext_reference_hysteresis_set(nv_settings.external_questionable_hysteresis);
  gps.references[0].override = GPS_REFERENCE_OVERRIDE_NONE;
  gps.references[1].override = GPS_REFERENCE_OVERRIDE_NONE;
  status_screen_print_interval = 0;
  gps.pps_check_disabled = 0;
  gps.fix_mode = ONCORE_POSITION_FIX;
  command_monitor_init();
  gps.tracking_log = nv_settings.tracking_log;
  gps.tracking_log_active = 0;
}

/* Fills gps_request_step's arguments for the Time RAIM setup, the 58503A
 * 3704's @@Bn: each second, the algorithm on or off and its alarm limit,
 * and the 1PPS on all the time.  This image always takes
 * nv_settings' position-hold settings, its test of position hold a
 * constant (0x5844a). */
void raim_request_arguments(void) {
  static const int32_t EACH_SECOND = 1;
  static const int32_t PULSE_ALWAYS = 1;
  /* nv_settings' RAIM settings in position hold. */
  static const uint8_t IN_HOLD = 0;

  long_array_set(EACH_SECOND, gps_request_step.arguments, 0);
  long_array_set((int8_t)nv_settings.raim_state[IN_HOLD],
                 gps_request_step.arguments, 1);
  long_array_set(nv_settings.raim_threshold[IN_HOLD],
                 gps_request_step.arguments, 2);
  long_array_set(PULSE_ALWAYS, gps_request_step.arguments, 3);
}

/* Takes the status snapshot of each second, or at 03:14:07 and 06:28:15
 * the flagged one, with the command log copied, its two addresses into
 * the copy; the same time twice freezes them (0x58aac). */
void status_snapshot_update(uint32_t time) {
  if (status_snapshots_frozen != 0) {
    return;
  }
  if (time == clock_pack(6, 28, 15) || time == clock_pack(3, 14, 7)) {
    if (time == status_snapshot_last_time) {
      status_snapshots_frozen = 1;
      return;
    }
    status_snapshot(&status_snapshot_flagged);
    command_log_snapshot = command_log;
    command_log_snapshot.read =
        command_log_snapshot.entries + (command_log.read - command_log.entries);
    command_log_snapshot.write = command_log_snapshot.entries +
                                 (command_log.write - command_log.entries);
  } else {
    status_snapshot(&status_snapshot_latest);
  }
  status_snapshot_last_time = time;
}

/* Clears the GPS task's counts, its last command and program sentence,
 * and the command log (0x58b5e). */
void clear_gps_debug(void) {
  memset(&gps_counts, 0, sizeof gps_counts);
  gps_last_command = 0;
  gps_last_program_message[0] = 0;
  command_log_clear();
}

/* Runs one of the task's own steps with its arguments (0x58690): the
 * 58503A's, the status snapshot the second of the screen's for a
 * setting's other sender, the instrument state through 0x53fca, and
 * this image's UTC selection and time and position arguments. */
void gps_console_request(gps_request_code_word code, int32_t* arguments,
                         uint8_t sender) {
  switch (code) {
    case GPS_STEP_STATUS_SNAPSHOT:
      status_snapshot(sender == GPS_SENDER_SETTING_OTHER
                          ? &status_screen_snapshots[1]
                          : &status_screen_snapshots[0]);
      break;
    case GPS_STEP_PRINT_STATUS:
      gps_print_status(&gps.status);
      break;
    case GPS_STEP_PRINT_VISIBILITY:
      gps_print_visibility();
      break;
    case GPS_STEP_PRINT_EXTENSION:
      gps_print_extension();
      break;
    case GPS_STEP_PRINT_BEST_COMBINATIONS:
      gps_print_best_combinations();
      break;
    case GPS_STEP_PRINT_ID:
      gps_print_id();
      break;
    case GPS_STEP_PRINT_PPS:
      gps_print_1pps();
      break;
    case GPS_STEP_PRINT_INSTRUMENT_STATE:
      gps_print_instrument_state();
      break;
    case GPS_STEP_PRINT_TIME_RAIM:
      gps_print_time_raim();
      break;
    case GPS_STEP_PRINT_AVERAGE:
      gps_print_average();
      break;
    case GPS_STEP_AVERAGE_RESET:
      gps.average_count = 0;
      break;
    case GPS_STEP_GPS_HYSTERESIS:
      gps_reference_hysteresis_set(arguments[0]);
      break;
    case GPS_STEP_EXTERNAL_HYSTERESIS:
      ext_reference_hysteresis_set(arguments[0]);
      break;
    case GPS_STEP_TRACKING_LOG:
      gps.tracking_log = (uint8_t)arguments[0];
      if (gps.tracking_log == 0) {
        gps.tracking_log_active = 0;
      }
      break;
    case GPS_STEP_SURVEY_START:
      gps.surveying = 1;
      break;
    case GPS_STEP_SURVEY_STOP:
      gps.surveying = 0;
      break;
    case GPS_STEP_PPS_CHECK:
      gps.pps_check_disabled = (uint8_t)arguments[0];
      break;
    case GPS_STEP_RAIM_ARGUMENTS:
      raim_request_arguments();
      break;
    case GPS_STEP_STATUS_MONITOR:
      gps.status_monitor_enabled = (uint8_t)arguments[0];
      break;
    case GPS_STEP_LINK_RESET:
      gps_link_reset();
      break;
    case GPS_STEP_UTC_SELECT:
      gps.utc_time = (oncore_time_mode)arguments[0];
      nmea_tps_time_commit(&gps.tps, &gps);
      break;
    case GPS_STEP_TIME_ARGUMENTS:
      gps_step_time_arguments();
      break;
    case GPS_STEP_DATE_ARGUMENTS:
      gps_step_date_arguments();
      break;
    case GPS_STEP_LATITUDE_ARGUMENTS:
      gps_step_position_arguments(GPS_STEP_LATITUDE);
      break;
    case GPS_STEP_LONGITUDE_ARGUMENTS:
      gps_step_position_arguments(GPS_STEP_LONGITUDE);
      break;
    default:
      break;
  }
}

/* How many times gpsm_task sends a step before it gives up on it. */
static const uint8_t GPS_TASK_SEND_TRIES = 3;

/* Decodes a complete sentence, whose result the task does not read,
 * and starts framing the next (0x57f98 to 0x57fae). */
static void gps_task_read_sentence(void) {
  nmea_sentence_decode((nmea_frame*)gps_response_buffer);
  gps_response_reset();
  gps_counts.responses_read++;
}

/* Starts the ring's oldest request: kept as received, then translated
 * to the GT-74's (0x57fd2 to 0x57fe6); one that translates to none is
 * answered at once.  A message runs as the one-step script
 * gps_script_message, a script as 0x57b2a finds it, each with
 * gps_request_step's arguments from the request's, unless commands are
 * blocked, and a step of the task's own at once (0x58028). */
static void gps_task_start(gps_running_command* command,
                           const gps_request_entry* next, uint8_t* tries) {
  static const uint8_t ARGUMENTS =
      sizeof gps_request_step.arguments / sizeof gps_request_step.arguments[0];

  command->request = *next;
  command->request.untranslated_code = command->request.code;
  command->request.untranslated_argument_mode = command->request.argument_mode;
  gps_request_translate(&command->request.code,
                        &command->request.argument_mode);
  if (command->request.code == GPS_REQUEST_NONE) {
    gps_count_response(GPS_RESULT_SUCCESS, &command->request);
    gps_counts.field_0e++;
    return;
  }
  switch (gps_request_class(command->request.code,
                            command->request.argument_mode)) {
    case GPS_CLASS_MESSAGE:
      if (gps_commands_blocked != 0) {
        break;
      }
      gps_request_step.code = command->request.code;
      gps_request_step.argument_mode = command->request.argument_mode;
      for (uint8_t i = 0; i < ARGUMENTS; i++) {
        gps_request_step.arguments[i] = command->request.arguments[i];
      }
      command->script = gps_script_message;
      *tries = 0;
      gps_task_state = GPS_PROCESS_SEND_COMMAND;
      gps_last_command = command;
      return;
    case GPS_CLASS_SCRIPT:
      if (gps_commands_blocked != 0) {
        break;
      }
      for (uint8_t i = 0; i < ARGUMENTS; i++) {
        gps_request_step.arguments[i] = command->request.arguments[i];
      }
      command->script = gps_script_lookup(command->request.code);
      *tries = 0;
      gps_task_state = GPS_PROCESS_SEND_COMMAND;
      gps_last_command = command;
      return;
    case GPS_CLASS_STEP:
      gps_console_request(command->request.code, command->request.arguments,
                          command->request.sender);
      break;
    default:
      gps_last_command = command;
      return;
  }
  gps_count_response(GPS_RESULT_SUCCESS, &command->request);
  gps_last_command = command;
}

/* Runs the script's next step: builds and sends its sentence and waits
 * for the answer, unless its descriptor matches none, keeping the
 * setting a query asks for in gps_periodic.query_code (0x58330 to
 * 0x58350); runs a step of the task's own; or, at the script's end, or
 * for a sentence that cannot be built, ends the request. */
static void gps_task_send(gps_running_command* command, uint32_t* sent_time,
                          uint8_t* tries) {
  gps_script_step* step = *command->script;
  gps_argument_mode mode;
  int32_t* arguments;

  if (step == NULL) {
    gps_count_response(GPS_RESULT_SUCCESS, &command->request);
    gps_task_state = GPS_PROCESS_TASK_IDLE;
    return;
  }
  const gps_request_code_word code = step->code;

  if (gps_request_class(code, step->argument_mode) == GPS_CLASS_STEP) {
    gps_console_request(code, step->arguments, command->request.sender);
    command->script++;
    return;
  }
  mode = step->argument_mode;
  if (mode == GPS_ARGUMENTS_FROM_REQUEST) {
    mode = GPS_ARGUMENTS_SET;
    arguments = &gps_request_step.arguments[long_array_get(step->arguments, 0)];
  } else {
    arguments = step->arguments;
  }
  const int16_t length =
      gps_request_build(code, mode, arguments, gps_command_message);
  if (length < 0) {
    command->script++;
  } else if (length == 0) {
    if (command->request.argument_mode == GPS_ARGUMENTS_SET) {
      gps_count_response(GPS_RESULT_NO_COMMAND, &command->request);
      libc_copy(gps_last_program_message, " // command not supported //\n");
    } else {
      gps_count_response(GPS_RESULT_NO_QUERY, &command->request);
      libc_copy(gps_last_program_message, " // query form not supported //\n");
    }
    gps_task_state = GPS_PROCESS_TASK_IDLE;
  } else {
    command->query = mode;
    command->message = code;
    command->length = (uint8_t)length;
    memcpy(command->sent, gps_command_message, length);
    drta_send_message((uint8_t*)gps_command_message, length);
    *sent_time = get_uptime();
    (*tries)++;
    gps_counts.commands_sent++;
    nmea_sentence_text(gps_command_message, (uint8_t)length,
                       gps_last_program_message);
    command_log_add(command, gps_last_program_message);
    if (gps_request_descriptor(code)->query != NULL) {
      gps_periodic.query_code = code;
    }
    if (gps_request_descriptor(code)->match_kind == GPS_SENTENCE_MATCH_NONE) {
      command->script++;
      *tries = 0;
    } else {
      gps_task_state = GPS_PROCESS_WAIT_FOR_RESPONSE;
    }
  }
}

/* Waits for the answer to the sentence sent: on it, goes on to the next
 * step; on a response that differs, or a timeout, sends again, up to
 * GPS_TASK_SEND_TRIES in all, then gives the request up; a sentence no
 * response answers, and the second fix algorithm's after its third try,
 * is passed over at its timeout (0x582ac to 0x583a0).  The response's
 * length it matches with is the word at 0x10170e, which nothing here
 * sets (docs/firmware/quirks.md). */
static void gps_task_wait(gps_running_command* command, bool complete,
                          uint32_t* sent_time, uint8_t* tries) {
  const uint8_t timed_out = gps_response_timed_out(command->message, sent_time);

  if (timed_out == 1) {
    if (gps_request_descriptor(command->message)->match_kind ==
            GPS_SENTENCE_MATCH_TIMEOUT ||
        (command->message == GPS_REQUEST_FIX_ALGORITHM_SECOND &&
         *tries > GPS_TASK_SEND_TRIES - 1)) {
      command->script++;
      *tries = 0;
      gps_task_state = GPS_PROCESS_SEND_COMMAND;
    } else if (*tries < GPS_TASK_SEND_TRIES) {
      gps_task_state = GPS_PROCESS_SEND_COMMAND;
      gps_counts.t_retries++;
    } else {
      gps.command_monitor_error = 1;
      gps_count_response(GPS_RESULT_TIMEOUT, &command->request);
      gps_task_state = GPS_PROCESS_TASK_IDLE;
    }
  }
  if (!complete) {
    return;
  }
  nmea_sentence_decode((nmea_frame*)gps_response_buffer);
  gps_counts.responses_read++;
  if (timed_out == 0) {
    switch (gps_response_match(
        command->message, command->sent, command->length, command->query,
        gps_response_index, (nmea_frame*)gps_response_buffer, DAT_0010170e)) {
      case GPS_MATCH_ANSWERED:
        gps.command_monitor_error = 0;
        command->script++;
        *tries = 0;
        gps_task_state = GPS_PROCESS_SEND_COMMAND;
        break;
      case GPS_MATCH_DIFFERS:
        if (*tries < GPS_TASK_SEND_TRIES) {
          gps_counts.e_retries++;
          gps_task_state = GPS_PROCESS_SEND_COMMAND;
        } else {
          gps.command_monitor_error = 1;
          gps_count_response(GPS_RESULT_REJECT, &command->request);
          gps_task_state = GPS_PROCESS_TASK_IDLE;
        }
        break;
      default:
        break;
    }
  }
  gps_response_reset();
}

/* The GPS task, gpsm: queues each request gpsx_exchange brings, runs the
 * ring's oldest a step at a time, and between them updates the command
 * monitor and the receiver status and prints the status screen every
 * status_screen_print_interval seconds, once for a negative one
 * (0x57e3c).  The 58503A 3704's over the GT-74's sentences.  While it
 * waits for an answer after more than one try, it runs a waiting status
 * snapshot request at once (0x57f42).  Its count of tries, D3, starts as
 * the task's entry left it: the image tests it before any sentence sets
 * it (docs/firmware/quirks.md). */
void gpsm_task(void) {
  static const uint32_t REQUEST_WAIT_TICKS = 10;
  uint8_t tries = CPU_CALLERS_REGISTER(uint32_t, "d3");
  /* Even, since gcc reads their words as such, misaligned at an odd
   * frame offset (MC68331UM, 4.4.4) and the type packed. */
  gps_request_entry __attribute__((aligned(2))) received;
  gps_request_entry __attribute__((aligned(2))) next;
  gps_running_command command;
  uint32_t sent_time;
  uint32_t screen_time;

  gps_link_reset();
  clear_gps_debug();
  command_log_clear();
  status_snapshots_frozen = 0;
  status_snapshot_last_time = 0;
  for (;;) {
    bool taken = false;

    watchdog_gps++;
    const uint8_t error = (uint8_t)psos_request_x(gpsx_exchange, &received, 0,
                                                  REQUEST_WAIT_TICKS);
    if (error == PSOS_ERROR_NONE) {
      gps_counts.received++;
      if (request_kind_valid(received.sender) == 0) {
        gps_count_response(GPS_RESULT_UNKNOWN_SENDER, &received);
      } else if (gps_request_class(received.code, received.argument_mode) ==
                 GPS_CLASS_INVALID) {
        gps_count_response(GPS_RESULT_UNKNOWN_COMMAND, &received);
      } else if (gps_request_admitted(received.sender, received.code) == 0) {
        gps_count_response(GPS_RESULT_SUCCESS, &received);
        gps_counts.field_0e++;
      } else {
        gps_request_queue(gps_task_state, &received, &command.request);
      }
    } else if (error != PSOS_ERROR_TIMEOUT) {
      gps_counts.received_errors++;
    }
    if (gps_task_state == GPS_PROCESS_TASK_IDLE && !request_ring_empty()) {
      request_ring_get(&next);
      taken = true;
    } else if (gps_task_state == GPS_PROCESS_WAIT_FOR_RESPONSE &&
               !request_ring_empty() && tries > 1) {
      gps_request_run_queued(GPS_STEP_STATUS_SNAPSHOT);
    }
    const bool complete = gps_receive() == GPS_FRAMING_COMPLETE;
    switch (gps_task_state) {
      case GPS_PROCESS_TASK_IDLE:
        if (complete) {
          gps_task_read_sentence();
        }
        if (taken) {
          gps_task_start(&command, &next, &tries);
        }
        break;
      case GPS_PROCESS_SEND_COMMAND:
        if (complete) {
          gps_task_read_sentence();
        }
        gps_task_send(&command, &sent_time, &tries);
        break;
      case GPS_PROCESS_WAIT_FOR_RESPONSE:
        gps_task_wait(&command, complete, &sent_time, &tries);
        break;
      default:
        break;
    }
    command_monitor();
    gps_receiver_status_update(GPS_RECEIVER_ONCORE, 0);
    gps_receiver_status_update(GPS_RECEIVER_ALTERNATE,
                               (char)gps_alternate_status());
    if (status_screen_print_interval != 0) {
      int32_t elapsed;

      uptime_elapsed(&screen_time, &elapsed);
      if (status_screen_print_interval < 0 ||
          (uint32_t)elapsed >= (uint32_t)status_screen_print_interval) {
        status_snapshot(&status_screen_snapshots[0]);
        status_screen(&status_screen_snapshots[0], 1, console_write,
                      (printf_function*)printf);
        screen_time = get_uptime();
        if (status_screen_print_interval < 0) {
          status_screen_print_interval = abs(status_screen_print_interval);
        }
      }
    }
  }
}
