/* pllp's control of the GPS engine: the client side of gpsx_exchange,
 * whose requests gpsm_task runs and acknowledges at plla_exchange, and
 * the sequences pllp_task steps once a second through it -- the
 * engine's initialization and survey, the leap second check and its
 * acknowledgment, the console's GPS commands and the GPS self-test
 * (docs/firmware/structures.md, "gps_request_entry").  Written by hand
 * in place of the decompiler's (docs/firmware/README.md, "Hand-written
 * C"; docs/firmware/modules.md, `applications/pll/gps_control`).  The
 * exported C includes this file after its own, so the types and
 * prototypes it declares are in scope; each function here keeps its
 * generated prototype. */

/* Ticks the self-test pauses between tries. */
static const uint32_t PLL_GPS_CONTROL_RETRY_TICKS = 0x400;

/* Sends a self-test request, trying while the send fails, up to 20
 * more times; the GPS's self-test failed if the tries ran out, even
 * when the last succeeded.  The arguments are the caller's unset
 * frame, as the image's are. */
static void pll_gps_control_selftest_send(int16_t code, int32_t* arguments) {
  uint8_t tries = 20;

  while (gpsx_send(code, GPS_ARGUMENTS_SET, arguments, GPS_ACK_SELFTEST) == 0 &&
         tries != 0) {
    psos_pause(PLL_GPS_CONTROL_RETRY_TICKS);
    watchdog_pll++;
    tries--;
  }
  if (tries == 0) {
    selftest.gps = SELFTEST_GPS_NO_ANSWER;
  }
}

/* If a self-test request was sent, polls plla_exchange for its reply,
 * up to tries more times, and takes the GPS's self-test result from
 * it. */
static void pll_gps_control_selftest_reply(uint8_t tries) {
  gps_request_entry reply;

  if (gps_ack_awaited == GPS_ACK_NONE) {
    return;
  }
  while ((uint8_t)psos_request_x(plla_exchange, &reply, 1, 0) != 0 &&
         tries != 0) {
    psos_pause(PLL_GPS_CONTROL_RETRY_TICKS);
    watchdog_pll++;
    tries--;
  }
  if (tries == 0 || (reply.result != PLL_GPS_CONTROL_ACK_OK &&
                     reply.result != PLL_GPS_CONTROL_ACK_TIMED_OUT)) {
    selftest.gps = SELFTEST_GPS_NO_ANSWER;
  } else if (reply.result == PLL_GPS_CONTROL_ACK_OK) {
    selftest.gps = gps.self_test;
  } else {
    selftest.gps = SELFTEST_GPS_TIMED_OUT;
  }
}

/* Sends an initialization request awaiting its acknowledgment, and
 * moves to the state that waits for it. */
static void pll_gps_control_init_send(int16_t code, int32_t* arguments,
                                      uint8_t ack_state) {
  if (gpsx_send(code, GPS_ARGUMENTS_SET, arguments, GPS_ACK_INIT) != 0) {
    pll.gps_init_state = ack_state;
  }
}

/* Whether the acknowledgment has come, and succeeded; one that failed
 * sends again from retry_state and prints format with its code. */
static bool pll_gps_control_acked(uint8_t* state, uint8_t retry_state,
                                  char* format) {
  char message[80];

  if (gps_ack_received() == 0) {
    return false;
  }
  if (gps_ack_code == PLL_GPS_CONTROL_ACK_OK) {
    return true;
  }
  *state = retry_state;
  debug_sprintf(message, format, (int32_t)gps_ack_code);
  task_dmessage(message);
  return false;
}

/* The arguments of a position, nv_settings.position, and position hold
 * on. */
static void pll_gps_control_position_arguments(int32_t* arguments) {
  arguments[0] = nv_settings.position[0];
  arguments[1] = nv_settings.position[1];
  arguments[2] = nv_settings.position[2];
  arguments[3] = 1;
}

/* The survey state: keeps the average position, and when the survey is
 * finished or has averaged its duration, saves it as the position and
 * holds there. */
static void pll_gps_control_survey(void) {
  if (gps.average_count != 0) {
    survey_position[0] = double_to_integer(gps.average_latitude);
    survey_position[1] = double_to_integer(gps.average_longitude);
    survey_position[2] = double_to_integer(gps.average_height_msl);
    if (survey_position_valid == 0) {
      survey_position_valid = 1;
    }
  }
  if ((survey_position_valid != 0 && douglas_finish_requested != 0) ||
      (nv_settings.douglas_duration_infinite == 0 &&
       (uint32_t)nv_settings.douglas_duration <= gps.average_count)) {
    nv_settings.position[0] = survey_position[0];
    nv_settings.position[1] = survey_position[1];
    nv_settings.position[2] = survey_position[2];
    nv_settings.douglas_state = 1;
    nv_save(douglas_finish_nv_argument);
    pll.gps_init_state = PLL_GPS_INIT_SURVEY_POSITION_SEND;
  }
}

/* Steps the leap second check: once asked, @@Bj, the pending leap
 * second, and from its answer the leap event; then again each time the
 * timer wraps or a recheck is asked for (0x46f64). */
void gps_leap_sequence(void) {
  int32_t arguments[4];
  char message[80];

  switch (pll.leap_check_state) {
    case PLL_LEAP_CHECK_PENDING_SEND:
      arguments[0] = 0;
      if (gpsx_send(GPS_REQUEST_LEAP_SECOND_PENDING, GPS_ARGUMENTS_SET,
                    arguments, GPS_ACK_LEAP_CHECK) != 0) {
        pll.leap_check_state = PLL_LEAP_CHECK_PENDING_ACK;
      }
      break;
    case PLL_LEAP_CHECK_PENDING_ACK:
      if (gps_ack_received() == 0) {
        break;
      }
      if (gps_ack_code == PLL_GPS_CONTROL_ACK_OK) {
        leap_event_create();
        pll.leap_check_timer = 0;
        pll.leap_check_state = PLL_LEAP_CHECK_WAIT;
      } else {
        pll.leap_check_state = PLL_LEAP_CHECK_PENDING_SEND;
        debug_sprintf(message, "leap pending ack failure: %02X\r\n",
                      (int32_t)gps_ack_code);
        task_dmessage(message);
      }
      break;
    case PLL_LEAP_CHECK_WAIT:
      if (leap_recheck_requested != 0) {
        pll.leap_check_timer = 0;
        pll.leap_check_state = PLL_LEAP_CHECK_PENDING_SEND;
        leap_recheck_requested = 0;
        break;
      }
      pll.leap_check_timer++;
      if (pll.leap_check_timer == 0) {
        pll.leap_check_state = PLL_LEAP_CHECK_PENDING_SEND;
      }
      break;
    case PLL_LEAP_CHECK_START:
    default:
      if (pll.leap_check_requested != 0) {
        pll.leap_check_state = PLL_LEAP_CHECK_PENDING_SEND;
      }
      break;
  }
}

/* An Oncore date as the long its bytes make, month << 24 | day << 16 |
 * year, date_to_mjd's argument. */
static uint32_t pll_gps_control_date_bits(oncore_date_fields date) {
  return (uint32_t)date.month << (3 * CLOCK_FIELD_BITS) |
         (uint32_t)date.day << CLOCK_HIGH_SHIFT | date.year;
}

/* The GPS-UTC offset, from the engine's time in the other mode less
 * the clock's, to 16 bits, as the engine's mode says. */
static int16_t pll_gps_control_utc_offset(void) {
  uint32_t date;
  time_of_day time;

  get_gps_datetime(&date, &time);
  int32_t days = date_to_mjd(pll_gps_control_date_bits(gps.status.date)) -
                 date_to_days(date);
  int16_t offset = days * CLOCK_SECONDS_PER_DAY +
                   hms_to_seconds(clock_time_bits(gps.status.time)) -
                   time_to_seconds(clock_time_bits(time));

  return gps.utc_time == ONCORE_TIME_UTC ? -offset : offset;
}

/* Sends the console's pending GPS command, gps_command, as the request
 * its code maps to, with its arguments, and clears it (0x47274). */
void gps_command_send(void) {
  gps_argument_mode argument_mode = GPS_ARGUMENTS_SET;
  int16_t code;

  switch (gps_command.code) {
    case GPS_CONSOLE_PRINT_STATUS:
      code = GPS_STEP_PRINT_STATUS;
      break;
    case GPS_CONSOLE_STATUS_RATE:
      code = GPS_REQUEST_POSITION_STATUS;
      break;
    case GPS_CONSOLE_TIME:
      code = GPS_REQUEST_TIME_OF_DAY;
      break;
    case GPS_CONSOLE_DATE:
      code = GPS_REQUEST_DATE;
      break;
    case GPS_CONSOLE_HOLD_POSITION:
      code = GPS_REQUEST_POSITION_HOLD_POSITION;
      break;
    case GPS_CONSOLE_POSITION_HOLD:
      code = GPS_REQUEST_POSITION_HOLD;
      break;
    case GPS_CONSOLE_STATUS_INTERVAL:
      /* Not a request: the status printout's interval. */
      gps_status_print_interval = (uint8_t)gps_command.arguments[0];
      gps_command.code = GPS_CONSOLE_NONE;
      return;
    case GPS_CONSOLE_MASK_ANGLE:
      code = GPS_REQUEST_MASK_ANGLE;
      break;
    case GPS_CONSOLE_MASK_ANGLE_QUERY:
      code = GPS_REQUEST_MASK_ANGLE;
      argument_mode = GPS_ARGUMENTS_QUERY;
      break;
    case GPS_CONSOLE_HOLD_POSITION_QUERY:
      code = GPS_REQUEST_POSITION_HOLD_POSITION;
      argument_mode = GPS_ARGUMENTS_QUERY;
      break;
    case GPS_CONSOLE_CABLE_DELAY:
      code = GPS_REQUEST_CABLE_DELAY;
      break;
    case GPS_CONSOLE_CABLE_DELAY_QUERY:
      code = GPS_REQUEST_CABLE_DELAY;
      argument_mode = GPS_ARGUMENTS_QUERY;
      break;
    case GPS_CONSOLE_VISIBILITY_RATE:
      code = GPS_REQUEST_VISIBILITY;
      break;
    case GPS_CONSOLE_DOP_RATE:
      code = GPS_REQUEST_DOP_TABLE;
      break;
    case GPS_CONSOLE_PRINT_VISIBILITY:
      code = GPS_STEP_PRINT_VISIBILITY;
      break;
    case GPS_CONSOLE_PRINT_BEST_COMBINATIONS:
      code = GPS_STEP_PRINT_BEST_COMBINATIONS;
      break;
    case GPS_CONSOLE_EXTENSION_RATE:
      code = GPS_REQUEST_POSITION_EXTENSION;
      break;
    case GPS_CONSOLE_PRINT_EXTENSION:
      code = GPS_STEP_PRINT_EXTENSION;
      break;
    case GPS_CONSOLE_PRINT_ID:
      code = GPS_STEP_PRINT_ID;
      break;
    case GPS_CONSOLE_PRINT_PPS:
      code = GPS_STEP_PRINT_PPS;
      break;
    case GPS_CONSOLE_PRINT_TIME_RAIM:
      code = GPS_STEP_PRINT_TIME_RAIM;
      break;
    case GPS_CONSOLE_AVERAGE:
      code = GPS_STEP_AVERAGE;
      break;
    case GPS_CONSOLE_CLEAR_AVERAGE:
      code = GPS_STEP_AVERAGE_RESET;
      break;
    case GPS_CONSOLE_INIT:
      code = GPS_SCRIPT_INIT;
      break;
    case GPS_CONSOLE_MASTER_RESET:
      code = GPS_SCRIPT_INIT_DEFAULTS;
      break;
    case GPS_CONSOLE_SELFTEST:
      code = GPS_SCRIPT_ENGINE_SELFTEST;
      break;
    case GPS_CONSOLE_QUERY_ALL:
      code = GPS_SCRIPT_QUERY_SETTINGS;
      break;
    case GPS_CONSOLE_QUIET_STATUS:
      code = GPS_SCRIPT_OUTPUTS_OFF;
      break;
    case GPS_CONSOLE_PRINT_AVERAGE:
      code = GPS_STEP_PRINT_AVERAGE;
      break;
    case GPS_CONSOLE_PRINT_INSTRUMENT_STATE:
      code = GPS_STEP_PRINT_INSTRUMENT_STATE;
      break;
    case GPS_CONSOLE_CHANGE:
      code = (int16_t)gps_command.request;
      break;
    case GPS_CONSOLE_QUERY:
      code = (int16_t)gps_command.request;
      argument_mode = GPS_ARGUMENTS_QUERY;
      break;
    default:
      gps_command.code = GPS_CONSOLE_NONE;
      return;
  }
  gpsx_send(code, argument_mode, gps_command.arguments, GPS_ACK_COMMAND);
  gps_command.code = GPS_CONSOLE_NONE;
}

/* A diagnostic send or acknowledgment wait that has not succeeded: one
 * more second, and past tries the GPS's self-test fails. */
static void pll_gps_control_diag_retry(uint8_t tries) {
  pll.gps_diag_substate++;
  if (pll.gps_diag_substate > tries) {
    selftest.gps = SELFTEST_GPS_NO_ANSWER;
    pll.gps_diag_state = PLL_GPS_DIAG_SETTLE;
  }
}

/* A diagnostic send's next second: on success its acknowledgment's
 * state, else a retry. */
static void pll_gps_control_diag_send(int16_t code, int32_t* arguments,
                                      uint8_t ack_state) {
  static const uint8_t SEND_TRIES = 20;

  interval_next_reading(INTERVAL_ARM_PLAIN, NULL);
  if (gpsx_send(code, GPS_ARGUMENTS_SET, arguments, GPS_ACK_SELFTEST) == 0) {
    pll_gps_control_diag_retry(SEND_TRIES);
  } else {
    pll.gps_diag_state = ack_state;
    pll.gps_diag_substate = 0;
  }
}

/* A diagnostic acknowledgment's next second: whether it came, after a
 * retry if not. */
static bool pll_gps_control_diag_acked(void) {
  static const uint8_t ACK_TRIES = 30;

  interval_next_reading(INTERVAL_ARM_PLAIN, NULL);
  if (gps_ack_received() == 0) {
    pll_gps_control_diag_retry(ACK_TRIES);
    return false;
  }
  return true;
}

/* The diag stage, a second at a time: for the GPS self-test, test 10,
 * resets the GPS link and asks the engine for its self-test, then runs
 * the self-test asked for; true once it has, for pllp_task to restart
 * the loop (0x473c6).  The arguments sent are its unset frame, as the
 * image's are. */
bool gps_diag_sequence(bool same_stage) {
  static const int32_t SELFTEST_GPS = 10;
  int32_t arguments[4];

  /* A state the image takes as signed: one of 0x80 or more is not
   * reset, but runs as the first. */
  if (!same_stage || (int8_t)pll.gps_diag_state > PLL_GPS_DIAG_RUN) {
    pll.gps_diag_state = selftest_number == 0 || selftest_number == SELFTEST_GPS
                             ? PLL_GPS_DIAG_RESET_SEND
                             : PLL_GPS_DIAG_SETTLE;
    pll.gps_diag_substate = 0;
  }
  switch (pll.gps_diag_state) {
    case PLL_GPS_DIAG_RESET_ACK:
      if (pll_gps_control_diag_acked()) {
        if (gps_ack_code != PLL_GPS_CONTROL_ACK_OK) {
          task_dmessage("Error in DIAG GPS RST ack\r\n");
        }
        pll.gps_diag_state = PLL_GPS_DIAG_TEST_SEND;
      }
      break;
    case PLL_GPS_DIAG_TEST_SEND:
      pll_gps_control_diag_send(GPS_SCRIPT_ENGINE_SELFTEST, arguments,
                                PLL_GPS_DIAG_TEST_ACK);
      break;
    case PLL_GPS_DIAG_TEST_ACK:
      if (pll_gps_control_diag_acked()) {
        if (gps_ack_code == PLL_GPS_CONTROL_ACK_OK) {
          selftest.gps = gps.self_test;
        } else if (gps_ack_code == PLL_GPS_CONTROL_ACK_TIMED_OUT) {
          selftest.gps = SELFTEST_GPS_TIMED_OUT;
        } else {
          task_dmessage("Error in DIAG GPS TST ack\r\n");
          selftest.gps = SELFTEST_GPS_NO_ANSWER;
        }
        pll.gps_diag_state = PLL_GPS_DIAG_SETTLE;
      }
      break;
    case PLL_GPS_DIAG_SETTLE:
      interval_wait_reading();
      pll.gps_diag_state = PLL_GPS_DIAG_RUN;
      break;
    case PLL_GPS_DIAG_RUN:
      selftest_run(selftest_number);
      selftest_requested = 0;
      gps_diag_complete((int32_t*)&plla_unexpected_count);
      return true;
    case PLL_GPS_DIAG_RESET_SEND:
    default:
      pll_gps_control_diag_send(GPS_STEP_LINK_RESET, arguments,
                                PLL_GPS_DIAG_RESET_ACK);
      break;
  }
  return false;
}
