/* The Z3815A 4010's hmon_channel_read, from its monitors.c, and its
 * adc_averages_print, from its scpi_diagnostic.c, copied for the 58503B
 * 1.01.04, each relocated in image_diff's matches against the Z3815A
 * (docs/firmware/58503b-1.01.04.md, "The health monitor's channels"):
 * the decompiler's drop the convert routine's reading and adc_average's
 * value from the calls they make.  The Z3816A 4001's hmon_channel_step,
 * relocated, whose decompiler's reading passes the table's routines a
 * channel from no register it set; hmon_task, the 58503A 3704's but
 * where this image's differs, and watchdog_report, the 58503A's,
 * relocated, whose decompiler's reading leaves supervisor mode on.
 * Written by hand in place of the decompiler's (docs/firmware/README.md,
 * "Hand-written C"); each keeps its generated prototype.  hmon_channels
 * (0x31b12) stays the image's ROM data, typed through names.tsv, so its
 * routines are the image's.
 * With them, from other images' rewritten C, of its functions, those the
 * 58503B's code carries identical or relocated, with the static helpers
 * they use (docs/firmware/58503b-1.01.04.md, "The link list"); their
 * comments' addresses are the 58503B's, but where they name another
 * image. */

/* hmon_channels' entries, of which hmon_channel_read reads the last for a
 * channel past it: an array size, so #define. */
#define MONITORS_CHANNELS 8

/* A convert routine called with the image's calling convention: its
 * reading is a one-byte argument at the start of the stack slot, where
 * the image reads it.  gcc lays a byte argument in the low byte of its
 * four-byte slot, so a gcc caller of the image routine puts the reading
 * in the slot's first byte itself. */
typedef float hmon_convert_image(uint32_t reading_at_slot_start);

/* Reads an ADC channel, the last for one past it, through its entry's
 * convert routine; 0 for a channel without one (0x31544). */
float hmon_channel_read(adc_channel channel) {
  if (channel > MONITORS_CHANNELS - 1) {
    channel = MONITORS_CHANNELS - 1;
  }
  if (hmon_channels[channel].convert == 0) {
    return 0.0f;
  }
  return ((hmon_convert_image*)hmon_channels[channel].convert)(
      (uint32_t)(uint8_t)adc_read(channel) << 24);
}

/* A byte argument as either copy of hmon_channels' routines reads it, a
 * channel: the image's at the start of a 2-byte slot, gcc's at the end
 * of its 4-byte one.  The table holds the image's routines, which gcc's
 * copy of hmon_channel_step calls too. */
typedef struct monitors_byte_slot {
  uint8_t image;    /* +0x00: where the image's routines read it */
  uint8_t field_01; /* unread: the rest of the 2-byte slot */
  uint8_t field_02; /* unread */
  uint8_t gcc;      /* +0x03: where gcc's read it */
} monitors_byte_slot;

/* The health monitor's step: the channel hmon_channel_index names is
 * added to its monitor and, while its check says so or it has none,
 * reported; then the next channel, round robin (0x319a4).  The Z3816A
 * 4001's, relocated, through this image's table: the decompiler's
 * reading passes the table's routines a channel from no register it
 * set. */
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
  if (hmon_channel_index > MONITORS_CHANNELS - 1) {
    hmon_channel_index = ADC_ANTENNA_VOLTAGE; /* the first */
  }
}

/* :DIAGnostic:ADC?'s report: the monitored average of each health monitor
 * channel marked for it, by its name, through the printer its query
 * passes, the 58503A 3704's diag_pr_adc_avg over a printer (0x3ab96). */
void adc_averages_print(adc_average_printer* print) {
  static const uint32_t CHANNELS =
      sizeof(hmon_channels) / sizeof(hmon_channels[0]);

  for (uint32_t channel = 0; channel < CHANNELS; channel++) {
    if (hmon_channels[channel].field_28 != 0) {
      print("%s: %.2f\r\n", hmon_channels[channel].name,
            float_to_double(adc_average((adc_channel)channel)));
    }
  }
}

/* Counts up a period of ticks: true, and the count started again, as
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
 * counts its pass for the watchdog, toggles the heartbeat and reports
 * the watchdog, marks the GPS 1PPS dead while its check is off or its
 * reference is not held (gps_pps_dead_update), checks both 1PPS,
 * reporting each one's failure once it has lasted and its end, steps
 * the health monitor's channels, checks the EFC and writes the lamps,
 * blinking, and when asked (0x334a4).  The 58503A 3704's but for the
 * heartbeat, bit 3 of the latch at 0x300000 (0x3353e); the GPS 1PPS's
 * failure reported after 12289 checks (0x3358e), where the 58503A's
 * waits 513, and its dead checks neither counted nor reported while
 * gps_pps_reference_lost is set (0x33586).  Both 1PPS tests read one
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
        board_leds_change(LATCH_HEARTBEAT, 0);
      } else {
        board_leds_change(0, LATCH_HEARTBEAT);
      }
    }
    gps_pps_dead_update();
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
      } else if (gps_pps_reference_lost == 0) {
        if (gps_dead_checks < GPS_PPS_FAILED_CHECKS) {
          gps_dead_checks++;
        } else if (gps_pps_unconfirmed == 0 && !gps_failure) {
          status_happening(STATUS_GPS_PPS_FAILURE_SET);
          gps_failure = true;
        }
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
 * and services the watchdog while the grace lasts; then clears them
 * (0x32eaa).  The 58503A 3704's, relocated. */
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
 * has them (hmon_channel_entry), so that hmon_channel_read
 * and the monitor step call the C's routines, with its argument
 * layout.  MONITORS_NO_EVENT is an entry's "none", 0x16, which
 * hmon_report_tolerance compares with and does not raise (0x317c2,
 * 0x31826); 0x16 is STATUS_INPUT_OVERRUN's code. */
static const status_condition_code MONITORS_NO_EVENT = STATUS_INPUT_OVERRUN;

/* Starts every ADC monitor afresh, the Z3805A's monitors_reset's loop
 * (0x3159e). */
void adc_monitors_reset(void) {
  for (size_t index = 0; index < MONITORS_CHANNELS; index++) {
    adc_monitor* monitor = &adc_monitors[index];

    monitor->valid = 0;
    monitor->sample_index = 0;
    monitor->out_of_range_count = 0;
    monitor->out_of_tolerance = 0;
  }
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
 * in, keeps their mean (0x315fa).  The Z3805A's takes a monitor and a
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
 * into its monitor, the first taken as it is (0x31680). */
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
 * raised (0x31704). */
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
 * nothing calls it (0x3184a). */
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
 * (0x318a0). */
uint8_t hmon_channel_out_of_range(uint8_t channel) {
  return monitor_out_of_range(
      &adc_monitors[channel], hmon_channels[channel].minimum,
      hmon_channels[channel].maximum, MONITORS_RANGE_LIMIT);
}

/* The 5V, 15V and -15V entries' report routine: the channel out of
 * tolerance after a run out of its range (0x31930). */
void hmon_report_range(adc_channel channel) {
  hmon_report_tolerance(channel, hmon_channel_out_of_range(channel));
}

/* A check routine no entry names: whether the uptime passes the
 * channel's entry's field_0c (0x3197a). */
uint8_t hmon_channel_settled(uint8_t channel) {
  return get_uptime() > (uint32_t)hmon_channels[channel].field_0c;
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
 * it (0x32bea). */
uint8_t porte_read_steady(uint8_t mask) {
  return monitors_read_steady(&sim.porte0, mask, MONITORS_STEADY_AGREEING,
                              MONITORS_STEADY_READS);
}

/* PORTF0's bits of mask, ten reads in a row within 1000; nothing calls
 * it (0x32c34). */
uint8_t portf0_read_steady(uint8_t mask) {
  return monitors_read_steady(&sim.portf0, mask, MONITORS_STEADY_AGREEING,
                              MONITORS_STEADY_READS);
}

/* PORTGP's bits of mask, four reads in a row within 100; nothing calls
 * it (0x32c7e). */
uint8_t portgp_read_steady(uint8_t mask) {
  static const int32_t AGREEING = 4;
  static const int32_t READS = 100;

  return monitors_read_steady(&gpt.portgp, mask, AGREEING, READS);
}

/* Starts every ADC monitor afresh (0x336c4). */
void monitors_reset(void) { adc_monitors_reset(); }

/* The Primary oven current entry's check: 1 while the loop is in holdover
 * to normal, the stages 3 to 6, else 0 (0x3195e).  The image compares
 * pll.stage as a signed byte, which for an enum of 0 to 9 is the same. */
uint8_t hmon_check_pll_stage(adc_channel channel) {
  return pll.stage >= PLL_STAGE_HOLDOVER && pll.stage <= PLL_STAGE_NORMAL_PLL;
}

/* Clears the internal 1PPS monitor's state, internal_pps_disabled first
 * (0x33328). */
void internal_pps_monitor_reset(void) {
  internal_pps_disabled = 0;
  internal_pps_dead = 0;
  internal_pps_started = 0;
}

static const float MONITORS_OVEN_SCALE = 0.0743f;

static const float MONITORS_OVEN_5V_SCALE = -0.0472f;

static const float MONITORS_5V_SCALE = 0.0242f;

static const float MONITORS_15V_SCALE = 0.0763f;

static const float MONITORS_CURRENT_SCALE = 250.0f / 255.0f;

static const float MONITORS_MINUS_15V_SCALE = 0.0438f;

static const float MONITORS_MINUS_15V_5V_SCALE = -0.084f;

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

/* hmon_channels' 5V convert routine (0x31404). */
float hmon_convert_5v(uint8_t reading) {
  return monitors_scaled(reading, MONITORS_5V_SCALE);
}

/* hmon_convert_oven's instructions again, byte for byte, which no
 * table entry, call or pointer names (0x3141e). */
float hmon_convert_oven_unused(uint8_t reading) {
  return monitors_scaled_less_5v(reading, MONITORS_OVEN_SCALE,
                                 MONITORS_OVEN_5V_SCALE);
}

/* hmon_channels' 15V convert routine (0x314ae). */
float hmon_convert_15v(uint8_t reading) {
  return monitors_scaled(reading, MONITORS_15V_SCALE);
}

/* hmon_channels' Oven convert routine (0x31466). */
float hmon_convert_oven(uint8_t reading) {
  return monitors_scaled_less_5v(reading, MONITORS_OVEN_SCALE,
                                 MONITORS_OVEN_5V_SCALE);
}

/* hmon_channels' -15V convert routine (0x314c8). */
float hmon_convert_minus_15v(uint8_t reading) {
  return monitors_scaled_less_5v(reading, MONITORS_MINUS_15V_SCALE,
                                 MONITORS_MINUS_15V_5V_SCALE);
}

/* hmon_channels' Antenna current convert routine, entry 7's (0x3152a). */
float hmon_convert_antenna_current(uint8_t reading) {
  return monitors_scaled(reading, MONITORS_CURRENT_SCALE);
}

/* hmon_channels' Primary oven current convert routine (0x31510). */
float hmon_convert_oscillator_current(uint8_t reading) {
  return monitors_scaled(reading, MONITORS_CURRENT_SCALE);
}

/* Whether the 1PPS monitors may check: no self-test running and the
 * FPGA, which counts the internal 1PPS, loaded. */
static bool monitors_pps_checkable(void) {
  return !selftest.running && pll.fpga_loaded && !fpga_programming;
}

/* Checks the internal 1PPS by its count since the last check, from the
 * second call that may on, reporting it dead or back; returns whether
 * it checked (0x3333c).  While internal_pps_disabled is set every count
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

/* Clears both 1PPS monitors, the outer oven's flag, the health
 * monitor's step, :DIAGnostic:TMODe and the lamps' state (0x33690). */
void pps_monitors_reset(void) {
  gps_pps_monitor_reset();
  internal_pps_monitor_reset();
  outer_oven_enabled = 0;
  hmon_reset();
  diag_tmode_state = 0;
  diag_tmode_data = 0;
  lamp_bits = 0;
  lamp_blink = 0;
  lamps_requested = 0;
}

/* Clears the GPS 1PPS monitor's state, its skipped checks first, where
 * the Z3805A's leaves them (0x330bc). */
void gps_pps_monitor_reset(void) {
  gps_pps_skipped_checks = 0;
  gps_pps_unconfirmed = 0;
  gps_pps_reference_lost = 0;
  gps_pps_dead = 0;
  gps_pps_counting = 0;
}

/* Sets oven_warmup_seconds to 600 and clears the two floats before it,
 * none of which any instruction reads (0x315c8).  The Z3817A 3752's,
 * which sets its oven_warmup_seconds alone, to 300, which its
 * oscillator_ready reads. */
void oven_warmup_reset(void) {
  static const uint32_t WARM_UP_SECONDS = 600;

  oven_warmup_unread_1 = 0.0f;
  oven_warmup_unread_2 = 0.0f;
  oven_warmup_seconds = WARM_UP_SECONDS;
}

/* Clears hmon_unread_flag, starts the health monitor's step at its first
 * channel and the warm-up afresh (0x315e8), as the Z3817A 3752's. */
void hmon_reset(void) {
  hmon_unread_flag = 0;
  hmon_channel_index = ADC_ANTENNA_VOLTAGE;
  oven_warmup_reset();
}

/* Whether the oven reads below -2, the Primary oven current entry's
 * warm-up test (0x313da), the 58503A 3704's; the Z3815A's and the
 * Z3816A's test the oven current's average below 340. */
bool oven_warmed_up(void) {
  static const float OVEN_WARM = -2.0f;

  return float_compare(adc_average(ADC_OVEN), OVEN_WARM) < 0;
}

/* The Primary oven current entry's report routine: out of tolerance
 * until oven_warmed_up (0x31946), where the 58503A 3704's takes
 * double_oven_warmed_up. */
void hmon_report_oven_warm(adc_channel channel) {
  hmon_report_tolerance(channel, !oven_warmed_up());
}

/* A channel's monitored value; while it has none, its first sample once
 * one is in, else its entry's nominal value; a channel past the last is
 * the last (0x31a40).  The bases' take the nominal value until the
 * monitor is valid. */
float adc_average(adc_channel channel) {
  if (channel > ADC_ANTENNA_CURRENT) {
    channel = ADC_ANTENNA_CURRENT;
  }
  if (adc_monitors[channel].valid) {
    return adc_monitors[channel].value;
  }
  if (adc_monitors[channel].sample_index != 0) {
    return adc_monitors[channel].samples[0];
  }
  return hmon_channels[channel].nominal;
}

/* :DIAG:GPS:ACURrent's alarm while it is enabled: for a "B" receiver
 * (gps.field_608's SIGNAL_SS bit) by the antenna current's average, 1
 * above 80 and 0xff, -1 as a byte, below 5; for another by @@Ea's
 * antenna sense, 0xff for under current and 1 for over; else 0
 * (0x31aa6).  The Z3816A 4001's tests the average alone, against 10 and
 * 90.  0x3ac76 calls it. */
uint8_t hmon_check_antenna_current(void) {
  static const float ANTENNA_CURRENT_LEAST = 5.0f;
  static const float ANTENNA_CURRENT_MOST = 80.0f;
  static const uint8_t ANTENNA_CURRENT_LOW = 0xff;
  static const uint8_t ANTENNA_CURRENT_HIGH = 1;

  if (nv_settings.antenna_current_alarm == 0) {
    return 0;
  }
  if ((gps.field_608 & GPS_FIELD_608_SIGNAL_SS) != 0) {
    float average = adc_average(ADC_ANTENNA_CURRENT);

    if (float_compare(average, ANTENNA_CURRENT_MOST) > 0) {
      return ANTENNA_CURRENT_HIGH;
    }
    if (float_compare(ANTENNA_CURRENT_LEAST, average) > 0) {
      return ANTENNA_CURRENT_LOW;
    }
    return 0;
  }
  if (gps.antenna_sense == GPS_ANTENNA_NORMAL) {
    return 0;
  }
  return gps.antenna_sense == GPS_ANTENNA_UNDER_CURRENT ? ANTENNA_CURRENT_LOW
                                                        : ANTENNA_CURRENT_HIGH;
}

/* Sets the EFC, pll.efc alone (0x32cc6), where the Z3815A 4010's and the
 * Z3817A 3752's set the puck's EFC written and expected reading too. */
void efc_set(float efc) { pll.efc = efc; }

/* The uptime, in seconds, before which the EFC monitors report nothing
 * at a limit or near the end of the range (0x32d62, 0x32e62). */
static const uint32_t MONITORS_EFC_SETTLING_SECONDS = 600;

/* Clamps an EFC to 0 to 1048560, either end itself clamped, reporting
 * the first clamp after the first ten minutes of uptime, which it logs,
 * and the first value back in range (0x32d34).  The Z3817A 3752's but
 * for the uptime. */
float efc_clamp(float efc) {
  if (float_compare(0.0f, efc) >= 0) {
    efc = 0.0f;
  } else if (float_compare(efc, PLL_EFC_MOST) >= 0) {
    efc = PLL_EFC_MOST;
  } else {
    if (efc_range.clamped) {
      status_happening(STATUS_EFC_AT_LIMIT_CLEARED);
      efc_range.clamped = 0;
    }
    return efc;
  }
  if (!efc_range.clamped && get_uptime() > MONITORS_EFC_SETTLING_SECONDS) {
    status_happening(STATUS_EFC_AT_LIMIT_SET);
    log_write("EFC at limit");
    efc_range.clamped = 1;
  }
  return efc;
}

/* Reports the EFC near the end of its range, outside 5% to 95%, once six
 * checks in a row find it there after the first ten minutes of uptime,
 * and back inside at once (0x32df2): the loop's, pll.efc, or in a user
 * mode :DIAGnostic:ROSCillator:EFControl:DATA, which takes the count to
 * its last at once.  The Z3817A 3752's but for the uptime. */
void hmon_check_efc(void) {
  /* The near-end bounds the health monitor keeps the EFC's 20 bits
   * to, and the checks outside them it waits for. */
  static const float EFC_NEAR_LOW = 52428.0f;   /* 5% of 2^20 */
  static const float EFC_NEAR_HIGH = 996132.0f; /* 95% */
  /* The count past which it reports: the sixth check. */
  static const uint8_t NEAR_END_CHECKS = 5;
  float efc = pll.efc;

  if (diag_efc_mode != EFC_MODE_NORMAL) {
    efc = integer_to_float(scpi_value_DIAGnostic_ROSCillator_EFControl_DATA);
    efc_range.near_end_checks = NEAR_END_CHECKS;
  }
  if (float_compare(efc, EFC_NEAR_LOW) >= 0 &&
      float_compare(efc, EFC_NEAR_HIGH) <= 0) {
    efc_range.near_end_checks = 0;
    if (efc_range.near_end) {
      efc_range.near_end = 0;
      status_happening(STATUS_EFC_NEAR_LIMIT_CLEARED);
    }
    return;
  }
  if (++efc_range.near_end_checks > NEAR_END_CHECKS) {
    efc_range.near_end_checks = NEAR_END_CHECKS;
    if (!efc_range.near_end && get_uptime() > MONITORS_EFC_SETTLING_SECONDS) {
      task_debug_message("EFC near end of range\r\n");
      status_happening(STATUS_EFC_NEAR_LIMIT_SET);
      efc_range.near_end = 1;
    }
  }
}

/* Whether the GPS record takes its 1PPS for lost: gps.field_608's bit 3
 * clear and the GPS reference's state neither valid, clearing nor
 * assessing. */
static bool monitors_gps_pps_lost(void) {
  return (gps.field_608 & GPS_FIELD_608_BIT_3) == 0 &&
         gps.references[0].state != GPS_SYNC_VALID &&
         gps.references[0].state != GPS_SYNC_QUESTIONABLE_CLEARING &&
         gps.references[0].state != GPS_SYNC_ASSESSING;
}

/* Marks the GPS 1PPS dead while gps.pps_check_disabled, unconfirmed and
 * its skipped checks cleared, or while the GPS record takes it for lost
 * and gps_pps_reference_lost is not yet set; hmon_task's, each pass
 * (0x330dc), where the 58503A 3704's hmon_task marks it for the first
 * itself. */
void gps_pps_dead_update(void) {
  if (gps.pps_check_disabled != 0) {
    gps_pps_unconfirmed = 1;
    gps_pps_skipped_checks = 0;
  } else if (gps_pps_reference_lost != 0 || !monitors_gps_pps_lost()) {
    return;
  }
  gps_pps_dead = 1;
}

/* Checks the GPS 1PPS by the pulses the GPT's pulse accumulator has
 * counted (PACTL's PAEN and PEDGE, rising edges, MC68331UM D.2.7),
 * reporting it dead, at the wrong frequency, or back; returns 0 on a
 * pass that could not check, else 1 (0x3312a).  The Z3805A 3543B's, but
 * that while the GPS record takes the 1PPS for lost it marks it dead and
 * gps_pps_reference_lost, once, and starts the counts again; a skipped
 * check clears the count; and a good short gate while the GPS reference
 * is questionable brings it back at once, and moves the gate to 3, so
 * that it never meets the long gate's 10 but passes it, which ends the
 * gate unchecked (0x3327a). */
uint8_t gps_pps_check(void) {
  static const uint8_t PACTL_COUNT_RISING = 0x50;
  static const uint8_t PPS_GATE_STEP = 2;
  static const uint8_t PPS_GATE = 10;
  static const uint8_t PPS_GATE_LEAST = 8;
  static const uint8_t PPS_GATE_MOST = 12;
  static const uint8_t PPS_SKIPS = 15;
  /* The gate a good short gate while questionable leaves. */
  static const uint8_t PPS_GATE_QUESTIONABLE = 3;
  uint8_t dead = gps_pps_dead;
  uint8_t checked = 1;
  bool short_gate_bad = false;

  if (gps.pps_check_disabled != 0) {
    gps_pps_unconfirmed = 1;
    gps_pps_skipped_checks = 0;
    dead = 1;
    gpt.pacnt = 0;
    gps_pps_gate = 0;
    gps_pps_last_count = 0;
  } else if (gps_pps_reference_lost == 0 && monitors_gps_pps_lost()) {
    gps_pps_reference_lost = 1;
    dead = 1;
    gpt.pacnt = 0;
    gps_pps_gate = 0;
    gps_pps_last_count = 0;
  } else if (gps_pps_unconfirmed != 0 && gps_pps_skipped_checks < PPS_SKIPS) {
    gps_pps_skipped_checks++;
    gpt.pacnt = 0;
  } else if (!monitors_pps_checkable()) {
    gpt.pacnt = 0;
    gps_pps_gate = 0;
    gps_pps_last_count = 0;
    checked = 0;
  } else if (gps_pps_counting == 0) {
    gpt.pacnt = 0;
    gpt.pactl = PACTL_COUNT_RISING;
    gps_pps_counting = 1;
    gps_pps_gate = 0;
    gps_pps_last_count = 0;
    checked = 0;
  } else {
    const uint8_t pulses = gpt.pacnt;
    const int8_t since_last = pulses - gps_pps_last_count;

    if (since_last < MONITORS_PPS_LEAST || since_last > MONITORS_PPS_MOST) {
      short_gate_bad = true;
      if (!dead) {
        task_dmessage("GPS 1pps signal is dead\r\n");
        debug_printf("short gate, count %d\r\n", since_last);
        dead = 1;
      }
    } else if ((gps.field_608 & GPS_FIELD_608_BIT_3) == 0 &&
               gps.references[0].valid == GPS_REFERENCE_QUESTIONABLE) {
      if (dead) {
        task_dmessage("GPS 1pps signal is ok\r\n");
        dead = 0;
      }
      gps_pps_unconfirmed = 0;
      gps_pps_reference_lost = 0;
      gps_pps_gate = PPS_GATE_QUESTIONABLE;
    }
    gps_pps_last_count = pulses;
    gps_pps_gate += PPS_GATE_STEP;
    if (gps_pps_gate == PPS_GATE) {
      gpt.pacnt = 0;
      if (pulses < PPS_GATE_LEAST || pulses > PPS_GATE_MOST) {
        if (!dead) {
          task_dmessage("GPS 1pps signal frequency is incorrect\r\n");
          debug_printf("long gate, count %d\r\n", (uint32_t)pulses);
          dead = 1;
        }
      } else if (!short_gate_bad) {
        if (dead) {
          task_dmessage("GPS 1pps signal is ok\r\n");
          dead = 0;
        }
        gps_pps_unconfirmed = 0;
        gps_pps_reference_lost = 0;
      }
      gps_pps_last_count = 0;
      gps_pps_gate = 0;
    } else if (gps_pps_gate > PPS_GATE) {
      gpt.pacnt = 0;
      gps_pps_last_count = 0;
      gps_pps_gate = 0;
    }
  }
  gps_pps_dead = dead;
  return checked;
}

/* Waits for the 1PPS tick's event, which the periodic interrupt signals,
 * or 10240 ticks (0x4a0dc): interval_next_reading's wait for a dead
 * 1PPS, which the other images make in their reading routine itself.
 * The seeding paired it, as a lead, with the Z3815A's backp task. */
void pps_tick_wait(void) {
  static const uint32_t PPS_TICK_EVENT = 0x200;
  static const uint32_t PPS_TICK_TIMEOUT = 0x2800;

  psos_wait_v(PPS_TICK_EVENT, 0, PPS_TICK_TIMEOUT, 0);
}
