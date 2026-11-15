/* The Z3817A 3752's arithmetic.c, copied for the 58503B 1.01.04, each of its
 * functions identical or relocated in image_diff's matches
 * (docs/firmware/58503b-1.01.04.md, "The export and the checks"); its
 * comments' addresses are the 58503B's.  The Z3817A's own: from
 * common/arithmetic.c, the Z3817A 3752's own beside common/'s: of its
 * functions, those whose decompiled C the function check finds differs from
 * the image, each the same in both images (docs/firmware/z3817a-3752.md, "The
 * function check"), with the static helpers and tables they use.  Each keeps
 * its generated prototype. random_dither too, common/'s, relocated here: the
 * decompiler's reading reads dither_table's upper half by its offset from
 * powers_of_ten_float, which gcc's copy lays elsewhere, so that hmon_task's
 * EFC writes differed (docs/firmware/58503b-1.01.04.md, "The function
 * check").  With them, from other images' rewritten C, of its functions,
 * those the 58503B's code carries identical or relocated, with the static
 * helpers they use (docs/firmware/58503b-1.01.04.md, "The link list"); their
 * comments' addresses are the 58503B's, but where they name another image. */

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
