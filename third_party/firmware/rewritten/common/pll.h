/* What the disciplining loop's files share (pll_aging.c,
 * pll_gps_control.c, pll_reading.c, pll_stages.c, pllp.c, and each
 * image's own; docs/firmware/loop.md): the aging fit's states, event,
 * failure and rms, the GPS sequences' acknowledgments and states, a
 * stage's result and the power-up stage's sub-states, whether a 1PPS is
 * dead, a float negated and fine sync's steps too far.  The exported
 * C includes the hand-written headers after its own declarations and
 * before the hand-written .c files, so every one of them can use it. */

/* The EFC's top, its 20 bits' (efc_clamp, efc_clamp), and its mid-scale,
 * the lifetime record's default. */
static const float PLL_EFC_MOST = 1048560.0f;

static const float PLL_EFC_MID_SCALE = 524288.0f;

/* The loop's first time constant, in s, which pll_init and a restarted
 * recovery set. */
static const float PLL_FIRST_TIME_CONSTANT = 150.0f;

/* Whether a float is zero of either sign, as the image tests its bits
 * (lsl.l #1, fit_three_point+0x12e). */
static bool pll_is_zero(float value) {
  uint32_t bits;

  memcpy(&bits, &value, sizeof bits);
  return bits << 1 == 0;
}

/* An acknowledgment's code, gps_ack_code, as a reply's result: success,
 * and the one pllp_task gives an acknowledgment that never came. */
static const int8_t PLL_GPS_CONTROL_ACK_OK = 0;

static const int8_t PLL_GPS_CONTROL_ACK_TIMED_OUT = 8;

/* GPT port GP bit 5, PGP5: the outer oven's enable (docs/firmware/
 * ovens.md). */
static const uint8_t PLL_STAGES_OUTER_OVEN_ENABLE = 0x20;

/* The event efc_history_update signals the curv task with, and the curv
 * task pllp_task with when its fit is done. */
static const uint32_t PLL_AGING_FIT_EVENT = 0x100;

/* aging_fit_collect's and the routines' results: a fit failed. */
static const int32_t PLL_AGING_FIT_FAILED = 1;

/* The square root of |sum / count|, a float, as the fits take an rms. */
static float pll_aging_root_mean(float sum, float count) {
  return double_to_float(
      sqrt(float_to_double(float_abs(float_divide(sum, count)))));
}

/* Whether either 1PPS, the GPS's or the internal one, is dead, so that
 * no reading comes. */
static bool pll_reading_pps_dead(void) {
  return gps_pps_dead || internal_pps_dead;
}

/* A float with its sign flipped, as the image does with bchg #31. */
static float pll_stages_negate(float value) {
  uint32_t bits;

  memcpy(&bits, &value, sizeof bits);
  bits ^= LIBC_SIGN;
  memcpy(&value, &bits, sizeof value);
  return value;
}

/* fine_sync_adjust's steps for a TI 300 ns or more off, which it meets
 * with one step and fine with another slew. */
static const uint8_t PLL_STAGES_SYNC_TOO_FAR = 4;
