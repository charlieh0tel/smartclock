/* The installer's copies of the small integer and float routines
 * (`platform/arithmetic`; arithmetic.c has the primary's), whose
 * bodies, libc.h's and records.h's, both share.  Written by hand in
 * place of the decompiler's (docs/firmware/README.md, "Hand-written
 * C"); each keeps its generated prototype. */

/* A float's magnitude, its sign bit cleared (installer::0x100efa). */
float installer_float_absolute_on_stack(float f) {
  return libc_float_magnitude(f);
}

/* A double with its sign bit cleared (installer::0x100f0a). */
double installer_double_abs(double value) {
  return libc_double_magnitude(value);
}

/* A long rotated left by a count modulo 32, as rol.l takes it
 * (installer::0x100f1e). */
uint32_t installer_rotate_left(uint32_t value, uint8_t count) {
  return libc_rotate_left(value, count);
}

/* A value of so many bits sign-extended to a long
 * (installer::0x1083ba). */
uint32_t installer_sign_extend_bits(uint32_t value, uint8_t width) {
  return records_sign_extend_bits(value, width);
}
