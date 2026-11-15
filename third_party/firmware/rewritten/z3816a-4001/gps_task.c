/* gps_task.c's functions that are this image's own: the rest are
 * common/gps_task.c's, the same in every image (docs/firmware/rewrite.md,
 * "Shared C").  From the 58503A 3704's rewritten/gps_task.c: of its functions,
 * those the Z3816A's code carries (identical or relocated by image_diff, or
 * changed with the same instructions), with the static helpers they use, each
 * the same here but where its comment says otherwise
 * (docs/firmware/z3816a-4001.md, "The link check").  Its comments' addresses
 * are the Z3816A's.  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype. */

/* Fills gps_request_step's arguments for @@Bn, the Time RAIM setup:
 * each second, the algorithm on or off and its alarm limit as set for
 * position hold or not, and the 1PPS on all the time (VPCommands.pdf,
 * Time RAIM Setup and Status) (0x511fc). */
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
 * settings' hysteresis, the command monitor, the antenna's sense and the
 * poll monitor, its list from its start; takes the tracking log from
 * the NV settings and clears the satellite view (0x50b1e).  The 58503A
 * 3704's but for the antenna, the poll monitor and the satellite
 * view. */
void gps_init(void) {
  memset(&gps, 0, sizeof gps);
  gps.field_56c[0] = 0; /* again, as the image does */
  gps_commands_blocked = 0;
  gps.time_status = GPS_TIME_STATUS_UNTRACKED;
  gps_reference_reset(0);
  gps_reference_reset(1);
  gps_reference_hysteresis_set(nv_settings.questionable_hysteresis);
  ext_reference_hysteresis_set(nv_settings.field_14);
  gps.references[0].override = GPS_REFERENCE_OVERRIDE_NONE;
  gps.references[1].override = GPS_REFERENCE_OVERRIDE_NONE;
  status_screen_print_interval = 0;
  gps.pps_check_disabled = 0;
  command_monitor_init();
  gps.antenna_sense = GPS_ANTENNA_SENSE_OK;
  gps.poll_monitor_enabled = 0;
  gps.poll_monitor_state = GPS_POLL_MONITOR_IDLE;
  gps.poll_monitor_place = poll_monitor_queries;
  gps.tracking_log = nv_settings.tracking_log;
  gps.tracking_log_active = 0;
  satellite_view_clear();
}

/* Clears the link's counters, the last Time RAIM alarm's record, the
 * last command and message and the command log: the console's
 * clr_gps_debug (0x515da).  The 58503A 3704's but for the alarm's
 * record. */
void clear_gps_debug(void) {
  memset(&gps_counts, 0, sizeof gps_counts);
  gps.raim_alarm_date = 0;
  gps_last_command = 0;
  gps_last_program_message[0] = 0;
  command_log_clear();
}

/* Prints a line through task_dmessage while gps_print_enabled is set;
 * nothing calls it (0x5025e). */
void gps_debug_print(char* text) {
  if (gps_print_enabled != 0) {
    task_dmessage(text);
  }
}

/* Runs one of the task's own steps, GPS_STEP_STATUS_SNAPSHOT to
 * GPS_STEP_LINK_RESET, with its arguments; any other does nothing
 * (0x51286).  The 58503A 3704's but for its own steps: the snapshot
 * :SYSTem:PRINt? asks for, sender 3's, into status_print_snapshot; the
 * satellite view's print; the survey's flag at +0x5c0; the fix seen
 * cleared, and the PPS check's fix mode, by the engine; and the poll
 * monitor; and no options parse, which @@Cj's decoder does. */
void gps_console_request(gps_request_code_word code, int32_t* arguments,
                         uint8_t sender) {
  switch (code) {
    case GPS_STEP_STATUS_SNAPSHOT:
      status_snapshot(sender == GPS_SENDER_SETTING_OTHER
                          ? &status_print_snapshot
                          : &status_screen_snapshot);
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
    case GPS_STEP_PRINT_SATELLITE_VIEW:
      satellite_view_print();
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
      gps.field_5c0 = 0;
      break;
    case GPS_STEP_FIELD_5C0_SET:
      gps.field_5c0 = 1;
      break;
    case GPS_STEP_FIX_SEEN_CLEAR:
      if ((gps.engine & GPS_ENGINE_VP) != 0) {
        gps.fix_seen = 0;
      }
      break;
    case GPS_STEP_PPS_CHECK: {
      const uint8_t disabled = (uint8_t)arguments[0];

      gps.pps_check_disabled = disabled;
      if ((gps.engine & GPS_ENGINE_VP) == 0) {
        gps.fix_mode = disabled != 0 ? ONCORE_IDLE : ONCORE_POSITION_FIX;
      }
      break;
    }
    case GPS_STEP_RAIM_ARGUMENTS:
      raim_request_arguments();
      break;
    case GPS_STEP_STATUS_MONITOR:
      gps.status_monitor_enabled = (uint8_t)arguments[0];
      break;
    case GPS_STEP_POLL_MONITOR:
      gps.poll_monitor_enabled = (uint8_t)arguments[0];
      break;
    case GPS_STEP_LINK_RESET:
      gps_link_reset();
      break;
    default:
      break;
  }
}

/* Decodes a complete response and starts framing the next. */
static void gps_task_read_response(void) {
  gps_response_decode(gps_response_buffer);
  gps_response_reset();
  gps_counts.responses_read++;
}

/* Takes the next request from the ring when idle: a message, run as
 * gps_script_message's one step, or a script, unless commands are
 * blocked, each from the step after; or a step of the task's own, run
 * at once.  A request not run is counted done (0x50d4e).  The 58503A
 * 3704's but that no response is kept to compare. */
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

/* Whether gpsm_task goes on at once after sending a message: one
 * answered at once, or @@Bo, which only a UT Plus answers, to any other
 * engine (0x50f98 to 0x50fc4). */
static bool gps_task_answer_awaited(gps_request_code_word code) {
  const oncore_match_kind kind = oncore_message_entry_at(code)->match_kind;

  return !(
      kind == ONCORE_MATCH_ANY ||
      (oncore_message_entry_at(code)->match_kind == ONCORE_MATCH_UTC_OFFSET &&
       ((gps.engine & GPS_ENGINE_VP) != 0 ||
        (gps.engine & GPS_ENGINE_UT_PLUS) == 0)));
}

/* Runs the script's next step: sends its message and waits for the
 * answer, unless it needs none; runs a step of the task's own; or, at
 * the script's end, or for a message that cannot be sent, ends the
 * request (0x50e64).  The 58503A 3704's but for the answers it does not
 * wait for (gps_task_answer_awaited), which start the next step's
 * tries afresh, and its messages' line ends. */
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
      libc_copy(gps_last_program_message, " // command not supported //\r\n");
    } else {
      gps_count_response(GPS_RESULT_NO_QUERY, &command->request);
      libc_copy(gps_last_program_message,
                " // query form not supported //\r\n");
    }
    gps_task_state = GPS_PROCESS_TASK_IDLE;
  } else {
    command->query = mode;
    command->message = code;
    command->length = (uint8_t)length;
    memcpy(command->sent, gps_command_message, length);
    drta_send_message(gps_command_message, length);
    *sent_time = get_uptime();
    (*tries)++;
    gps_counts.commands_sent++;
    gps_message_to_text(gps_command_message, (uint8_t)length,
                        gps_last_program_message);
    command_log_add(command, gps_last_program_message);
    if (gps_task_answer_awaited(code)) {
      gps_task_state = GPS_PROCESS_WAIT_FOR_RESPONSE;
    } else {
      command->script++;
      *tries = 0;
    }
  }
}

/* Waits for the answer to the message sent: on it, goes on to the next
 * step; on one that differs, sends again; and on a timeout sends again,
 * up to SEND_TRIES in all, passing over a message no answer is awaited
 * for (ONCORE_MATCH_WAIT), and the second @@Ea, which an engine may not
 * answer, once tried three times (0x51024).  The 58503A 3704's but that
 * a response that differs is sent again only SEND_TRIES times, not
 * until it differs the same way twice. */
static void gps_task_wait(gps_running_command* command, bool complete,
                          uint32_t* sent_time, uint8_t* tries) {
  static const uint8_t SEND_TRIES = 3;
  const uint8_t timed_out = gps_response_timed_out(command->message, sent_time);

  if (timed_out == 1) {
    if (oncore_message_entry_at(command->message)->match_kind ==
            ONCORE_MATCH_WAIT ||
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
        if (*tries < SEND_TRIES) {
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

/* The GPS task, gpsm: queues each request gpsx_exchange brings, runs
 * the ring's oldest a step at a time, and between them updates the
 * command and poll monitors and the receiver status, gathers the
 * satellite view as the tracking log's countdown ends, and prints the
 * status screen every status_screen_print_interval seconds, once for a
 * negative one (0x50bf2).  While it waits for an answer after more than
 * one try, it runs a waiting status snapshot request at once.  Its
 * count of tries, D3, starts as the task's entry left it: the image
 * tests it before any message sets it (docs/firmware/quirks.md).  The
 * 58503A 3704's but for the poll monitor, the satellite view, and its
 * tries (gps_task_wait). */
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
  gps_tracking_countdown = 0;
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
    if (nv_settings.tracking_log == 0) {
      gps_tracking_countdown = 0;
    } else {
      if (gps_tracking_countdown == 1) {
        satellite_view_gather();
      }
      if (gps_tracking_countdown != 0) {
        gps_tracking_countdown--;
      }
    }
    if (status_screen_print_interval != 0) {
      int32_t elapsed;

      uptime_elapsed(&screen_time, true, &elapsed);
      if (status_screen_print_interval < 0 ||
          (uint32_t)elapsed >= (uint32_t)status_screen_print_interval) {
        status_snapshot(&status_screen_snapshot);
        status_screen(&status_screen_snapshot, 1, console_write,
                      (printf_function*)printf);
        screen_time = get_uptime();
        if (status_screen_print_interval < 0) {
          status_screen_print_interval = abs(status_screen_print_interval);
        }
      }
    }
  }
}
