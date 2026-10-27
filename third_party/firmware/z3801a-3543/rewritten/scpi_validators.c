/* scpi_validators.c's functions that are this image's own: the rest are
 * common/scpi_validators.c's, the same in every image
 * (docs/firmware/rewrite.md, "Shared C").  Written by hand in place of the
 * decompiler's (docs/firmware/README.md, "Hand-written C"); each keeps its
 * generated prototype. */

/* A double step record's own step, load_double's code again; the
 * descriptors at 0x32524 to 0x325a2 point at it (0x31c3a). */
double load_double_param(scpi_double_step* step, double* value, int32_t index) {
  return step->value;
}
