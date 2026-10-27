/* The health monitor and the PPS monitors of the 58503A 3704, written by
 * hand in place of the decompiler's (docs/firmware/README.md,
 * "Hand-written C"; docs/firmware/modules.md, `services/monitors`).  The
 * exported C includes this file after its own, so the types and
 * prototypes it declares are in scope; each function here keeps its
 * generated prototype.  It is the health monitor's channel table, its
 * channels' convert, add, check and report routines, its read, step and
 * averages, the ovens' warm-up, the EFC's clamp and check, the PPS
 * monitors' checks and resets, and the health monitor's task.  The monitors'
 * state is docs/firmware/structures.md's "The health monitor's supplies and
 * EFC" and "The health monitor's ADC monitors"; the table is
 * docs/firmware/58503a-3704.md's "The health monitor by table". */

/* The 58503A 3704's health monitor channels, by channel, as the image
 * has them at 0x2238a (hmon_channel_entry), so that hmon_channel_read
 * and the monitor step call the C's routines, with its argument
 * layout.  MONITORS_NO_EVENT is an entry's "none", 0x16, which
 * hmon_report_tolerance compares with and does not raise (0x22026,
 * 0x2208a); 0x16 is STATUS_INPUT_OVERRUN's code. */
static const status_condition_code MONITORS_NO_EVENT = STATUS_INPUT_OVERRUN;

/* hmon_channels' entries, of which hmon_channel_read reads the last for
 * a channel past it: an array size, so #define. */
#define MONITORS_CHANNELS 8

/* Even, as the image's is, since its words and longs at an odd address
 * are misaligned (MC68331UM, 4.4.4) and the type is packed. */
extern const hmon_channel_entry
    __attribute__((aligned(2))) hmon_channels[MONITORS_CHANNELS];

const hmon_channel_entry hmon_channels[MONITORS_CHANNELS] = {
    {.convert = hmon_convert_temperature,
     .add = monitor_average_add,
     .nominal = 25.0f,
     .name = "Temperature",
     .events = {MONITORS_NO_EVENT, MONITORS_NO_EVENT},
     .field_28 = 1},
    {.convert = hmon_convert_5v,
     .add = monitor_average_add,
     .report = hmon_report_range,
     .minimum = MONITORS_PLUS_5V_LEAST,
     .maximum = MONITORS_PLUS_5V_MOST,
     .nominal = 5.0f,
     .name = "5V",
     .events = {STATUS_PLUS_5V_SET, STATUS_PLUS_5V_CLEARED},
     .bit = SELFTEST_POWER_5V,
     .field_28 = 1},
    {.name = "", .events = {MONITORS_NO_EVENT, MONITORS_NO_EVENT}},
    {.convert = hmon_convert_oven,
     .add = monitor_average_add,
     .maximum = -2.0f,
     .nominal = 4.0f,
     .name = "Oven",
     .events = {MONITORS_NO_EVENT, MONITORS_NO_EVENT},
     .field_28 = 1},
    {.convert = hmon_convert_15v,
     .add = monitor_average_add,
     .report = hmon_report_range,
     .minimum = MONITORS_PLUS_15V_LEAST,
     .maximum = MONITORS_PLUS_15V_MOST,
     .nominal = 15.0f,
     .name = "15V",
     .events = {STATUS_PLUS_15V_SET, STATUS_PLUS_15V_CLEARED},
     .bit = SELFTEST_POWER_P15V,
     .field_28 = 1},
    {.convert = hmon_convert_minus_15v,
     .add = monitor_average_add,
     .report = hmon_report_range,
     .minimum = MONITORS_MINUS_15V_LEAST,
     .maximum = MONITORS_MINUS_15V_MOST,
     .nominal = -15.0f,
     .name = "-15V",
     .events = {STATUS_MINUS_15V_SET, STATUS_MINUS_15V_CLEARED},
     .bit = SELFTEST_POWER_M15V,
     .field_28 = 1},
    {.convert = hmon_convert_oven_current,
     .add = monitor_filter_add_58503a,
     .check = hmon_check_pll_stage,
     .report = hmon_report_oven_warm,
     .nominal = 250.0f,
     .name = "Primary oven current",
     .events = {STATUS_OVEN_SUPPLY_SET, STATUS_OVEN_SUPPLY_CLEARED},
     .field_28 = 1},
    {.convert = hmon_convert_antenna_current,
     .add = monitor_average_add,
     .nominal = 50.0f,
     .name = "Antenna current",
     .events = {MONITORS_NO_EVENT, MONITORS_NO_EVENT}},
};

/* The convert routines' scales, the volts, degrees or milliamperes of
 * an ADC count, and the 5V channel's share the oven's and -15V's take
 * off theirs. */
static const float MONITORS_TEMPERATURE_SCALE = 0.273f;

static const float MONITORS_5V_SCALE = 0.0242f;

static const float MONITORS_OVEN_SCALE = 0.0743f;

static const float MONITORS_OVEN_5V_SCALE = -0.0472f;

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

/* hmon_channels' Temperature convert routine (0x21b48). */
float hmon_convert_temperature(uint8_t reading) {
  return monitors_scaled(reading, MONITORS_TEMPERATURE_SCALE);
}

/* hmon_channels' 5V convert routine (0x21b62). */
float hmon_convert_5v(uint8_t reading) {
  return monitors_scaled(reading, MONITORS_5V_SCALE);
}

/* hmon_channels' Oven convert routine (0x21bc4); 0x21b7c, which
 * nothing names, is its copy. */
float hmon_convert_oven(uint8_t reading) {
  return monitors_scaled_less_5v(reading, MONITORS_OVEN_SCALE,
                                 MONITORS_OVEN_5V_SCALE);
}

/* hmon_channels' 15V convert routine (0x21c0c). */
float hmon_convert_15v(uint8_t reading) {
  return monitors_scaled(reading, MONITORS_15V_SCALE);
}

/* hmon_channels' -15V convert routine (0x21c26). */
float hmon_convert_minus_15v(uint8_t reading) {
  return monitors_scaled_less_5v(reading, MONITORS_MINUS_15V_SCALE,
                                 MONITORS_MINUS_15V_5V_SCALE);
}

/* hmon_channels' Primary oven current convert routine (0x21c6e). */
float hmon_convert_oven_current(uint8_t reading) {
  return monitors_scaled(reading, MONITORS_CURRENT_SCALE);
}

/* hmon_channels' Antenna current convert routine (0x21c88). */
float hmon_convert_antenna_current(uint8_t reading) {
  return monitors_scaled(reading, MONITORS_CURRENT_SCALE);
}

/* Starts every ADC monitor afresh, the Z3805A's monitors_reset's loop
 * (0x21cfc). */
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
static uint8_t* const MONITORS_UNREAD_FLAG = (uint8_t*)0x101e6e;

/* Clears MONITORS_UNREAD_FLAG and starts the health monitor's step at its
 * first channel (0x21d26). */
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
 * in, keeps their mean (0x21d34).  The Z3805A's takes a monitor and a
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
 * into its monitor, the first taken as it is (0x21dba). */
void monitor_filter_add_58503a(adc_channel channel) {
  adc_monitor* monitor = &adc_monitors[channel];

  if (!monitor->valid) {
    monitor->valid = 1;
    monitor->value = hmon_channel_read(channel);
    return;
  }
  monitors_filter(monitor, hmon_channel_read(channel));
}

/* An add routine no entry names and nothing calls (revisions.md): filters
 * the channel's reading in as monitor_filter_add_58503a does, the first
 * kept in samples[0], and every 75th reading keeps the change since in
 * samples[1] and the value in samples[0], and sets samples[2] to 1, but
 * in the first 600 seconds only when the change passes the entry's
 * minimum and the value is below its maximum (0x21e3e). */
void hmon_smoothed_add(adc_channel channel) {
  static const uint8_t READINGS = 75;
  static const uint32_t SETTLING_SECONDS = 601;
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

/* A report routine's message's size. */
#define MONITORS_MESSAGE_SIZE 80

/* Reports a channel out of tolerance, logged, printed and its entry's
 * first happening, or back within it, printed and its second, each once
 * by its monitor's out_of_tolerance; an entry's MONITORS_NO_EVENT is not
 * raised (0x21f68). */
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
 * nothing calls it (0x220ae). */
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
 * (0x22104). */
uint8_t hmon_channel_out_of_range(uint8_t channel) {
  return monitor_out_of_range(
      &adc_monitors[channel], hmon_channels[channel].minimum,
      hmon_channels[channel].maximum, MONITORS_RANGE_LIMIT);
}

/* The 5V, 15V and -15V entries' report routine: the channel out of
 * tolerance after a run out of its range (0x22194). */
void hmon_report_range(adc_channel channel) {
  hmon_report_tolerance(channel, hmon_channel_out_of_range(channel));
}

/* The Primary oven current entry's report routine: out of tolerance
 * until double_oven_warmed_up (0x221aa). */
void hmon_report_oven_warm(adc_channel channel) {
  hmon_report_tolerance(channel, !double_oven_warmed_up());
}

/* A check routine no entry names: whether the uptime passes the
 * channel's entry's field_0c (0x221e0). */
uint8_t hmon_channel_settled(uint8_t channel) {
  return get_uptime() > (uint32_t)hmon_channels[channel].field_0c;
}

/* A channel's monitored value, or its entry's nominal value while it has
 * none; a channel past the last is the last (0x222a6). */
float adc_average(adc_channel channel) {
  if (channel > ADC_ANTENNA_CURRENT) {
    channel = ADC_ANTENNA_CURRENT;
  }
  if (!adc_monitors[channel].valid) {
    return hmon_channels[channel].nominal;
  }
  return adc_monitors[channel].value;
}

/* 1 while :DIAG:GPS:ACURrent's alarm is enabled and the antenna current
 * is outside 10 to 200; nothing calls it (revisions.md, 0x222f2). */
uint8_t hmon_check_antenna_current(void) {
  static const float ANTENNA_CURRENT_LEAST = 10.0f;
  static const float ANTENNA_CURRENT_MOST = 200.0f;
  float average = adc_average(ADC_ANTENNA_CURRENT);

  return nv_settings.antenna_current_alarm != 0 &&
         (float_compare(average, ANTENNA_CURRENT_LEAST) < 0 ||
          float_compare(average, ANTENNA_CURRENT_MOST) > 0);
}

/* The oven's warm-up threshold, in the reading's volts. */
static const float MONITORS_OVEN_WARM = -2.0f;

/* Whether the oven reads below -2, powerup's warm-up test (0x2233a). */
bool oven_warmed_up(void) {
  return float_compare(adc_average(ADC_OVEN), MONITORS_OVEN_WARM) < 0;
}

/* oven_warmed_up's test again: the 58503A's reads the oven, not the
 * Z3805A's double oven (0x22362). */
bool double_oven_warmed_up(void) {
  return float_compare(adc_average(ADC_OVEN), MONITORS_OVEN_WARM) < 0;
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

/* PORTF0's bits of mask, ten reads in a row within 1000; nothing calls
 * it (0x234ae). */
uint8_t portf0_read_steady(uint8_t mask) {
  static const int32_t AGREEING = 10;
  static const int32_t READS = 1000;

  return monitors_read_steady(&sim.portf0, mask, AGREEING, READS);
}

/* PORTGP's bits of mask, four reads in a row within 100; nothing calls
 * it (0x234f8). */
uint8_t portgp_read_steady(uint8_t mask) {
  static const int32_t AGREEING = 4;
  static const int32_t READS = 100;

  return monitors_read_steady(&gpt.portgp, mask, AGREEING, READS);
}

/* Clamps an EFC to 0 to 1048560, reporting each clamp, and the first
 * value back in range (0x23592). */
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
 * (0x23614). */
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

/* Whether the 1PPS monitors may check: no self-test running and the
 * FPGA, which counts the internal 1PPS, loaded. */
static bool monitors_pps_checkable(void) {
  return !selftest.running && pll.fpga_loaded && !fpga_programming;
}

/* Checks the GPS 1PPS by the pulses the GPT has counted, reporting it
 * dead, at the wrong frequency, or back; returns 0 when it could not
 * check (0x238d0).  A gate found good clears gps_pps_unset_flag too. */
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
      gps_pps_unset_flag = 0;
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

/* Checks the internal 1PPS by its count since the last check, from the
 * second call that may on, reporting it dead or back; returns whether
 * it checked (0x23a50).  While internal_pps_disabled is set every count
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
 * monitor's step, :DIAGnostic:TMODe and the lamps' state (0x23d8e). */
void pps_monitors_reset_58503a(void) {
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

/* Starts every ADC monitor afresh (0x23dc2). */
void monitors_reset(void) { adc_monitors_reset(); }

/* Reads an ADC channel, the last for one past it, through its entry's
 * convert routine; 0 for a channel without one (0x21ca2). */
float hmon_channel_read(adc_channel channel) {
  if (channel > MONITORS_CHANNELS - 1) {
    channel = MONITORS_CHANNELS - 1;
  }
  if (hmon_channels[channel].convert == 0) {
    return 0.0f;
  }
  return hmon_channels[channel].convert((uint8_t)adc_read(channel));
}

/* The Primary oven current entry's check: 1 while the loop is in holdover
 * to normal, the stages 3 to 6, else 0 (0x221c4).  The image compares
 * pll.stage as a signed byte, which for an enum of 0 to 9 is the same. */
uint8_t hmon_check_pll_stage(adc_channel channel) {
  return pll.stage >= PLL_STAGE_HOLDOVER && pll.stage <= PLL_STAGE_NORMAL_PLL;
}

/* The health monitor's step: the channel hmon_channel_index names is
 * added to its monitor and, while its check says so or it has none,
 * reported; then the next channel, round robin (0x2220a). */
void hmon_channel_step(void) {
  if (hmon_channels[hmon_channel_index].add != 0) {
    hmon_channels[hmon_channel_index].add(hmon_channel_index);
  }
  if (hmon_channels[hmon_channel_index].report != 0) {
    uint8_t due =
        hmon_channels[hmon_channel_index].check == 0
            ? 1
            : hmon_channels[hmon_channel_index].check(hmon_channel_index);

    if (due != 0) {
      hmon_channels[hmon_channel_index].report(hmon_channel_index);
    }
  }
  hmon_channel_index++;
  if (hmon_channel_index > MONITORS_CHANNELS - 1) {
    hmon_channel_index = ADC_TEMPERATURE; /* the first */
  }
}

/* Clears the GPS 1PPS monitor's state, its skipped checks first, where
 * the Z3805A's leaves them (0x238b0). */
void gps_pps_monitor_reset(void) {
  gps_pps_skipped_checks = 0;
  gps_pps_unconfirmed = 0;
  gps_pps_unset_flag = 0;
  gps_pps_dead = 0;
  gps_pps_counting = 0;
}

/* Clears the internal 1PPS monitor's state, internal_pps_disabled first
 * (0x23a3c). */
void internal_pps_monitor_reset(void) {
  internal_pps_disabled = 0;
  internal_pps_dead = 0;
  internal_pps_started = 0;
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
 * (0x23b88).  The GPS 1PPS's failure is the GPS reference's, while its
 * record is valid (gps.references[0].valid 1), and is withheld while
 * gps_pps_unset_flag, which nothing sets, is.  Both 1PPS tests read one
 * result, D5 in the image: the last check's of either, and before any
 * its caller's (docs/firmware/quirks.md). */
void hmon_task(void) {
  static const uint32_t TICK_EVENT = 1;
  static const uint16_t HEARTBEAT_TICKS = 112;
  static const uint16_t PPS_CHECK_TICKS = 204;
  /* Checks a dead 1PPS lasts before its failure is reported. */
  static const uint16_t GPS_PPS_FAILED_CHECKS = 513;
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
        board_leds_change(BOARD_LED_HEARTBEAT, 0);
      } else {
        board_leds_change(0, BOARD_LED_HEARTBEAT);
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
      } else if (gps_pps_unconfirmed == 0 && gps_pps_unset_flag == 0 &&
                 !gps_failure) {
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
