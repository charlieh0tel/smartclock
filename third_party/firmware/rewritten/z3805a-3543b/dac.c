/* dac.c's functions that are this image's own: the rest are
 * common/dac.c's, the same in every image (docs/firmware/rewrite.md,
 * "Shared C").  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype. */

/* The efc_write word's: the value to the EFC's DAC, then its strobe
 * (0x1f646). */
void efc_write(uint16_t value) {
  qspi_write(QSPI_EFC, &value, 1);
  efc_strobe = 0;
}

/* Writes pll.efc, rounded, plus random_dither, clamped to 0 to
 * 0xffff0, to the EFC's DAC (0x2421c). */
void efc_write_dithered(void) {
  /* The EFC's range: 20 bits, its DAC taking the top 16. */
  static const int32_t EFC_MOST = 0xffff0;
  static const int32_t EFC_DAC_SHIFT = 4;
  int32_t efc = double_round_to_long(float_to_double(pll.efc));

  if (efc >= 0 && efc <= EFC_MOST) {
    efc += random_dither();
  }
  if (efc < 0) {
    efc = 0;
  } else if (efc > EFC_MOST) {
    efc = EFC_MOST;
  }
  efc_write(efc >> EFC_DAC_SHIFT);
}
