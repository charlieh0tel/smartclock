/* The Z3817A 3752's ADC read, read beside the Z3815A 4010's
 * rewritten/adc.c, which sends one word where this one sends two.  Written
 * by hand in place of the decompiler's (docs/firmware/README.md,
 * "Hand-written C"); it keeps its generated prototype. */

/* Reads an ADC channel through the QSPI, a word of 0 and then the
 * command, the channel's input the channel shifted right one, plus 4 for
 * an odd channel; the second reply's reading, in its bits 9 to 2
 * (0x2e614). */
int32_t adc_read(adc_channel channel) {
  /* The ADC's command word, and its reply, the reading in bits 9 to 2. */
  static const uint16_t COMMAND = 0xc000;
  static const uint16_t INPUT_SHIFT = 11;
  static const uint16_t REPLY_SHIFT = 2;
  static const uint8_t CHANNELS = 7;
  static const uint8_t ODD_INPUT = 4;
  uint8_t input = (channel & CHANNELS) >> 1;

  if (channel & 1) {
    input |= ODD_INPUT;
  }
  uint16_t sent[2] = {0, (uint16_t)(input << INPUT_SHIFT) | COMMAND};
  uint16_t received[2];
  qspi_buffers buffers = {sent, received};

  qspi_read(QSPI_ADC, &buffers, 2);
  return received[1] >> REPLY_SHIFT;
}
