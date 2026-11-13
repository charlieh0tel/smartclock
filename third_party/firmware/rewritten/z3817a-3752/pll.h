/* What this image's loop files share (pll_aging.c, pllc.c, pll_stages.c,
 * pllp.c, and console.c):
 * the EFC history's entries, and a stage's result by its hold causes.
 * The exported C includes the hand-written headers after its own
 * declarations and before the hand-written .c files, so every one of
 * them can use it. */

/* The EFC history's entries, a ring of 32 (pll.efc_history, "Types"),
 * as the Z3815A's, where the 58503A's is 64: linear_regression's bound
 * (0x4748a, 0x4757a). */
#define PLL_AGING_ENTRIES 32

static const uint32_t PLL_AGING_INDEX_MASK = PLL_AGING_ENTRIES - 1;

/* An EFC history entry's span, 2700 s, the aging fit's time unit
 * (holdover's age, 0x48c0e; the steering's slope, 0x499aa). */
static const float PLL_ENTRY_SECONDS = 2700.0f;

/* A TI from which the loop's time constant starts again from
 * initial_loop_time: a block's mean, in the start-up loop and the normal
 * loop, and holdover recovery's last reading (0x4976e, 0x4ced4,
 * 0x49312). */
static const float PLL_LOST_TI = 5e-07f;

/* The hold causes a missing GPS reference or a dead internal 1PPS add,
 * and a stage's result with any cause: failed (holdover_recovery,
 * 0x49288 to 0x492ae; pll_reference_check, 0x4b762 to 0x4b786). */
static pll_stage_result pll_stages_check_causes(pll_stage_result result) {
  if (gps_reference_good == 0) {
    pll.hold_causes |= HOLD_CAUSE_GPS;
  }
  if (internal_pps_dead != 0) {
    pll.hold_causes |= HOLD_CAUSE_HARDWARE_FAILURE;
  }
  return pll.hold_causes != HOLD_CAUSE_NONE ? PLL_STAGE_RESULT_FAILED : result;
}
