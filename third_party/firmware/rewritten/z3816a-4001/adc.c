/* From the 58503A 3704's rewritten/adc.c, the Z3816A 4001's own beside
 * common/'s: of its functions, those the Z3816A's code carries (identical or
 * relocated by image_diff, or changed with the same instructions), with the
 * static helpers they use, each the same here but where its comment says
 * otherwise (docs/firmware/z3816a-4001.md, "The link check").  Its comments'
 * addresses are the Z3816A's.  Written by hand in place of the decompiler's
 * (docs/firmware/README.md, "Hand-written C"); each keeps its generated
 * prototype. */

/* Reads an ADC channel through the QSPI: the channel's input is the
 * channel shifted right one, plus 4 for an odd channel (smartclock.h's
 * adc_channel) (0x2e3b0). */
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
