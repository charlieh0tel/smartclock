/* The console's word phase_off, the loop's time interval setpoint, the
 * same in each image that includes this file (docs/firmware/rewrite.md,
 * "Shared C", "A file for some images").  An image without the word leaves
 * the file out.  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype. */

void diag_phase_off(int32_t setpoint) {
  ti_setpoint = integer_to_float(setpoint);
}
