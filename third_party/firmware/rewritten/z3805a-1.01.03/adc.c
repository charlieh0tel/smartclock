/* From the 58503B 1.01.04's adc.c, the Z3805A 1.01.03's own beside
 * common/'s: of its functions, those common/ does not hold, each identical
 * or relocated in image_diff's matches (docs/firmware/z3805a-1.01.03.md,
 * "The export and the checks"); their comments' addresses are the Z3805A
 * 1.01.03's.  The 58503B's note follows. */

/* The 58503A 3704's adc.c, copied for the 58503B 1.01.04, adc_read
 * relocated in image_diff's matches (docs/firmware/58503b-1.01.04.md,
 * "The health monitor's channels").  Written by hand in place of the
 * decompiler's (docs/firmware/README.md, "Hand-written C"), which kept
 * the buffers' two pointers apart; each keeps its generated
 * prototype. */

/* Reads an ADC channel through the QSPI: the channel's input is the
 * channel shifted right one, plus 4 for an odd channel (smartclock.h's
 * adc_channel) (0x1f4ea). */
int32_t adc_read(adc_channel channel) {
  /* The ADC's command word, as adc_read builds it, and its reply, the
   * reading in bits 9 to 2. */
  static const uint16_t COMMAND = 0xc000;
  static const uint16_t INPUT_SHIFT = 11;
  static const uint16_t REPLY_SHIFT = 2;
  static const uint8_t CHANNELS = 7;
  static const uint8_t ODD_INPUT = 4;
  uint8_t input = (channel & CHANNELS) >> 1;

  if (channel & 1) {
    input |= ODD_INPUT;
  }
  uint16_t command = (uint16_t)(input << INPUT_SHIFT) | COMMAND;
  uint16_t reply;
  qspi_buffers buffers = {&command, &reply};

  qspi_read(QSPI_ADC, &buffers, 1);
  return reply >> REPLY_SHIFT;
}
