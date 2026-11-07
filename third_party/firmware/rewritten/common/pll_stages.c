/* The functions of pll_stages.c the images share, the same in each
 * (docs/firmware/rewrite.md, "Shared C"); each image's own
 * rewritten/pll_stages.c describes the module and holds the rest.  Written by
 * hand in place of the decompiler's (docs/firmware/README.md,
 * "Hand-written C"); each keeps its generated prototype. */

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
