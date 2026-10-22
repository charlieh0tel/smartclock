/* The health monitor and the PPS monitors, written by hand in place of
 * the decompiler's (docs/firmware/README.md, "Hand-written C";
 * docs/firmware/modules.md, `services/monitors`).  The exported C
 * includes this file after its own, so the types and prototypes it
 * declares are in scope; each function here keeps its generated
 * prototype.  It is the ADC channels' averages and filter (monitor_*,
 * adc_average), the supplies' and the EFC's checks and the EFC's clamp,
 * the ovens' warm-up, the GPS and internal PPS checks and their resets,
 * and the watchdog's report.  The monitors' state is
 * docs/firmware/structures.md's "The health monitor's supplies and EFC"
 * and "The health monitor's ADC monitors". */

/* Clamps an EFC to 0 to 1048560, reporting each clamp, and the first
 * value back in range (0x2426e). */
float efc_clamp(float efc) {
  /* The EFC's clamp, the top of its 20 bits. */
  static const float EFC_MOST = 1048560.0f;

  if (float_compare(0.0f, efc) > 0) {
    efc = 0.0f;
  } else if (float_compare(efc, EFC_MOST) > 0) {
    efc = EFC_MOST;
  } else {
    if (efc_range.clamped) {
      status_happening(STATUS_EFC_AT_LIMIT_CLEARED);
      efc_range.clamped = 0;
    }
    return efc;
  }
  status_happening(STATUS_EFC_AT_LIMIT_SET);
  efc_range.clamped = 1;
  return efc;
}

/* Clears efc_clamp's and hmon_check_efc's state (0x242de). */
void efc_clamp_reset(void) {
  efc_range.clamped = 0;
  efc_range.near_end = 0;
  efc_range.near_end_checks = 0;
}

/* Starts every ADC monitor afresh, and the supplies' check at its first
 * step; hmon_task calls it as it starts (0x23c2a). */
void monitors_reset(void) {
  for (size_t index = 0; index < sizeof adc_monitors / sizeof adc_monitors[0];
       index++) {
    adc_monitor* monitor = &adc_monitors[index];

    monitor->valid = 0;
    monitor->sample_index = 0;
    monitor->out_of_range_count = 0;
    monitor->out_of_tolerance = 0;
  }
  hmon_supply_step = 0;
}

/* Puts a reading in a monitor's ring of ten and, once ten are in, keeps
 * their mean (0x23c5a). */
void monitor_average_add(adc_monitor* monitor, float reading) {
  /* The health monitor's averages: ten samples. */
  static const uint32_t SAMPLES = 10;

  monitor->samples[monitor->sample_index++] = reading;
  if (monitor->sample_index >= SAMPLES) {
    monitor->sample_index = 0;
    monitor->valid = 1;
  }
  if (monitor->valid) {
    float sum = 0.0f;

    for (uint32_t i = 0; i < SAMPLES; i++) {
      sum = float_add(sum, monitor->samples[i]);
    }
    monitor->value = float_divide(sum, SAMPLES);
  }
}

/* Filters a reading into a monitor, the first taken as it is
 * (0x23cc4). */
void monitor_filter_add(adc_monitor* monitor, float reading) {
  /* The oscillator current's filter, 0.9 of the old value and 0.1 of
   * the new, which goes to 0 below 1e-20. */
  static const float FILTER_KEEP = 0.9f;
  static const float FILTER_TAKE = 0.1f;
  static const float FILTER_FLOOR = 1e-20f;

  if (!monitor->valid) {
    monitor->valid = 1;
    monitor->value = reading;
    return;
  }
  monitor->value = float_add(float_multiply(monitor->value, FILTER_KEEP),
                             float_multiply(reading, FILTER_TAKE));
  if (float_compare(FILTER_FLOOR, monitor->value) > 0) {
    monitor->value = 0.0f;
  }
}

/* Counts a valid monitor's consecutive values outside minimum to
 * maximum, held at limit; true once the count passes the limit
 * (0x23d28). */
uint8_t monitor_out_of_range(adc_monitor* monitor, float minimum, float maximum,
                             uint8_t limit) {
  bool past_limit;

  if (!monitor->valid) {
    return 0;
  }
  if (float_compare(monitor->value, minimum) < 0 ||
      float_compare(monitor->value, maximum) > 0) {
    monitor->out_of_range_count++;
  } else {
    monitor->out_of_range_count = 0;
  }
  past_limit = monitor->out_of_range_count > limit;
  if (past_limit) {
    monitor->out_of_range_count = limit;
  }
  return past_limit;
}

/* hmon_check_supplies' readings in the order hmon_supply_step takes
 * them, which is also adc_monitors' order and the ADC channels'
 * (structures.md, "The health monitor's ADC monitors"). */
enum {
  MONITORS_STEP_TEMPERATURE,
  MONITORS_STEP_DOUBLE_OVEN,
  MONITORS_STEP_PLUS_15V,
  MONITORS_STEP_OSCILLATOR_CURRENT,
  MONITORS_STEP_5V,
  MONITORS_STEP_OVEN,
  MONITORS_STEP_MINUS_15V,
  MONITORS_STEP_ANTENNA_CURRENT,
  MONITORS_STEPS,
};

/* A supply whose tolerance hmon_check_supplies reports: its name, as
 * the messages give it, and the status happenings for leaving and
 * returning. */
typedef struct monitors_supply {
  adc_channel channel;
  char* out_message;
  char* out_log;
  char* in_message;
  status_condition_code out_happening;
  status_condition_code in_happening;
} monitors_supply;

/* Reports a supply leaving its tolerance, or returning to it, as
 * out_of_range says, if that changes the monitor's state. */
static void monitors_supply_report(const monitors_supply* supply,
                                   bool out_of_range) {
  adc_monitor* monitor = &adc_monitors[supply->channel];

  if (!out_of_range) {
    if (monitor->out_of_tolerance) {
      task_debug_message(supply->in_message);
      status_happening(supply->in_happening);
      monitor->out_of_tolerance = 0;
    }
  } else if (!monitor->out_of_tolerance) {
    float value = adc_average(supply->channel);
    char text[80];

    log_write(supply->out_log, float_to_double(value));
    debug_sprintf(text, supply->out_message, float_to_double(value));
    task_debug_message(text);
    status_happening(supply->out_happening);
    monitor->out_of_tolerance = 1;
  }
}

static const monitors_supply monitors_5v = {
    ADC_5V,
    "5V is out of tolerance, value: %.2f\r\n",
    "5V is out of tolerance, value: %.2f",
    "5V is now within tolerance\r\n",
    STATUS_PLUS_5V_SET,
    STATUS_PLUS_5V_CLEARED,
};

static const monitors_supply monitors_double_oven = {
    ADC_DOUBLE_OVEN,
    "Secondary oven voltage is out of tolerance, value: %.2f\r\n",
    "Secondary oven voltage is out of tolerance, value: %.2f",
    "Secondary oven voltage is now within tolerance\r\n",
    STATUS_SECONDARY_OVEN_SET,
    STATUS_SECONDARY_OVEN_CLEARED,
};

static const monitors_supply monitors_plus_15v = {
    ADC_PLUS_15V,
    "15V is out of tolerance, value: %.2f\r\n",
    "15V is out of tolerance, value: %.2f",
    "15V is now within tolerance\r\n",
    STATUS_PLUS_15V_SET,
    STATUS_PLUS_15V_CLEARED,
};

static const monitors_supply monitors_minus_15v = {
    ADC_MINUS_15V,
    "-15V is out of tolerance, value: %.2f\r\n",
    "-15V is out of tolerance, value: %.2f",
    "-15V is now within tolerance\r\n",
    STATUS_MINUS_15V_SET,
    STATUS_MINUS_15V_CLEARED,
};

/* Takes one of the eight readings into its monitor, hmon_supply_step's,
 * and reports the supplies' and the double oven's tolerances; then
 * steps on (0x23d80). */
void hmon_check_supplies(void) {
  /* The supplies' tolerances and the readings outside one that count
   * as leaving it. */
  static const uint8_t TOLERANCE_LIMIT = 5;
  static const float MONITORS_5V_LEAST = 4.5f;
  static const float MONITORS_5V_MOST = 5.5f;
  static const float PLUS_15V_LEAST = 14.0f;
  static const float PLUS_15V_MOST = 16.0f;
  static const float MINUS_15V_LEAST = -16.0f;
  static const float MINUS_15V_MOST = -14.0f;
  static const float DOUBLE_OVEN_LEAST = -1e37f;
  static const float DOUBLE_OVEN_MOST = 6.8f;
  adc_monitor* monitor = &adc_monitors[hmon_supply_step];

  switch (hmon_supply_step) {
    case MONITORS_STEP_TEMPERATURE:
      monitor_average_add(monitor, read_adc_temp());
      break;
    case MONITORS_STEP_DOUBLE_OVEN:
      monitor_average_add(monitor, read_adc_doven());
      /* the double oven's tolerance only while the outer oven is on */
      monitors_supply_report(
          &monitors_double_oven,
          outer_oven_enabled &&
              monitor_out_of_range(monitor, DOUBLE_OVEN_LEAST, DOUBLE_OVEN_MOST,
                                   TOLERANCE_LIMIT));
      break;
    case MONITORS_STEP_PLUS_15V:
      monitor_average_add(monitor, read_adc_p15v());
      monitors_supply_report(
          &monitors_plus_15v,
          monitor_out_of_range(monitor, PLUS_15V_LEAST, PLUS_15V_MOST,
                               TOLERANCE_LIMIT));
      break;
    case MONITORS_STEP_OSCILLATOR_CURRENT:
      monitor_filter_add(monitor, read_adc_osc_curr());
      break;
    case MONITORS_STEP_5V:
      monitor_average_add(monitor, read_adc_5v());
      monitors_supply_report(
          &monitors_5v,
          monitor_out_of_range(monitor, MONITORS_5V_LEAST, MONITORS_5V_MOST,
                               TOLERANCE_LIMIT));
      break;
    case MONITORS_STEP_OVEN:
      monitor_average_add(monitor, read_adc_oven());
      break;
    case MONITORS_STEP_MINUS_15V:
      monitor_average_add(monitor, read_adc_m15v());
      monitors_supply_report(
          &monitors_minus_15v,
          monitor_out_of_range(monitor, MINUS_15V_LEAST, MINUS_15V_MOST,
                               TOLERANCE_LIMIT));
      break;
    case MONITORS_STEP_ANTENNA_CURRENT:
      monitor_average_add(monitor, read_adc_ant_curr());
      break;
  }
  if (++hmon_supply_step >= MONITORS_STEPS) {
    hmon_supply_step = 0;
  }
}

/* What adc_average gives a channel while its monitor has no value, by
 * channel. */
static const float monitors_adc_defaults[] = {
    [ADC_TEMPERATURE] = 42.0f, [ADC_DOUBLE_OVEN] = 4.0f, [ADC_PLUS_15V] = 15.0f,
    [ADC_CURRENT] = 250.0f,    [ADC_5V] = 5.0f,          [ADC_OVEN] = 4.0f,
    [ADC_MINUS_15V] = -15.0f,
};

/* A channel's monitored value, or its default while it has none; 0
 * for a channel out of range (0x2410e). */
float adc_average(adc_channel channel) {
  if (channel > ADC_MINUS_15V) {
    return 0.0f;
  }
  if (!adc_monitors[channel].valid) {
    return monitors_adc_defaults[channel];
  }
  return adc_monitors[channel].value;
}

/* Whether the double oven reads above 1.5; nothing calls it
 * (0x241cc). */
bool double_oven_warmed_up(void) {
  /* The double oven's warm-up threshold, in the reading's volts. */
  static const float DOUBLE_OVEN_WARM = 1.5f;

  return float_compare(adc_average(ADC_DOUBLE_OVEN), DOUBLE_OVEN_WARM) > 0;
}

/* Whether the oven reads below -2, powerup's warm-up test (0x241f4). */
bool oven_warmed_up(void) {
  /* The oven's warm-up threshold, in the reading's volts. */
  static const float OVEN_WARM = -2.0f;

  return float_compare(adc_average(ADC_OVEN), OVEN_WARM) < 0;
}

/* Reports pll.efc near the end of its range, outside 5% to 95%, once
 * five checks in a row find it there, and back inside at once
 * (0x242f0). */
void hmon_check_efc(void) {
  /* The near-end bounds the health monitor keeps the EFC's 20 bits
   * to, and the checks outside them it waits for. */
  static const float EFC_NEAR_LOW = 52428.0f;   /* 5% of 2^20 */
  static const float EFC_NEAR_HIGH = 996132.0f; /* 95% */
  static const uint8_t NEAR_END_CHECKS = 5;

  if (float_compare(pll.efc, EFC_NEAR_LOW) >= 0 &&
      float_compare(pll.efc, EFC_NEAR_HIGH) <= 0) {
    efc_range.near_end_checks = 0;
    if (efc_range.near_end) {
      efc_range.near_end = 0;
      status_happening(STATUS_EFC_NEAR_LIMIT_CLEARED);
    }
    return;
  }
  if (++efc_range.near_end_checks > NEAR_END_CHECKS) {
    efc_range.near_end_checks = NEAR_END_CHECKS;
    if (!efc_range.near_end) {
      task_debug_message("EFC near end of range\r\n");
      status_happening(STATUS_EFC_NEAR_LIMIT_SET);
      efc_range.near_end = 1;
    }
  }
}

/* The reports, after a full one, for which the health monitor services
 * the watchdog. */
static const int16_t MONITORS_WATCHDOG_GRACE = 8;

/* Checks the tasks' watchdog counts, printing them if one is short,
 * and services the watchdog while the grace lasts; then clears them
 * (0x2437a). */
void watchdog_report(void) {
  /* The watchdog: each task counts its passes, and the health monitor
   * services the MC68331's software watchdog (MC68331UM D.3.15, SWSR's
   * CPU_SWSR_FIRST then CPU_SWSR_SECOND) only while every count since
   * the last report is enough, and for eight reports after. */
  static const uint8_t WATCHDOG_GPS_LEAST = 21;
  static const uint8_t WATCHDOG_MONITOR_LEAST = 51;

  if (watchdog_clock != 0 && watchdog_gps >= WATCHDOG_GPS_LEAST &&
      watchdog_monitor >= WATCHDOG_MONITOR_LEAST && watchdog_spool != 0 &&
      watchdog_pll != 0) {
    watchdog_countdown = MONITORS_WATCHDOG_GRACE;
  } else {
    debug_printf(
        "watchdog: clock: %4d gps: %4d pll: %4d mon: %4d spool %4d\r\n",
        watchdog_clock, watchdog_gps, watchdog_pll, watchdog_monitor,
        watchdog_spool);
  }
  if (watchdog_countdown != 0) {
    watchdog_countdown--;
  }
  if (watchdog_countdown != 0) {
    uint16_t sr = enter_supervisor();

    sim.swsr = CPU_SWSR_FIRST;
    sim.swsr = CPU_SWSR_SECOND;
    cpu_sr_write(sr);
  }
  watchdog_clock = 0;
  watchdog_gps = 0;
  watchdog_pll = 0;
  watchdog_monitor = 0;
  watchdog_spool = 0;
}

/* Clears the GPS 1PPS monitor's state (0x2458c). */
void gps_pps_monitor_reset(void) {
  gps_pps_skipped_checks = 0;
  gps_pps_unconfirmed = 0;
  gps_pps_dead = 0;
  gps_pps_counting = 0;
}

/* Clears the internal 1PPS monitor's state (0x2470c). */
void internal_pps_monitor_reset(void) {
  internal_pps_dead = 0;
  internal_pps_started = 0;
}

/* The internal 1PPS's count between checks: 1 to 3 is alive. */
static const int8_t MONITORS_PPS_LEAST = 1;

static const int8_t MONITORS_PPS_MOST = 3;

/* Whether the 1PPS monitors may check: no self-test running and the
 * FPGA, which counts the internal 1PPS, loaded. */
static bool monitors_pps_checkable(void) {
  return !selftest.running && pll.fpga_loaded && !fpga_programming;
}

/* Checks the internal 1PPS by its count since the last check, from the
 * second call that may on, reporting it dead or back; returns whether
 * it checked (0x2471a). */
uint8_t internal_pps_check(void) {
  bool checkable = monitors_pps_checkable();
  int8_t count;

  if (!checkable || !internal_pps_started) {
    if (checkable) {
      internal_pps_started = 1;
    }
    internal_pps_last_count = internal_pps_count;
    return 0;
  }
  count = internal_pps_count - internal_pps_last_count;
  internal_pps_last_count = internal_pps_count;
  if (count >= MONITORS_PPS_LEAST && count <= MONITORS_PPS_MOST) {
    if (internal_pps_dead) {
      task_dmessage("Internal 1pps signal is ok\r\n");
      internal_pps_dead = 0;
    }
  } else if (!internal_pps_dead) {
    task_dmessage("Internal 1pps signal is dead\r\n");
    debug_printf("count %d\r\n", count);
    internal_pps_dead = 1;
  }
  return 1;
}

/* Checks the GPS 1PPS by the pulses the GPT has counted, reporting it
 * dead, at the wrong frequency, or back; returns 0 when it could not
 * check (0x245a6). */
uint8_t gps_pps_check(void) {
  /* The GPS 1PPS monitor: the GPT's pulse accumulator counts its
   * rising edges (PACTL's PAEN and PEDGE, MC68331UM D.2.7); each check
   * wants 1 to 3 pulses since the last, and every fifth check's gate 8
   * to 12 in all.  While the check is unconfirmed it skips 15 checks
   * first. */
  static const uint8_t PACTL_COUNT_RISING = 0x50;
  static const uint8_t PPS_GATE_STEP = 2;
  static const uint8_t PPS_GATE = 10;
  static const uint8_t PPS_GATE_LEAST = 8;
  static const uint8_t PPS_GATE_MOST = 12;
  static const uint8_t PPS_SKIPS = 15;
  uint8_t pulses;
  int8_t since_last;
  bool short_gate_bad = false;
  uint8_t dead;

  if (gps.pps_check_disabled) {
    gps_pps_unconfirmed = 1;
    gps_pps_skipped_checks = 0;
    gps_pps_dead = 1;
    gpt.pacnt = 0;
    gps_pps_gate = 0;
    gps_pps_last_count = 0;
    return 1;
  }
  if (gps_pps_unconfirmed && gps_pps_skipped_checks < PPS_SKIPS) {
    gps_pps_skipped_checks++;
    return 1;
  }
  if (!monitors_pps_checkable() || !gps_pps_counting) {
    gpt.pacnt = 0;
    if (monitors_pps_checkable()) {
      gpt.pactl = PACTL_COUNT_RISING;
      gps_pps_counting = 1;
    }
    gps_pps_gate = 0;
    gps_pps_last_count = 0;
    return 0;
  }
  pulses = gpt.pacnt;
  since_last = pulses - gps_pps_last_count;
  dead = gps_pps_dead;
  if (since_last < MONITORS_PPS_LEAST || since_last > MONITORS_PPS_MOST) {
    short_gate_bad = true;
    if (!dead) {
      task_dmessage("GPS 1pps signal is dead\r\n");
      debug_printf("short gate, count %d\r\n", since_last);
      dead = 1;
    }
  }
  gps_pps_gate += PPS_GATE_STEP;
  if (gps_pps_gate == PPS_GATE) {
    gpt.pacnt = 0;
    if (pulses < PPS_GATE_LEAST || pulses > PPS_GATE_MOST) {
      if (!dead) {
        task_dmessage("GPS 1pps signal frequency is incorrect\r\n");
        debug_printf("long gate, count %d\r\n", pulses);
        dead = 1;
      }
    } else if (!short_gate_bad) {
      if (dead) {
        task_dmessage("GPS 1pps signal is ok\r\n");
        dead = 0;
      }
      gps_pps_unconfirmed = 0;
    }
  } else if (gps_pps_gate < PPS_GATE) {
    gps_pps_dead = dead;
    gps_pps_last_count = pulses;
    return 1;
  } else {
    gpt.pacnt = 0;
  }
  gps_pps_dead = dead;
  gps_pps_last_count = 0;
  gps_pps_gate = 0;
  return 1;
}

/* Clears both 1PPS monitors and the outer oven's flag (0x24984). */
void pps_monitors_reset(void) {
  gps_pps_monitor_reset();
  internal_pps_monitor_reset();
  outer_oven_enabled = 0;
}

/* Counts down a period of ticks: true, and the count started again, as
 * it reaches the period. */
static bool monitors_period_up(uint16_t* count, uint16_t period) {
  (*count)++;
  if (*count < period) {
    return false;
  }
  *count = 0;
  return true;
}

/* The health monitor's task, which root_task starts: on each event the
 * clock tick's interrupt signals, every tenth tick (user_66, 0x11f82),
 * it writes the EFC, counts its pass for the watchdog, toggles the
 * heartbeat LED and reports the watchdog, checks both 1PPS, reporting
 * each one's failure once it has lasted and its end, and the supplies
 * and the EFC (0x247d4).  Both 1PPS tests read one result, D4 in the
 * image: the last check's of either, and before any its caller's
 * (docs/firmware/quirks.md). */
void hmon_task(void) {
  static const uint32_t TICK_EVENT = 1;
  static const uint16_t HEARTBEAT_TICKS = 112;
  static const uint16_t PPS_CHECK_TICKS = 204;
  /* Checks a dead 1PPS lasts before its failure is reported. */
  static const uint16_t GPS_PPS_FAILED_CHECKS = 513;
  static const uint16_t INTERNAL_PPS_FAILED_CHECKS = 717;
  static const uint16_t SUPPLY_TICKS = 10;
  static const uint16_t EFC_TICKS = 1024;
  uint8_t checked = CPU_CALLERS_REGISTER(uint32_t, "d4");
  bool heartbeat_on = false;
  uint16_t heartbeat_ticks = 0;
  uint16_t gps_check_ticks = 0;
  uint16_t internal_check_ticks = 0;
  uint16_t supply_ticks = 0;
  uint16_t efc_ticks = 0;
  uint16_t gps_dead_checks = 0;
  uint16_t internal_dead_checks = 0;
  bool gps_failure = false;
  bool internal_failure = false;

  watchdog_countdown = MONITORS_WATCHDOG_GRACE;
  monitors_reset();
  efc_clamp_reset();
  psos_get_v(TICK_EVENT, 0, NULL);
  for (;;) {
    uint32_t error = psos_wait_v(TICK_EVENT, 0, 0, NULL);

    efc_write_dithered();
    if ((uint8_t)error == 0) {
      watchdog_monitor++;
    }
    if (monitors_period_up(&heartbeat_ticks, HEARTBEAT_TICKS)) {
      watchdog_report();
      heartbeat_on = !heartbeat_on;
      if (heartbeat_on) {
        duart_output_bits(BOARD_LED_HEARTBEAT, 0);
      } else {
        duart_output_bits(0, BOARD_LED_HEARTBEAT);
      }
    }
    if (gps.pps_check_disabled != 0) {
      gps_pps_unconfirmed = 1;
      gps_pps_skipped_checks = 0;
      gps_pps_dead = 1;
    }
    if (monitors_period_up(&gps_check_ticks, PPS_CHECK_TICKS)) {
      checked = gps_pps_check();
    }
    if (checked != 0) {
      if (gps_pps_dead == 0 || gps.reference_qualified == 0) {
        gps_dead_checks = 0;
        if (gps_failure) {
          status_happening(STATUS_GPS_PPS_FAILURE_CLEARED);
          gps_failure = false;
        }
      } else if (gps_dead_checks < GPS_PPS_FAILED_CHECKS) {
        gps_dead_checks++;
      } else if (gps_pps_unconfirmed == 0 && !gps_failure) {
        status_happening(STATUS_GPS_PPS_FAILURE_SET);
        gps_failure = true;
      }
    }
    if (monitors_period_up(&internal_check_ticks, PPS_CHECK_TICKS)) {
      checked = internal_pps_check();
    }
    if (checked != 0) {
      if (internal_pps_dead == 0) {
        internal_dead_checks = 0;
        if (internal_failure) {
          status_happening(STATUS_INTERNAL_REFERENCE_CLEARED);
          internal_failure = false;
        }
      } else if (internal_dead_checks < INTERNAL_PPS_FAILED_CHECKS) {
        internal_dead_checks++;
      } else if (!internal_failure) {
        status_happening(STATUS_INTERNAL_REFERENCE_SET);
        internal_failure = true;
      }
    }
    if (supply_ticks == 0) {
      hmon_check_supplies();
      supply_ticks = SUPPLY_TICKS;
    }
    supply_ticks--;
    if (efc_ticks == 0) {
      hmon_check_efc();
      efc_ticks = EFC_TICKS;
    }
    efc_ticks--;
  }
}
