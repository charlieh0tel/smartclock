/* From the Z3815A 4010's rewritten/monitors.c, the Z3817A 3752's own beside
 * common/'s: hmon_channel_read, the same code in both images, whose indirect
 * convert call the decompiler drops; and the 58503A 3704's hmon_channel_step,
 * whose channel the decompiler took from a register its caller left, and
 * watchdog_report, which enters supervisor mode and restores the status
 * register as the decompiler cannot (docs/firmware/z3817a-3752.md, "The
 * function check").  hmon_channels (0x32498) stays the image's ROM data, typed
 * through names.tsv, as in the Z3815A.  And this image's own, read beside the
 * Z3815A's and the 58503A's: hmon_task, the EFC's setting, clamp and check, the
 * oscillator's frames on DUART channel B and the EFC correction drawn from
 * them, and the reference pulse's monitor, which counts it as a 1PPS and as a
 * PP2S (docs/firmware/z3817a-3752.md, "The pulse", "DUART channel B: the
 * oscillator").  Its comments' addresses are the Z3817A's.  Each keeps its
 * generated prototype. */

/* hmon_channels' entries, of which hmon_channel_read reads the last for a
 * channel past it: an array size, so #define. */
#define MONITORS_CHANNELS 8

/* A convert routine called with the image's calling convention: its
 * reading is a one-byte argument at the start of the stack slot (offset
 * 4), where the image reads it.  gcc lays a byte argument in the low byte
 * of its four-byte slot, so a gcc caller of the image routine must put the
 * reading in the slot's first byte itself.  Because hmon_channels stays
 * the image's ROM data (above), its convert pointer is the image routine,
 * not a gcc copy, so hmon_channel_read makes the call this way rather
 * than through the table's own hmon_convert_function type. */
typedef float hmon_convert_image(uint32_t reading_at_slot_start);

/* The same for an entry's add, check and report routines, which take the
 * channel so. */
typedef uint8_t hmon_channel_image(uint32_t channel_at_slot_start);

/* A byte argument at the start of its slot, as the image passes it. */
static uint32_t monitors_slot_start(adc_channel channel) {
  return (uint32_t)(uint8_t)channel << 24;
}

/* Reads an ADC channel, the last for one past it, through its entry's
 * convert routine; 0 for a channel without one (0x31f4e).  The decompiler
 * cannot reconstruct the indirect convert call, which reads adc_read's
 * byte and passes it to the entry's convert on the stack (0x31f78 to
 * 0x31f98), so its reading drops the call and returns 0. */
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

/* The health monitor's step: the channel hmon_channel_index names is
 * added to its monitor and, while its check says so or it has none,
 * reported; then the next channel, round robin, each entry read afresh
 * by the index as the image does (0x3239a). */
void hmon_channel_step(void) {
  if (hmon_channels[hmon_channel_index].add != 0) {
    ((hmon_channel_image*)hmon_channels[hmon_channel_index].add)(
        monitors_slot_start(hmon_channel_index));
  }
  if (hmon_channels[hmon_channel_index].report != 0) {
    const uint8_t due =
        hmon_channels[hmon_channel_index].check == 0
            ? 1
            : ((hmon_channel_image*)hmon_channels[hmon_channel_index].check)(
                  monitors_slot_start(hmon_channel_index));

    if (due != 0) {
      ((hmon_channel_image*)hmon_channels[hmon_channel_index].report)(
          monitors_slot_start(hmon_channel_index));
    }
  }
  hmon_channel_index++;
  if (hmon_channel_index > MONITORS_CHANNELS - 1) {
    hmon_channel_index = ADC_CHANNEL_0;
  }
}

/* Checks the tasks' watchdog counts, printing them if one is short,
 * and services the watchdog while the grace lasts; then clears them
 * (0x33a04).  The 58503A 3704's, but that with the pulse a PP2S the
 * loop's task, which runs a block of two seconds, may miss a report
 * that follows one it passed. */
void watchdog_report(void) {
  static const uint8_t WATCHDOG_GPS_LEAST = 21;
  static const uint8_t WATCHDOG_MONITOR_LEAST = 51;
  const bool pll_passed =
      watchdog_pll != 0 ||
      (watchdog_pll_last != 0 && pll.pulse_mode != REFERENCE_PULSE_1PPS);

  if (watchdog_clock != 0 && watchdog_gps >= WATCHDOG_GPS_LEAST &&
      watchdog_monitor >= WATCHDOG_MONITOR_LEAST && watchdog_spool != 0 &&
      pll_passed) {
    watchdog_countdown = MONITORS_WATCHDOG_GRACE;
  } else {
    debug_printf(
        "watchdog: clock: %4d gps: %4d pll: %4d mon: %4d spool %4d\r\n",
        watchdog_clock, watchdog_gps, watchdog_pll, watchdog_monitor,
        watchdog_spool);
  }
  watchdog_pll_last = watchdog_pll != 0;
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

/* Clears the ADC monitors, which system_init calls as the 58503A 3704's
 * does (0x345c0), the 58503A's. */
void monitors_reset(void) { adc_monitors_reset(); }

/* MONITORS_NO_EVENT is a health monitor channel entry's "none", 0x16, which
 * hmon_report_tolerance compares with and does not raise (0x321b8,
 * 0x3221c); 0x16 is STATUS_INPUT_OVERRUN's code. */
static const status_condition_code MONITORS_NO_EVENT = STATUS_INPUT_OVERRUN;

/* Starts every ADC monitor afresh, the Z3805A's monitors_reset's loop
 * (0x31fa8). */
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

/* A check routine no entry names here, the Z3815A's Primary oven current
 * entry's: 1 while the loop is in holdover to normal, the stages 3 to 6,
 * else 0 (0x32354).  The image compares
 * pll.stage as a signed byte, which for an enum of 0 to 9 is the same. */
uint8_t hmon_check_pll_stage(adc_channel channel) {
  return pll.stage >= PLL_STAGE_HOLDOVER && pll.stage <= PLL_STAGE_NORMAL_PLL;
}

/* A monitor's value with a reading filtered in. */
static void monitors_filter(adc_monitor* monitor, float reading) {
  monitor->value =
      float_add(float_multiply(monitor->value, MONITORS_FILTER_KEEP),
                float_multiply(reading, MONITORS_FILTER_TAKE));
  if (float_compare(MONITORS_FILTER_FLOOR, monitor->value) > 0) {
    monitor->value = 0.0f;
  }
}

/* hmon_channels' add routine, every entry's here: puts the channel's
 * reading in its monitor's ring of ten and, once ten are
 * in, keeps their mean (0x31ff0).  The Z3805A's takes a monitor and a
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

/* An add routine no entry names here, the Z3815A's Primary oven
 * current's: filters the channel's reading into its monitor, the first
 * taken as it is (0x32076). */
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
 * raised (0x320fa). */
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
 * nothing calls it (0x32240). */
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
 * (0x32296). */
uint8_t hmon_channel_out_of_range(uint8_t channel) {
  return monitor_out_of_range(
      &adc_monitors[channel], hmon_channels[channel].minimum,
      hmon_channels[channel].maximum, MONITORS_RANGE_LIMIT);
}

/* The five entries' report routine: the channel out of tolerance after a
 * run out of its range (0x32326). */
void hmon_report_range(adc_channel channel) {
  hmon_report_tolerance(channel, hmon_channel_out_of_range(channel));
}

/* A check routine no entry names: whether the uptime passes the
 * channel's entry's field_0c (0x32370). */
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

/* PORTE0's bits of mask, ten reads in a row within 1000; its one caller,
 * gps_alternate_status, reads PE5, the reference's VALID (0x51d68;
 * docs/firmware/z3817a-3752.md, "The valid line: PE5") (0x33670). */
uint8_t porte_read_steady(uint8_t mask) {
  return monitors_read_steady(&sim.porte0, mask, MONITORS_STEADY_AGREEING,
                              MONITORS_STEADY_READS);
}

/* PORTF0's bits of mask, ten reads in a row within 1000; nothing calls
 * it (0x336ba). */
uint8_t portf0_read_steady(uint8_t mask) {
  return monitors_read_steady(&sim.portf0, mask, MONITORS_STEADY_AGREEING,
                              MONITORS_STEADY_READS);
}

/* PORTGP's bits of mask, four reads in a row within 100; nothing calls
 * it (0x33704). */
uint8_t portgp_read_steady(uint8_t mask) {
  static const int32_t AGREEING = 4;
  static const int32_t READS = 100;

  return monitors_read_steady(&gpt.portgp, mask, AGREEING, READS);
}

/* Clears the internal 1PPS monitor's state, internal_pps_disabled first
 * (0x34148). */
void internal_pps_monitor_reset(void) {
  internal_pps_disabled = 0;
  internal_pps_dead = 0;
  internal_pps_started = 0;
}

/* The supplies' scales, by the convert routines' constants
 * (docs/firmware/z3817a-3752.md, "The health monitor's channels"): the
 * +15V's serves the 12B channel too, and the -15V's and the 5V
 * channel's part of it the -12B's. */
static const float MONITORS_5V_SCALE = 0.0242f;

static const float MONITORS_15V_SCALE = 0.0763f;

static const float MONITORS_MINUS_15V_SCALE = 0.0438f;

static const float MONITORS_MINUS_15V_5V_SCALE = -0.084f;

/* The Z3815A's antenna current's scale, in no channel's entry here. */
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

/* hmon_channels' 5V convert routine, channel 1's (0x31e56). */
float hmon_convert_5v(uint8_t reading) {
  return monitors_scaled(reading, MONITORS_5V_SCALE);
}

/* hmon_channels' 12B convert routine, channel 4's (0x31e70). */
float hmon_convert_12b(uint8_t reading) {
  return monitors_scaled(reading, MONITORS_15V_SCALE);
}

/* hmon_channels' -12B convert routine, channel 5's (0x31e8a). */
float hmon_convert_minus_12b(uint8_t reading) {
  return monitors_scaled_less_5v(reading, MONITORS_MINUS_15V_SCALE,
                                 MONITORS_MINUS_15V_5V_SCALE);
}

/* hmon_channels' 15V convert routine, channel 2's (0x31ed2). */
float hmon_convert_15v(uint8_t reading) {
  return monitors_scaled(reading, MONITORS_15V_SCALE);
}

/* hmon_channels' -15V convert routine, channel 3's (0x31eec). */
float hmon_convert_minus_15v(uint8_t reading) {
  return monitors_scaled_less_5v(reading, MONITORS_MINUS_15V_SCALE,
                                 MONITORS_MINUS_15V_5V_SCALE);
}

/* The Z3815A's antenna current convert routine, which no channel's entry
 * names here (0x31f34). */
float hmon_convert_antenna_current(uint8_t reading) {
  return monitors_scaled(reading, MONITORS_CURRENT_SCALE);
}

/* A channel's monitored value, or its entry's nominal value while it has
 * none; a channel past the last is the last (0x32436). */
float adc_average(adc_channel channel) {
  if (channel > ADC_CHANNEL_7) {
    channel = ADC_CHANNEL_7;
  }
  if (!adc_monitors[channel].valid) {
    return hmon_channels[channel].nominal;
  }
  return adc_monitors[channel].value;
}

/* Whether the 1PPS monitors may check: no self-test running and the
 * FPGA, which counts the internal 1PPS, loaded. */
static bool monitors_pps_checkable(void) {
  return !selftest.running && pll.fpga_loaded && !fpga_programming;
}

/* Checks the internal 1PPS by its count since the last check, from the
 * second call that may on, reporting it dead or back; returns whether
 * it checked (0x3415c).  While internal_pps_disabled is set every count
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

/* Turns the puck's EFC corrections on or off, puck.correction_restart
 * (0x337f2): the pForth word efc_comp's (0x2b10a) and
 * efc_calibration_measure's (0x4dacc), the bytes of the Z3815A's
 * s3_test_mode_set. */
void s3_test_mode_set(uint8_t on) { puck.correction_restart = on; }

/* Sets the warm-up time oscillator_ready waits for, an oscillator of a
 * kind other than 0, to 300 s of uptime (0x31fd2). */
void oven_warmup_reset(void) {
  static const uint32_t WARM_UP_SECONDS = 300;

  oven_warmup_seconds = WARM_UP_SECONDS;
}

/* Clears hmon_unread_flag, starts the health monitor's step at its first
 * channel and the warm-up time afresh (0x31fde). */
void hmon_reset(void) {
  hmon_unread_flag = 0;
  hmon_channel_index = ADC_CHANNEL_0;
  oven_warmup_reset();
}

/* Whether the oscillator is ready: of kind 0, while its frame reports
 * ready and its link is alive (puck_warm); of any other kind, once the
 * uptime is past oven_warmup_seconds (0x31e16). */
uint8_t oscillator_ready(void) {
  if (oscillator_kind == 0) {
    return puck_warm() != 0;
  }
  return get_uptime() > oven_warmup_seconds;
}

/* oscillator_ready as a bool, hmon_report_oven_warm's test, as the
 * 58503A's tests its double oven (0x31e44). */
bool double_oven_warmed_up(void) { return oscillator_ready() != 0; }

/* oscillator_ready as a bool, powerup's warm-up test, where the Z3815A's
 * tests the oven current's average (0x32486). */
bool oven_warmed_up(void) { return oscillator_ready() != 0; }

/* A report routine no entry names here, the Z3815A's Primary oven
 * current entry's: out of tolerance until double_oven_warmed_up
 * (0x3233c). */
void hmon_report_oven_warm(adc_channel channel) {
  hmon_report_tolerance(channel, !double_oven_warmed_up());
}

/* Whether the oscillator's last good frame reports it ready, its status
 * byte's bit 0, and its link is alive, where the Z3815A's also wants 30 s
 * of uptime (0x33542). */
uint8_t puck_warm(void) {
  static const uint8_t PUCK_READY = 0x01;
  static const uint8_t PUCK_STATUS = 4; /* the frame's status byte */

  return (puck.frame[PUCK_STATUS] & PUCK_READY) != 0 && puck_alive != 0;
}

/* The oscillator's frames on DUART channel B (docs/firmware/z3817a-3752.md,
 * "DUART channel B: the oscillator"): a 0x55 and five bytes, the EFC
 * ADC's 24-bit reading at bytes 1 to 3, most significant first, and the
 * sum of the first five last. */
static const uint8_t PUCK_HEADER = 0x55;

static const uint32_t PUCK_FRAME_LENGTH = 6;

static const uint8_t PUCK_READING_HIGH = 1;

static const uint8_t PUCK_READING_MIDDLE = 2;

static const uint8_t PUCK_READING_LOW = 3;

/* How long a good frame keeps the link alive, in seconds of uptime. */
static const uint32_t PUCK_ALIVE_SECONDS = 10;

/* Corrects the EFC by the oscillator's reading, a good frame's
 * (0x3355e): the reading against the one expected of the EFC.  While the
 * error is inside the ADC's 24 bits, the frames since the EFC was set are
 * counted to five; after them, with corrections on, the error is added to
 * puck.error_sum, whose thousandth is the correction, back to 0 past
 * 500000; and the EFC written is pll.efc, plus the correction while
 * corrections are on.  While efc_adc_debug is 1 it prints the error, the
 * correction, the EFC written and the reading, whatever the error.  The
 * Z3815A 4010's puck_efc_correct adds a thousandth of each error to the
 * correction, restarts it from the error over :DADC and logs an error out
 * of range. */
void efc_adc_correct(void) {
  static const int32_t ERROR_RANGE = 0xffffff;
  static const uint32_t SETTLE_FRAMES = 5;
  static const float CORRECTION_GAIN = 0.001f;
  static const float CORRECTION_RANGE = 500000.0f;
  static const uint8_t DEBUG_ON = 1;
  int32_t error;

  efc_adc_reading = ((uint32_t)puck.frame[PUCK_READING_HIGH] << CPU_BYTE_BITS |
                     puck.frame[PUCK_READING_MIDDLE])
                        << CPU_BYTE_BITS |
                    puck.frame[PUCK_READING_LOW];
  error = puck.efc_adc_expected - efc_adc_reading;
  if (abs(error) < ERROR_RANGE) {
    if (puck.frames_since_efc < SETTLE_FRAMES) {
      puck.frames_since_efc++;
    } else if (puck.correction_restart != 0) {
      float_add_in_place(&puck.error_sum, integer_to_float(error));
      puck.correction = float_multiply(puck.error_sum, CORRECTION_GAIN);
      if (float_compare(float_absolute_on_stack(puck.correction),
                        CORRECTION_RANGE) > 0) {
        puck.correction = 0.0f;
      }
    }
    if (puck.correction_restart != 0) {
      puck.efc_written = float_add(pll.efc, puck.correction);
    } else {
      puck.efc_written = pll.efc;
    }
  }
  if (efc_adc_debug == DEBUG_ON) {
    debug_printf("err= %d corr= %f efc= %f adc= %d\r\n", error,
                 float_to_double(puck.correction),
                 float_to_double(puck.efc_written), efc_adc_reading);
  }
}

/* Takes a byte of the oscillator's frames from DUART channel B's ring,
 * if one is waiting (0x3374c): a 0x55 starts a frame, whose bytes go to
 * puck.frame through puck.cursor; at its sixth, a frame whose first five
 * bytes sum to the sixth is good, corrects the EFC and keeps the link
 * alive for 10 s more.  The link lapses first once the uptime is past
 * puck_alive_until.  efc_write_dithered calls it, each hmon_task pass. */
void puck_frame_receive(void) {
  uint8_t character;

  if (get_uptime() > puck_alive_until) {
    puck_alive = 0;
  }
  if (duartb_receive_ready() == 0) {
    return;
  }
  character = duartb_getc(NULL);
  if (puck.framing == 0) {
    if (character == PUCK_HEADER) {
      puck.cursor = puck.frame;
      *puck.cursor++ = character;
      puck.framing = 1;
    }
    return;
  }
  *puck.cursor++ = character;
  if (puck.cursor != &puck.frame[PUCK_FRAME_LENGTH]) {
    return;
  }
  uint8_t sum = 0;
  const uint8_t* next = puck.frame;

  for (uint32_t index = 0; index < PUCK_FRAME_LENGTH - 1; index++) {
    sum += *next++;
  }
  if (sum == puck.frame[PUCK_FRAME_LENGTH - 1]) {
    efc_adc_correct();
    puck_alive = 1;
    puck_alive_until = get_uptime() + PUCK_ALIVE_SECONDS;
  }
  puck.framing = 0;
}

/* Sets the EFC: pll.efc, the EFC written, plus the correction while
 * corrections are on, which restarts the frames' count, and the ADC
 * reading expected of it (0x337fc).  The Z3815A 4010's, whose
 * corrections are efc_compensation's. */
void efc_set(float efc) {
  pll.efc = efc;
  if (puck.correction_restart != 0) {
    puck.frames_since_efc = 0;
    puck.efc_written = float_add(efc, puck.correction);
  } else {
    puck.efc_written = efc;
  }
  puck.efc_adc_expected = float_to_integer(
      float_multiply(efc, scpi_value_DIAGnostic_ROSCillator_EFControl_DADC));
}

/* Clamps an EFC to 0 to 1048560, either end itself clamped, reporting
 * the first clamp, which it logs, and the first value back in range
 * (0x338b8).  The Z3815A 4010's reports each clamp and logs none. */
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
  if (!efc_range.clamped) {
    status_happening(STATUS_EFC_AT_LIMIT_SET);
    log_write("EFC at limit");
    efc_range.clamped = 1;
  }
  return efc;
}

/* Reports the EFC near the end of its range, outside 5% to 95%, once six
 * checks in a row find it there, and back inside at once (0x3395a): the
 * loop's, pll.efc, or in a user mode :DIAGnostic:ROSCillator:EFControl:DATA,
 * which takes the count to its last at once, so that the next check outside
 * reports.  The 58503A 3704's but for the user mode. */
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
    if (!efc_range.near_end) {
      task_debug_message("EFC near end of range\r\n");
      status_happening(STATUS_EFC_NEAR_LIMIT_SET);
      efc_range.near_end = 1;
    }
  }
}

/* Clears the reference pulse monitor's state, the PP2S's verdict left
 * dead (0x33c3a). */
void gps_pps_monitor_reset(void) {
  gps_pps_skipped_checks = 0;
  gps_pps_unconfirmed = 0;
  gps_pps_unset_flag = 0;
  gps_pps_dead = 0;
  gps_pps_counting = 0;
  reference_pps_dead = 0;
  reference_pp2s_dead = 1;
  gps_pps_from_engine = 0;
  gps_pps_timeref = 0;
}

/* Whether the GPS record's sync state takes the reference pulse for
 * dead: a state but valid, questionable clearing or assessing, unless
 * gps.field_592's GPS_FIELD_592_PPS_CONTINUOUS or gps_pps_unset_flag is set
 * (0x33cbc, 0x33df8). */
static bool monitors_gps_unsynchronized(void) {
  return (gps.field_592 & GPS_FIELD_592_PPS_CONTINUOUS) == 0 &&
         gps_pps_unset_flag == 0 && gps.references[0].state != GPS_SYNC_VALID &&
         gps.references[0].state != GPS_SYNC_QUESTIONABLE_CLEARING &&
         gps.references[0].state != GPS_SYNC_ASSESSING;
}

/* Whether a good short gate brings the pulse back at once: while the
 * GPS record's tests apply, without GPS_FIELD_592_PPS_CONTINUOUS, and the
 * record is questionable (0x33eee, 0x33f6e). */
static bool monitors_gps_questionable(void) {
  return gps_pps_from_engine != 0 &&
         (gps.field_592 & GPS_FIELD_592_PPS_CONTINUOUS) == 0 &&
         gps.references[0].valid == GPS_REFERENCE_QUESTIONABLE;
}

/* Marks both of the reference pulse's verdicts dead. */
static void monitors_ref_pulses_dead(void) {
  reference_pps_dead = 1;
  reference_pp2s_dead = 1;
}

/* gps_pps_dead, the verdict the loop takes: the 1PPS's or the PP2S's,
 * by pll.pulse_mode (0x33cf2, 0x34124). */
static void monitors_gps_pps_pick(void) {
  gps_pps_dead = pll.pulse_mode == REFERENCE_PULSE_1PPS ? reference_pps_dead
                                                        : reference_pp2s_dead;
}

/* The reference pulse monitor's part of every hmon_task pass (0x33c74):
 * while the GPS record's tests apply and the TIMEREF selection is the
 * one the counts began under, gps.pps_check_disabled marks the check
 * unconfirmed and both verdicts dead, as a sync state that takes the
 * pulse for dead does; then gps_pps_dead is picked afresh.  The 58503A
 * 3704's hmon_task tests gps.pps_check_disabled alone. */
void gps_pps_pass_update(void) {
  if (gps_pps_from_engine == 0 || pll.timeref_select != gps_pps_timeref) {
    return;
  }
  if (gps.pps_check_disabled != 0) {
    gps_pps_unconfirmed = 1;
    gps_pps_skipped_checks = 0;
    monitors_ref_pulses_dead();
  } else if (monitors_gps_unsynchronized()) {
    /* the image's store, which the pick below overwrites (0x33ce4) */
    gps_pps_dead = 1;
    monitors_ref_pulses_dead();
  }
  monitors_gps_pps_pick();
}

/* Starts the pulse counts afresh: the accumulator, both last counts and
 * the gate. */
static void monitors_pps_counts_restart(void) {
  gpt.pacnt = 0;
  gps_pps_gate = 0;
  gps_pps_last_count = 0;
  gps_pps_count_before = 0;
}

/* A short gate's test of one verdict: pulses since the check it counts
 * from outside 1 to 3 make the verdict dead, logged once, and the gate
 * bad; inside them, while monitors_gps_questionable, bring it back at
 * once, clear the checks' flags and set the gate to 21, past the long
 * gates (0x33ea8 to 0x33f28, 0x33f2c to 0x33f98). */
static bool monitors_short_gate(int8_t since, uint8_t* dead, char* dead_text,
                                char* ok_text) {
  static const uint8_t PPS_GATE_RESTART = 21;

  if (since < MONITORS_PPS_LEAST || since > MONITORS_PPS_MOST) {
    if (*dead == 0) {
      task_dmessage(dead_text);
      debug_printf("short gate, count %d\r\n", since);
      *dead = 1;
    }
    return true;
  }
  if (monitors_gps_questionable()) {
    if (*dead != 0) {
      task_dmessage(ok_text);
      *dead = 0;
    }
    gps_pps_unconfirmed = 0;
    gps_pps_unset_flag = 0;
    gps_pps_gate = PPS_GATE_RESTART;
  }
  return false;
}

/* A long gate's test of the 1PPS's verdict: pulses outside the gate's
 * range make it dead, logged once; inside it, with the short gate good,
 * bring it back and clear the checks' flags (0x33fba to 0x3401a, 0x34026
 * to 0x3407a). */
static void monitors_pps_long_gate(uint8_t pulses, uint8_t least, uint8_t most,
                                   bool short_gate_bad) {
  if (pulses < least || pulses > most) {
    if (reference_pps_dead == 0) {
      task_dmessage("Ref 1pps signal frequency is incorrect\r\n");
      debug_printf("long gate, count %d\r\n", (uint32_t)pulses);
      reference_pps_dead = 1;
    }
  } else if (!short_gate_bad) {
    if (reference_pps_dead != 0) {
      task_dmessage("Ref 1pps signal is ok\r\n");
      reference_pps_dead = 0;
    }
    gps_pps_unconfirmed = 0;
    gps_pps_unset_flag = 0;
  }
}

/* Checks the reference pulse by the pulses the GPT's pulse accumulator
 * has counted (PACTL's PAEN and PEDGE, rising edges, MC68331UM D.2.7),
 * as a 1PPS and as a PP2S, into reference_pps_dead and reference_pp2s_dead,
 * each logged as it changes, and picks gps_pps_dead from them by pll.pulse_mode
 * (0x33d08).  Each check wants 1 to 3 pulses since the last for the 1PPS and
 * since the one before for the PP2S; the gate, 2 a check, ends the long gates,
 * 8 to 12 at 10 and 17 to 23 at 20 for the 1PPS, and 8 to 12 at 20 for the
 * PP2S, after which the counts start again.  While the external record is not
 * valid and the GPS record valid with the pulse alive, the GPS record's tests
 * apply too (gps_pps_from_engine): gps.pps_check_disabled and a sync state that
 * takes the pulse for dead mark both verdicts dead, an unconfirmed check
 * skips 15 checks, and a good short gate while the record is questionable
 * brings a verdict back at once.  A change of the TIMEREF selection
 * restarts the counts.  Returns 0 on a pass that restarted them for that,
 * for a self-test or the FPGA, or to start the accumulator, else 1. */
uint8_t gps_pps_check(void) {
  static const uint8_t PACTL_COUNT_RISING = 0x50;
  static const uint8_t PPS_SKIPS = 15;
  static const uint8_t PPS_GATE_STEP = 2;
  /* The long gates, at which the gate ends, and the pulses each wants. */
  static const uint8_t PPS_GATE_FIRST = 10;
  static const uint8_t PPS_GATE_LAST = 20;
  static const uint8_t PPS_GATE_FIRST_LEAST = 8;
  static const uint8_t PPS_GATE_FIRST_MOST = 12;
  static const uint8_t PPS_GATE_LAST_LEAST = 17;
  static const uint8_t PPS_GATE_LAST_MOST = 23;
  uint8_t checked = 1;

  if (gps.references[1].valid == GPS_REFERENCE_VALID ||
      pll.timeref_select != gps_pps_timeref) {
    gps_pps_from_engine = 0;
  } else if (gps.references[0].valid == GPS_REFERENCE_VALID &&
             gps_pps_dead == 0) {
    gps_pps_from_engine = 1;
  }
  if (pll.timeref_select != gps_pps_timeref) {
    monitors_pps_counts_restart();
    checked = 0;
    gps_pps_timeref = pll.timeref_select;
  } else if (gps_pps_from_engine != 0 && gps.pps_check_disabled != 0) {
    gps_pps_unconfirmed = 1;
    gps_pps_skipped_checks = 0;
    monitors_ref_pulses_dead();
    monitors_pps_counts_restart();
  } else if (gps_pps_from_engine != 0 && monitors_gps_unsynchronized()) {
    gps_pps_unset_flag = 1;
    monitors_ref_pulses_dead();
    monitors_pps_counts_restart();
  } else if (gps_pps_from_engine != 0 && gps_pps_unconfirmed != 0 &&
             gps_pps_skipped_checks < PPS_SKIPS) {
    gps_pps_skipped_checks++;
    gpt.pacnt = 0;
  } else if (!monitors_pps_checkable()) {
    monitors_pps_counts_restart();
    checked = 0;
  } else if (gps_pps_counting == 0) {
    gpt.pacnt = 0;
    gpt.pactl = PACTL_COUNT_RISING;
    gps_pps_counting = 1;
    monitors_pps_counts_restart();
    checked = 0;
  } else {
    const uint8_t pulses = gpt.pacnt;
    bool pp2s_short_bad = false;
    bool pps_short_bad;

    if (gps_pps_gate != 0) {
      pp2s_short_bad = monitors_short_gate(
          pulses - gps_pps_count_before, &reference_pp2s_dead,
          "Ref pp2s signal is dead\r\n", "Ref pp2s signal is ok\r\n");
    }
    pps_short_bad = monitors_short_gate(
        pulses - gps_pps_last_count, &reference_pps_dead,
        "Ref 1pps signal is dead\r\n", "Ref 1pps signal is ok\r\n");
    gps_pps_count_before = gps_pps_last_count;
    gps_pps_last_count = pulses;
    gps_pps_gate += PPS_GATE_STEP;
    if (gps_pps_gate == PPS_GATE_FIRST) {
      monitors_pps_long_gate(pulses, PPS_GATE_FIRST_LEAST, PPS_GATE_FIRST_MOST,
                             pps_short_bad);
    } else if (gps_pps_gate == PPS_GATE_LAST) {
      monitors_pps_long_gate(pulses, PPS_GATE_LAST_LEAST, PPS_GATE_LAST_MOST,
                             pps_short_bad);
      if (pulses < PPS_GATE_FIRST_LEAST || pulses > PPS_GATE_FIRST_MOST) {
        if (reference_pp2s_dead == 0) {
          task_dmessage("Ref pp2s signal frequency is incorrect\r\n");
          debug_printf("long gate, count %d\r\n", (uint32_t)pulses);
          reference_pp2s_dead = 1;
        }
      } else if (!pp2s_short_bad && reference_pp2s_dead != 0) {
        task_dmessage("Ref pp2s signal is ok\r\n");
        reference_pp2s_dead = 0;
      }
      monitors_pps_counts_restart();
    } else if (gps_pps_gate > PPS_GATE_LAST) {
      monitors_pps_counts_restart();
    }
  }
  monitors_gps_pps_pick();
  return checked;
}

/* Clears both pulse monitors, powerup_flag, the health monitor's step,
 * :DIAGnostic:TMODe and the lamps' state, and starts the oscillator's
 * link afresh: the ADC reading expected of pll.efc, no correction and no
 * error sum, corrections on for an oscillator of kind 0 and off for any
 * other, the link taken for alive for 10 s, efc_adc_debug off and
 * puck.field_0d set (0x34524).  The Z3815A 4010's starts its port E link,
 * its corrections always on. */
void pps_monitors_reset(void) {
  gps_pps_monitor_reset();
  internal_pps_monitor_reset();
  powerup_flag = 0;
  hmon_reset();
  diag_tmode_state = 0;
  diag_tmode_data = 0;
  lamp_bits = 0;
  lamp_blink = 0;
  lamps_requested = 0;
  puck.efc_adc_expected = float_to_integer(float_multiply(
      pll.efc, scpi_value_DIAGnostic_ROSCillator_EFControl_DADC));
  puck.correction = 0.0f;
  puck.error_sum = 0.0f;
  puck.correction_restart = oscillator_kind == 0;
  puck_alive = 1;
  puck_alive_until = get_uptime() + PUCK_ALIVE_SECONDS;
  efc_adc_debug = 0;
  puck.field_0d = 1;
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
 * clock tick's interrupt signals it, every tenth tick, it writes the EFC
 * (efc_write_dithered, which takes the oscillator's frames first), counts
 * its pass for the watchdog, toggles the heartbeat LED and reports the
 * watchdog, updates the reference pulse's dead flag
 * (gps_pps_pass_update), checks the reference pulse and the internal
 * 1PPS, reporting each one's failure once it has lasted and its end,
 * steps the health monitor's channels, checks the EFC, and composes and
 * writes the lamps, blinking, their phase stepped each 20th pass, and
 * unstepped when asked (0x34332).  The 58503A 3704's but for
 * gps_pps_pass_update in place of its gps.pps_check_disabled test, the
 * heartbeat on OP6, a reference pulse failure after 12289 checks, neither
 * counted nor reported while gps_pps_unset_flag is set, and the lamps' 20
 * passes and phase.  Both pulse tests read one result, D5 in the image:
 * the last check's of either, and before any its caller's
 * (docs/firmware/quirks.md). */
void hmon_task(void) {
  static const uint32_t TICK_EVENT = 1;
  static const uint16_t HEARTBEAT_TICKS = 112;
  static const uint16_t PPS_CHECK_TICKS = 204;
  /* Checks a dead pulse lasts before its failure is reported. */
  static const uint16_t GPS_PPS_FAILED_CHECKS = 12289;
  static const uint16_t INTERNAL_PPS_FAILED_CHECKS = 717;
  static const uint16_t CHANNEL_TICKS = 10;
  static const uint16_t EFC_TICKS = 1024;
  static const uint16_t LAMP_TICKS = 20;
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
    gps_pps_pass_update();
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
      } else if (gps_pps_unset_flag == 0) {
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
      lamps_compose(1);
      lamps_write(blink);
      blink = blink == 0;
      lamp_ticks = LAMP_TICKS;
    } else if (lamps_requested != 0) {
      lamps_requested = 0;
      lamps_compose(0);
      lamps_write(blink);
    }
    lamp_ticks--;
  }
}
