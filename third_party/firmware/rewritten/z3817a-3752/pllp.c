/* From the 58503A 3704's rewritten/pllp.c and the Z3815A 4010's
 * rewritten/pllp.c, the Z3817A 3752's own beside common/'s: of its
 * functions, those the Z3817A's code carries identical or relocated
 * (docs/firmware/z3817a-3752.md, "The function check"); then the pllp task,
 * its set-up and restart and the stages' status as this image changes them,
 * each written from the image's code.  Its comments' addresses are the
 * Z3817A's, but where they name another image.  Each keeps its generated
 * prototype. */

/* The status screen's mode, from the stage and pll.manual (0x4b0b0). */
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

/* The time code's FFOM, by the stage: 0 locked, 1 settling, 2 in
 * holdover or its recovery, 3 otherwise; always 1 (0x47b02). */
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

/* For :HOLDover:WAITing? and the status snapshot (0x50cdc): in holdover, the
 * hold cause it waits on, hardware (for a TI error too), GPS or the TI limit;
 * else 0 (0x4b188). */
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

/* The power-up mode's suffix, the screen's ladder: 0 none, OCXO
 * warm-up, GPS acquisition, coarse, fine frequency, phase alignment, leap
 * second determination (0x4b0f8). */
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
 * (0x4c60a). */
uint32_t leap_ack_pending(void) {
  return pll.leap_ack_state != PLL_LEAP_ACK_IDLE || pll.leap_ack_requested != 0;
}

/* The :KENneth queries' resolution, a power of ten. */
static const int8_t PLLP_KENNETH_RESOLUTION = -7;

/* Whether the loop is in holdover or recovering from it. */
static bool pllp_holding(void) {
  return pll.stage == PLL_STAGE_HOLDOVER ||
         pll.stage == PLL_STAGE_HOLDOVER_RECOVERY;
}

/* For :KENneth:PRESent?: in holdover or its recovery, the TFOM and its
 * resolution; whether it was (0x47a86). */
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
 * was a value (0x47ab0). */
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

/* Whether the clock's GPS date and time are gps.status's, clearing the
 * mismatch flag; a mismatch counts only while gps.status's time has
 * moved since pllp_task's last pass, and the second in a row sets the
 * clock from GPS, time set 'L' unless pllp_rebooted and the clock
 * agreed.  A clock not yet set, get_gps_datetime's 1, counts as a
 * mismatch too (0x49082). */
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

/* The recovery mode's suffix while it sets the frequency finely: 1 fine
 * frequency, 2 phase alignment; else 0 (0x4b158). */
pll_recovery_suffix recovery_suffix_index(void) {
  if (pll.stage != PLL_STAGE_HOLDOVER_RECOVERY ||
      pll.recovery_state != PLL_RECOVERY_FINE) {
    return PLL_RECOVERY_SUFFIX_NONE;
  }
  return pllp_fine_slewing() ? PLL_RECOVERY_SUFFIX_PHASE_ALIGNMENT
                             : PLL_RECOVERY_SUFFIX_FINE_FREQUENCY;
}

/* PE7, the selector between TIMEREF1, high, and TIMEREF2, low
 * (docs/firmware/z3817a-3752.md, "TIMEREF1 or TIMEREF2"). */
static const uint8_t PLLP_TIMEREF_LINE = 0x80;

/* Switches to the other reference: PE7 set from pll.timeref_select
 * before it is inverted, so that 0, TIMEREF1, takes PE7 low, the GPS
 * task restarted for the new reference's stream, and "Switched gps ref"
 * logged (0x4d430). */
void timeref_switch(void) {
  if (pll.timeref_select != 0) {
    sim.porte0 |= PLLP_TIMEREF_LINE;
  } else {
    sim.porte0 &= (uint8_t)~PLLP_TIMEREF_LINE;
  }
  pll.timeref_select = pll.timeref_select == 0;
  gps_restart();
  task_dmessage("Switched gps ref\r\n");
}

/* Makes PE7 agree with pll.timeref_select, high for TIMEREF1, and
 * restarts the GPS task when it changed it (0x4d46e). */
void timeref_select_apply(void) {
  bool changed = false;

  if (pll.timeref_select == 0) {
    if ((sim.porte0 & PLLP_TIMEREF_LINE) == 0) {
      sim.porte0 |= PLLP_TIMEREF_LINE;
      changed = true;
    }
  } else if ((sim.porte0 & PLLP_TIMEREF_LINE) != 0) {
    sim.porte0 &= (uint8_t)~PLLP_TIMEREF_LINE;
    changed = true;
  }
  if (changed) {
    gps_restart();
  }
}

/* :DIAGnostic:REFerence:CPORt's action on the loop task: in holdover, or
 * in power-up with no good reference, asks unit_and_reference_check to
 * switch the reference at its next pass; else -221, Settings conflict,
 * for the source (0x4d6ca). */
void timeref_switch_request(int32_t index, int32_t source) {
  if (pll.stage == PLL_STAGE_HOLDOVER ||
      (pll.stage == PLL_STAGE_POWERUP && gps_reference_good == 0)) {
    pll.timeref_switch_requested = 1;
  } else {
    status_report_from(STATUS_SETTINGS_CONFLICT, source);
  }
}

/* A count of seconds unit_and_reference_check steps a pass: a pass is a
 * pulse, 2 s in PP2S (docs/firmware/z3817a-3752.md, "The pulse"). */
static void pllp_count_pass(uint16_t* seconds) {
  if (pll.pulse_mode == REFERENCE_PULSE_1PPS) {
    *seconds += 1;
  } else {
    *seconds += 2;
  }
}

/* Once a pass, after the stage (0x4d4b0; docs/firmware/z3817a-3752.md,
 * "Failing the unit" and "The pulse").  Until it has failed the unit: with
 * oscillator byte 0, the oscillator's not-ready count and osc code, 1 for
 * the link down, 2 for never ready in 1800 s, 3 for not ready 300 s
 * after being so; and the count of the hardware condition's bits 0x70cf.
 * An osc code or 300 s of the bits fail the unit once: logged, the EFC
 * at full scale in user mode, its hold cause, and the alarm byte.  Then
 * the hunt's count; the GPS's cable delay, whose change holds the loop a
 * pass (HOLD_CAUSE_TEMPORARY_MODE); TIMEREF switched when asked, or after
 * 300 s, in power-up with no good reference or in a holdover past 300 s
 * for the GPS, its command monitor or the TI limit; and with no good
 * reference past 60 s, the pulse's mode switched to the kind still
 * alive. */
void unit_and_reference_check(void) {
  static const uint16_t NOT_READY_FAIL = 1800;
  static const uint16_t READY_LOST_FAIL = 300;
  static const uint16_t HARDWARE_FAIL = 300;
  static const uint16_t HARDWARE_BITS = 0x70cf;
  /* the EFC failing sets, 0xffff0, its 20 bits' top */
  static const int32_t FAILED_EFC = 0xffff0;
  static const uint16_t HUNT_MOST = 43200;
  static const uint16_t HUNT_SWITCH = 300;
  static const uint32_t HOLDOVER_SWITCH = 300;
  static const uint16_t MODE_SWITCH = 60;
  /* a cable delay at or below it is none heard (gps_cable_delay_heard) */
  static const int32_t NO_CABLE_DELAY = -1000000;
  static const pll_hold_cause SWITCH_HOLDS = HOLD_CAUSE_GPS_COMMAND_MONITOR |
                                             HOLD_CAUSE_TI_LIMIT_EXCEEDED |
                                             HOLD_CAUSE_GPS;
  /* the readings to a block, 10 s, in each mode */
  static const uint8_t BLOCK_1PPS = 10;
  static const uint8_t BLOCK_PP2S = 5;
  /* the osc codes the log gives: the link down, never ready, ready lost */
  static const int32_t OSC_LINK_DOWN = 1;
  static const int32_t OSC_NEVER_READY = 2;
  static const int32_t OSC_READY_LOST = 3;

  if (pll.unit_failed == 0) {
    int32_t osc_code = 0;

    if (oscillator_kind == 0) {
      if (puck_warm() != 0) {
        pll.not_ready_seconds = 0;
        pll.oscillator_was_ready = 1;
      } else {
        pllp_count_pass(&pll.not_ready_seconds);
      }
      if (puck_alive == 0) {
        osc_code = OSC_LINK_DOWN;
      } else if (pll.not_ready_seconds > NOT_READY_FAIL) {
        osc_code = OSC_NEVER_READY;
      } else if (pll.oscillator_was_ready != 0 &&
                 pll.not_ready_seconds > READY_LOST_FAIL) {
        osc_code = OSC_READY_LOST;
      }
    }
    const uint16_t condition = status_hardware_condition();

    if ((condition & HARDWARE_BITS) != 0) {
      pllp_count_pass(&pll.hardware_fault_seconds);
    } else {
      pll.hardware_fault_seconds = 0;
    }
    if (pll.hardware_fault_seconds >= HARDWARE_FAIL || osc_code != 0) {
      pll.unit_failed = 1;
      log_write("Unit failed, OSR HW code: %d, osc code: %d",
                (int32_t)(int16_t)condition, osc_code);
      scpi_value_DIAGnostic_ROSCillator_EFControl_DATA = FAILED_EFC;
      diag_efc_mode = EFC_MODE_USER;
      pll.hold_causes |= HOLD_CAUSE_EFC_CONTROL;
      pll.ti_limit_override = 1;
      efc_clamp(
          integer_to_float(scpi_value_DIAGnostic_ROSCillator_EFControl_DATA));
      led_alarm = 1;
    }
  }
  if (pll.reference_hunt_seconds < HUNT_MOST) {
    pllp_count_pass(&pll.reference_hunt_seconds);
  }
  if (pll.cable_delay_changed != 0) {
    pll.cable_delay_changed = 0;
    pll.hold_causes &= ~HOLD_CAUSE_TEMPORARY_MODE;
  }
  const int32_t cable_delay = gps_cable_delay_heard();

  if (cable_delay > NO_CABLE_DELAY) {
    if (pll.cable_delay_known == 0 || cable_delay != pll.cable_delay) {
      pll.cable_delay_changed = 1;
      pll.hold_causes |= HOLD_CAUSE_TEMPORARY_MODE;
      pll.ti_limit_override = 1;
    }
    pll.cable_delay = cable_delay;
    pll.cable_delay_known = 1;
  }
  const bool powerup_unreferenced =
      pll.stage == PLL_STAGE_POWERUP && gps_reference_good == 0;

  if ((pll.timeref_switch_requested != 0 &&
       (powerup_unreferenced || pll.stage == PLL_STAGE_HOLDOVER)) ||
      (pll.reference_hunt_seconds > HUNT_SWITCH &&
       (powerup_unreferenced ||
        (pll.stage == PLL_STAGE_HOLDOVER &&
         (pll.hold_causes & SWITCH_HOLDS) != HOLD_CAUSE_NONE &&
         pll.holdover_duration > HOLDOVER_SWITCH)))) {
    timeref_switch();
    pll.cable_delay_known = 0;
    pll.ti_limit_override = 1;
    pll.reference_hunt_seconds = 0;
    pll.timeref_switch_requested = 0;
  }
  if (gps_reference_good != 0 || pll.reference_hunt_seconds <= MODE_SWITCH) {
    return;
  }
  if (pll.pulse_mode == REFERENCE_PULSE_1PPS && reference_pps_dead != 0 &&
      reference_pp2s_dead == 0) {
    pll.pulse_mode = REFERENCE_PULSE_PP2S;
    pll.block_length = BLOCK_PP2S;
    task_dmessage("Switched to PP2S mode\r\n");
  } else if (pll.pulse_mode == REFERENCE_PULSE_PP2S &&
             reference_pp2s_dead != 0 && reference_pps_dead == 0) {
    pll.pulse_mode = REFERENCE_PULSE_1PPS;
    pll.block_length = BLOCK_1PPS;
    task_dmessage("Switched to 1PPS mode\r\n");
  }
}

/* Gives up an awaited acknowledgment: one already in is no longer
 * awaited; one still to come is abandoned, and dropped when it comes
 * (0x4caa6, 0x4cd1c). */
static void pllp_abandon_ack(void) {
  if (gps_ack_ready == 0) {
    gps_ack_abandoned = 1;
  } else {
    gps_ack_awaited = GPS_ACK_NONE;
  }
}

/* The power-up recovery stage: raises powerup_flag, waits for a reading,
 * after a failed one for another, and sets the clock from the reference;
 * done unless a hold cause, failed if one, as a pll_stage_result
 * (0x4b6f6).  The Z3815A's. */
uint8_t pll_reference_check(char again) {
  pll_stage_result result = PLL_STAGE_RESULT_RUNNING;

  if (!again || (int8_t)pll.reference_check_state > PLL_REFERENCE_AGAIN) {
    pll.reference_check_state = PLL_REFERENCE_START;
  }
  switch (pll.reference_check_state) {
    case PLL_REFERENCE_FIRST:
      interval_wait_reading();
      if (interval_reading.valid == 0) {
        counter_reset();
        pll.reference_check_state = PLL_REFERENCE_AGAIN;
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
      powerup_flag = 1;
      interval_rearm(INTERVAL_ARM_CALIBRATE);
      pll.reference_check_state = PLL_REFERENCE_FIRST;
      break;
  }
  return result;
}

/* Sets the GPS lock and holdover LEDs' flags and the status they imply
 * for the stage just entered; locking logs it, to the external or the
 * GPS reference by field_3e9, and the locked stages clear the holdover
 * threshold and ask for the leap second check; leaving holdover clears
 * its three conditions (0x4ba32).  The idle stage changes neither.  The
 * 58503A's, but that it tests field_3e9 for field_3e5 and forces no
 * PWMA level. */
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
        log_event(pll.field_3e9 != 0 ? LOG_EXT_LOCK_STARTED
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
 * set and kept, a TFOM of 1 s, the ramped time constant at
 * initial_loop_time, the configuration the FPGA's table ends at, a
 * block of 10 readings and the first tolerance; and, for an oscillator
 * of kind 1, a largest time constant of 500 s (0x4c9a0;
 * docs/firmware/z3817a-3752.md, "The loop's constants"). */
void pll_init(void) {
  static const float FIRST_TFOM = 1.0f;
  static const float FIRST_TOLERANCE = 1e-06f;
  static const uint8_t FIELD_3EB_FIRST = 9;
  static const uint8_t BLOCK_READINGS = 10;
  static const uint8_t OSCILLATOR_KIND_500 = 1;
  static const float MAX_LOOP_TIME_KIND_500 = 500.0f;

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
  pll.field_3eb = FIELD_3EB_FIRST;
  pll.block_length = BLOCK_READINGS;
  pll.sync_requested = 0;
  gps_reference_good = 0;
  gps_reference_questionable = 0;
  time_zone_change_pending = 0;
  holdover_duration_exceeded = 0;
  pll.frequency_tolerance = FIRST_TOLERANCE;
  pll.field_3e9 = 0;
  if (oscillator_kind == OSCILLATOR_KIND_500) {
    max_loop_time = MAX_LOOP_TIME_KIND_500;
  }
}

/* Restarts the loop from power-up, pll cleared but for the FPGA's
 * configuration and loaded flag, the counter's registers loaded again,
 * the count's start select cleared and PE7 made to agree with the
 * TIMEREF selection, which pll_init has cleared (0x4ca66).  The
 * 58503A's ends at the stage. */
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
  counter_start_select_set(0);
  timeref_select_apply();
}

/* Sets the EFC's gain, the fractional frequency a count, and what the
 * loop derives from it: its reciprocal and the step limit in counts
 * (0x4cae2).  The 58503A's also sets the aging bound. */
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
 * acknowledgment, a setting's, from either of its two senders, completes
 * it (0x4cf9c to 0x4d03e).  The 58503A's takes a setting's from
 * GPS_SENDER_SETTING alone. */
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
  } else if (reply.sender == GPS_SENDER_SETTING ||
             reply.sender == GPS_SENDER_SETTING_OTHER) {
    pll_scpi_complete((int32_t*)&plla_unexpected_count,
                      reply.result == PLL_GPS_CONTROL_ACK_OK);
  } else {
    plla_unknown_count++;
  }
  plla_received_count++;
}

/* Counts the seconds an acknowledgment is awaited: one ready after 10 is
 * no longer awaited; at 1200 it times out (0x4d044 to 0x4d09e). */
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

/* Steps the stage a second, reporting one just entered (0x4cd3c to
 * 0x4cf54).  The 58503A's, but that a failed power-up reports the
 * restarted stage, and that the normal loop, given a block's mean past
 * 500 ns, starts the start-up loop again from initial_loop_time. */
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
      pll_stage_result result = pll_reference_check(same_stage);

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

/* The pllp task (0x4cb34): sets the loop up, from power-up or after a
 * reboot as power-up recovery, with the EFC's gain from the OCXO
 * calibration's a, the count's start select cleared and PE7 agreeing
 * with the TIMEREF selection; then each second steps its stage, counts
 * down the EFC control mode, checks the unit and the reference
 * (unit_and_reference_check), takes the GPS's acknowledgments, steps the
 * GPS sequences and the settings server and keeps the GPS's and the
 * loop's state and the ADC monitors in the preserved region.  The
 * 58503A's, but that this one clears the performance test as it starts
 * and has no TI setpoint to clear, where the 58503A's takes its gain from
 * a constant. */
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
  ptest_reset();
  efc_gain_set(scpi_value_DIAGnostic_ROSCillator_EFControl_ASLOPe);
  if (pll.stage == PLL_STAGE_INVALID) {
    pll.stage = PLL_STAGE_POWERUP;
    if (preserved_region.restore == PRESERVED_REGION_RESTORE) {
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
    pll.previous_stage = pll.stage;
    if (pll.stage != PLL_STAGE_POWERUP) {
      pll.stage = PLL_STAGE_POWERUP_RECOVERY;
    }
    log_event(LOG_REBOOT);
    pllp_rebooted = 1;
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
  counter_start_select_set(0);
  timeref_select_apply();
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
    unit_and_reference_check();
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
