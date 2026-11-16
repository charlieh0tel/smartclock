/* From the 58503B 1.01.04's rewritten/pll.h, copied for the Z3805A 1.01.03:
 * what its copied functions use.  Its comments' addresses are the
 * Z3805A 1.01.03's, but where they name another image. */

/* The EFC history's entries, a ring of 64 indexed modulo its size, as
 * the 58503A's (docs/firmware/58503b-1.01.04.md, "Types", efc_ring). */
#define PLL_AGING_ENTRIES 64

static const uint32_t PLL_AGING_INDEX_MASK = PLL_AGING_ENTRIES - 1;

/* An entry's span, 2700 s of the loop's (measured_uncertainty_compute,
 * 0x41588). */
static const float PLL_EFC_HISTORY_ENTRY_SECONDS = 2700.0f;

/* A block's mean TI past which the loop is lost, 500 ns: startup_pll
 * starts settling again (0x44e4c) and pllp_task takes the normal loop
 * back to it (0x4836e). */
static const float PLL_LOST_TI = 5e-07f;

/* The EFC less its temperature correction, the TC times the oscillator
 * current. */
static float pll_efc_less_temperature(float current) {
  return float_subtract_reversed(tc_coefficient_product(current), pll.efc);
}

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
