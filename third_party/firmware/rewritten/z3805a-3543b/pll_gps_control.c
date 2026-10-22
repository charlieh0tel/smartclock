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

/* selftest.gps for an engine that never answered, and for one whose
 * acknowledgment timed out. */
static const int16_t PLL_GPS_CONTROL_SELFTEST_NO_ANSWER = 0x4000;

static const int16_t PLL_GPS_CONTROL_SELFTEST_TIMED_OUT = -0x8000;

/* Ticks the self-test pauses between tries. */
static const uint32_t PLL_GPS_CONTROL_RETRY_TICKS = 0x400;

/* Sends a request to gpsx_exchange, its four arguments copied to
 * gpsx_arguments, unless an acknowledgment is awaited; whether it was
 * sent, and then ack_awaited awaits its acknowledgment (0x46636). */
uint8_t gpsx_send(int16_t code, uint8_t argument_mode, int32_t* arguments,
                  uint8_t ack_awaited) {
  gps_request_entry request;

  if (gps_ack_awaited != PLL_GPS_CONTROL_AWAITED_NONE) {
    return 0;
  }
  for (int32_t i = 0; i < 4; i++) {
    gpsx_arguments[i] = arguments[i];
  }
  request.field_00 = 0;
  request.field_04 = 0;
  request.code = code;
  request.argument_mode = argument_mode;
  request.result = 1;
  request.sender = GPS_SENDER_SEQUENCE;
  request.arguments = (int32_t*)gpsx_arguments;
  if ((uint8_t)psos_send_x(gpsx_exchange, &request) != 0) {
    return 0;
  }
  gps_ack_awaited = ack_awaited;
  gps_ack_ready = 0;
  gps_ack_code = 1;
  gpsx_sent_count++;
  gps_ack_wait_count = 0;
  return 1;
}

/* Whether the awaited acknowledgment has come, no longer awaiting it
 * once it has (0x46620). */
uint8_t gps_ack_received(void) {
  if (gps_ack_ready != 0) {
    gps_ack_awaited = PLL_GPS_CONTROL_AWAITED_NONE;
  }
  return gps_ack_ready;
}

/* Sends a self-test request, trying while the send fails, up to 20
 * more times; the GPS's self-test failed if the tries ran out, even
 * when the last succeeded.  The arguments are the caller's unset
 * frame, as the image's are. */
static void pll_gps_control_selftest_send(int16_t code, int32_t* arguments) {
  uint8_t tries = 20;

  while (gpsx_send(code, 0, arguments, PLL_GPS_CONTROL_AWAITED_SELFTEST) == 0 &&
         tries != 0) {
    psos_pause(PLL_GPS_CONTROL_RETRY_TICKS);
    watchdog_pll++;
    tries--;
  }
  if (tries == 0) {
    selftest.gps = PLL_GPS_CONTROL_SELFTEST_NO_ANSWER;
  }
}

/* If a self-test request was sent, polls plla_exchange for its reply,
 * up to tries more times, and takes the GPS's self-test result from
 * it. */
static void pll_gps_control_selftest_reply(uint8_t tries) {
  gps_request_entry reply;

  if (gps_ack_awaited == PLL_GPS_CONTROL_AWAITED_NONE) {
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
    selftest.gps = PLL_GPS_CONTROL_SELFTEST_NO_ANSWER;
  } else if (reply.result == PLL_GPS_CONTROL_ACK_OK) {
    selftest.gps = gps.self_test;
  } else {
    selftest.gps = PLL_GPS_CONTROL_SELFTEST_TIMED_OUT;
  }
}

/* Self-test 10's GPS part: resets the GPS link, and if that answers,
 * asks the engine for its self-test, taking the result into
 * selftest.gps (0x466ca). */
void gps_selftest_run(void) {
  static const uint8_t RESET_REPLY_TRIES = 130;
  static const uint8_t SELFTEST_REPLY_TRIES = 30;
  int32_t arguments[4];

  pll_gps_control_selftest_send(GPS_STEP_LINK_RESET, arguments);
  pll_gps_control_selftest_reply(RESET_REPLY_TRIES);
  gps_ack_awaited = PLL_GPS_CONTROL_AWAITED_NONE;
  if (selftest.gps == 0) {
    pll_gps_control_selftest_send(GPS_SCRIPT_ENGINE_SELFTEST, arguments);
    pll_gps_control_selftest_reply(SELFTEST_REPLY_TRIES);
  }
  gps_ack_awaited = PLL_GPS_CONTROL_AWAITED_NONE;
}

/* Sends an initialization request awaiting its acknowledgment, and
 * moves to the state that waits for it. */
static void pll_gps_control_init_send(int16_t code, int32_t* arguments,
                                      uint8_t ack_state) {
  if (gpsx_send(code, 0, arguments, PLL_GPS_CONTROL_AWAITED_INIT) != 0) {
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
    pll.gps_init_state = PLL_GPS_CONTROL_SURVEY_POSITION_SEND;
  }
}

/* Steps the GPS engine's initialization: the settings, each sent and
 * its acknowledgment checked, then a held position, or the survey
 * (:DOUGlas) that finds one (0x46838). */
void gps_init_sequence(void) {
  /* The source nv_save gets when the survey finishes unasked. */
  static const uint8_t SURVEY_NV_SOURCE = 4;
  uint8_t* state = &pll.gps_init_state;
  int32_t arguments[4];

  switch (*state) {
    case PLL_GPS_CONTROL_INIT_ACK:
      if (pll_gps_control_acked(state, PLL_GPS_CONTROL_INIT_START,
                                "gps init ack failure: %02X\r\n")) {
        *state = PLL_GPS_CONTROL_UTC_SEND;
      }
      break;
    case PLL_GPS_CONTROL_UTC_SEND:
      arguments[0] = (int8_t)nv_settings.gps_utc;
      pll_gps_control_init_send(GPS_REQUEST_UTC_SELECT, arguments,
                                PLL_GPS_CONTROL_UTC_ACK);
      break;
    case PLL_GPS_CONTROL_UTC_ACK:
      if (pll_gps_control_acked(state, PLL_GPS_CONTROL_UTC_SEND,
                                "utc corr ack failure: %02X\r\n")) {
        *state = PLL_GPS_CONTROL_MASK_ANGLE_SEND;
      }
      break;
    case PLL_GPS_CONTROL_MASK_ANGLE_SEND:
      arguments[0] = nv_settings.elevation_mask;
      pll_gps_control_init_send(GPS_REQUEST_MASK_ANGLE, arguments,
                                PLL_GPS_CONTROL_MASK_ANGLE_ACK);
      break;
    case PLL_GPS_CONTROL_MASK_ANGLE_ACK:
      if (pll_gps_control_acked(state, PLL_GPS_CONTROL_MASK_ANGLE_SEND,
                                "mask angle ack failure: %02X\r\n")) {
        *state = PLL_GPS_CONTROL_ANTENNA_DELAY_SEND;
      }
      break;
    case PLL_GPS_CONTROL_ANTENNA_DELAY_SEND:
      arguments[0] = nv_settings.antenna_delay;
      pll_gps_control_init_send(GPS_REQUEST_CABLE_DELAY, arguments,
                                PLL_GPS_CONTROL_ANTENNA_DELAY_ACK);
      break;
    case PLL_GPS_CONTROL_ANTENNA_DELAY_ACK:
      if (pll_gps_control_acked(state, PLL_GPS_CONTROL_ANTENNA_DELAY_SEND,
                                "ant delay ack failure: %02X\r\n")) {
        *state = PLL_GPS_CONTROL_IGNORE_SEND;
      }
      break;
    case PLL_GPS_CONTROL_IGNORE_SEND:
      arguments[0] = nv_settings.ignored_satellites;
      pll_gps_control_init_send(GPS_REQUEST_IGNORE_LIST, arguments,
                                PLL_GPS_CONTROL_IGNORE_ACK);
      break;
    case PLL_GPS_CONTROL_IGNORE_ACK:
      if (pll_gps_control_acked(state, PLL_GPS_CONTROL_IGNORE_SEND,
                                "sat ign ack failure: %02X\r\n")) {
        *state = nv_settings.douglas_state == 0 ? PLL_GPS_CONTROL_HOLD_OFF_SEND
                                                : PLL_GPS_CONTROL_POSITION_SEND;
      }
      break;
    case PLL_GPS_CONTROL_POSITION_SEND:
      pll_gps_control_position_arguments(arguments);
      pll_gps_control_init_send(GPS_SCRIPT_HOLD_AT, arguments,
                                PLL_GPS_CONTROL_POSITION_ACK);
      break;
    case PLL_GPS_CONTROL_POSITION_ACK:
      if (pll_gps_control_acked(state, PLL_GPS_CONTROL_POSITION_SEND,
                                "pos location ack failure: %02X\r\n")) {
        status_happening(STATUS_POSITION_HOLD_SET);
        *state = PLL_GPS_CONTROL_INIT_DONE;
      }
      break;
    case PLL_GPS_CONTROL_HOLD_OFF_SEND:
      arguments[0] = 0;
      pll_gps_control_init_send(GPS_SCRIPT_HOLD_SELECT, arguments,
                                PLL_GPS_CONTROL_HOLD_OFF_ACK);
      break;
    case PLL_GPS_CONTROL_HOLD_OFF_ACK:
      if (pll_gps_control_acked(state, PLL_GPS_CONTROL_HOLD_OFF_SEND,
                                "disable pos hold ack failure: %02X\r\n")) {
        status_happening(STATUS_POSITION_HOLD_CLEARED);
        *state = PLL_GPS_CONTROL_SURVEY;
      }
      break;
    case PLL_GPS_CONTROL_SURVEY:
      if (douglas_stop_requested != 0) {
        *state = PLL_GPS_CONTROL_INIT_DONE;
        douglas_stop_requested = 0;
        break;
      }
      pll_gps_control_survey();
      douglas_start_requested = 0;
      break;
    case PLL_GPS_CONTROL_SURVEY_POSITION_SEND:
      if (douglas_stop_requested != 0) {
        *state = PLL_GPS_CONTROL_INIT_DONE;
        douglas_stop_requested = 0;
        break;
      }
      pll_gps_control_position_arguments(arguments);
      pll_gps_control_init_send(GPS_SCRIPT_HOLD_AT_QUIET, arguments,
                                PLL_GPS_CONTROL_SURVEY_POSITION_ACK);
      break;
    case PLL_GPS_CONTROL_SURVEY_POSITION_ACK:
      if (gps_ack_received() == 0) {
        break;
      }
      if (douglas_stop_requested != 0) {
        *state = PLL_GPS_CONTROL_INIT_DONE;
        douglas_stop_requested = 0;
        break;
      }
      if (gps_ack_code == PLL_GPS_CONTROL_ACK_OK) {
        *state = PLL_GPS_CONTROL_INIT_DONE;
        log_event(LOG_POSITION_HOLD_STARTED);
        status_happening(STATUS_POSITION_HOLD_SET);
      } else {
        char message[80];

        *state = PLL_GPS_CONTROL_SURVEY_POSITION_SEND;
        debug_sprintf(message, "set survey location ack failure: %02X\r\n",
                      (int32_t)gps_ack_code);
        task_dmessage(message);
      }
      break;
    case PLL_GPS_CONTROL_INIT_DONE:
      if (douglas_stop_requested != 0) {
        douglas_stop_requested = 0;
      }
      if (douglas_finish_requested != 0) {
        douglas_finish_requested = 0;
        douglas_finish_nv_argument = SURVEY_NV_SOURCE;
      }
      if (douglas_start_requested != 0) {
        *state = PLL_GPS_CONTROL_SURVEY;
        douglas_start_requested = 0;
      }
      break;
    case PLL_GPS_CONTROL_INIT_START:
    default:
      arguments[0] = 0;
      pll_gps_control_init_send(GPS_SCRIPT_INIT, arguments,
                                PLL_GPS_CONTROL_INIT_ACK);
      break;
  }
}

/* gps_leap_sequence's states, pll.leap_check_state, as
 * print_leap_check_state names them. */
enum {
  PLL_GPS_CONTROL_LEAP_START = 0,
  PLL_GPS_CONTROL_LEAP_PENDING_SEND = 1,
  PLL_GPS_CONTROL_LEAP_PENDING_ACK = 2,
  PLL_GPS_CONTROL_LEAP_WAIT = 3,
};

/* Steps the leap second check: once asked, @@Bj, the pending leap
 * second, and from its answer the leap event; then again each time the
 * timer wraps or a recheck is asked for (0x46d98). */
void gps_leap_sequence(void) {
  int32_t arguments[4];
  char message[80];

  switch (pll.leap_check_state) {
    case PLL_GPS_CONTROL_LEAP_PENDING_SEND:
      arguments[0] = 0;
      if (gpsx_send(GPS_REQUEST_LEAP_SECOND_PENDING, 0, arguments,
                    PLL_GPS_CONTROL_AWAITED_LEAP_CHECK) != 0) {
        pll.leap_check_state = PLL_GPS_CONTROL_LEAP_PENDING_ACK;
      }
      break;
    case PLL_GPS_CONTROL_LEAP_PENDING_ACK:
      if (gps_ack_received() == 0) {
        break;
      }
      if (gps_ack_code == PLL_GPS_CONTROL_ACK_OK) {
        leap_event_create();
        pll.leap_check_timer = 0;
        pll.leap_check_state = PLL_GPS_CONTROL_LEAP_WAIT;
      } else {
        pll.leap_check_state = PLL_GPS_CONTROL_LEAP_PENDING_SEND;
        debug_sprintf(message, "leap pending ack failure: %02X\r\n",
                      (int32_t)gps_ack_code);
        task_dmessage(message);
      }
      break;
    case PLL_GPS_CONTROL_LEAP_WAIT:
      if (leap_recheck_requested != 0) {
        pll.leap_check_timer = 0;
        pll.leap_check_state = PLL_GPS_CONTROL_LEAP_PENDING_SEND;
        leap_recheck_requested = 0;
        break;
      }
      pll.leap_check_timer++;
      if (pll.leap_check_timer == 0) {
        pll.leap_check_state = PLL_GPS_CONTROL_LEAP_PENDING_SEND;
      }
      break;
    case PLL_GPS_CONTROL_LEAP_START:
    default:
      if (pll.leap_check_requested != 0) {
        pll.leap_check_state = PLL_GPS_CONTROL_LEAP_PENDING_SEND;
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

/* Steps the leap second's acknowledgment: measures the GPS-UTC offset by
 * switching the engine's time mode, @@Aw, and sets it (0x46e70). */
void gps_leap_ack(void) {
  uint8_t* state = &pll.leap_ack_state;
  int32_t arguments[4];

  switch (*state) {
    case PLL_GPS_CONTROL_LEAP_ACK_CHECK: {
      bool mode_set =
          (gps.utc_time == ONCORE_TIME_UTC && nv_settings.gps_utc != 0) ||
          (gps.utc_time == ONCORE_TIME_GPS && nv_settings.gps_utc == 0);

      *state = mode_set ? PLL_GPS_CONTROL_LEAP_ACK_SWITCH_SEND
                        : PLL_GPS_CONTROL_LEAP_ACK_MODE_SEND;
      return;
    }
    case PLL_GPS_CONTROL_LEAP_ACK_MODE_SEND:
      arguments[0] = (int8_t)nv_settings.gps_utc;
      if (gpsx_send(GPS_REQUEST_UTC_SELECT, 0, arguments,
                    PLL_GPS_CONTROL_AWAITED_LEAP_ACK) != 0) {
        *state = PLL_GPS_CONTROL_LEAP_ACK_MODE_ACK;
      }
      return;
    case PLL_GPS_CONTROL_LEAP_ACK_MODE_ACK:
      if (gps_ack_received() == 0) {
        return;
      }
      if (gps_ack_code != PLL_GPS_CONTROL_ACK_OK) {
        *state = PLL_GPS_CONTROL_LEAP_ACK_MODE_SEND;
        break;
      }
      *state = PLL_GPS_CONTROL_LEAP_ACK_SWITCH_SEND;
      return;
    case PLL_GPS_CONTROL_LEAP_ACK_SWITCH_SEND:
      arguments[0] = gps.utc_time == ONCORE_TIME_GPS;
      if (gpsx_send(GPS_SCRIPT_UTC_SET, 0, arguments,
                    PLL_GPS_CONTROL_AWAITED_LEAP_ACK) != 0) {
        *state = PLL_GPS_CONTROL_LEAP_ACK_SWITCH_ACK;
      }
      return;
    case PLL_GPS_CONTROL_LEAP_ACK_SWITCH_ACK:
      if (gps_ack_received() == 0) {
        return;
      }
      if (gps_ack_code != PLL_GPS_CONTROL_ACK_OK) {
        *state = PLL_GPS_CONTROL_LEAP_ACK_CHECK;
        break;
      }
      set_gps_utc_offset(pll_gps_control_utc_offset());
      *state = PLL_GPS_CONTROL_LEAP_ACK_BACK_SEND;
      return;
    case PLL_GPS_CONTROL_LEAP_ACK_BACK_SEND:
      arguments[0] = gps.utc_time == ONCORE_TIME_GPS;
      if (gpsx_send(GPS_REQUEST_UTC_SELECT, 0, arguments,
                    PLL_GPS_CONTROL_AWAITED_LEAP_ACK) != 0) {
        *state = PLL_GPS_CONTROL_LEAP_ACK_BACK_ACK;
      }
      return;
    case PLL_GPS_CONTROL_LEAP_ACK_BACK_ACK:
      if (gps_ack_received() == 0) {
        return;
      }
      if (gps_ack_code != PLL_GPS_CONTROL_ACK_OK) {
        *state = PLL_GPS_CONTROL_LEAP_ACK_BACK_SEND;
        break;
      }
      *state = PLL_GPS_CONTROL_LEAP_ACK_IDLE;
      pll.leap_ack_done = 1;
      pll.leap_ack_again = 0;
      return;
    case PLL_GPS_CONTROL_LEAP_ACK_IDLE:
    default:
      if (pll.leap_ack_requested != 0) {
        *state = PLL_GPS_CONTROL_LEAP_ACK_CHECK;
        pll.leap_ack_requested = 0;
        pll.leap_ack_done = 0;
      }
      return;
  }
  task_dmessage("Error in leapsecond ack\r\n");
}

/* Sends the console's pending GPS command, gps_command, as the request
 * its code maps to, with its arguments, and clears it (0x470aa). */
void gps_command_send(void) {
  uint8_t argument_mode = 0;
  int16_t code;

  switch (gps_command.code) {
    case 1:
      code = GPS_STEP_PRINT_STATUS;
      break;
    case 2:
      code = GPS_REQUEST_POSITION_STATUS;
      break;
    case 3:
      code = GPS_REQUEST_TIME_OF_DAY;
      break;
    case 4:
      code = GPS_REQUEST_DATE;
      break;
    case 5:
      code = GPS_REQUEST_POSITION_HOLD_POSITION;
      break;
    case 6:
      code = GPS_REQUEST_POSITION_HOLD;
      break;
    case 7:
      /* Not a request: the status printout's interval. */
      gps_status_print_interval = (uint8_t)gps_command.arguments[0];
      gps_command.code = 0;
      return;
    case 8:
      code = GPS_REQUEST_MASK_ANGLE;
      break;
    case 9:
      code = GPS_REQUEST_MASK_ANGLE;
      argument_mode = 1;
      break;
    case 10:
      code = GPS_REQUEST_POSITION_HOLD_POSITION;
      argument_mode = 1;
      break;
    case 0x0b:
      code = GPS_REQUEST_CABLE_DELAY;
      break;
    case 0x0c:
      code = GPS_REQUEST_CABLE_DELAY;
      argument_mode = 1;
      break;
    case 0x0d:
      code = GPS_REQUEST_VISIBILITY;
      break;
    case 0x0e:
      code = GPS_REQUEST_DOP_TABLE;
      break;
    case 0x0f:
      code = GPS_STEP_PRINT_VISIBILITY;
      break;
    case 0x10:
      code = GPS_STEP_PRINT_BEST_COMBINATIONS;
      break;
    case 0x11:
      code = GPS_REQUEST_POSITION_EXTENSION;
      break;
    case 0x12:
      code = GPS_STEP_PRINT_EXTENSION;
      break;
    case 0x13:
      code = GPS_STEP_PRINT_ID;
      break;
    case 0x14:
      code = GPS_STEP_PRINT_PPS;
      break;
    case 0x15:
      code = GPS_STEP_PRINT_TIME_RAIM;
      break;
    case 0x16:
      code = 0x3c; /* a gps_request_code step smartclock.h does not name */
      break;
    case 0x17:
      code = GPS_STEP_AVERAGE_RESET;
      break;
    case 0x18:
      code = GPS_SCRIPT_INIT;
      break;
    case 0x19:
      code = GPS_SCRIPT_INIT_DEFAULTS;
      break;
    case 0x1a:
      code = GPS_SCRIPT_ENGINE_SELFTEST;
      break;
    case 0x1b:
      code = GPS_SCRIPT_QUERY_SETTINGS;
      break;
    case 0x1c:
      code = GPS_SCRIPT_OUTPUTS_OFF;
      break;
    case 0x1d:
      code = GPS_STEP_PRINT_AVERAGE;
      break;
    case 0x1e:
      code = GPS_STEP_PRINT_INSTRUMENT_STATE;
      break;
    case 0x1f:
      code = (int16_t)gps_command.request;
      break;
    case 0x20:
      code = (int16_t)gps_command.request;
      argument_mode = 1;
      break;
    default:
      gps_command.code = 0;
      return;
  }
  gpsx_send(code, argument_mode, gps_command.arguments,
            PLL_GPS_CONTROL_AWAITED_COMMAND);
  gps_command.code = 0;
}

/* gps_diag_sequence's states, pll.gps_diag_state. */
enum {
  PLL_GPS_CONTROL_DIAG_RESET_SEND = 0,
  PLL_GPS_CONTROL_DIAG_RESET_ACK = 1,
  PLL_GPS_CONTROL_DIAG_TEST_SEND = 2,
  PLL_GPS_CONTROL_DIAG_TEST_ACK = 3,
  PLL_GPS_CONTROL_DIAG_SETTLE = 4,
  PLL_GPS_CONTROL_DIAG_RUN = 5,
};

/* A diagnostic send or acknowledgment wait that has not succeeded: one
 * more second, and past tries the GPS's self-test fails. */
static void pll_gps_control_diag_retry(uint8_t tries) {
  pll.gps_diag_substate++;
  if (pll.gps_diag_substate > tries) {
    selftest.gps = PLL_GPS_CONTROL_SELFTEST_NO_ANSWER;
    pll.gps_diag_state = PLL_GPS_CONTROL_DIAG_SETTLE;
  }
}

/* A diagnostic send's next second: on success its acknowledgment's
 * state, else a retry. */
static void pll_gps_control_diag_send(int16_t code, int32_t* arguments,
                                      uint8_t ack_state) {
  static const uint8_t SEND_TRIES = 20;

  interval_next_reading(INTERVAL_ARM_PLAIN, NULL);
  if (gpsx_send(code, 0, arguments, PLL_GPS_CONTROL_AWAITED_SELFTEST) == 0) {
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
 * the loop (0x471fc).  The arguments sent are its unset frame, as the
 * image's are. */
bool gps_diag_sequence(bool same_stage) {
  static const int32_t SELFTEST_GPS = 10;
  int32_t arguments[4];

  /* A state the image takes as signed: one of 0x80 or more is not
   * reset, but runs as the first. */
  if (!same_stage || (int8_t)pll.gps_diag_state > PLL_GPS_CONTROL_DIAG_RUN) {
    pll.gps_diag_state = selftest_number == 0 || selftest_number == SELFTEST_GPS
                             ? PLL_GPS_CONTROL_DIAG_RESET_SEND
                             : PLL_GPS_CONTROL_DIAG_SETTLE;
    pll.gps_diag_substate = 0;
  }
  switch (pll.gps_diag_state) {
    case PLL_GPS_CONTROL_DIAG_RESET_ACK:
      if (pll_gps_control_diag_acked()) {
        if (gps_ack_code != PLL_GPS_CONTROL_ACK_OK) {
          task_dmessage("Error in DIAG GPS RST ack\r\n");
        }
        pll.gps_diag_state = PLL_GPS_CONTROL_DIAG_TEST_SEND;
      }
      break;
    case PLL_GPS_CONTROL_DIAG_TEST_SEND:
      pll_gps_control_diag_send(GPS_SCRIPT_ENGINE_SELFTEST, arguments,
                                PLL_GPS_CONTROL_DIAG_TEST_ACK);
      break;
    case PLL_GPS_CONTROL_DIAG_TEST_ACK:
      if (pll_gps_control_diag_acked()) {
        if (gps_ack_code == PLL_GPS_CONTROL_ACK_OK) {
          selftest.gps = gps.self_test;
        } else if (gps_ack_code == PLL_GPS_CONTROL_ACK_TIMED_OUT) {
          selftest.gps = PLL_GPS_CONTROL_SELFTEST_TIMED_OUT;
        } else {
          task_dmessage("Error in DIAG GPS TST ack\r\n");
          selftest.gps = PLL_GPS_CONTROL_SELFTEST_NO_ANSWER;
        }
        pll.gps_diag_state = PLL_GPS_CONTROL_DIAG_SETTLE;
      }
      break;
    case PLL_GPS_CONTROL_DIAG_SETTLE:
      interval_wait_reading();
      pll.gps_diag_state = PLL_GPS_CONTROL_DIAG_RUN;
      break;
    case PLL_GPS_CONTROL_DIAG_RUN:
      selftest_run(selftest_number);
      selftest_requested = 0;
      gps_diag_complete((int32_t*)&plla_unexpected_count);
      return true;
    case PLL_GPS_CONTROL_DIAG_RESET_SEND:
    default:
      pll_gps_control_diag_send(GPS_STEP_LINK_RESET, arguments,
                                PLL_GPS_CONTROL_DIAG_RESET_ACK);
      break;
  }
  return false;
}
