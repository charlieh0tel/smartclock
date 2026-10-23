/* What the disciplining loop's files share (pll_aging.c,
 * pll_gps_control.c, pll_stages.c, pllp.c; docs/firmware/loop.md): the
 * aging fit's states, the GPS sequences' acknowledgments and states,
 * a stage's result and the power-up stage's sub-states.  The exported
 * C includes the hand-written headers after its own declarations and
 * before the hand-written .c files, so every one of them can use it. */

/* The history's entries, a ring indexed modulo its size. */
#define PLL_AGING_ENTRIES 64

static const uint32_t PLL_AGING_INDEX_MASK = PLL_AGING_ENTRIES - 1;

/* The EFC less its temperature correction, the TC times the oscillator
 * current (0x41410, 0x442fa). */
static float pll_efc_less_temperature(float current) {
  return float_subtract_reversed(
      float_multiply(scpi_value_DIAGnostic_ROSCillator_TCOefficient, current),
      pll.efc);
}

/* Whether a float is zero of either sign, as the image tests its bits
 * (lsl.l #1, 0x42256). */
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

/* The hold causes a missing GPS reference or a dead internal 1PPS add,
 * and a stage's result with any cause: failed. */
static pll_stage_result pll_stages_check_causes(pll_stage_result result) {
  if (gps_reference_good == 0) {
    pll.hold_causes |= HOLD_CAUSE_GPS;
  }
  if (internal_pps_dead != 0) {
    pll.hold_causes |= HOLD_CAUSE_HARDWARE_FAILURE;
  }
  return pll.hold_causes != HOLD_CAUSE_NONE ? PLL_STAGE_RESULT_FAILED : result;
}
