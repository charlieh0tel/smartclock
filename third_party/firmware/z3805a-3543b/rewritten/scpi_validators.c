/* scpi_validators.c's functions that are this image's own: the rest are
 * common/scpi_validators.c's, the same in every image
 * (docs/firmware/rewrite.md, "Shared C").  Written by hand in place of the
 * decompiler's (docs/firmware/README.md, "Hand-written C"); each keeps its
 * generated prototype. */

/* A double step record's own step, load_double's code again; the
 * descriptors at 0x33720 to 0x3379e point at it (0x32e36). */
double load_double_param(scpi_double_step* step, double* value, int32_t index) {
  return step->value;
}
