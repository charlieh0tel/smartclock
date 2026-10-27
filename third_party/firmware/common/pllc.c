/* The functions of pllc.c the images share, the same in each
 * (docs/firmware/rewrite.md, "Shared C"); each image's own
 * rewritten/pllc.c describes the module and holds the rest.  Written by
 * hand in place of the decompiler's (docs/firmware/README.md,
 * "Hand-written C"); each keeps its generated prototype. */

/* A descriptor of any type, as pll_scpi_param points at it: its first
 * byte says which. */
typedef union pllc_param {
  scpi_param_type type;
  scpi_numeric_param numeric;
  scpi_setting_param setting;
  scpi_action_param action;
} pllc_param;

static const uint16_t PLLC_FLAG_SAVED_IN_NV = 2;

static const uint16_t PLLC_FLAG_SAVED_IN_RECORD = 4;

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
 * descriptor's type and a numeric one's is_integer say. */
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
 * alone leaves a reply pending. */
void pll_scpi_do_action(uint8_t* reply_pending) {
  *reply_pending = pll_scpi_param == &scpi_action_self_test;
  ((pllc_action*)pllc_current()->action.action)((int8_t)pll_scpi_index,
                                                (int8_t)pll_scpi_source);
}

/* Whether a setting's new value differs from its current one, refusing
 * one above its maximum. */
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
 * one at the index's bounds and step. */
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
      /* read again after each validate, as the image does (validate_num+0x5a)
       */
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
 * of the request is to be done. */
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

/* The descriptor's flag to save its value in the NV register. */
uint16_t scpi_param_saved_in_nv(void) {
  return pllc_flags() & PLLC_FLAG_SAVED_IN_NV;
}

/* The descriptor's flag to save its value in the EEPROM record at
 * 0x400000. */
uint16_t scpi_param_saved_in_record(void) {
  return pllc_flags() & PLLC_FLAG_SAVED_IN_RECORD;
}

/* The GPS's reply to a request: finishes the pending one, reporting a
 * failure, and signals the waiting task; with none pending, counts the
 * reply in *unexpected. */
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
 * waiting task; with none pending, counts it in *unexpected. */
void gps_diag_complete(int32_t* unexpected) {
  if (pll_scpi_reply_pending == 0) {
    (*unexpected)++;
    return;
  }
  pll_scpi_reply_pending = 0;
  psos_signal_v(pll_scpi_waiting_task, PLLC_DONE_EVENT);
}

/* Does nothing: the changed hook of most numeric and setting descriptors,
 * and the done hook of some. */
void scpi_param_hook_nothing(void) {}

/* :DIAG:PTIMe:TINTerval?'s action, which does nothing. */
void pll_action_nothing(int32_t index, int32_t source) {}

/* Requests the self-test numbered by the index, unless one is. */
void selftest_request(int32_t index, int32_t source) {
  if (selftest_requested == 0) {
    selftest_number = index;
    selftest_requested = 1;
  }
}
