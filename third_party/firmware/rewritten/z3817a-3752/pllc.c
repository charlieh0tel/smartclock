/* From the Z3815A 4010's rewritten/pllc.c, the Z3817A 3752's own beside
 * common/'s: of its functions, those the Z3817A's code carries identical or
 * relocated (docs/firmware/z3817a-3752.md, "The function check"); then the
 * settings server's sender and finisher, the 58503A 3704's, and the EFC
 * control mode's done hook, each written from the image's code.  Its
 * comments' addresses are the Z3817A's, but where they name another image.
 * Each keeps its generated prototype. */

/* Clears the temporary-mode hold cause temporary_hold_set sets
 * (0x4b042). */
void temporary_hold_clear(void) {
  pll.hold_causes &= ~HOLD_CAUSE_TEMPORARY_MODE;
}

/* :DIAG:TOFFset's done hook: clears the temporary-mode hold cause and
 * saves the calibration block (0x4ae14), the 58503A 3704's.  It passes
 * calibration_block_save_reported the low bytes of its own two stack
 * arguments, sign-extended, which finish_scpi_cmd does not push for a
 * numeric done hook: what its frame holds there. */
void time_offset_done(void) {
  const int8_t index = (int8_t)CPU_CALLERS_STACK_LONG(0);
  const int8_t source = (int8_t)CPU_CALLERS_STACK_LONG(1);

  temporary_hold_clear();
  calibration_block_save_reported(index, source);
}

/* The span a fit is allowed off by, half an EFC history entry; the time
 * uncertainty's nanoseconds, and the most the test logs, a second either way;
 * and the test's duration rounded to hours, from a quarter of one; and the time
 * from which a new month counts, 00:03:00. */
static const float PLLC_HALF_ENTRY = 0.5f;

static const float PLLC_NANOSECONDS = 1e9f;

static const int32_t PLLC_PTEST_UNAVAILABLE_NANOSECONDS = 1000000000;

static const uint32_t PLLC_PTEST_HOUR_ROUNDING = 2700;

/* value clamped to least and most, its tests in the image's order: least
 * against value, the lesser against most, then least against value
 * again, as ptest_efc_average and ptest_log clamp. */
static float pllc_clamp(float value, float least, float most) {
  const float raised = float_compare(least, value) > 0 ? least : value;

  if (float_compare(raised, most) > 0) {
    return most;
  }
  return float_compare(least, value) > 0 ? least : value;
}

/* :PTESt:TYPE's done hook: the test turned off, as :PTESt:MODE's hook
 * then takes it (0x46100). */
void ptest_type_done(void) {
  scpi_value_DIAGnostic_ROSCillator_PTESt_MODE = 0;
  ptest_mode_done();
}

/* The done hook of :PTESt:MODE, :COUNt, :TTHR and :PTHR: the test reset
 * while its mode is off, else armed from off, or a running one checked
 * against the settings (0x4610e). */
void ptest_mode_done(void) {
  if (scpi_value_DIAGnostic_ROSCillator_PTESt_MODE == 0) {
    ptest_reset();
  } else if (ptest.state == PTEST_STATE_OFF) {
    ptest.state = PTEST_STATE_ARMED;
  } else if (ptest.state == PTEST_STATE_RUNNING) {
    ptest_check();
  }
}

/* :PTESt:DURation?: the hours the test has run, to now while it runs,
 * else to its end, its seconds rounded up from a quarter hour; 0 off or
 * armed (0x4613a). */
uint16_t ptest_duration_hours(void) {
  if (ptest.state == PTEST_STATE_OFF || ptest.state == PTEST_STATE_ARMED) {
    return 0;
  }
  const uint32_t end =
      ptest.state == PTEST_STATE_RUNNING ? get_uptime() : ptest.end_uptime;

  return (uint16_t)((end - ptest.start_uptime + PLLC_PTEST_HOUR_ROUNDING) /
                    (uint32_t)CLOCK_SECONDS_PER_HOUR);
}

/* :PTESt:STATe?'s second value, the passes in a row (0x46182). */
uint8_t ptest_passes(void) { return ptest.passes; }

/* The test's state, for :PTESt:STATe? (0x4618a). */
ptest_state ptest_state_get(void) { return ptest.state; }

/* The test off, its run cleared (0x46192). */
void ptest_reset(void) {
  ptest.state = PTEST_STATE_OFF;
  ptest.passes = 0;
  ptest.start_uptime = 0;
  ptest.end_uptime = 0;
}

/* Logs an armed test's entry while its uncertainty is not valid, its
 * format the image's own text, whose address the entry keeps
 * (0x46218). */
void ptest_log_unavailable(void) {
  const int32_t average = ptest_efc_average();

  log_write(scpi_value_DIAGnostic_ROSCillator_PTESt_TYPE == PTEST_TYPE_PREDICTED
                ? ptest_log_predicted_unavailable
                : ptest_log_measured_unavailable,
            average);
}

/* Logs a running test's entry: the uncertainty in ns, rounded, the
 * predicted one's clamped to 0 to 1e9, the measured one's to plus or
 * minus 1e9 and 1e9 while it is not valid; and the EFC average
 * (0x46244). */
void ptest_log(float uncertainty, uint8_t valid) {
  int32_t nanoseconds;

  if (scpi_value_DIAGnostic_ROSCillator_PTESt_TYPE == PTEST_TYPE_PREDICTED) {
    nanoseconds = float_to_integer(
        pllc_clamp(float_add(float_multiply(uncertainty, PLLC_NANOSECONDS),
                             PLLC_HALF_ENTRY),
                   0.0f, PLLC_NANOSECONDS));
    log_write(ptest_log_predicted, nanoseconds, ptest_efc_average());
    return;
  }
  if (valid == 0) {
    nanoseconds = PLLC_PTEST_UNAVAILABLE_NANOSECONDS;
  } else {
    const float rounding = float_compare(0.0f, uncertainty) > 0
                               ? -PLLC_HALF_ENTRY
                               : PLLC_HALF_ENTRY;

    nanoseconds = float_to_integer(pllc_clamp(
        float_add(float_multiply(uncertainty, PLLC_NANOSECONDS), rounding),
        -PLLC_NANOSECONDS, PLLC_NANOSECONDS));
  }
  log_write(ptest_log_measured, nanoseconds, ptest_efc_average());
}

/* pll.field_3eb from tfom_code of a TFOM, the 58503A 3704's field_3e6;
 * nothing calls it (0x4b220). */
void pll_tfom_digit_store(uint32_t tfom) { pll.field_3eb = tfom_code(tfom); }

/* The pllc task's pass: unless a reply is pending, counts down a signal
 * delay or takes a request from pllc_exchange, sets it up and handles
 * it, then signals the waiting task (0x48518). */
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
    get_setup_vars(&request);
    pll_scpi_command(&pll_scpi_reply_pending);
    if (pll_scpi_reply_pending != 0 || pll_scpi_signal_delay != 0) {
      return;
    }
  }
  psos_signal_v(pll_scpi_waiting_task, PLLC_DONE_EVENT);
}

/* :SYSTem:PON's action: in supervisor mode, zeroes the preserved region
 * a long at a time up to reset_cleared_ram, so that the restart that
 * trap #12 makes finds nothing to restore (docs/firmware/restart.md)
 * (0x4b200). */
void power_on_reset(void) {
  enter_supervisor_mode();
  for (uint32_t* next = (uint32_t*)&preserved_region;
       next < (uint32_t*)&reset_cleared_ram; next++) {
    *next = 0;
  }
  __m68k_trap(12);
}

/* :DIAG:TOFFset's and :GPS:ADELay's action: the temporary-mode hold
 * cause, with the TI limit overridden, which the Z3805A's does not
 * set (0x4b028). */
void temporary_hold_set(int32_t index, int32_t source) {
  pll.hold_causes |= HOLD_CAUSE_TEMPORARY_MODE;
  pll.ti_limit_override = 1;
  pll_scpi_signal_delay = 1;
}

/* Handles the request get_setup_vars took: validates it, sends what the
 * GPS must hear, and, unless a reply from it is pending, stores and
 * saves it (0x48496).  The 58503A 3704's, but that a request from
 * another source than SCPI or the silent one goes to the GPS too, as the
 * setting's other sender (0x48504). */
void pll_scpi_command(uint8_t* reply_pending) {
  char needs_loop;
  /* Unset unless the request was sent: the image then reads its frame. */
  uint8_t loop_status;

  validate_or_doaction(&needs_loop, (char*)reply_pending);
  if (needs_loop == 0) {
    return;
  }
  douglas_log_event = LOG_CLEARED;
  if (pll_scpi_needs_loop() != 0) {
    const gps_request_sender sender =
        pll_scpi_source == REQUEST_SOURCE_SCPI ||
                pll_scpi_source == REQUEST_SOURCE_SILENT_TO_GPS
            ? GPS_SENDER_SETTING
            : GPS_SENDER_SETTING_OTHER;

    *reply_pending = pll_scpi_send_to_loop(sender, &loop_status);
    if (*reply_pending != 0) {
      return;
    }
    if (loop_status != 0) {
      status_report_from((int8_t)loop_status, pll_scpi_source);
    }
  }
  finish_scpi_cmd();
}

/* Copies a request into pll_scpi_param to pll_scpi_values, as its
 * descriptor's type and a numeric one's is_integer say (0x483c4).  The
 * 58503A 3704's, but that a request from another source than SCPI waits
 * on the task "sci" (task_sci) where the 58503A's leaves the waiting task
 * as it was. */
void get_setup_vars(pllc_request* request) {
  pll_scpi_source = request->source;
  pll_scpi_waiting_task =
      pll_scpi_source == REQUEST_SOURCE_SCPI ? task_scpi : task_sci;
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

/* 1 when the request goes to the GPS before it is stored; a
 * :DOUGlas:STATe 0 from 0 instead logs position hold (0x47f02). */
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

/* The time error a holdover begun the given number of EFC history entries
 * ago would have made: each entry since, its EFC average less what the
 * fit kept at that entry gives, times the EFC's gain there by the OCXO
 * calibration, a + b efc^2, over the entry's 2700 s, summed into
 * *uncertainty ("calc_hold").  32 entries take the one the newest
 * replaced.  0, and 0 in *uncertainty, when that entry's time is not the
 * entries' span back within half an entry, or it has no weight
 * (0x45dea).  The Z3815A's, but that it keeps the sum, where the Z3815A's
 * keeps the greatest magnitude the sum reaches. */
uint8_t measured_uncertainty_compute(int8_t entries, float* uncertainty) {
  static const float ENTRY_SECONDS = 2700.0f;
  const int32_t newest = aging_curve.newest_index;
  uint8_t valid = 1;
  float span;
  uint8_t weight;
  char line[80];

  *uncertainty = 0.0f;
  if (entries == PLL_AGING_ENTRIES) {
    span = float_subtract(pll.efc_history.times[newest],
                          pll.efc_history.replaced_time);
    weight = pll.efc_history.replaced_weight;
  } else {
    const int32_t start = (newest - entries) & PLL_AGING_INDEX_MASK;

    span = float_subtract(pll.efc_history.times[newest],
                          pll.efc_history.times[start]);
    weight = pll.efc_history.weights[start];
  }
  if (float_compare(float_add(integer_to_float(entries), -PLLC_HALF_ENTRY),
                    span) > 0 ||
      float_compare(float_add(integer_to_float(entries), PLLC_HALF_ENTRY),
                    span) < 0 ||
      weight == 0) {
    valid = 0;
  } else {
    const int32_t start = (newest - entries) & PLL_AGING_INDEX_MASK;
    const float a = pll.efc_history.fit_a_history[start];
    const float b = pll.efc_history.fit_b_history[start];
    const float c = pll.efc_history.fit_c_history[start];
    uint8_t index = (uint8_t)start;

    do {
      index = (index + 1) & PLL_AGING_INDEX_MASK;
      const float time = pll.efc_history.times[index];
      const float log_term =
          float_multiply(double_to_float(log(float_to_double(time))), c);
      const float fit =
          float_add(float_add(float_multiply(b, time), a), log_term);
      const float average = pll.efc_history.averages[index];
      const float residual = float_subtract_reversed(fit, average);
      const float gain = float_add(
          float_multiply(
              float_multiply(scpi_value_DIAGnostic_ROSCillator_EFControl_BSLOPe,
                             average),
              average),
          scpi_value_DIAGnostic_ROSCillator_EFControl_ASLOPe);

      float_add_in_place(
          uncertainty,
          float_multiply(float_multiply(residual, gain), ENTRY_SECONDS));
    } while (index != newest);
  }
  if (pll_debug_enabled != 0) {
    const double hold = float_to_double(*uncertainty);
    const double learn = float_to_double(
        pll.efc_history
            .times[(newest - PLL_AGING_ENTRIES) & PLL_AGING_INDEX_MASK]);

    debug_sprintf(line, "learn_time= %f calc_hold= %e valid= %d\r\n", learn,
                  hold, (uint32_t)valid);
    task_dmessage(line);
  }
  return valid;
}

/* Ends a running time uncertainty test: passed after :PTESt:COUNt passes
 * in a row; failed at the second :PTESt:TTHR's hours, or from the first's
 * on with a valid uncertainty, the predicted or the measured one's
 * magnitude by :PTESt:TYPE, not within the first :PTESt:PTHR
 * (0x46062). */
void ptest_check(void) {
  float uncertainty;
  uint8_t valid;

  if (ptest.state != PTEST_STATE_RUNNING) {
    return;
  }
  if (scpi_value_DIAGnostic_ROSCillator_PTESt_TYPE == PTEST_TYPE_PREDICTED) {
    uncertainty = pll.predicted_uncertainty;
    valid = 1;
  } else {
    uncertainty = float_absolute_on_stack(pll.measured_uncertainty);
    valid = pll.measured_uncertainty_valid;
  }
  const int32_t hours = ptest_duration_hours();

  if ((int32_t)ptest.passes >= scpi_value_DIAGnostic_ROSCillator_PTESt_COUNt) {
    ptest.end_uptime = get_uptime();
    ptest.state = PTEST_STATE_PASSED;
  } else if (hours >= scpi_value_DIAGnostic_ROSCillator_PTESt_TTHR[1] ||
             (hours >= scpi_value_DIAGnostic_ROSCillator_PTESt_TTHR[0] &&
              valid != 0 &&
              float_compare(uncertainty,
                            scpi_value_DIAGnostic_ROSCillator_PTESt_PTHR[0]) >=
                  0)) {
    ptest.end_uptime = get_uptime();
    ptest.state = PTEST_STATE_FAILED;
  }
}

/* The latest EFC history entry's average, rounded and clamped to 0 to
 * 1e9, the test's log's "efc avg" (0x461a8). */
int32_t ptest_efc_average(void) {
  const float average = float_add(
      pll.efc_history
          .averages[(pll.efc_history.stop_ptr - 1) & PLL_AGING_INDEX_MASK],
      PLLC_HALF_ENTRY);

  return float_to_integer(pllc_clamp(average, 0.0f, PLLC_NANOSECONDS));
}

/* The measured time uncertainty over the whole EFC history, and whether
 * it is valid, pr_bad_act's "tunc:meas"; then the fit kept at the newest
 * entry, for the uncertainty's next (0x46016). */
void measured_uncertainty_update(void) {
  pll.measured_uncertainty_valid = measured_uncertainty_compute(
      PLL_AGING_ENTRIES, &pll.measured_uncertainty);
  pll.efc_history.fit_a_history[aging_curve.newest_index] = aging_curve.a;
  pll.efc_history.fit_b_history[aging_curve.newest_index] = aging_curve.b;
  pll.efc_history.fit_c_history[aging_curve.newest_index] = aging_curve.c;
}

/* Steps the time uncertainty test at each EFC history entry: an armed
 * test starts running once its uncertainty is valid, else logs that it
 * is not; a running one counts the entries in a row whose valid
 * uncertainty is within the second :PTESt:PTHR, is checked and logs the
 * entry (0x46398). */
void ptest_update(void) {
  float uncertainty;
  uint8_t valid;

  if (scpi_value_DIAGnostic_ROSCillator_PTESt_TYPE == PTEST_TYPE_PREDICTED) {
    uncertainty = pll.predicted_uncertainty;
    valid = 1;
  } else {
    uncertainty = pll.measured_uncertainty;
    valid = pll.measured_uncertainty_valid;
  }
  if (ptest.state == PTEST_STATE_ARMED) {
    if (valid != 0) {
      ptest.start_uptime = get_uptime();
      ptest.state = PTEST_STATE_RUNNING;
    } else {
      ptest_log_unavailable();
    }
  }
  if (ptest.state == PTEST_STATE_RUNNING) {
    if (valid != 0 &&
        float_compare(float_absolute_on_stack(uncertainty),
                      scpi_value_DIAGnostic_ROSCillator_PTESt_PTHR[1]) < 0) {
      ptest.passes++;
    } else {
      ptest.passes = 0;
    }
    ptest_check();
    ptest_log(uncertainty, valid);
  }
}

/* After how many of pllp_task's passes the timed EFC control mode
 * expires. */
static const uint32_t PLLC_EFC_MODE_PASSES = 600;

/* pllp_task's: in EFC control mode 2, after 600 passes, the hold cause
 * and the mode cleared (revisions.md, "Holdover cause") (0x4afb2). */
void efc_mode_expire(void) {
  if (diag_efc_mode == EFC_MODE_TIMED &&
      ++pll.efc_control_passes > PLLC_EFC_MODE_PASSES) {
    pll.hold_causes &= ~HOLD_CAUSE_EFC_CONTROL;
    pll.efc_control_passes = 0;
    diag_efc_mode = EFC_MODE_NORMAL;
  }
}

/* :DIAGnostic:ROSCillator:LTIMe:INIT's action: keeps the initial time
 * constant as it was, for the done hook, and holds the loop (0x4ae34). */
void loop_time_initial_hold(int32_t index, int32_t source) {
  initial_loop_time_before = initial_loop_time;
  temporary_hold_set(index, source);
}

/* :DIAGnostic:ROSCillator:LTIMe:INIT's done hook: the largest time
 * constant at least the initial; the ramped one restarted at the new
 * initial unless it had passed the old and is not below the new, then
 * kept to the largest; the hold released (0x4ae46). */
void loop_time_initial_done(void) {
  if (float_compare(initial_loop_time, max_loop_time) > 0) {
    max_loop_time = initial_loop_time;
  }
  if (float_compare(pll.loop_time_constant, initial_loop_time_before) > 0 &&
      float_compare(initial_loop_time, pll.loop_time_constant) <= 0) {
    if (float_compare(pll.loop_time_constant, max_loop_time) > 0) {
      pll.loop_time_constant = max_loop_time;
    }
  } else {
    pll.loop_time_constant = initial_loop_time;
  }
  temporary_hold_clear();
}

/* :DIAGnostic:ROSCillator:LTIMe:MAX's done hook: the initial time
 * constant at most the largest, the ramped one kept between them, and
 * the hold released (0x4aeb8). */
void loop_time_max_done(void) {
  if (float_compare(initial_loop_time, max_loop_time) > 0) {
    initial_loop_time = max_loop_time;
  }
  if (float_compare(pll.loop_time_constant, max_loop_time) > 0) {
    pll.loop_time_constant = max_loop_time;
  } else if (float_compare(initial_loop_time, pll.loop_time_constant) > 0) {
    pll.loop_time_constant = initial_loop_time;
  }
  temporary_hold_clear();
}

/* The done hook of :DIAGnostic:ROSCillator:EFControl:MODE and :DATA
 * (their descriptors' +0x1a, 0x44428 and 0x44456): hold cause
 * HOLD_CAUSE_EFC_CONTROL while the mode is set, with the TI limit
 * overridden and the SCPI task signaled a pass late; efc_clamp given
 * :DATA's value while the mode is set, else the loop's EFC, its result
 * dropped; pll.efc_control_passes cleared (0x4af64).  The 58503A's
 * calls no efc_clamp and sets no override. */
void efc_mode_done(void) {
  float efc;

  if (diag_efc_mode == EFC_MODE_NORMAL) {
    pll.hold_causes &= ~HOLD_CAUSE_EFC_CONTROL;
    efc = pll.efc;
  } else {
    pll.hold_causes |= HOLD_CAUSE_EFC_CONTROL;
    pll.ti_limit_override = 1;
    pll_scpi_signal_delay = 1;
    efc = integer_to_float(scpi_value_DIAGnostic_ROSCillator_EFControl_DATA);
  }
  efc_clamp(efc);
  pll.efc_control_passes = 0;
}

/* The hooks' and actions' shapes, which the descriptors keep as void
 * pointers.  A changed hook is passed its caller's D3 first. */
typedef void pllc_changed_hook(uint32_t caller_d3, int32_t index,
                               int32_t source);

typedef void pllc_setting_done_hook(int32_t index, int32_t source);

typedef void pllc_numeric_done_hook(void);

/* Sends the descriptor's GPS request to gpsx_exchange, its arguments in
 * pll_scpi_gps_arguments by the request; 1 when sent, else 0 and in
 * *status the condition to report, 0 for none (0x47f4e).  The 58503A
 * 3704's, its codes this image's, with one more taking its value,
 * GPS_STEP_FIELD_577_SET (0x4803c). */
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
    case GPS_STEP_GPS_HYSTERESIS:
    case GPS_STEP_EXTERNAL_HYSTERESIS:
    case GPS_STEP_FIELD_577_SET:
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
    case GPS_STEP_TRACKING_LOG:
      arguments[0] = (int8_t)pll_scpi_setting;
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
    case GPS_SCRIPT_TRAIM:
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
    case GPS_REQUEST_DEFAULTS:
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
 * :DOUGlas:STATe change (0x48228).  The 58503A 3704's. */
void finish_scpi_cmd(void) {
  /* The image passes each changed hook a byte of its caller's D3, which
   * it never sets; read before gcc's code uses the register. */
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
    parameter_record_save_reported(pll_scpi_source);
  }
  if (douglas_log_event == LOG_SURVEY_STARTED) {
    log_event(LOG_SURVEY_STARTED);
    status_happening(STATUS_POSITION_HOLD_CLEARED);
  } else if (douglas_log_event == LOG_POSITION_HOLD_STARTED) {
    log_event(LOG_POSITION_HOLD_STARTED);
    status_happening(STATUS_POSITION_HOLD_SET);
  }
}
