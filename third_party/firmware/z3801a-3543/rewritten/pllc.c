/* pllc's side of the SCPI settings, the firmware's own "pll_scpi"
 * (finish_scpi_cmd's message): the server that takes a setter's request
 * from the pllc exchange, validates it by its descriptor, sends what the
 * GPS must hear and stores and saves the rest, and the hooks and actions that
 * no one command owns (docs/firmware/structures.md, "Numeric SCPI parameters"
 * and "The pllc task's SCPI requests").  A command's own hooks and actions are
 * in its tree's file (scpi_source_ptime.c and the others), and the descriptors'
 * validators scpi_validators.c's.  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); the exported C includes this
 * file after its own, so the types and prototypes it declares are in scope;
 * each function here keeps its generated prototype. */

/* A descriptor's flags: the request goes to the GPS through
 * pll_scpi_send_to_loop, the value is saved in the NV register, and in
 * the EEPROM record at 0x400000. */
static const uint16_t PLLC_FLAG_SEND_TO_GPS = 1;

/* The hooks' and actions' shapes, which the descriptors keep as void
 * pointers.  A changed hook is passed its caller's D3 first. */
typedef void pllc_changed_hook(uint32_t caller_d3, int32_t index,
                               int32_t source);

typedef void pllc_setting_done_hook(int32_t index, int32_t source);

typedef void pllc_numeric_done_hook(void);

/* 1 when the request goes to the GPS before it is stored; a
 * :DOUGlas:STATe 0 from 0 instead logs position hold (0x42da4). */
uint32_t pll_scpi_needs_loop(void) {
  if ((pllc_flags() & PLLC_FLAG_SEND_TO_GPS) != 0) {
    if (pll_scpi_setting == 0 || pll_scpi_param != &scpi_param_DOUGlas_STATe) {
      return 1;
    }
    if (nv_settings.douglas_state == 0) {
      douglas_log_event = LOG_POSITION_HOLD_STARTED;
    }
  }
  return 0;
}

/* Sends the descriptor's GPS request to gpsx_exchange, its arguments in
 * pll_scpi_gps_arguments by the request; 1 when sent, else 0 and in
 * *status the condition to report, 0 for none (0x42df0). */
uint32_t pll_scpi_send_to_loop(gps_request_sender sender, uint8_t* status) {
  pllc_param* param = pllc_current();
  uint32_t* arguments = pll_scpi_gps_arguments;
  gps_request_entry request;

  /* The descriptor's request: gps_request_code, GPS_REQUEST_NONE for none. */
  int16_t code =
      (int8_t)(param->type == SCPI_PARAM_SETTING ? param->setting.gps_request
                                                 : param->numeric.gps_request);
  *status = 0;
  switch (code) {
    case GPS_REQUEST_TIME_OF_DAY:
      arguments[0] = pll_scpi_validated[0];
      arguments[1] = pll_scpi_validated[1];
      arguments[2] = pll_scpi_validated[2];
      break;
    case GPS_REQUEST_DATE:
      arguments[0] = pll_scpi_validated[1];
      arguments[1] = pll_scpi_validated[2];
      arguments[2] = pll_scpi_validated[0];
      break;
    case GPS_REQUEST_LATITUDE:
    case GPS_REQUEST_LONGITUDE:
    case GPS_REQUEST_MASK_ANGLE:
    case GPS_REQUEST_IGNORE_LIST:
    case GPS_REQUEST_PPS_OFFSET:
    case GPS_REQUEST_CABLE_DELAY:
      arguments[0] = pll_scpi_validated[0];
      break;
    case GPS_REQUEST_HEIGHT:
      arguments[0] = pll_scpi_validated[0];
      arguments[1] = 1;
      break;
    case GPS_REQUEST_POSITION_HOLD_POSITION:
    case GPS_SCRIPT_HOLD_AT:
    case GPS_SCRIPT_HOLD_POSITION:
      arguments[0] = pll_scpi_validated[0];
      arguments[1] = pll_scpi_validated[1];
      arguments[2] = pll_scpi_validated[2];
      arguments[3] = 1;
      break;
    case GPS_REQUEST_POSITION_HOLD:
    case GPS_SCRIPT_HOLD_RELEASE:
    case GPS_SCRIPT_HOLD_SELECT:
      arguments[0] = (int8_t)pll_scpi_setting;
      if (nv_settings.douglas_state != 0 && pll_scpi_setting == 0) {
        douglas_log_event = LOG_SURVEY_STARTED;
      } else if (nv_settings.douglas_state == 0 && pll_scpi_setting != 0) {
        douglas_log_event = LOG_POSITION_HOLD_STARTED;
      }
      break;
    case GPS_REQUEST_TRAIM:
      if (!(pll_scpi_index == 0 && nv_settings.douglas_state != 0) &&
          !(pll_scpi_index == 1 && nv_settings.douglas_state == 0)) {
        *status = 0;
        return 0;
      }
      arguments[0] = 1;
      arguments[3] = 1;
      if (param->type == SCPI_PARAM_SETTING) {
        arguments[1] = (int8_t)pll_scpi_setting;
        arguments[2] = nv_settings.raim_threshold[(int8_t)pll_scpi_index];
      } else {
        arguments[1] = (int8_t)nv_settings.raim_state[(int8_t)pll_scpi_index];
        arguments[2] = pll_scpi_validated[0];
      }
      break;
    case GPS_STEP_STATUS_SNAPSHOT:
    case GPS_SCRIPT_HOLD_OFF:
      break;
    default:
      *status = STATUS_DEVICE_SPECIFIC_ERROR;
      return 0;
  }
  /* The rest of the message is left as the frame holds it. */
  request.field_00 = 0;
  request.field_04 = 0;
  request.code = code;
  request.argument_mode = GPS_ARGUMENTS_SET;
  request.result = GPS_RESULT_REJECT;
  request.sender = sender;
  /* gps_request_entry's arguments are longs the export declares
   * unsigned. */
  request.arguments = (int32_t*)arguments;
  if ((uint8_t)psos_send_x(gpsx_exchange, &request) == 0) {
    *status = 0;
    return 1;
  }
  *status = STATUS_DEVICE_SPECIFIC_ERROR;
  return 0;
}

/* Stores a request's validated values or setting, calling the
 * descriptor's hooks, saves them as its flags say, and logs a
 * :DOUGlas:STATe change (0x43096). */
void finish_scpi_cmd(void) {
  /* The image passes each changed hook a byte of its caller's D3, which
   * it never sets (0x430e2, 0x43140, 0x43188); read before gcc's code
   * uses the register. */
  uint32_t caller_d3 = (uint8_t)CPU_CALLERS_REGISTER(uint32_t, "d3");
  pllc_param* param = pllc_current();
  int8_t index = (int8_t)pll_scpi_index;

  switch (param->type) {
    case SCPI_PARAM_NUMERIC: {
      if (param->numeric.is_integer == PLLC_VALUE_LIST ||
          param->numeric.is_integer == PLLC_VALUE_PAIR) {
        for (uint8_t i = 0; i <= (int8_t)pll_scpi_index; i++) {
          scpi_numeric_param* numeric = &pllc_current()->numeric;
          ((uint32_t*)numeric->value)[i] = pll_scpi_validated[i];
          ((pllc_changed_hook*)numeric->changed)(caller_d3, i,
                                                 (int8_t)pll_scpi_source);
        }
      } else {
        scpi_numeric_param* numeric = &pllc_current()->numeric;
        ((uint32_t*)numeric->value)[(int8_t)pll_scpi_index] =
            pll_scpi_validated[0];
        ((pllc_changed_hook*)numeric->changed)(
            caller_d3, (int8_t)pll_scpi_index, (int8_t)pll_scpi_source);
      }
      ((pllc_numeric_done_hook*)pllc_current()->numeric.done)();
      break;
    }
    case SCPI_PARAM_SETTING:
      param->setting.value[index] = pll_scpi_setting;
      ((pllc_changed_hook*)param->setting.changed)(
          caller_d3, (int8_t)pll_scpi_index, (int8_t)pll_scpi_source);
      ((pllc_setting_done_hook*)param->setting.done)((int8_t)pll_scpi_index,
                                                     (int8_t)pll_scpi_source);
      break;
    case SCPI_PARAM_ACTION:
    case SCPI_PARAM_DOUBLE:
      break;
    default:
      fatal_error("pll_scpi: finish_scpi_cmd() -- unknown structure type");
      break;
  }
  if (scpi_param_saved_in_nv() != 0) {
    nv_save(pll_scpi_source);
  }
  if (scpi_param_saved_in_record() != 0) {
    save_parameter_record(pll_scpi_source);
  }
  if (douglas_log_event == LOG_SURVEY_STARTED) {
    log_event(LOG_SURVEY_STARTED);
    status_happening(STATUS_POSITION_HOLD_CLEARED);
  } else if (douglas_log_event == LOG_POSITION_HOLD_STARTED) {
    log_event(LOG_POSITION_HOLD_STARTED);
    status_happening(STATUS_POSITION_HOLD_SET);
  }
}

/* Handles one request: validates it, sends what the GPS must hear, and,
 * unless a reply from it is pending, stores and saves it (0x432e8). */
void pll_scpi_command(pllc_request* request, uint8_t* reply_pending) {
  char needs_loop;
  /* Unset unless the request was sent: the image then reads its frame
   * (0x43346). */
  uint8_t loop_status;

  get_setup_vars(request);
  validate_or_doaction(&needs_loop, (char*)reply_pending);
  if (needs_loop == 0) {
    return;
  }
  douglas_log_event = LOG_CLEARED;
  if (pll_scpi_needs_loop() != 0) {
    if (pll_scpi_source == REQUEST_SOURCE_SCPI ||
        pll_scpi_source == REQUEST_SOURCE_SILENT_TO_GPS) {
      *reply_pending = pll_scpi_send_to_loop(GPS_SENDER_SETTING, &loop_status);
    }
    if (*reply_pending != 0) {
      return;
    }
    if (loop_status != 0) {
      status_report_from((int8_t)loop_status, pll_scpi_source);
    }
  }
  finish_scpi_cmd();
}

/* The pllc task's pass: unless a reply is pending, counts down a signal
 * delay or takes and handles a request from pllc_exchange, then signals
 * the waiting task (0x4336c). */
void pll_scpi_receive(void) {
  pllc_request request;

  if (pll_scpi_reply_pending != 0) {
    return;
  }
  if (pll_scpi_signal_delay != 0) {
    pll_scpi_signal_delay--;
    if (pll_scpi_signal_delay != 0) {
      return;
    }
  } else {
    if ((uint8_t)psos_request_x(pllc_exchange, &request, 1, 0) != 0) {
      return;
    }
    pll_scpi_command(&request, &pll_scpi_reply_pending);
    if (pll_scpi_reply_pending != 0 || pll_scpi_signal_delay != 0) {
      return;
    }
  }
  psos_signal_v(pll_scpi_waiting_task, PLLC_DONE_EVENT);
}

/* :DIAG:TOFFset's and :GPS:ADELay's action: the temporary-mode hold
 * cause (0x45b28). */
void temporary_hold_set(int32_t index, int32_t source) {
  pll.hold_causes |= HOLD_CAUSE_TEMPORARY_MODE;
  pll_scpi_signal_delay = 1;
}

/* nv_settings.field_b8's done hook, which does nothing (0x45ba6). */
void nv_param_field_b8_done(void) {}
