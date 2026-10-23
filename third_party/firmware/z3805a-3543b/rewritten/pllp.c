/* The pllp task, the disciplining loop's: its state, pll, set up and
 * restarted, the stage stepped once a second (pll_stages.c) with the
 * GPS sequences (pll_gps_control.c), the GPS's acknowledgments taken,
 * and the state the status screen, the time code and the SCPI queries
 * read (docs/firmware/loop.md; docs/firmware/structures.md, "The loop's
 * state").  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"; docs/firmware/modules.md,
 * `applications/pll/pllp`).  The exported C includes this file after
 * its own, so the types and prototypes it declares are in scope; each
 * function here keeps its generated prototype. */

/* Clears pll and sets its defaults: the EFC the lifetime record saved,
 * a TFOM of 1 s, the first time constant and tolerance (0x473d2). */
void pll_init(void) {
  static const float FIRST_TFOM = 1.0f;
  static const float FIRST_TOLERANCE = 1e-07f;

  memset(&pll, 0, sizeof pll);
  pll.stage = PLL_STAGE_INVALID;
  pll.gps_init_state = PLL_GPS_INIT_START;
  pll.aging_state = PLL_AGING_START;
  pll.efc = efc_clamp(lifetime.efc);
  pll.loop_time_constant = PLL_FIRST_TIME_CONSTANT;
  holdover_recovery_auto = 1;
  pll.hold_causes = HOLD_CAUSE_NONE;
  pll.manual = 0;
  pll.ti_limit_override = 0;
  pll.fpga_configuration = FPGA_CONFIGURATION_LAST;
  leap_recheck_requested = 0;
  pll.tfom = FIRST_TFOM;
  pll.sync_requested = 0;
  gps_reference_good = 0;
  time_zone_change_pending = 0;
  holdover_duration_exceeded = 0;
  pwma_force_level(0);
  pll.frequency_tolerance = FIRST_TOLERANCE;
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

/* Restarts the loop from power-up, pll cleared but for the FPGA's
 * configuration and loaded flag (0x47466). */
void pll_restart(void) {
  uint8_t fpga_loaded = pll.fpga_loaded;
  fpga_configuration_index fpga_configuration = pll.fpga_configuration;

  pll_init();
  status_happening(STATUS_HOLDOVER_THRESHOLD_CLEARED);
  pll.fpga_configuration = fpga_configuration;
  pll.fpga_loaded = fpga_loaded;
  pll.loop_running = 1;
  if (gps_ack_awaited != GPS_ACK_NONE) {
    pllp_abandon_ack();
  }
  pll.stage = PLL_STAGE_POWERUP;
}

/* Sets the EFC's gain, the fractional frequency a count, and what the
 * loop derives from it: its reciprocal, the step limit and the aging
 * bound in counts (0x474c2). */
void efc_gain_set(float gain) {
  static const float ONE = 1.0f;
  static const float STEP_LIMIT = 6.25e-10f;
  /* 5e-9 a day, a second. */
  static const float AGING_PER_SECOND = 5.787037e-14f;

  efc_gains.gain = gain;
  efc_gains.reciprocal_fine = float_divide(ONE, gain);
  efc_gains.reciprocal_ti = efc_gains.reciprocal_fine;
  efc_gains.reciprocal_tau = efc_gains.reciprocal_fine;
  efc_gains.step_limit = float_divide_reversed(
      float_absolute_on_stack(efc_gains.gain), STEP_LIMIT);
  efc_gains.aging_per_day = float_divide(AGING_PER_SECOND, efc_gains.gain);
}

/* Does nothing; pllp_task calls it after counter_init (0x45c8e). */
void pllp_start_nothing_a(void) {}

/* Does nothing; pllp_task calls it after pllp_start_nothing_a
 * (0x45c90). */
void pllp_start_nothing_b(void) {}

/* Sets the clock from the reference: the GPS time plus the seconds the
 * FPGA's count and the uptime say have passed since pllp_task started,
 * 0 to 59 (0x46290). */
void clock_set_from_reference(void) {
  static const uint8_t FPGA_SECONDS_MASK = 0x0f;
  static const uint8_t FPGA_SECONDS_WRAP = 0x10;
  static const int32_t LAST_SECOND = 60 - 1;
  uint8_t seconds = fpga_read_seconds() & FPGA_SECONDS_MASK;
  packed_datetime change;
  packed_datetime now;

  restart_reference_seconds &= FPGA_SECONDS_MASK;
  if (seconds <= restart_reference_seconds) {
    seconds += FPGA_SECONDS_WRAP;
  }
  uint32_t uptime = get_uptime();

  if (uptime < restart_reference_uptime) {
    restart_reference_uptime = 0;
  }
  int32_t elapsed = (int32_t)(seconds - restart_reference_seconds) +
                    (int32_t)(restart_reference_uptime - uptime);

  if (elapsed < 0) {
    elapsed = 0;
  } else if (elapsed > LAST_SECOND) {
    elapsed = LAST_SECOND;
  }
  change.time = clock_time_of(elapsed);
  change.date = 0;
  get_gps_datetime(&now.date, &now.time);
  datetime_add(&change, &now);
  set_timedate_unflagged(now.date, clock_time_bits(now.time));
}

/* The power-up recovery stage: turns the outer oven on, waits for a
 * reading, after a failed one for another, and sets the clock from the
 * reference; done unless a hold cause, failed if one (0x46188). */
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

/* Whether the clock's GPS date and time are gps.status's, clearing the
 * mismatch flag; on a mismatch the second in a row sets the clock from
 * GPS, time set 'L'.  A clock not yet set, get_gps_datetime's 1, counts
 * as a mismatch too (0x43f3e) (0x43f18). */
uint32_t clock_check_against_gps(void) {
  uint32_t date;
  time_of_day time;

  if (get_gps_datetime(&date, &time) ||
      oncore_date_pack(gps.status.date) != date ||
      clock_time_bits(time) != clock_time_bits(gps.status.time)) {
    bool second_mismatch = clock_gps_mismatch != 0;

    if (second_mismatch) {
      clock_set(gps.status.time, oncore_date_pack(gps.status.date));
      status_happening(STATUS_TIME_SET);
    }
    clock_gps_mismatch = !second_mismatch;
    return 0;
  }
  clock_gps_mismatch = 0;
  return 1;
}

/* Sets the GPS lock and holdover LEDs' flags and the status they imply
 * for the stage just entered; locking logs it and clears the holdover
 * threshold, and asks for the leap second check (0x46524). */
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
      interval_mean_flag = 0;
      break;
    case PLL_STAGE_STARTUP_PLL:
    case PLL_STAGE_NORMAL_PLL:
      if (pll.led_gps_lock == 0) {
        pll.led_gps_lock = 1;
        log_event(LOG_GPS_LOCK_STARTED);
        status_happening(STATUS_LOCKED_SET);
      }
      holdover_duration_exceeded = 0;
      pwma_force_level(holdover_duration_exceeded);
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

/* The status screen's mode, from the stage and pll.manual (0x45b70). */
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

/* Whether the fine sub-state slews the phase. */
static bool pllp_fine_slewing(void) {
  return (int8_t)pll.fine_state >= PLL_FINE_SLEW &&
         (int8_t)pll.fine_state <= PLL_FINE_FINE_SLEW;
}

/* The power-up mode's suffix, the screen's ladder: 0 none, OCXO
 * warm-up, GPS acquisition, coarse, fine frequency, phase alignment, leap
 * second determination (0x45bb8). */
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

/* The recovery mode's suffix while it sets the frequency finely: 1 fine
 * frequency, 2 phase alignment; else 0 (0x45c18). */
pll_recovery_suffix recovery_suffix_index(void) {
  if (pll.stage != PLL_STAGE_HOLDOVER_RECOVERY ||
      pll.recovery_state != PLL_RECOVERY_FINE) {
    return PLL_RECOVERY_SUFFIX_NONE;
  }
  return pllp_fine_slewing() ? PLL_RECOVERY_SUFFIX_PHASE_ALIGNMENT
                             : PLL_RECOVERY_SUFFIX_FINE_FREQUENCY;
}

/* For :HOLDover:WAITing?: in holdover, the hold cause it waits on,
 * hardware (for a TI error too), GPS or the TI limit; else 0
 * (0x45c48). */
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

/* Whether the leap second's acknowledgment is asked for or under way
 * (0x47078). */
uint32_t leap_ack_pending(void) {
  return pll.leap_ack_state != PLL_LEAP_ACK_IDLE || pll.leap_ack_requested != 0;
}

/* The time code's FFOM, by the stage: 0 locked, 1 settling, 2 in
 * holdover or its recovery, 3 otherwise; always 1 (0x42a18). */
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

/* The :KENneth queries' resolution, a power of ten. */
static const int8_t PLLP_KENNETH_RESOLUTION = -7;

/* Whether the loop is in holdover or recovering from it. */
static bool pllp_holding(void) {
  return pll.stage == PLL_STAGE_HOLDOVER ||
         pll.stage == PLL_STAGE_HOLDOVER_RECOVERY;
}

/* For :KENneth:PRESent?: in holdover or its recovery, the TFOM and its
 * resolution; whether it was (0x4299c). */
bool kenneth_present_get(float* value, int8_t* resolution) {
  if (!pllp_holding()) {
    return false;
  }
  *value = pll.tfom;
  *resolution = PLLP_KENNETH_RESOLUTION;
  return true;
}

/* For :KENneth:PREDicted?: from start-up PLL on, the HQ and its
 * resolution; whether in holdover through holding, and whether there
 * was a value (0x429c6). */
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
    gps_ack_code = reply.result;
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

/* Reports the GPS reference's coming and going as satellites
 * sufficient. */
static void pllp_reference_status(void) {
  if (gps.reference_qualified == 0 || gps_pps_dead != 0) {
    if (gps_reference_good != 0) {
      gps_reference_good = 0;
      status_happening(STATUS_SATELLITES_SUFFICIENT_CLEARED);
    }
  } else if (gps_reference_good == 0) {
    gps_reference_good = 1;
    status_happening(STATUS_SATELLITES_SUFFICIENT_SET);
  }
}

/* Steps the stage a second, reporting one just entered. */
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

/* The pllp task: sets the loop up, from power-up or after a reboot as
 * power-up recovery, then each second steps its stage, takes the GPS's
 * acknowledgments, steps the GPS sequences and the settings server and
 * keeps the GPS's and the loop's state in the preserved region
 * (0x47528). */
void pllp_task(void) {
  /* The EFC's gain, fractional frequency a count. */
  static const float EFC_GAIN = 6.25e-13f;
  bool system_preset = false;
  pll_stage last_stage;

  watchdog_pll++;
  interval_tick_wait = 0;
  pll_unread_flag_1 = 0;
  pll_unread_flag_2 = 0;
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
  efc_gain_set(EFC_GAIN);
  if (pll.stage == PLL_STAGE_INVALID) {
    pll.stage = PLL_STAGE_POWERUP;
    system_preset = preserved_region.restore != PRESERVED_REGION_RESTORE;
    if (system_preset) {
      log_event(LOG_SYSTEM_PRESET);
    } else {
      status_happening(STATUS_POWER_ON);
      log_event(LOG_POWER_ON);
    }
    if (nv_settings.douglas_state_powerup != 0) {
      nv_settings.douglas_state = 0;
    }
  } else {
    pll.previous_stage = pll.stage;
    if (pll.stage != PLL_STAGE_POWERUP) {
      pll.stage = PLL_STAGE_POWERUP_RECOVERY;
    }
    log_event(LOG_REBOOT);
  }
  ti_setpoint = 0.0f;
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
  }
  pll_stage_status();
  es_enabled = 1;
  pll.loop_running = 1;
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
    pllp_reference_status();
    pllp_step_stage(same_stage);
    pll.reference_seconds = fpga_read_seconds();
    pll.reference_uptime = get_uptime();
    watchdog_pll++;
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
    memcpy(preserved_region.values, &retained_block,
           sizeof preserved_region.values);
    preserved_region.gps = gps;
    preserved_region.pll = pll;
    preserved_region.restore = PRESERVED_REGION_RESTORE;
    preserved_region.checksum =
        record_checksum(&preserved_region, sizeof preserved_region);
    leds_update();
  }
}
