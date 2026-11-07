/* The functions of monitors.c the images share, the same in each
 * (docs/firmware/rewrite.md, "Shared C"); each image's own
 * rewritten/monitors.c describes the module and holds the rest.  Written by
 * hand in place of the decompiler's (docs/firmware/README.md,
 * "Hand-written C"); each keeps its generated prototype. */

/* Clears efc_clamp's and hmon_check_efc's state. */
void efc_clamp_reset(void) {
  efc_range.clamped = 0;
  efc_range.near_end = 0;
  efc_range.near_end_checks = 0;
}
