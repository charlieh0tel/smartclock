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

/* Gives up an awaited acknowledgment: one already in is no longer
 * awaited; one still to come is abandoned, and dropped when it comes. */
static void pllp_abandon_ack(void) {
  if (gps_ack_ready == 0) {
    gps_ack_abandoned = 1;
  } else {
    gps_ack_awaited = GPS_ACK_NONE;
  }
}

/* Does nothing; pllp_task calls it after counter_init (0x45e62). */
void pllp_start_nothing_a(void) {}

/* Does nothing; pllp_task calls it after pllp_start_nothing_a
 * (0x45e64). */
void pllp_start_nothing_b(void) {}

/* Sets the clock from the reference: the GPS time plus the seconds the
 * FPGA's count and the uptime say have passed since pllp_task started,
 * 0 to 59 (0x46488). */
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

/* The status screen's mode, from the stage and pll.manual (0x45d44). */
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
 * second determination (0x45d8c). */
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
 * (0x47242). */
uint32_t leap_ack_pending(void) {
  return pll.leap_ack_state != PLL_LEAP_ACK_IDLE || pll.leap_ack_requested != 0;
}

/* The time code's FFOM, by the stage: 0 locked, 1 settling, 2 in
 * holdover or its recovery, 3 otherwise; always 1 (0x42ac2). */
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
