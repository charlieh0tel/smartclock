/* dac.c's functions that are this image's own: the rest are
 * common/dac.c's, the same in every image (docs/firmware/rewrite.md,
 * "Shared C").  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype. */

/* The DUART output port's OP0, which efc_write pulses, set, clear and
 * set again, after the value, where the 58503A's writes a strobe at
 * 0x306000 (0x2fb9c to 0x2fbb6). */
static const uint8_t DAC_EFC_STROBE_BIT = 0x01;

/* The value to the EFC's DAC, then OP0 pulsed (0x2fb82). */
void efc_write(uint16_t value) {
  qspi_write(QSPI_EFC, &value, 1);
  duart_output_bit_set(DAC_EFC_STROBE_BIT, 1);
  duart_output_bit_set(DAC_EFC_STROBE_BIT, 0);
  duart_output_bit_set(DAC_EFC_STROBE_BIT, 1);
}

/* Writes puck.efc_written, pll.efc as the port E link corrects it,
 * rounded, plus random_dither, clamped to 0 to 0xffff0, to the EFC's DAC
 * (0x363a6). */
void efc_write_dithered(void) {
  /* The EFC's range: 20 bits, its DAC taking the top 16. */
  static const int32_t EFC_MOST = 0xffff0;
  static const int32_t EFC_DAC_SHIFT = 4;
  int32_t efc = double_round_to_long(float_to_double(puck.efc_written));

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
