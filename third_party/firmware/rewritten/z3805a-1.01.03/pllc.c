/* From the 58503B 1.01.04's rewritten/pllc.c, the Z3805A 1.01.03's own
 * beside common/'s: of its functions, those the Z3805A 1.01.03's code
 * carries identical or relocated (docs/firmware/z3805a-1.01.03.md, "The
 * link list"). Its comments' addresses are the Z3805A 1.01.03's, but where
 * they name another image. Each keeps its generated prototype. With them
 * initial_loop_time_keep, this image's own, pll_scpi_needs_loop and
 * get_setup_vars, the Z3805A 3543B's C read against this image's code, and
 * pll_scpi_send_to_loop, read from this image's code. */

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

/* After how many of pllp_task's passes the timed EFC control mode
 * expires. */
static const uint32_t PLLC_EFC_MODE_PASSES = 600;

/* :SYSTem:PON's action: in supervisor mode, zeroes the preserved region
 * a long at a time up to reset_cleared_ram, so that the restart that
 * trap #12 makes finds nothing to restore (docs/firmware/restart.md)
 * (0x4675e). */
void power_on_reset(void) {
  enter_supervisor_mode();
  for (uint32_t* next = (uint32_t*)&preserved_region;
       next < (uint32_t*)&reset_cleared_ram; next++) {
    *next = 0;
  }
  __m68k_trap(12);
}

/* The pllc task's pass: unless a reply is pending, counts down a signal
 * delay or takes a request from pllc_exchange, sets it up and handles
 * it, then signals the waiting task (0x43b98). */
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

/* Clears the temporary-mode hold cause temporary_hold_set sets
 * (0x4659e). */
void temporary_hold_clear(void) {
  pll.hold_causes &= ~HOLD_CAUSE_TEMPORARY_MODE;
}

/* :DIAG:TOFFset's done hook: clears the temporary-mode hold cause and
 * saves the calibration block (0x46370).  It passes
 * calibration_block_save_reported the low bytes of its own two stack
 * arguments, sign-extended, which finish_scpi_cmd does not push for a
 * numeric done hook: what its frame holds there. */
void time_offset_done(void) {
  const int8_t index = (int8_t)CPU_CALLERS_STACK_LONG(0);
  const int8_t source = (int8_t)CPU_CALLERS_STACK_LONG(1);

  temporary_hold_clear();
  calibration_block_save_reported(index, source);
}

/* :PTESt:TYPE's done hook: the test turned off, as :PTESt:MODE's hook
 * then takes it (0x416f4). */
void ptest_type_done(void) {
  scpi_value_DIAGnostic_ROSCillator_PTESt_MODE = 0;
  ptest_mode_done();
}

/* The done hook of :PTESt:MODE, :COUNt, :TTHR and :PTHR: the test reset
 * while its mode is off, else armed from off, or a running one checked
 * against the settings (0x41702). */
void ptest_mode_done(void) {
  if (scpi_value_DIAGnostic_ROSCillator_PTESt_MODE == 0) {
    ptest_reset();
  } else if (ptest.state == PTEST_STATE_OFF) {
    ptest.state = PTEST_STATE_ARMED;
  } else if (ptest.state == PTEST_STATE_RUNNING) {
    ptest_check();
  }
}

/* :DIAG:TOFFset's and :GPS:ADELay's action: the temporary-mode hold
 * cause, with the TI limit overridden, which the Z3805A's does not
 * set (0x46584). */
void temporary_hold_set(int32_t index, int32_t source) {
  pll.hold_causes |= HOLD_CAUSE_TEMPORARY_MODE;
  pll.ti_limit_override = 1;
  pll_scpi_signal_delay = 1;
}

/* :PTESt:DURation?: the hours the test has run, to now while it runs,
 * else to its end, its seconds rounded up from a quarter hour; 0 off or
 * armed (0x4172e). */
uint16_t ptest_duration_hours(void) {
  if (ptest.state == PTEST_STATE_OFF || ptest.state == PTEST_STATE_ARMED) {
    return 0;
  }
  const uint32_t end =
      ptest.state == PTEST_STATE_RUNNING ? get_uptime() : ptest.end_uptime;

  return (uint16_t)((end - ptest.start_uptime + PLLC_PTEST_HOUR_ROUNDING) /
                    (uint32_t)CLOCK_SECONDS_PER_HOUR);
}

/* :PTESt:STATe?'s second value, the passes in a row (0x41776). */
uint8_t ptest_passes(void) { return ptest.passes; }

/* The test's state, for :PTESt:STATe? (0x4177e). */
ptest_state ptest_state_get(void) { return ptest.state; }

/* The test off, its run cleared (0x41786). */
void ptest_reset(void) {
  ptest.state = PTEST_STATE_OFF;
  ptest.passes = 0;
  ptest.start_uptime = 0;
  ptest.end_uptime = 0;
}

/* Logs an armed test's entry while its uncertainty is not valid, its
 * format the image's own text, whose address the entry keeps
 * (0x4180c). */
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
 * (0x41838). */
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

/* pllp_task's: in EFC control mode 2, after 600 passes, the hold cause
 * and the mode cleared (revisions.md, "Holdover cause") (0x4650e). */
void efc_mode_expire(void) {
  if (diag_efc_mode == EFC_MODE_TIMED &&
      ++pll.efc_control_passes > PLLC_EFC_MODE_PASSES) {
    pll.hold_causes &= ~HOLD_CAUSE_EFC_CONTROL;
    pll.efc_control_passes = 0;
    diag_efc_mode = EFC_MODE_NORMAL;
  }
}

/* The time error a holdover begun the given number of EFC history entries
 * ago would have made: each entry since, its EFC average less what the
 * fit kept at that entry gives, times the EFC's gain there by the OCXO
 * calibration, a + b efc^2, over the entry's 2700 s, summed, in
 * *uncertainty ("calc_hold").  64 entries take the one the newest
 * replaced.  0, and 0 in *uncertainty, when that entry's time is not the
 * entries' span back within half an entry, or it has no weight
 * (0x413da).  The Z3815A 4010's keeps the sum's greatest magnitude
 * instead, and prints the learning time of the whole history, where this
 * image's prints the first entry's. */
uint8_t measured_uncertainty_compute(int8_t entries, float* uncertainty) {
  const int32_t newest = aging_curve.newest_index;
  const int32_t start = (newest - entries) & PLL_AGING_INDEX_MASK;
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

      float_add_in_place(uncertainty,
                         float_multiply(float_multiply(residual, gain),
                                        PLL_EFC_HISTORY_ENTRY_SECONDS));
    } while (index != newest);
  }
  if (pll_debug_enabled != 0) {
    const double hold = float_to_double(*uncertainty);
    const double learn = float_to_double(pll.efc_history.times[start]);

    debug_sprintf(line, "learn_time= %f calc_hold= %e valid= %d\r\n", learn,
                  hold, (uint32_t)valid);
    task_dmessage(line);
  }
  return valid;
}

/* The measured time uncertainty over the whole EFC history, and whether
 * it is valid, pr_bad_act's "tunc:meas"; then the fit kept at the newest
 * entry, for the uncertainty's next (0x4160a). */
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
 * entry, or that its uncertainty is not valid (0x4198c).  The Z3815A
 * 4010's logs a running test's invalid entry as a valid one's. */
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
  if (ptest.state != PTEST_STATE_RUNNING) {
    return;
  }
  if (valid != 0 &&
      float_compare(float_absolute_on_stack(uncertainty),
                    scpi_value_DIAGnostic_ROSCillator_PTESt_PTHR[1]) < 0) {
    ptest.passes++;
  } else {
    ptest.passes = 0;
  }
  ptest_check();
  if (valid != 0) {
    ptest_log(uncertainty, valid);
  } else {
    ptest_log_unavailable();
  }
}

/* The latest EFC history entry's average, rounded and clamped to 0 to
 * 1e9, the test's log's "efc avg" (0x4179c). */
int32_t ptest_efc_average(void) {
  const float average = float_add(
      pll.efc_history
          .averages[(pll.efc_history.stop_ptr - 1) & PLL_AGING_INDEX_MASK],
      PLLC_HALF_ENTRY);

  return float_to_integer(pllc_clamp(average, 0.0f, PLLC_NANOSECONDS));
}

/* :DIAG:ROSC:LTIMe:MAX's done hook: initial_loop_time lowered to the new
 * maximum, the loop's time constant held between them; then the
 * temporary-mode hold cause cleared (0x46414).  New in this image. */
void max_loop_time_done(void) {
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

/* The action after :SYSTem:PON's in its table (0x413b0), which no
 * handler requests: as power_on_reset, zeroes the preserved region, then
 * trap #10, whose handler (0x14ce8) shuts down, resets and enters the
 * boot code through its own vector table, where power_on_reset's
 * trap #12 restarts the primary (0x4677e).  New in this image. */
void power_on_reset_boot(void) {
  enter_supervisor_mode();
  for (uint32_t* next = (uint32_t*)&preserved_region;
       next < (uint32_t*)&reset_cleared_ram; next++) {
    *next = 0;
  }
  __m68k_trap(10);
}

/* pll.field_6fb from tfom_code of a TFOM; nothing calls it (0x4679e), the
 * 58503A 3704's field_3e6. */
void pll_tfom_digit_store(uint32_t tfom) { pll.field_6fb = tfom_code(tfom); }

/* Ends a running time uncertainty test: passed after :PTESt:COUNt passes
 * in a row; failed at the second :PTESt:TTHR's hours, or from the first's
 * on with a valid uncertainty, the predicted or the measured one's
 * magnitude by :PTESt:TYPE, not within the first :PTESt:PTHR
 * (0x41656). */
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

/* :DIAG:ROSC:EFControl:MODE's done hook: the EFC control hold cause while
 * a mode is set, the TI limit overridden, the SCPI task signaled a pass
 * late and the EFC set from :EFControl:DATA's value, else the loop's EFC
 * set again; and the passes toward its expiry cleared (0x464c0). */
void efc_mode_done(void) {
  if (diag_efc_mode == EFC_MODE_NORMAL) {
    pll.hold_causes &= ~HOLD_CAUSE_EFC_CONTROL;
    efc_clamp(pll.efc);
  } else {
    pll.hold_causes |= HOLD_CAUSE_EFC_CONTROL;
    pll.ti_limit_override = 1;
    pll_scpi_signal_delay = 1;
    efc_clamp(
        integer_to_float(scpi_value_DIAGnostic_ROSCillator_EFControl_DATA));
  }
  pll.efc_control_passes = 0;
}

/* Requests the self-test numbered by the index, and keeps the request's
 * source, unless one is requested (0x46e8c). */
void selftest_request(int32_t index, int32_t source) {
  if (selftest_requested == 0) {
    selftest_number = index;
    selftest_source = source;
    selftest_requested = 1;
  }
}

static const uint32_t PLLC_MONTH_FROM = 3 << 8;

/* 1 once a GPS month starts, from 00:03:00 of its first valid time away
 * from a leap second, the month kept in gps_month_last, which pllp_task
 * clears (0x48088); else 0.  startup_pll and pll_normal then ask for the
 * leap second's acknowledgment again, through leap_ack_ask (0x44c62 to
 * 0x44c6c, 0x45462 to 0x4546c) (0x467b2). */
uint32_t gps_month_changed(void) {
  uint32_t date;
  time_of_day time;

  if (get_gps_datetime(&date, &time) || leap_second_near()) {
    return 0;
  }
  const uint8_t month = clock_middle(date);

  if (month == gps_month_last || clock_time_bits(time) < PLLC_MONTH_FROM) {
    return 0;
  }
  gps_month_last = month;
  return 1;
}

/* :DIAG:ROSC:LTIMe:INIT's action: keeps the initial loop time from
 * before the setting, for initial_loop_time_done, then holds as
 * temporary_hold_set, its two arguments passed on (0x46390).  New in
 * this image. */
void initial_loop_time_keep(int32_t index, int32_t source) {
  initial_loop_time_last = initial_loop_time;
  temporary_hold_set(index, source);
}

/* :DIAG:ROSC:LTIMe:INIT's done hook: max_loop_time raised to the new
 * initial time; the loop's time constant set to it, unless above both it
 * and the initial time before the setting (initial_loop_time_last), then
 * held to max_loop_time; then the temporary-mode hold cause cleared
 * (0x463a2).  New in this image. */
void initial_loop_time_done(void) {
  if (float_compare(initial_loop_time, max_loop_time) > 0) {
    max_loop_time = initial_loop_time;
  }
  if (float_compare(pll.loop_time_constant, initial_loop_time_last) <= 0 ||
      float_compare(initial_loop_time, pll.loop_time_constant) > 0) {
    pll.loop_time_constant = initial_loop_time;
  } else if (float_compare(pll.loop_time_constant, max_loop_time) > 0) {
    pll.loop_time_constant = max_loop_time;
  }
  temporary_hold_clear();
}

/* 1 when the request goes to the GPS before it is stored; a
 * :DOUGlas:STATe not 0 does not, and, the stored state 0, logs position
 * hold instead (0x43568). */
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
 * *status the condition to report, 0 for none (0x435b4).  The codes are
 * this image's: with the 58503A 3704's, the defaults at 0x2f, the
 * Furuno path's scripts, :DIAGnostic:GPSystem:DCXO's three numbers
 * (0x43684) and GPS_STEP_67's one (0x436ac). */
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
    case GPS_REQUEST_DCXO:
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
    case GPS_STEP_67:
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
    case GPS_SCRIPT_FIX_RESTART:
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

/* Copies a request into pll_scpi_param to pll_scpi_values, as its
 * descriptor's type and a numeric one's is_integer say (0x43a56). */
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

/* Handles the request get_setup_vars took: validates it, sends what the
 * GPS must hear, and, unless a reply from it is pending, stores and
 * saves it (0x43b1e).  Only a request from SCPI or the silent source is
 * sent; the Z3815A 4010's sends any other's as the setting's other
 * sender, and here it is finished unsent, by the reply and the status
 * as they stand (0x43b6e, 0x43b72). */
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

/* The hooks' and actions' shapes, which the descriptors keep as void
 * pointers.  A changed hook is passed its caller's D3 first. */
typedef void pllc_changed_hook(uint32_t caller_d3, int32_t index,
                               int32_t source);

/* A done hook, a numeric's as a setting's here, where the Z3815A 4010's
 * numeric one is passed nothing (0x439bc to 0x439d2). */
typedef void pllc_done_hook(int32_t index, int32_t source);

/* Stores a request's validated values or setting, calling the
 * descriptor's hooks, saves them as its flags say, and logs a
 * :DOUGlas:STATe change (0x438ba). */
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
      ((pllc_done_hook*)pllc_current()->numeric.done)((int8_t)pll_scpi_index,
                                                      (int8_t)pll_scpi_source);
      break;
    }
    case SCPI_PARAM_SETTING:
      param->setting.value[index] = pll_scpi_setting;
      ((pllc_changed_hook*)param->setting.changed)(
          caller_d3, (int8_t)pll_scpi_index, (int8_t)pll_scpi_source);
      ((pllc_done_hook*)param->setting.done)((int8_t)pll_scpi_index,
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
