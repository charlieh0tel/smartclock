/* pllc's side of the SCPI settings, the firmware's own "pll_scpi"
 * (finish_scpi_cmd's message): the server that takes a setter's request
 * from the pllc exchange, validates it by its descriptor, sends what the
 * GPS must hear and stores and saves the rest; the descriptors'
 * validators and clamps; and the hooks and actions that no one command
 * owns (docs/firmware/structures.md, "Numeric SCPI parameters" and "The
 * pllc task's SCPI requests").  A command's own hooks and actions are in
 * its tree's file (scpi_source_ptime.c and the others).  Written by hand
 * in place of the decompiler's (docs/firmware/README.md, "Hand-written
 * C"); the exported C includes this file after its own, so the types
 * and prototypes it declares are in scope; each function here keeps its
 * generated prototype. */

/* A descriptor of any type, as pll_scpi_param points at it: its first
 * byte says which. */
typedef union pllc_param {
  scpi_param_type type;
  scpi_numeric_param numeric;
  scpi_setting_param setting;
  scpi_action_param action;
} pllc_param;

/* The descriptor type validate_or_doaction, get_setup_vars and
 * finish_scpi_cmd accept and do nothing with (scpi_param_type has no
 * member for it). */
#define PLLC_PARAM_TYPE_IGNORED 3

/* A numeric descriptor's is_integer values (structures.md, "The pllc
 * task's SCPI requests"): a float, an integer, index + 1 longs, two. */
#define PLLC_VALUE_FLOAT 0

#define PLLC_VALUE_INTEGER 1

#define PLLC_VALUE_LIST 2

#define PLLC_VALUE_PAIR 3

/* A descriptor's flags: the request goes to the GPS through
 * pll_scpi_send_to_loop, the value is saved in the NV register, and in
 * the EEPROM record at 0x400000. */
static const uint16_t PLLC_FLAG_SEND_TO_GPS = 1;

static const uint16_t PLLC_FLAG_SAVED_IN_NV = 2;

static const uint16_t PLLC_FLAG_SAVED_IN_RECORD = 4;

/* A request's source: the SCPI task, which waits for the done event; and
 * the other source whose requests go to the GPS (0x432fe). */
static const uint8_t PLLC_SOURCE_SCPI = 0;

static const uint8_t PLLC_SOURCE_ALSO_TO_GPS = 5;

/* The hooks' and actions' shapes, which the descriptors keep as void
 * pointers.  A changed hook is passed its caller's D3 first. */
typedef void pllc_changed_hook(uint32_t caller_d3, int32_t index,
                               int32_t source);

typedef void pllc_setting_done_hook(int32_t index, int32_t source);

typedef void pllc_numeric_done_hook(void);

typedef void pllc_action(int32_t index, int32_t source);

/* round_float_to_step and clamp_double_to_step_and_limits: beyond so
 * many steps the step is a thousand times coarser; a value rounds half
 * away from zero. */
static const double PLLC_COARSE_STEPS = 1e9;

static const float PLLC_COARSER = 1000.0f;

static const float PLLC_HALF = 0.5f;

static pllc_param* pllc_current(void) { return pll_scpi_param; }

/* The current descriptor's flags, a setting's at +0x14, any other's at
 * a numeric's +0x1e. */
static uint16_t pllc_flags(void) {
  pllc_param* param = pllc_current();

  return param->type == SCPI_PARAM_SETTING ? param->setting.flags
                                           : param->numeric.flags;
}

/* Copies a request into pll_scpi_param to pll_scpi_values, as its
 * descriptor's type and a numeric one's is_integer say (0x431f4). */
void get_setup_vars(pllc_request* request) {
  pll_scpi_source = request->source;
  if (pll_scpi_source == PLLC_SOURCE_SCPI) {
    pll_scpi_waiting_task = task_scpi;
  }
  pll_scpi_index = request->index;
  pll_scpi_param = request->param;
  pllc_param* param = pllc_current();
  switch (param->type) {
    case SCPI_PARAM_NUMERIC:
      switch ((int8_t)param->numeric.is_integer) {
        case PLLC_VALUE_FLOAT:
          pll_scpi_values[0] = request->values[1];
          break;
        case PLLC_VALUE_INTEGER:
          pll_scpi_values[0] = request->values[0];
          break;
        case PLLC_VALUE_LIST: {
          /* values[0] holds the list's address. */
          uint32_t* list = (uint32_t*)request->values[0];
          for (uint8_t i = 0; i <= (int8_t)pll_scpi_index; i++) {
            pll_scpi_values[i] = list[i];
          }
          break;
        }
        case PLLC_VALUE_PAIR:
          pll_scpi_values[0] = request->values[0];
          pll_scpi_values[1] = request->values[1];
          break;
        default:
          break;
      }
      break;
    case SCPI_PARAM_SETTING:
      pll_scpi_setting = request->setting;
      break;
    case SCPI_PARAM_ACTION:
    case PLLC_PARAM_TYPE_IGNORED:
      break;
    default:
      fatal_error("pll_scpi: get_setup_vars() -- unknown structure type");
      break;
  }
}

/* Runs an action's routine with the index and the source; the self-test's
 * alone leaves a reply pending (0x42b8a). */
void pll_scpi_do_action(uint8_t* reply_pending) {
  *reply_pending = pll_scpi_param == &scpi_action_self_test;
  ((pllc_action*)pllc_current()->action.action)((int8_t)pll_scpi_index,
                                                (int8_t)pll_scpi_source);
}

/* Whether a setting's new value differs from its current one, refusing
 * one above its maximum (0x42bc4). */
void toggle_change(uint8_t* changed) {
  scpi_setting_param* setting = &pllc_current()->setting;

  if (setting->maximum < (int8_t)pll_scpi_setting) {
    fatal_error("pll_scpi: toggle_change() -- new value > max value");
  } else if (setting->value[(int8_t)pll_scpi_index] != pll_scpi_setting) {
    *changed = 1;
    return;
  }
  *changed = 0;
}

/* Runs each new value through the numeric descriptor's validate into
 * pll_scpi_validated: index + 1 of them for a list or a pair, else the
 * one at the index's bounds and step (0x42c10). */
void validate_num(void) {
  /* the longest list; at it the loop writes past pll_scpi_validated,
   * over douglas_log_event, as the image does */
  static const int8_t MOST_VALUES = 4;
  scpi_numeric_param* numeric = &pllc_current()->numeric;

  if (numeric->is_integer == PLLC_VALUE_LIST ||
      numeric->is_integer == PLLC_VALUE_PAIR) {
    if ((int8_t)pll_scpi_index > MOST_VALUES) {
      fatal_error("pll_scpi: validate_num() -- too many in multiple");
    }
    if (pll_scpi_index != numeric->last_index) {
      fatal_error("pll_scpi: validate_num() -- multiple cmd/desc disagree");
    }
    for (uint8_t i = 0; i <= (int8_t)pll_scpi_index; i++) {
      /* read again after each validate, as the image does (0x42c6a) */
      numeric = &pllc_current()->numeric;
      pll_scpi_validated[i] = numeric->validate(
          &pll_scpi_values[i], i, (uint32_t*)numeric->minimum + i,
          (uint32_t*)numeric->maximum + i, (scpi_step*)numeric->steps + i,
          (int8_t)pll_scpi_source);
    }
  } else {
    int8_t index = (int8_t)pll_scpi_index;
    pll_scpi_validated[0] = numeric->validate(
        pll_scpi_values, index, (uint32_t*)numeric->minimum + index,
        (uint32_t*)numeric->maximum + index, (scpi_step*)numeric->steps + index,
        (int8_t)pll_scpi_source);
  }
}

/* Validates a request by its descriptor's type: a numeric one's values,
 * a setting's change, or an action run; *needs_loop is 1 when the rest
 * of the request is to be done (0x42d02). */
void validate_or_doaction(char* needs_loop, char* reply_pending) {
  pllc_param* param = pllc_current();

  *needs_loop = 0;
  if ((int8_t)param->numeric.last_index < (int8_t)pll_scpi_index) {
    fatal_error("pll_scpi: validate_or_doaction() -- setup index > max index");
    return;
  }
  switch (param->type) {
    case SCPI_PARAM_NUMERIC:
      *needs_loop = 1;
      validate_num();
      break;
    case SCPI_PARAM_SETTING:
      toggle_change((uint8_t*)needs_loop);
      break;
    case SCPI_PARAM_ACTION:
      *needs_loop = 0;
      pll_scpi_do_action((uint8_t*)reply_pending);
      break;
    case PLLC_PARAM_TYPE_IGNORED:
      break;
    default:
      fatal_error("pll_scpi: validate_or_doaction() -- unknown structure type");
      break;
  }
}

/* 1 when the request goes to the GPS before it is stored; a
 * :DOUGlas:STATe 0 from 0 instead logs position hold (0x42d78). */
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
 * *status the condition to report, 0 for none (0x42dc4). */
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
  request.argument_mode = 0;
  request.result = 1;
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

/* The descriptor's flag to save its value in the NV register (0x4301a). */
uint16_t scpi_param_saved_in_nv(void) {
  return pllc_flags() & PLLC_FLAG_SAVED_IN_NV;
}

/* The descriptor's flag to save its value in the EEPROM record at
 * 0x400000 (0x43042). */
uint16_t scpi_param_saved_in_record(void) {
  return pllc_flags() & PLLC_FLAG_SAVED_IN_RECORD;
}

/* Stores a request's validated values or setting, calling the
 * descriptor's hooks, saves them as its flags say, and logs a
 * :DOUGlas:STATe change (0x4306a). */
void finish_scpi_cmd(void) {
  /* The image passes each changed hook a byte of its caller's D3, which
   * it never sets (0x430b6, 0x43114, 0x4315c); read before gcc's code
   * uses the register. */
  register uint32_t entry_d3 __asm__("d3");
  __asm__("" : "=r"(entry_d3));
  uint32_t caller_d3 = (uint8_t)entry_d3;
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
    case PLLC_PARAM_TYPE_IGNORED:
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
 * unless a reply from it is pending, stores and saves it (0x432bc). */
void pll_scpi_command(pllc_request* request, uint8_t* reply_pending) {
  char needs_loop;
  /* Unset unless the request was sent: the image then reads its frame
   * (0x4331a). */
  uint8_t loop_status;

  get_setup_vars(request);
  validate_or_doaction(&needs_loop, (char*)reply_pending);
  if (needs_loop == 0) {
    return;
  }
  douglas_log_event = LOG_CLEARED;
  if (pll_scpi_needs_loop() != 0) {
    if (pll_scpi_source == PLLC_SOURCE_SCPI ||
        pll_scpi_source == PLLC_SOURCE_ALSO_TO_GPS) {
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
 * the waiting task (0x43340). */
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

/* The GPS's reply to a request: finishes the pending one, reporting a
 * failure, and signals the waiting task; with none pending, counts the
 * reply in *unexpected (0x433b6). */
void pll_scpi_complete(int32_t* unexpected, char success) {
  if (pll_scpi_reply_pending == 0) {
    (*unexpected)++;
    return;
  }
  pll_scpi_reply_pending = 0;
  if (success == 0) {
    status_report_from(STATUS_EXECUTION_ERROR, pll_scpi_source);
  }
  finish_scpi_cmd();
  psos_signal_v(pll_scpi_waiting_task, PLLC_DONE_EVENT);
}

/* The GPS self-test's end: ends the pending reply and signals the
 * waiting task; with none pending, counts it in *unexpected (0x433fc). */
void gps_diag_complete(int32_t* unexpected) {
  if (pll_scpi_reply_pending == 0) {
    (*unexpected)++;
    return;
  }
  pll_scpi_reply_pending = 0;
  psos_signal_v(pll_scpi_waiting_task, PLLC_DONE_EVENT);
}

/* Prints that a value was clamped to its minimum (0) or maximum, and
 * reports it out of range (0x31ff8). */
void report_clamped(char too_large, uint8_t source) {
  if (too_large == 0) {
    task_dmessage("Value too small, set to min limit\r\n");
  } else {
    task_dmessage("Value too large, set to max limit\r\n");
  }
  status_report_from(STATUS_DATA_OUT_OF_RANGE, source);
}

/* A float clamped to its minimum and maximum into *clamped, reporting a
 * clamp: 1 if it clamped (0x3209c). */
uint32_t clamp_float(float* value, float* clamped, float* minimum,
                     float* maximum, uint8_t source) {
  if (float_compare(*minimum, *value) > 0) {
    report_clamped(0, source);
    *clamped = *minimum;
    return 1;
  }
  if (float_compare(*value, *maximum) > 0) {
    report_clamped(1, source);
    *clamped = *maximum;
    return 1;
  }
  *clamped = *value;
  return 0;
}

/* A float rounded to the nearest multiple of its step, half away from
 * zero, the step a thousand times coarser beyond 1e9 steps (0x3217e). */
float round_float_to_step(float* value, float* step_pointer) {
  float step = *step_pointer;
  double rounded = float_to_double(*value);
  double whole;

  float steps = double_to_float(
      double_divide_reversed_pointer(float_to_double(step), &rounded));
  if (double_compare(float_to_double(steps), PLLC_COARSE_STEPS) < 0 ||
      double_compare(float_to_double(steps), -PLLC_COARSE_STEPS) > 0) {
    step = float_multiply(step, PLLC_COARSER);
  }
  float half = double_compare(0.0, rounded) < 0 ? -PLLC_HALF : PLLC_HALF;
  double_add_in_place(&rounded, float_to_double(float_multiply(half, step)));
  double_divide_in_place(&rounded, float_to_double(step));
  modf(rounded, &whole);
  double_multiply_in_place(&whole, float_to_double(step));
  return double_to_float(whole);
}

/* A long rounded toward zero to its step, then clamped to its minimum and
 * maximum, reporting a clamp (0x322b4). */
int32_t clamp_to_limits(int32_t* value, int32_t index, int32_t* minimum,
                        int32_t* maximum, scpi_step* step, int32_t source) {
  int32_t step_size = step->function(step, value, index);

  if (step_size != 1) {
    *value = step_size * (*value / step_size);
  }
  if (*value > *maximum) {
    report_clamped(1, (uint8_t)source);
    return *maximum;
  }
  if (*value < *minimum) {
    report_clamped(0, (uint8_t)source);
    return *minimum;
  }
  return *value;
}

/* The larger of two floats (0x323b2). */
float float_max_of(float* a, float* b) {
  return float_compare(*a, *b) > 0 ? *a : *b;
}

/* The smaller of two floats (0x323d6). */
float float_min_of(float* a, float* b) {
  return float_compare(*b, *a) > 0 ? *a : *b;
}

/* A double clamped to its minimum and maximum, then divided by its step
 * and rounded half away from zero, the step a thousand times coarser
 * beyond 1e9 steps: the steps, as an int (0x32596). */
int32_t clamp_double_to_step_and_limits(double* value, uint32_t index,
                                        double* minimum, double* maximum,
                                        scpi_double_step* step_record,
                                        int32_t source) {
  double clamped;
  double whole;

  /* clamp_double's prototype stores the double through a long pointer. */
  clamp_double(value, (uint32_t*)&clamped, minimum, maximum, (uint8_t)source);
  double step = step_record->function(step_record, &clamped, index);
  double steps = double_divide_reversed_pointers(&step, &clamped);
  if (double_compare(PLLC_COARSE_STEPS, steps) > 0 ||
      double_compare(-PLLC_COARSE_STEPS, steps) < 0) {
    double_multiply_in_place(&step, PLLC_COARSER);
  }
  double half = double_compare(0.0, clamped) < 0 ? -PLLC_HALF : PLLC_HALF;
  double_add_in_place(&clamped, double_multiply_pointers(&step, &half));
  double_divide_in_place(&clamped, step);
  modf(clamped, &whole);
  return double_to_integer(whole);
}

/* A step record's long, the position setters' unit step; load_long's
 * code again (0x32e26). */
uint32_t load_long_unit_step(uint32_t* step) { return *step; }

/* A step record's long, the step of most integer descriptors (0x32e2e). */
uint32_t load_long(uint32_t* step) { return *step; }

/* Does nothing: the changed hook of most numeric and setting descriptors,
 * and the done hook of some (0x12af0). */
void scpi_param_hook_nothing(void) {}

/* :DIAG:PTIMe:TINTerval?'s action, which does nothing (0x41328). */
void pll_action_nothing(int32_t index, int32_t source) {}

/* :DIAG:TOFFset's and :GPS:ADELay's action: the temporary-mode hold
 * cause (0x45af0). */
void temporary_hold_set(int32_t index, int32_t source) {
  pll.hold_causes |= HOLD_CAUSE_TEMPORARY_MODE;
  pll_scpi_signal_delay = 1;
}

/* nv_settings.field_b8's done hook, which does nothing (0x45b6e). */
void nv_param_field_b8_done(void) {}

/* Requests the self-test numbered by the index, unless one is (0x46326). */
void selftest_request(int32_t index, int32_t source) {
  if (selftest_requested == 0) {
    selftest_number = index;
    selftest_requested = 1;
  }
}
