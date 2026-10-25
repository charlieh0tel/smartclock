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

/* A descriptor of any type, as pll_scpi_param points at it: its first
 * byte says which. */
typedef union pllc_param {
  scpi_param_type type;
  scpi_numeric_param numeric;
  scpi_setting_param setting;
  scpi_action_param action;
} pllc_param;

/* A descriptor's flags: the request goes to the GPS through
 * pll_scpi_send_to_loop, the value is saved in the NV register, and in
 * the EEPROM record at 0x400000. */
static const uint16_t PLLC_FLAG_SEND_TO_GPS = 1;

static const uint16_t PLLC_FLAG_SAVED_IN_NV = 2;

static const uint16_t PLLC_FLAG_SAVED_IN_RECORD = 4;

/* The hooks' and actions' shapes, which the descriptors keep as void
 * pointers.  A changed hook is passed its caller's D3 first. */
typedef void pllc_changed_hook(uint32_t caller_d3, int32_t index,
                               int32_t source);

typedef void pllc_setting_done_hook(int32_t index, int32_t source);

typedef void pllc_numeric_done_hook(void);

typedef void pllc_action(int32_t index, int32_t source);

static pllc_param* pllc_current(void) { return pll_scpi_param; }

/* The current descriptor's flags, a setting's at +0x14, any other's at
 * a numeric's +0x1e. */
static uint16_t pllc_flags(void) {
  pllc_param* param = pllc_current();

  return param->type == SCPI_PARAM_SETTING ? param->setting.flags
                                           : param->numeric.flags;
}

/* Copies a request into pll_scpi_param to pll_scpi_values, as its
 * descriptor's type and a numeric one's is_integer say (0x4325a). */
void get_setup_vars(pllc_request* request) {
  pll_scpi_source = request->source;
  if (pll_scpi_source == REQUEST_SOURCE_SCPI) {
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
    case SCPI_PARAM_DOUBLE:
      break;
    default:
      fatal_error("pll_scpi: get_setup_vars() -- unknown structure type");
      break;
  }
}

/* Runs an action's routine with the index and the source; the self-test's
 * alone leaves a reply pending (0x42bc2). */
void pll_scpi_do_action(uint8_t* reply_pending) {
  *reply_pending = pll_scpi_param == &scpi_action_self_test;
  ((pllc_action*)pllc_current()->action.action)((int8_t)pll_scpi_index,
                                                (int8_t)pll_scpi_source);
}

/* Whether a setting's new value differs from its current one, refusing
 * one above its maximum (0x42bfc). */
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
 * one at the index's bounds and step (0x42c48). */
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
 * of the request is to be done (0x42d3a). */
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
    case SCPI_PARAM_DOUBLE:
      break;
    default:
      fatal_error("pll_scpi: validate_or_doaction() -- unknown structure type");
      break;
  }
}

/* 1 when the request goes to the GPS before it is stored; a
 * :DOUGlas:STATe 0 from 0 instead logs position hold (0x42db0). */
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

/* The descriptor's flag to save its value in the NV register (0x43080). */
uint16_t scpi_param_saved_in_nv(void) {
  return pllc_flags() & PLLC_FLAG_SAVED_IN_NV;
}

/* The descriptor's flag to save its value in the EEPROM record at
 * 0x400000 (0x430a8). */
uint16_t scpi_param_saved_in_record(void) {
  return pllc_flags() & PLLC_FLAG_SAVED_IN_RECORD;
}

/* The GPS's reply to a request: finishes the pending one, reporting a
 * failure, and signals the waiting task; with none pending, counts the
 * reply in *unexpected (0x43416). */
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
 * waiting task; with none pending, counts it in *unexpected (0x4345c). */
void gps_diag_complete(int32_t* unexpected) {
  if (pll_scpi_reply_pending == 0) {
    (*unexpected)++;
    return;
  }
  pll_scpi_reply_pending = 0;
  psos_signal_v(pll_scpi_waiting_task, PLLC_DONE_EVENT);
}

/* Does nothing: the changed hook of most numeric and setting descriptors,
 * and the done hook of some (0x12c36). */
void scpi_param_hook_nothing(void) {}

/* :DIAG:PTIMe:TINTerval?'s action, which does nothing (0x413c8). */
void pll_action_nothing(int32_t index, int32_t source) {}

/* nv_settings.field_be's done hook, which does nothing (0x45d42). */
void nv_param_field_b8_done(void) {}

/* :DIAG:TOFFset's and :GPS:ADELay's action: the temporary-mode hold
 * cause, with the TI limit overridden, which the Z3805A's does not
 * set (0x45cbc). */
void temporary_hold_set(int32_t index, int32_t source) {
  pll.hold_causes |= HOLD_CAUSE_TEMPORARY_MODE;
  pll.ti_limit_override = 1;
  pll_scpi_signal_delay = 1;
}

/* Requests the self-test numbered by the index, unless one is (0x4651e). */
void selftest_request(int32_t index, int32_t source) {
  if (selftest_requested == 0) {
    selftest_number = index;
    selftest_requested = 1;
  }
}
