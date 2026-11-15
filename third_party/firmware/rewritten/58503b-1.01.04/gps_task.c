/* From the Z3816A 4001's rewritten/gps_task.c, the 58503B 1.01.04's own
 * beside common/'s: of its functions, those the 58503B's code carries
 * identical or relocated (docs/firmware/58503b-1.01.04.md, "The link list").
 * Its comments' addresses are the 58503B's, but where they name another
 * image.  Each keeps its generated prototype. */

/* Fills gps_request_step's arguments for @@Bn, the Time RAIM setup:
 * each second, the algorithm on or off and its alarm limit as set for
 * position hold or not, and the 1PPS on all the time (VPCommands.pdf,
 * Time RAIM Setup and Status) (0x5353a). */
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

/* Clears gps, its two references' qualification, set to the NV
 * settings' hysteresis, the command monitor and the antenna sense, points
 * the poll monitor at its first step, takes the tracking log from the NV
 * settings and clears sky_statistics, where the 58503A's has no poll monitor
 * (0x52e5c). */
void gps_init(void) {
  memset(&gps, 0, sizeof gps);
  gps.field_56c = 0; /* again, as the image does */
  gps_commands_blocked = 0;
  gps.time_status = GPS_TIME_STATUS_UNTRACKED;
  gps_reference_reset(0);
  gps_reference_reset(1);
  gps_reference_hysteresis_set(nv_settings.questionable_hysteresis);
  ext_reference_hysteresis_set(nv_settings.external_questionable_hysteresis);
  gps.references[0].override = GPS_REFERENCE_OVERRIDE_NONE;
  gps.references[1].override = GPS_REFERENCE_OVERRIDE_NONE;
  status_screen_print_interval = 0;
  gps.pps_check_disabled = 0;
  command_monitor_init();
  gps.antenna_sense = GPS_ANTENNA_NORMAL;
  gps.status_monitor_failed = 0;
  gps.field_5fd = GPS_RECEIVER_POLL_IDLE;
  gps.field_5fe = (uint32_t)poll_monitor_queries;
  gps.tracking_log = nv_settings.tracking_log;
  gps.tracking_log_active = 0;
  sky_statistics_clear();
}

/* Runs one of the task's own steps with its arguments: the status
 * snapshot the second of the screen's for a setting's other sender; the
 * survey steps on gps.field_5bf and field_5c0, where the 58503A's sets
 * gps.surveying; gps.surveying cleared for a "B" engine alone; the 1PPS
 * check's fix mode taken from it for any other engine; and the status
 * monitor's failure flag (0x535c4). */
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
      gps_print_sat_view();
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
      gps.field_5bf = 1;
      break;
    case GPS_STEP_SURVEY_STOP:
      gps.field_5bf = 0;
      gps.field_5c0 = 0;
      break;
    case GPS_STEP_FIELD_5C0_SET:
      gps.field_5c0 = 1;
      break;
    case GPS_STEP_SURVEYING_CLEAR:
      if ((gps.field_608 & GPS_FIELD_608_SIGNAL_SS) != 0) {
        gps.surveying = 0;
      }
      break;
    case GPS_STEP_PPS_CHECK:
      gps.pps_check_disabled = (uint8_t)arguments[0];
      if ((gps.field_608 & GPS_FIELD_608_SIGNAL_SS) == 0) {
        gps.fix_mode = gps.pps_check_disabled == 0;
      }
      break;
    case GPS_STEP_RAIM_ARGUMENTS:
      raim_request_arguments();
      break;
    case GPS_STEP_STATUS_MONITOR:
      gps.status_monitor_enabled = (uint8_t)arguments[0];
      break;
    case GPS_STEP_MONITOR_5FC:
      gps.status_monitor_failed = (uint8_t)arguments[0];
      break;
    case GPS_STEP_LINK_RESET:
      gps_link_reset();
      break;
    default:
      break;
  }
}

/* Decodes the response gps_receive has framed and expects the next. */
static void gps_task_read_response(void) {
  gps_response_decode(gps_response_buffer);
  gps_response_reset();
  gps_counts.responses_read++;
}

/* Starts the request the ring gave: a message runs as a one-step script
 * from gps_request_step, a script from its first step, each unless the
 * commands are blocked, and the task's own step at once; one not started
 * is answered done.  The command is gps_last_command after (0x530b2 to
 * 0x53198). */
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

/* Runs the script's next step: a step of the task's own at once; a
 * message built and sent, which waits for its answer unless its match
 * kind wants none, or is an @@Bo the engine's kind does not answer; at
 * the script's end, the request answered done (0x531be to 0x5335e). */
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
  if (length > 0) {
    oncore_match_kind kind;

    command->message = code;
    command->query = mode;
    command->length = (uint8_t)length;
    memcpy(command->sent, gps_command_message, length);
    acia_send_message(gps_command_message, length);
    *sent_time = get_uptime();
    (*tries)++;
    gps_counts.commands_sent++;
    gps_message_to_text(gps_command_message, (uint8_t)length,
                        gps_last_program_message);
    command_log_add((int32_t)command, (int32_t)gps_last_program_message);
    kind = oncore_message_entry_at(code)->match_kind;
    if (kind == ONCORE_MATCH_ANY ||
        (kind == ONCORE_MATCH_SAME_SIGNAL_CN &&
         ((gps.field_608 & GPS_FIELD_608_SIGNAL_SS) != 0 ||
          (gps.field_608 & GPS_FIELD_608_SIGNAL_CN) == 0))) {
      command->script++;
      *tries = 0;
    } else {
      gps_task_state = GPS_PROCESS_WAIT_FOR_RESPONSE;
    }
  } else if (length < 0) {
    command->script++;
  } else {
    if (command->request.argument_mode == GPS_ARGUMENTS_SET) {
      gps_count_response(GPS_RESULT_NO_COMMAND, &command->request);
      libc_copy(gps_last_program_message, " // command not supported //\n");
    } else {
      gps_count_response(GPS_RESULT_NO_QUERY, &command->request);
      libc_copy(gps_last_program_message, " // query form not supported //\n");
    }
    gps_task_state = GPS_PROCESS_TASK_IDLE;
  }
}

/* Waits for the message's answer: after its timeout, the next step for
 * ONCORE_MATCH_TIMEOUT, or for the second @@Ea after its three tries;
 * else sent again, three tries in all, then the request timed out.  A
 * response within the timeout that answers takes the next step; one
 * that differs sends again, three tries in all, then rejects the
 * request, where the 58503A's rejects the same wrong response twice
 * (0x53362 to 0x53464). */
static void gps_task_wait(gps_running_command* command, bool complete,
                          uint32_t* sent_time, uint8_t* tries) {
  static const uint8_t SEND_TRIES = 3;
  const uint8_t timed_out = gps_response_timed_out(command->message, sent_time);

  if (timed_out == 1) {
    if (oncore_message_entry_at(command->message)->match_kind ==
            ONCORE_MATCH_TIMEOUT ||
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
 * command monitor, the poll monitor and the receiver status, counts the
 * tracking log down to sky_statistics_update, and prints the status screen
 * every status_screen_print_interval seconds, once for a negative one
 * (0x52f30).  While it waits for an answer after more than one try, it
 * runs a waiting status snapshot request at once (0x53042).  Its count
 * of tries, D3, starts as the task's entry left it: the image tests it
 * before any message sets it (docs/firmware/quirks.md).  The 58503A's,
 * but where its helpers say otherwise. */
void gpsm_task(void) {
  static const uint32_t REQUEST_WAIT_TICKS = 10;
  /* tracking_log_countdown's last pass, at which the log is written. */
  static const uint8_t TRACKING_LOG_DUE = 1;
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
  tracking_log_countdown = 0;
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
    poll_monitor();
    gps_receiver_status_update(GPS_RECEIVER_ONCORE, 0);
    gps_receiver_status_update(GPS_RECEIVER_ALTERNATE, gps_status_alternate());
    if (nv_settings.tracking_log != 0) {
      if (tracking_log_countdown == TRACKING_LOG_DUE) {
        sky_statistics_update();
      }
      if (tracking_log_countdown != 0) {
        tracking_log_countdown--;
      }
    } else {
      tracking_log_countdown = 0;
    }
    if (status_screen_print_interval != 0) {
      int32_t elapsed;

      uptime_elapsed(&screen_time, 1, &elapsed);
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

/* Clears the link's counters, the RAIM alarm's date, the last command and
 * message and the command log: the console's clr_gps_debug (0x53918). */
void clear_gps_debug(void) {
  memset(&gps_counts, 0, sizeof gps_counts);
  gps.raim_alarm_date = 0;
  gps_last_command = 0;
  gps_last_program_message[0] = 0;
  command_log_clear();
}
