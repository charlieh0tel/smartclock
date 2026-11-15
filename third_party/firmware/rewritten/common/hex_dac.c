/* The hex DAC's routines that the images with one share, the same in each
 * image that includes this file (docs/firmware/rewrite.md, "Shared C", "A
 * file for some images"): the console's words that write it, the step of
 * the interpolator's low reference's DAC and the 36-bit shift its QSPI
 * self-test reads its answer with.  An image without the hex DAC leaves
 * the file out.  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype. */

void diag_hdac_write(uint32_t value, uint32_t channel) {
  hdac_write(value, channel);
}

void diag_hdac_all(uint32_t value_1, uint32_t value_2, uint32_t value_3,
                   uint32_t value_4, uint32_t value_5, uint32_t value_6) {
  hdac_write_all(value_1, value_2, value_3, value_4, value_5, value_6);
}

/* Moves the low reference's DAC a step against the spread's error,
 * unless it is at either end of its range; returns the DAC. */
uint8_t hdac_nudge(int32_t error, uint8_t hdac) {
  if (hdac == 0 || hdac >= TI_HDAC_MAXIMUM || error == 0) {
    return hdac;
  }
  hdac += error < 0 ? 1 : -1;
  hdac_set(hdac, TI_HDAC_SLOT_LOW);
  return hdac;
}

/* Shifts three 12-bit words, the first most significant, right one bit
 * as a 36-bit number; returns the bit carried from the first word into
 * the second. */
uint16_t shift_right_36(uint16_t* words) {
  static const uint16_t WORD_MASK = 0xfff;
  static const uint16_t WORD_TOP_BIT = 0x800;

  words[2] &= WORD_MASK;
  words[1] &= WORD_MASK;
  words[0] &= WORD_MASK;
  words[2] >>= 1;
  if (words[1] & 1) {
    words[2] |= WORD_TOP_BIT;
  }
  words[1] >>= 1;
  uint16_t carry = words[0] & 1;
  if (carry) {
    words[1] |= WORD_TOP_BIT;
  }
  words[0] >>= 1;
  return carry;
}
