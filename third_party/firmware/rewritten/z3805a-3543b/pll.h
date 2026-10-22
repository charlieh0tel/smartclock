/* What the disciplining loop's files share (pll_aging.c,
 * pll_gps_control.c, pll_stages.c, pllp.c; docs/firmware/loop.md): the
 * aging fit's states, the GPS sequences' acknowledgments and states,
 * a stage's result and the power-up stage's sub-states.  The exported
 * C includes the hand-written headers after its own declarations and
 * before the hand-written .c files, so every one of them can use it. */

/* efc_history_update's state, pll.aging_state (structures.md); also
 * read by the loop's stages in pll_stages.c. */
enum {
  PLL_AGING_START = 0,      /* stores the EFC as the fit, HQ its first */
  PLL_AGING_SETTLING = 1,   /* waits for startup_pll's loop_settled */
  PLL_AGING_IDLE = 2,       /* nothing sets it; nothing is done in it */
  PLL_AGING_COLLECTING = 3, /* sums the EFC into the history */
};

/* The history's entries, a ring indexed modulo its size. */
#define PLL_AGING_ENTRIES 64

static const uint32_t PLL_AGING_INDEX_MASK = PLL_AGING_ENTRIES - 1;

/* Whether a float is zero of either sign, as the image tests its bits
 * (lsl.l #1, 0x42256). */
static bool pll_aging_is_zero(float value) {
  uint32_t bits;

  memcpy(&bits, &value, sizeof bits);
  return bits << 1 == 0;
}

/* Which sequence waits for the acknowledgment of the request it sent,
 * gps_ack_awaited, gpsx_send's last argument; pllp_task's reply handling
 * (pllp.c) tells the command's and the self-test's apart. */
enum {
  PLL_GPS_CONTROL_AWAITED_NONE = 0,
  PLL_GPS_CONTROL_AWAITED_INIT = 1,
  PLL_GPS_CONTROL_AWAITED_LEAP_ACK = 2,
  PLL_GPS_CONTROL_AWAITED_LEAP_CHECK = 3,
  PLL_GPS_CONTROL_AWAITED_COMMAND = 4,
  PLL_GPS_CONTROL_AWAITED_SELFTEST = 5,
};

/* An acknowledgment's code, gps_ack_code, as a reply's result: success,
 * and the one pllp_task gives an acknowledgment that never came. */
static const int8_t PLL_GPS_CONTROL_ACK_OK = 0;

static const int8_t PLL_GPS_CONTROL_ACK_TIMED_OUT = 8;

/* gps_init_sequence's states, pll.gps_init_state, as
 * print_gps_init_state names them: each setting's send, then its
 * acknowledgment, which on a failure sends it again. */
enum {
  PLL_GPS_CONTROL_INIT_START = 0x00,
  PLL_GPS_CONTROL_INIT_ACK = 0x01,
  PLL_GPS_CONTROL_UTC_SEND = 0x02,
  PLL_GPS_CONTROL_UTC_ACK = 0x03,
  PLL_GPS_CONTROL_MASK_ANGLE_SEND = 0x04,
  PLL_GPS_CONTROL_MASK_ANGLE_ACK = 0x05,
  PLL_GPS_CONTROL_ANTENNA_DELAY_SEND = 0x06,
  PLL_GPS_CONTROL_ANTENNA_DELAY_ACK = 0x07,
  PLL_GPS_CONTROL_IGNORE_SEND = 0x08,
  PLL_GPS_CONTROL_IGNORE_ACK = 0x09,
  PLL_GPS_CONTROL_POSITION_SEND = 0x0a,
  PLL_GPS_CONTROL_POSITION_ACK = 0x0b,
  PLL_GPS_CONTROL_HOLD_OFF_SEND = 0x0c,
  PLL_GPS_CONTROL_HOLD_OFF_ACK = 0x0d,
  PLL_GPS_CONTROL_SURVEY = 0x0e,
  PLL_GPS_CONTROL_SURVEY_POSITION_SEND = 0x0f,
  PLL_GPS_CONTROL_SURVEY_POSITION_ACK = 0x10,
  PLL_GPS_CONTROL_INIT_DONE = 0x11,
};

/* gps_leap_ack's states, pll.leap_ack_state: the engine's time mode
 * set to :DIAG:GPS:UTC, switched to the other one to read the GPS-UTC
 * offset from the change, and switched back. */
enum {
  PLL_GPS_CONTROL_LEAP_ACK_IDLE = 0,
  PLL_GPS_CONTROL_LEAP_ACK_CHECK = 1,
  PLL_GPS_CONTROL_LEAP_ACK_MODE_SEND = 2,
  PLL_GPS_CONTROL_LEAP_ACK_MODE_ACK = 3,
  PLL_GPS_CONTROL_LEAP_ACK_SWITCH_SEND = 4,
  PLL_GPS_CONTROL_LEAP_ACK_SWITCH_ACK = 5,
  PLL_GPS_CONTROL_LEAP_ACK_BACK_SEND = 6,
  PLL_GPS_CONTROL_LEAP_ACK_BACK_ACK = 7,
};

/* What a stage gives pllp_task: go on in it, it is done (the next
 * stage), or it failed (holdover, or for power-up a restart). */
enum {
  PLL_STAGES_RUNNING = 0,
  PLL_STAGES_DONE = 1,
  PLL_STAGES_FAILED = 2,
};

/* GPT port GP bit 5, PGP5: the outer oven's enable (docs/firmware/
 * ovens.md). */
static const uint8_t PLL_STAGES_OUTER_OVEN_ENABLE = 0x20;

/* The hold causes a missing GPS reference or a dead internal 1PPS add,
 * and a stage's result with any cause: failed. */
static uint8_t pll_stages_check_causes(uint8_t result) {
  if (gps_reference_good == 0) {
    pll.hold_causes |= HOLD_CAUSE_GPS;
  }
  if (internal_pps_dead != 0) {
    pll.hold_causes |= HOLD_CAUSE_HARDWARE_FAILURE;
  }
  return pll.hold_causes != HOLD_CAUSE_NONE ? PLL_STAGES_FAILED : result;
}

/* The sub-states of powerup, pll.powerup_state, as the status screen's
 * ladder names them (docs/screen-format-strings.md). */
enum {
  PLL_STAGES_POWERUP_START = 0,
  PLL_STAGES_POWERUP_INNER_OVEN = 1,
  PLL_STAGES_POWERUP_OUTER_OVEN = 2,
  PLL_STAGES_POWERUP_WAITING_FOR_GPS = 3,
  PLL_STAGES_POWERUP_COARSE = 4,
  PLL_STAGES_POWERUP_FINE = 5,
  PLL_STAGES_POWERUP_WAITING = 6,
  PLL_STAGES_POWERUP_LEAP_SECONDS = 7,
};
