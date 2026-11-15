/* monitors.c's functions that are this image's own: the rest are
 * common/monitors.c's, the same in every image (docs/firmware/rewrite.md,
 * "Shared C").  From the 58503A 3704's rewritten/monitors.c:
 * hmon_channel_read, whose decompiler's reading calls a channel's convert
 * routine without adc_read's reading, so that the averages it feeds
 * (monitor_average_add, monitor_filter_add, hmon_smoothed_add) differ;
 * hmon_task, whose decompiler's reading the function check finds otherwise,
 * the 58503A's code but where this image's differs, with hmon_channel_step,
 * whose decompiler's reading passes the table's routines a channel from no
 * register it set; and watchdog_report, which hmon_task calls, whose
 * decompiler's reading leaves supervisor mode on (0x33938).  The channel table
 * is the image's, hmon_channels at 0x32596 (a hand row), where the 58503A's C
 * defines its own.  Its comments' addresses are the Z3816A's.  Written by hand
 * in place of the decompiler's (docs/firmware/README.md, "Hand-written C");
 * each function keeps its generated prototype. With them, from the 58503A
 * 3704's, the Z3815A 4010's and the Z3805A 3543B's rewritten/monitors.c, of
 * its functions, those the Z3816A's code carries (identical or relocated by
 * image_diff, or changed with the same instructions), with the static helpers
 * they use, each the same here but where its comment says otherwise
 * (docs/firmware/z3816a-4001.md, "The link check").  Their comments' addresses
 * are the Z3816A's. */

/* hmon_channels' entries, of which hmon_channel_read reads the last for
 * a channel past it and hmon_channel_step steps to the first after. */
static const adc_channel MONITORS_LAST_CHANNEL = ADC_ANTENNA_CURRENT;

/* A byte argument as either copy of hmon_channels' routines reads it, a
 * convert routine's reading or another's channel: the image's at the
 * start of a 2-byte slot (hmon_convert_5v, 0x31dea, reads %sp@(4);
 * monitor_average_add, 0x31f44, %sp@(16) past its saves), gcc's at the
 * end of its 4-byte one.  The table holds the image's routines, which
 * gcc's copies of hmon_channel_read and hmon_channel_step call too. */
typedef struct monitors_byte_slot {
  uint8_t image;    /* +0x00: where the image's routines read it */
  uint8_t field_01; /* unread: the rest of the 2-byte slot */
  uint8_t field_02; /* unread */
  uint8_t gcc;      /* +0x03: where gcc's read it */
} monitors_byte_slot;

/* Reads an ADC channel, the last for one past it, through its entry's
 * convert routine; 0 for a channel without one (0x31eac). */
float hmon_channel_read(adc_channel channel) {
  typedef float convert_either(monitors_byte_slot slot);

  if (channel > MONITORS_LAST_CHANNEL) {
    channel = MONITORS_LAST_CHANNEL;
  }
  if (hmon_channels[channel].convert == 0) {
    return 0.0f;
  }
  const uint8_t reading = (uint8_t)adc_read(channel);

  return ((convert_either*)hmon_channels[channel].convert)(
      (monitors_byte_slot){.image = reading, .gcc = reading});
}

/* The health monitor's step: the channel hmon_channel_index names is
 * added to its monitor and, while its check says so or it has none,
 * reported; then the next channel, round robin (0x32414).  The 58503A
 * 3704's, through this image's table. */
void hmon_channel_step(void) {
  typedef void channel_either(monitors_byte_slot slot);
  typedef uint8_t test_either(monitors_byte_slot slot);
  const hmon_channel_entry* const entry = &hmon_channels[hmon_channel_index];
  const monitors_byte_slot slot = {.image = hmon_channel_index,
                                   .gcc = hmon_channel_index};

  if (entry->add != 0) {
    ((channel_either*)entry->add)(slot);
  }
  if (entry->report != 0) {
    const uint8_t due =
        entry->check == 0 ? 1 : ((test_either*)entry->check)(slot);

    if (due != 0) {
      ((channel_either*)entry->report)(slot);
    }
  }
  hmon_channel_index++;
  if (hmon_channel_index > MONITORS_LAST_CHANNEL) {
    hmon_channel_index = ADC_TEMPERATURE; /* the first */
  }
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
 * clock tick's interrupt signals, every tenth tick, it writes the EFC,
 * counts its pass for the watchdog, toggles the heartbeat LED and
 * reports the watchdog, checks both 1PPS, reporting each one's failure
 * once it has lasted and its end, steps the health monitor's channels,
 * checks the EFC and writes the lamps, blinking, and when asked
 * (0x33dc4).  The 58503A 3704's but for the heartbeat on OP6, as the
 * Z3815A 4010's (0x33e5e); the GPS 1PPS's failure reported after 12289
 * checks (0x33ec0), where the 58503A's waits 513; and no
 * gps_pps_unset_flag to withhold it (0x33ece).  Both 1PPS tests read one
 * result, D5 in the image: the last check's of either, and before any
 * its caller's (docs/firmware/quirks.md). */
void hmon_task(void) {
  static const uint32_t TICK_EVENT = 1;
  static const uint16_t HEARTBEAT_TICKS = 112;
  static const uint16_t PPS_CHECK_TICKS = 204;
  /* Checks a dead 1PPS lasts before its failure is reported. */
  static const uint16_t GPS_PPS_FAILED_CHECKS = 12289;
  static const uint16_t INTERNAL_PPS_FAILED_CHECKS = 717;
  static const uint16_t CHANNEL_TICKS = 10;
  static const uint16_t EFC_TICKS = 1024;
  static const uint16_t LAMP_TICKS = 51;
  /* The heartbeat's LED, OP6 here, which the others' tests name. */
  static const board_led HEARTBEAT = BOARD_LED_DUART;
  uint8_t checked = CPU_CALLERS_REGISTER(uint32_t, "d5");
  uint16_t heartbeat_ticks = 0;
  uint16_t gps_check_ticks = 0;
  uint16_t internal_check_ticks = 0;
  uint16_t lamp_ticks = 0;
  uint16_t channel_ticks = 0;
  uint16_t efc_ticks = 0;
  uint16_t gps_dead_checks = 0;
  uint16_t internal_dead_checks = 0;
  bool gps_failure = false;
  bool internal_failure = false;
  bool heartbeat_on = false;
  uint8_t blink = 0;

  watchdog_countdown = MONITORS_WATCHDOG_GRACE;
  efc_clamp_reset();
  psos_get_v(TICK_EVENT, 0, NULL);
  for (;;) {
    uint32_t error = (uint8_t)psos_wait_v(TICK_EVENT, 0, 0, NULL);

    efc_write_dithered();
    if (error == 0) {
      watchdog_monitor++;
    }
    if (monitors_period_up(&heartbeat_ticks, HEARTBEAT_TICKS)) {
      watchdog_report();
      heartbeat_on = !heartbeat_on;
      if (heartbeat_on) {
        board_leds_change(HEARTBEAT, 0);
      } else {
        board_leds_change(0, HEARTBEAT);
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
      if (gps_pps_dead == 0 || gps.references[0].valid != GPS_REFERENCE_VALID) {
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
    if (channel_ticks == 0) {
      hmon_channel_step();
      channel_ticks = CHANNEL_TICKS;
    }
    channel_ticks--;
    if (efc_ticks == 0) {
      hmon_check_efc();
      efc_ticks = EFC_TICKS;
    }
    efc_ticks--;
    if (lamp_ticks == 0) {
      lamps_compose();
      lamps_write(blink);
      blink = blink == 0;
      lamp_ticks = LAMP_TICKS;
    } else if (lamps_requested != 0) {
      lamps_requested = 0;
      lamps_compose();
      lamps_write(blink);
    }
    lamp_ticks--;
  }
}

/* Checks the tasks' watchdog counts, printing them if one is short,
 * and services the watchdog while the grace lasts; then clears them. */
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
    uint16_t sr = cpu_enter_supervisor();

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

/* The 58503A 3704's health monitor channels, by channel, as the image
 * has them at 0x32596 (hmon_channel_entry), so that hmon_channel_read
 * and the monitor step call the C's routines, with its argument
 * layout.  MONITORS_NO_EVENT is an entry's "none", 0x16, which
 * hmon_report_tolerance compares with and does not raise (0x32230,
 * 0x32294); 0x16 is STATUS_INPUT_OVERRUN's code. */
static const status_condition_code MONITORS_NO_EVENT = STATUS_INPUT_OVERRUN;

/* hmon_channels' entries, of which hmon_channel_read reads the last for
 * a channel past it: an array size, so #define. */
#define MONITORS_CHANNELS 8

static const float MONITORS_5V_SCALE = 0.0242f;

static const float MONITORS_15V_SCALE = 0.0763f;

static const float MONITORS_MINUS_15V_SCALE = 0.0438f;

static const float MONITORS_MINUS_15V_5V_SCALE = -0.084f;

static const float MONITORS_CURRENT_SCALE = 250.0f / 255.0f;

/* A reading times its scale. */
static float monitors_scaled(uint8_t reading, float scale) {
  return float_multiply(unsigned_to_float(reading), scale);
}

/* A reading times its scale, plus the 5V channel's reading times its
 * own. */
static float monitors_scaled_less_5v(uint8_t reading, float scale,
                                     float five_volt_scale) {
  float five_volt = monitors_scaled((uint8_t)adc_read(ADC_5V), five_volt_scale);

  return float_add(monitors_scaled(reading, scale), five_volt);
}

/* hmon_channels' 5V convert routine (0x31dea). */
float hmon_convert_5v(uint8_t reading) {
  return monitors_scaled(reading, MONITORS_5V_SCALE);
}

/* hmon_channels' 15V convert routine (0x31e04). */
float hmon_convert_15v(uint8_t reading) {
  return monitors_scaled(reading, MONITORS_15V_SCALE);
}

/* hmon_channels' -15V convert routine (0x31e1e). */
float hmon_convert_minus_15v(uint8_t reading) {
  return monitors_scaled_less_5v(reading, MONITORS_MINUS_15V_SCALE,
                                 MONITORS_MINUS_15V_5V_SCALE);
}

/* hmon_channels' Antenna current convert routine, entry 7's (0x31e92). */
float hmon_convert_antenna_current(uint8_t reading) {
  return monitors_scaled(reading, MONITORS_CURRENT_SCALE);
}

/* Starts every ADC monitor afresh, the Z3805A's monitors_reset's loop
 * (0x31f06). */
void adc_monitors_reset(void) {
  for (size_t index = 0; index < MONITORS_CHANNELS; index++) {
    adc_monitor* monitor = &adc_monitors[index];

    monitor->valid = 0;
    monitor->sample_index = 0;
    monitor->out_of_range_count = 0;
    monitor->out_of_tolerance = 0;
  }
}

/* The byte below hmon_channel_index, which hmon_reset clears and nothing
 * reads; the export declares no object for it. */
static uint8_t* const MONITORS_UNREAD_FLAG = (uint8_t*)0x102224;

/* Clears MONITORS_UNREAD_FLAG and starts the health monitor's step at its
 * first channel (0x31f30). */
void hmon_reset(void) {
  *MONITORS_UNREAD_FLAG = 0;
  hmon_channel_index = ADC_TEMPERATURE;
}

/* The filters' share of the old value and of the new, and the value
 * below which they go to 0. */
static const float MONITORS_FILTER_KEEP = 0.9f;

static const float MONITORS_FILTER_TAKE = 0.1f;

static const float MONITORS_FILTER_FLOOR = 1e-20f;

/* A monitor's value with a reading filtered in. */
static void monitors_filter(adc_monitor* monitor, float reading) {
  monitor->value =
      float_add(float_multiply(monitor->value, MONITORS_FILTER_KEEP),
                float_multiply(reading, MONITORS_FILTER_TAKE));
  if (float_compare(MONITORS_FILTER_FLOOR, monitor->value) > 0) {
    monitor->value = 0.0f;
  }
}

/* hmon_channels' add routine but for the Primary oven current's: puts
 * the channel's reading in its monitor's ring of ten and, once ten are
 * in, keeps their mean (0x31f3e).  The Z3805A's takes a monitor and a
 * reading. */
void monitor_average_add(adc_channel channel) {
  /* The health monitor's averages: ten samples. */
  static const uint32_t SAMPLES = 10;
  adc_monitor* monitor = &adc_monitors[channel];

  monitor->samples[monitor->sample_index++] = hmon_channel_read(channel);
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

/* The Primary oven current's add routine: filters the channel's reading
 * into its monitor, the first taken as it is (0x31fc4). */
void monitor_filter_add(adc_channel channel) {
  adc_monitor* monitor = &adc_monitors[channel];

  if (!monitor->valid) {
    monitor->valid = 1;
    monitor->value = hmon_channel_read(channel);
    return;
  }
  monitors_filter(monitor, hmon_channel_read(channel));
}

/* A report routine's message's size. */
#define MONITORS_MESSAGE_SIZE 80

/* Reports a channel out of tolerance, logged, printed and its entry's
 * first happening, or back within it, printed and its second, each once
 * by its monitor's out_of_tolerance; an entry's MONITORS_NO_EVENT is not
 * raised (0x32172). */
void hmon_report_tolerance(adc_channel channel, uint8_t out) {
  const hmon_channel_entry* entry = &hmon_channels[channel];
  adc_monitor* monitor = &adc_monitors[channel];
  char message[MONITORS_MESSAGE_SIZE];

  if (!out) {
    if (!monitor->out_of_tolerance) {
      return;
    }
    debug_sprintf(message, "%s is now within tolerance\r\n", entry->name);
    task_debug_message(message);
    if (entry->events[1] != MONITORS_NO_EVENT) {
      status_happening(entry->events[1]);
    }
    monitor->out_of_tolerance = 0;
    return;
  }
  if (monitor->out_of_tolerance) {
    return;
  }
  double average = float_to_double(adc_average(channel));

  log_write("%s is out of tolerance, value: %.2f", entry->name, average);
  debug_sprintf(message, "%s is out of tolerance, value: %.2f\r\n", entry->name,
                average);
  task_debug_message(message);
  if (entry->events[0] != MONITORS_NO_EVENT) {
    status_happening(entry->events[0]);
  }
  monitor->out_of_tolerance = 1;
}

/* The run of readings out of range past which a channel is out of
 * tolerance, where its count is held. */
static const uint8_t MONITORS_RANGE_LIMIT = 5;

/* Counts a valid monitor's consecutive values outside minimum to
 * maximum, held at MONITORS_RANGE_LIMIT; true once the count passes it.
 * The 58503A's tests its own limit, not limit, which it does not read;
 * nothing calls it (0x322b8). */
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
  past_limit = monitor->out_of_range_count > MONITORS_RANGE_LIMIT;
  if (past_limit) {
    monitor->out_of_range_count = MONITORS_RANGE_LIMIT;
  }
  return past_limit;
}

/* monitor_out_of_range on a channel's monitor and its entry's range
 * (0x3230e). */
uint8_t hmon_channel_out_of_range(uint8_t channel) {
  return monitor_out_of_range(
      &adc_monitors[channel], hmon_channels[channel].minimum,
      hmon_channels[channel].maximum, MONITORS_RANGE_LIMIT);
}

/* The 5V, 15V and -15V entries' report routine: the channel out of
 * tolerance after a run out of its range (0x3239e). */
void hmon_report_range(adc_channel channel) {
  hmon_report_tolerance(channel, hmon_channel_out_of_range(channel));
}

/* A check routine no entry names: whether the uptime passes the
 * channel's entry's field_0c (0x323ea). */
uint8_t hmon_channel_settled(uint8_t channel) {
  return get_uptime() > (uint32_t)hmon_channels[channel].field_0c;
}

/* A channel's monitored value, or its entry's nominal value while it has
 * none; a channel past the last is the last (0x324b0). */
float adc_average(adc_channel channel) {
  if (channel > ADC_ANTENNA_CURRENT) {
    channel = ADC_ANTENNA_CURRENT;
  }
  if (!adc_monitors[channel].valid) {
    return hmon_channels[channel].nominal;
  }
  return adc_monitors[channel].value;
}

/* A port's bits of mask, read until so many reads in a row agree or the
 * reads run out; 1 if any is set ("Reads until steady"). */
static uint8_t monitors_read_steady(volatile uint8_t* port, uint8_t mask,
                                    int32_t agreeing, int32_t reads) {
  uint8_t steady = *port & mask;
  int32_t agreed = 0;

  for (int32_t read = 0; agreed < agreeing && read < reads; read++) {
    uint8_t bits = *port & mask;

    if (bits == steady) {
      agreed++;
    } else {
      agreed = 0;
      steady = bits;
    }
  }
  return steady != 0;
}

/* How many reads in a row must agree, and within how many reads, for
 * PORTE0 and PORTF0. */
static const int32_t MONITORS_STEADY_AGREEING = 10;
static const int32_t MONITORS_STEADY_READS = 1000;

/* PORTE0's bits of mask, ten reads in a row within 1000; nothing calls
 * it (0x3366a). */
uint8_t porte_read_steady(uint8_t mask) {
  return monitors_read_steady(&sim.porte0, mask, MONITORS_STEADY_AGREEING,
                              MONITORS_STEADY_READS);
}

/* PORTF0's bits of mask, ten reads in a row within 1000; nothing calls
 * it (0x336b4). */
uint8_t portf0_read_steady(uint8_t mask) {
  return monitors_read_steady(&sim.portf0, mask, MONITORS_STEADY_AGREEING,
                              MONITORS_STEADY_READS);
}

/* PORTGP's bits of mask, four reads in a row within 100; nothing calls
 * it (0x336fe). */
uint8_t portgp_read_steady(uint8_t mask) {
  static const int32_t AGREEING = 4;
  static const int32_t READS = 100;

  return monitors_read_steady(&gpt.portgp, mask, AGREEING, READS);
}

/* Clamps an EFC to 0 to 1048560, reporting each clamp, and the first
 * value back in range (0x33798). */
float efc_clamp(float efc) {
  if (float_compare(0.0f, efc) > 0) {
    efc = 0.0f;
  } else if (float_compare(efc, PLL_EFC_MOST) > 0) {
    efc = PLL_EFC_MOST;
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

/* Reports pll.efc near the end of its range, outside 5% to 95%, once
 * six checks in a row find it there, and back inside at once
 * (0x3381a). */
void hmon_check_efc(void) {
  /* The near-end bounds the health monitor keeps the EFC's 20 bits
   * to, and the checks outside them it waits for. */
  static const float EFC_NEAR_LOW = 52428.0f;   /* 5% of 2^20 */
  static const float EFC_NEAR_HIGH = 996132.0f; /* 95% */
  /* The count past which it reports: the sixth check. */
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

/* Starts every ADC monitor afresh (0x3400c). */
void monitors_reset(void) { adc_monitors_reset(); }

/* The Primary oven current entry's check: 1 while the loop is in holdover
 * to normal, the stages 3 to 6, else 0 (0x323ce).  The image compares
 * pll.stage as a signed byte, which for an enum of 0 to 9 is the same. */
uint8_t hmon_check_pll_stage(adc_channel channel) {
  return pll.stage >= PLL_STAGE_HOLDOVER && pll.stage <= PLL_STAGE_NORMAL_PLL;
}

/* An add routine no entry names and nothing calls (revisions.md): filters
 * the channel's reading in as monitor_filter_add does, the first
 * kept in samples[0], and every 75th reading keeps the change since in
 * samples[1] and the value in samples[0], and sets samples[2] to 1, but
 * in the first 900 seconds, where the 58503A's waits 600, only when the
 * change passes the entry's minimum and the value is below its maximum
 * (0x32048). */
void hmon_smoothed_add(adc_channel channel) {
  static const uint8_t READINGS = 75;
  static const uint32_t SETTLING_SECONDS = 901;
  adc_monitor* monitor = &adc_monitors[channel];

  if (!monitor->valid) {
    monitor->valid = 1;
    monitor->value = hmon_channel_read(channel);
    monitor->samples[0] = monitor->value;
    monitor->samples[1] = hmon_channels[channel].minimum;
    monitor->samples[2] = 0.0f;
    return;
  }
  monitors_filter(monitor, hmon_channel_read(channel));
  if (++monitor->sample_index < READINGS) {
    return;
  }
  monitor->sample_index = 0;
  monitor->samples[1] = float_subtract(monitor->value, monitor->samples[0]);
  monitor->samples[0] = monitor->value;
  if (get_uptime() < SETTLING_SECONDS &&
      (float_compare(monitor->samples[1], hmon_channels[channel].minimum) <=
           0 ||
       float_compare(hmon_channels[channel].maximum, monitor->value) <= 0)) {
    return;
  }
  monitor->samples[2] = 1.0f;
}

/* Whether the Primary oven current's average is below 340, powerup's
 * warm-up test; the 58503A's tests the oven's reading below -2
 * (0x3256e). */
bool oven_warmed_up(void) {
  static const float OVEN_CURRENT_WARM = 340.0f;

  return float_compare(adc_average(ADC_OVEN_CURRENT), OVEN_CURRENT_WARM) < 0;
}

/* Clears the GPS 1PPS monitor's state (0x33ab6). */
void gps_pps_monitor_reset(void) {
  gps_pps_skipped_checks = 0;
  gps_pps_unconfirmed = 0;
  gps_pps_dead = 0;
  gps_pps_counting = 0;
}

/* Whether the 1PPS monitors may check: no self-test running and the
 * FPGA, which counts the internal 1PPS, loaded. */
static bool monitors_pps_checkable(void) {
  return !selftest.running && pll.fpga_loaded && !fpga_programming;
}

/* Checks the internal 1PPS by its count since the last check, from the
 * second call that may on, reporting it dead or back; returns whether
 * it checked (0x33c4a).  While internal_pps_disabled is set every count
 * is dead. */
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
  if (count >= MONITORS_PPS_LEAST && count <= MONITORS_PPS_MOST &&
      !internal_pps_disabled) {
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

/* Clears the internal 1PPS monitor's state, internal_pps_disabled first
 * (0x33c36). */
void internal_pps_monitor_reset(void) {
  internal_pps_disabled = 0;
  internal_pps_dead = 0;
  internal_pps_started = 0;
}

/* Checks the GPS 1PPS by the pulses the GPT has counted, reporting it
 * dead, at the wrong frequency, or back; returns 0 when it could not
 * check (0x33ad0). */
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

/* The Primary oven current entry's report routine: out of tolerance
 * until oven_warmed_up, where the 58503A's takes double_oven_warmed_up,
 * the same test there (0x323b4). */
void hmon_report_oven_warm(adc_channel channel) {
  hmon_report_tolerance(channel, !oven_warmed_up());
}

/* The Primary oven current's scale, the double at 0x31e78. */
static const double MONITORS_OVEN_CURRENT_SCALE = 0x1.1f4b5e1f4b5e2p+2;

/* hmon_channels' Primary oven current convert routine, entry 6's: the
 * reading times MONITORS_OVEN_CURRENT_SCALE in double precision, where
 * the 58503A's takes 250/255 in single (0x31e66). */
float hmon_convert_oven_current(uint8_t reading) {
  return double_to_float(
      double_multiply(float_to_double(unsigned_to_float(reading)),
                      MONITORS_OVEN_CURRENT_SCALE));
}

/* :DIAG:GPS:ACURrent's alarm while it is enabled: -1 for an antenna
 * current below 10, 1 above 90, else 0; the 58503A's gives 1 for both
 * outside 10 to 200.  Nothing calls it (0x324fc). */
int8_t hmon_check_antenna_current(void) {
  static const float ANTENNA_CURRENT_LEAST = 10.0f;
  static const float ANTENNA_CURRENT_MOST = 90.0f;
  float average;

  if (nv_settings.antenna_current_alarm == 0) {
    return 0;
  }
  average = adc_average(ADC_ANTENNA_CURRENT);
  if (float_compare(ANTENNA_CURRENT_LEAST, average) > 0) {
    return -1;
  }
  return float_compare(average, ANTENNA_CURRENT_MOST) > 0;
}

/* The oven current monitor's settled flag, past which ocxo_warm takes the
 * OCXO for warm. */
static const float MONITORS_OCXO_WARM = 0.9f;

/* Whether the OCXO is warm: the oven current monitor's third sample past
 * 0.9, the Z3815A's ocxo_warm without its puck (0x3254a). */
uint8_t ocxo_warm(void) {
  return float_compare(adc_monitors[ADC_OVEN_CURRENT].samples[2],
                       MONITORS_OCXO_WARM) > 0;
}

/* Clears both 1PPS monitors, the outer oven's flag, the health monitor's
 * step, :DIAGnostic:TMODe and the lamps' state, as the 58503A's, and
 * latch_bits, and sets the heartbeat's shadow bit (0x33fc2). */
void pps_monitors_reset(void) {
  gps_pps_monitor_reset();
  internal_pps_monitor_reset();
  outer_oven_enabled = 0;
  hmon_reset();
  diag_tmode_state = 0;
  diag_tmode_data = 0;
  latch_bits = 0;
  lamp_bits = 0;
  lamp_blink = 0;
  lamps_requested = 0;
  duart_output_bit_set(BOARD_LED_HEARTBEAT, 1);
}

/* The DUART's input port's bits alarm_hardware_inputs reads, each low on
 * its fault, and the alarm bits it gives for them (0x49f22 to 0x49f40);
 * which signal each is, is inferred from the Z3815A 4010's
 * alarm_hardware_update, which reads the same bits. */
static const uint8_t MONITORS_INPUT_19_6608_MHZ = 0x04;
static const uint8_t MONITORS_INPUT_PP2S = 0x10;
static const uint8_t MONITORS_INPUT_10_MHZ = 0x20;
static const uint8_t ALARM_HARDWARE_19_6608_MHZ = 1 << 0;
static const uint8_t ALARM_HARDWARE_10_MHZ = 1 << 1;
static const uint8_t ALARM_HARDWARE_PP2S = 1 << 2;

/* The DUART output port's bit alarm_hardware_inputs sets as it is done,
 * after clearing it once it has read the inputs. */
static const uint8_t MONITORS_OUTPUT_OP2 = 0x04;

/* The hardware alarm's input bits, one a fault: while the loop runs, the
 * DUART's inputs, OP2 set and cleared after; while it is stopped, the
 * 10 MHz bit of the value D3 holds on entry, which the image reads
 * unset, status_summary_update's hardware condition, OP2 set
 * (0x49f06). */
uint8_t alarm_hardware_inputs(void) {
  register uint32_t entry_d3 __asm__("d3");
  uint8_t faults = 0;
  char set;

  __asm__("" : "=r"(entry_d3));
  if (pll.loop_running != 0) {
    uint8_t input = duart_input_port_read();

    if ((input & MONITORS_INPUT_19_6608_MHZ) == 0) {
      faults |= ALARM_HARDWARE_19_6608_MHZ;
    }
    if ((input & MONITORS_INPUT_10_MHZ) == 0) {
      faults |= ALARM_HARDWARE_10_MHZ;
    }
    if ((input & MONITORS_INPUT_PP2S) == 0) {
      faults |= ALARM_HARDWARE_PP2S;
    }
    duart_output_bit_set(MONITORS_OUTPUT_OP2, 1);
    set = 0;
  } else {
    if ((entry_d3 & MONITORS_INPUT_10_MHZ) == 0) {
      faults |= ALARM_HARDWARE_10_MHZ;
    }
    set = 1;
  }
  duart_output_bit_set(MONITORS_OUTPUT_OP2, set);
  return faults;
}
