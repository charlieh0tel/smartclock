/* bytes_equal_n, double_to_int64: this image's own, split from
 * common/arithmetic.c where it differs between the images (rewrite.md, "Shared
 * C", rule 6). */

/* Whether two byte strings have the same nonzero length and bytes. */
uint32_t bytes_equal_n(uint8_t* a, int16_t a_length, uint8_t* b,
                       int16_t b_length) {
  if (a_length != b_length || a_length == 0) {
    return 0;
  }
  for (; *a == *b; a++, b++) {
    if (--a_length == 0) {
      return 1;
    }
  }
  return 0;
}
/* A double as a signed 64-bit number: its
 * magnitude over 2^32 the high long, what remains the low, negated after;
 * nothing calls it. */
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
