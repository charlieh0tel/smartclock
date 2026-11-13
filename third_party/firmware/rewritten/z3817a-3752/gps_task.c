/* The GPS task, gpsm, the Z3817A 3752's own beside common/'s: its
 * state's start, the steps of its own a request or script runs in place
 * of a message to the engine, and the task, from the 58503A 3704's
 * rewritten/gps_task.c, whose Oncore code this image's is, each written
 * as this image's where its code differs (docs/firmware/z3817a-3752.md,
 * "The export and the checks").  Its comments' addresses are the
 * Z3817A's, but where they name another image.  Each keeps its generated
 * prototype. */

/* Takes a status snapshot at each @@Ba into status_snapshot_latest, or
 * at 03:14:07 and 06:28:15, pi and two pi, into status_snapshot_flagged
 * with a copy of the command log; stops for good when one of those
 * comes twice running (0x5346a). */
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

/* Fills gps_request_step's arguments for @@Bn, the Time RAIM setup:
 * each second, the algorithm on or off and its alarm limit as set for
 * position hold or not, and the 1PPS on all the time (VPCommands.pdf,
 * Time RAIM Setup and Status) (0x5309e). */
void raim_request_arguments(void) {
  static const int32_t EACH_SECOND = 1;
  static const int32_t PULSE_ALWAYS = 1;
  /* nv_settings' RAIM settings in position hold, and out of it. */
  static const uint8_t IN_HOLD = 0;
  static const uint8_t OUT_OF_HOLD = 1;
  uint8_t setting = gps.position_hold == 1 ? IN_HOLD : OUT_OF_HOLD;

  long_array_set(EACH_SECOND, gps_request_step.arguments, 0);
  long_array_set((int8_t)nv_settings.raim_state[setting],
                 gps_request_step.arguments, 1);
  long_array_set(nv_settings.raim_threshold[setting],
                 gps_request_step.arguments, 2);
  long_array_set(PULSE_ALWAYS, gps_request_step.arguments, 3);
}

/* Restarts the GPS task's state, gps_init, for a switch of reference
 * (0x52ab6). */
void gps_restart(void) { gps_init(); }

/* While the status monitor is on, counts each of gps.echo_ages up, to
 * 0xfe, in each new second of the uptime, which gps.echoes_aged keeps;
 * an uptime gone back resets the echoes (0x520f6). */
void gps_echoes_age(void) {
  static const uint8_t AGES = sizeof gps.echo_ages / sizeof gps.echo_ages[0];
  static const uint8_t AGE_MOST = 0xfe;
  uint32_t uptime;

  if (gps.status_monitor_enabled == 0) {
    return;
  }
  uptime = get_uptime();
  if (uptime < gps.echoes_aged) {
    gps.echoes_aged = uptime;
    gps_echoes_reset();
  } else if (uptime != gps.echoes_aged) {
    gps.echoes_aged = uptime;
    for (uint8_t i = 0; i < AGES; i++) {
      if (gps.echo_ages[i] < AGE_MOST) {
        gps.echo_ages[i]++;
      }
    }
  }
}

/* Decodes a complete response and starts framing the next. */
static void gps_task_read_response(void) {
  gps_response_decode(gps_response_buffer);
  gps_response_reset();
  gps_counts.responses_read++;
}

/* Clears gps and its two references' qualification, set to the NV
 * settings' hysteresis; presets what the engine's echoes would give, fix
 * mode, position hold, a status rate of a second and the echoes heard,
 * and gps.field_578, which the GPS reference wants; the monitors, the
 * channel count, and the tracking log from the NV settings (0x52a16). */
void gps_init(void) {
  static const uint8_t STATUS_RATE = 1; /* s */

  memset(&gps, 0, sizeof gps);
  gps.field_525 = 0; /* again, as the image does */
  gps.field_578 = 1;
  gps_commands_blocked = 0;
  gps_reference_reset(0);
  gps_reference_reset(1);
  gps_reference_hysteresis_set(nv_settings.questionable_hysteresis);
  ext_reference_hysteresis_set(nv_settings.external_questionable_hysteresis);
  gps.references[0].override = GPS_REFERENCE_OVERRIDE_NONE;
  gps.references[1].override = GPS_REFERENCE_OVERRIDE_NONE;
  gps.fix_mode = ONCORE_POSITION_FIX;
  gps.position_hold = 1;
  status_screen_print_interval = 0;
  gps.pps_check_disabled = 0;
  command_monitor_init();
  gps_channel_count_update();
  gps.status_rate = STATUS_RATE;
  gps_echoes_reset();
  gps.tracking_log = nv_settings.tracking_log;
  gps.tracking_log_active = 0;
}

/* Clears the link's counters, the last command and message and the
 * command log: the console's clr_gps_debug (0x53512). */
void clear_gps_debug(void) {
  memset(&gps_counts, 0, sizeof gps_counts);
  gps_last_command = 0;
  gps_last_program_message[0] = 0;
  command_log_clear();
}

/* Runs one of the task's own steps, GPS_STEP_STATUS_SNAPSHOT to
 * GPS_STEP_LINK_RESET, with its arguments; any other does nothing
 * (0x53128, its jump table at 0x53152).  The status snapshot goes to the
 * second of status_screen_snapshots for the SCI's sender, else to the
 * first (0x53184). */
void gps_console_request(gps_request_code_word code, int32_t* arguments,
                         uint8_t sender) {
  switch (code) {
    case GPS_STEP_STATUS_SNAPSHOT:
      status_snapshot(
          &status_screen_snapshots[sender == GPS_SENDER_SETTING_OTHER]);
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
      gps.field_577 = 0;
      break;
    case GPS_STEP_FIELD_577_SET:
      gps.field_577 = 1;
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
    default:
      break;
  }
}

/* Takes the next request from the ring when idle: a message, run as
 * gps_script_message's one step, or a script, unless commands are
 * blocked, each from the step after; or a step of the task's own, run
 * at once.  A request not run is counted done (0x52c6a to 0x52d5a). */
static void gps_task_start(gps_running_command* command,
                           const gps_request_entry* next, uint8_t* tries) {
  static const uint32_t ARGUMENTS =
      sizeof gps_request_step.arguments / sizeof gps_request_step.arguments[0];
  bool started = false;

  command->request = *next;
  switch (gps_request_class(next->code, next->argument_mode)) {
    case GPS_CLASS_MESSAGE:
      if (gps_commands_blocked == 0) {
        gps_request_step.code = command->request.code;
        gps_request_step.argument_mode = command->request.argument_mode;
        for (uint8_t i = 0; i < ARGUMENTS; i++) {
          gps_request_step.arguments[i] = command->request.arguments[i];
        }
        command->script = gps_script_message;
        started = true;
      }
      break;
    case GPS_CLASS_SCRIPT:
      if (gps_commands_blocked == 0) {
        for (uint8_t i = 0; i < ARGUMENTS; i++) {
          gps_request_step.arguments[i] = command->request.arguments[i];
        }
        command->script = gps_script_lookup(command->request.code);
        started = true;
      }
      break;
    case GPS_CLASS_STEP:
      gps_console_request(command->request.code, command->request.arguments,
                          command->request.sender);
      break;
    default:
      gps_last_command = command;
      return;
  }
  if (started) {
    *tries = 0;
    gps_task_state = GPS_PROCESS_SEND_COMMAND;
  } else {
    gps_count_response(GPS_RESULT_SUCCESS, &command->request);
  }
  gps_last_command = command;
}

/* Runs the script's next step: sends its message and waits for the
 * answer, unless it needs none; runs a step of the task's own; or, at
 * the script's end, or for a message that cannot be sent, ends the
 * request (0x52d80 to 0x52ef0). */
static void gps_task_send(gps_running_command* command, uint32_t* sent_time,
                          uint8_t* tries) {
  gps_script_step* step = *command->script;
  gps_argument_mode mode;
  int32_t* arguments;
  int16_t length;

  if (step == NULL) {
    gps_count_response(GPS_RESULT_SUCCESS, &command->request);
    gps_task_state = GPS_PROCESS_TASK_IDLE;
    return;
  }
  gps_request_code_word code = step->code;

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
  length = gps_request_build(code, mode, arguments, gps_command_message);
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
    command->message = code;
    command->query = mode;
    command->length = (uint8_t)length;
    memcpy(command->sent, gps_command_message, length);
    drta_send_message(gps_command_message, length);
    *sent_time = get_uptime();
    (*tries)++;
    gps_counts.commands_sent++;
    gps_message_to_text(gps_command_message, (uint8_t)length,
                        gps_last_program_message);
    command_log_add(command, gps_last_program_message);
    if (oncore_message_entry_at(code)->match_kind == ONCORE_MATCH_ANY) {
      command->script++;
    } else {
      gps_task_state = GPS_PROCESS_WAIT_FOR_RESPONSE;
    }
  }
}

/* Waits for the answer to the message sent: on it, goes on to the next
 * step; on a response that differs, or a timeout, sends again, up to
 * SEND_TRIES in all.  At a timeout a message of ONCORE_MATCH_PASS is
 * passed over at once, and the second @@Ea, which an engine may not
 * answer, after its last try (0x52ef4 to 0x52ff6).  The 58503A's ends
 * the request at a second response that differs alike. */
static void gps_task_wait(gps_running_command* command, bool complete,
                          uint32_t* sent_time, uint8_t* tries) {
  static const uint8_t SEND_TRIES = 3;
  const uint8_t timed_out = gps_response_timed_out(command->message, sent_time);

  if (timed_out == 1) {
    if (oncore_message_entry_at(command->message)->match_kind ==
            ONCORE_MATCH_PASS ||
        (command->message == GPS_REQUEST_POSITION_STATUS_8_SECOND &&
         *tries >= SEND_TRIES)) {
      command->script++;
      *tries = 0;
      gps_task_state = GPS_PROCESS_SEND_COMMAND;
    } else if (*tries < SEND_TRIES) {
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
  gps_response_decode(gps_response_buffer);
  gps_counts.responses_read++;
  if (timed_out == 0) {
    switch (gps_response_match(command->message, command->sent, command->length,
                               command->query, gps_response_index,
                               gps_response_buffer, gps_response_length)) {
      case GPS_MATCH_ANSWERED:
        gps.command_monitor_error = 0;
        command->script++;
        *tries = 0;
        gps_task_state = GPS_PROCESS_SEND_COMMAND;
        break;
      case GPS_MATCH_DIFFERS:
        if (*tries >= SEND_TRIES) {
          gps.command_monitor_error = 1;
          gps_count_response(GPS_RESULT_REJECT, &command->request);
          gps_task_state = GPS_PROCESS_TASK_IDLE;
        } else {
          gps_counts.e_retries++;
          gps_task_state = GPS_PROCESS_SEND_COMMAND;
        }
        break;
      default:
        break;
    }
  }
  gps_response_reset();
}

/* The GPS task, gpsm: queues each request gpsx_exchange brings, runs
 * the ring's oldest a step at a time, and between them updates the
 * monitors, the echoes' ages and both references' receiver status, and
 * prints the status screen every status_screen_print_interval seconds,
 * once for a negative one (0x52af8).  While it waits for an answer after
 * more than one try, it runs a waiting status snapshot request at once
 * (0x52c12).  Its count of tries, D3, starts as the task's entry left
 * it: the image tests it before any message sets it
 * (docs/firmware/quirks.md). */
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
    bool complete;
    uint8_t error;

    watchdog_gps++;
    error = (uint8_t)psos_request_x(gpsx_exchange, &received, 0,
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
    complete = gps_receive() == GPS_FRAMING_COMPLETE;
    switch (gps_task_state) {
      case GPS_PROCESS_TASK_IDLE:
        if (complete) {
          gps_task_read_response();
        }
        if (taken) {
          gps_task_start(&command, &next, &tries);
        }
        break;
      case GPS_PROCESS_SEND_COMMAND:
        if (complete) {
          gps_task_read_response();
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
    gps_echoes_age();
    gps_receiver_status_update(GPS_RECEIVER_ONCORE, 0);
    gps_receiver_status_update(GPS_RECEIVER_ALTERNATE, gps_alternate_status());
    if (status_screen_print_interval != 0) {
      int32_t elapsed;

      uptime_elapsed(&screen_time, 1, &elapsed);
      if (status_screen_print_interval < 0 ||
          (uint32_t)status_screen_print_interval <= (uint32_t)elapsed) {
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
