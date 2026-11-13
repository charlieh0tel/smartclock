/* From the 58503A 3704's rewritten/dac.c, the Z3817A 3752's own beside
 * common/'s: of its functions, those the Z3817A's code carries identical or
 * relocated (docs/firmware/z3817a-3752.md, "The function check"), and
 * efc_write_dithered, this image's own, read beside it.  Its comments'
 * addresses are the Z3817A's, but where they name another image.  Each
 * keeps its generated prototype. */

/* The EFC DAC's strobe, which efc_write writes after the value; the
 * export declares no object for it. */
static volatile uint8_t* const DAC_EFC_STROBE = (volatile uint8_t*)0x306000;

/* The efc_write word's: the value to the EFC's DAC, then its strobe
 * (0x2e67c). */
void efc_write(uint16_t value) {
  qspi_write(QSPI_EFC, &value, 1);
  *DAC_EFC_STROBE = 0;
}

/* Takes a byte of the oscillator's frames (puck_frame_receive), then
 * writes the EFC to its DAC: puck.efc_written rounded, or in a user mode
 * :DIAGnostic:ROSCillator:EFControl:DATA, plus random_dither, clamped to
 * 0 to 0xffff0 (0x33850).  The 58503A 3704's writes pll.efc in every
 * mode. */
void efc_write_dithered(void) {
  /* The EFC's range: 20 bits, its DAC taking the top 16. */
  static const int32_t EFC_MOST = 0xffff0;
  static const int32_t EFC_DAC_SHIFT = 4;
  int32_t efc;

  puck_frame_receive();
  if (diag_efc_mode == EFC_MODE_NORMAL) {
    efc = double_round_to_long(float_to_double(puck.efc_written));
  } else {
    efc = scpi_value_DIAGnostic_ROSCillator_EFControl_DATA;
  }
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
