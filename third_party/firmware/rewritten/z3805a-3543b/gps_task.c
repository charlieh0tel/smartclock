/* The GPS task, gpsm: its state's start, the link's reset, and the steps
 * of its own a request or script runs in place of a message to the
 * engine (docs/firmware/gps.md; docs/firmware/structures.md).  Written
 * by hand in place of the decompiler's (docs/firmware/README.md,
 * "Hand-written C"; docs/firmware/modules.md, `applications/gps/task`).
 * The exported C includes this file after its own, so the types and
 * prototypes it declares are in scope; each function here keeps its
 * generated prototype. */

/* Clears gps and resets the receiver status and the command monitor
 * (0x4bef6). */
void gps_init(void) {
  memset(&gps, 0, sizeof gps);
  gps.field_504[0] = 0; /* again, as the image does */
  gps_commands_blocked = 0;
  gps_receiver_status_reset();
  gps.reference_valid_override = 0;
  status_screen_print_interval = 0;
  gps.pps_check_disabled = 0;
  reference_valid_override_request = 0;
  command_monitor_init();
}

/* Idles the task, resets the response framing and empties the request
 * ring (0x4bf3a). */
void gps_link_reset(void) {
  gps_task_state = GPS_PROCESS_TASK_IDLE;
  gps_response_reset();
  request_ring_clear();
}

/* Clears the link's counters, the last command and message and the
 * command log: the console's clr_gps_debug (0x4c918). */
void clear_gps_debug(void) {
  memset(&gps_counts, 0, sizeof gps_counts);
  gps_last_command = 0;
  gps_last_program_message[0] = 0;
  command_log_clear();
}

/* Fills gps_request_step's arguments for @@Bn, the Time RAIM setup:
 * each second, the algorithm on or off and its alarm limit as set for
 * position hold or not, and the 1PPS on all the time (VPCommands.pdf,
 * Time RAIM Setup and Status) (0x4c4e8). */
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

/* Runs one of the task's own steps, GPS_STEP_STATUS_SNAPSHOT to
 * GPS_STEP_LINK_RESET, with its arguments; any other does nothing
 * (0x4c572). */
void gps_console_request(int16_t code, int32_t* arguments, uint8_t sender) {
  switch (code) {
    case GPS_STEP_STATUS_SNAPSHOT:
      status_snapshot(&status_screen_snapshot);
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
    case GPS_STEP_SURVEY_START:
      gps.surveying = 1;
      break;
    case GPS_STEP_SURVEY_STOP:
      gps.surveying = 0;
      break;
    case GPS_STEP_PPS_CHECK:
      gps.pps_check_disabled = (uint8_t)arguments[0];
      if (gps.pps_check_disabled == 1) {
        gps_receiver_status_reset();
      }
      break;
    case GPS_STEP_RAIM_ARGUMENTS:
      raim_request_arguments();
      break;
    case GPS_STEP_PARSE_OPTIONS:
      gps_parse_options();
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

/* Takes a status snapshot at each @@Ba into status_snapshot_latest, or
 * at 03:14:07 and 06:28:15, pi and two pi, into status_snapshot_flagged
 * with a copy of the command log; stops for good when one of those
 * comes twice running (0x4c870). */
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

/* Decodes a complete response and starts framing the next. */
static void gps_task_read_response(void) {
  gps_response_decode(gps_response_index, (char*)gps_response_buffer);
  gps_response_reset();
  gps_counts.responses_read++;
}

/* Takes the next request from the ring when idle: a message, run as
 * gps_script_message's one step, or a script, unless commands are
 * blocked, each from the step after; or a step of the task's own, run
 * at once.  A request not run is counted done. */
static void gps_task_start(gps_running_command* command,
                           const gps_request_entry* next, uint8_t* tries,
                           uint8_t* rejected_length) {
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
    *rejected_length = 0;
    gps_task_state = GPS_PROCESS_SEND_COMMAND;
  } else {
    gps_count_response(GPS_RESULT_SUCCESS, &command->request);
  }
  gps_last_command = command;
}

/* Runs the script's next step: sends its message and waits for the
 * answer, unless it needs none; runs a step of the task's own; or, at
 * the script's end, or for a message that cannot be sent, ends the
 * request (0x4c188). */
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
  if (step->code >= GPS_STEP_STATUS_SNAPSHOT &&
      step->code < GPS_SCRIPT_INIT_DEFAULTS) {
    gps_console_request(step->code, step->arguments, command->request.sender);
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
  length = gps_request_build(step->code, mode, arguments,
                             (char*)gps_command_message);
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
    command->message = step->code;
    command->query = mode;
    command->length = (uint8_t)length;
    memcpy(command->sent, gps_command_message, length);
    drta_send_message((char*)gps_command_message, length);
    *sent_time = get_uptime();
    (*tries)++;
    gps_counts.commands_sent++;
    gps_message_to_text((char*)gps_command_message, (uint8_t)length,
                        gps_last_program_message);
    command_log_add(command, gps_last_program_message);
    if (oncore_messages[step->code].match_kind == ONCORE_MATCH_ANY) {
      command->script++;
    } else {
      gps_task_state = GPS_PROCESS_WAIT_FOR_RESPONSE;
    }
  }
}

/* Waits for the answer to the message sent: on it, goes on to the next
 * step; on a response that differs, sends again, unless it is the one
 * that differed last, a rejection; and sends again on a timeout, up to
 * SEND_TRIES in all (0x4c31a). */
static void gps_task_wait(gps_running_command* command, bool complete,
                          uint32_t* sent_time, uint8_t* tries,
                          uint8_t* rejected_length, char* rejected,
                          uint8_t rejected_size) {
  static const uint8_t SEND_TRIES = 3;
  const uint32_t timed_out =
      gps_response_timed_out(command->message, sent_time);

  if (timed_out == 1) {
    if (*tries < SEND_TRIES) {
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
  gps_response_decode(gps_response_index, (char*)gps_response_buffer);
  gps_counts.responses_read++;
  if (timed_out == 0) {
    switch (gps_response_match(command->message, command->sent, command->length,
                               command->query, gps_response_index,
                               gps_response_buffer, gps_response_length)) {
      case GPS_MATCH_ANSWERED:
        gps.command_monitor_error = 0;
        command->script++;
        *tries = 0;
        *rejected_length = 0;
        gps_task_state = GPS_PROCESS_SEND_COMMAND;
        break;
      case GPS_MATCH_DIFFERS:
        if (bytes_equal_n((char*)gps_response_buffer, gps_response_length,
                          rejected, *rejected_length) == 1) {
          gps.command_monitor_error = 1;
          gps_count_response(GPS_RESULT_REJECT, &command->request);
          gps_task_state = GPS_PROCESS_TASK_IDLE;
        } else {
          *rejected_length = gps_response_length < rejected_size
                                 ? (uint8_t)gps_response_length
                                 : rejected_size;
          memcpy(rejected, gps_response_buffer, *rejected_length);
          gps_task_state = GPS_PROCESS_SEND_COMMAND;
          gps_counts.e_retries++;
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
 * receiver status and the command monitor and prints the status screen
 * every status_screen_print_interval seconds, once for a negative one
 * (0x4bf4a). */
void gpsm_task(void) {
  static const uint32_t REQUEST_WAIT_TICKS = 10;
  gps_request_entry received;
  gps_request_entry next;
  gps_running_command command;
  uint32_t sent_time;
  uint8_t tries;
  uint8_t rejected_length;
  /* The start of the last response that differed from its command. */
  char rejected[25];
  uint32_t screen_time;

  gps_link_reset();
  clear_gps_debug();
  command_log_clear();
  status_snapshots_frozen = 0;
  status_snapshot_last_time = 0;
  for (;;) {
    bool taken;
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
      } else {
        gps_request_queue(gps_task_state, &received, &command.request);
      }
    } else if (error != PSOS_ERROR_TIMEOUT) {
      gps_counts.received_errors++;
    }
    taken = gps_task_state == GPS_PROCESS_TASK_IDLE && !request_ring_empty();
    if (taken) {
      request_ring_get(&next);
    }
    complete = gps_receive() == GPS_FRAMING_COMPLETE;
    switch (gps_task_state) {
      case GPS_PROCESS_TASK_IDLE:
        if (complete) {
          gps_task_read_response();
        }
        if (taken) {
          gps_task_start(&command, &next, &tries, &rejected_length);
        }
        break;
      case GPS_PROCESS_SEND_COMMAND:
        if (complete) {
          gps_task_read_response();
        }
        gps_task_send(&command, &sent_time, &tries);
        break;
      case GPS_PROCESS_WAIT_FOR_RESPONSE:
        gps_task_wait(&command, complete, &sent_time, &tries, &rejected_length,
                      rejected, sizeof rejected);
        break;
      default:
        break;
    }
    gps_receiver_status_update(GPS_RECEIVER_ONCORE, 0);
    gps_receiver_status_update(GPS_RECEIVER_ALTERNATE, gps_status_alternate());
    command_monitor();
    if (status_screen_print_interval != 0) {
      int32_t elapsed;

      uptime_elapsed(&screen_time, &elapsed);
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
