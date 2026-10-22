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
