/* The health monitor and the PPS monitors of the 58503A 3704, written by
 * hand in place of the decompiler's (docs/firmware/README.md,
 * "Hand-written C"; docs/firmware/modules.md, `services/monitors`).  The
 * exported C includes this file after its own, so the types and
 * prototypes it declares are in scope; each function here keeps its
 * generated prototype.  It is the health monitor's channel table and
 * its read, the oven current's check, step and tolerance report, the PPS
 * monitors' resets and the health monitor's task.  The monitors' state is
 * docs/firmware/structures.md's "The health monitor's supplies and EFC"
 * and "The health monitor's ADC monitors"; the table is
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
