/* The Z3815A 4010's health monitor and the OCXO's port E link, written by hand
 * in place of the decompiler's (docs/firmware/README.md, "Hand-written C";
 * docs/firmware/modules.md, `services/monitors`).  The exported C
 * includes this file after its own, so the types and prototypes it
 * declares are in scope.
 *
 * hmon_channels (0x33c9c) is typed through names.tsv as the 58503A's
 * eight-entry hmon_channel_entry table over the Z3815A's own channels
 * (docs/firmware/z3815a-4010.md).  It stays the image's ROM data, as the
 * other typed ROM tables do (status_register_layouts and the rest), and
 * is not a hand-written C object: every reader indexes it in ROM, so a
 * channel past the last -- which the generated cases pass
 * (function_check CHARS) -- reads the same bytes in both copies.  Its
 * convert, add, check and report routines, the monitor averages and the
 * monitor step are written here, with the indirect convert call the
 * decompiler drops reconstructed (the function check,
 * docs/firmware/z3815a-4010.md).  hmon_task is the 58503A 3704's with
 * this image's own (below), and the port E link it clocks, the puck's,
 * is this image's own, over its puck record (puck_link, smartclock.h);
 * the GPS 1PPS monitor is the Z3805A's with this image's reference
 * states and its external 1PPS. */

/* hmon_channels' entries, of which hmon_channel_read reads the last for a
 * channel past it: an array size, so #define. */
#define MONITORS_CHANNELS 8

/* A convert routine called with the image's calling convention: its
 * reading is a one-byte argument at the start of the stack slot (offset
 * 4), where the image reads it (its sidecar's stack offset 4, size 1).
 * gcc lays a byte argument in the low byte of its four-byte slot, so a
 * gcc caller of the image routine must put the reading in the slot's
 * first byte itself.  Because hmon_channels stays the image's ROM data
 * (above), its convert pointer is the image routine, not a gcc copy, so
 * hmon_channel_read makes the call this way rather than through the
 * table's own hmon_convert_function type. */
typedef float hmon_convert_image(uint32_t reading_at_slot_start);

/* Reads an ADC channel, the last for one past it, through its entry's
 * convert routine; 0 for a channel without one (0x33588).  The decompiler
 * cannot reconstruct the indirect convert call, which reads adc_read's
 * byte and passes it to the entry's convert on the stack, so its reading
 * drops the call and returns 0. */
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

/* An add, check or report routine called with the image's calling
 * convention, its channel a one-byte argument at the start of the stack
 * slot, as hmon_convert_image's reading (above); a check's result in
 * D0's low byte. */
typedef uint8_t hmon_routine_image(uint32_t channel_at_slot_start);

/* An hmon_channels entry's routine, called with a channel as the image
 * calls it. */
static uint8_t monitors_call(const void* routine, uint8_t channel) {
  return ((hmon_routine_image*)routine)((uint32_t)channel << 24);
}

/* The health monitor's step: the channel hmon_channel_index names is
 * added to its monitor and, while its check says so or it has none,
 * reported; then the next channel, round robin (0x33af0).  The 58503A
 * 3704's, its entry's routines called as hmon_channel_read calls its
 * convert routine (above): the decompiler's reading passed them D2's
 * high word, not the index the image pushes. */
void hmon_channel_step(void) {
  if (hmon_channels[hmon_channel_index].add != 0) {
    monitors_call(hmon_channels[hmon_channel_index].add, hmon_channel_index);
  }
  if (hmon_channels[hmon_channel_index].report != 0) {
    uint8_t due = hmon_channels[hmon_channel_index].check == 0
                      ? 1
                      : monitors_call(hmon_channels[hmon_channel_index].check,
                                      hmon_channel_index);

    if (due != 0) {
      monitors_call(hmon_channels[hmon_channel_index].report,
                    hmon_channel_index);
    }
  }
  hmon_channel_index++;
  if (hmon_channel_index > MONITORS_CHANNELS - 1) {
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

/* Marks the GPS reference's 1PPS dead, and the 1PPS the loop takes too
 * while it takes the GPS's. */
static void monitors_gps_pps_lost(void) {
  gps_reference_pps_dead = 1;
  if (pll.external_reference == 0) {
    gps_pps_dead = 1;
  }
}

/* The health monitor's task, which root_task starts: on each event the
 * clock tick's interrupt signals it writes the EFC, clocks the port E
 * link (puck_link_clock), counts its pass for the watchdog, toggles the
 * heartbeat LED and reports the watchdog, checks both 1PPS, reporting
 * each one's failure once it has lasted and its end, steps the health
 * monitor's channels, checks the EFC and debounces DUART inputs 0 and 1
 * (0x36c52).  The 58503A 3704's but for this image's own: the port E
 * link; the heartbeat on OP6; the GPS reference's 1PPS taken for lost,
 * besides while its check is off, in any state but valid, assessing or
 * clearing a questionable state, unless gps_pps_unset_flag; its failure
 * reported by gps_reference_pps_dead, after 2561 checks; no lamps; and
 * the inputs.  Both 1PPS tests read one result, D4 in the image: the
 * last check's of either, and before any its caller's
 * (docs/firmware/quirks.md). */
void hmon_task(void) {
  static const uint32_t TICK_EVENT = 1;
  static const uint16_t HEARTBEAT_TICKS = 112;
  static const uint16_t PPS_CHECK_TICKS = 204;
  /* Checks a dead 1PPS lasts before its failure is reported. */
  static const uint16_t GPS_PPS_FAILED_CHECKS = 2561;
  static const uint16_t INTERNAL_PPS_FAILED_CHECKS = 717;
  static const uint16_t CHANNEL_TICKS = 10;
  static const uint16_t EFC_TICKS = 1024;
  /* The heartbeat's LED, OP6 here, which the others' tests name. */
  static const board_led HEARTBEAT = BOARD_LED_DUART;
  uint8_t checked = CPU_CALLERS_REGISTER(uint32_t, "d4");
  uint16_t heartbeat_ticks = 0;
  uint16_t gps_check_ticks = 0;
  uint16_t internal_check_ticks = 0;
  uint16_t channel_ticks = 0;
  uint16_t efc_ticks = 0;
  uint16_t gps_dead_checks = 0;
  uint16_t internal_dead_checks = 0;
  bool gps_failure = false;
  bool internal_failure = false;
  bool heartbeat_on = false;

  watchdog_countdown = MONITORS_WATCHDOG_GRACE;
  efc_clamp_reset();
  psos_get_v(TICK_EVENT, 0, NULL);
  for (;;) {
    uint32_t error = (uint8_t)psos_wait_v(TICK_EVENT, 0, 0, NULL);

    efc_write_dithered();
    puck_link_clock();
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
      monitors_gps_pps_lost();
    } else if (gps_pps_unset_flag == 0 &&
               gps.references[0].state != GPS_SYNC_VALID &&
               gps.references[0].state != GPS_SYNC_QUESTIONABLE_CLEARING &&
               gps.references[0].state != GPS_SYNC_ASSESSING) {
      monitors_gps_pps_lost();
    }
    if (monitors_period_up(&gps_check_ticks, PPS_CHECK_TICKS)) {
      checked = gps_pps_check();
    }
    if (checked != 0) {
      if (gps_reference_pps_dead == 0 ||
          gps.references[0].valid != MONITORS_REFERENCE_VALID) {
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
    input_debounce_update(duart_input_0_debounced, &duart_input_0_debounce,
                          &duart_input_0_state);
    input_debounce_update(duart_input_1_debounced, &duart_input_1_debounce,
                          &duart_input_1_state);
  }
}

/* Checks the tasks' watchdog counts, printing them if one is short, and
 * services the watchdog while the grace lasts; then clears them
 * (0x36664).  The 58503A 3704's with a count of this image's own,
 * "bp", which backp_task's task counts (0x34e3c), last. */
void watchdog_report(void) {
  /* The watchdog: each task counts its passes, and the health monitor
   * services the MC68331's software watchdog (MC68331UM D.3.15, SWSR's
   * CPU_SWSR_FIRST then CPU_SWSR_SECOND) only while every count since
   * the last report is enough, and for eight reports after. */
  static const uint8_t WATCHDOG_GPS_LEAST = 21;
  static const uint8_t WATCHDOG_MONITOR_LEAST = 51;
  static const uint8_t WATCHDOG_BP_LEAST = 26;

  if (watchdog_clock != 0 && watchdog_gps >= WATCHDOG_GPS_LEAST &&
      watchdog_monitor >= WATCHDOG_MONITOR_LEAST && watchdog_spool != 0 &&
      watchdog_pll != 0 && watchdog_bp >= WATCHDOG_BP_LEAST) {
    watchdog_countdown = MONITORS_WATCHDOG_GRACE;
  } else {
    debug_printf(
        "watchdog: clock: %4d gps: %4d pll: %4d mon: %4d spool %4d bp %4d\r\n",
        watchdog_clock, watchdog_gps, watchdog_pll, watchdog_monitor,
        watchdog_spool, watchdog_bp);
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
  watchdog_bp = 0;
}

/* The OCXO's port E link, the puck's (docs/firmware/z3815a-4010.md): its
 * bits on port E, PE2 high while a frame is sent, PE5 its data and PE7 the
 * clock hmon_task's pass gives it (0x365be to 0x36642). */
static const uint8_t PUCK_FRAMED = 0x04;

static const uint8_t PUCK_DATA = 0x20;

static const uint8_t PUCK_DATA_SHIFT = 5;

static const uint8_t PUCK_CLOCK = 0x80;

/* A frame's first byte, and its length by it: a 0x55 frame's reading and
 * status, and an 0xa1 frame, which the link checks but takes nothing
 * from. */
static const uint8_t PUCK_HEADER_READING = 0x55;

static const uint8_t PUCK_HEADER_OTHER = 0xa1;

static const uint8_t PUCK_READING_LENGTH = 6;

static const uint8_t PUCK_OTHER_LENGTH = 7;

static const uint8_t PUCK_BYTE_BITS = 8;

/* How long a good frame keeps the link alive, and how long after power-up
 * the puck may be warm, in seconds of uptime. */
static const uint32_t PUCK_ALIVE_SECONDS = 10;

static const uint32_t PUCK_WARM_UPTIME = 30;

/* A 0x55 frame's status bit 0, "puck warm". */
static const uint8_t PUCK_STATUS_WARM = 0x01;

/* The ADC's error, the expected reading less the reading, at which it is
 * out of range, 10% of 2^24, and the correction past which it starts again
 * from 0. */
static const int32_t PUCK_ERROR_RANGE = 1677721;

static const float PUCK_CORRECTION_RANGE = 500000.0f;

/* What of the error a correction takes after its first. */
static const float PUCK_CORRECTION_GAIN = 0.001f;

/* Frames after the EFC is set before the readback corrects it, and in n3
 * lock pll's pll state. */
static const uint32_t PUCK_SETTLE_FRAMES = 3;

static const uint32_t PUCK_SETTLE_FRAMES_LOCKED = 1;

/* Whether the link is alive (0x35f98). */
uint8_t puck_alive(void) { return puck.alive; }

/* The puck word's report: the OCXO calibration and, while the link is
 * alive, whether the puck is warm, the ADC error flag and the status
 * bytes (0x35fa0). */
void puck_report(void) {
  printf("a= %e b= %e a/d= %e\r\n",
         float_to_double(scpi_value_DIAGnostic_ROSCillator_EFControl_ASLOPe),
         float_to_double(scpi_value_DIAGnostic_ROSCillator_EFControl_BSLOPe),
         float_to_double(scpi_value_DIAGnostic_ROSCillator_EFControl_DADC));
  if (puck_alive() == 0) {
    printf("puck communication is not alive\r\n");
    return;
  }
  printf("puck communication is alive\r\n");
  printf("puck warm= %d\r\n", (uint32_t)puck_warm());
  printf("puck EFC ADC err= %d\r\n", (uint32_t)puck.adc_error_logged);
  printf("status1 byte= %x\r\n", (uint32_t)puck.status1);
  printf("status2 byte= %x\r\n", (uint32_t)puck.status2);
}

/* Whether the puck is warm: its status says so, the link is alive and the
 * uptime is past 30 s (0x36054). */
uint8_t puck_warm(void) {
  return (puck.status & PUCK_STATUS_WARM) != 0 && puck.alive != 0 &&
         get_uptime() > PUCK_WARM_UPTIME;
}

/* Whether the EFC has settled for the readback to act on: three frames
 * since it was set, or one in n3 lock pll's pll state. */
static bool puck_settled(void) {
  return (pll.stage == PLL_STAGE_N3_LOCK_PLL &&
          puck.frames_since_efc >= PUCK_SETTLE_FRAMES_LOCKED &&
          pll.n3_lock_state == PLL_N3_LOCK_PLL) ||
         puck.frames_since_efc >= PUCK_SETTLE_FRAMES;
}

/* Corrects the EFC by its readback, a good frame's (0x3607c): the error,
 * the expected reading less the ADC's, once settled and while
 * efc_compensation is set, gives the correction, the error over :DADC
 * after a restart, else a thousandth of it added, back to 0 past its
 * range; the EFC written is pll.efc plus it.  An error out of range is
 * logged, once until one is back in range.  The float tests are the
 * image's: :DADC's bits but its sign (0x360f6), the correction's
 * magnitude through float_absolute_on_stack. */
void puck_efc_correct(void) {
  int32_t error = puck.efc_adc_expected - efc_adc_reading;

  if (abs(error) < PUCK_ERROR_RANGE) {
    if (!puck_settled()) {
      puck.frames_since_efc++;
    } else if (efc_compensation != 0) {
      uint32_t dac_per_adc_bits =
          *(const uint32_t*)&scpi_value_DIAGnostic_ROSCillator_EFControl_DADC;

      if (puck.correction_restart != 0 && (dac_per_adc_bits << 1) != 0) {
        puck.correction =
            float_divide(integer_to_float(error),
                         scpi_value_DIAGnostic_ROSCillator_EFControl_DADC);
        puck.correction_restart = 0;
      } else {
        float_add_in_place(
            &puck.correction,
            float_multiply(integer_to_float(error), PUCK_CORRECTION_GAIN));
      }
      if (float_compare(float_absolute_on_stack(puck.correction),
                        PUCK_CORRECTION_RANGE) > 0) {
        puck.correction = 0.0f;
      }
    }
    if (efc_compensation != 0) {
      puck.efc_written = float_add(pll.efc, puck.correction);
    } else {
      puck.efc_written = pll.efc;
    }
    puck.adc_error_logged = 0;
  } else if (puck.adc_error_logged == 0 &&
             efc_compensation == PUCK_COMPENSATION_ON && puck_settled()) {
    log_write("EFC ADC out of range (adc= %d)", efc_adc_reading);
    log_write("EFC ADC out of range (efc= %f)", float_to_double(pll.efc));
    puck.adc_error_logged = 1;
  } else {
    puck.frames_since_efc++;
  }
  if (efc_compensation_debug == PUCK_DEBUG_ON) {
    debug_printf("err= %d corr= %f ", error, float_to_double(puck.correction));
    debug_printf("efc= %f adc= %d\r\n", float_to_double(puck.efc_written),
                 efc_adc_reading);
  }
}

/* Takes a good frame: its header, and from a 0x55 frame its status and
 * the 24-bit EFC ADC reading, most significant byte first (0x3622c). */
void puck_frame_decode(void) {
  puck.header = puck.frame[0];
  if (puck.frame[0] != PUCK_HEADER_READING) {
    return;
  }
  puck.status = puck.frame[4];
  puck.status1 = puck.frame[4];
  puck.status2 = 0;
  efc_adc_reading = ((int32_t)puck.frame[1] << 16) +
                    ((int32_t)puck.frame[2] << 8) + puck.frame[3];
}

/* Sets the EFC: pll.efc, the EFC written, plus the correction while
 * efc_compensation is set, which restarts the frames' count, and the ADC
 * reading expected of it (0x36350). */
void efc_set(float efc) {
  pll.efc = efc;
  if (efc_compensation != 0) {
    puck.frames_since_efc = 0;
    puck.efc_written = float_add(efc, puck.correction);
  } else {
    puck.efc_written = efc;
  }
  puck.efc_adc_expected = float_to_integer(
      float_multiply(efc, scpi_value_DIAGnostic_ROSCillator_EFControl_DADC));
}

/* Starts a frame afresh at its first byte. */
static void puck_frame_restart(void) {
  puck.cursor = puck.frame;
  *puck.cursor = 0;
  puck.framing = 0;
}

/* A byte of a frame shifted in: the next, or at the frame's last, its sum
 * checked, a good frame decoded, the EFC corrected by it and the link
 * kept alive, and the frame started again (0x36502). */
void puck_byte_done(void) {
  uint8_t sum = 0;

  if (puck.cursor < &puck.frame[puck.frame_length - 1]) {
    puck.cursor++;
    *puck.cursor = 0;
    return;
  }
  puck.framing = 0;
  puck.cursor = puck.frame;
  for (int32_t i = 0; i < puck.frame_length - 1; i++) {
    sum += *puck.cursor++;
  }
  if (sum == puck.frame[puck.frame_length - 1]) {
    puck_frame_decode();
    puck_efc_correct();
    puck.alive = 1;
    puck.alive_until = get_uptime() + PUCK_ALIVE_SECONDS;
  }
  puck_frame_restart();
  puck.frame[0] = 0;
}

/* Begins a frame by its first byte: its length, and the next byte. */
static void puck_frame_begin(uint8_t length) {
  puck.frame_length = length;
  puck.framing = 1;
  puck.cursor++;
  *puck.cursor = 0;
  puck.bits = 0;
}

/* Clocks the link once, each hmon_task pass (0x3658e): the link lapses
 * past alive_until; while PE2 is high, PE5's bit is added to the byte
 * being shifted in, which until a frame's first byte has come is tested
 * after each bit for 0x55 or 0xa1, and after it is done each eighth bit;
 * else shifted left; then PE7 pulsed low.  While PE2 is low, the frame
 * starts again. */
void puck_link_clock(void) {
  if (get_uptime() > puck.alive_until) {
    puck.alive = 0;
  }
  if ((sim.porte0 & PUCK_FRAMED) == 0) {
    puck_frame_restart();
    puck.frame[0] = 0;
    puck.bits = 0;
    return;
  }
  *puck.cursor += (uint8_t)((sim.porte0 & PUCK_DATA) >> PUCK_DATA_SHIFT);
  if (puck.framing == 1) {
    puck.bits++;
    if (puck.bits == PUCK_BYTE_BITS) {
      puck_byte_done();
      puck.bits = 0;
    } else {
      *puck.cursor <<= 1;
    }
  } else if (puck.frame[0] == PUCK_HEADER_READING) {
    puck_frame_begin(PUCK_READING_LENGTH);
  } else if (puck.frame[0] == PUCK_HEADER_OTHER) {
    puck_frame_begin(PUCK_OTHER_LENGTH);
  } else {
    *puck.cursor <<= 1;
  }
  sim.porte0 &= (uint8_t)~PUCK_CLOCK;
  sim.porte0 |= PUCK_CLOCK;
}

/* Clears both 1PPS monitors, the flag powerup sets, the health monitor's
 * step, :DIAGnostic:TMODe and the lamps' state, as the 58503A 3704's
 * pps_monitors_reset, and starts the port E link afresh: the ADC
 * reading expected of pll.efc, no correction, the correction's restart,
 * efc_compensation on and its debugging off, the link taken for alive
 * for 10 s and a frame at its start (0x36e84). */
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
  efc_compensation = PUCK_COMPENSATION_ON;
  puck.adc_error_logged = 0;
  puck.alive = 1;
  puck.alive_until = get_uptime() + PUCK_ALIVE_SECONDS;
  efc_compensation_debug = 0;
  puck.correction_restart = 1;
  puck.status = 0;
  puck.bits = 0;
  puck.cursor = puck.frame;
  puck.frames_since_efc = 0;
}

/* Reads after the first that must agree before a debounced input takes
 * the value. */
static const int16_t MONITORS_DEBOUNCE_READS = 5;

/* Debounces an input: its reader's value, taken into steady once more
 * than five reads in a row after the first have agreed; 1 when taken
 * (0x36bba).  A count below 0 starts it again. */
uint8_t input_debounce_update(input_debounce_reader* reader,
                              input_debounce* record, uint8_t* steady) {
  uint8_t value = reader();

  if (value != record->value || record->count < 0) {
    record->value = value;
    record->count = 0;
    return 0;
  }
  if (record->count <= MONITORS_DEBOUNCE_READS) {
    record->count++;
    return 0;
  }
  *steady = value;
  return 1;
}

/* Clears the ADC monitors, which system_init calls as the 58503A 3704's
 * does (0x36f2e), the 58503A's. */
void monitors_reset(void) { adc_monitors_reset(); }

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
 * has them at 0x33c9c (hmon_channel_entry), so that hmon_channel_read
 * and the monitor step call the C's routines, with its argument
 * layout.  MONITORS_NO_EVENT is an entry's "none", 0x16, which
 * hmon_report_tolerance compares with and does not raise (0x3390c,
 * 0x33970); 0x16 is STATUS_INPUT_OVERRUN's code. */
static const status_condition_code MONITORS_NO_EVENT = STATUS_INPUT_OVERRUN;

static const float MONITORS_5V_SCALE = 0.0242f;

static const float MONITORS_15V_SCALE = 0.0763f;

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

/* hmon_channels' 5V convert routine (0x334d8). */
float hmon_convert_5v(uint8_t reading) {
  return monitors_scaled(reading, MONITORS_5V_SCALE);
}

/* hmon_channels' 15V convert routine (0x334f2). */
float hmon_convert_15v(uint8_t reading) {
  return monitors_scaled(reading, MONITORS_15V_SCALE);
}

/* hmon_channels' -15V convert routine (0x3350c). */
float hmon_convert_minus_15v(uint8_t reading) {
  return monitors_scaled_less_5v(reading, MONITORS_MINUS_15V_SCALE,
                                 MONITORS_MINUS_15V_5V_SCALE);
}

/* hmon_channels' Primary oven current convert routine: the reading times
 * 3.9035797, where the 58503A's is times 250/255 (0x33554). */
float hmon_convert_oven_current(uint8_t reading) {
  static const float SCALE = 3.9035797f;

  return monitors_scaled(reading, SCALE);
}

/* hmon_channels' Antenna current convert routine: the reading times
 * 0.3837, where the 58503A's is times 250/255 (0x3356e). */
float hmon_convert_antenna_current(uint8_t reading) {
  static const float SCALE = 0.3837f;

  return monitors_scaled(reading, SCALE);
}

/* Starts every ADC monitor afresh, the Z3805A's monitors_reset's loop
 * (0x335e2). */
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
static uint8_t* const MONITORS_UNREAD_FLAG = (uint8_t*)0x102174;

/* Clears MONITORS_UNREAD_FLAG and starts the health monitor's step at its
 * first channel (0x3360c). */
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
 * in, keeps their mean (0x3361a).  The Z3805A's takes a monitor and a
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
 * into its monitor, the first taken as it is (0x336a0). */
void monitor_filter_add(adc_channel channel) {
  adc_monitor* monitor = &adc_monitors[channel];

  if (!monitor->valid) {
    monitor->valid = 1;
    monitor->value = hmon_channel_read(channel);
    return;
  }
  monitors_filter(monitor, hmon_channel_read(channel));
}

/* An add routine no entry names and nothing calls (revisions.md): filters
 * the channel's reading in as monitor_filter_add does, the first
 * kept in samples[0], and every 75th reading keeps the change since in
 * samples[1] and the value in samples[0], and sets samples[2] to 1, but
 * in the first 900 seconds, where the 58503A's waits 600, only when the
 * change passes the entry's minimum and the value is below its maximum
 * (0x33724). */
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

/* A report routine's message's size. */
#define MONITORS_MESSAGE_SIZE 80

/* Reports a channel out of tolerance, logged, printed and its entry's
 * first happening, or back within it, printed and its second, each once
 * by its monitor's out_of_tolerance; an entry's MONITORS_NO_EVENT is not
 * raised (0x3384e). */
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
 * nothing calls it (0x33994). */
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
 * (0x339ea). */
uint8_t hmon_channel_out_of_range(uint8_t channel) {
  return monitor_out_of_range(
      &adc_monitors[channel], hmon_channels[channel].minimum,
      hmon_channels[channel].maximum, MONITORS_RANGE_LIMIT);
}

/* The 5V, 15V and -15V entries' report routine: the channel out of
 * tolerance after a run out of its range (0x33a7a). */
void hmon_report_range(adc_channel channel) {
  hmon_report_tolerance(channel, hmon_channel_out_of_range(channel));
}

/* The Primary oven current entry's report routine: out of tolerance
 * until oven_warmed_up, where the 58503A's waits for
 * double_oven_warmed_up (0x33a90). */
void hmon_report_oven_warm(adc_channel channel) {
  hmon_report_tolerance(channel, !oven_warmed_up());
}

/* The Primary oven current entry's check: 1 while the loop is in holdover
 * to normal, the stages 3 to 7, n3 lock pll among them, else 0
 * (0x33aaa).  The image compares pll.stage as a signed byte, which for an
 * enum of 0 to 10 is the same. */
uint8_t hmon_check_pll_stage(adc_channel channel) {
  return pll.stage >= PLL_STAGE_HOLDOVER && pll.stage <= PLL_STAGE_NORMAL_PLL;
}

/* A check routine no entry names: whether the uptime passes the
 * channel's entry's field_0c (0x33ac6). */
uint8_t hmon_channel_settled(uint8_t channel) {
  return get_uptime() > (uint32_t)hmon_channels[channel].field_0c;
}

/* A channel's monitored value, or its entry's nominal value while it has
 * none; a channel past the last is the last (0x33b8c). */
float adc_average(adc_channel channel) {
  if (channel > ADC_ANTENNA_CURRENT) {
    channel = ADC_ANTENNA_CURRENT;
  }
  if (!adc_monitors[channel].valid) {
    return hmon_channels[channel].nominal;
  }
  return adc_monitors[channel].value;
}

/* PORTF0's bit 1, clear when the antenna is faulted (0x33c14). */
static const uint8_t MONITORS_PORTF0_ANTENNA_OK = 0x02;

/* 1 while :DIAG:GPS:ACURrent's alarm is enabled and the antenna current
 * is a fault: below 10 with the switch word's bit 13 set, above 70, or
 * with PORTF0's bit 1 clear.  The 58503A's tests below 10 or above 200
 * alone (0x33bd8). */
uint8_t hmon_check_antenna_current(void) {
  static const float ANTENNA_CURRENT_LEAST = 10.0f;
  static const float ANTENNA_CURRENT_MOST = 70.0f;
  float average = adc_average(ADC_ANTENNA_CURRENT);

  return nv_settings.antenna_current_alarm != 0 &&
         ((float_compare(ANTENNA_CURRENT_LEAST, average) > 0 &&
           (switch_word & SWITCH_WORD_BIT_13) != 0) ||
          (sim.portf0 & MONITORS_PORTF0_ANTENNA_OK) == 0 ||
          float_compare(average, ANTENNA_CURRENT_MOST) > 0);
}

/* Whether the Primary oven current's average is below 340, powerup's
 * warm-up test; the 58503A's tests the oven's reading below -2
 * (0x33c74). */
bool oven_warmed_up(void) {
  static const float OVEN_CURRENT_WARM = 340.0f;

  return float_compare(adc_average(ADC_OVEN_CURRENT), OVEN_CURRENT_WARM) < 0;
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
 * it (0x36274). */
uint8_t porte_read_steady(uint8_t mask) {
  return monitors_read_steady(&sim.porte0, mask, MONITORS_STEADY_AGREEING,
                              MONITORS_STEADY_READS);
}

/* PORTF0's bits of mask, ten reads in a row within 1000; nothing calls
 * it (0x362be). */
uint8_t portf0_read_steady(uint8_t mask) {
  return monitors_read_steady(&sim.portf0, mask, MONITORS_STEADY_AGREEING,
                              MONITORS_STEADY_READS);
}

/* PORTGP's bits of mask, four reads in a row within 100; nothing calls
 * it (0x36308). */
uint8_t portgp_read_steady(uint8_t mask) {
  static const int32_t AGREEING = 4;
  static const int32_t READS = 100;

  return monitors_read_steady(&gpt.portgp, mask, AGREEING, READS);
}

/* Clamps an EFC to 0 to 1048560, reporting each clamp, and the first
 * value back in range (0x363f6). */
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
 * (0x36478). */
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

/* Checks the internal 1PPS by its count since the last check, from the
 * second call that may on, reporting it dead or back; returns whether
 * it checked (0x36afa).  While internal_pps_disabled is set every count
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
 * (0x36ae6). */
void internal_pps_monitor_reset(void) {
  internal_pps_disabled = 0;
  internal_pps_dead = 0;
  internal_pps_started = 0;
}

/* Clears the GPS 1PPS monitor's state: the 58503A's, and the reference
 * and external 1PPS' dead flags, gps_pps_checked and gps_pps_reset_long
 * (0x36892). */
void gps_pps_monitor_reset(void) {
  gps_pps_skipped_checks = 0;
  gps_pps_unconfirmed = 0;
  gps_pps_unset_flag = 0;
  gps_pps_dead = 0;
  gps_reference_pps_dead = 0;
  external_pps_dead = 0;
  gps_pps_checked = 0;
  gps_pps_reset_long = 0;
  gps_pps_counting = 0;
}

/* Checks the GPS 1PPS by the pulses the GPT has counted, reporting it
 * dead, at the wrong frequency, or back, into gps_reference_pps_dead,
 * and the external 1PPS dead as return_one gives it, into
 * external_pps_dead; gps_pps_dead is the one the loop takes, the external
 * while pll.external_reference.  Returns 0 when it could not check
 * (0x368ca).  Unlike the Z3805A's, a GPS reference whose state is not
 * valid, questionable clearing or assessing marks it dead once, until
 * gps_pps_unset_flag is cleared; and a good short gate while the
 * reference is questionable (its valid 2) brings it back at once and
 * sets the gate to 3, which the step of 2 then never takes to 10, so
 * that while the reference stays questionable the long gate's frequency
 * test never runs (0x36a04). */
uint32_t gps_pps_check(void) {
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
  /* The gate a good short gate sets while the reference is questionable
   * (0x369e0, 0x36a04). */
  static const uint8_t PPS_GATE_RESTART = 3;
  uint8_t checked = 1;
  bool short_gate_bad = false;
  uint8_t dead = gps_reference_pps_dead;

  if (gps.pps_check_disabled) {
    gps_pps_unconfirmed = 1;
    gps_pps_skipped_checks = 0;
    dead = 1;
    gpt.pacnt = 0;
    gps_pps_gate = 0;
    gps_pps_last_count = 0;
  } else if (!gps_pps_unset_flag && gps.references[0].state != GPS_SYNC_VALID &&
             gps.references[0].state != GPS_SYNC_QUESTIONABLE_CLEARING &&
             gps.references[0].state != GPS_SYNC_ASSESSING) {
    gps_pps_unset_flag = 1;
    dead = 1;
    gpt.pacnt = 0;
    gps_pps_gate = 0;
    gps_pps_last_count = 0;
  } else if (gps_pps_unconfirmed && gps_pps_skipped_checks < PPS_SKIPS) {
    gps_pps_skipped_checks++;
  } else if (!monitors_pps_checkable() || !gps_pps_counting) {
    gpt.pacnt = 0;
    if (monitors_pps_checkable()) {
      gpt.pactl = PACTL_COUNT_RISING;
      gps_pps_counting = 1;
    }
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
    } else if (gps.references[0].valid == MONITORS_REFERENCE_QUESTIONABLE) {
      if (dead) {
        task_dmessage("GPS 1pps signal is ok\r\n");
        dead = 0;
      }
      gps_pps_unconfirmed = 0;
      gps_pps_unset_flag = 0;
      gps_pps_gate = PPS_GATE_RESTART;
    }
    gps_pps_last_count = pulses;
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
      gps_pps_last_count = 0;
      gps_pps_gate = 0;
    } else if (gps_pps_gate > PPS_GATE) {
      gpt.pacnt = 0;
      gps_pps_last_count = 0;
      gps_pps_gate = 0;
    }
  }
  external_pps_dead = (uint8_t)return_one() == 0;
  gps_pps_checked = 1;
  gps_reference_pps_dead = dead;
  gps_pps_dead = pll.external_reference ? external_pps_dead : dead;
  return checked;
}

/* The output's controller's requests and readers, which the :OUTPut and
 * :ALARm handlers, the holdover stages and the status snapshot call
 * (0x34036 to 0x3414c).  The controller itself, which steps
 * output_control_state on these requests, is output_control_step, and
 * output_status_update sets the output's status from it, below. */

/* How long output_active_request's request stands, in uptime seconds
 * (0x34070). */
static const uint32_t OUTPUT_ACTIVE_REQUEST_SECONDS = 2;

/* A request's results: taken, or refused, which the handlers report as
 * a settings conflict. */
static const uint32_t OUTPUT_REQUEST_TAKEN = 0;
static const uint32_t OUTPUT_REQUEST_REFUSED = 1;

/* output_status' bits, the word output_status_update writes to the
 * output latch too (0x34c32 to 0x34dfc), the first five and bit 6 as
 * output_state_get reads them (0x340fa), the rest by what sets each. */
static const uint16_t OUTPUT_STATUS_ACTIVE = 1 << 0;  /* switch word bit 0 */
static const uint16_t OUTPUT_STATUS_STANDBY = 1 << 1; /* switch word bit 1 */
static const uint16_t OUTPUT_STATUS_UNLOCKED = 1 << 2;
static const uint16_t OUTPUT_STATUS_BLOCKED = 1 << 3; /* blocked or extracted */
static const uint16_t OUTPUT_STATUS_GPS_VALID = 1 << 4;
static const uint16_t OUTPUT_STATUS_HOLDOVER = 1 << 5;
static const uint16_t OUTPUT_STATUS_ALARM = 1 << 6;
static const uint16_t OUTPUT_STATUS_OPERATION_ALARM = 1 << 7;
static const uint16_t OUTPUT_STATUS_POWER_MISMATCH = 1 << 8;
static const uint16_t OUTPUT_STATUS_NO_OPERATION_ALARM = 1 << 9;
static const uint16_t OUTPUT_STATUS_ALWAYS = 1 << 10;
static const uint16_t OUTPUT_STATUS_NO_HARDWARE_FAULT = 1 << 11;
/* unlocked, active or standby, but for an :OUTPut:INACtive */
static const uint16_t OUTPUT_STATUS_SERVING = 1 << 12;
static const uint16_t OUTPUT_STATUS_STANDBY_READY = 1 << 13;
/* blocked or extracted, or an :OUTPut:INACtive */
static const uint16_t OUTPUT_STATUS_INACTIVE = 1 << 14;
static const uint16_t OUTPUT_STATUS_ACTIVE_REQUESTED = 1 << 15;

/* :OUTPut:ACTive:DISable's, and the switch word's bit 8's (0x34036). */
void output_active_disable(void) {
  output_active_enabled = 0;
  output_active_disabled = 1;
}

/* :OUTPut:ACTive:ENABle's (0x34046). */
void output_active_enable(void) {
  output_active_disabled = 0;
  output_active_enabled = 1;
}

/* :OUTPut:ACTive's: asks the controller to make the output active, for
 * OUTPUT_ACTIVE_REQUEST_SECONDS, in standby or active alone (0x34056). */
uint32_t output_active_request(void) {
  if (output_control_state != OUTPUT_CONTROL_STANDBY &&
      output_control_state != OUTPUT_CONTROL_ACTIVE) {
    return OUTPUT_REQUEST_REFUSED;
  }
  output_active_request_uptime = get_uptime() + OUTPUT_ACTIVE_REQUEST_SECONDS;
  output_active_requested = 1;
  return OUTPUT_REQUEST_TAKEN;
}

/* :OUTPut:INACtive's and the switchover check's: asks the controller to
 * make the output inactive while the switch word's bits 0 and 5 are set
 * and bit 7 clear (0x34088). */
uint32_t output_inactive_request(void) {
  if (switch_word_bit_0 == 0 || switch_word_bit_5 == 0 ||
      switch_word_bit_7 != 0) {
    return OUTPUT_REQUEST_REFUSED;
  }
  output_inactive_requested = 1;
  return OUTPUT_REQUEST_TAKEN;
}

/* output_inactive_request with the switch word's bit 11 for bit 5, the
 * holdover stages' (0x340ae). */
uint32_t output_inactive_request_bit_11(void) {
  if (switch_word_bit_0 == 0 || switch_word_bit_11 == 0 ||
      switch_word_bit_7 != 0) {
    return OUTPUT_REQUEST_REFUSED;
  }
  output_inactive_requested = 1;
  return OUTPUT_REQUEST_TAKEN;
}

/* :DIAGnostic:OUTPut's: asks the controller to turn the output on or off
 * (0x340d4). */
void output_diagnostic_set(uint8_t on) {
  if (on != 0) {
    output_diagnostic_off = 0;
    output_diagnostic_on = 1;
  } else {
    output_diagnostic_on = 0;
    output_diagnostic_off = 1;
  }
}

/* The output's state from output_status' bits, the alarm first
 * (0x340fa). */
output_state output_state_get(void) {
  if ((output_status & OUTPUT_STATUS_ALARM) != 0) {
    return OUTPUT_ALARM;
  }
  if ((output_status & OUTPUT_STATUS_ACTIVE) != 0) {
    return OUTPUT_ACTIVE;
  }
  if ((output_status & OUTPUT_STATUS_STANDBY) != 0) {
    return OUTPUT_STANDBY;
  }
  if ((output_status & OUTPUT_STATUS_UNLOCKED) == 0 &&
      (output_status & OUTPUT_STATUS_BLOCKED) != 0) {
    return OUTPUT_BLOCKED;
  }
  return OUTPUT_UNLOCKED;
}

/* :ALARm:HARDware?'s word (0x3413c). */
uint16_t alarm_hardware_get(void) { return alarm_hardware; }

/* :ALARm:OPERation?'s word (0x34144). */
uint16_t alarm_operation_get(void) { return alarm_operation; }

/* :OUTPut:INACtive?'s cause (0x3414c). */
uint16_t output_inactive_cause_get(void) { return output_inactive_cause; }

/* The pForth console's s3 words' routines (0x33f02 to 0x33f5c): a test
 * mode in which output_status_update writes s3_output in place of its
 * own and two of the DUART's output bits follow the words.  The bits the
 * DUART's output port drives here, named by the words that move them. */
static const uint8_t MONITORS_OUTPUT_OP1 = 0x02;
static const uint8_t MONITORS_OUTPUT_GPS_AVAILABLE = 0x04; /* OP2 */
static const uint8_t MONITORS_OUTPUT_HARDWARE_FAIL = 0x20; /* OP5 */

/* s3_testmode's (0x33f02). */
void s3_test_mode_set(uint8_t on) { s3_test_mode = on; }

/* s3_out_reg's (0x33f0c). */
void s3_output_set(uint16_t value) { s3_output = value; }

/* s3_hw_fail's: OP5, in the test mode alone (0x33f16). */
void s3_hardware_fail_set(char on) {
  if (s3_test_mode != 0) {
    duart_output_bit_set(MONITORS_OUTPUT_HARDWARE_FAIL, on != 0);
  }
}

/* s3_gps_avail's: OP2, in the test mode alone (0x33f3a). */
void s3_gps_available_set(char on) {
  if (s3_test_mode != 0) {
    duart_output_bit_set(MONITORS_OUTPUT_GPS_AVAILABLE, on != 0);
  }
}

/* standby_from_unlock's bounds on |interval_mean|, in seconds, and the
 * passes within the first it takes (0x33fa4, 0x33fc0, 0x33ff6). */
static const float MONITORS_STANDBY_MEAN_WITHIN = 4e-9f;
static const float MONITORS_STANDBY_MEAN_PAST = 5e-9f;
static const float MONITORS_STANDBY_PASSES = 700.0f;

/* Whether the output may go from unlocked to standby: in n3 lock pll's
 * pll state, once 700 passes have found the interval's mean within 4 ns,
 * until it is past 5 ns, its last answer between the two; each call is
 * a pass (docs/firmware/quirks.md) (0x33f5e). */
uint8_t standby_from_unlock(void) {
  const uint8_t ready = standby_from_unlock_ready;
  const float mean = float_absolute_on_stack(interval_mean);

  if (pll.n3_lock_state != PLL_N3_LOCK_PLL ||
      pll.stage != PLL_STAGE_N3_LOCK_PLL || (uint8_t)return_one() == 0) {
    standby_from_unlock_ready = 0;
    return 0;
  }
  if (float_compare(MONITORS_STANDBY_MEAN_WITHIN, mean) > 0) {
    if (float_compare(unsigned_to_float(standby_from_unlock_count),
                      MONITORS_STANDBY_PASSES) < 0) {
      standby_from_unlock_count++;
      return 0;
    }
    if (standby_from_unlock_ready == 0 && debug_enabled != 0) {
      log_write("STBY from UNLOCK");
    }
    standby_from_unlock_ready = 1;
    return 1;
  }
  if (float_compare(mean, MONITORS_STANDBY_MEAN_PAST) <= 0) {
    return ready;
  }
  if (standby_from_unlock_ready == 1 && debug_enabled != 0) {
    log_write("UNLOCK from STBY");
  }
  standby_from_unlock_count = 0;
  standby_from_unlock_ready = 0;
  return 0;
}

/* :DIAGnostic:INPut:DATA?'s word: the switch word's bits 7, 6, 5, 4, 11,
 * 8 and 10, read once, as bits 0 to 6 (0x34154). */
uint16_t input_data_get(void) {
  static const switch_word_bit GATHERED[] = {
      SWITCH_WORD_BIT_7,  SWITCH_WORD_BIT_6,  SWITCH_WORD_BIT_5,
      SWITCH_WORD_BIT_4,  SWITCH_WORD_BIT_11, SWITCH_WORD_BIT_8,
      SWITCH_WORD_BIT_10,
  };
  const uint16_t word = switch_word;
  uint16_t data = 0;

  for (uint16_t bit = 0; bit < sizeof GATHERED / sizeof GATHERED[0]; bit++) {
    if ((word & GATHERED[bit]) != 0) {
      data |= 1 << bit;
    }
  }
  return data;
}

/* status_hardware_condition's bits, as the alarms' updates read them
 * (0x342aa to 0x34318, 0x343b0 to 0x343e6); the screen names the parts
 * they are (screen.c, SCREEN_HEALTH_PARTS). */
static const uint16_t MONITORS_CONDITION_SELFTEST = 0x0001;
static const uint16_t MONITORS_CONDITION_POWER = 0x602e;
static const uint16_t MONITORS_CONDITION_OPERATION_6 = 0x0040;
static const uint16_t MONITORS_CONDITION_EFC_LIMIT = 0x0080;
static const uint16_t MONITORS_CONDITION_OPERATION_8 = 0x0100;
static const uint16_t MONITORS_CONDITION_OPERATION_9 = 0x0200;
static const uint16_t MONITORS_CONDITION_OPERATION_11 = 0x0800;
static const uint16_t MONITORS_CONDITION_OCXO = 0x1000;

/* The DUART's input port's bits alarm_hardware_update reads, each low on
 * its fault (0x341f2 to 0x34280). */
static const uint8_t MONITORS_INPUT_19_6608_MHZ = 0x04;
static const uint8_t MONITORS_INPUT_19_8_MHZ = 0x08;
static const uint8_t MONITORS_INPUT_PP2S = 0x10;
static const uint8_t MONITORS_INPUT_10_MHZ = 0x20;

/* alarm_hardware's bits, one a fault, :ALARm:HARDware?'s word, each named
 * by the text alarm_hardware_update logs as it sets it (0x341b8 to
 * 0x3436e). */
static const uint16_t ALARM_HARDWARE_19_6608_MHZ = 1 << 0;
static const uint16_t ALARM_HARDWARE_19_8_MHZ = 1 << 1;
static const uint16_t ALARM_HARDWARE_10_MHZ = 1 << 2;
static const uint16_t ALARM_HARDWARE_PP2S = 1 << 3;
static const uint16_t ALARM_HARDWARE_SELFTEST = 1 << 4;
static const uint16_t ALARM_HARDWARE_POWER = 1 << 5;
static const uint16_t ALARM_HARDWARE_EFC_LIMIT = 1 << 6;
static const uint16_t ALARM_HARDWARE_OCXO = 1 << 7;
static const uint16_t ALARM_HARDWARE_PAL = 1 << 8;

/* alarm_operation's bits, :ALARm:OPERation?'s word, by what
 * alarm_operation_update sets each for (0x3437e to 0x34402). */
static const uint16_t ALARM_OPERATION_ANTENNA = 1 << 0;
static const uint16_t ALARM_OPERATION_HOLDOVER_DURATION = 1 << 1;
static const uint16_t ALARM_OPERATION_CONDITION_6 = 1 << 2;
static const uint16_t ALARM_OPERATION_CONDITION_9 = 1 << 3;
static const uint16_t ALARM_OPERATION_CONDITION_8 = 1 << 4;
static const uint16_t ALARM_OPERATION_CONDITION_11 = 1 << 5;
static const uint16_t ALARM_OPERATION_SEATING = 1 << 6;

/* alarm_hardware's faults but the OCXO's, each with the text
 * alarm_hardware_update logs as it first sets it, in the order it
 * checks them (0x341fa to 0x34304). */
typedef struct monitors_hardware_alarm {
  uint16_t alarm;
  const char* text;
} monitors_hardware_alarm;

static const monitors_hardware_alarm MONITORS_HARDWARE_ALARMS[] = {
    {ALARM_HARDWARE_19_6608_MHZ, "19.6608 MHz Fail"},
    {ALARM_HARDWARE_19_8_MHZ, "19.8 MHz Fail"},
    {ALARM_HARDWARE_10_MHZ, "10 MHz Fail"},
    {ALARM_HARDWARE_PAL, "PAL Fail"},
    {ALARM_HARDWARE_PP2S, "PP2S Fail"},
    {ALARM_HARDWARE_SELFTEST, "Selftest Fail"},
    {ALARM_HARDWARE_POWER, "Power Fail"},
    {ALARM_HARDWARE_EFC_LIMIT, "EFC Limit Fail"},
};

/* The hardware's faults into alarm_hardware, each logged the first time,
 * 1 for any: the clocks, checked while the loop runs, which clears OP1,
 * the PAL among them with the switch word's bit 3 set and no clock
 * failed; the PP2S past powerup recovery; the self-test, power and EFC
 * limit conditions; and the OCXO, the condition's or, with the switch
 * word's bit 12, the puck not alive (0x341b8).  Its logs, kernel calls,
 * come before the OCXO's reads of the switch word, as the image's do. */
uint8_t alarm_hardware_update(void) {
  const uint16_t condition = status_hardware_condition();
  const uint8_t input = duart_input_port_read();
  uint16_t faults = 0;

  if (pll.loop_running != 0) {
    duart_output_bit_set(MONITORS_OUTPUT_OP1, 0);
    if ((input & MONITORS_INPUT_19_6608_MHZ) == 0) {
      faults |= ALARM_HARDWARE_19_6608_MHZ;
    }
    if ((input & MONITORS_INPUT_19_8_MHZ) == 0) {
      faults |= ALARM_HARDWARE_19_8_MHZ;
    }
    if ((input & MONITORS_INPUT_10_MHZ) == 0) {
      faults |= ALARM_HARDWARE_10_MHZ;
    }
    if (switch_word_bit_3 != 0 && faults == 0) {
      faults |= ALARM_HARDWARE_PAL;
    }
  }
  if ((input & MONITORS_INPUT_PP2S) == 0 &&
      (int8_t)pll.stage > PLL_STAGE_POWERUP_RECOVERY) {
    faults |= ALARM_HARDWARE_PP2S;
  }
  if ((condition & MONITORS_CONDITION_SELFTEST) != 0) {
    faults |= ALARM_HARDWARE_SELFTEST;
  }
  if ((condition & MONITORS_CONDITION_POWER) != 0) {
    power_ok = 0;
    faults |= ALARM_HARDWARE_POWER;
  }
  if ((condition & MONITORS_CONDITION_EFC_LIMIT) != 0) {
    faults |= ALARM_HARDWARE_EFC_LIMIT;
  }
  for (uint16_t i = 0;
       i < sizeof MONITORS_HARDWARE_ALARMS / sizeof MONITORS_HARDWARE_ALARMS[0];
       i++) {
    const monitors_hardware_alarm* alarm = &MONITORS_HARDWARE_ALARMS[i];

    if ((faults & alarm->alarm) != 0 && (alarm_hardware & alarm->alarm) == 0) {
      alarm_hardware |= alarm->alarm;
      log_write(alarm->text);
    }
  }
  if ((condition & MONITORS_CONDITION_OCXO) != 0 ||
      ((switch_word & SWITCH_WORD_BIT_12) != 0 && puck_alive() == 0)) {
    if ((alarm_hardware & ALARM_HARDWARE_OCXO) == 0) {
      if ((condition & MONITORS_CONDITION_OCXO) != 0) {
        log_write("Internal OCXO Fail");
      }
      if ((switch_word & SWITCH_WORD_BIT_12) != 0 && puck_alive() == 0) {
        log_write("OCXO Communication Fail");
      }
      alarm_hardware |= ALARM_HARDWARE_OCXO;
    }
    faults |= ALARM_HARDWARE_OCXO;
  }
  return faults != 0;
}

/* The operation's faults into alarm_operation, 1 for any (0x3437e). */
uint8_t alarm_operation_update(void) {
  const uint16_t condition = status_hardware_condition();
  uint16_t alarms = 0;

  if (hmon_check_antenna_current() != 0) {
    alarms |= ALARM_OPERATION_ANTENNA;
  }
  if (holdover_duration_exceeded != 0) {
    alarms |= ALARM_OPERATION_HOLDOVER_DURATION;
  }
  if ((condition & MONITORS_CONDITION_OPERATION_6) != 0) {
    alarms |= ALARM_OPERATION_CONDITION_6;
  }
  if ((condition & MONITORS_CONDITION_OPERATION_9) != 0) {
    alarms |= ALARM_OPERATION_CONDITION_9;
  }
  if ((condition & MONITORS_CONDITION_OPERATION_8) != 0) {
    alarms |= ALARM_OPERATION_CONDITION_8;
  }
  if ((condition & MONITORS_CONDITION_OPERATION_11) != 0) {
    alarms |= ALARM_OPERATION_CONDITION_11;
  }
  if (n3_lock_seating_error() != 0) {
    alarms |= ALARM_OPERATION_SEATING;
  }
  alarm_operation = alarms;
  return alarms != 0;
}

/* 1 while clock_deviation_alarm is set, the snapshot's and the ES
 * message's (0x34412). */
uint8_t clock_deviation_alarm_get(void) { return clock_deviation_alarm != 0; }

/* es_build's, once it has reported it (0x34422). */
void clock_deviation_alarm_clear(void) {
  if (clock_deviation_alarm != 0) {
    clock_deviation_alarm = 0;
  }
}

/* How long the unlock timer runs before it logs, in uptime seconds
 * (0x34462). */
static const uint32_t MONITORS_UNLOCK_LOG_SECONDS = 300;

/* (0x34432) */
void unlock_timer_stop(void) {
  unlock_timer_stopped = 1;
  unlock_timer_start = 0;
  unlock_timer_logged = 0;
}

/* (0x34448) */
void unlock_timer_restart(void) {
  unlock_timer_start = get_uptime();
  unlock_timer_stopped = 0;
}

/* Logs once, MONITORS_UNLOCK_LOG_SECONDS after the timer's start, while
 * it runs (0x3445c). */
void unlock_timer_check(void) {
  if (get_uptime() >= unlock_timer_start + MONITORS_UNLOCK_LOG_SECONDS &&
      unlock_timer_logged == 0 && unlock_timer_stopped != 1) {
    log_write("UNLOCK greater than 5 minutes");
    unlock_timer_logged = 1;
  }
}

/* switchover_check's bounds: the loop locked a day of uptime, its
 * measured uncertainty past 2e-5, and the switch word's bit 5 clear 300 s
 * (0x344de, 0x344f2, 0x34508). */
static const uint32_t MONITORS_SWITCHOVER_LOCKED_SECONDS = 86400;
static const float MONITORS_SWITCHOVER_UNCERTAINTY = 2e-5f;
static const uint32_t MONITORS_SWITCHOVER_BIT_5_SECONDS = 300;

/* pll.measured_uncertainty_valid's value for valid, which it tests
 * (0x344e6). */
static const uint8_t MONITORS_UNCERTAINTY_VALID = 1;

/* The switchover check: with the loop locked a day in normal or start-up
 * pll and its measured uncertainty past 2e-5, arms once the switch
 * word's bit 5 has been clear 300 s and, armed, the bit set again, asks
 * the output inactive; else marks the loop's lock from now (0x3449a). */
uint8_t switchover_check(void) {
  const uint32_t uptime = get_uptime();

  if (switch_word_bit_5 != 0) {
    standby_unlock_ticks = uptime;
  }
  if (pll.stage != PLL_STAGE_NORMAL_PLL && pll.stage != PLL_STAGE_STARTUP_PLL) {
    locked_ticks = uptime;
  } else if (uptime - locked_ticks > MONITORS_SWITCHOVER_LOCKED_SECONDS &&
             pll.measured_uncertainty_valid == MONITORS_UNCERTAINTY_VALID &&
             float_compare(pll.measured_uncertainty,
                           MONITORS_SWITCHOVER_UNCERTAINTY) > 0) {
    if (uptime - standby_unlock_ticks > MONITORS_SWITCHOVER_BIT_5_SECONDS) {
      switchover_armed = 1;
      return 0;
    }
    if (switchover_armed != 1 || switch_word_bit_5 == 0) {
      return 0;
    }
    log_write("switchover ACT unstable");
    output_inactive_request();
  }
  switchover_armed = 0;
  return 0;
}

/* pr_bad_act: switchover_check's ticks and the measured uncertainty
 * (0x34544). */
void diag_pr_bad_act(void) {
  printf("ticks = %d\r\n", get_uptime());
  printf("locked_ticks = %d\r\n", locked_ticks);
  printf("standby_unlck_ticks = %d\r\n", standby_unlock_ticks);
  printf("tunc:meas = %f\r\n", float_to_double(pll.measured_uncertainty));
  printf("tunc:meas valid = %d\r\n", pll.measured_uncertainty_valid);
}

/* The output controller's reset, backp_task's start: OP1 set (0x345ae). */
void output_control_reset(void) {
  switchover_armed = 0;
  standby_unlock_ticks = 0;
  locked_ticks = 0;
  n3_lock.seating_error = 0;
  standby_from_unlock_count = 0;
  s3_output = 0;
  s3_test_mode = 0;
  ptest_blink = 1;
  *PLL_CLOCK_FROM_GPS_HELD = 0;
  porte_extractor_bit = 0;
  standby_from_unlock_ready = 0;
  output_control_unused_byte = 0;
  output_control_unused_word = 0;
  power_ok = 1;
  clock_deviation_alarm = 0;
  output_active_requested = 0;
  output_inactive_requested = 0;
  alarm_hardware = 0;
  alarm_operation = 0;
  output_inactive_cause = OUTPUT_INACTIVE_NONE;
  switch_word_bit_4_copy = 0;
  duart_input_0_state = 1;
  duart_input_1_state = 1;
  output_control_state = OUTPUT_CONTROL_WARMUP;
  output_warmup_forced = 0;
  output_diagnostic_on = 0;
  output_diagnostic_off = 0;
  output_active_disabled = 0;
  output_active_enabled = 0;
  duart_output_bit_set(MONITORS_OUTPUT_OP1, 1);
}

/* PORTE's bit switch_word_decode takes for porte_extractor_bit
 * (0x34686). */
static const uint8_t MONITORS_PORTE_EXTRACTOR = 0x08;

/* The switch word's bits 4 and 5 both set, which clear the bits from 4
 * to 8 and 11 (0x3469e). */
static const uint16_t MONITORS_SWITCH_BITS_4_5 =
    SWITCH_WORD_BIT_4 | SWITCH_WORD_BIT_5;

/* The DUART's input port's bits switch_word_decode takes, each low
 * (0x34790 to 0x347ce). */
static const uint8_t MONITORS_INPUT_2 = 0x04;
static const uint8_t MONITORS_INPUT_3 = 0x08;
static const uint8_t MONITORS_INPUT_4 = 0x10;
static const uint8_t MONITORS_INPUT_5 = 0x20;

/* A bit of the switch word's high byte as its decoded cell keeps it,
 * masked in place in the byte (0x346e8 to 0x3477e). */
static uint8_t monitors_high_bit(uint16_t word, switch_word_bit bit) {
  return (word & bit) >> 8;
}

/* Decodes the switch word into its cells, the PORTE extractor bit and
 * four of the DUART's inputs, each pass of backp_task (0x34676). */
void switch_word_decode(void) {
  porte_extractor_bit = sim.porte0 & MONITORS_PORTE_EXTRACTOR;
  const uint16_t word = switch_word;

  if ((word & MONITORS_SWITCH_BITS_4_5) == MONITORS_SWITCH_BITS_4_5) {
    switch_word_bit_4 = 0;
    switch_word_bit_5 = 0;
    switch_word_bit_6 = 0;
    switch_word_bit_7 = 0;
    switch_word_bit_8 = 0;
    switch_word_bit_11 = 0;
  } else {
    switch_word_bit_4 = word & SWITCH_WORD_BIT_4;
    switch_word_bit_5 = word & SWITCH_WORD_BIT_5;
    switch_word_bit_6 = word & SWITCH_WORD_BIT_6;
    switch_word_bit_7 = word & SWITCH_WORD_BIT_7;
    switch_word_bit_8 = monitors_high_bit(word, SWITCH_WORD_BIT_8);
    switch_word_bit_11 = monitors_high_bit(word, SWITCH_WORD_BIT_11);
  }
  switch_word_bit_4_copy = switch_word_bit_4;
  if (switch_word_bit_8 != 0) {
    output_active_disable();
  }
  switch_word_bit_0 = word & SWITCH_WORD_BIT_0;
  switch_word_bit_1 = word & SWITCH_WORD_BIT_1;
  switch_word_bit_2 = word & SWITCH_WORD_BIT_2;
  switch_word_bit_3 = word & SWITCH_WORD_BIT_3;
  switch_word_bit_9 = monitors_high_bit(word, SWITCH_WORD_BIT_9);
  switch_word_bit_10 = monitors_high_bit(word, SWITCH_WORD_BIT_10);
  switch_word_bit_12 = monitors_high_bit(word, SWITCH_WORD_BIT_12);
  switch_word_bit_13 = monitors_high_bit(word, SWITCH_WORD_BIT_13);
  switch_word_bit_14 = monitors_high_bit(word, SWITCH_WORD_BIT_14);
  switch_word_bit_15 = monitors_high_bit(word, SWITCH_WORD_BIT_15);

  const uint8_t input = duart_input_port_read();

  duart_input_2_low = (input & MONITORS_INPUT_2) == 0;
  duart_input_3_low = (input & MONITORS_INPUT_3) == 0;
  if (time_valid() != 0) {
    duart_input_4_low = (input & MONITORS_INPUT_4) == 0;
  }
  duart_input_5_low = (input & MONITORS_INPUT_5) == 0;
}

/* How long a block holds after it began, in uptime seconds (0x34bb6). */
static const uint32_t MONITORS_BLOCK_SECONDS = 60;

/* Whether the loop's stage lets the output out of warm-up: start-up,
 * n3 lock or normal pll, or a holdover (0x348a2, 0x34992, 0x34c10). */
static bool monitors_stage_serving(void) {
  return (int8_t)pll.stage >= PLL_STAGE_HOLDOVER &&
         (int8_t)pll.stage <= PLL_STAGE_NORMAL_PLL;
}

/* Whether :DIAGnostic:OUTPut OFF, or input 0 and the forced warm-up both
 * clear, send the output to warm-up (0x3497c, 0x34b10). */
static bool monitors_warmup_wanted(void) {
  return output_diagnostic_off != 0 ||
         (output_warmup_forced == 0 && duart_input_0_state == 0);
}

/* The output's controller's warm-up: out of it to unlocked for
 * :DIAGnostic:OUTPut ON, input 0 and the forced warm-up both clear, or
 * input 1 set, the output not disabled and the stage serving; blocked
 * for input 1 clear or the output disabled (0x3484a). */
static void monitors_from_warmup(void) {
  if (porte_extractor_bit == 0) {
    output_control_state = OUTPUT_CONTROL_EXTRACTED;
  }
  if (output_warmup_forced == 1 && duart_input_0_state != 0) {
    output_warmup_forced = 0;
  }
  if (output_diagnostic_on != 0 ||
      (duart_input_0_state == 0 && output_warmup_forced == 0)) {
    output_warmup_forced = 1;
    output_diagnostic_on = 0;
    output_control_state = OUTPUT_CONTROL_UNLOCKED;
  } else if (duart_input_1_state == 0 || output_active_disabled != 0) {
    output_control_state = OUTPUT_CONTROL_BLOCKED;
  } else if (monitors_stage_serving()) {
    output_control_state = OUTPUT_CONTROL_UNLOCKED;
  }
}

/* From unlocked: to active with the switch word's bit 0 set, else to
 * standby once standby_from_unlock allows; blocked for input 1 clear or
 * the output disabled (0x348ba). */
static void monitors_from_unlocked(void) {
  switchover_check();
  if (porte_extractor_bit == 0) {
    output_control_state = OUTPUT_CONTROL_EXTRACTED;
  } else if (duart_input_1_state == 0 || output_active_disabled != 0) {
    output_control_state = OUTPUT_CONTROL_BLOCKED;
  } else if (switch_word_bit_0 != 0) {
    clock_deviation_alarm = 1;
    log_write("ACT from UNLOCK");
    output_control_state = OUTPUT_CONTROL_ACTIVE;
  } else if (standby_from_unlock() != 0) {
    output_control_state = OUTPUT_CONTROL_STANDBY;
  }
}

/* From standby: to active with the switch word's bit 0 set, a time
 * deviation with output_status' standby bit clear or the switch word's
 * bit 7 set; then to warm-up as monitors_warmup_wanted says, with the
 * stage before a holdover (0x34ab0). */
static void monitors_from_standby(void) {
  switchover_check();
  if (porte_extractor_bit == 0) {
    output_control_state = OUTPUT_CONTROL_EXTRACTED;
  } else if (duart_input_1_state == 0 || output_active_disabled != 0) {
    output_control_state = OUTPUT_CONTROL_BLOCKED;
  } else if (switch_word_bit_0 != 0) {
    if ((output_status & OUTPUT_STATUS_STANDBY) == 0 ||
        switch_word_bit_7 != 0) {
      log_write("ACT from STBY time deviation");
      clock_deviation_alarm = 1;
    } else {
      log_write("ACT from STBY");
    }
    output_control_state = OUTPUT_CONTROL_ACTIVE;
  }
  if (monitors_warmup_wanted() && (int8_t)pll.stage < PLL_STAGE_HOLDOVER) {
    if (switch_word_bit_5 == 0) {
      clock_deviation_alarm = 1;
    }
    output_warmup_forced = 1;
    output_diagnostic_off = 0;
    output_control_state = OUTPUT_CONTROL_WARMUP;
  }
}

/* Leaves active for cause, setting clock_deviation_alarm with the
 * switch word's bit 5 clear. */
static void monitors_clock_deviation_unless_bit_5(void) {
  if (switch_word_bit_5 == 0) {
    clock_deviation_alarm = 1;
  }
}

/* From active: blocked when extracted; asks the output inactive in a
 * holdover past :OUTPut:ACTive:HOLDover:DURation:THReshold with the
 * switch word's bit 11 set and 7 clear; to warm-up as
 * monitors_warmup_wanted says before a holdover; blocked for input 1
 * clear with bit 5 set or the output disabled, by another card with bit
 * 8 set, else the user; to unlocked, the standby the debug texts name,
 * with bit 0 clear, for the holdover's request, another card or a time
 * deviation, or with it set on an :OUTPut:INACtive; and a hardware fault
 * on any pass (0x3490c). */
static void monitors_from_active(void) {
  switchover_check();
  if (output_warmup_forced == 1 && duart_input_0_state != 0) {
    output_warmup_forced = 0;
  }
  if (porte_extractor_bit == 0) {
    monitors_clock_deviation_unless_bit_5();
    log_write("BLK from ACT, extractor");
    output_control_state = OUTPUT_CONTROL_EXTRACTED;
    output_inactive_cause = OUTPUT_INACTIVE_EXTRACTOR;
  } else if (pll.holdover_duration >
                 (uint32_t)nv_settings.active_holdover_threshold &&
             pll.stage == PLL_STAGE_HOLDOVER && switch_word_bit_11 != 0 &&
             switch_word_bit_7 == 0) {
    output_inactive_cause = OUTPUT_INACTIVE_HOLD;
    output_inactive_request();
  } else if (monitors_warmup_wanted() &&
             (int8_t)pll.stage < PLL_STAGE_HOLDOVER) {
    monitors_clock_deviation_unless_bit_5();
    log_write("WARMUP from ACT, clock off");
    output_inactive_cause = OUTPUT_INACTIVE_MANUAL;
    output_warmup_forced = 1;
    output_diagnostic_off = 0;
    output_control_state = OUTPUT_CONTROL_WARMUP;
  } else if ((duart_input_1_state == 0 && switch_word_bit_5 != 0) ||
             output_active_disabled != 0) {
    monitors_clock_deviation_unless_bit_5();
    if (switch_word_bit_8 != 0) {
      output_inactive_cause = OUTPUT_INACTIVE_EXTERNAL;
      log_write("BLK from ACT, other card");
    } else {
      output_inactive_cause = OUTPUT_INACTIVE_MANUAL;
      log_write("BLK from ACT, user");
    }
    output_control_state = OUTPUT_CONTROL_BLOCKED;
  } else if (switch_word_bit_0 == 0) {
    if (output_inactive_cause == OUTPUT_INACTIVE_HOLD) {
      log_write("STBY from ACT, 10 min hold");
    } else if (switch_word_bit_8 != 0) {
      clock_deviation_alarm = 1;
      output_inactive_cause = OUTPUT_INACTIVE_EXTERNAL;
      log_write("STBY from ACT, other card");
    } else {
      clock_deviation_alarm = 1;
      output_inactive_cause = OUTPUT_INACTIVE_TIME_DEVIATION;
      log_write("STBY from ACT, time deviation");
    }
    output_control_state = OUTPUT_CONTROL_UNLOCKED;
  } else if (output_inactive_requested == 1) {
    if (switch_word_bit_5 == 0) {
      clock_deviation_alarm = 1;
      log_write("STBY from ACT, time dev");
    } else {
      log_write("STBY from ACT");
    }
    output_inactive_cause = OUTPUT_INACTIVE_MANUAL;
    output_control_state = OUTPUT_CONTROL_UNLOCKED;
  }
}

/* Out of a block, or extracted, back to warm-up, and on to unlocked with
 * the stage serving (0x34c08). */
static void monitors_block_end(void) {
  output_active_enabled = 0;
  output_control_state = OUTPUT_CONTROL_WARMUP;
  if (monitors_stage_serving()) {
    output_control_state = OUTPUT_CONTROL_UNLOCKED;
  }
}

/* Whether MONITORS_BLOCK_SECONDS have passed since the block began
 * (0x34bb0, 0x34be8). */
static bool monitors_block_over(void) {
  return get_uptime() > redundancy_change_uptime + MONITORS_BLOCK_SECONDS;
}

/* Steps the output's controller, output_control_state, a pass of
 * backp_task, by its jump table (0x34836): each state as its function
 * says, and from any but the hardware fault, which only checks the
 * hardware again, to the hardware fault on alarm_hardware_update's
 * fault, active with OUTPUT_INACTIVE_HARD and clock_deviation_alarm set
 * (0x347e4). */
void output_control_step(void) {
  switch (output_control_state) {
    case OUTPUT_CONTROL_STANDBY:
      monitors_from_standby();
      break;
    case OUTPUT_CONTROL_ACTIVE:
      monitors_from_active();
      if (alarm_hardware_update() != 0) {
        clock_deviation_alarm = 1;
        output_inactive_cause = OUTPUT_INACTIVE_HARD;
        output_control_state = OUTPUT_CONTROL_HARDWARE_FAULT;
      }
      return;
    case OUTPUT_CONTROL_UNLOCKED:
      monitors_from_unlocked();
      break;
    case OUTPUT_CONTROL_HARDWARE_FAULT:
      alarm_hardware_update();
      return;
    case OUTPUT_CONTROL_BLOCKED:
      redundancy_change_uptime = get_uptime();
      if (porte_extractor_bit == 0) {
        output_control_state = OUTPUT_CONTROL_EXTRACTED;
      }
      if (duart_input_1_state != 0 || output_active_disabled != 0) {
        output_control_state = OUTPUT_CONTROL_BLOCK_HOLD;
      }
      break;
    case OUTPUT_CONTROL_BLOCK_HOLD:
      if (porte_extractor_bit == 0) {
        output_control_state = OUTPUT_CONTROL_EXTRACTED;
      }
      if (duart_input_1_state == 0 || output_active_enabled != 0 ||
          (output_active_disabled == 0 && monitors_block_over())) {
        output_active_disabled = 0;
        output_control_state = OUTPUT_CONTROL_BLOCK_RELEASE;
      }
      break;
    case OUTPUT_CONTROL_EXTRACTED:
      if (porte_extractor_bit != 0) {
        output_active_disabled = 0;
        monitors_block_end();
      }
      break;
    case OUTPUT_CONTROL_BLOCK_RELEASE:
      if (porte_extractor_bit == 0) {
        output_control_state = OUTPUT_CONTROL_EXTRACTED;
      }
      if (duart_input_1_state != 0 || output_active_enabled != 0 ||
          monitors_block_over()) {
        monitors_block_end();
      }
      break;
    default:
      monitors_from_warmup();
      break;
  }
  if (alarm_hardware_update() != 0) {
    output_control_state = OUTPUT_CONTROL_HARDWARE_FAULT;
  }
}

/* Sets output_status and the output latch from the controller, the
 * switch word, the pp test, the loop and the alarms, a pass of
 * backp_task; in the s3 test mode to s3_output (0x34c32). */
void output_status_update(void) {
  const ptest_state ptest = ptest_state_get();
  uint16_t status = 0;

  if (s3_test_mode != 0) {
    output_status = s3_output;
    output_latch = s3_output;
    return;
  }
  if (output_control_state == OUTPUT_CONTROL_UNLOCKED ||
      output_control_state == OUTPUT_CONTROL_ACTIVE ||
      output_control_state == OUTPUT_CONTROL_STANDBY) {
    status |= OUTPUT_STATUS_SERVING;
  }
  if (switch_word_bit_0 != 0) {
    status |= OUTPUT_STATUS_ACTIVE;
  }
  if (output_control_state == OUTPUT_CONTROL_STANDBY &&
      standby_from_unlock() != 0) {
    status |= OUTPUT_STATUS_STANDBY_READY;
  }
  if (ptest == PTEST_STATE_OFF) {
    if (output_control_state == OUTPUT_CONTROL_UNLOCKED) {
      status |= OUTPUT_STATUS_UNLOCKED;
    }
    if (switch_word_bit_1 != 0) {
      status |= OUTPUT_STATUS_STANDBY;
      unlock_timer_restart();
    } else if (output_control_state == OUTPUT_CONTROL_STANDBY) {
      status |= OUTPUT_STATUS_UNLOCKED;
      unlock_timer_check();
    } else {
      unlock_timer_stop();
    }
    if (output_control_state == OUTPUT_CONTROL_WARMUP ||
        output_control_state == OUTPUT_CONTROL_WARMUP_1) {
      status |= OUTPUT_STATUS_UNLOCKED;
    }
  } else {
    if (get_uptime() != ptest_blink_uptime) {
      ptest_blink_uptime = get_uptime();
      ptest_blink = ptest_blink == 0;
    }
    if (ptest_blink == 1) {
      status |= OUTPUT_STATUS_UNLOCKED;
      if (ptest == PTEST_STATE_PASSED) {
        status |= OUTPUT_STATUS_STANDBY;
      }
      if (ptest == PTEST_STATE_FAILED) {
        status |= OUTPUT_STATUS_ALARM;
      }
    }
  }
  if (output_control_state >= OUTPUT_CONTROL_BLOCKED &&
      output_control_state <= OUTPUT_CONTROL_BLOCK_RELEASE) {
    status |= OUTPUT_STATUS_INACTIVE | OUTPUT_STATUS_BLOCKED;
  }
  uint8_t gps_available = 0;

  if (gps.references[0].valid != GPS_REFERENCE_INVALID) {
    status |= OUTPUT_STATUS_GPS_VALID;
    gps_available = pll.stage == PLL_STAGE_NORMAL_PLL ||
                    pll.stage == PLL_STAGE_STARTUP_PLL ||
                    pll.stage == PLL_STAGE_N3_LOCK_PLL;
  }
  duart_output_bit_set(MONITORS_OUTPUT_GPS_AVAILABLE, gps_available);
  if (pll.stage == PLL_STAGE_HOLDOVER ||
      pll.stage == PLL_STAGE_HOLDOVER_RECOVERY) {
    status |= OUTPUT_STATUS_HOLDOVER;
  }
  if (output_control_state == OUTPUT_CONTROL_HARDWARE_FAULT) {
    status |= OUTPUT_STATUS_ALARM;
  } else {
    status |= OUTPUT_STATUS_NO_HARDWARE_FAULT;
  }
  if (alarm_operation_update() != 0) {
    status |= OUTPUT_STATUS_OPERATION_ALARM;
  } else {
    status |= OUTPUT_STATUS_NO_OPERATION_ALARM;
  }
  if ((power_ok != 0) != ((switch_word & SWITCH_WORD_BIT_14) != 0)) {
    status |= OUTPUT_STATUS_POWER_MISMATCH;
  }
  status |= OUTPUT_STATUS_ALWAYS;
  if (output_inactive_requested == 1) {
    output_inactive_requested = 0;
    status = (status | OUTPUT_STATUS_INACTIVE) & ~OUTPUT_STATUS_SERVING;
  }
  if (output_active_requested == 1) {
    if (get_uptime() > output_active_request_uptime) {
      output_active_requested = 0;
    }
    status |= OUTPUT_STATUS_ACTIVE_REQUESTED;
  }
  output_status = status;
  output_latch = status;
}

/* The backp task: the output's controller's reset, then each tick the
 * switch word decoded, the controller stepped, the output's status set
 * and the ES message's errors updated, watchdog_bp counted for each
 * tick's event (0x34e1e). */
void backp_task(void) {
  static const uint32_t TICK_EVENT = 1;

  output_control_reset();
  for (;;) {
    if ((uint8_t)psos_wait_v(TICK_EVENT, 0, 0, NULL) == 0) {
      watchdog_bp++;
    }
    switch_word_decode();
    output_control_step();
    output_status_update();
    es_error_update();
  }
}

/* The oven monitor's reading above which the OCXO is warm, without the
 * puck (0x33c5c). */
static const float MONITORS_OCXO_WARM = 0.9f;

/* Whether the OCXO is warm: the puck's, with the switch word's bit 12
 * set, else the oven current monitor's third sample past 0.9 (0x33c3a). */
uint8_t ocxo_warm(void) {
  if ((switch_word & SWITCH_WORD_BIT_12) != 0) {
    return puck_warm() != 0;
  }
  return float_compare(adc_monitors[ADC_OVEN_CURRENT].samples[2],
                       MONITORS_OCXO_WARM) > 0;
}
