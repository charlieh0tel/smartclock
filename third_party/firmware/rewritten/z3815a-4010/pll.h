/* What this image's loop files share (pll_aging.c, pll_stages.c,
 * pll_reading.c, pllc.c, pllp.c): the EFC history's entries, its pointers
 * stepped modulo their count, and an entry's 2700 seconds, the aging
 * fit's time unit (docs/firmware/structures.md, "The loop's state"); the
 * flag at 0x10218b, the EFC less its temperature term, and a stage's
 * result by its hold causes.  The
 * exported C includes the hand-written headers after its own declarations
 * and before the hand-written .c files, so every one of them can use it. */

/* The EFC history's entries, a ring of 32 stepped modulo 32
 * (efc_history_update, 0x4a902), where the others' is 64. */
#define PLL_AGING_ENTRIES 32

static const uint32_t PLL_AGING_INDEX_MASK = PLL_AGING_ENTRIES - 1;

/* An entry's span, 2700 s of the loop's (efc_history_update). */
static const float PLL_EFC_HISTORY_ENTRY_SECONDS = 2700.0f;

/* Which only output_control_reset writes, clearing it: set, outside the n3 lock
 * pll stage, the readings are a tick's wait and valid, the GPS reference
 * good, and the clock neither checked against GPS nor set from it
 * (docs/firmware/z3815a-4010.md, "Not established"). */
static uint8_t* const PLL_CLOCK_FROM_GPS_HELD = (uint8_t*)0x10218b;

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

/* The EFC less its temperature correction, the TC times the oscillator
 * current (efc_history_update, 0x4a63c; n3 lock pll's base). */
static float pll_efc_less_temperature(float current) {
  return float_subtract_reversed(
      float_multiply(scpi_value_DIAGnostic_ROSCillator_TCOefficient, current),
      pll.efc);
}
