/* The comparison of two counted byte strings, the same in each image that
 * includes this file (docs/firmware/rewrite.md, "Shared C", "A file for
 * some images").  An image without the routine leaves the file out.
 * Written by hand in place of the decompiler's (docs/firmware/README.md,
 * "Hand-written C"); each keeps its generated prototype. */

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
