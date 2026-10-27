/* The functions of pll_stages.c the images share, the same in each
 * (docs/firmware/rewrite.md, "Shared C"); each image's own
 * rewritten/pll_stages.c describes the module and holds the rest.  Written by
 * hand in place of the decompiler's (docs/firmware/README.md,
 * "Hand-written C"); each keeps its generated prototype. */

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

/* For a TI of 100 ns or more, asks for that many 100 ns steps against
 * it, the steps; one and 4 for 300 ns or more; 0 within 100 ns. */
uint8_t fine_sync_adjust(float ti) {
  static const float STEP = 1e-07f;
  static const float MOST = 3e-07f;

  if (float_compare(ti, STEP) < 0) {
    if (float_compare(pll_stages_negate(STEP), ti) < 0) {
      return 0;
    }
    if (float_compare(pll_stages_negate(MOST), ti) < 0) {
      uint32_t steps =
          float_to_unsigned(float_divide(ti, pll_stages_negate(STEP)));

      sync_request(COUNTER_SYNC_NEGATIVE, (uint8_t)steps);
      return steps;
    }
    sync_request(COUNTER_SYNC_NEGATIVE, 1);
    return PLL_STAGES_SYNC_TOO_FAR;
  }
  if (float_compare(ti, MOST) < 0) {
    uint32_t steps = float_to_unsigned(float_divide(ti, STEP));

    sync_request(COUNTER_SYNC_POSITIVE, (uint8_t)steps);
    return steps;
  }
  sync_request(COUNTER_SYNC_POSITIVE, 1);
  return PLL_STAGES_SYNC_TOO_FAR;
}

/* Clears holdover's TI statistics. */
void holdover_ti_reset(void) {
  holdover_steering.ti_sum = 0.0f;
  holdover_steering.ti_weighted_sum = 0.0f;
  holdover_steering.ti_largest = 0.0f;
  holdover_steering.ti_count = 0;
}

/* Adds a TI reading to holdover's statistics: its sum, its sum weighted
 * by index, the count and the largest magnitude. */
void holdover_ti_add(float ti) {
  float_add_in_place(&holdover_steering.ti_sum, ti);
  float_add_in_place(
      &holdover_steering.ti_weighted_sum,
      float_multiply(integer_to_float(holdover_steering.ti_count), ti));
  holdover_steering.ti_count++;
  if (float_compare(float_absolute_on_stack(ti), holdover_steering.ti_largest) >
      0) {
    holdover_steering.ti_largest = float_absolute_on_stack(ti);
  }
}

/* The largest TI magnitude holdover has seen. */
float holdover_ti_max(void) { return holdover_steering.ti_largest; }
