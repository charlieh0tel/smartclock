/* What the disciplining loop's files share (pll_aging.c,
 * pll_gps_control.c, pll_stages.c, pllp.c; docs/firmware/loop.md): the
 * aging fit's states, the GPS sequences' acknowledgments and states,
 * a stage's result and the power-up stage's sub-states.  The exported
 * C includes the hand-written headers after its own declarations and
 * before the hand-written .c files, so every one of them can use it. */

/* The EFC's top, its 20 bits' (efc_clamp, efc_clamp), and its mid-scale,
 * the lifetime record's default. */
static const float PLL_EFC_MOST = 1048560.0f;

static const float PLL_EFC_MID_SCALE = 524288.0f;

/* The loop's first time constant, in s, which pll_init and a restarted
 * recovery set. */
static const float PLL_FIRST_TIME_CONSTANT = 150.0f;

/* The history's entries, a ring indexed modulo its size. */
#define PLL_AGING_ENTRIES 64

static const uint32_t PLL_AGING_INDEX_MASK = PLL_AGING_ENTRIES - 1;

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
