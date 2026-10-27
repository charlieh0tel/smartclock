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
 * a TFOM of 1 s, the first time constant and tolerance (0x4739c). */
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
 * configuration and loaded flag (0x47430). */
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
 * bound in counts (0x4748c). */
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

/* The power-up recovery stage: turns the outer oven on, waits for a
 * reading, after a failed one for another, and sets the clock from the
 * reference; done unless a hold cause, failed if one (0x461c0). */
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
 * as a mismatch too (0x43f6a) (0x43f44). */
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
 * threshold, and asks for the leap second check (0x464ee). */
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

/* The status screen's mode, from the stage and pll.manual (0x45ba8). */
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
 * second determination (0x45bf0). */
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
 * frequency, 2 phase alignment; else 0 (0x45c50). */
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
 * (0x45c80). */
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
 * (0x47042). */
uint32_t leap_ack_pending(void) {
  return pll.leap_ack_state != PLL_LEAP_ACK_IDLE || pll.leap_ack_requested != 0;
}

/* The time code's FFOM, by the stage: 0 locked, 1 settling, 2 in
 * holdover or its recovery, 3 otherwise; always 1 (0x42a44). */
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
 * resolution; whether it was (0x429c8). */
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
 * was a value (0x429f2). */
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
