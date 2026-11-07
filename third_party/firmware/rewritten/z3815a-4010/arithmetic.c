/* double_to_int64: this image's own, split from common/arithmetic.c
 * where it differs between the images (rewrite.md, "Shared C", rule 6).
 * Written by hand in place of the decompiler's (docs/firmware/README.md,
 * "Hand-written C"); it keeps its generated prototype. */

/* A double as a signed 64-bit number: its magnitude over 2^32 the high
 * long, what remains the low, negated after; nothing calls it.  The
 * 58503A's (0x49de2). */
int64_t double_to_int64(double x) {
  union libc_double value = {.value = x};
  /* double_compare(0, x) is negative when x < 0. */
  const bool negative = double_compare(0.0, value.value) < 0;

  if (negative) {
    value.word.high ^= LIBC_SIGN;
  }
  const uint32_t result_high = double_to_unsigned(
      double_divide_reversed_pointers(&two_to_32, &value.value));

  double_subtract_in_place(
      &value.value,
      double_multiply_pointer(unsigned_to_double(result_high), &two_to_32));
  const uint32_t result_low = double_to_unsigned(value.value);

  if (negative) {
    return int64_negate(result_high, result_low);
  }
  return arithmetic_int64(result_high, result_low);
}
