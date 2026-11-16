/* From the 58503B 1.01.04's rewritten/pllp.c and the Z3815A 4010's
 * rewritten/pllp.c, the Z3805A 1.01.03's own beside common/'s: of its
 * functions, those the Z3805A 1.01.03's code carries identical or
 * relocated (docs/firmware/z3805a-1.01.03.md, "The link list").  Its
 * comments' addresses are the Z3805A 1.01.03's, but where they name
 * another image.  Each keeps its generated prototype. */

/* Whether the fine sub-state slews the phase. */
static bool pllp_fine_slewing(void) {
  return (int8_t)pll.fine_state >= PLL_FINE_SLEW &&
         (int8_t)pll.fine_state <= PLL_FINE_FINE_SLEW;
}

/* Gives up an awaited acknowledgment: one already in is no longer
 * awaited; one still to come is abandoned, and dropped when it comes. */
static void pllp_abandon_ack(void) {
  if (gps_ack_ready == 0) {
    gps_ack_abandoned = 1;
  } else {
    gps_ack_awaited = GPS_ACK_NONE;
  }
}

/* The :KENneth queries' resolution, a power of ten. */
static const int8_t PLLP_KENNETH_RESOLUTION = -7;

/* Whether the loop is in holdover or recovering from it. */
static bool pllp_holding(void) {
  return pll.stage == PLL_STAGE_HOLDOVER ||
         pll.stage == PLL_STAGE_HOLDOVER_RECOVERY;
}

/* The power-up mode's suffix, the screen's ladder: 0 none, OCXO
 * warm-up, GPS acquisition, coarse, fine frequency, phase alignment, leap
 * second determination (0x46656). */
pll_powerup_suffix pll_powerup_suffix_index(void) {
  if (pll.stage != PLL_STAGE_POWERUP) {
    return PLL_POWERUP_SUFFIX_NONE;
  }
  switch (pll.powerup_state) {
    case PLL_POWERUP_INNER_OVEN:
    case PLL_POWERUP_OUTER_OVEN:
      return PLL_POWERUP_SUFFIX_WARM_UP;
    case PLL_POWERUP_WAITING_FOR_GPS:
      return PLL_POWERUP_SUFFIX_GPS_ACQUISITION;
    case PLL_POWERUP_COARSE:
      return PLL_POWERUP_SUFFIX_COARSE;
    case PLL_POWERUP_FINE:
      return pllp_fine_slewing() ? PLL_POWERUP_SUFFIX_PHASE_ALIGNMENT
                                 : PLL_POWERUP_SUFFIX_FINE_FREQUENCY;
    case PLL_POWERUP_WAITING:
    case PLL_POWERUP_LEAP_SECONDS:
      return PLL_POWERUP_SUFFIX_LEAP_SECOND;
    default:
      return PLL_POWERUP_SUFFIX_NONE;
  }
}

/* Whether the leap second's acknowledgment is asked for or under way
 * (0x47a78). */
uint32_t leap_ack_pending(void) {
  return pll.leap_ack_state != PLL_LEAP_ACK_IDLE || pll.leap_ack_requested != 0;
}

/* Asks for the leap second's acknowledgment, which the stages set
 * pll.leap_ack_requested for in line in the other images (0x47a54). */
void leap_ack_ask(void) { pll.leap_ack_requested = 1; }

/* Whether the leap second's acknowledgment asked for is done: none asked
 * for and the offset set (0x47a5e). */
uint8_t leap_ack_finished(void) {
  return pll.leap_ack_requested == 0 && pll.leap_ack_done != 0;
}

/* Whether the clock's GPS date and time are gps.status's, clearing the
 * mismatch flag; a mismatch counts only while gps.status's time has
 * moved since pllp_task's last pass, and the second in a row sets the
 * clock from GPS, time set 'L' unless pllp_rebooted and the clock
 * agreed.  A clock not yet set, get_gps_datetime's 1, counts as a
 * mismatch too (0x4470e). */
uint32_t clock_check_against_gps(void) {
  uint32_t clock_date;
  time_of_day clock_time;
  bool not_set = get_gps_datetime(&clock_date, &clock_time);
  time_of_day time = gps.status.time;
  uint32_t date = oncore_date_pack(gps.status.date);

  if (!(not_set || date != clock_date ||
        clock_time_bits(time) != clock_time_bits(clock_time)) ||
      clock_time_bits(time) == clock_time_bits(pll.reference_time)) {
    clock_gps_mismatch = 0;
    return 1;
  }
  if (clock_gps_mismatch == 0) {
    clock_gps_mismatch = 1;
    return 0;
  }
  clock_set(time, date);
  if (pllp_rebooted == 0 || date != clock_date ||
      clock_time_bits(time) != clock_time_bits(clock_time)) {
    status_happening(STATUS_TIME_SET);
  }
  pllp_rebooted = 0;
  clock_gps_mismatch = 0;
  return 0;
}

/* For :HOLDover:WAITing? and the front panel: in holdover, the hold
 * cause it waits on, hardware (for a TI error too), GPS or the TI
 * limit; else 0 (0x466e6). */
uint8_t holdover_waiting_reason(void) {
  if (pll.stage != PLL_STAGE_HOLDOVER) {
    return HOLD_CAUSE_NONE;
  }
  if ((pll.hold_causes & HOLD_CAUSE_HARDWARE_FAILURE) != HOLD_CAUSE_NONE ||
      (pll.hold_causes & HOLD_CAUSE_TI_ERROR) != HOLD_CAUSE_NONE) {
    return HOLD_CAUSE_HARDWARE_FAILURE;
  }
  if ((pll.hold_causes & HOLD_CAUSE_GPS) != HOLD_CAUSE_NONE) {
    return HOLD_CAUSE_GPS;
  }
  if ((pll.hold_causes & HOLD_CAUSE_TI_LIMIT_EXCEEDED) != HOLD_CAUSE_NONE) {
    return HOLD_CAUSE_TI_LIMIT_EXCEEDED;
  }
  return HOLD_CAUSE_NONE;
}

/* Restarts the loop from power-up, pll cleared but for the FPGA's
 * configuration and loaded flag, and the counter's registers loaded
 * again (0x47f06). */
void pll_restart(void) {
  uint8_t fpga_loaded = pll.fpga_loaded;
  fpga_configuration_index fpga_configuration = pll.fpga_configuration;

  pll_init();
  status_happening(STATUS_HOLDOVER_THRESHOLD_CLEARED);
  status_happening(STATUS_CONDITION_35);
  pll.fpga_configuration = fpga_configuration;
  pll.fpga_loaded = fpga_loaded;
  counter_load_registers();
  pll.loop_running = 1;
  if (gps_ack_awaited != GPS_ACK_NONE) {
    pllp_abandon_ack();
  }
  pll.stage = PLL_STAGE_POWERUP;
}

/* The recovery mode's suffix while it sets the frequency finely: 1 fine
 * frequency, 2 phase alignment; else 0 (0x466b6). */
pll_recovery_suffix recovery_suffix_index(void) {
  if (pll.stage != PLL_STAGE_HOLDOVER_RECOVERY ||
      pll.recovery_state != PLL_RECOVERY_FINE) {
    return PLL_RECOVERY_SUFFIX_NONE;
  }
  return pllp_fine_slewing() ? PLL_RECOVERY_SUFFIX_PHASE_ALIGNMENT
                             : PLL_RECOVERY_SUFFIX_FINE_FREQUENCY;
}

/* For :KENneth:PRESent?: in holdover or its recovery, the TFOM and its
 * resolution; whether it was (0x430ea). */
bool kenneth_present_get(float* value, int8_t* resolution) {
  if (!pllp_holding()) {
    return false;
  }
  *value = pll.tfom;
  *resolution = PLLP_KENNETH_RESOLUTION;
  return true;
}

/* Sets the EFC's gain, the fractional frequency a count, and what the
 * loop derives from it: its reciprocal and the step limit (0x47f72).  The
 * 58503A 3704's keeps the aging bound in counts too, which this image's
 * efc_gain_values has no place for. */
void efc_gain_set(float gain) {
  static const float ONE = 1.0f;
  static const float STEP_LIMIT = 6.25e-10f;

  efc_gains.gain = gain;
  efc_gains.reciprocal_fine = float_divide(ONE, gain);
  efc_gains.reciprocal_ti = efc_gains.reciprocal_fine;
  efc_gains.reciprocal_tau = efc_gains.reciprocal_fine;
  efc_gains.step_limit = float_divide_reversed(
      float_absolute_on_stack(efc_gains.gain), STEP_LIMIT);
}

/* The time code's FFOM, by the stage: 0 locked, 1 settling, 2 in
 * holdover or its recovery, 3 otherwise, n3 lock pll among them; always
 * 1 (0x43166). */
bool get_ffom(frequency_figure_of_merit* ffom) {
  switch (pll.stage) {
    case PLL_STAGE_NORMAL_PLL:
      *ffom = FFOM_STABILIZED;
      break;
    case PLL_STAGE_STARTUP_PLL:
      *ffom = FFOM_STABILIZING;
      break;
    case PLL_STAGE_HOLDOVER:
    case PLL_STAGE_HOLDOVER_RECOVERY:
      *ffom = FFOM_HOLDOVER;
      break;
    default:
      *ffom = FFOM_UNLOCKED;
      break;
  }
  return true;
}

/* For :KENneth:PREDicted?: from start-up PLL on, the HQ and its
 * resolution; whether in holdover through holding, and whether there
 * was a value (0x43114). */
bool kenneth_predicted_get(float* value, int8_t* resolution, uint8_t* holding) {
  *holding = pllp_holding();
  if (pll.stage != PLL_STAGE_STARTUP_PLL && pll.stage != PLL_STAGE_NORMAL_PLL &&
      !pllp_holding()) {
    return false;
  }
  *value = pll.predicted_uncertainty;
  *resolution = PLLP_KENNETH_RESOLUTION;
  return true;
}

/* The status screen's mode, from the stage and pll.manual (0x4660c). */
pll_screen_mode pll_mode(void) {
  switch (pll.stage) {
    case PLL_STAGE_POWERUP:
      return PLL_MODE_POWERUP;
    case PLL_STAGE_HOLDOVER:
      return pll.manual == 0 ? PLL_MODE_HOLDOVER : PLL_MODE_HOLDOVER_MANUAL;
    case PLL_STAGE_HOLDOVER_RECOVERY:
      return PLL_MODE_RECOVERY;
    case PLL_STAGE_STARTUP_PLL:
    case PLL_STAGE_NORMAL_PLL:
      return PLL_MODE_LOCKED;
    default:
      return PLL_MODE_NONE;
  }
}

/* The power-up recovery stage: turns the outer oven on, waits for a
 * reading, after a failed one for another, and sets the clock from the
 * reference; done unless a hold cause, failed if one (0x46cee). */
pll_stage_result pll_check_reference(bool same_stage) {
  pll_stage_result result = PLL_STAGE_RESULT_RUNNING;

  if (!same_stage || (int8_t)pll.reference_check_state > PLL_REFERENCE_AGAIN) {
    pll.reference_check_state = PLL_REFERENCE_START;
  }
  switch (pll.reference_check_state) {
    case PLL_REFERENCE_FIRST:
      interval_wait_reading();
      if (interval_reading.valid == 0) {
        counter_reset();
        pll.reference_check_state = PLL_REFERENCE_AGAIN;
        result = PLL_STAGE_RESULT_RUNNING;
      } else {
        clock_set_from_reference();
        result = pll_stages_check_causes(PLL_STAGE_RESULT_DONE);
      }
      interval_rearm(INTERVAL_ARM_CALIBRATE);
      break;
    case PLL_REFERENCE_AGAIN:
      if (interval_next_reading(INTERVAL_ARM_CALIBRATE, NULL) == 0) {
        if (internal_pps_dead == 0) {
          pll.hold_causes |= HOLD_CAUSE_TI_ERROR;
        } else {
          pll.hold_causes |= HOLD_CAUSE_HARDWARE_FAILURE;
        }
        result = PLL_STAGE_RESULT_FAILED;
      } else {
        clock_set_from_reference();
        result = pll_stages_check_causes(PLL_STAGE_RESULT_DONE);
      }
      break;
    case PLL_REFERENCE_START:
    default:
      gpt.portgp |= PLL_STAGES_OUTER_OVEN_ENABLE;
      outer_oven_enabled = 1;
      interval_rearm(INTERVAL_ARM_CALIBRATE);
      pll.reference_check_state = PLL_REFERENCE_FIRST;
      break;
  }
  return result;
}

/* Sets the GPS lock and holdover LEDs' flags and the status they imply
 * for the stage just entered; locking logs it, to the external or the
 * GPS reference by pll.field_6f9, and the locked stages clear the
 * holdover threshold and ask for the leap second check; leaving
 * holdover clears its three conditions (0x470a8).  The unnamed and idle
 * stages change neither, by the tables at 0x470d2 and 0x47152. */
void pll_stage_status(void) {
  switch (pll.stage) {
    case PLL_STAGE_POWERUP:
    case PLL_STAGE_POWERUP_RECOVERY:
    case PLL_STAGE_HOLDOVER:
    case PLL_STAGE_HOLDOVER_RECOVERY:
    case PLL_STAGE_DIAG:
    case PLL_STAGE_FATAL_ERROR:
      if (pll.led_gps_lock != 0) {
        pll.led_gps_lock = 0;
        status_happening(STATUS_LOCKED_CLEARED);
      }
      break;
    case PLL_STAGE_STARTUP_PLL:
    case PLL_STAGE_NORMAL_PLL:
      if (pll.led_gps_lock == 0) {
        pll.led_gps_lock = 1;
        log_event(pll.field_6f9 != 0 ? LOG_EXT_LOCK_STARTED
                                     : LOG_GPS_LOCK_STARTED);
        status_happening(STATUS_LOCKED_SET);
      }
      holdover_duration_exceeded = 0;
      status_happening(STATUS_HOLDOVER_THRESHOLD_CLEARED);
      pll.leap_check_requested = 1;
      break;
    default:
      break;
  }
  switch (pll.stage) {
    case PLL_STAGE_POWERUP:
    case PLL_STAGE_POWERUP_RECOVERY:
    case PLL_STAGE_STARTUP_PLL:
    case PLL_STAGE_NORMAL_PLL:
    case PLL_STAGE_DIAG:
    case PLL_STAGE_FATAL_ERROR:
      if (pll.led_holdover != 0) {
        pll.led_holdover = 0;
        status_happening(STATUS_HOLDING_CLEARED);
        status_happening(STATUS_WAITING_CLEARED);
        status_happening(STATUS_RECOVERING_CLEARED);
      }
      break;
    case PLL_STAGE_HOLDOVER:
      if (pll.led_holdover == 0) {
        pll.led_holdover = 1;
      }
      status_happening(pll.manual == 0 ? STATUS_WAITING_SET
                                       : STATUS_HOLDING_SET);
      break;
    case PLL_STAGE_HOLDOVER_RECOVERY:
      if (pll.led_holdover == 0) {
        pll.led_holdover = 1;
      }
      status_happening(STATUS_RECOVERING_SET);
      break;
    default:
      break;
  }
}

/* Clears pll and sets its defaults: the EFC the lifetime record saved,
 * set, a TFOM of 1 s, initial_loop_time for the time constant, the
 * tolerance, the FPGA's last configuration, field_6fb and the loop's
 * block of 10 seconds (0x47e54). */
void pll_init(void) {
  static const float FIRST_TFOM = 1.0f;
  static const float FIRST_TOLERANCE = 1e-07f;
  static const uint8_t FIELD_6FB_FIRST = 9;
  static const uint8_t BLOCK_SECONDS = 10;

  memset(&pll, 0, sizeof pll);
  pll.stage = PLL_STAGE_INVALID;
  pll.gps_init_state = PLL_GPS_INIT_START;
  efc_set(efc_clamp(lifetime.efc));
  pll.aging_state = PLL_AGING_START;
  pll.efc = efc_clamp(lifetime.efc);
  pll.loop_time_constant = initial_loop_time;
  holdover_recovery_auto = 1;
  pll.hold_causes = HOLD_CAUSE_NONE;
  pll.manual = 0;
  pll.ti_limit_override = 0;
  pll.fpga_configuration = fpga_configurations.last;
  leap_recheck_requested = 0;
  pll.tfom = FIRST_TFOM;
  pll.field_6fb = FIELD_6FB_FIRST;
  pll.block_seconds = BLOCK_SECONDS;
  pll.sync_requested = 0;
  gps_reference_good = 0;
  gps_reference_questionable = 0;
  time_zone_change_pending = 0;
  holdover_duration_exceeded = 0;
  pll.frequency_tolerance = FIRST_TOLERANCE;
  pll.field_6f9 = 0;
}

/* An acknowledgment has come or timed out: an abandoned one is dropped,
 * a GPS command's is no longer awaited. */
static void pllp_ack_done(void) {
  if (gps_ack_abandoned != 0) {
    gps_ack_awaited = GPS_ACK_NONE;
    gps_ack_abandoned = 0;
  } else if (gps_ack_awaited == GPS_ACK_COMMAND) {
    gps_ack_awaited = GPS_ACK_NONE;
    task_dmessage("GPS command completed\r\n");
  }
}

/* Takes a reply from plla_exchange, if one waits: a sequence's is the
 * acknowledgment, a setting's completes it. */
static void pllp_take_reply(void) {
  gps_request_entry reply;

  if ((uint8_t)psos_request_x(plla_exchange, &reply, 1, 0) != 0) {
    return;
  }
  if (reply.result != PLL_GPS_CONTROL_ACK_OK) {
    plla_failed_count++;
  }
  if (reply.sender == GPS_SENDER_SEQUENCE) {
    if (gps_ack_awaited == GPS_ACK_NONE) {
      plla_unexpected_count++;
    }
    gps_ack_code = (int8_t)reply.result;
    gps_ack_ready = 1;
    gps_ack_wait_count = 0;
    pllp_ack_done();
  } else if (reply.sender == GPS_SENDER_SETTING) {
    pll_scpi_complete((int32_t*)&plla_unexpected_count,
                      reply.result == PLL_GPS_CONTROL_ACK_OK);
  } else {
    plla_unknown_count++;
  }
  plla_received_count++;
}

/* Counts a second of an awaited acknowledgment: one in is no longer
 * awaited after 10, and one not in times out after 1200. */
static void pllp_ack_wait(void) {
  static const uint16_t READY_SECONDS = 10;
  static const uint16_t TIMEOUT_SECONDS = 1200;

  if (gps_ack_awaited == GPS_ACK_NONE) {
    return;
  }
  gps_ack_wait_count++;
  if (gps_ack_wait_count > READY_SECONDS && gps_ack_ready != 0) {
    gps_ack_awaited = GPS_ACK_NONE;
  } else if (gps_ack_wait_count > TIMEOUT_SECONDS) {
    gps_ack_code = PLL_GPS_CONTROL_ACK_TIMED_OUT;
    gps_ack_ready = 1;
    gps_ack_wait_count = 0;
    task_dmessage("GPS acknowledgment timed out\r\n");
    pllp_ack_done();
  }
}

/* Steps the stage a second, reporting one just entered.  A power-up
 * that fails restarts the loop and reports the stage again; the normal
 * loop, a block's mean past 500 ns, goes back to the start-up loop at
 * initial_loop_time. */
static void pllp_step_stage(bool same_stage) {
  /* The ticks an idle or failed loop pauses for. */
  static const uint32_t IDLE_TICKS = 0x400;

  switch (pll.stage) {
    case PLL_STAGE_POWERUP: {
      if (!same_stage) {
        pll_lock = 0;
        pll_stage_status();
        task_dmessage("PLL powerup\r\n");
      }
      pll_stage_result result = powerup(same_stage);

      if (result == PLL_STAGE_RESULT_DONE) {
        pll.stage = PLL_STAGE_STARTUP_PLL;
      } else if (result == PLL_STAGE_RESULT_FAILED) {
        pll_restart();
        pll_stage_status();
      }
      return;
    }
    case PLL_STAGE_POWERUP_RECOVERY: {
      if (!same_stage) {
        pll_stage_status();
        task_dmessage("Went into powerup recovery\r\n");
        leap_recheck_requested = 1;
      }
      pll_stage_result result = pll_check_reference(same_stage);

      if (result == PLL_STAGE_RESULT_DONE) {
        pll.stage = PLL_STAGE_HOLDOVER_RECOVERY;
      } else if (result == PLL_STAGE_RESULT_FAILED) {
        enter_holdover();
      }
      return;
    }
    case PLL_STAGE_HOLDOVER:
      if (!same_stage) {
        pll_stage_status();
        task_dmessage("Went into holdover\r\n");
      }
      if (holdover(same_stage) == PLL_STAGE_RESULT_DONE) {
        pll.stage = PLL_STAGE_HOLDOVER_RECOVERY;
      }
      return;
    case PLL_STAGE_HOLDOVER_RECOVERY: {
      if (!same_stage) {
        pll_lock = 1;
        pll_stage_status();
        task_dmessage("Went into holdover recovery\r\n");
      }
      pll_stage_result result = holdover_recovery(same_stage);

      if (result == PLL_STAGE_RESULT_DONE) {
        pll.stage = PLL_STAGE_STARTUP_PLL;
      } else if (result == PLL_STAGE_RESULT_FAILED) {
        enter_holdover();
      }
      return;
    }
    case PLL_STAGE_STARTUP_PLL: {
      if (!same_stage) {
        pll_lock = 1;
        pll_stage_status();
        task_dmessage("Went into PLL startup\r\n");
      }
      pll_stage_result result = startup_pll(same_stage);

      if (result == PLL_STAGE_RESULT_DONE) {
        pll.stage = PLL_STAGE_NORMAL_PLL;
      } else if (result == PLL_STAGE_RESULT_FAILED) {
        enter_holdover();
      }
      return;
    }
    case PLL_STAGE_NORMAL_PLL:
      if (!same_stage) {
        pll_stage_status();
        task_dmessage("Went into PLL normal\r\n");
      }
      if (pll_normal(same_stage) == PLL_STAGE_RESULT_FAILED) {
        enter_holdover();
      } else if (interval_mean_flag != 0 &&
                 float_compare(float_absolute_on_stack(interval_mean),
                               PLL_LOST_TI) > 0) {
        pll.loop_time_constant = initial_loop_time;
        pll.stage = PLL_STAGE_STARTUP_PLL;
      }
      return;
    case PLL_STAGE_DIAG:
      if (!same_stage) {
        pll_stage_status();
        pll.loop_running = 0;
        task_dmessage("Went into Diag\r\n");
      }
      if (gps_diag_sequence(same_stage)) {
        pll_restart();
      }
      return;
    case PLL_STAGE_IDLE:
      if (!same_stage) {
        pll_stage_status();
        task_dmessage("Went into Idle\r\n");
      }
      break;
    case PLL_STAGE_FATAL_ERROR:
      if (!same_stage) {
        pll_stage_status();
        task_dmessage("Went into fatal error\r\n");
      }
      break;
    default:
      pll.stage = PLL_STAGE_POWERUP;
      break;
  }
  psos_pause(IDLE_TICKS);
}

/* The pllp task: sets the loop up from power-up, logging what the
 * preserved region says the start was, then each second steps its
 * stage, counts down the EFC control mode, keeps the status summary,
 * takes the GPS's acknowledgments, steps the GPS sequences and the
 * settings server and keeps the GPS's and the loop's state and the ADC
 * monitors in the preserved region (0x47fc4).  A loop restored with a
 * stage starts from power-up too, logged a reboot: unlike the Z3815A
 * 4010's it does not start in power-up recovery, sets no pllp_rebooted
 * and no TI setpoint, and it starts the timecode's ES message
 * (es_enabled). */
void pllp_task(void) {
  bool system_preset = false;
  pll_stage last_stage;

  watchdog_pll++;
  interval_tick_wait = 0;
  pll_restart_requested = 0;
  gps_ack_wait_count = 0;
  gps_ack_code = 1;
  pll_scpi_reply_pending = 0;
  pll_scpi_signal_delay = 0;
  gps_ack_ready = 0;
  gps_ack_awaited = GPS_ACK_NONE;
  last_stage = PLL_STAGE_INVALID;
  gps_ack_abandoned = 0;
  douglas_start_requested = 0;
  douglas_stop_requested = 0;
  douglas_finish_requested = 0;
  douglas_finish_nv_argument = REQUEST_SOURCE_INTERNAL;
  selftest_requested = 0;
  clock_change_applied = 0;
  clock_change_armed = 0;
  pll_debug_enabled = 0;
  restart_reference_seconds = pll.reference_seconds;
  restart_reference_uptime = pll.reference_uptime;
  interval_mean_flag = 0;
  clock_gps_mismatch = 0;
  pllp_rebooted = 0;
  gps_month_last = 0;
  ptest_reset();
  efc_gain_set(scpi_value_DIAGnostic_ROSCillator_EFControl_ASLOPe);
  if (pll.stage == PLL_STAGE_INVALID) {
    pll.stage = PLL_STAGE_POWERUP;
    if (preserved_region.restore == PRESERVED_REGION_REBOOTED) {
      log_event(LOG_REBOOT);
    } else if (preserved_region.restore == PRESERVED_REGION_RESTORE) {
      status_happening(STATUS_POWER_ON);
      log_event(LOG_POWER_ON);
    } else {
      log_event(LOG_SYSTEM_PRESET);
      system_preset = true;
    }
    if (nv_settings.douglas_state_powerup != 0) {
      nv_settings.douglas_state = 0;
    }
  } else {
    pll.stage = PLL_STAGE_POWERUP;
    log_event(LOG_REBOOT);
  }
  counter_init();
  pllp_start_nothing_a();
  pllp_start_nothing_b();
  gpsx_sent_count = 0;
  plla_received_count = 0;
  plla_unexpected_count = 0;
  plla_unknown_count = 0;
  plla_failed_count = 0;
  watchdog_pll++;
  if (pll.stage == PLL_STAGE_POWERUP) {
    counter_reset();
    selftest_print_failure(system_preset);
    counter_load_registers();
  }
  pll_stage_status();
  pll.loop_running = 1;
  es_enabled = 1;
  for (;;) {
    if (pll_restart_requested != 0) {
      pll_restart();
      last_stage = PLL_STAGE_INVALID;
      pll_restart_requested = 0;
    }
    if (selftest_requested != 0) {
      pll.stage = PLL_STAGE_DIAG;
    }
    bool same_stage = pll.stage == last_stage;

    if (!same_stage) {
      last_stage = pll.stage;
      if (gps_ack_awaited == GPS_ACK_SELFTEST) {
        pllp_abandon_ack();
      }
      pll.sync_requested = 0;
      clock_gps_mismatch = 0;
    }
    pllp_step_stage(same_stage);
    pll.reference_seconds = fpga_read_seconds();
    pll.reference_uptime = get_uptime();
    watchdog_pll++;
    efc_mode_expire();
    status_summary_update();
    if (clock_change_applied != 0) {
      nv_save(REQUEST_SOURCE_INTERNAL);
      clock_change_applied = 0;
      clock_change_armed = 0;
    }
    pllp_take_reply();
    pllp_ack_wait();
    gps_init_sequence();
    if ((int8_t)pll.gps_init_state > PLL_GPS_INIT_HOLD_OFF_ACK) {
      gps_leap_ack();
    }
    gps_leap_sequence();
    if (gps_command.code != GPS_CONSOLE_NONE &&
        gps_ack_awaited == GPS_ACK_NONE) {
      gps_command_send();
    }
    pll_scpi_receive();
    pll.reference_time = gps.status.time;
    memcpy(preserved_region.values, retained_block,
           sizeof preserved_region.values);
    preserved_region.gps = gps;
    preserved_region.pll = pll;
    memcpy(preserved_region.adc_monitors, adc_monitors,
           sizeof preserved_region.adc_monitors);
    preserved_region.restore = PRESERVED_REGION_RESTORE;
    preserved_region.checksum =
        record_checksum(&preserved_region, sizeof preserved_region);
  }
}
