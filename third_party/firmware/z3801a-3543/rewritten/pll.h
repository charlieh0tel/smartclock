/* The PLL's static helpers that read pll_state, laid out differently
 * in each image, so this image's own; common/pll.h has the PLL's
 * shared constants. */

/* The EFC less its temperature correction, the TC times the oscillator
 * current (0x4143c, 0x44326). */
static float pll_efc_less_temperature(float current) {
  return float_subtract_reversed(
      float_multiply(scpi_value_DIAGnostic_ROSCillator_TCOefficient, current),
      pll.efc);
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
